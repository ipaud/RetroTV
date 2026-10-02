"""Live (HLS) sessions end to end against a real uvicorn: FFmpeg only lives as long as its
session, whatever ends it."""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from collections.abc import Iterator
from pathlib import Path

import pytest

from http_client import get

SERVER_DIR = Path(__file__).resolve().parent.parent


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    out = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return bool(out) and not out.startswith("Z")  # a zombie counts as a leak too: fail on it below


def wait_gone(pid: int, seconds: float = 5.0) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        if not pid_alive(pid):
            return True
        time.sleep(0.1)
    return False


class Server:
    def __init__(self, root: Path, hls_url: str) -> None:
        channels = {"channels": [
            {"id": 20, "number": 20, "name": "HLS TEST", "provider": "hls", "source_type": "hls", "source": hls_url},
            {"id": 21, "number": 21, "name": "GONE", "provider": "hls", "source_type": "hls",
             "source": hls_url.replace("master.m3u8", "gone.m3u8")},
            {"id": 22, "number": 22, "name": "LIGHT", "provider": "hls", "source_type": "hls", "source": hls_url,
             "profile": "20fps-q18", "prebuffer_ms": 2500},
        ]}
        (root / "channels.json").write_text(json.dumps(channels))
        self.port = free_port()
        env = {**os.environ, "PAUTV_CHANNELS": str(root / "channels.json"), "PAUTV_MEDIA_ROOT": str(root)}
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "uvicorn", "app.main:create_app", "--factory", "--port", str(self.port),
             "--timeout-graceful-shutdown", "1"],
            cwd=SERVER_DIR, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                self.json("/health")
                return
            except OSError:
                time.sleep(0.1)

    def json(self, path: str, method: str = "GET") -> dict | list:
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", method=method)
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read())

    def error(self, path: str, method: str = "GET") -> tuple[int, str]:
        try:
            self.json(path, method)
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())["detail"]
        return 200, ""

    def open(self, path: str):
        return get("127.0.0.1", self.port, path)

    def stop(self) -> None:
        self.proc.terminate()
        self.proc.wait(timeout=10)


@pytest.fixture
def server(tmp_path: Path, hls_url: str) -> Iterator[Server]:
    s = Server(tmp_path, hls_url)
    yield s
    if s.proc.poll() is None:
        s.stop()


def ffmpeg_pids(server: Server) -> list[int]:
    return [d["ffmpeg_pid"] for d in server.json("/api/debug/sessions") if d["ffmpeg_pid"]]


def test_live_session_is_ready_only_with_data_and_dies_with_its_session(server: Server) -> None:
    info = server.json("/api/sessions/20", "POST")
    assert info["position_ms"] == 0 and info["audio"]
    status = server.json("/api/status/20")
    assert status["ffmpeg"] == "running" and status["provider"] == "hls" and status["source"] == "hls"
    [pid] = ffmpeg_pids(server)
    (v, video), (a, audio) = server.open(info["video"]), server.open(info["audio"])
    with v, a:
        assert video.read(3) == b"\xff\xd8\xff" and audio.read(1) == b"\xff"  # immediately: it was ready
        assert len(video.read(30_000)) == 30_000 and len(audio.read(2_000)) == 2_000
    assert wait_gone(pid), "FFmpeg outlived its session"
    assert server.json("/api/status/20")["ffmpeg"] == "idle"


def test_channel_profile_reaches_ffmpeg_and_the_tv(server: Server) -> None:
    info = server.json("/api/sessions/22", "POST")
    assert info["fps"] == 20 and info["prebuffer_ms"] == 2500
    assert server.json("/api/status/22")["profile"] == "20fps-q18"
    default = server.json("/api/sessions/20", "POST")
    assert default["fps"] == 20 and default["prebuffer_ms"] == 1500
    assert server.json("/api/status/20")["profile"] == "20fps-q14"


def test_bench_streams_filler_for_the_asked_time(server: Server) -> None:
    t0 = time.time()
    sock, response = server.open("/api/bench?seconds=1")
    with sock:
        assert response.status == 200
        got = sum(len(chunk) for chunk in iter(lambda: response.read(65536), b""))
    assert got > 100_000 and 0.9 < time.time() - t0 < 5


def test_channel_switch_leaves_no_old_ffmpeg(server: Server) -> None:
    first = server.json("/api/sessions/20", "POST")
    [old] = ffmpeg_pids(server)
    for kind in ("video", "audio"):  # the TV hangs up the old channel first
        s, r = server.open(first[kind])
        r.read(100)
        s.close()
    second = server.json("/api/sessions/20", "POST")
    assert wait_gone(old)
    [new] = ffmpeg_pids(server)
    assert new != old and pid_alive(new)
    assert [d["id"] for d in server.json("/api/debug/sessions")] == [second["session_id"]]


def test_tv_leaving_while_ffmpeg_starts_leaves_nothing(tmp_path: Path, hls_url: str) -> None:
    """A source that takes 2.5 s to start (fake FFmpeg): the TV zaps away after 0.3 s. No session
    may be created and the process must go at once, not live on until the session TTL."""
    fake = str(Path(__file__).with_name("fake_ffmpeg.py"))
    os.environ.update(PAUTV_FFMPEG=fake, FAKE_FFMPEG="slow")
    try:
        server = Server(tmp_path, hls_url)
    finally:
        del os.environ["PAUTV_FFMPEG"], os.environ["FAKE_FFMPEG"]
    try:
        s = socket.create_connection(("127.0.0.1", server.port), timeout=5)
        s.sendall(b"POST /api/sessions/20 HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n")
        time.sleep(0.3)
        s.close()  # zapped away before the answer
        time.sleep(4)  # past the fake's 2.5 s start
        assert server.json("/api/debug/sessions") == []
        left = subprocess.run(["pgrep", "-f", "fake_ffmpeg.py"], capture_output=True, text=True).stdout.split()
        assert not [p for p in left if pid_alive(int(p))], "FFmpeg kept running for a TV that left"
    finally:
        server.stop()


def test_unavailable_source_is_a_clear_503(server: Server) -> None:
    code, detail = server.error("/api/sessions/21", "POST")
    assert code == 503 and "manifest unavailable" in detail
    status = server.json("/api/status/21")
    assert status["online"] is False and "manifest unavailable" in status["reason"]
    assert ffmpeg_pids(server) == []


def test_server_shutdown_stops_ffmpeg(server: Server) -> None:
    info = server.json("/api/sessions/20", "POST")
    [pid] = ffmpeg_pids(server)
    s, r = server.open(info["video"])  # the TV is still watching
    r.read(100)
    server.stop()
    s.close()
    assert wait_gone(pid), "FFmpeg outlived the server"
