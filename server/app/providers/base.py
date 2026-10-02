"""What every channel source gives the session layer. A provider only finds the media: files
already in the TV's format, or a live HLS source (generic, 3Cat...). Turning HLS into the TV's
format is FfmpegSession's job (app/ffmpeg.py); serving it is the session layer's. Providers
never touch either."""

from __future__ import annotations

import logging
import time
from abc import ABC, abstractmethod
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path


class SourceError(Exception):
    """The channel cannot be played now; the message is the reason the API reports."""


@dataclass(frozen=True)
class StreamSpec:
    """One byte stream the TV reads: raw MJPEG or ADTS AAC, from `offset`, looping or not."""

    path: Path
    offset: int = 0
    loop: bool = True


@dataclass(frozen=True)
class FileSource:
    """Files in the TV's format: video and audio both start at position_ms."""

    position_ms: int
    video: StreamSpec
    audio: StreamSpec | None


@dataclass(frozen=True)
class HlsSource:
    """A live HLS source and what FFmpeg takes from it (stream specifiers of one input)."""

    url: str
    video_map: str
    audio_map: str | None
    source_latency_ms: int | None = None  # age of the live edge when resolved, if the playlist says


ResolvedSource = FileSource | HlsSource


@dataclass(frozen=True)
class Health:
    online: bool
    detail: str = ""


@dataclass(frozen=True)
class Airing:
    """One programme in a channel's guide, as its source publishes it (epoch ms, UTF-8 title)."""

    title: str
    start_ms: int
    end_ms: int


class Provider(ABC):
    name = "provider"

    @abstractmethod
    def health(self) -> Health: ...

    @abstractmethod
    def metadata(self) -> dict: ...

    @abstractmethod
    def resolve(self, now_ms: int) -> ResolvedSource:
        """Where the channel is on air right now. Raises SourceError when it cannot be played."""

    def guide(self, now_ms: int) -> list[Airing]:
        """What is on now and next, for sources that publish it; empty otherwise. Never raises."""
        return []


class HealthCache:
    """Remote providers check their source over the network: once per TTL, not per request."""

    def __init__(self, check: Callable[[], Health], ttl_s: float = 30.0, clock: Callable[[], float] = time.monotonic):
        self._check = check
        self._ttl_s = ttl_s
        self._clock = clock
        self._value: Health | None = None
        self._at = 0.0

    def get(self) -> Health:
        now = self._clock()
        if self._value is None or now - self._at >= self._ttl_s:
            self._value = self._check()
            self._at = now
        return self._value

    def set(self, value: Health) -> None:
        self._value = value
        self._at = self._clock()


class GuideCache:
    """A source's guide, fetched at most once per TTL; a failure is an empty guide until then."""

    def __init__(self, fetch: Callable[[int], list[Airing]], name: str, ttl_s: float = 60.0,
                 clock: Callable[[], float] = time.monotonic):
        self._fetch = fetch
        self._name = name
        self._ttl_s = ttl_s
        self._clock = clock
        self._value: list[Airing] = []
        self._at: float | None = None

    def get(self, now_ms: int) -> list[Airing]:
        now = self._clock()
        if self._at is None or now - self._at >= self._ttl_s:
            try:
                self._value = self._fetch(now_ms)
            except SourceError as e:
                logging.getLogger("GUIDE").warning("%s: %s", self._name, e)
                self._value = []
            self._at = now
        return self._value


class UnavailableProvider(Provider):
    """A channel this server cannot play (unknown provider, bad configuration): listed, offline."""

    def __init__(self, provider: str, reason: str | None = None) -> None:
        self.name = provider
        self._reason = reason or f"provider '{provider}' not available in this version"

    def health(self) -> Health:
        return Health(False, self._reason)

    def metadata(self) -> dict:
        return {"provider": self.name}

    def resolve(self, now_ms: int) -> ResolvedSource:
        raise SourceError(self._reason)
