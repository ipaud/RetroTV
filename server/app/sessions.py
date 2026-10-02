"""Playback sessions: one tune-in of one TV to one channel.

Video and audio travel on two HTTP connections, so they must describe the same instant. The
session fixes it once, when it is created: for files, where the channel is on air (t0) and
both streams start there; for a live source, one FFmpeg process whose two outputs share one
clock (app/ffmpeg.py). A session whose streams have all opened and closed is finished; one
with no stream open for the TTL (never connected, or half gone) is dropped. Either way its
FFmpeg is stopped: a session never outlives its process, nor the other way round."""

from __future__ import annotations

import logging
import secrets
import time
from collections.abc import Callable
from dataclasses import dataclass, field

from .ffmpeg import FfmpegSession
from .providers.base import FileSource, ResolvedSource

log = logging.getLogger("SESSION")


class SessionError(Exception):
    """status: the HTTP status the API answers with."""

    def __init__(self, status: int, detail: str) -> None:
        super().__init__(detail)
        self.status = status
        self.detail = detail


@dataclass
class Session:
    id: str
    channel: int
    source: ResolvedSource
    live: FfmpegSession | None
    created: float
    touched: float  # last create / connect / close
    opened: set[str] = field(default_factory=set)
    open_now: set[str] = field(default_factory=set)

    def kinds(self) -> tuple[str, ...]:
        if self.live is not None:
            return tuple(self.live.outputs)
        assert isinstance(self.source, FileSource)
        return ("video", "audio") if self.source.audio is not None else ("video",)


class SessionManager:
    def __init__(self, ttl_s: float, max_sessions: int, clock: Callable[[], float] = time.monotonic) -> None:
        self._ttl_s = ttl_s
        self._max = max_sessions
        self._clock = clock
        self._sessions: dict[str, Session] = {}

    def create(self, channel: int, source: ResolvedSource, live: FfmpegSession | None = None) -> Session:
        self.expire()
        while len(self._sessions) >= self._max:
            oldest = min(self._sessions.values(), key=lambda s: s.created)
            self._drop(oldest, "too many sessions")
        now = self._clock()
        session = Session(secrets.token_hex(4), channel, source, live, now, now)
        self._sessions[session.id] = session
        where = f"at {source.position_ms} ms" if isinstance(source, FileSource) else "live"
        log.info("%s ready: channel %d %s", session.id, channel, where)
        return session

    def attach(self, session_id: str, kind: str) -> Session:
        """The session this connection streams from. Each stream can be opened once."""
        self.expire()
        session = self._sessions.get(session_id)
        if session is None:
            raise SessionError(404, "unknown or expired session")
        if kind not in session.kinds():
            raise SessionError(404, f"session has no {kind}")
        if kind in session.opened:
            raise SessionError(409, f"{kind} already streamed")
        session.opened.add(kind)
        session.open_now.add(kind)
        session.touched = self._clock()
        log.info("%s %s connected", session_id, kind)
        return session

    def detach(self, session_id: str, kind: str) -> None:
        session = self._sessions.get(session_id)
        if session is None:
            return
        session.open_now.discard(kind)
        session.touched = self._clock()
        log.info("%s %s closed", session_id, kind)
        if not session.open_now and session.opened == set(session.kinds()):
            self._drop(session, "closed")

    def expire(self) -> None:
        now = self._clock()
        for s in list(self._sessions.values()):
            if not s.open_now and now - s.touched > self._ttl_s:
                self._drop(s, "expired: no stream open")

    def _drop(self, session: Session, why: str) -> None:
        del self._sessions[session.id]
        if session.live is not None:
            session.live.close()
        log.info("%s %s", session.id, why)

    def live_for(self, channel: int) -> FfmpegSession | None:
        for s in self._sessions.values():
            if s.channel == channel and s.live is not None:
                return s.live
        return None

    def all(self) -> list[Session]:
        return list(self._sessions.values())

    def count(self, channel: int | None = None) -> int:
        self.expire()
        return sum(1 for s in self._sessions.values() if channel is None or s.channel == channel)

    async def close_all(self) -> None:
        """Server shutdown: no FFmpeg is left behind."""
        lives = [s.live for s in self._sessions.values() if s.live is not None]
        self._sessions.clear()
        for live in lives:
            await live.aclose()
