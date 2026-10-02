"""A live HLS source given in channels.json. Only URLs from the configuration are ever
fetched: nothing in the API takes a URL (no open proxy, no SSRF)."""

from __future__ import annotations

import logging
import time
from collections.abc import Callable
from urllib.parse import urljoin, urlparse

from ..hls_playlist import PlaylistError, choose, is_master, parse_master, parse_media
from ..http_fetch import Fetch, FetchError, fetch_text
from .base import Health, HealthCache, HlsSource, Provider, SourceError

log = logging.getLogger("HLS")

MAX_PLAUSIBLE_LATENCY_MS = 10 * 60 * 1000


def check_source_url(url: str) -> str:
    """http(s) with a host, or ValueError: a configuration mistake, not a runtime failure."""
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        raise ValueError(f"not an http(s) URL: {url!r}")
    return url


def resolve_hls(url: str, fetch: Fetch, audio_language: str | None = None, pdt_offset_is_utc: bool = False,
                now: Callable[[], float] = time.time, allow_aes128: bool = False) -> HlsSource:
    """Reads the playlist, picks variant and audio, refuses encryption (DRM is unsupported).

    allow_aes128: the one exception, for providers the user allowed it for (Pluto TV, decided
    2026-09-30): plain HLS AES-128, whose key the official player fetches like any segment. Any
    other method (SAMPLE-AES, a DRM system) is refused even then."""
    try:
        text = fetch(url)
        if is_master(text):
            master = parse_master(text)
            choice = choose(master, audio_language)
            media = parse_media(fetch(urljoin(url, choice.media_uri)), pdt_offset_is_utc)
            methods = set(master.key_methods) | set(media.key_methods)
            if choice.audio_uri:
                methods |= parse_media(fetch(urljoin(url, choice.audio_uri))).key_methods
            video_map, audio_map = choice.video_map, choice.audio_map
        else:  # a bare media playlist: its only video and audio
            media = parse_media(text, pdt_offset_is_utc)
            methods = set(media.key_methods)
            video_map, audio_map = "0:v:0", "0:a:0?"
    except FetchError as e:
        raise SourceError(f"manifest unavailable: {e}") from e
    except PlaylistError as e:
        raise SourceError(str(e)) from e
    if methods and not (allow_aes128 and methods == {"AES-128"}):
        raise SourceError("encrypted (DRM): unsupported")
    latency = None
    if media.last_segment_pdt is not None:
        age = int(now() * 1000 - media.last_segment_pdt.timestamp() * 1000)
        latency = age if 0 <= age <= MAX_PLAUSIBLE_LATENCY_MS else None  # a wrong clock says nothing
    return HlsSource(url, video_map, audio_map, latency)


class HlsProvider(Provider):
    name = "hls"

    def __init__(self, url: str, audio_language: str | None = None, fetch: Fetch = fetch_text,
                 clock: Callable[[], float] = time.monotonic) -> None:
        self.url = check_source_url(url)
        self.audio_language = audio_language
        self._fetch = fetch
        self._health = HealthCache(self._check, clock=clock)

    def _check(self) -> Health:
        try:
            self.resolve(0)
        except SourceError as e:
            return Health(False, str(e))
        return Health(True)

    def health(self) -> Health:
        return self._health.get()

    def metadata(self) -> dict:
        return {"provider": self.name, "source": "hls", "host": urlparse(self.url).hostname}

    def resolve(self, now_ms: int) -> HlsSource:
        try:
            source = resolve_hls(self.url, self._fetch, self.audio_language)
        except SourceError as e:
            self._health.set(Health(False, str(e)))
            log.warning("%s: %s", urlparse(self.url).hostname, e)
            raise
        self._health.set(Health(True))
        return source
