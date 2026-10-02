"""HLS and 3Cat providers with canned answers: no network in the test suite."""

from __future__ import annotations

import json

import pytest

from app.http_fetch import FetchError
from app.providers.base import Airing, HlsSource, SourceError
from app.providers.hls import HlsProvider
from app.providers.threecat import ThreeCatProvider, media_api_url, parse_guide, pick_manifest
from test_hls_playlist import MEDIA, THREECAT_MASTER

LIVE = "https://directes-tv-cat.3catdirectes.cat/live-content/super3-hls/master.m3u8"


def site(pages: dict[str, str]):
    """A fake fetch: canned pages by URL; anything else is a 404."""
    calls = []

    def fetch(url: str) -> str:
        calls.append(url)
        if url in pages:
            return pages[url]
        raise FetchError("HTTP 404")

    fetch.calls = calls
    return fetch


def threecat_site(media: list[dict] | dict | None = None) -> dict[str, str]:
    base = LIVE.rsplit("/", 1)[0]
    answer = {"informacio": {"id": "sx3", "arafem": {"titol": "Objectiu, camaleó",
                                                    "titol_complet": "Els germans Kratt - Objectiu, camaleó"}},
              "media": media if media is not None else [
                  {"geo": "CATALUNYA", "format": "HLS", "url": LIVE},
                  {"geo": "ESPANYA", "format": "HLS", "url": LIVE.replace("-cat.", "-es.")}]}
    return {media_api_url("sx3"): json.dumps(answer), LIVE: THREECAT_MASTER,
            f"{base}/bitrate_1.m3u8": MEDIA, f"{base}/bitrate_7.m3u8": MEDIA}


# --- HlsProvider --------------------------------------------------------------------------

def test_hls_valid_config_resolves() -> None:
    base = "https://example.org/live"
    fetch = site({f"{base}/master.m3u8": THREECAT_MASTER, f"{base}/bitrate_1.m3u8": MEDIA,
                  f"{base}/bitrate_7.m3u8": MEDIA})
    provider = HlsProvider(f"{base}/master.m3u8", audio_language="ca", fetch=fetch)
    source = provider.resolve(0)
    assert isinstance(source, HlsSource)
    assert source.url == f"{base}/master.m3u8"
    assert source.video_map == "0:p:0:v:0" and source.audio_map == "0:p:0:a:m:language:ca"
    assert provider.health().online
    assert provider.metadata() == {"provider": "hls", "source": "hls", "host": "example.org"}


@pytest.mark.parametrize("url", ["ftp://example.org/x.m3u8", "file:///etc/passwd", "example.org/x.m3u8", ""])
def test_hls_invalid_url_is_a_configuration_error(url: str) -> None:
    with pytest.raises(ValueError):
        HlsProvider(url, fetch=site({}))


def test_hls_provider_failure_is_offline_with_a_reason() -> None:
    provider = HlsProvider("https://example.org/gone.m3u8", fetch=site({}))
    health = provider.health()
    assert not health.online and health.detail == "manifest unavailable: HTTP 404"
    with pytest.raises(SourceError):
        provider.resolve(0)


def test_hls_encrypted_source_is_unsupported() -> None:
    base = "https://example.org/drm"
    encrypted = MEDIA.replace("#EXTINF:6,\nseg1", '#EXT-X-KEY:METHOD=SAMPLE-AES,URI="skd://x"\n#EXTINF:6,\nseg1')
    fetch = site({f"{base}/master.m3u8": THREECAT_MASTER, f"{base}/bitrate_1.m3u8": encrypted,
                  f"{base}/bitrate_7.m3u8": MEDIA})
    health = HlsProvider(f"{base}/master.m3u8", fetch=fetch).health()
    assert not health.online and "DRM" in health.detail


def test_health_is_cached() -> None:
    base = "https://example.org/live"
    fetch = site({f"{base}/master.m3u8": THREECAT_MASTER, f"{base}/bitrate_1.m3u8": MEDIA,
                  f"{base}/bitrate_7.m3u8": MEDIA})
    provider = HlsProvider(f"{base}/master.m3u8", fetch=fetch, clock=lambda: 100.0)
    provider.health()
    calls = len(fetch.calls)
    provider.health()
    assert len(fetch.calls) == calls  # the second check within the TTL does not fetch


# --- ThreeCatProvider ---------------------------------------------------------------------

