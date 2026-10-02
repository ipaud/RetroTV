#!/usr/bin/env python3
"""Remote streaming tuning on the board: where the network bottleneck is and which live profile
the TV plays best. Drives the board over serial and starts its own RETROTV Server.

Phases (--phases, comma separated, run in this order):
  cold       --profile tuned the moment Wi-Fi joins, for 60 s: the first tune after power-on
  bench      raw TCP throughput to the board (serial command B) with 1, 2 and 4 connections,
             right after Wi-Fi joins and again once the link is warm, with ping RTT under load
             and the server's retransmitted bytes for those connections (nettop)
  profiles   each live profile (--profiles) on the live channel for --minutes
  livestart  --profile starting from the newest segment (-1) and from 3 segments back (-3)
  prebuffer  --profile with 1.0, 2.0 and 3.0 s of prebuffer
  sd         SD read errors: live and SD channels alternating, then the SD alone
  soak       --profile for --soak-minutes, memory snapshot every minute

Needs what tools/remote_device_tests.py needs, plus a live channel in server/config/channels.json
(--channel, default 10 = 3Cat SX3). Opening the serial port resets the board.

Usage: tools/stream_profiles.py [--phases bench,profiles] [--minutes 2.5] [--out DIR]

Prints a Markdown table per phase; --out keeps the serial log, the server log and results.json.
"""
import argparse
import json
import math
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from device_tests import FATAL, Board  # noqa: E402
from remote_device_tests import SD_ERROR, SERVER_DIR, Server, free_port, lan_ip  # noqa: E402

PROFILES = ["24fps-q12", "24fps-q16", "20fps-q14", "20fps-q18", "18fps-q16"]
BASE_ENV = {"PAUTV_STATS_PERIOD_S": "1"}
WARM_S = 45  # measured: the link to the board is slow for its first ~20-30 s  # server [STATS] every second: per-second bitrate
NET = re.compile(
    r"\[NET\] t=(?P<t>\d+)s network_kbps (?P<kbps>\d+) buffer_bytes v(?P<vbuf>\d+) a(?P<abuf>\d+) "
    r"buffer_fill v(?P<vfill>\d+)% a(?P<afill>\d+)% read_stalls \+(?P<stalls>\d+) wait_ms (?P<wait>\d+) "
    r"timeouts (?P<timeouts>\d+) reconnects (?P<reconnects>\d+) rssi (?P<rssi>-?\d+) "
    r"dropped_frames \+(?P<dropped>\d+) fps (?P<fps>\d+) av_drift_ms avg (?P<drift>-?\d+) max (?P<drift_max>-?\d+) "
    r"link data (?P<data>\d+)% idle (?P<idle>\d+)% full (?P<full>\d+)% gap_max_ms (?P<gap>\d+)")
STATS = re.compile(r"\[STATS\] ch\d+ t=(?P<t>\d+)s \S+ video out (?P<v>\d+) kbps sent \d+ kbps backlog (?P<vb>\d+) KB"
                   r"(?: audio out (?P<a>\d+) kbps)?")
BENCH = re.compile(r"\[BENCH\] result n=(\d+) total (\d+) KB/s per connection KB/s:([\d ]+)")
RTT = re.compile(r"time=([\d.]+) ms")
LOCAL_TUNE = re.compile(r"CH\d\d (?!TEST).*: (on air|test card|teletext|/retrotv)")


class TimedBoard(Board):
    """Board that also keeps when each line arrived (seconds since the port opened)."""

    def __init__(self, port: str) -> None:
        self.t0 = time.time()
        self.times: list[float] = []
        super().__init__(port)

    def pump(self, seconds: float) -> None:
        end = time.time() + seconds
        while time.time() < end:
            self._partial += self.serial.read(4096)
            *done, self._partial = self._partial.split(b"\n")
            self.lines += [line.decode(errors="replace").rstrip("\r") for line in done]
            self.times += [time.time() - self.t0] * len(done)


