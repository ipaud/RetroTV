"""RTVE provider with canned answers: no network in the test suite."""

from __future__ import annotations

import html
import json

import pytest

from app.http_fetch import FetchError
from app.providers.rtve import CHANNELS, LIVE_PAGE, LOCATOR, RtveProvider
from test_hls_playlist import MEDIA
from test_providers import site

CDN = "https://rtvelivestream.rtve.es/rtvesec/24h"
MASTER = f"{CDN}/24h_main_dvr.m3u8?idasset=1694255"
MASTER_TEXT = f"""#EXTM3U
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audios",NAME="Castellano",LANGUAGE="spa",CHANNELS="2",DEFAULT=YES,AUTOSELECT=YES
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audios",NAME="Original",LANGUAGE="qaa",CHANNELS="2",DEFAULT=NO,URI="{CDN}/24h_main_dvr_a_14.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=3012608,RESOLUTION=1280x720,CODECS="avc1.640029,mp4a.40.2",AUDIO="audios"
{CDN}/24h_main_dvr_720.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=1155072,RESOLUTION=640x360,CODECS="avc1.640029,mp4a.40.2",AUDIO="audios"
{CDN}/24h_main_dvr_360.m3u8
"""


def page(asset: str, has_drm: object = False, require_logged: object = False, live: object = "true") -> str:
    """A live page with the official player configuration for one stream (and a decoy)."""
    decoy = {"idAsset": "999", "isLive": "true", "hasDRM": False, "requireLogged": False}
    setup = {"id": "4", "idAsset": asset, "isLive": live, "hasDRM": has_drm, "requireLogged": require_logged}
    return "".join(f'<div data-setup="{html.escape(json.dumps(s))}"></div>' for s in (decoy, setup))


def rtve(channel: str = "24h", pages: dict | None = None, target: str = MASTER):
    slug, asset = CHANNELS[channel]
    base = {LIVE_PAGE.format(slug=slug): page(asset), MASTER: MASTER_TEXT, f"{CDN}/24h_main_dvr_360.m3u8": MEDIA}
    base.update(pages or {})
    located = []

    def locate(url: str) -> str:
        located.append(url)
        if isinstance(target, Exception):
            raise target
        return target

    provider = RtveProvider(channel, fetch=site(base), locate=locate)
    provider.located = located
    return provider


def test_24h_resolves_the_official_manifest() -> None:
    provider = rtve()
    source = provider.resolve(0)
    assert source.url == MASTER
    assert provider.located == [LOCATOR.format(asset="1694255")]
    assert source.video_map == "0:p:1:v:0"  # 360p: the smallest at least 240 lines
    assert source.audio_map == "0:p:1:a:m:language:spa"  # Spanish, the default track


@pytest.mark.parametrize("drm, logged, reason", [
    (True, False, "DRM"), ("true", False, "DRM"),
    (False, True, "signing in"), (False, "true", "signing in"),
])
def test_drm_or_login_leaves_the_channel_offline(drm, logged, reason: str) -> None:
    pages = {LIVE_PAGE.format(slug="24h"): page("1694255", drm, logged)}
    provider = rtve(pages=pages)
    health = provider.health()
    assert not health.online and reason in health.detail
    assert provider.located == []  # the stream is never even asked for


def test_page_that_no_longer_describes_the_stream_fails_closed() -> None:
    for text in ("<html>new design</html>", page("1694255", live="false"), page("123")):
        health = rtve(pages={LIVE_PAGE.format(slug="24h"): text}).health()
        assert not health.online and "no longer describes" in health.detail


def test_missing_login_flag_counts_as_login_required() -> None:
    setup = {"idAsset": "1694255", "isLive": True, "hasDRM": False}
    text = f'<div data-setup="{html.escape(json.dumps(setup))}"></div>'
    assert "signing in" in rtve(pages={LIVE_PAGE.format(slug="24h"): text}).health().detail


@pytest.mark.parametrize("target", ["https://iptv.example.com/24h.m3u8", "http://rtvelivestream.rtve.es/x.m3u8",
                                    "https://rtve.es.example.com/x.m3u8"])
def test_redirect_outside_rtve_is_refused(target: str) -> None:
    health = rtve(target=target).health()
    assert not health.online and "outside its own hosts" in health.detail


def test_page_or_locator_down() -> None:
    assert "live page unavailable" in RtveProvider("24h", fetch=site({}), locate=lambda u: MASTER).health().detail
    assert "locator unavailable" in rtve(target=FetchError("HTTP 500")).health().detail


def test_unknown_channel_is_a_configuration_error() -> None:
    with pytest.raises(ValueError):
        RtveProvider("la7")
