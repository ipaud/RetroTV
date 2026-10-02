"""Streams from a real uvicorn process, read with raw sockets the way the TV reads them."""

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

from conftest import ADTS_BYTES, FPS, FRAME_BYTES, SECONDS, adts, frame, make_root
from http_client import HttpResponse, get

SERVER_DIR = Path(__file__).resolve().parent.parent


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Live:
    def __init__(self, port: int) -> None:
        self.port = port
        self.base = f"http://127.0.0.1:{port}"
        self._responses: dict[socket.socket, HttpResponse] = {}

    def json(self, path: str, method: str = "GET") -> dict:
        with urllib.request.urlopen(urllib.request.Request(self.base + path, method=method), timeout=3) as r:
            return json.loads(r.read())

    def status(self, path: str) -> int:
        try:
            with urllib.request.urlopen(self.base + path, timeout=3) as r:
                return r.status
        except urllib.error.HTTPError as e:
            return e.code

    def open(self, path: str) -> socket.socket:
        """GET over HTTP/1.1, like the TV; read the body with body()."""
        s, response = get("127.0.0.1", self.port, path)
        assert response.status == 200, response.status_line
        self._responses[s] = response
        return s

    def body(self, s: socket.socket, n: int) -> bytes:
        """The first n bytes of the decoded body, whatever its HTTP framing."""
        return self._responses[s].read(n)


@pytest.fixture(scope="module")
def live(tmp_path_factory: pytest.TempPathFactory) -> Iterator[Live]:
    root = make_root(tmp_path_factory.mktemp("live"))
    port = free_port()
    env = {**os.environ, "PAUTV_CHANNELS": str(root / "channels.json"), "PAUTV_MEDIA_ROOT": str(root)}
    proc = subprocess.Popen(
        [sys.executable, "-m", "uvicorn", "app.main:create_app", "--factory", "--port", str(port),
         "--timeout-graceful-shutdown", "1"],
        cwd=SERVER_DIR, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    live = Live(port)
    for _ in range(100):
        try:
            live.json("/health")
            break
        except OSError:
            time.sleep(0.1)
    yield live
    proc.terminate()  # uvicorn shuts down gracefully, then re-raises SIGTERM (-15)
    assert proc.wait(timeout=5) in (0, -15)  # it ended: nothing left running


def test_both_streams_start_at_the_same_second(live: Live) -> None:
    info = live.json("/api/sessions/1", "POST")
    second = info["position_ms"] // 1000  # wherever the wall clock put the channel
    with live.open(info["video"]) as v, live.open(info["audio"]) as a:
        assert live.body(v, FRAME_BYTES) == frame(second * FPS)
        assert live.body(a, ADTS_BYTES) == adts(second)


def test_stream_loops_past_the_end(live: Live) -> None:
    info = live.json("/api/sessions/2", "POST")  # unindexed: from 0
    size = FPS * SECONDS * FRAME_BYTES
    with live.open(info["video"]) as v:
        body = live.body(v, size + FRAME_BYTES)
    assert body[size:] == frame(0)  # the file again, from its start


def test_each_stream_opens_once(live: Live) -> None:
    info = live.json("/api/sessions/1", "POST")
    with live.open(info["video"]) as v:
        live.body(v, 10)
        assert live.status(info["video"]) == 409
    assert live.status("/session/unknown/video") == 404


def test_stopping_the_server_cuts_open_streams(tmp_path: Path) -> None:
    """Ctrl+C with the TV still connected: the server must exit and close the connection."""
    root = make_root(tmp_path)
    port = free_port()
    env = {**os.environ, "PAUTV_CHANNELS": str(root / "channels.json"), "PAUTV_MEDIA_ROOT": str(root)}
    proc = subprocess.Popen(
        [sys.executable, "-m", "uvicorn", "app.main:create_app", "--factory", "--port", str(port),
         "--timeout-graceful-shutdown", "1"],
        cwd=SERVER_DIR, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    live = Live(port)
    for _ in range(100):
        try:
            live.json("/health")
            break
        except OSError:
            time.sleep(0.1)
    info = live.json("/api/sessions/1", "POST")
    with live.open(info["video"]) as v:
        live.body(v, 10)
        proc.terminate()
        assert proc.wait(timeout=5) in (0, -15)
        v.settimeout(3)
        while v.recv(65536):  # the rest in flight, then the end of the connection
            pass


def test_hang_up_mid_stream_ends_the_session(live: Live) -> None:
    info = live.json("/api/sessions/1", "POST")
    for kind in ("video", "audio"):
        with live.open(info[kind]) as s:
            live.body(s, 10)
    # Both sockets closed mid-stream (the files loop forever). A live session would answer a
    # second open with 409; a finished one no longer exists.
    for _ in range(50):
        if live.status(info["video"]) == 404:
            break
        time.sleep(0.1)
    assert live.status(info["video"]) == 404
