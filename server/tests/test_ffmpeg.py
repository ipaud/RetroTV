"""FfmpegSession: real FFmpeg on a local HLS source for the output, a fake one for failures."""

from __future__ import annotations

import asyncio
import functools
import http.server
import os
import shutil
import subprocess
import sys
import threading
from pathlib import Path

import pytest

from app.ffmpeg import (DEFAULT_PROFILE, LIVE_PROFILES, FfmpegError, FfmpegSession, FfmpegSettings, build_command,
                        classify)
from app.providers.base import HlsSource

FAKE = str(Path(__file__).with_name("fake_ffmpeg.py"))
HAVE_FFMPEG = shutil.which("ffmpeg") is not None


def fake(mode: str, **kw) -> FfmpegSettings:
    os.environ["FAKE_FFMPEG"] = mode
    return FfmpegSettings(binary=FAKE, **{"startup_timeout_s": 2.0, "stop_timeout_s": 1.0, **kw})


def alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


def reaped(pid: int) -> bool:
    """True once the process is gone and waited for: no zombie left."""
    try:
        os.waitpid(pid, os.WNOHANG)
    except ChildProcessError:
        return not alive(pid)
    return False


SOURCE = HlsSource("http://127.0.0.1:9/x.m3u8", "0:p:0:v:0", "0:p:0:a:m:language:ca")


# --- pure ---------------------------------------------------------------------------------

def test_command_has_one_input_and_two_outputs() -> None:
    cmd = build_command(SOURCE, 7, 9, FfmpegSettings())
    assert cmd.count("-i") == 1 and cmd[cmd.index("-i") + 1] == SOURCE.url
    assert cmd[-1] == "pipe:9" and "pipe:7" in cmd
    video = cmd[cmd.index("pipe:7") - 12:cmd.index("pipe:7")]
    assert "0:p:0:v:0" in video and "mjpeg" in video and "yuvj420p" in video
    assert cmd[cmd.index("-q:v") + 1] == "14"  # live default (20fps-q14): measured on the TV
    assert build_command(SOURCE, 7, 9, FfmpegSettings(video_quality=8))[cmd.index("-q:v") + 1] == "8"
    vf = cmd[cmd.index("-vf") + 1]
    assert "fps=20:start_time=0" in vf and "pad=320:240" in vf and "force_original_aspect_ratio=decrease" in vf
    assert "crop=trunc(iw/16)*16:trunc(ih/16)*16" in vf and "trunc((240-ih)/32)*16" in vf  # bars on JPEG blocks
    audio = cmd[cmd.index("pipe:7") + 1:]
    assert audio[:2] == ["-map", "0:p:0:a:m:language:ca"]
    assert "adts" in audio and "32k" in audio and "44100" in audio and audio[audio.index("-ac") + 1] == "1"
    assert "loudnorm" not in " ".join(cmd)
    assert "loudnorm" in " ".join(build_command(SOURCE, 7, 9, FfmpegSettings(loudnorm=True)))


def test_profiles_set_frame_rate_and_quality() -> None:
    assert LIVE_PROFILES[DEFAULT_PROFILE] == (20, 14)
    assert FfmpegSettings().profile == DEFAULT_PROFILE
    for name, (fps, quality) in LIVE_PROFILES.items():
        settings = FfmpegSettings(fps=fps, video_quality=quality)
        assert settings.profile == name
        cmd = build_command(SOURCE, 7, 9, settings)
        assert f"fps={fps}:start_time=0" in cmd[cmd.index("-vf") + 1] and cmd[cmd.index("-q:v") + 1] == str(quality)


def test_crop_4_3_keeps_the_middle_of_a_16_9_frame() -> None:
    plain = build_command(SOURCE, 7, 9, FfmpegSettings())
    assert "crop='trunc(ih*4/3/2)*2':ih" not in plain[plain.index("-vf") + 1]
    cmd = build_command(SOURCE, 7, 9, FfmpegSettings(crop_4_3=True))
    vf = cmd[cmd.index("-vf") + 1]
    assert vf.index("crop='trunc(ih*4/3/2)*2':ih") < vf.index("fps=")  # before anything is scaled down


def test_live_start_index_reaches_the_command() -> None:
    cmd = build_command(SOURCE, 7, 9, FfmpegSettings())
    assert cmd[cmd.index("-live_start_index") + 1] == "-3"  # a segment in hand: no gaps at the live edge
    cmd = build_command(SOURCE, 7, 9, FfmpegSettings(live_start_index=-1))
    assert cmd[cmd.index("-live_start_index") + 1] == "-1"


def test_command_without_audio() -> None:
    cmd = build_command(HlsSource("https://x/y.m3u8", "0:v:0", None), 7, None, FfmpegSettings())
    assert cmd[-1] == "pipe:7" and "adts" not in cmd


def test_classify() -> None:
    assert classify(["HTTP error 403 Forbidden"], 1) == "source refused (HTTP 403)"
    assert classify(["Server returned 404 Not Found"], 1) == "manifest unavailable (HTTP 404)"
    assert classify(["Connection refused"], 1) == "source unreachable"
    assert classify([], 1).startswith("ffmpeg exited (1)")
    assert classify(["whatever"], 0) == "source ended (the playlist was closed)"
    assert classify(["source stalled"], -15) == "source stalled (no new data)"


# --- failures, with the fake ----------------------------------------------------------------

