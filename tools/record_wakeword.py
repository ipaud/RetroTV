#!/usr/bin/env python3
"""Records wake word samples through the TV's own microphone (docs/WAKEWORD.md, «Hey Retro»).

The TV must be in STANDBY VOZ with the standby app (voice_ww builds): serial A makes it send the microphone
over the USB (frames of "PCM" + sequence byte + 512 samples, 16 kHz, 16-bit). Nothing is kept on the TV.

    tools/record_wakeword.py record OUT.wav --seconds 480 [--port /dev/cu.usbmodem101]
    tools/record_wakeword.py split OUT.wav CLIPS_DIR      # one WAV per phrase, cut at the pauses
    tools/record_wakeword.py --self-test

Recordings are training data: keep them outside the repository.
"""

import math
import struct
import sys
import time
import wave
from pathlib import Path

RATE = 16000
FRAME_SAMPLES = 512
FRAME_BYTES = 4 + 2 * FRAME_SAMPLES


def frames_from(buf: bytearray):
    """Takes every whole frame out of buf (in place): yields (seq, pcm bytes). Text and broken frames are skipped."""
    while True:
        i = buf.find(b"PCM")
        if i < 0:
            del buf[: max(0, len(buf) - 2)]
            return
        if len(buf) - i < FRAME_BYTES:
            del buf[:i]
            return
        nxt = buf.find(b"PCM", i + 3, i + FRAME_BYTES)
        if nxt >= 0:  # cut short: resync on the next header
            del buf[:nxt]
            continue
        yield buf[i + 3], bytes(buf[i + 4 : i + FRAME_BYTES])
        del buf[: i + FRAME_BYTES]


def record(out: Path, seconds: float, port: str) -> int:
    import serial

    s = serial.Serial(port, 115200, timeout=0.1)
    s.write(b"A")
    buf, pcm, last, gaps = bytearray(), bytearray(), None, 0
    t0 = time.time()
    shown = 0
    try:
        while time.time() - t0 < seconds:
            buf += s.read(8192)
            for seq, data in frames_from(buf):
                if last is not None and seq != (last + 1) & 0xFF:
                    gaps += (seq - last - 1) & 0xFF
                    pcm += bytes(2 * FRAME_SAMPLES * ((seq - last - 1) & 0xFF))  # keep the timing
                last = seq
                pcm += data
            if time.time() - t0 >= shown + 30:
                shown += 30
                print(f"{shown} s recorded", flush=True)
    finally:
        s.write(b"A")
        s.close()
    with wave.open(str(out), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(pcm))
    print(f"{out}: {len(pcm) / 2 / RATE:.1f} s, {gaps} frames lost")
    return 0


def segments(samples, rate=RATE):
    """(start, end) sample ranges of the phrases: 10 ms energy over the room floor + 12 dB, pauses >= 300 ms."""
    win = rate // 100
    db = []
    for k in range(0, len(samples) - win + 1, win):
        e = sum(x * x for x in samples[k : k + win]) / win
        db.append(10 * math.log10(e + 1.0))
    if not db:
        return []
    floor = sorted(db)[len(db) // 5]
    loud = [d > floor + 12 for d in db]
    out, start, quiet = [], None, 0
    for k, on in enumerate(loud + [False] * 30):
        if on:
            start = k if start is None else start
            quiet = 0
        elif start is not None:
            quiet += 1
            if quiet >= 30:
                end = k - quiet + 1
                if 30 <= end - start <= 200:  # 0.3-2 s: a phrase, not a click or a conversation
                    out.append((max(0, (start - 25) * win), min(len(samples), (end + 25) * win)))
                start, quiet = None, 0
    return out


def split(wav_path: Path, out_dir: Path) -> int:
    with wave.open(str(wav_path)) as w:
        samples = struct.unpack(f"<{w.getnframes()}h", w.readframes(w.getnframes()))
    out_dir.mkdir(parents=True, exist_ok=True)
    found = segments(samples)
    for n, (a, b) in enumerate(found):
        with wave.open(str(out_dir / f"{wav_path.stem}_{n:04d}.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(struct.pack(f"<{b - a}h", *samples[a:b]))
    print(f"{len(found)} phrases -> {out_dir}")
    return 0


def self_test() -> int:
    frame = lambda seq: b"PCM" + bytes([seq]) + bytes(2 * FRAME_SAMPLES)
    buf = bytearray(b"[STANDBY] text\n" + frame(7) + b"PCM" + bytes(10) + frame(8) + frame(9)[:100])
    got = [seq for seq, _ in frames_from(buf)]
    assert got == [7, 8], got
    assert buf.startswith(b"PCM") and len(buf) == 100, len(buf)
    quiet = [0] * RATE
    burst = [int(8000 * math.sin(i / 3)) for i in range(int(0.8 * RATE))]
    samples = quiet + burst + quiet + burst + quiet + [9000] * 40 + quiet  # two phrases and a click
    assert len(segments(samples)) == 2, segments(samples)
    print("record_wakeword self-test: OK")
    return 0


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["--self-test"]:
        sys.exit(self_test())
    if len(a) >= 2 and a[0] == "record":
        port = a[a.index("--port") + 1] if "--port" in a else "/dev/cu.usbmodem101"
        secs = float(a[a.index("--seconds") + 1]) if "--seconds" in a else 480
        sys.exit(record(Path(a[1]), secs, port))
    if len(a) == 3 and a[0] == "split":
        sys.exit(split(Path(a[1]), Path(a[2])))
    print(__doc__)
    sys.exit(2)
