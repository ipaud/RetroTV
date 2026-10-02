"""3Cat (CCMA) live channels, from their official sources only (docs/PROVIDERS.md).

The manifest is never stored: each tune asks 3Cat's own media API, the one its web player
uses, which answers with the channel's official HLS manifests per region:

    https://api-media.3cat.cat/pvideo/media.jsp?media=video&versio=vast&idint=<code>&...
      -> {"media": [{"geo": "CATALUNYA", "format": "HLS", "url": "https://directes-tv-cat.3catdirectes.cat/..."},
                    {"geo": "ESPANYA", ...}], "informacio": {"arafem": {"titol": ...}}}

Only manifests on 3Cat's own hosts are accepted, never encrypted ones (DRM is unsupported),
and nothing tries to get around a region: if 3Cat does not serve this network, the channel
is offline. The audio track is Catalan when the channel has one."""

from __future__ import annotations

import json
import logging
import time
from collections.abc import Callable
from datetime import datetime
from urllib.parse import urlencode, urlparse

from ..http_fetch import Fetch, FetchError, fetch_text
from .base import Airing, GuideCache, Health, HealthCache, HlsSource, Provider, SourceError
from .hls import resolve_hls

log = logging.getLogger("3CAT")

MEDIA_API = "https://api-media.3cat.cat/pvideo/media.jsp"
OFFICIAL_HOST_SUFFIXES = (".3catdirectes.cat", ".3cat.cat", ".ccma.cat")
DEFAULT_REGIONS = ("CATALUNYA", "ESPANYA", "TOTS")  # TOTS: 3Cat's own feed for everyone (3/24 has only this)


def media_api_url(code: str) -> str:
    query = {"media": "video", "versio": "vast", "idint": code, "profile": "pc_3cat", "broadcast": "false",
             "format": "dm"}
    return f"{MEDIA_API}?{urlencode(query)}"


def parse_guide(answer: dict) -> list[Airing]:
    """What is on now ("arafem") and next ("despresfem"), when 3Cat names them: its 24h FAST
    channels answer without titles."""
    info = answer.get("informacio") if isinstance(answer.get("informacio"), dict) else {}
    airings = []
    for key in ("arafem", "despresfem"):
        item = info.get(key)
        if not isinstance(item, dict):
            continue
        title = item.get("titol_complet") or item.get("titol")
        try:
            start = datetime.fromisoformat(item["data_emissio"]["utc"])
            end = datetime.fromisoformat(item["data_caducitat"]["utc"])
        except (KeyError, TypeError, ValueError):
            continue
        if title and start.tzinfo and end > start:
            airings.append(Airing(str(title), int(start.timestamp() * 1000), int(end.timestamp() * 1000)))
    return airings


def is_official(url: str) -> bool:
    parsed = urlparse(url)
    host = parsed.hostname or ""
    return parsed.scheme == "https" and any(host.endswith(s) for s in OFFICIAL_HOST_SUFFIXES)


def pick_manifest(answer: dict, regions: tuple[str, ...]) -> str:
    """The HLS manifest for the first region 3Cat offers, from an official host."""
    media = answer.get("media")
    if isinstance(media, dict):  # a single entry comes as an object
        media = [media]
    if not isinstance(media, list) or not media:
        raise SourceError("3Cat offers no stream for this channel now")
    hls = [m for m in media if isinstance(m, dict) and str(m.get("format", "")).upper() == "HLS"]
    if not hls:
        raise SourceError("3Cat offers no HLS stream for this channel (DRM or another format): unsupported")
    for region in regions:
        for m in hls:
            if str(m.get("geo", "")).upper() == region and is_official(str(m.get("url", ""))):
                return str(m["url"])
    raise SourceError(f"no official HLS manifest for {', '.join(regions)}")


class ThreeCatProvider(Provider):
    name = "3cat"

    def __init__(self, code: str, regions: tuple[str, ...] = DEFAULT_REGIONS, fetch: Fetch = fetch_text,
                 clock: Callable[[], float] = time.monotonic) -> None:
        if not code or not code.replace("_", "").isalnum():
            raise ValueError(f"bad 3Cat channel code: {code!r}")
        self.code = code
        self.regions = tuple(r.upper() for r in regions)
        self._fetch = fetch
        self._now_playing: str | None = None
        self._health = HealthCache(self._check, clock=clock)
        self._guide = GuideCache(lambda _: parse_guide(self._media_answer()), f"3cat {code}", clock=clock)

    def _check(self) -> Health:
        try:
            self.resolve(0)
        except SourceError as e:
            return Health(False, str(e))
        return Health(True)

    def health(self) -> Health:
        return self._health.get()

    def metadata(self) -> dict:
        return {"provider": self.name, "source": "hls", "channel": self.code, "now_playing": self._now_playing}

    def guide(self, now_ms: int) -> list[Airing]:
        return self._guide.get(now_ms)

    def resolve(self, now_ms: int) -> HlsSource:
        log.info("resolving %s", self.code)
        try:
            source = self._resolve()
        except SourceError as e:
            self._health.set(Health(False, str(e)))
            log.warning("%s: %s", self.code, e)
            raise
        self._health.set(Health(True))
        return source

    def _media_answer(self) -> dict:
        try:
            answer = json.loads(self._fetch(media_api_url(self.code)))
        except FetchError as e:
            raise SourceError(f"3Cat media API unavailable: {e}") from e
        except ValueError as e:
            raise SourceError("3Cat media API answered something that is not JSON") from e
        if not isinstance(answer, dict):
            raise SourceError("3Cat media API answered something unexpected")
        return answer

    def _resolve(self) -> HlsSource:
        answer = self._media_answer()
        info = answer.get("informacio") if isinstance(answer.get("informacio"), dict) else {}
        now_playing = info.get("arafem") if isinstance(info.get("arafem"), dict) else {}
        self._now_playing = now_playing.get("titol_complet") or now_playing.get("titol")
        manifest = pick_manifest(answer, self.regions)
        log.info("manifest resolved: %s", urlparse(manifest).hostname)
        # 3Cat stamps EXT-X-PROGRAM-DATE-TIME in UTC but labels it +02:00 (checked 2026-09-29).
        return resolve_hls(manifest, self._fetch, audio_language="ca", pdt_offset_is_utc=True)
