"""Server settings, from the environment (run.sh sets them)."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

from .ffmpeg import DEFAULT_PROFILE, LIVE_PROFILES, FfmpegSettings

SERVER_DIR = Path(__file__).resolve().parent.parent
PREBUFFER_MS_RANGE = (1000, 3000)  # video the TV buffers before playing: startup vs stability


def live_profile(name: str) -> tuple[int, int]:
    """(fps, q:v) of a named profile; unknown names are a configuration error."""
    if name not in LIVE_PROFILES:
        raise ValueError(f"unknown live profile {name!r}: use one of {', '.join(LIVE_PROFILES)}")
    return LIVE_PROFILES[name]


def live_start_index(value: int) -> int:
    if not -6 <= value <= -1:
        raise ValueError(f"live_start_index {value} outside -6..-1 (segments back from the newest)")
    return value


def check_prebuffer_ms(value: int) -> int:
    low, high = PREBUFFER_MS_RANGE
    if not low <= value <= high:
        raise ValueError(f"prebuffer_ms {value} outside {low}-{high}")
    return value


@dataclass(frozen=True)
class Settings:
    channels_file: Path
    media_root: Path  # relative paths in channels.json resolve against it
    announce: bool = False  # mDNS: retrotv-server.local (only when listening on the LAN)
    port: int = 8080
    session_ttl_s: float = 15.0  # a session no stream opened within this is dropped
    max_sessions: int = 8  # one TV needs one; the oldest goes first
    chunk_bytes: int = 16384
    prebuffer_ms: int = 1500
    ffmpeg: FfmpegSettings = field(default_factory=FfmpegSettings)


def load_settings() -> Settings:
    env = os.environ.get
    fps, quality = live_profile(env("PAUTV_LIVE_PROFILE", DEFAULT_PROFILE))
    return Settings(
        channels_file=Path(env("PAUTV_CHANNELS", str(SERVER_DIR / "config" / "channels.json"))),
        media_root=Path(env("PAUTV_MEDIA_ROOT", str(SERVER_DIR))),
        announce=env("PAUTV_ANNOUNCE", "0") == "1",
        port=int(env("PAUTV_PORT", "8080")),
        prebuffer_ms=check_prebuffer_ms(int(env("PAUTV_PREBUFFER_MS", "1500"))),
        ffmpeg=FfmpegSettings(binary=env("PAUTV_FFMPEG", "ffmpeg"), loudnorm=env("PAUTV_LOUDNORM", "0") == "1",
                              fps=fps, video_quality=int(env("PAUTV_LIVE_QUALITY", str(quality))),
                              live_start_index=live_start_index(int(env("PAUTV_LIVE_START_INDEX", "-3"))),
                              stats_period_s=float(env("PAUTV_STATS_PERIOD_S", "5"))),
    )
