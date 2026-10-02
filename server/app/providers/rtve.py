"""RTVE live channels, from their official sources only (docs/PROVIDERS.md).

Each tune asks RTVE the two things its own web player uses, and nothing is stored:

1. The channel's official live page (rtve.es/play/.../canales-lineales/<slug>/): its player
   configuration says, per stream, whether RTVE protects it with DRM ("hasDRM") and whether it
   requires signing in ("requireLogged"). Either one: the channel is offline, with the reason.
   Nothing here gets around DRM or a login. A page that no longer describes the stream also
   leaves the channel offline (fail closed).
2. RTVE's stream locator, ztnr.rtve.es/ztnr/<asset>.m3u8, which redirects to the HLS manifest
   on RTVE's own CDN. Only https hosts under rtve.es are accepted.

Checked 2026-09-30: 24h is the only linear channel RTVE offers without DRM and without
signing in. La 1, La 2 and Teledeporte have DRM; Clan requires signing in."""

from __future__ import annotations

import html
import json
import logging
import re
import time
from collections.abc import Callable
from urllib.parse import urlparse

from ..http_fetch import Fetch, FetchError, fetch_redirect, fetch_text
from .base import Health, HealthCache, HlsSource, Provider, SourceError
from .hls import resolve_hls

log = logging.getLogger("RTVE")

LIVE_PAGE = "https://www.rtve.es/play/videos/directo/canales-lineales/{slug}/"
LOCATOR = "https://ztnr.rtve.es/ztnr/{asset}.m3u8"
OFFICIAL_HOST_SUFFIX = ".rtve.es"
# provider_channel -> (live page slug, RTVE asset id). Whether each one can be played is decided
# by RTVE's own flags at every tune, not here.
CHANNELS = {
    "24h": ("24h", "1694255"),
    "clan": ("clan", "5466990"),
    "la1": ("la-1", "1688877"),
    "la2": ("la-2", "1688885"),
    "tdp": ("teledeporte", "1712295"),
}
_SETUP = re.compile(r'data-setup="([^"]*)"')


def is_official(url: str) -> bool:
    parsed = urlparse(url)
    host = parsed.hostname or ""
    return parsed.scheme == "https" and host.endswith(OFFICIAL_HOST_SUFFIX)


def _truthy(value: object) -> bool:
    return value is True or str(value).lower() == "true"


def stream_flags(page: str, asset: str) -> dict:
    """The official player configuration for this asset's live stream, from the page."""
    for match in _SETUP.finditer(page):
        try:
            setup = json.loads(html.unescape(match.group(1)))
        except ValueError:
            continue
        if isinstance(setup, dict) and str(setup.get("idAsset")) == asset and _truthy(setup.get("isLive")):
            return setup
    raise SourceError("RTVE's live page no longer describes this channel: offline until checked")


def check_public(setup: dict) -> None:
    if _truthy(setup.get("hasDRM")):
        raise SourceError("RTVE protects this channel with DRM: unsupported")
    if _truthy(setup.get("requireLogged")) or "requireLogged" not in setup:
        raise SourceError("RTVE requires signing in for this channel: unsupported")


class RtveProvider(Provider):
    name = "rtve"

    def __init__(self, channel: str, fetch: Fetch = fetch_text, locate: Callable[[str], str] = fetch_redirect,
                 clock: Callable[[], float] = time.monotonic) -> None:
        if channel not in CHANNELS:
            raise ValueError(f"unknown RTVE channel {channel!r}: use one of {', '.join(CHANNELS)}")
        self.channel = channel
        self.slug, self.asset = CHANNELS[channel]
        self._fetch = fetch
        self._locate = locate
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
        return {"provider": self.name, "source": "hls", "channel": self.channel}

    def resolve(self, now_ms: int) -> HlsSource:
        log.info("resolving %s", self.channel)
        try:
            source = self._resolve()
        except SourceError as e:
            self._health.set(Health(False, str(e)))
            log.warning("%s: %s", self.channel, e)
            raise
        self._health.set(Health(True))
        return source

    def _resolve(self) -> HlsSource:
        try:
            page = self._fetch(LIVE_PAGE.format(slug=self.slug))
        except FetchError as e:
            raise SourceError(f"RTVE live page unavailable: {e}") from e
        check_public(stream_flags(page, self.asset))
        try:
            manifest = self._locate(LOCATOR.format(asset=self.asset))
        except FetchError as e:
            raise SourceError(f"RTVE stream locator unavailable: {e}") from e
        if not is_official(manifest):
            raise SourceError("RTVE pointed outside its own hosts: refused")
        log.info("manifest resolved: %s", urlparse(manifest).hostname)
        return resolve_hls(manifest, self._fetch, audio_language="es")
