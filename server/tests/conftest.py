"""Synthetic channels for the server tests: tiny fake MJPEG / ADTS files with a matching .idx,
so no real media (and no FFmpeg) is needed."""

from __future__ import annotations

import functools
import http.server
import json
import shutil
import struct
import subprocess
import threading
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.config import Settings
from app.main import create_app
from app.media_index import HEADER, MAGIC

FPS = 24
SECONDS = 3
FRAME_BYTES = 102
ADTS_BYTES = 32



class QuietServer(http.server.ThreadingHTTPServer):
    """A client (FFmpeg) hanging up mid-segment is normal here: no traceback."""

    def handle_error(self, request, client_address) -> None:
        pass

def frame(i: int) -> bytes:
    """Fake JPEG number i: SOI, its number, EOI. 102 bytes."""
    return b"\xff\xd8\xff\xe0" + f"{i:04d}".encode() + b"v" * 92 + b"\xff\xd9"


def adts(k: int) -> bytes:
    """Fake ADTS frame for second k: sync word, its number. 32 bytes."""
    return b"\xff\xf1" + f"{k:02d}".encode() + b"a" * 28


def write_episode(folder: Path, name: str, indexed: bool = True) -> None:
    """name.mjpeg (SECONDS s at FPS), name.aac and, if indexed, name.idx (one entry per second)."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f"{name}.mjpeg").write_bytes(b"".join(frame(i) for i in range(FPS * SECONDS)))
    (folder / f"{name}.aac").write_bytes(b"".join(adts(k) for k in range(SECONDS)))
    if indexed:
        head = HEADER.pack(MAGIC, 1, FPS, FPS * SECONDS, SECONDS * 1000, FPS, 0, SECONDS, 44100)
        entries = b"".join(struct.pack("<II", k * FPS * FRAME_BYTES, k * ADTS_BYTES) for k in range(SECONDS))
        (folder / f"{name}.idx").write_bytes(head + entries)


CHANNELS = [
    {"id": 1, "number": 1, "name": "REMOTE DEMO", "provider": "local", "video": "media/demo.mjpeg",
     "audio": "media/demo.aac"},
    {"id": 2, "number": 2, "name": "NO INDEX", "provider": "local", "video": "media/plain.mjpeg",
     "audio": "media/plain.aac"},
    {"id": 3, "number": 3, "name": "MISSING", "provider": "local", "video": "media/nothing.mjpeg"},
    {"id": 10, "number": 10, "name": "SX3", "provider": "3cat", "source_type": "hls"},
]


class FakeClock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


def make_root(folder: Path) -> Path:
    write_episode(folder / "media", "demo")
    write_episode(folder / "media", "plain", indexed=False)
    (folder / "channels.json").write_text(json.dumps({"channels": CHANNELS}))
    return folder


@pytest.fixture
def root(tmp_path: Path) -> Path:
    return make_root(tmp_path)


@pytest.fixture
def clock() -> FakeClock:
    return FakeClock()


@pytest.fixture
def client(root: Path, clock: FakeClock) -> TestClient:
    settings = Settings(channels_file=root / "channels.json", media_root=root, session_ttl_s=15.0, max_sessions=2)
    # 1.5 s into the 3 s programme: entry 1
    return TestClient(create_app(settings, now_ms=lambda: 3000 * 1000 + 1500, clock=clock))


@pytest.fixture(scope="session")
def hls_url(tmp_path_factory: pytest.TempPathFactory):
    """A 16:9, 25 fps HLS source with audio (a master playlist and one variant), served over HTTP."""
    if shutil.which("ffmpeg") is None:
        pytest.skip("ffmpeg not installed")
    root = tmp_path_factory.mktemp("hls")
    subprocess.run(
        ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
         "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
         "-t", "8", "-c:v", "libx264", "-preset", "ultrafast", "-g", "25", "-c:a", "aac",
         "-f", "hls", "-hls_time", "2", "-hls_playlist_type", "vod",
         "-master_pl_name", "master.m3u8", "-var_stream_map", "v:0,a:0", str(root / "v%v.m3u8")],
        check=True)
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args) -> None:  # no access log on the console
            pass

    handler = functools.partial(Quiet, directory=str(root))
    server = QuietServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    yield f"http://127.0.0.1:{server.server_port}/master.m3u8"
    server.shutdown()
