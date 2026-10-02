"""Pluto TV provider with canned answers: no network in the test suite."""

from __future__ import annotations

import json

import pytest

from app.http_fetch import FetchError
from app.providers.base import Airing, SourceError
from app.providers.hls import resolve_hls
from app.providers.pluto import GUIDE_PATH, PlutoProvider, PlutoSession, boot_url, timelines_url
from test_hls_playlist import PLUTO_MASTER
from test_providers import site

CHANNELS = "https://service-channels.clusters.pluto.tv"
STITCHER = "https://cfd-v4-service-channel-stitcher-use1-1.prd.pluto.tv"
CHANNEL_ID = "685a996c7c4f2ad40cb4f276"
MANIFEST = f"{STITCHER}/v2/stitch/hls/channel/{CHANNEL_ID}/master.m3u8?appName=web&country=ES&jwt=TOKEN&masterJWTPassthrough=true"
MEDIA_URL = f"{STITCHER}/v2/stitch/hls/channel/{CHANNEL_ID}/640930/playlist.m3u8?terminate=false"


def media(method: str = "AES-128") -> str:
    key = f'#EXT-X-KEY:METHOD={method},URI="https://service-stitcher-ipv4.clusters.pluto.tv/key"\n' if method else ""
    return f"#EXTM3U\n#EXT-X-TARGETDURATION:6\n{key}#EXTINF:6.0,\nseg1.ts\n#EXTINF:6.0,\nseg2.ts\n"


def boot(stitcher: str = STITCHER, channels: str = CHANNELS) -> dict:
    return {"sessionToken": "TOKEN", "stitcherParams": "appName=web&country=ES", "refreshInSec": 28800,
            "servers": {"channels": channels, "stitcher": stitcher}, "session": {"activeRegion": "ES"}}


NOW_MS = 1790770620000  # 2026-09-30T12:17:00Z
TIMELINES = {"data": [{"channelId": CHANNEL_ID, "timelines": [
    {"start": "2026-09-30T11:47:00.000Z", "stop": "2026-09-30T12:13:09.000Z", "title": "One Piece",
     "episode": {"name": "El enfrentamiento", "series": {"name": "One Piece"}}},
    {"start": "2026-09-30T12:13:09.000Z", "stop": "2026-09-30T12:39:17.000Z", "title": "One Piece",
     "episode": {"name": "El Capitan Usuff"}},
    {"start": "2026-09-30T12:39:17.000Z", "stop": "2026-09-30T13:05:00.000Z",
     "title": "Erase una vez... El espacio: La ciudad voladora", "episode": {"name": "La ciudad voladora"}},
    {"start": "bad", "stop": "2026-09-30T13:05:00.000Z", "title": "Broken"}]}]}


def pluto(method: str = "AES-128", boot_answer: dict | None = None, guide: list | None = None, clock=None):
    answers = {boot_url("client"): json.dumps(boot_answer or boot()),
               CHANNELS + GUIDE_PATH: json.dumps({"data": guide if guide is not None else [
                   {"id": CHANNEL_ID, "slug": "dragon-ball-es", "name": "Dragon Ball"}]}),
               timelines_url(CHANNELS, CHANNEL_ID, NOW_MS): json.dumps(TIMELINES)}
    calls = []

    def fetch_with_headers(url: str, headers: dict) -> str:
        calls.append((url, headers))
        if url in answers:
            return answers[url]
        raise FetchError("HTTP 404")

    session = PlutoSession(fetch_with_headers, clock=clock or (lambda: 0.0), client_id="client")
    playlists = site({MANIFEST: PLUTO_MASTER, MEDIA_URL: media(method)})
    provider = PlutoProvider("dragon-ball-es", session=session, fetch=playlists)
    provider.calls = calls
    return provider


def test_channel_resolves_through_the_anonymous_session() -> None:
    provider = pluto()
    source = provider.resolve(0)
    assert source.url == MANIFEST
    assert source.video_map == "0:p:1:v:0" and source.audio_map == "0:p:1:a:0"
    assert provider.calls[1][1] == {"Authorization": "Bearer TOKEN"}  # the guide, with the session token
    assert provider.metadata()["now_playing"] == "Dragon Ball"


def test_the_session_is_reused_until_pluto_asks_for_a_new_one() -> None:
    now = [0.0]
    provider = pluto(clock=lambda: now[0])
    provider.resolve(0)
    provider.resolve(0)
    assert len(provider.calls) == 2  # one boot, one guide
    now[0] = 28800
    provider.resolve(0)
    assert len(provider.calls) == 4


@pytest.mark.parametrize("method", ["SAMPLE-AES", "SAMPLE-AES-CTR"])
def test_only_plain_aes128_is_allowed(method: str) -> None:
    health = pluto(method).health()
    assert not health.online and "encrypted" in health.detail


def test_aes128_stays_refused_for_everyone_else() -> None:
    fetch = site({MANIFEST: PLUTO_MASTER, MEDIA_URL: media("AES-128")})
    with pytest.raises(SourceError, match="encrypted"):
        resolve_hls(MANIFEST, fetch)
    assert resolve_hls(MANIFEST, fetch, allow_aes128=True).url == MANIFEST


def test_channel_not_offered_here() -> None:
    health = pluto(guide=[{"id": "x", "slug": "heidi", "name": "Heidi"}]).health()
    assert not health.online and "does not offer this channel" in health.detail


@pytest.mark.parametrize("answer", [boot(stitcher="https://stitcher.example.com"), boot(channels="http://service-channels.clusters.pluto.tv")])
def test_hosts_outside_pluto_are_refused(answer: dict) -> None:
    health = pluto(boot_answer=answer).health()
    assert not health.online and "outside its own hosts" in health.detail


def test_boot_without_a_session() -> None:
    health = pluto(boot_answer={"servers": {}}).health()
    assert not health.online and "without a session" in health.detail


def test_bad_slug_is_a_configuration_error() -> None:
    with pytest.raises(ValueError):
        PlutoProvider("../../etc")


def test_guide_comes_from_the_channel_timeline() -> None:
    provider = pluto()
    assert provider.guide(NOW_MS) == [
        Airing("One Piece - El enfrentamiento", 1790768820000, 1790770389000),
        Airing("One Piece - El Capitan Usuff", 1790770389000, 1790771957000),
        Airing("Erase una vez... El espacio: La ciudad voladora", 1790771957000, 1790773500000)]
    assert provider.calls[-1] == (timelines_url(CHANNELS, CHANNEL_ID, NOW_MS), {"Authorization": "Bearer TOKEN"})
    assert "start=2026-09-30T12%3A17%3A00.000Z" in provider.calls[-1][0]


def test_guide_of_a_channel_not_offered_is_empty() -> None:
    assert pluto(guide=[{"id": "x", "slug": "heidi", "name": "Heidi"}]).guide(NOW_MS) == []