def test_process_failure_before_ready() -> None:
    session = FfmpegSession(SOURCE, fake("fail404"), "t")
    with pytest.raises(FfmpegError, match="HTTP 404"):
        asyncio.run(session.start())
    assert session.state == "stopped" and reaped(session.pid)


def test_startup_timeout_kills_the_process() -> None:
    session = FfmpegSession(SOURCE, fake("silent", startup_timeout_s=0.5), "t")
    with pytest.raises(FfmpegError, match="no output within"):
        asyncio.run(session.start())
    assert reaped(session.pid)


def test_stubborn_process_is_killed_and_double_stop_is_safe() -> None:
    async def run() -> int:
        session = FfmpegSession(SOURCE, fake("stubborn", startup_timeout_s=0.3), "t")
        await session._launch()
        pid = session.pid
        await session.aclose()  # SIGTERM ignored: SIGKILL after stop_timeout_s
        await session.aclose()  # again: nothing happens
        session.close()
        return pid

    pid = asyncio.run(run())
    assert reaped(pid)


def test_source_drop_restarts_then_gives_up() -> None:
    async def run() -> tuple[FfmpegSession, bytes]:
        session = FfmpegSession(SOURCE, fake("drop", max_restarts=2, restart_backoff_s=(0.1,)), "t")
        await session.start()
        received = b""
        async for chunk in session.read("video"):  # one JPEG per run, then the end
            received += chunk
        await session.aclose()
        return session, received

    session, received = asyncio.run(run())
    assert session.restarts == 2 and session.state == "failed"
    assert received.count(b"\xff\xd8\xff\xe0") == 3  # first run + two restarts
    assert "gave up after 2 restarts" in session.failure


def test_stalled_source_is_restarted_then_given_up() -> None:
    async def run() -> FfmpegSession:
        settings = fake("stall", max_restarts=1, restart_backoff_s=(0.1,), stall_timeout_s=0.5)
        session = FfmpegSession(SOURCE, settings, "t")
        await session.start()
        async for _ in session.read("video"):
            pass  # ends when the session gives up
        await session.aclose()
        return session

    session = asyncio.run(run())
    assert session.restarts == 1 and session.state == "failed"
    assert "source stalled" in session.failure


def test_reader_too_far_behind_ends_the_session() -> None:
    async def run() -> FfmpegSession:
        settings = fake("flood", video_buffer_bytes=200_000)
        session = FfmpegSession(HlsSource(SOURCE.url, "0:v:0", None), settings, "t")
        try:
            await session.start()  # it may already fail while starting: the flood is that fast
        except FfmpegError:
            return session
        for _ in range(100):  # nobody reads
            if session.state == "failed":
                break
            await asyncio.sleep(0.05)
        await session.aclose()
        return session

    session = asyncio.run(run())
    assert session.state == "failed" and "not reading video" in session.failure
    assert reaped(session.pid)


def test_missing_binary() -> None:
    session = FfmpegSession(SOURCE, FfmpegSettings(binary="/nonexistent/ffmpeg"), "t")
    with pytest.raises(FfmpegError, match="cannot run"):
        asyncio.run(session.start())


# --- real FFmpeg on a local HLS source -------------------------------------------------------

def jpeg_size(data: bytes) -> tuple[int, int]:
    """(width, height) from the first baseline JPEG's SOF0."""
    at = data.index(b"\xff\xc0")
    return int.from_bytes(data[at + 7:at + 9], "big"), int.from_bytes(data[at + 5:at + 7], "big")


def test_real_ffmpeg_gives_both_outputs_then_stops_clean(hls_url: str) -> None:
    async def run() -> tuple[FfmpegSession, bytes, bytes]:
        session = FfmpegSession(HlsSource(hls_url, "0:p:0:v:0", "0:p:0:a:0"),
                                FfmpegSettings(startup_timeout_s=10), "t")
        await session.start()
        assert session.state == "running"
        video, audio = b"", b""
        video_reader, audio_reader = session.read("video"), session.read("audio")
        while len(video) < 60_000 or len(audio) < 4_000:
            if len(video) < 60_000:
                video += await video_reader.__anext__()
            if len(audio) < 4_000:
                audio += await audio_reader.__anext__()
        await session.aclose()
        await session.aclose()  # double stop
        return session, video, audio

    session, video, audio = asyncio.run(run())
    assert video.startswith(b"\xff\xd8\xff") and video.count(b"\xff\xd8\xff") >= 5  # JPEGs back to back
    assert jpeg_size(video) == (320, 240)  # 16:9 letterboxed into 320x240, never stretched
    assert audio[0] == 0xFF and audio[1] & 0xF6 == 0xF0  # ADTS sync word
    assert (audio[2] >> 2) & 0x0F == 4  # sampling frequency index 4 = 44100 Hz
    assert ((audio[2] & 1) << 2 | audio[3] >> 6) == 1  # channel configuration 1 = mono
    assert session.state == "stopped" and reaped(session.pid)


def test_real_ffmpeg_unknown_audio_stream_is_refused(hls_url: str) -> None:
    session = FfmpegSession(HlsSource(hls_url, "0:p:0:v:0", "0:p:0:a:m:language:xx"), FfmpegSettings(), "t")
    with pytest.raises(FfmpegError, match="no such video or audio"):
        asyncio.run(session.start())
    assert reaped(session.pid)