def p95(xs: list[float]) -> float:
    return sorted(xs)[max(0, math.ceil(0.95 * len(xs)) - 1)] if xs else 0.0


def mean(xs: list[float]) -> float:
    return sum(xs) / len(xs) if xs else 0.0


class Pinger:
    """ping every 0.2 s in the background: RTT and loss while something else loads the link."""

    def __init__(self, ip: str) -> None:
        self.proc = subprocess.Popen(["ping", "-i", "0.2", ip], stdout=subprocess.PIPE, text=True)

    def stop(self) -> dict:
        self.proc.send_signal(signal.SIGINT)
        out = self.proc.communicate(timeout=5)[0]
        rtts = [float(x) for x in RTT.findall(out)]
        sent = re.search(r"(\d+) packets transmitted", out)
        n = int(sent.group(1)) if sent else 0
        return {"ping_p50": sorted(rtts)[len(rtts) // 2] if rtts else None, "ping_p95": p95(rtts) if rtts else None,
                "ping_max": max(rtts) if rtts else None, "ping_loss_pct": round(100 * (n - len(rtts)) / n, 1) if n else None}


def retransmits(pid: int, board_ip: str) -> dict:
    """The server's TCP connections to the board, as nettop sees them now (per-socket totals)."""
    out = subprocess.run(["nettop", "-m", "tcp", "-L", "1", "-x", "-p", str(pid), "-J", "bytes_out,re-tx,rtt_avg"],
                         capture_output=True, text=True, timeout=15).stdout
    sent = retx = 0
    rtts = []
    for line in out.splitlines():
        cols = line.split(",")
        if f"<->{board_ip}:" not in cols[0] or len(cols) < 4:
            continue
        sent += int(cols[1] or 0)
        retx += int(cols[2] or 0)
        if cols[3]:
            rtts.append(float(cols[3].split()[0]))
    return {"tx_bytes": sent, "retx_bytes": retx, "retx_pct": round(100 * retx / sent, 2) if sent else None,
            "srtt_ms": round(mean(rtts), 1) if rtts else None}


class Rig:
    def __init__(self, args) -> None:
        self.args = args
        self.server = Server(free_port(), SERVER_DIR / "config/channels.json", channel=args.channel)
        self.server.env = dict(BASE_ENV)
        self.server.start()
        self.live_env = {"PAUTV_LIVE_PROFILE": args.profile, "PAUTV_LIVE_START_INDEX": str(args.live_start)}
        self.base = f"http://{lan_ip()}:{self.server.port}"
        self.url = f"{self.base}/channel/{args.channel}"
        self.board = TimedBoard(args.port)
        online = self.board.wait(r"\[WIFI\] ONLINE after \d+ ms, ip (\S+)", 100, 0)
        if not online:
            raise SystemExit("the board did not get Wi-Fi")
        self.ip = online.group(1)
        self.online_at = self.board.times[-1] if self.board.times else 0.0

    def restart_server(self, env: dict) -> int:
        """Restarts the server with these settings; returns where its new log starts."""
        self.server.stop()
        self.server.env = {**BASE_ENV, **env}
        at = self.server.log.stat().st_size if self.server.log.exists() else 0
        self.server.start()
        return at

    def server_log(self, since: int) -> list[str]:
        with self.server.log.open("rb") as f:
            f.seek(since)
            return f.read().decode(errors="replace").splitlines()

    def leave_remote(self) -> None:
        self.board.send("n")  # an SD channel, so the server can restart under no stream
        self.board.pump(3)

    # --- bench -----------------------------------------------------------------------------
    def bench(self, n: int) -> dict:
        pinger = Pinger(self.ip)
        mark = self.board.send(f"B {n} {self.base}/api/bench?seconds=12\n")
        self.board.pump(8)
        tx = retransmits(self.server.proc.pid, self.ip)
        m = self.board.wait(BENCH.pattern, 10, mark)
        ping = pinger.stop()
        seconds = [int(x) for x in re.findall(r"\[BENCH\] \+\d+s (\d+) kbps", "\n".join(self.board.lines[mark:]))]
        return {"connections": n, "total_kBps": int(m.group(2)) if m else None,
                "per_connection_kBps": m.group(3).split() if m else None,
                "min_second_kbps": min(seconds) if seconds else None, **ping, **tx}

    # --- one live run ----------------------------------------------------------------------
    def play(self, label: str, env: dict, seconds: float, leave: bool = True) -> dict:
        if leave:
            self.leave_remote()
        log_at = self.restart_server(env)
        pinger = Pinger(self.ip)
        mark = self.board.send(f"T {self.url}\n")
        self.board.pump(max(seconds - 5, 1))
        tx = retransmits(self.server.proc.pid, self.ip)
        self.board.pump(5)
        ping = pinger.stop()
        return {"label": label, **summarize(self.board.lines[mark:], self.server_log(log_at)), **ping, **tx}


def summarize(lines: list[str], server_lines: list[str]) -> dict:
    net = [{k: int(v) for k, v in m.groupdict().items()} for line in lines if (m := NET.search(line))]
    weights = [n["t"] - p["t"] if i else 1 for i, (p, n) in enumerate(zip([{"t": 0}] + net, net))]
    played_s = sum(weights)

    def weighted(key: str) -> float:
        return sum(w * n[key] for w, n in zip(weights, net)) / played_s if played_s else 0.0

    first = next((int(m.group(1)) for x in lines if (m := re.search(r"time_to_first_video_ms (\d+)", x))), None)
    stable = next((int(m.group(1)) for x in lines if (m := re.search(r"time_to_stable_playback_ms (\d+)", x))), None)
    rate = [int(m["v"]) + int(m["a"] or 0) for x in server_lines if (m := STATS.search(x)) and int(m["t"]) > 3]
    backlog = [int(m["vb"]) for x in server_lines if (m := STATS.search(x))]
    dropped = sum(n["dropped"] for n in net)
    return {
        "mean_kbps": round(mean(rate)), "p95_kbps": round(p95(rate)), "first_video_ms": first, "stable_ms": stable,
        "dropped": dropped, "dropped_per_min": round(dropped * 60 / played_s, 1) if played_s else None,
        "fps": round(weighted("fps"), 1), "read_stalls": sum(n["stalls"] for n in net),
        "stall_wait_ms": sum(n["wait"] for n in net),
        "reconnects": sum("[REMOTE] reconnect #" in x for x in lines),
        "no_signal": sum("NO SIGNAL" in x for x in lines),
        "timeouts": net[-1]["timeouts"] - net[0]["timeouts"] if net else None,
        "drift_avg": round(weighted("drift")), "drift_max": max((n["drift_max"] for n in net), default=None),
        "gap_max_ms": max((n["gap"] for n in net), default=None), "vbuf_min_kb": min((n["vbuf"] for n in net), default=0) // 1024,
        "idle_pct": round(weighted("idle")), "full_pct": round(weighted("full")),
        "rssi_avg": round(weighted("rssi")), "rssi_min": min((n["rssi"] for n in net), default=None),
        "server_backlog_max_kb": max(backlog, default=None), "played_s": played_s,
        "sd_errors": sum(bool(SD_ERROR.search(x)) for x in lines),
    }


def table(rows: list[dict], columns: list[tuple[str, str]]) -> str:
    head = "| " + " | ".join(title for _, title in columns) + " |"
    rule = "|" + "|".join("---" for _ in columns) + "|"
    body = ["| " + " | ".join("—" if r.get(k) is None else str(r.get(k)) for k, _ in columns) + " |" for r in rows]
    return "\n".join([head, rule, *body])


PLAY_COLUMNS = [("label", "run"), ("mean_kbps", "mean kbps"), ("p95_kbps", "p95 kbps"),
                ("first_video_ms", "first video ms"), ("stable_ms", "stable ms"), ("fps", "fps"),
                ("dropped", "dropped"), ("dropped_per_min", "dropped/min"), ("read_stalls", "stalls"),
                ("stall_wait_ms", "stall ms"), ("reconnects", "reconnects"), ("drift_avg", "drift avg"),
                ("drift_max", "drift max"), ("gap_max_ms", "gap max ms"), ("vbuf_min_kb", "buf min KB"),
                ("rssi_avg", "rssi"), ("ping_p50", "ping p50"), ("ping_p95", "ping p95"),
                ("retx_pct", "re-tx %"), ("sd_errors", "SD err")]
BENCH_COLUMNS = [("when", "when"), ("connections", "conns"), ("total_kBps", "total KB/s"),
                 ("per_connection_kBps", "per conn KB/s"), ("min_second_kbps", "worst second kbps"),
                 ("ping_p50", "ping p50"), ("ping_p95", "ping p95"), ("ping_max", "ping max"),
                 ("ping_loss_pct", "loss %"), ("retx_pct", "re-tx %"), ("srtt_ms", "srtt ms")]


def sd_phase(rig: Rig, cycles: int) -> dict:
    """Live and SD alternating (Wi-Fi busy while the SD may be read), then the SD alone."""
    b = rig.board
    rig.leave_remote()
    rig.restart_server(rig.live_env)
    start = len(b.lines)
    for _ in range(cycles):
        b.send(f"T {rig.url}\n")
        b.pump(60)
        b.send("n")
        b.pump(60)
    mixed_end = len(b.lines)
    rig.server.stop()
    for _ in range(20):  # SD only: zaps, then one channel
        b.send("n")
        b.pump(6)
    b.pump(60)
    events = []
    playing = "?"
    last_net = None
    for i in range(start, len(b.lines)):
        line = b.lines[i]
        if "[REMOTE] playback started" in line:
            playing = "live"
        elif LOCAL_TUNE.search(line):
            playing = "SD"
        if m := NET.search(line):
            last_net = m
        if SD_ERROR.search(line):
            events.append({"t_s": round(b.times[i], 1), "phase": "mixed" if i < mixed_end else "SD only",
                           "playing": playing, "rssi": int(last_net["rssi"]) if last_net else None,
                           "network_kbps": int(last_net["kbps"]) if last_net else None, "line": line.strip()[:120]})
    return {"mixed_minutes": cycles * 2, "sd_only_minutes": 3, "errors": events}


def soak_phase(rig: Rig, minutes: float) -> dict:
    b = rig.board
    rig.leave_remote()
    rig.restart_server(rig.live_env)
    mark = b.send(f"T {rig.url}\n")
    b.pump(15)
    b.send("m")
    b.pump(2)
    b.send("S0\n")
    b.pump(minutes * 60)
    b.send("s")
    b.pump(3)
    b.send("m")
    b.pump(3)
    lines = b.lines[mark:]
    soak = [x.strip() for x in lines if "[SOAK]" in x]
    heap = [int(m.group(1)) for x in soak if (m := re.search(r"heap (\d+) KB", x))]
    minimum = [int(m.group(1)) for x in soak if (m := re.search(r"min (\d+),", x))]
    return {"minutes": minutes, "snapshots": soak, "heap_kb_first": heap[0] if heap else None,
            "heap_kb_last": heap[-1] if heap else None, "heap_min_kb": min(minimum) if minimum else None,
            **summarize(lines, [])}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    parser.add_argument("--channel", type=int, default=10)
    parser.add_argument("--phases", default="bench,profiles,livestart")
    parser.add_argument("--profiles", default=",".join(PROFILES))
    parser.add_argument("--profile", default="24fps-q12", help="for livestart, prebuffer, sd and soak")
    parser.add_argument("--live-start", type=int, default=-1, help="PAUTV_LIVE_START_INDEX for every live run")
    parser.add_argument("--minutes", type=float, default=2.5)
    parser.add_argument("--soak-minutes", type=float, default=30)
    parser.add_argument("--sd-cycles", type=int, default=4)
    parser.add_argument("--out", type=Path, default=Path(tempfile.mkdtemp(prefix="pautv-profiles-")))
    args = parser.parse_args()
    phases = args.phases.split(",")
    args.out.mkdir(parents=True, exist_ok=True)
    results: dict = {}
    start_env = {"PAUTV_LIVE_START_INDEX": str(args.live_start)}
    rig = Rig(args)
    try:
        if "cold" in phases:  # before anything warms the link
            row = rig.play(f"{args.profile} cold", rig.live_env, 60, leave=False)
            results["cold"] = [row]
            print("\n## cold\n" + table([row], PLAY_COLUMNS), flush=True)
        if "bench" in phases:  # first thing after joining: the warm-up
            rows = [{"when": f"+{rig.board.times[-1] - rig.online_at:.0f} s after joining", **rig.bench(1)}]
            rig.board.pump(max(0, 40 - (rig.board.times[-1] - rig.online_at)))
            for n in (1, 2, 4):
                rows.append({"when": "warm", **rig.bench(n)})
                rig.board.pump(2)
            results["bench"] = rows
            print("\n## bench\n" + table(rows, BENCH_COLUMNS), flush=True)
        rig.board.pump(max(0, WARM_S - (rig.board.times[-1] - rig.online_at)))  # the rest on a warm link
        if "profiles" in phases:
            rows = [rig.play(p, {"PAUTV_LIVE_PROFILE": p, **start_env}, args.minutes * 60)
                    for p in args.profiles.split(",")]
            results["profiles"] = rows
            print("\n## profiles\n" + table(rows, PLAY_COLUMNS), flush=True)
        if "livestart" in phases:
            rows = [rig.play(f"{args.profile} start {i}", {"PAUTV_LIVE_PROFILE": args.profile,
                                                            "PAUTV_LIVE_START_INDEX": str(i)}, args.minutes * 60)
                    for i in (-1, -3)]
            results["livestart"] = rows
            print("\n## livestart\n" + table(rows, PLAY_COLUMNS), flush=True)
        if "prebuffer" in phases:
            rows = [rig.play(f"{args.profile} prebuffer {ms}", {"PAUTV_LIVE_PROFILE": args.profile,
                                                                 "PAUTV_PREBUFFER_MS": str(ms), **start_env},
                             args.minutes * 60) for ms in (1000, 2000, 3000)]
            results["prebuffer"] = rows
            print("\n## prebuffer\n" + table(rows, PLAY_COLUMNS), flush=True)
        if "sd" in phases:
            results["sd"] = sd_phase(rig, args.sd_cycles)
            print(f"\n## sd\n{len(results['sd']['errors'])} SD errors", flush=True)
            for e in results["sd"]["errors"]:
                print(f"- {e['t_s']} s {e['phase']}, playing {e['playing']}, rssi {e['rssi']}, "
                      f"{e['network_kbps']} kbps: {e['line']}")
        if "soak" in phases:
            results["soak"] = soak_phase(rig, args.soak_minutes)
            s = results["soak"]
            print(f"\n## soak\n{args.soak_minutes} min: heap {s['heap_kb_first']} -> {s['heap_kb_last']} KB, "
                  f"min {s['heap_min_kb']} KB, dropped/min {s['dropped_per_min']}, stalls {s['read_stalls']}, "
                  f"reconnects {s['reconnects']}", flush=True)
            for line in s["snapshots"]:
                print("   ", line)
    finally:
        rig.server.stop()
        (args.out / "serial.log").write_text(
            "\n".join(f"{t:8.1f} {x}" for t, x in zip(rig.board.times, rig.board.lines)))
        if rig.server.log.exists():
            (args.out / "server.log").write_text(rig.server.log.read_text(errors="replace"))
        (args.out / "results.json").write_text(json.dumps(results, indent=1))
    boots = sum("[BOOT] RETROTV" in x for x in rig.board.lines)
    fatal = [x for x in rig.board.lines if FATAL.search(x)]
    print(f"\nboots {boots}, fatal {fatal[:3]}; logs in {args.out}")
    return 0 if boots == 1 and not fatal else 1


if __name__ == "__main__":
    sys.exit(main())
