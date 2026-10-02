#!/usr/bin/env python3
"""Writes <name>.idx next to each RETROTV episode (<name>.mjpeg + <name>.aac).

The index lets the TV start an episode at any second, so a channel is "already on air" when
you tune in: one entry per second with the byte offset of the MJPEG frame and of the AAC
(ADTS) frame playing at that moment. Binary layout: src/media/EpisodeIndex.h.

Usage:
  tools/make_index.py <file.mjpeg | folder> [--fps 20] [--force]
  tools/make_index.py --self-test

Folders are searched recursively (hidden files skipped). An index is written when it is
missing or older than its episode; --force rewrites it anyway. Written as .part and renamed.
"""
import argparse
import mmap
import os
import struct
import sys
import tempfile
from typing import Iterator, Optional, Tuple, Union

Bytes = Union[bytes, bytearray, mmap.mmap]

MAGIC = b"PAUTVIDX"
VERSION = 1
HEADER = struct.Struct("<8sHHIIHHII")  # 32 bytes, see EpisodeIndex.h
ENTRY = struct.Struct("<II")
NO_AUDIO = 0xFFFFFFFF
AAC_RATES = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]
SOI = b"\xff\xd8\xff"  # every JPEG starts FF D8 FF; FF D8 cannot occur inside JPEG data


def frame_offsets(data: Bytes) -> list:
    offsets = []
    i = data.find(SOI)
    while i != -1:
        offsets.append(i)
        i = data.find(SOI, i + 2)
    return offsets


def adts_frames(data: Bytes) -> Tuple[list, int]:
    """[(sample position, byte offset)] of every ADTS frame, and the sample rate."""
    frames, rate, samples, i, n = [], 0, 0, 0, len(data)
    while i + 7 <= n:
        if data[i] != 0xFF or (data[i + 1] & 0xF6) != 0xF0:  # 0xFFF sync, layer 0
            j = data.find(b"\xff", i + 1)
            if j == -1:
                break
            i = j
            continue
        rate_index = (data[i + 2] >> 2) & 0x0F
        length = ((data[i + 3] & 0x03) << 11) | (data[i + 4] << 3) | (data[i + 5] >> 5)
        if length < 7 or rate_index >= len(AAC_RATES):
            i += 1
            continue
        rate = rate or AAC_RATES[rate_index]
        frames.append((samples, i))
        samples += 1024 * ((data[i + 6] & 0x03) + 1)
        i += length
    return frames, rate


def build_index(video: Bytes, audio: Optional[Bytes], fps: int = 24, interval: int = 24) -> Tuple[bytes, int, int]:
    frames = frame_offsets(video)
    if not frames:
        raise ValueError("no JPEG frames found")
    audio_frames, rate = adts_frames(audio) if audio is not None else ([], 0)

    entries, a = [], 0
    for first in range(0, len(frames), interval):
        # Audio frame that starts at or before this video frame: samples * fps <= frame * rate.
        while a + 1 < len(audio_frames) and audio_frames[a + 1][0] * fps <= first * rate:
            a += 1
        audio_offset = audio_frames[a][1] if audio_frames else NO_AUDIO
        entries.append((frames[first], audio_offset))

    duration_ms = len(frames) * 1000 // fps
    head = HEADER.pack(MAGIC, VERSION, fps, len(frames), duration_ms, interval, 0, len(entries), rate)
    return head + b"".join(ENTRY.pack(v, au) for v, au in entries), duration_ms, len(entries)


def read_mapped(path: str) -> Bytes:
    with open(path, "rb") as f:
        if os.fstat(f.fileno()).st_size == 0:
            return b""
        return mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)


def indexed_fps(idx: str) -> Optional[int]:
    """The frame rate an existing index was written with, or None."""
    try:
        with open(idx, "rb") as f:
            magic, version, fps, *_ = HEADER.unpack(f.read(HEADER.size))
    except (OSError, struct.error):
        return None
    return fps if magic == MAGIC and version == VERSION and fps > 0 else None


