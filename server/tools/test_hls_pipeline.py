#!/usr/bin/env python3
"""End-to-end check of the live pipeline, no ESP32 and no Internet needed:

  local live HLS source (FFmpeg, 16:9 25 fps + tone) -> RETROTV Server -> FFmpeg session
  -> /session/<id>/video + /audio read like the TV reads them

Checks that the video is MJPEG 320x240 at the session's frame rate, the audio ADTS 44.1 kHz mono, that both
carry the same amount of time (same source clock), that stopping the source is noticed and
recovered once it is back, and that closing the session leaves no FFmpeg running.

Usage: server/.venv/bin/python server/tools/test_hls_pipeline.py [--seconds 12]
"""
from __future__ import annotations

import argparse
import functools
import http.client
import http.server
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

SERVER_DIR = Path(__file__).resolve().parent.parent

AUDIO_RATE = 44100
ADTS_RATES = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]



class QuietServer(http.server.ThreadingHTTPServer):
    """A client (FFmpeg) hanging up mid-segment is normal here: no traceback."""

    def handle_error(self, request, client_address) -> None:
        pass

class LiveSource:
    """FFmpeg writing a live HLS playlist (2 s segments, a sliding window) served over HTTP."""

    def __init__(self, folder: Path) -> None:
        self.folder = folder
        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *args) -> None:  # no access log on the console
                pass

        handler = functools.partial(Quiet, directory=str(folder))
        self.http = QuietServer(("127.0.0.1", 0), handler)
        threading.Thread(target=self.http.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.http.server_port}/live.m3u8"
        self.proc = None

    def start(self) -> None:
        self.proc = subprocess.Popen(
            ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-re",
             "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=25",
             "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
             "-c:v", "libx264", "-preset", "ultrafast", "-g", "50", "-c:a", "aac",
             "-f", "hls", "-hls_time", "2", "-hls_list_size", "6", "-hls_flags", "delete_segments+program_date_time",
             str(self.folder / "live.m3u8")])

    def stop(self) -> None:
        if self.proc:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
            self.proc = None

    def close(self) -> None:
        self.stop()
        self.http.shutdown()


class Server:
    def __init__(self, source_url: str, folder: Path) -> None:
        (folder / "channels.json").write_text(json.dumps({"channels": [
            {"id": 1, "number": 1, "name": "HLS PIPELINE", "provider": "hls", "source_type": "hls",
             "source": source_url}]}))
        self.port = free_port()
        env = {**os.environ, "PAUTV_CHANNELS": str(folder / "channels.json"), "PAUTV_MEDIA_ROOT": str(folder)}
        self.proc = subprocess.Popen(
            [str(SERVER_DIR / ".venv/bin/uvicorn"), "app.main:create_app", "--factory", "--port", str(self.port),
             "--timeout-graceful-shutdown", "1", "--log-level", "warning"],
            cwd=SERVER_DIR, env=env)
        for _ in range(100):
            try:
                self.api("/health")
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("server did not start")

    def api(self, path: str, method: str = "GET"):
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", method=method)
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read())

    def stop(self) -> None:
        self.proc.terminate()
        self.proc.wait(timeout=10)


def jpeg_frames(data: bytes) -> list[bytes]:
    frames, at = [], 0
    while (start := data.find(b"\xff\xd8\xff", at)) >= 0 and (end := data.find(b"\xff\xd9", start)) >= 0:
        frames.append(data[start:end + 2])
        at = end + 2
    return frames


def jpeg_size(frame: bytes) -> tuple[int, int]:
    at = frame.index(b"\xff\xc0")
    return int.from_bytes(frame[at + 7:at + 9], "big"), int.from_bytes(frame[at + 5:at + 7], "big")


def adts_frames(data: bytes) -> list[tuple[int, int]]:
    """(sample rate, channels) of each ADTS frame, walking the frame lengths."""
    frames, at = [], 0
    while at + 7 <= len(data):
        if data[at] != 0xFF or data[at + 1] & 0xF6 != 0xF0:
            raise ValueError(f"lost ADTS sync at byte {at}")
        length = ((data[at + 3] & 0x03) << 11) | (data[at + 4] << 3) | (data[at + 5] >> 5)
        if at + length > len(data):
            break
        rate = ADTS_RATES[(data[at + 2] >> 2) & 0x0F]
        channels = ((data[at + 2] & 1) << 2) | (data[at + 3] >> 6)
        frames.append((rate, channels))
        at += length
    return frames


def read_both(server: Server, info: dict, seconds: float) -> tuple[bytes, bytes, float, dict[str, list[tuple[float, int]]]]:
    """Reads video and audio at the same time for `seconds`, as the TV would. Also returns, per
    stream, (time, bytes so far) marks to measure the pace after the start-up burst."""
    data = {"video": bytearray(), "audio": bytearray()}
    marks: dict[str, list[tuple[float, int]]] = {"video": [], "audio": []}
    t0 = time.time()

    def reader(kind: str) -> None:
        conn = http.client.HTTPConnection("127.0.0.1", server.port, timeout=30)
        conn.request("GET", info[kind])
        response = conn.getresponse()
        while time.time() - t0 < seconds:
            chunk = response.read1(65536)
            if not chunk:
                break
            data[kind] += chunk
            marks[kind].append((time.time() - t0, len(data[kind])))
        conn.close()

    threads = [threading.Thread(target=reader, args=(k,)) for k in ("video", "audio")]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return bytes(data["video"]), bytes(data["audio"]), time.time() - t0, marks


