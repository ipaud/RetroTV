"""HLS playlists, only what RETROTV Server needs from them: which variant and which audio to
hand to FFmpeg, whether the stream is encrypted (DRM: unsupported) and how old the live edge
is. Pure: tested without network."""

from __future__ import annotations

import re
from dataclasses import dataclass
from datetime import datetime, timezone

MIN_HEIGHT = 240  # the TV shows 320x240: the smallest variant at least this tall is enough


class PlaylistError(Exception):
    """The source cannot be used; the message is the reason the API reports."""


@dataclass(frozen=True)
class Variant:
    index: int  # order in the master playlist = FFmpeg's program id for it
    bandwidth: int
    height: int  # 0 when the playlist does not say
    audio_group: str | None
    has_audio: bool
    has_video: bool
    uri: str


@dataclass(frozen=True)
class Rendition:
    group: str
    language: str | None
    default: bool
    uri: str | None


@dataclass(frozen=True)
class MasterPlaylist:
    variants: list[Variant]
    audio: list[Rendition]
    session_key: bool
    key_methods: frozenset[str] = frozenset()  # EXT-X-SESSION-KEY methods other than NONE


@dataclass(frozen=True)
class MediaPlaylist:
    encrypted: bool
    last_segment_pdt: datetime | None  # EXT-X-PROGRAM-DATE-TIME of the newest segment
    segments: int
    key_methods: frozenset[str] = frozenset()  # EXT-X-KEY methods other than NONE


@dataclass(frozen=True)
class Choice:
    """What FFmpeg maps from the master playlist (stream specifiers of one input)."""

    video_map: str
    audio_map: str | None
    media_uri: str  # the chosen variant's playlist, relative to the master
    audio_uri: str | None  # the chosen audio rendition's playlist, if separate


_ATTRIBUTE = re.compile(r'([A-Z0-9-]+)=("[^"]*"|[^,]*)')


def parse_attributes(text: str) -> dict[str, str]:
    """KEY=VALUE,KEY="quoted, with commas" -> {KEY: value}."""
    return {k: v.strip('"') for k, v in _ATTRIBUTE.findall(text)}


def _lines(text: str) -> list[str]:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if not lines or lines[0] != "#EXTM3U":
        raise PlaylistError("not an HLS playlist")
    return lines


def is_master(text: str) -> bool:
    return "#EXT-X-STREAM-INF" in text


def parse_master(text: str) -> MasterPlaylist:
    lines = _lines(text)
    variants: list[Variant] = []
    audio: list[Rendition] = []
    session_key = False
    methods: set[str] = set()
    for i, line in enumerate(lines):
        if line.startswith("#EXT-X-SESSION-KEY"):
            method = parse_attributes(line.split(":", 1)[1]).get("METHOD", "NONE")
            if method != "NONE":
                session_key = True
                methods.add(method)
        elif line.startswith("#EXT-X-MEDIA:"):
            a = parse_attributes(line.split(":", 1)[1])
            if a.get("TYPE") == "AUDIO":
                audio.append(Rendition(a.get("GROUP-ID", ""), a.get("LANGUAGE"), a.get("DEFAULT") == "YES", a.get("URI")))
        elif line.startswith("#EXT-X-STREAM-INF:") and i + 1 < len(lines) and not lines[i + 1].startswith("#"):
            a = parse_attributes(line.split(":", 1)[1])
            height = int(a["RESOLUTION"].split("x")[1]) if "x" in a.get("RESOLUTION", "") else 0
            codecs = a.get("CODECS", "")
            has_video = height > 0 or any(c in codecs for c in ("avc1", "avc3", "hvc1", "hev1", "av01", "vp09"))
            # No CODECS at all (Pluto TV): assume the usual muxed video + audio.
            variants.append(Variant(len(variants), int(a.get("BANDWIDTH", "0")), height, a.get("AUDIO"),
                                    "mp4a" in codecs or "AUDIO" in a or not codecs, has_video or not codecs,
                                    lines[i + 1]))
    if not variants:
        raise PlaylistError("master playlist without variants")
    return MasterPlaylist(variants, audio, session_key, frozenset(methods))


def _parse_pdt(value: str, offset_is_utc: bool) -> datetime | None:
    try:
        stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    if offset_is_utc or stamp.tzinfo is None:  # a provider that labels UTC with a local offset
        stamp = stamp.replace(tzinfo=timezone.utc)
    return stamp


def parse_media(text: str, pdt_offset_is_utc: bool = False) -> MediaPlaylist:
    lines = _lines(text)
    encrypted = False
    methods: set[str] = set()
    pdt: datetime | None = None
    segments = 0
    for line in lines:
        if line.startswith("#EXT-X-KEY:"):
            method = parse_attributes(line.split(":", 1)[1]).get("METHOD", "NONE")
            if method != "NONE":
                encrypted = True
                methods.add(method)
        elif line.startswith("#EXT-X-PROGRAM-DATE-TIME:"):
            pdt = _parse_pdt(line.split(":", 1)[1], pdt_offset_is_utc)
        elif line.startswith("#EXTINF"):
            segments += 1
    return MediaPlaylist(encrypted, pdt, segments, frozenset(methods))


def _language_matches(have: str | None, want: str) -> bool:
    if not have:
        return False
    have, want = have.lower(), want.lower()
    return have == want or have.split("-")[0] == want or (len(want) == 2 and have.startswith(want))


def choose(master: MasterPlaylist, audio_language: str | None) -> Choice:
    """The smallest variant at least MIN_HEIGHT tall (the TV shows 320x240: less to download
    and decode), and its audio in audio_language when there is one, else the default."""
    with_video = [v for v in master.variants if v.has_video]
    if not with_video:
        raise PlaylistError("no video in this stream")
    tall_enough = [v for v in with_video if v.height >= MIN_HEIGHT]
    known = [v for v in with_video if v.height > 0]
    if tall_enough:
        variant = min(tall_enough, key=lambda v: (v.height, v.bandwidth))
    elif known:
        variant = max(known, key=lambda v: v.height)
    else:
        variant = min(with_video, key=lambda v: v.bandwidth)
    program = f"0:p:{variant.index}"

    group = [r for r in master.audio if r.group == variant.audio_group] if variant.audio_group else []
    if group:
        wanted = [r for r in group if audio_language and _language_matches(r.language, audio_language)]
        pick = (wanted or [r for r in group if r.default] or group)[0]
        audio_map = f"{program}:a:m:language:{pick.language}" if pick.language else f"{program}:a:0"
        return Choice(f"{program}:v:0", audio_map, variant.uri, pick.uri)
    return Choice(f"{program}:v:0", f"{program}:a:0" if variant.has_audio else None, variant.uri, None)
