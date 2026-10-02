"""HLS playlist parsing and choices, on playlists shaped like the real ones (3Cat, Akamai)."""

from __future__ import annotations

from datetime import datetime, timezone

import pytest

from app.hls_playlist import PlaylistError, choose, is_master, parse_attributes, parse_master, parse_media

THREECAT_MASTER = """#EXTM3U
#EXT-X-INDEPENDENT-SEGMENTS
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=1728000,CODECS="avc1.4d001f,mp4a.40.2",RESOLUTION=854x480,AUDIO="audio",SUBTITLES="subs",FRAME-RATE=25.000
bitrate_1.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=2128000,CODECS="avc1.4d001f,mp4a.40.2",RESOLUTION=1280x720,AUDIO="audio",SUBTITLES="subs",FRAME-RATE=25.000
bitrate_2.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=4128000,CODECS="avc1.4d0029,mp4a.40.2",RESOLUTION=1920x1080,AUDIO="audio",SUBTITLES="subs",FRAME-RATE=25.000
bitrate_3.m3u8
#EXT-X-MEDIA:TYPE=AUDIO,CHANNELS="2",GROUP-ID="audio",NAME="Català",DEFAULT=YES,AUTOSELECT=YES,LANGUAGE="ca",URI="bitrate_7.m3u8"
#EXT-X-MEDIA:TYPE=AUDIO,CHANNELS="2",GROUP-ID="audio",NAME="Versió Original",DEFAULT=NO,AUTOSELECT=YES,LANGUAGE="qaa",URI="bitrate_8.m3u8"
"""

MUXED_MASTER = """#EXTM3U
#EXT-X-STREAM-INF:BANDWIDTH=12865600,CODECS="avc1.4d4028,mp4a.40.5",RESOLUTION=1920x1080
hi.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=900000,CODECS="avc1.4d401e,mp4a.40.5",RESOLUTION=640x360
mid.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=300000,CODECS="avc1.42000d,mp4a.40.5",RESOLUTION=320x180
low.m3u8
"""

MEDIA = """#EXTM3U
#EXT-X-TARGETDURATION:6
#EXT-X-MEDIA-SEQUENCE:298452822
#EXT-X-PROGRAM-DATE-TIME:2026-09-29T21:22:00.179+0200
#EXTINF:6,
seg1.ts
#EXT-X-PROGRAM-DATE-TIME:2026-09-29T21:22:06.179+0200
#EXTINF:6,
seg2.ts
"""


def test_attributes_with_quoted_commas() -> None:
    a = parse_attributes('BANDWIDTH=1728000,CODECS="avc1.4d001f,mp4a.40.2",RESOLUTION=854x480')
    assert a == {"BANDWIDTH": "1728000", "CODECS": "avc1.4d001f,mp4a.40.2", "RESOLUTION": "854x480"}


def test_threecat_master_takes_480p_and_catalan() -> None:
    master = parse_master(THREECAT_MASTER)
    assert [v.height for v in master.variants] == [480, 720, 1080]
    choice = choose(master, "ca")
    assert choice.video_map == "0:p:0:v:0" and choice.media_uri == "bitrate_1.m3u8"
    assert choice.audio_map == "0:p:0:a:m:language:ca" and choice.audio_uri == "bitrate_7.m3u8"
    assert choose(master, None).audio_map == "0:p:0:a:m:language:ca"  # the default rendition
    assert choose(master, "qaa").audio_map == "0:p:0:a:m:language:qaa"


def test_muxed_audio_takes_the_smallest_variant_at_least_240_lines() -> None:
    choice = choose(parse_master(MUXED_MASTER), "ca")
    assert choice.video_map == "0:p:1:v:0" and choice.media_uri == "mid.m3u8"
    assert choice.audio_map == "0:p:1:a:0" and choice.audio_uri is None


def test_small_only_takes_the_largest() -> None:
    master = parse_master(MUXED_MASTER.replace("1920x1080", "300x160").replace("640x360", "200x120"))
    assert choose(master, None).media_uri == "low.m3u8"  # 320x180 is the tallest


def test_audio_only_variants_are_never_chosen() -> None:
    master = parse_master("""#EXTM3U
#EXT-X-STREAM-INF:BANDWIDTH=75000,CODECS="mp4a.40.2",AUDIO="aud"
audio_only.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=658000,CODECS="avc1.4d401f,mp4a.40.2",RESOLUTION=1280x720,AUDIO="aud"
video.m3u8
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",LANGUAGE="en",DEFAULT=YES,URI="a.m3u8"
""")
    assert choose(master, "ca").media_uri == "video.m3u8"
    only_audio = parse_master('#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1,CODECS="mp4a.40.2"\na.m3u8\n')
    with pytest.raises(PlaylistError, match="no video"):
        choose(only_audio, None)


PLUTO_MASTER = """#EXTM3U
#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",DEFAULT=NO,FORCED=NO,URI="subtitle/en/playlist.m3u8"
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=1042180,SUBTITLES="subs"
1042180/playlist.m3u8?terminate=false
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=640930,SUBTITLES="subs"
640930/playlist.m3u8?terminate=false
"""


def test_variants_without_codecs_carry_muxed_audio() -> None:
    choice = choose(parse_master(PLUTO_MASTER), None)
    assert choice.media_uri.startswith("640930/")  # no resolution given: the lightest
    assert choice.video_map == "0:p:1:v:0" and choice.audio_map == "0:p:1:a:0"


def test_media_playlist() -> None:
    media = parse_media(MEDIA)
    assert not media.encrypted and media.segments == 2
    assert media.last_segment_pdt == datetime(2026, 9, 29, 19, 22, 6, 179000, tzinfo=timezone.utc)
    utc = parse_media(MEDIA, pdt_offset_is_utc=True).last_segment_pdt  # 3Cat's labelling
    assert utc == datetime(2026, 9, 29, 21, 22, 6, 179000, tzinfo=timezone.utc)


def test_encryption_is_detected() -> None:
    assert parse_media(MEDIA.replace("#EXTINF:6,\nseg1", '#EXT-X-KEY:METHOD=SAMPLE-AES,URI="skd://k"\n#EXTINF:6,\nseg1')).encrypted
    assert not parse_media(MEDIA.replace("#EXTINF:6,\nseg1", "#EXT-X-KEY:METHOD=NONE\n#EXTINF:6,\nseg1")).encrypted
    master = parse_master(THREECAT_MASTER.replace("#EXT-X-INDEPENDENT-SEGMENTS",
                                                  '#EXT-X-SESSION-KEY:METHOD=SAMPLE-AES,KEYFORMAT="com.apple.streamingkeydelivery"'))
    assert master.session_key


def test_not_a_playlist() -> None:
    with pytest.raises(PlaylistError):
        parse_master("<html>404</html>")
    with pytest.raises(PlaylistError):
        parse_master("#EXTM3U\n#EXT-X-VERSION:3\n")
    assert is_master(THREECAT_MASTER) and not is_master(MEDIA)
