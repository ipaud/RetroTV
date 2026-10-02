#!/usr/bin/env python3
"""Stands in for ffmpeg in the failure tests. FAKE_FFMPEG picks the behaviour:
  fail404   print an HTTP 404 error and exit 1 at once
  silent    produce nothing, run until killed
  stubborn  produce nothing and ignore SIGTERM (needs SIGKILL)
  drop      write a JPEG and an ADTS frame on its pipes, then exit 1 (a source that drops)
  flood     write to the video pipe as fast as it can
  stall     write a JPEG and an ADTS frame, then nothing, without exiting (a source that stopped)
  slow      nothing for 2.5 s (a remote source starting), then a JPEG and an ADTS frame every 40 ms
"""
import os
import signal
import sys
import time

mode = os.environ.get("FAKE_FFMPEG", "silent")
fds = [int(a.split(":")[1]) for a in sys.argv if a.startswith("pipe:")]
if mode == "fail404":
    print("[https @ 0x1] HTTP error 404 Not Found", file=sys.stderr, flush=True)
    sys.exit(1)
if mode == "stubborn":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
if mode == "drop":
    os.write(fds[0], b"\xff\xd8\xff\xe0" + b"x" * 100 + b"\xff\xd9")
    if len(fds) > 1:
        os.write(fds[1], b"\xff\xf1" + b"a" * 30)
    time.sleep(0.3)
    print("[hls @ 0x1] Failed to reload playlist", file=sys.stderr, flush=True)
    sys.exit(1)
if mode == "stall":
    os.write(fds[0], b"\xff\xd8\xff\xe0" + b"x" * 100 + b"\xff\xd9")
    if len(fds) > 1:
        os.write(fds[1], b"\xff\xf1" + b"a" * 30)
if mode == "slow":
    time.sleep(2.5)
    while True:
        os.write(fds[0], b"\xff\xd8\xff\xe0" + b"x" * 100 + b"\xff\xd9")
        if len(fds) > 1:
            os.write(fds[1], b"\xff\xf1" + b"a" * 30)
        time.sleep(0.04)
if mode == "flood":
    while True:
        os.write(fds[0], b"\xff\xd8" + b"v" * 65536)
while True:
    time.sleep(1)
