"""Sends one StreamSpec over HTTP. No pacing: the TV reads at playback speed and TCP flow
control holds the file reads back, so a slow reader never piles data up here."""

from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator

from .providers.base import StreamSpec


async def file_chunks(spec: StreamSpec, chunk_bytes: int) -> AsyncIterator[bytes]:
    with spec.path.open("rb") as f:
        f.seek(spec.offset)
        at_start = spec.offset == 0
        while True:
            data = await asyncio.to_thread(f.read, chunk_bytes)
            if data:
                at_start = False
                yield data
                continue
            if not spec.loop or at_start:  # at_start: an empty file never spins
                return
            f.seek(0)  # loop: MJPEG frames and ADTS frames simply follow on
            at_start = True
