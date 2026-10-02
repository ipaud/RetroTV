#!/usr/bin/env python3
"""On-board tests of remote channels (RETROTV Server), driven over the serial port.

Needs:
  - firmware built with PAUTV_DEBUG_STATS 1 (the default), flashed and connected by USB;
  - the board on the same Wi-Fi as this computer, and the SD in the board (local channels);
  - server/.venv set up once with server/run.sh (it makes the demo clip too);
  - on macOS, the firewall allowing incoming connections to Python (the first run asks).

Usage: tools/remote_device_tests.py [--port /dev/cu.usbmodem101] [--live]

The script starts its own RETROTV Server on a free port and this computer's LAN address (no
mDNS), so it can stop it and start it again. Opening the serial port resets the board.
Exit code 0 when everything passed.

--live tests a live HLS channel instead of the file demo: it also starts a local live HLS
source (FFmpeg test pattern + tone, like server/tools/test_hls_pipeline.py), so it can stop the
source and bring it back, and adds 20 zaps between the live channel and the SD.
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from device_tests import FATAL, LOG_LAG_S, Board  # noqa: E402

SERVER_DIR = Path(__file__).resolve().parent.parent / "server"
sys.path.insert(0, str(SERVER_DIR / "tools"))
SD_ERROR = re.compile(r"sdmmc_read_blocks failed|diskio_sdmmc")
PLAY_WAIT_S = 15  # file channel: session + ~1.3 s of video buffered
LIVE_PLAY_WAIT_S = 30  # live: the server's FFmpeg starts first (5-8 s measured)


def lan_ip() -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect(("192.0.2.1", 9))
        return s.getsockname()[0]


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("0.0.0.0", 0))
        return s.getsockname()[1]


class Server:
    def __init__(self, port: int, channels: Path | None = None, channel: int = 1) -> None:
        self.port = port
        self.channels = channels
        self.channel = channel
        self.env: dict[str, str] = {}  # extra PAUTV_* settings for the next start()
        self.proc = None
        self.log = Path(tempfile.gettempdir()) / f"pautv-server-{port}.log"  # shown when a check fails

    def start(self) -> None:
        env = {**os.environ, "PAUTV_ANNOUNCE": "0", "PAUTV_PORT": str(self.port), **self.env}
        if self.channels:
            env["PAUTV_CHANNELS"] = str(self.channels)
        self.proc = subprocess.Popen(
            [str(SERVER_DIR / ".venv/bin/uvicorn"), "app.main:create_app", "--factory", "--host", "0.0.0.0",
             "--port", str(self.port), "--timeout-graceful-shutdown", "1"],
            cwd=SERVER_DIR, env=env, stdout=self.log.open("a"), stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                self.api("/health")
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("RETROTV Server did not start")

    def stop(self) -> None:
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
            self.proc = None

    def api(self, path: str) -> dict:
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}{path}", timeout=3) as r:
            return json.loads(r.read())

    def sessions(self) -> int:
        return self.api(f"/api/status/{self.channel}").get("sessions", 0)

    def ffmpeg_pids(self) -> list[int]:
        return [d["ffmpeg_pid"] for d in self.api("/api/debug/sessions") if d.get("ffmpeg_pid")]


def tune(b: Board, url: str) -> int:
    return b.send(f"T {url}\n")


def last_match(b: Board, pattern: str, since: int):
    rx = re.compile(pattern)
    found = None
    for line in b.lines[since:]:
        m = rx.search(line)
        if m:
            found = m
    return found


def test_remote_plays(b: Board, url: str, wait_s: int) -> str:
    mark = tune(b, url)
    if not b.wait(r"\[REMOTE\] playback started", wait_s, mark):
        return f"FAIL: no playback within {wait_s} s"
    b.pump(12 + LOG_LAG_S)
    media = last_match(b, r"\[MEDIA\] fps ([\d.]+) .*av_drift_ms avg (-?\d+) .*dropped_frames (\d+)", mark)
    net = last_match(b, r"\[NET\] t=\d+s network_kbps (\d+) .* timeouts (\d+) .* av_drift_ms avg (-?\d+)", mark)
    if not media or not net:
        return "FAIL: no [MEDIA] / [NET] stats while playing"
    if float(media.group(1)) < 20:
        return f"FAIL: {media.group(1)} fps"
    if any("NO SIGNAL" in line for line in b.lines[mark:]):
        return "FAIL: NO SIGNAL while playing"
    return (f"PASS ({media.group(1)} fps, dropped {media.group(3)}, av drift {net.group(3)} ms, "
            f"{net.group(1)} kbps)")


def test_remote_to_local(b: Board, server: Server) -> str:
    mark = b.send("n")
    if not b.wait(r"CH\d\d (?!TEST).*: (on air|test card|teletext|/retrotv)", 8, mark):
        return "FAIL: the next local channel did not start"
    for _ in range(30):
        if server.sessions() == 0:
            return "PASS (session closed on the server)"
        time.sleep(0.1)
    return "FAIL: the server still has the session: sockets not closed"


def test_local_to_remote(b: Board, url: str, wait_s: int) -> str:
    mark = tune(b, url)
    return "PASS" if b.wait(r"\[REMOTE\] playback started", wait_s, mark) else "FAIL: no playback"


def test_remote_to_remote(b: Board, url: str, server: Server, wait_s: int) -> str:
    b.pump(2)
    before = server.ffmpeg_pids()
    mark = tune(b, url)
    if not b.wait(r"\[REMOTE\] playback started", wait_s, mark):
        return "FAIL: no playback after re-tuning"
    b.pump(2)
    n = server.sessions()
    old_alive = [p for p in before if p in server.ffmpeg_pids()]
    if n != 1 or old_alive:
        return f"FAIL: {n} sessions, old FFmpeg still running: {old_alive}"
    return "PASS (old session closed" + (", old FFmpeg gone)" if before else ")")


def test_server_down_and_back(b: Board, server: Server) -> str:
    b.pump(3)
    mark = len(b.lines)
    server.stop()
    if not b.wait(r"NO SIGNAL: (SIGNAL LOST|NO SERVER)", 10 + LOG_LAG_S, mark):
        return "FAIL: no NO SIGNAL after the server stopped"
    if not b.wait(r"\[REMOTE\] reconnect #", 10, mark):
        return "FAIL: no retry"
    b.pump(5)
    mark_back = len(b.lines)
    server.start()
    if not b.wait(r"\[REMOTE\] playback started", 25 + server_start_extra(server), mark_back):
        return "FAIL: did not reconnect once the server was back"
    retries = sum("[REMOTE] reconnect #" in line for line in b.lines[mark:])
    return f"PASS (NO SIGNAL, {retries} retries, back by itself)"


def server_start_extra(server: Server) -> int:
    return 15 if server.channels else 0  # a live channel starts FFmpeg first


def test_source_down_and_back(b: Board, source) -> str:
    b.pump(3)
    mark = len(b.lines)
    source.stop()
    if not b.wait(r"NO SIGNAL: (SIGNAL LOST|NO SERVER|CHANNEL OFF|NO DATA)", 30 + LOG_LAG_S, mark):
        return "FAIL: no NO SIGNAL after the source stopped"
    b.pump(3)
    back = len(b.lines)
    source.start()
    if not b.wait(r"\[REMOTE\] playback started", 60, back):
        return "FAIL: did not come back with the source"
    return "PASS (NO SIGNAL, then back by itself)"


def test_twenty_zaps(b: Board, url: str) -> str:
    """Ten times: to the live channel (it must play) and back to the SD."""
    played = 0
    for _ in range(10):
        mark = tune(b, url)
        if b.wait(r"\[REMOTE\] playback started", LIVE_PLAY_WAIT_S, mark):
            played += 1
        b.pump(3)
        mark = b.send("n")
        b.wait(r"CH\d\d (?!TEST)", 8, mark)
        b.pump(2)
    return f"PASS ({played}/10 live tunes played)" if played == 10 else f"FAIL: {played}/10 live tunes played"


def test_zapping_still_works(b: Board) -> str:
    mark = b.send("n")
    if not b.wait(r"CH\d\d (?!TEST)", 8, mark):
        return "FAIL: zapping did not answer"
    return "PASS"


def link_warm_up(b: Board, demo_url: str) -> str:
    """20 s of the file demo first. Measured on the home Wi-Fi: right after the board joins, a
    single TCP connection to it carries ~50-95 KB/s for 15-20 s, then ~150-190 KB/s. A live
    channel tuned during that window starves; this is reported, not hidden (docs/TEST_PLAN.md)."""
    mark = tune(b, demo_url)
    b.wait(r"\[REMOTE\] playback started", PLAY_WAIT_S, mark)
    b.pump(20)
    rates = [int(m.group(1)) for line in b.lines[mark:] if (m := re.search(r"\[NET\] t=\d+s network_kbps (\d+)", line))]
    return " -> ".join(f"{r} kbps" for r in rates) or "no [NET] lines"


def live_setup(folder: Path):
    """A local live HLS source and a channels.json with the file demo (1) and it (2)."""
    from test_hls_pipeline import LiveSource  # server/tools

    source = LiveSource(folder)
    source.start()
    time.sleep(5)  # a few segments in the window
    channels = folder / "channels.json"
    channels.write_text(json.dumps({"channels": [
        {"id": 1, "number": 1, "name": "REMOTE DEMO", "provider": "local", "source_type": "file",
         "video": str(SERVER_DIR / "media/demo/demo.mjpeg"), "audio": str(SERVER_DIR / "media/demo/demo.aac")},
        {"id": 2, "number": 2, "name": "LOCAL LIVE", "provider": "hls", "source_type": "hls", "source": source.url}]}))
    return source, channels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    parser.add_argument("--live", action="store_true", help="test a live HLS channel (local source)")
    args = parser.parse_args()

    tmp = tempfile.TemporaryDirectory()
    source = None
    if args.live:
        source, channels = live_setup(Path(tmp.name))
        server = Server(free_port(), channels, channel=2)
    else:
        server = Server(free_port())
    wait_s = LIVE_PLAY_WAIT_S if args.live else PLAY_WAIT_S
    server.start()
    url = f"http://{lan_ip()}:{server.port}/channel/{server.channel}"
    board = Board(args.port)
    try:
        if not board.wait(r"\[WIFI\] ONLINE", 100, 0):
            print("FAIL: the board did not get Wi-Fi (the retry comes every 60 s)")
            return 1
        board.pump(3)
        mark = board.send("m")
        board.wait(r"snapshot", 3, mark)
        warm_up = ""
        if args.live:  # the Wi-Fi link to the board is slow for its first seconds of traffic
            warm_up = link_warm_up(board, url.replace(f"/channel/{server.channel}", "/channel/1"))

        results = [("remote channel plays", test_remote_plays(board, url, wait_s)),
                   ("remote -> local (SD)", test_remote_to_local(board, server)),
                   ("local -> remote", test_local_to_remote(board, url, wait_s)),
                   ("remote -> remote", test_remote_to_remote(board, url, server, wait_s)),
                   ("server down, then back", test_server_down_and_back(board, server))]
        if source is not None:
            results += [("live source down, then back", test_source_down_and_back(board, source)),
                        ("20 zaps between live and SD", test_twenty_zaps(board, url))]
        results.append(("zapping afterwards", test_zapping_still_works(board)))
        board.pump(2)
        mark_end = board.send("m")
        board.wait(r"snapshot", 3, mark_end)
    finally:
        server.stop()
        if source is not None:
            source.close()
        tmp.cleanup()

    boots = sum("[BOOT] RETROTV" in line for line in board.lines)
    fatal = [line for line in board.lines if FATAL.search(line)]
    results.append(("no reboot, crash, watchdog or stuck task",
                    "PASS" if boots == 1 and not fatal else f"FAIL: boots={boots} {fatal[:3]}"))
    sd_errors = sum(bool(SD_ERROR.search(line)) for line in board.lines)
    results.append(("no SD read errors", "PASS" if sd_errors == 0 else f"FAIL: {sd_errors} SD errors"))

    if warm_up:
        print(f"    link warm-up (file demo, 20 s): {warm_up}")
    for name, result in results:
        print(f"{'OK ' if result.startswith('PASS') else 'XX '} {name:42} {result}")
    if not all(r.startswith("PASS") for _, r in results):  # what the board said about the network
        for line in board.lines:
            if any(tag in line for tag in ("[REMOTE]", "[SERVER]", "NO SIGNAL", "[WIFI]")):
                print("   ", line.strip()[:150])
        print("    server log:")
        for line in server.log.read_text(errors="replace").splitlines():
            if any(tag in line for tag in ("[SESSION]", "[FFMPEG]", "[HLS]", "[3CAT]", "Error", "error")):
                print("    |", line.strip()[:150])
    for line in board.lines:
        if "snapshot" in line:
            print("   ", line.strip())
    return 0 if all(r.startswith("PASS") for _, r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