def test_threecat_resolves_the_official_manifest_with_catalan_audio() -> None:
    fetch = site(threecat_site())
    provider = ThreeCatProvider("sx3", fetch=fetch)
    source = provider.resolve(0)
    assert source.url == LIVE
    assert source.audio_map == "0:p:0:a:m:language:ca"
    assert fetch.calls[0] == media_api_url("sx3")
    assert provider.metadata()["now_playing"] == "Els germans Kratt - Objectiu, camaleó"


def test_threecat_international_feed_is_the_last_choice() -> None:
    tots = LIVE.replace("-cat.", "-int.")
    base = tots.rsplit("/", 1)[0]
    pages = threecat_site([{"geo": "TOTS", "format": "HLS", "url": tots}])  # like 3/24
    pages.update({tots: THREECAT_MASTER, f"{base}/bitrate_1.m3u8": MEDIA, f"{base}/bitrate_7.m3u8": MEDIA})
    assert ThreeCatProvider("sx3", fetch=site(pages)).resolve(0).url == tots  # default regions
    both = [{"geo": "TOTS", "format": "HLS", "url": tots}, {"geo": "ESPANYA", "format": "HLS", "url": LIVE}]
    assert pick_manifest({"media": both}, ("CATALUNYA", "ESPANYA", "TOTS")) == LIVE


def test_threecat_region_order() -> None:
    media = [{"geo": "CATALUNYA", "format": "HLS", "url": LIVE},
             {"geo": "ESPANYA", "format": "HLS", "url": LIVE.replace("-cat.", "-es.")}]
    assert pick_manifest({"media": media}, ("ESPANYA", "CATALUNYA")).startswith("https://directes-tv-es.")
    assert pick_manifest({"media": media[0]}, ("CATALUNYA",)) == LIVE  # a single entry as an object


@pytest.mark.parametrize("media, reason", [
    ([], "no stream"),
    ([{"geo": "CATALUNYA", "format": "DASH", "url": LIVE}], "no HLS"),
    ([{"geo": "CATALUNYA", "format": "HLS", "url": "https://iptv.example.com/sx3.m3u8"}], "no official"),
    ([{"geo": "CATALUNYA", "format": "HLS", "url": LIVE.replace("https", "http")}], "no official"),
])
def test_threecat_refuses_what_is_not_official_hls(media, reason: str) -> None:
    health = ThreeCatProvider("sx3", fetch=site(threecat_site(media))).health()
    assert not health.online and reason in health.detail


def test_threecat_api_down_or_garbage() -> None:
    assert "media API unavailable" in ThreeCatProvider("sx3", fetch=site({})).health().detail
    garbage = site({media_api_url("sx3"): "<html>maintenance</html>"})
    assert "not JSON" in ThreeCatProvider("sx3", fetch=garbage).health().detail


def test_threecat_bad_code() -> None:
    with pytest.raises(ValueError):
        ThreeCatProvider("sx3/../../x")


def test_threecat_guide_is_now_and_next() -> None:
    def item(title: str, start: str, end: str) -> dict:
        return {"titol": title.split(" - ")[1], "titol_complet": title, "data_emissio": {"utc": start},
                "data_caducitat": {"utc": end}}
    answer = {"informacio": {"arafem": item("Bola de drac Z - Uns quarts", "2026-09-30T14:11:13+02:00",
                                            "2026-09-30T14:33:31+02:00"),
                             "despresfem": item("Bola de drac Z - El combat", "2026-09-30T14:33:31+02:00",
                                                "2026-09-30T14:55:48+02:00")}}
    assert parse_guide(answer) == [Airing("Bola de drac Z - Uns quarts", 1790770273000, 1790771611000),
                                   Airing("Bola de drac Z - El combat", 1790771611000, 1790772948000)]
    # The 24h FAST channels answer without titles or times; garbage is no guide either.
    assert parse_guide({"informacio": {"arafem": {"durada": {"milisegons": 3600000}}, "despresfem": {}}}) == []
    assert parse_guide({"informacio": {"arafem": item("A - B", "yesterday", "today")}}) == []


def test_threecat_guide_is_cached_and_never_raises() -> None:
    calls = []

    def down(url: str) -> str:
        calls.append(url)
        raise FetchError("HTTP 503")
    clock = [0.0]
    provider = ThreeCatProvider("sx3", fetch=down, clock=lambda: clock[0])
    assert provider.guide(0) == [] and provider.guide(0) == []
    assert len(calls) == 1  # once per TTL, even when it fails
    clock[0] = 61.0
    provider.guide(0)
    assert len(calls) == 2
