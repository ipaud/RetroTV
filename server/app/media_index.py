"""Reads "<name>.idx" (tools/make_index.py, firmware src/media/EpisodeIndex.h): where each
second of an episode starts in its .mjpeg and .aac. The server uses it the way the TV does:
to start video and audio at the same second."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

MAGIC = b"PAUTVIDX"
VERSION = 1
HEADER = struct.Struct("<8sHHIIHHII")  # 32 bytes
ENTRY = struct.Struct("<II")
NO_AUDIO = 0xFFFFFFFF


@dataclass(frozen=True)
class EpisodeIndex:
    path: Path
    fps: int
    duration_ms: int
    interval: int  # frames per entry
    entries: int

    def entry_for(self, position_ms: int) -> int:
        """The entry at or before position_ms (the last one when past the end)."""
        k = position_ms * self.fps // 1000 // self.interval
        return min(k, self.entries - 1)

    def entry_time_ms(self, k: int) -> int:
        return k * self.interval * 1000 // self.fps

    def offsets(self, k: int) -> tuple[int, int]:
        """(video offset, audio offset) of entry k; the audio one is NO_AUDIO without .aac."""
        with self.path.open("rb") as f:
            f.seek(HEADER.size + k * ENTRY.size)
            data = f.read(ENTRY.size)
        if len(data) != ENTRY.size:
            raise ValueError(f"{self.path}: entry {k} missing")
        return ENTRY.unpack(data)


def read_index(path: Path) -> EpisodeIndex | None:
    """None for anything the TV would not trust either (see parseIndexHeader)."""
    try:
        with path.open("rb") as f:
            head = f.read(HEADER.size)
    except OSError:
        return None
    if len(head) != HEADER.size:
        return None
    magic, version, fps, _frames, duration_ms, interval, _reserved, entries, _rate = HEADER.unpack(head)
    if magic != MAGIC or version != VERSION or fps == 0 or interval == 0 or entries == 0:
        return None
    return EpisodeIndex(path, fps, duration_ms, interval, entries)


def is_jpeg_start(head: bytes) -> bool:
    return len(head) >= 3 and head[0] == 0xFF and head[1] == 0xD8 and head[2] == 0xFF


def is_adts_start(head: bytes) -> bool:
    return len(head) >= 2 and head[0] == 0xFF and (head[1] & 0xF6) == 0xF0
