"""Pluto TV channels, through the same public flow as Pluto TV's own web player (docs/PROVIDERS.md).

Pluto TV is free, ad-supported TV; its web player needs no account. Like that player, the server:

1. "Boots" an anonymous session (boot.pluto.tv, with a random client id for this server run).
   Pluto answers with the country it detects, its servers, a session token and the parameters
   its stream stitcher expects. The session is reused until Pluto asks for a new one.
2. Reads the channel guide for that country and finds the channel by its slug
   ("dragon-ball-es"): what Pluto offers here, nothing else.
3. Asks Pluto's stitcher for the channel's HLS manifest, with the ads Pluto inserts.

The teletext's guide comes from the same session: the channel's timeline, like the web
player's programme grid.

Only https hosts under pluto.tv are accepted. Pluto encrypts every segment with plain HLS
AES-128 and gives the key to any session, as its web player needs it: the user decided on
2026-09-30 to allow that one method for Pluto only (to be removed if it causes trouble).
Any other encryption (SAMPLE-AES, a DRM system) is still refused. Nothing here needs an
account or gets around one."""

from __future__ import annotations

import json
import logging
import threading
import time
import uuid
from collections.abc import Callable
from datetime import UTC, datetime
from urllib.parse import urlencode, urlparse

from ..http_fetch import Fetch, FetchError, fetch_text
from .base import Airing, GuideCache, Health, HealthCache, HlsSource, Provider, SourceError
from .hls import resolve_hls

log = logging.getLogger("PLUTO")

BOOT = "https://boot.pluto.tv/v4/start"
GUIDE_PATH = "/v2/guide/channels?channelIds=&offset=0&limit=1000&sort=number%3Aasc"
TIMELINES_PATH = "/v2/guide/timelines"
TIMELINE_MINUTES = 240
OFFICIAL_HOST_SUFFIX = ".pluto.tv"
DEFAULT_REFRESH_S = 3600
FetchWithHeaders = Callable[[str, dict], str]


def _fetch_with_headers(url: str, headers: dict) -> str:
    return fetch_text(url, headers=headers)


def is_official(url: str) -> bool:
    parsed = urlparse(url)
    return parsed.scheme == "https" and (parsed.hostname or "").endswith(OFFICIAL_HOST_SUFFIX)


def timelines_url(channels_server: str, channel_id: str, start_ms: int) -> str:
    start = datetime.fromtimestamp(start_ms / 1000, UTC).strftime("%Y-%m-%dT%H:%M:%S.000Z")
    query = {"start": start, "channelIds": channel_id, "duration": TIMELINE_MINUTES}
    return f"{channels_server}{TIMELINES_PATH}?{urlencode(query)}"


def airing_title(item: dict) -> str:
    """"One Piece - El Capitan Usuff...": the show, then the episode when the title lacks it."""
    title = str(item.get("title") or "")
    episode = item.get("episode") if isinstance(item.get("episode"), dict) else {}
    name = str(episode.get("name") or "")
    return f"{title} - {name}" if name and name not in title else title


def parse_timelines(answer: object) -> list[Airing]:
    data = answer.get("data") if isinstance(answer, dict) else None
    items = data[0].get("timelines") if isinstance(data, list) and data and isinstance(data[0], dict) else None
    airings = []
    for item in items if isinstance(items, list) else []:
        try:
            start = datetime.fromisoformat(item["start"])
            end = datetime.fromisoformat(item["stop"])
        except (KeyError, TypeError, ValueError):
            continue
        title = airing_title(item)
        if title and start.tzinfo and end > start:
            airings.append(Airing(title, int(start.timestamp() * 1000), int(end.timestamp() * 1000)))
    return airings


def boot_url(client_id: str) -> str:
    query = {"appName": "web", "appVersion": "9.0.0", "deviceVersion": "128.0.0", "deviceModel": "web",
             "deviceMake": "chrome", "deviceType": "web", "clientID": client_id, "clientModelNumber": "1.0.0",
             "serverSideAds": "false", "drmCapabilities": "", "blockingMode": ""}
    return f"{BOOT}?{urlencode(query)}"


