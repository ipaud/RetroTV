#!/usr/bin/env python3
"""Logs a battery discharge through the TV's web API and proposes the voltage curve.

Charge the LiPo full, unplug the USB-C, start a channel and run this on a computer in the same
Wi-Fi (keep it awake: `caffeinate -i tools/battery_log.py ...`). It reads /api/state every
--every seconds into a CSV until the TV stops answering for --gone seconds (it went to standby
on a flat cell), then prints the run time and, for each point of the curve in
src/power/Battery.h, the voltage at which that much of the run was left.

Usage:
  tools/battery_log.py <http://retrotv.local> <log.csv> [--every 30] [--gone 180]
  tools/battery_log.py --report <log.csv>      # the summary again, from a saved log
  tools/battery_log.py --self-test
"""
import argparse
import csv
import json
import sys
import time
import urllib.request
from typing import List, Tuple

CURVE_PCT = [0, 5, 10, 25, 35, 45, 55, 62, 75, 88, 100]  # the points of batteryPercent() in Battery.h

Row = Tuple[float, int, int]  # seconds since the start, battery_mv, battery %


def propose_curve(rows: List[Row]) -> List[Tuple[int, int]]:
    """(pct, mV) for each curve point: the reading when that fraction of the run was still left."""
    rows = [r for r in rows if r[1] > 0]
    if len(rows) < 2:
        return []
    t0, t_end = rows[0][0], rows[-1][0]
    out = []
    for pct in CURVE_PCT:
        t = t_end - (t_end - t0) * pct / 100
        closest = min(rows, key=lambda r: abs(r[0] - t))
        out.append((pct, closest[1]))
    return out


def report(rows: List[Row]) -> None:
    if len(rows) < 2:
        print("not enough readings")
        return
    hours = (rows[-1][0] - rows[0][0]) / 3600
    print(f"run time {hours:.2f} h, {rows[0][1]} -> {rows[-1][1]} mV, shown {rows[0][2]} -> {rows[-1][2]} %")
    print("proposed curve (Battery.h): " + ", ".join(f"{{{mv}, {pct}}}" for pct, mv in propose_curve(rows)))


def read_csv(path: str) -> List[Row]:
    with open(path) as f:
        return [(float(r["t_s"]), int(r["mv"] or 0), int(r["pct"] or 0)) for r in csv.DictReader(f)]


def log(base: str, path: str, every: float, gone: float) -> List[Row]:
    rows: List[Row] = []
    start = last_ok = time.time()
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["time", "t_s", "mv", "pct", "screen"])
        while time.time() - last_ok < gone:
            try:
                s = json.loads(urllib.request.urlopen(base.rstrip("/") + "/api/state", timeout=5).read())
                last_ok = time.time()
                row = (round(last_ok - start, 1), s.get("battery_mv") or 0, s.get("battery") or 0)
                rows.append(row)
                w.writerow([time.strftime("%H:%M:%S"), *row, s.get("screen")])
                f.flush()
                print(f"{time.strftime('%H:%M:%S')}  {row[1]} mV  {row[2]} %  {s.get('screen')}", flush=True)
            except Exception as e:  # the TV is busy or gone: keep trying until --gone
                print(f"{time.strftime('%H:%M:%S')}  no answer ({e.__class__.__name__})", flush=True)
            time.sleep(every)
    print("the TV stopped answering: standby or off")
    return rows


def self_test() -> None:
    # A linear discharge 4200 -> 3400 mV over 10 h: half the run left at 3800 mV.
    rows = [(t * 60.0, 4200 - t * 800 // 600, 0) for t in range(601)]
    curve = dict(propose_curve(rows))
    assert curve[100] == 4200 and curve[0] == 3400 and abs(curve[55] - 3840) <= 2, curve
    assert propose_curve(rows[:1]) == []
    print("battery_log self-test: OK")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("url", nargs="?")
    p.add_argument("csv", nargs="?")
    p.add_argument("--every", type=float, default=30)
    p.add_argument("--gone", type=float, default=180)
    p.add_argument("--report")
    p.add_argument("--self-test", action="store_true")
    a = p.parse_args()
    if a.self_test:
        self_test()
    elif a.report:
        report(read_csv(a.report))
    elif a.url and a.csv:
        report(log(a.url, a.csv, a.every, a.gone))
    else:
        p.error("give <url> <log.csv>, --report <log.csv> or --self-test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
