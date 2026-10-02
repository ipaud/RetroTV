"""RETROTV Server: channels for the RETROTV set, in the only format it plays (raw MJPEG 320x240
+ ADTS AAC mono), over plain HTTP on the local network. Personal use: never expose it to the
Internet.

    GET  /health                 {"status": "ok"}
    GET  /api/channels           the channel list, with online state
    GET  /api/status/<n>         one channel: provider, health, FFmpeg, where it is on air
    GET  /api/guide              what is on now and next, for the channels whose source says
                                 (the TV's teletext)
    POST /api/sessions/<n>       tune in: fixes t0 (files) or starts FFmpeg (live) and returns
                                 the two stream URLs once there is data
    GET  /session/<id>/video     raw MJPEG from t0
    GET  /session/<id>/audio     ADTS AAC from t0
    GET  /api/debug/sessions     open sessions (development, LAN)
    GET  /api/bench?seconds=N    N s (1-30) of filler bytes as fast as TCP takes them: the TV's raw
                                 throughput (serial command B), nothing decoded (development)

Nothing takes a URL: sources come only from config/channels.json (no proxy, no SSRF).
Start it with server/run.sh (see server/README.md)."""

from __future__ import annotations

import asyncio
import logging
import os
import time
from collections.abc import AsyncIterator, Callable
from contextlib import asynccontextmanager
from dataclasses import replace

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import StreamingResponse

from . import mdns
from .channels import Channel, ChannelRegistry
from .config import Settings, live_profile, load_settings
from .ffmpeg import FfmpegError, FfmpegSession
from .models import SessionInfo
from .providers.base import FileSource, SourceError
from .sessions import SessionError, SessionManager
from .streaming import file_chunks

VERSION = "0.2.0-alpha2"
MEDIA_TYPES = {"video": "video/x-motion-jpeg", "audio": "audio/aac"}
FILE_FPS = 24  # local files are converted at 24 fps (tools/convert_video.sh)
BENCH_MAX_SECONDS = 30
BENCH_MAX_STREAMS = 4  # the TV's bench opens up to 4 connections
BENCH_CHUNK = os.urandom(16384)  # incompressible, like MJPEG
GUIDE_AIRINGS = 3  # now and the next two: the teletext shows no more
GUIDE_TITLE_LEN = 64  # the teletext cuts at 40 columns anyway

log = logging.getLogger("SERVER")


async def start_unless_tv_leaves(live: FfmpegSession, request: Request) -> bool:
    """Starts FFmpeg, but gives up at once (and stops it) if the TV hangs up meanwhile: it zapped
    away, and a session nobody will open must not keep an FFmpeg running. FfmpegError passes."""

    async def hang_up() -> None:
        while (await request.receive())["type"] != "http.disconnect":
            pass

    start = asyncio.ensure_future(live.start())
    gone = asyncio.ensure_future(hang_up())
    try:
        await asyncio.wait({start, gone}, return_when=asyncio.FIRST_COMPLETED)
    finally:
        gone.cancel()
    if start.done():
        try:
            start.result()
        except FfmpegError as e:
            raise HTTPException(503, f"offline: {e.reason}") from e
        return True
    start.cancel()
    await live.aclose()
    return False


def wall_clock_ms() -> int:
    return time.time_ns() // 1_000_000


