"""channels.json and API shapes."""

from __future__ import annotations

from pydantic import BaseModel, field_validator

from .config import check_prebuffer_ms, live_profile


class ChannelConfig(BaseModel):
    id: int
    number: int
    name: str
    provider: str  # "local", "hls", "3cat"; "rtve", "tunarr" later
    source_type: str = "file"
    enabled: bool = True
    # local files
    video: str | None = None
    audio: str | None = None
    loop: bool = True
    # hls: the source URL (only URLs from this file are ever fetched)
    source: str | None = None
    audio_language: str | None = None
    video_quality: int | None = None  # live MJPEG -q:v for this channel (default: the profile's)
    profile: str | None = None  # live profile for this channel (default: the server's)
    prebuffer_ms: int | None = None  # video the TV buffers before playing (default: the server's)
    crop_4_3: bool = False  # live: a 4:3 picture inside a 16:9 frame with side bars
    # 3cat: 3Cat's channel code ("sx3", "tv3", ...) and the regions to ask for, in order
    provider_channel: str | None = None
    regions: list[str] | None = None


    @field_validator("profile")
    @classmethod
    def _known_profile(cls, value: str | None) -> str | None:
        if value is not None:
            live_profile(value)
        return value

    @field_validator("prebuffer_ms")
    @classmethod
    def _prebuffer_in_range(cls, value: int | None) -> int | None:
        return None if value is None else check_prebuffer_ms(value)


class ChannelsFile(BaseModel):
    channels: list[ChannelConfig]


class SessionInfo(BaseModel):
    session_id: str
    channel: int
    position_ms: int
    video: str
    audio: str | None
    fps: int  # the TV paces the video at this rate
    prebuffer_ms: int