def media_seconds(kind: str, data: bytes, fps: int) -> float:
    return len(jpeg_frames(data)) / fps if kind == "video" else len(adts_frames(data)) * 1024 / AUDIO_RATE


def pace(kind: str, data: bytes, marks: list[tuple[float, int]], after: float, fps: int) -> float:
    """Media seconds delivered per wall second once `after` seconds have passed (no burst)."""
    start = next((m for m in marks if m[0] >= after), None)
    if start is None or marks[-1][0] - start[0] < 2:
        return 0.0
    cut = start[1]
    if kind == "audio":  # cut on an ADTS frame boundary
        at = 0
        while at < cut:
            at += ((data[at + 3] & 0x03) << 11) | (data[at + 4] << 3) | (data[at + 5] >> 5)
        cut = at
    return media_seconds(kind, data[cut:], fps) / (marks[-1][0] - start[0])


def pid_alive(pid: int) -> bool:
    out = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return bool(out) and not out.startswith("Z")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--seconds", type=float, default=12.0)
    args = parser.parse_args()
    if shutil.which("ffmpeg") is None:
        print("ffmpeg not found")
        return 1

    results: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str = "") -> None:
        results.append((name, ok, detail))

    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        source = LiveSource(folder)
        source.start()
        time.sleep(5)  # a few segments in the live window
        server = Server(source.url, folder)
        try:
            t0 = time.time()
            info = server.api("/api/sessions/1", "POST")
            check("session ready with data", True, f"{time.time() - t0:.1f} s")
            [pid] = [d["ffmpeg_pid"] for d in server.api("/api/debug/sessions")]

            video, audio, took, marks = read_both(server, info, args.seconds)
            frames = jpeg_frames(video)
            sizes = {jpeg_size(f) for f in frames[:50]}
            check("video is MJPEG 320x240", bool(frames) and sizes == {(320, 240)},
                  f"{len(frames)} frames at {info['fps']} fps, {sizes}")
            fps = info["fps"]
            video_s = len(frames) / fps
            try:
                adts = adts_frames(audio)
                formats = set(adts)
                audio_s = len(adts) * 1024 / AUDIO_RATE
                check("audio is ADTS 44.1 kHz mono", formats == {(AUDIO_RATE, 1)}, f"{len(adts)} frames, {formats}")
            except ValueError as e:
                audio_s = 0.0
                check("audio is ADTS 44.1 kHz mono", False, str(e))
            video_pace = pace("video", video, marks["video"], 4.0, fps)
            audio_pace = pace("audio", audio, marks["audio"], 4.0, fps)
            check("real time after the start-up burst", 0.9 < video_pace < 1.1 and 0.9 < audio_pace < 1.1,
                  f"video x{video_pace:.2f}, audio x{audio_pace:.2f}")
            check("video and audio never drift apart", abs(video_s - audio_s) < 2.5,
                  f"delivered video {video_s:.1f} s, audio {audio_s:.1f} s in {took:.1f} s")
            gone = False
            for _ in range(50):
                if not pid_alive(pid):
                    gone = True
                    break
                time.sleep(0.1)
            check("FFmpeg gone once the TV hangs up", gone, f"pid {pid}")

            # Source stops publishing while the TV keeps reading, then comes back.
            info = server.api("/api/sessions/1", "POST")
            watcher = threading.Thread(target=read_both, args=(server, info, 25))
            watcher.start()
            time.sleep(3)
            source.stop()
            status: dict = {}
            for _ in range(40):
                time.sleep(0.5)
                status = server.api("/api/status/1")
                if status.get("restarts", 0) >= 1 or status.get("ffmpeg") in ("failed", "idle"):
                    break
            noticed = status.get("restarts", 0) >= 1 or status.get("ffmpeg") in ("failed", "idle")
            check("stopped source noticed", noticed, f"state {status.get('ffmpeg')}, restarts "
                  f"{status.get('restarts')}, {status.get('failure') or 'source stalled or ended'}")
            watcher.join()
            source.start()
            time.sleep(6)
            recovered = False
            for _ in range(10):
                try:
                    again = server.api("/api/sessions/1", "POST")
                    v, a, _, _ = read_both(server, again, 3)
                    recovered = len(jpeg_frames(v)) > 20 and len(a) > 1000
                    break
                except OSError:
                    time.sleep(2)
            check("playing again once the source is back", recovered)
        finally:
            leftovers = [d["ffmpeg_pid"] for d in server.api("/api/debug/sessions") if d["ffmpeg_pid"]]
            server.stop()
            time.sleep(1)
            alive = [p for p in leftovers if pid_alive(p)]
            check("no FFmpeg left after the server stops", not alive, f"alive: {alive}")
            source.close()

    for name, ok, detail in results:
        print(f"{'OK ' if ok else 'XX '} {name:40} {detail}")
    return 0 if all(ok for _, ok, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