def create_app(
    settings: Settings | None = None,
    now_ms: Callable[[], int] = wall_clock_ms,
    clock: Callable[[], float] = time.monotonic,
) -> FastAPI:
    settings = settings or load_settings()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(name)s] %(message)s", datefmt="%H:%M:%S")
    registry = ChannelRegistry.load(settings.channels_file, settings.media_root)
    sessions = SessionManager(settings.session_ttl_s, settings.max_sessions, clock)

    @asynccontextmanager
    async def lifespan(_app: FastAPI) -> AsyncIterator[None]:
        stop = await mdns.announce(settings.port) if settings.announce else None
        yield
        await sessions.close_all()  # no FFmpeg outlives the server
        if stop:
            await stop()

    app = FastAPI(title="RETROTV Server", version=VERSION, lifespan=lifespan)

    def channel_or_404(number: int):
        channel = registry.get(number)
        if channel is None:
            raise HTTPException(404, f"no channel {number}")
        return channel

    @app.get("/health")
    async def health() -> dict:
        return {"status": "ok"}

    async def health_of(channel: Channel):
        return await asyncio.to_thread(channel.provider.health)  # remote providers use the network

    @app.get("/api/channels")
    async def channels() -> dict:
        healths = await asyncio.gather(*(health_of(c) for c in registry.all()))
        return {
            "channels": [
                {"id": c.config.id, "number": c.config.number, "name": c.config.name,
                 "provider": c.config.provider, "online": h.online}
                for c, h in zip(registry.all(), healths)
            ]
        }

    @app.get("/api/status/{number}")
    async def status(number: int) -> dict:
        channel = channel_or_404(number)
        health = await health_of(channel)
        answer = {"number": number, "name": channel.config.name, "online": health.online,
                  **channel.provider.metadata()}
        if not health.online:
            return {**answer, "reason": health.detail}
        answer["sessions"] = sessions.count(number)
        live = sessions.live_for(number)
        if answer.get("source") == "hls":
            answer.update(live.stats() if live else {"ffmpeg": "idle"})
        else:
            source = await asyncio.to_thread(channel.provider.resolve, now_ms())
            answer["position_ms"] = source.position_ms if isinstance(source, FileSource) else None
        return answer

    @app.get("/api/guide")
    async def guide() -> dict:
        now = now_ms()
        guides = await asyncio.gather(*(asyncio.to_thread(c.provider.guide, now) for c in registry.all()))
        channels = []
        for c, airings in zip(registry.all(), guides):
            upcoming = [a for a in airings if a.end_ms > now][:GUIDE_AIRINGS]
            if upcoming:
                channels.append({"number": c.config.number, "airings": [
                    {"title": a.title[:GUIDE_TITLE_LEN], "start_ms": a.start_ms, "end_ms": a.end_ms} for a in upcoming]})
        return {"now_ms": now, "channels": channels}

    # Handlers are async on purpose: sessions are only touched from the event loop thread.
    @app.post("/api/sessions/{number}")
    async def create_session(number: int, request: Request) -> SessionInfo:
        await request.body()  # read now: from here on, receive() only reports a hang-up
        channel = channel_or_404(number)
        try:  # resolving checks the source too (and refreshes its cached health)
            source = await asyncio.to_thread(channel.provider.resolve, now_ms())
        except SourceError as e:
            raise HTTPException(503, f"channel {number} offline: {e}") from e
        live = None
        fps = FILE_FPS
        if not isinstance(source, FileSource):  # live: never answer before FFmpeg produces data
            ffmpeg = settings.ffmpeg
            if channel.config.profile:
                fps, quality = live_profile(channel.config.profile)
                ffmpeg = replace(ffmpeg, fps=fps, video_quality=quality)
            if channel.config.video_quality:
                ffmpeg = replace(ffmpeg, video_quality=channel.config.video_quality)
            if channel.config.crop_4_3:
                ffmpeg = replace(ffmpeg, crop_4_3=True)
            fps = ffmpeg.fps
            live = FfmpegSession(source, ffmpeg, name=f"ch{number}")
            if not await start_unless_tv_leaves(live, request):
                log.info("channel %d: the TV left while FFmpeg was starting", number)
                raise HTTPException(499, "client closed the request")
        session = sessions.create(number, source, live)
        return SessionInfo(
            session_id=session.id,
            channel=number,
            position_ms=source.position_ms if isinstance(source, FileSource) else 0,
            video=f"/session/{session.id}/video",
            audio=f"/session/{session.id}/audio" if "audio" in session.kinds() else None,
            fps=fps,
            prebuffer_ms=channel.config.prebuffer_ms or settings.prebuffer_ms,
        )

    def stream(session_id: str, kind: str) -> StreamingResponse:
        try:
            session = sessions.attach(session_id, kind)
        except SessionError as e:
            raise HTTPException(e.status, e.detail) from e
        if session.live is not None:
            chunks = session.live.read(kind)
        else:
            assert isinstance(session.source, FileSource)
            spec = session.source.video if kind == "video" else session.source.audio
            assert spec is not None
            chunks = file_chunks(spec, settings.chunk_bytes)

        async def body() -> AsyncIterator[bytes]:
            try:
                async for chunk in chunks:
                    yield chunk
            finally:  # also when the TV hangs up: the response task is cancelled
                sessions.detach(session_id, kind)

        return StreamingResponse(body(), media_type=MEDIA_TYPES[kind], headers={"Cache-Control": "no-store"})

    @app.get("/api/debug/sessions")
    async def debug_sessions() -> list[dict]:
        now = time.monotonic()
        return [
            {"id": s.id, "channel": s.channel, "provider": registry.get(s.channel).config.provider,
             "age_s": int(now - s.created), "video_clients": int("video" in s.open_now),
             "audio_clients": int("audio" in s.open_now), "ffmpeg_pid": s.live.pid if s.live else None}
            for s in sessions.all()
        ]

    bench_streams = 0

    @app.get("/api/bench")
    async def bench(seconds: float = 12.0) -> StreamingResponse:
        nonlocal bench_streams
        if not 1 <= seconds <= BENCH_MAX_SECONDS:
            raise HTTPException(422, f"seconds must be 1-{BENCH_MAX_SECONDS}")
        if bench_streams >= BENCH_MAX_STREAMS:  # ponytail: soft cap, requests racing in together can pass it
            raise HTTPException(429, "too many bench streams")

        async def body() -> AsyncIterator[bytes]:
            nonlocal bench_streams
            bench_streams += 1  # counted here: a body that never starts never leaks a slot
            end = time.monotonic() + seconds
            try:
                while time.monotonic() < end:
                    yield BENCH_CHUNK
            finally:
                bench_streams -= 1

        return StreamingResponse(body(), media_type="application/octet-stream", headers={"Cache-Control": "no-store"})

    @app.get("/session/{session_id}/video")
    async def video(session_id: str) -> StreamingResponse:
        return stream(session_id, "video")

    @app.get("/session/{session_id}/audio")
    async def audio(session_id: str) -> StreamingResponse:
        return stream(session_id, "audio")

    return app
