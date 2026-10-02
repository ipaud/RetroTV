from __future__ import annotations

from pathlib import Path

from app.media_index import is_adts_start, is_jpeg_start, read_index
from conftest import ADTS_BYTES, FPS, FRAME_BYTES, write_episode


def test_index_entries(tmp_path: Path) -> None:
    write_episode(tmp_path, "ep")
    index = read_index(tmp_path / "ep.idx")
    assert index is not None and index.duration_ms == 3000 and index.entries == 3
    assert index.entry_for(0) == 0 and index.entry_for(1999) == 1 and index.entry_for(99999) == 2
    assert index.entry_time_ms(2) == 2000
    assert index.offsets(1) == (FPS * FRAME_BYTES, ADTS_BYTES)


def test_untrusted_index(tmp_path: Path) -> None:
    assert read_index(tmp_path / "missing.idx") is None
    (tmp_path / "short.idx").write_bytes(b"PAUTVIDX")
    assert read_index(tmp_path / "short.idx") is None
    write_episode(tmp_path, "ep")
    raw = bytearray((tmp_path / "ep.idx").read_bytes())
    raw[0:8] = b"NOTANIDX"
    (tmp_path / "bad.idx").write_bytes(bytes(raw))
    assert read_index(tmp_path / "bad.idx") is None


def test_stream_starts() -> None:
    assert is_jpeg_start(b"\xff\xd8\xff") and not is_jpeg_start(b"\xff\xd8")
    assert is_adts_start(b"\xff\xf1") and not is_adts_start(b"\xff\xe1")
