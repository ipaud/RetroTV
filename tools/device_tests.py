#!/usr/bin/env python3
"""On-board tests of the playback engine, driven over the serial port.

Needs:
  - firmware built with PAUTV_DEBUG_STATS 1 (the default), flashed and connected by USB;
  - the SD prepared with tools/make_test_fixtures.sh <sd_root> and inserted in the board;
  - pyserial (PlatformIO already depends on it).

Usage: tools/device_tests.py [--port /dev/cu.usbmodem101]

Opening the port resets the board. Each test tunes to a fixture with the debug command
"T <path>" and checks the log. Exit code 0 when everything passed.
"""
import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip install pyserial (or use PlatformIO's python)")

FIXTURES = "/retrotv/test"
# The USB serial port can hold a log line back until more output pushes it out: while video
# plays, that is the next [MEDIA] line, up to MEDIA_STATS_PERIOD_MS later. Waits for events that
# happen during playback get this much extra time.
LOG_LAG_S = 6
FATAL = re.compile(r"Guru Meditation|panic|abort\(\)|Task watchdog|stop timeout|MEDIA STUCK|Backtrace")


class Board:
    def __init__(self, port: str) -> None:
        self.serial = serial.Serial()
        self.serial.port, self.serial.baudrate, self.serial.timeout = port, 115200, 0.2
        self.serial.dtr = self.serial.rts = False
        self.serial.open()  # resets the board
        self.lines: list = []
        self._partial = b""

    def pump(self, seconds: float) -> None:
        end = time.time() + seconds
        while time.time() < end:
            self._partial += self.serial.read(4096)
            *done, self._partial = self._partial.split(b"\n")
            self.lines += [line.decode(errors="replace").rstrip("\r") for line in done]

    def wait(self, pattern: str, timeout: float, since: int):
        rx, end = re.compile(pattern), time.time() + timeout
        while True:
            for line in self.lines[since:]:
                m = rx.search(line)
                if m:
                    return m
            if time.time() >= end:
                return None
            self.pump(0.2)

    def send(self, text: str) -> int:
        mark = len(self.lines)
        self.serial.write(text.encode())
        return mark


def tune(board: Board, folder: str) -> int:
    return board.send(f"T {FIXTURES}/{folder}\n")


def test_transition(b: Board) -> str:
    mark = tune(b, "transition")
    m = b.wait(r"CH00 TEST: on air.*episode (\d)/2", 8, mark)
    if not m:
        return "FAIL: no on-air tune line"
    first = int(m.group(1))
    second = first % 2 + 1
    if not b.wait(rf"CH00 TEST: next episode, episode {second}/2 at 0:00", 12 + LOG_LAG_S, mark):
        return f"FAIL: episode {second} did not follow {first}"
    if not b.wait(rf"CH00 TEST: next episode, episode {first}/2 at 0:00", 10 + LOG_LAG_S, mark):
        return "FAIL: no wrap back to the first episode"
    return f"PASS ({first} -> {second} -> {first}, each from 0:00)"


def test_stale(b: Board) -> str:
    for attempt in range(1, 4):
        mark = tune(b, "stale")
        m = b.wait(r"CH00 TEST: on air.* at (\d+:\d+) of", 8, mark)
        if not m:
            return "FAIL: no tune line"
        # The warning comes before the tune line, and a detected index restarts at 0:00 too.
        if any("stale/ep.idx does not match its episode" in line for line in b.lines[mark:]):
            if m.group(1) != "0:00":
                return f"FAIL: detected but started at {m.group(1)}"
            return f"PASS (detected, restarted from 0; attempt {attempt})"
        if m.group(1) != "0:00":
            return "FAIL: stale index not detected"
        # Landed in the first second: no seek, entry 0 is valid by design. Try again.
    return "FAIL: landed on 0:00 three times"


def test_noindex(b: Board) -> str:
    mark = tune(b, "noindex")
    if not b.wait(r"no usable \.idx for /retrotv/test/noindex/ep\.mjpeg", 6, mark):
        return "FAIL: missing index not reported"
    if not b.wait(r"CH00 TEST: /retrotv/test/noindex/ep\.mjpeg", 3, mark):
        return "FAIL: V0.1 fallback did not play"
    return "PASS (fallback: from the beginning)"


def test_eof(b: Board) -> str:
    mark = tune(b, "eof")
    if not b.wait(r"CH00 TEST: on air", 8, mark):
        return "FAIL: no tune line"
    b.pump(22)  # a (video 6 s / audio 3 s) and b (video 4 s / audio 8 s), at least twice over
    nexts = [line for line in b.lines[mark:] if "CH00 TEST: next episode" in line]
    if len(nexts) < 2:
        return f"FAIL: only {len(nexts)} clean episode changes"
    return f"PASS ({len(nexts)} clean episode changes)"


def test_empty(b: Board) -> str:
    mark = tune(b, "empty")
    if not b.wait(r"CH00 NO SIGNAL: FILE ERROR", 6, mark):
        return "FAIL: empty file not reported as FILE ERROR"
    b.pump(4)
    starts = sum("CH00 TEST: /retrotv/test/empty" in line for line in b.lines[mark:])
    return "PASS (no restart loop)" if starts <= 1 else f"FAIL: restarted {starts} times"


def test_back_to_channels(b: Board) -> str:
    mark = b.send("n")
    m = b.wait(r"CH(\d\d) (?!TEST)", 6, mark)
    if not m or m.group(1) == "00":
        return "FAIL: CH_NEXT did not return to channels.json"
    return f"PASS (CH{m.group(1)})"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    args = parser.parse_args()

    board = Board(args.port)
    if not board.wait(r"state HOME -> PLAYING", 60, 0):
        print("FAIL: the board did not reach PLAYING")
        return 1
    board.pump(2)

    tests = [("transition between indexed episodes", test_transition),
             ("stale / mismatching .idx", test_stale),
             ("channel without indexes", test_noindex),
             ("clean audio/video EOF", test_eof),
             ("empty file, no restart loop", test_empty),
             ("zapping leaves the test tune", test_back_to_channels)]
    results = [(name, fn(board)) for name, fn in tests]

    boots = sum("[BOOT] RETROTV" in line for line in board.lines)
    fatal = [line for line in board.lines if FATAL.search(line)]
    results.append(("no reboot, crash, watchdog or stuck task",
                    "PASS" if boots == 1 and not fatal else f"FAIL: boots={boots} {fatal[:3]}"))

    for name, result in results:
        print(f"{'OK ' if result.startswith('PASS') else 'XX '} {name:42} {result}")
    return 0 if all(r.startswith("PASS") for _, r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