class PlutoSession:
    """One anonymous Pluto TV session for the whole server, shared by every Pluto channel."""

    def __init__(self, fetch: FetchWithHeaders = _fetch_with_headers, clock: Callable[[], float] = time.monotonic,
                 client_id: str | None = None) -> None:
        self._fetch = fetch
        self._clock = clock
        self._client_id = client_id or str(uuid.uuid4())
        self._lock = threading.Lock()  # providers resolve from worker threads
        self._boot: dict | None = None
        self._guide: list[dict] | None = None
        self._expires = 0.0

    def current(self) -> tuple[dict, list[dict]]:
        """The boot answer and the channel guide, fetched again once Pluto's refresh time is up."""
        with self._lock:
            if self._boot is None or self._clock() >= self._expires:
                self._boot, self._guide = self._start()
            return self._boot, self._guide or []

    def forget(self) -> None:
        with self._lock:
            self._boot = None

    def timelines(self, channel_id: str, start_ms: int) -> list[Airing]:
        boot, _ = self.current()
        url = timelines_url(boot["servers"]["channels"], channel_id, start_ms)
        try:
            answer = json.loads(self._fetch(url, {"Authorization": f"Bearer {boot['sessionToken']}"}))
        except FetchError as e:
            raise SourceError(f"Pluto TV timeline unavailable: {e}") from e
        except ValueError as e:
            raise SourceError("Pluto TV timeline is not JSON") from e
        return parse_timelines(answer)

    def _start(self) -> tuple[dict, list[dict]]:
        try:
            boot = json.loads(self._fetch(boot_url(self._client_id), {}))
        except FetchError as e:
            raise SourceError(f"Pluto TV unavailable: {e}") from e
        except ValueError as e:
            raise SourceError("Pluto TV answered something that is not JSON") from e
        token = boot.get("sessionToken") if isinstance(boot, dict) else None
        servers = boot.get("servers") if isinstance(boot, dict) else None
        if not token or not isinstance(servers, dict) or not boot.get("stitcherParams"):
            raise SourceError("Pluto TV answered without a session")
        channels = str(servers.get("channels", ""))
        if not is_official(channels) or not is_official(str(servers.get("stitcher", ""))):
            raise SourceError("Pluto TV pointed outside its own hosts: refused")
        try:
            guide = json.loads(self._fetch(channels + GUIDE_PATH, {"Authorization": f"Bearer {token}"}))
        except FetchError as e:
            raise SourceError(f"Pluto TV channel guide unavailable: {e}") from e
        except ValueError as e:
            raise SourceError("Pluto TV channel guide is not JSON") from e
        items = guide.get("data") if isinstance(guide, dict) else None
        if not isinstance(items, list):
            raise SourceError("Pluto TV channel guide is empty")
        region = boot.get("session", {}).get("activeRegion") if isinstance(boot.get("session"), dict) else None
        log.info("session for region %s: %d channels", region, len(items))
        self._expires = self._clock() + float(boot.get("refreshInSec") or DEFAULT_REFRESH_S)
        return boot, items


SHARED = PlutoSession()


class PlutoProvider(Provider):
    name = "pluto"

    def __init__(self, slug: str, session: PlutoSession = SHARED, fetch: Fetch = fetch_text,
                 clock: Callable[[], float] = time.monotonic) -> None:
        if not slug or not slug.replace("-", "").isalnum():
            raise ValueError(f"bad Pluto TV channel slug: {slug!r}")
        self.slug = slug
        self._session = session
        self._fetch = fetch
        self._name: str | None = None
        self._health = HealthCache(self._check, clock=clock)
        self._guide = GuideCache(lambda now_ms: self._session.timelines(self._channel()["id"], now_ms),
                                 f"pluto {slug}", clock=clock)

    def _check(self) -> Health:
        try:
            self.resolve(0)
        except SourceError as e:
            return Health(False, str(e))
        return Health(True)

    def health(self) -> Health:
        return self._health.get()

    def metadata(self) -> dict:
        return {"provider": self.name, "source": "hls", "channel": self.slug, "now_playing": self._name}

    def guide(self, now_ms: int) -> list[Airing]:
        return self._guide.get(now_ms)

    def _channel(self) -> dict:
        _, guide = self._session.current()
        channel = next((c for c in guide if isinstance(c, dict) and c.get("slug") == self.slug), None)
        if channel is None or not channel.get("id"):
            raise SourceError("Pluto TV does not offer this channel here now")
        return channel

    def resolve(self, now_ms: int) -> HlsSource:
        log.info("resolving %s", self.slug)
        try:
            source = self._resolve()
        except SourceError as e:
            self._health.set(Health(False, str(e)))
            log.warning("%s: %s", self.slug, e)
            raise
        self._health.set(Health(True))
        return source

    def _resolve(self) -> HlsSource:
        channel = self._channel()
        boot, _ = self._session.current()
        self._name = channel.get("name")
        manifest = (f'{boot["servers"]["stitcher"]}/v2/stitch/hls/channel/{channel["id"]}/master.m3u8'
                    f'?{boot["stitcherParams"]}&jwt={boot["sessionToken"]}&masterJWTPassthrough=true')
        log.info("manifest resolved: %s", urlparse(manifest).hostname)  # never the token
        try:
            # The user's exception (2026-09-30): Pluto encrypts every segment with plain HLS
            # AES-128 and hands the key to any session; nothing else encrypted is played.
            return resolve_hls(manifest, self._fetch, allow_aes128=True)
        except SourceError:
            self._session.forget()  # an expired or refused session: a fresh one next time
            raise
