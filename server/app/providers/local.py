"""A channel from files already in the TV's format (tools/convert_video.sh): a .mjpeg, an
optional .aac and, for "on air", the .idx next to them. With the index the channel is already
running when you tune in: position = now modulo its length, like the TV does from the SD."""

from __future__ import annotations

from pathlib import Path

from ..media_index import NO_AUDIO, is_adts_start, is_jpeg_start, read_index
from .base import FileSource, Health, Provider, SourceError, StreamSpec


def _head(path: Path, offset: int) -> bytes:
    with path.open("rb") as f:
        f.seek(offset)
        return f.read(3)


class LocalProvider(Provider):
    name = "local"

    def __init__(self, video: Path, audio: Path | None, loop: bool = True) -> None:
        self.video = video
        self.audio = audio
        self.loop = loop

    def health(self) -> Health:
        for label, path in (("video", self.video), ("audio", self.audio)):
            if path is None:
                continue
            if not path.is_file():
                return Health(False, f"{label} file missing: {path}")
            if path.stat().st_size == 0:
                return Health(False, f"{label} file empty: {path}")
        return Health(True)

    def metadata(self) -> dict:
        index = read_index(self.video.with_suffix(".idx"))
        return {  # no file paths: this answer travels over the LAN
            "provider": "local",
            "source": "file",
            "audio": self.audio is not None,
            "indexed": index is not None,
            "duration_ms": index.duration_ms if index else None,
        }

    def resolve(self, now_ms: int) -> FileSource:
        health = self.health()
        if not health.online:
            raise SourceError(health.detail)
        position, video_at, audio_at = self._on_air(now_ms)
        audio = StreamSpec(self.audio, audio_at, self.loop) if self.audio else None
        return FileSource(position, StreamSpec(self.video, video_at, self.loop), audio)

    def _on_air(self, now_ms: int) -> tuple[int, int, int]:
        """(position, video offset, audio offset). Any doubt about the index: from the start."""
        index = read_index(self.video.with_suffix(".idx"))
        if index is None or index.duration_ms == 0 or not self.loop:
            return 0, 0, 0
        k = index.entry_for(now_ms % index.duration_ms)
        video_at, audio_at = index.offsets(k)
        if not is_jpeg_start(_head(self.video, video_at)):
            return 0, 0, 0
        if self.audio is not None:
            if audio_at == NO_AUDIO or not is_adts_start(_head(self.audio, audio_at)):
                return 0, 0, 0
        return index.entry_time_ms(k), video_at, audio_at if self.audio else 0