def index_episode(mjpeg: str, fps: Optional[int], force: bool) -> bool:
    stem = mjpeg[: -len(".mjpeg")]
    aac, idx = stem + ".aac", stem + ".idx"
    sources = [p for p in (mjpeg, aac) if os.path.exists(p)]
    if not force and os.path.exists(idx) and os.path.getmtime(idx) >= max(map(os.path.getmtime, sources)):
        print(f"skip     {os.path.basename(idx)} (up to date)")
        return True
    fps = fps or indexed_fps(idx) or 24  # re-indexing without --fps keeps the episode's rate
    try:
        video = read_mapped(mjpeg)
        audio = read_mapped(aac) if os.path.exists(aac) else None
        data, duration_ms, entries = build_index(video, audio, fps, fps)  # one entry per second
    except (OSError, ValueError) as e:
        print(f"FAILED   {os.path.basename(mjpeg)}: {e}")
        return False
    with open(idx + ".part", "wb") as f:
        f.write(data)
    os.replace(idx + ".part", idx)
    minutes, seconds = divmod(duration_ms // 1000, 60)
    print(f"index    {os.path.basename(idx)} ({entries} entries, {minutes}:{seconds:02d}"
          f"{'' if audio is not None else ', no audio'})")
    return True


def episodes_in(path: str) -> Iterator[str]:
    if os.path.isfile(path):
        yield path
        return
    for root, dirs, files in os.walk(path):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(files):
            if not name.startswith(".") and name.lower().endswith(".mjpeg"):
                yield os.path.join(root, name)


# ---------------------------------------------------------------------------------------------

def self_test() -> None:
    fps, interval = 10, 10
    video = bytearray(b"junk")
    starts = []
    for i in range(35):  # 3.5 s: 4 entries
        starts.append(len(video))
        video += b"\xff\xd8\xff\xe0" + bytes([i]) * (20 + i) + b"\xff\x00\xff\xd9"
    audio = bytearray()
    audio_starts = []
    for i in range(160):  # 1024 samples at 44.1 kHz each: ~3.7 s
        audio_starts.append(len(audio))
        length = 7 + 30
        header = bytes([0xFF, 0xF1, (1 << 6) | (4 << 2), 0x40 | (length >> 11),
                        (length >> 3) & 0xFF, ((length & 7) << 5) | 0x1F, 0xFC])
        audio += header + bytes(30)

    data, duration_ms, entries = build_index(bytes(video), bytes(audio), fps, interval)
    magic, version, h_fps, frames, h_duration, h_interval, _, h_entries, rate = HEADER.unpack_from(data)
    assert magic == MAGIC and version == VERSION and h_fps == fps and h_interval == interval
    assert frames == 35 and h_duration == duration_ms == 3500 and h_entries == entries == 4 and rate == 44100
    assert len(data) == HEADER.size + 4 * ENTRY.size
    for k in range(4):
        v, a = ENTRY.unpack_from(data, HEADER.size + k * ENTRY.size)
        assert v == starts[k * interval], (k, v)
        t = k * interval / fps  # seconds
        frame = int(t * 44100 // 1024)  # audio frame that started at or before t
        assert a == audio_starts[frame], (k, a, audio_starts[frame])
    _, _, _ = build_index(bytes(video), None, fps, interval)  # silent episode
    assert ENTRY.unpack_from(build_index(bytes(video), None, fps, interval)[0], HEADER.size)[1] == NO_AUDIO

    with tempfile.TemporaryDirectory() as d:  # file handling: write, skip when fresh, .part gone
        mjpeg = os.path.join(d, "ep.mjpeg")
        with open(mjpeg, "wb") as f:
            f.write(video)
        assert index_episode(mjpeg, fps, False) and os.path.exists(os.path.join(d, "ep.idx"))
        assert index_episode(mjpeg, fps, False)
        assert not os.path.exists(os.path.join(d, "ep.idx.part"))
        # Re-converted episode (newer than its index): the index is rewritten, not skipped.
        idx = os.path.join(d, "ep.idx")
        with open(idx, "rb") as f:
            before = f.read()
        with open(mjpeg, "wb") as f:
            f.write(b"\xff\xd8\xff\xe0" + bytes(500) + b"\xff\xd9" + bytes(video))
        stamp = os.path.getmtime(idx) + 10
        os.utime(mjpeg, (stamp, stamp))
        assert index_episode(mjpeg, fps, False)
        with open(idx, "rb") as f:
            assert f.read() != before, "stale index was not rewritten"
        assert index_episode(mjpeg, None, True) and indexed_fps(idx) == fps  # no --fps: keeps it
    print("make_index self-test: OK")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("path", nargs="?", help=".mjpeg file or folder (searched recursively)")
    parser.add_argument("--fps", type=int, help="frame rate of the episodes (default: the one in their index, else 24)")
    parser.add_argument("--force", action="store_true", help="rewrite indexes that look up to date")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.path or not os.path.exists(args.path):
        parser.error("give an existing .mjpeg file or folder")
    results = [index_episode(p, args.fps, args.force) for p in episodes_in(args.path)]
    if not results:
        print("no .mjpeg episodes found")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
