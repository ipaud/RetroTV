"""The channel list: channels.json, each channel with its provider."""

from __future__ import annotations

import logging
from dataclasses import dataclass
from pathlib import Path

from .models import ChannelConfig, ChannelsFile
from .providers.base import Provider, UnavailableProvider
from .providers.hls import HlsProvider
from .providers.local import LocalProvider
from .providers.pluto import PlutoProvider
from .providers.rtve import RtveProvider
from .providers.threecat import DEFAULT_REGIONS, ThreeCatProvider

log = logging.getLogger("CHANNELS")


@dataclass(frozen=True)
class Channel:
    config: ChannelConfig
    provider: Provider


def _provider_for(c: ChannelConfig, media_root: Path) -> Provider:
    if not c.enabled:
        return UnavailableProvider(c.provider, "disabled in channels.json")
    try:
        if c.provider == "local" and c.source_type == "file" and c.video:
            audio = media_root / c.audio if c.audio else None
            return LocalProvider(media_root / c.video, audio, c.loop)
        if c.provider == "hls" and c.source:
            return HlsProvider(c.source, c.audio_language)
        if c.provider == "3cat" and c.provider_channel:
            return ThreeCatProvider(c.provider_channel, tuple(c.regions or DEFAULT_REGIONS))
        if c.provider == "rtve" and c.provider_channel:
            return RtveProvider(c.provider_channel)
        if c.provider == "pluto" and c.provider_channel:
            return PlutoProvider(c.provider_channel)
    except ValueError as e:  # a configuration mistake: the channel is listed but offline
        return UnavailableProvider(c.provider, f"bad configuration: {e}")
    return UnavailableProvider(c.provider)


class ChannelRegistry:
    def __init__(self, channels: list[Channel]) -> None:
        self._by_number = {c.config.number: c for c in channels}

    @classmethod
    def load(cls, channels_file: Path, media_root: Path) -> "ChannelRegistry":
        parsed = ChannelsFile.model_validate_json(channels_file.read_text(encoding="utf-8"))
        numbers = [c.number for c in parsed.channels]
        if len(numbers) != len(set(numbers)):
            raise ValueError(f"{channels_file}: channel numbers must be unique")
        channels = [Channel(c, _provider_for(c, media_root)) for c in parsed.channels]
        for c in channels:  # remote sources are checked on first use, not at startup
            log.info("%02d %-16s %s", c.config.number, c.config.name, c.config.provider)
        return cls(channels)

    def all(self) -> list[Channel]:
        return sorted(self._by_number.values(), key=lambda c: c.config.number)

    def get(self, number: int) -> Channel | None:
        return self._by_number.get(number)
