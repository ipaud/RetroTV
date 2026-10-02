"""FFmpeg live transcoding: one process per session turns a live HLS source into the TV's two
streams. Every subprocess of the server is started here and nowhere else.

    ffmpeg -i <hls> -+- video: fps, scale + pad 320x240, MJPEG q:v -> pipe:<fd A> -> buffer -> /video
                     +- audio: 44.1 kHz, mono AAC 32k ADTS          -> pipe:<fd B> -> buffer -> /audio

Same instant for video and audio: one input and one process, so both outputs share FFmpeg's
clock. Both start at output time 0 (fps=<n>:start_time=0 repeats the first picture,
aresample first_pts=0 pads the first silence, whichever track the source starts later) and
stay locked to it (fps and aresample async keep them continuous across source gaps).

Two outputs of one process: two extra pipes passed as file descriptors (FFmpeg's pipe:<fd>).
POSIX, the same on macOS and Linux, and nothing to clean up on disk (no FIFOs, no sockets).
The server drains both pipes all the time into bounded buffers, so a slow reader of one
output never stalls FFmpeg for the other; a reader that falls further behind than its buffer
(a few seconds) ends the session instead of growing memory.

Pace: -readrate 1 after a 2 s burst, from 3 segments behind the newest (-live_start_index -3):
real-time output plus a 2 s head start that becomes the TV's cushion (it waits for prebuffer_ms of
video before playing). Starting at the newest segment keeps FFmpeg at the live edge: at the end of
each segment it waits for the next one to be published, so the output stops 1-5 s every segment
and then bursts (measured on 3Cat, 6 s segments). Two segments in hand cost ~12 s of delay.

Bitrate: the TV reads each stream over one TCP connection, and its lwIP receive window is fixed
at 5760 bytes in the Arduino core, so one connection carries ~150-190 KB/s on a home Wi-Fi.
MJPEG q8 (the SD's quality) is ~150 KB/s on real channels: no margin, the picture starves. Live
uses a profile (LIVE_PROFILES: frame rate + MJPEG quality), per server and per channel; the
session tells the TV the frame rate. Measurements: docs/NETWORK_TUNING.md."""

from __future__ import annotations

import asyncio
import collections
import logging
import os
import time
from dataclasses import dataclass

from .http_fetch import USER_AGENT
from .providers.base import HlsSource

log = logging.getLogger("FFMPEG")
stats_log = logging.getLogger("STATS")

# name -> (frames per second, MJPEG -q:v). Fewer frames or a higher q:v = fewer bytes per second.
LIVE_PROFILES: dict[str, tuple[int, int]] = {
    "24fps-q12": (24, 12),
    "24fps-q16": (24, 16),
    "20fps-q14": (20, 14),
    "20fps-q18": (20, 18),
    "18fps-q16": (18, 16),
}
DEFAULT_PROFILE = "20fps-q14"  # measured best on the TV: docs/NETWORK_TUNING.md
STATS_DETAILED_S = 30.0  # [STATS] every second for this long after a session starts, then every 5 s


def video_filter(fps: int, crop_4_3: bool = False) -> str:
    """The same picture as tools/convert_video.sh: 4:3 fills, 16:9 is 320x176 with bars.

    The picture is cut to whole 16-pixel JPEG blocks and placed on block boundaries (a 16:9
    picture loses 2 rows at the top and 2 at the bottom, like a TV's overscan). A bar edge in
    the middle of a block let JPEG noise from the picture bleed into the bar: colored dashes on
    the TV, up to 46/255 measured on SX3; with whole blocks the bars stay black."""
    # crop_4_3: a 4:3 series sent inside a 16:9 frame with black side bars (Pluto TV): keep the
    # middle 4:3, or it ends up small with bars on all four sides.
    crop = "crop='trunc(ih*4/3/2)*2':ih," if crop_4_3 else ""
    return (f"scale='trunc(iw*sar/2)*2':ih,setsar=1,{crop}fps={fps}:start_time=0,"
            "scale=320:240:force_original_aspect_ratio=decrease,crop=trunc(iw/16)*16:trunc(ih/16)*16,"
            "pad=320:240:trunc((320-iw)/32)*16:trunc((240-ih)/32)*16,setsar=1")

AUDIO_FILTER = "aresample=44100:async=1:first_pts=0"
LOUDNORM = "loudnorm=I=-16:TP=-1.5:LRA=11"
READ_CHUNK = 65536
STDERR_LINES_KEPT = 20
HARMLESS = ("deprecated pixel format",  # yuvj420p is what the TV's JPEG decoder wants
            "mime type is not rfc8216 compliant",  # simple HTTP servers; the playlist is fine
            "Can't support the subtitle")  # subtitle renditions are never mapped


@dataclass(frozen=True)
class FfmpegSettings:
    binary: str = "ffmpeg"
    startup_timeout_s: float = 10.0  # first video and audio bytes, or the session is refused
    max_restarts: int = 3  # after the source drops mid-session; then the session ends
    restart_backoff_s: tuple[float, ...] = (1.0, 2.0, 4.0)
    stop_timeout_s: float = 2.0  # SIGTERM, then SIGKILL
    stall_timeout_s: float = 8.0  # running but no video for this long: the source stopped
    video_buffer_bytes: int = 1_048_576  # ~5 s at ~200 KB/s: more behind than this ends the session
    audio_buffer_bytes: int = 65_536  # ~16 s at 4 KB/s
    loudnorm: bool = False  # adds CPU and ~3 s of audio lookahead; off for live by default
    video_quality: int = 14  # MJPEG -q:v (2 best .. 31); 8 is the SD's, too heavy for the TV's Wi-Fi
    fps: int = 20  # 24 fps drops frames on the TV while the Wi-Fi receives (decoding shares core 0)
    live_start_index: int = -3  # the segment to start from: -1 newest (least delay, but source gaps)
    stats_period_s: float = 5.0
    crop_4_3: bool = False  # per channel: see video_filter  # [STATS] after the first STATS_DETAILED_S (every second before)

    @property
    def profile(self) -> str:
        return f"{self.fps}fps-q{self.video_quality}"


class FfmpegError(Exception):
    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


def build_command(source: HlsSource, video_fd: int, audio_fd: int | None, settings: FfmpegSettings) -> list[str]:
    cmd = [settings.binary, "-hide_banner", "-nostdin", "-loglevel", "warning",
           "-user_agent", USER_AGENT, "-live_start_index", str(settings.live_start_index), "-seg_max_retry", "3",
           "-readrate", "1", "-readrate_initial_burst", "2",
           "-i", source.url,
           "-map", source.video_map, "-an", "-sn", "-vf", video_filter(settings.fps, settings.crop_4_3), "-pix_fmt", "yuvj420p",
           "-q:v", str(settings.video_quality),
           "-f", "mjpeg", f"pipe:{video_fd}"]
    if source.audio_map is not None and audio_fd is not None:
        audio_filter = AUDIO_FILTER + ("," + LOUDNORM if settings.loudnorm else "")
        cmd += ["-map", source.audio_map, "-vn", "-sn", "-af", audio_filter, "-ac", "1", "-ar", "44100",
                "-c:a", "aac", "-b:a", "32k", "-f", "adts", f"pipe:{audio_fd}"]
    return cmd


def classify(stderr_tail: list[str], returncode: int | None) -> str:
    """Why FFmpeg stopped, in words the API and the logs can show."""
    text = " ".join(stderr_tail).lower()
    if "403" in text:
        return "source refused (HTTP 403)"
    if "404" in text:
        return "manifest unavailable (HTTP 404)"
    if "connection refused" in text or "timed out" in text or "could not resolve" in text:
        return "source unreachable"
    if "matches no streams" in text:
        return "the source has no such video or audio stream"
    if "source stalled" in text:
        return "source stalled (no new data)"
    if returncode == 0:
        return "source ended (the playlist was closed)"
    last = stderr_tail[-1] if stderr_tail else "no message"
    return f"ffmpeg exited ({returncode}): {last[:160]}"


class OutputBuffer:
    """What FFmpeg produced and the TV has not read yet, bounded in bytes. One reader."""

    def __init__(self, limit: int) -> None:
        self.limit = limit
        self.total = 0  # bytes FFmpeg produced
        self.taken = 0  # bytes the TV read
        self.first_at: float | None = None
        self.last_at: float | None = None
        self._chunks: collections.deque[bytes] = collections.deque()
        self._size = 0
        self._ended = False
        self._wake = asyncio.Event()

    def put(self, data: bytes) -> bool:
        """False when the reader is further behind than the limit."""
        self.last_at = time.monotonic()
        if self.first_at is None:
            self.first_at = self.last_at
        self._chunks.append(data)
        self._size += len(data)
        self.total += len(data)
        self._wake.set()
        return self._size <= self.limit

    def end(self) -> None:
        self._ended = True
        self._wake.set()

    async def get(self) -> bytes | None:
        """The next chunk; None once the stream is over."""
        while not self._chunks:
            if self._ended:
                return None
            self._wake.clear()
            await self._wake.wait()
        chunk = self._chunks.popleft()
        self._size -= len(chunk)
        self.taken += len(chunk)
        return chunk

    @property
    def backlog(self) -> int:
        """Bytes produced and not read yet: grows when the TV reads slower than real time."""
        return self._size


class FfmpegSession:
    def __init__(self, source: HlsSource, settings: FfmpegSettings, name: str) -> None:
        self.source = source
        self.settings = settings
        self.name = name
        self.outputs = {"video": OutputBuffer(settings.video_buffer_bytes)}
        if source.audio_map is not None:
            self.outputs["audio"] = OutputBuffer(settings.audio_buffer_bytes)
        self.state = "new"  # starting, running, restarting, stopped, failed
        self.restarts = 0
        self.failure: str | None = None
        self.started_at: float | None = None
        self._proc: asyncio.subprocess.Process | None = None
        self._tasks: set[asyncio.Task] = set()
        self._stderr: collections.deque[str] = collections.deque(maxlen=STDERR_LINES_KEPT)
        self._watcher: asyncio.Task | None = None
        self._reaper: asyncio.Task | None = None
        self._stopping = False

    @property
    def pid(self) -> int | None:
        return self._proc.pid if self._proc else None

    # ---------------------------------------------------------------------------------------
    async def start(self) -> None:
        """Launches FFmpeg and returns once every output has produced its first bytes."""
        self.state = "starting"
        t0 = time.monotonic()
        log.info("starting session %s", self.name)
        await self._launch()
        try:
            await asyncio.wait_for(self._first_bytes(), self.settings.startup_timeout_s)
        except TimeoutError:
            await self.aclose()
            raise FfmpegError(f"no output within {self.settings.startup_timeout_s:.0f} s") from None
        except FfmpegError:
            await self.aclose()
            raise
        if self._stopping:  # failed while starting (a reader already too far behind)
            await self.aclose()
            raise FfmpegError(self.failure or "stopped while starting")
        for kind, out in self.outputs.items():
            log.info("first %s bytes after %d ms", kind, int((out.first_at - t0) * 1000))
        self.state = "running"
        self.started_at = time.monotonic()
        self._watcher = self._spawn(self._watch())
        self._spawn(self._watch_stall())
        self._spawn(self._report())

    async def _launch(self) -> None:
        read_fds: dict[str, int] = {}
        write_fds: dict[str, int] = {}
        for kind in self.outputs:
            read_fds[kind], write_fds[kind] = os.pipe()
        cmd = build_command(self.source, write_fds["video"], write_fds.get("audio"), self.settings)
        try:
            proc = await asyncio.create_subprocess_exec(
                *cmd, stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.PIPE, pass_fds=tuple(write_fds.values()))
        except OSError as e:
            for fd in read_fds.values():
                os.close(fd)
            raise FfmpegError(f"cannot run {self.settings.binary}: {e}") from e
        finally:
            for fd in write_fds.values():  # the child has its own copies, or there is no child
                os.close(fd)
        self._proc = proc
        self._stderr.clear()
        for kind, fd in read_fds.items():
            self._spawn(self._pump(kind, fd))
        self._spawn(self._read_stderr(self._proc))

    def _spawn(self, coro) -> asyncio.Task:
        task = asyncio.ensure_future(coro)
        self._tasks.add(task)
        task.add_done_callback(self._tasks.discard)
        return task

    async def _pump(self, kind: str, fd: int) -> None:
        """One pipe into its buffer, all the time, so FFmpeg never blocks on a slow reader."""
        loop = asyncio.get_running_loop()
        reader = asyncio.StreamReader(limit=READ_CHUNK * 4)
        transport, _ = await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader),
                                                    os.fdopen(fd, "rb", buffering=0))
        try:
            while True:
                data = await reader.read(READ_CHUNK)
                if not data:
                    return  # this FFmpeg run is over; the watcher decides what comes next
                if not self.outputs[kind].put(data):
                    self._fail(f"the TV is not reading {kind} (over {self.outputs[kind].limit // 1024} KB behind)")
                    return
        finally:
            transport.close()

    async def _read_stderr(self, proc: asyncio.subprocess.Process) -> None:
        assert proc.stderr is not None
        shown = 0
        while line := await proc.stderr.readline():
            text = line.decode("utf-8", "replace").strip()
            if not text:
                continue
            if any(h in text for h in HARMLESS):
                continue
            self._stderr.append(text)
            if shown < 3:  # the first few say what went wrong; the rest only fill the tail
                log.warning("%s: %s", self.name, text[:200])
                shown += 1

    async def _first_bytes(self) -> None:
        assert self._proc is not None
        exited = asyncio.ensure_future(self._proc.wait())
        try:
            while any(out.first_at is None for out in self.outputs.values()):
                if exited.done():
                    await asyncio.sleep(0.05)  # let stderr arrive
                    raise FfmpegError(classify(list(self._stderr), self._proc.returncode))
                await asyncio.sleep(0.02)
        finally:
            if not exited.done():
                exited.cancel()

    async def _watch(self) -> None:
        """FFmpeg exiting mid-session means the source dropped: restart a few times, then give up."""
        while not self._stopping:
            assert self._proc is not None
            code = await self._proc.wait()
            if self._stopping:
                return
            reason = classify(list(self._stderr), code)
            if self.restarts >= self.settings.max_restarts:
                self._fail(f"{reason}; gave up after {self.restarts} restarts")
                return
            delay = self.settings.restart_backoff_s[min(self.restarts, len(self.settings.restart_backoff_s) - 1)]
            self.restarts += 1
            self.state = "restarting"
            log.warning("%s: %s; restart %d/%d in %.0f s", self.name, reason, self.restarts,
                        self.settings.max_restarts, delay)
            await asyncio.sleep(delay)
            if self._stopping:
                return
            try:
                await self._launch()
            except FfmpegError as e:
                self._fail(e.reason)
                return
            self.state = "running"

    async def _watch_stall(self) -> None:
        """A live source that stops publishing leaves FFmpeg waiting, not exiting: end that run so
        the restart policy decides (a new run starts again from the newest segment)."""
        video = self.outputs["video"]
        while not self._stopping:
            await asyncio.sleep(min(1.0, self.settings.stall_timeout_s / 4))
            quiet = time.monotonic() - (video.last_at or time.monotonic())
            if self.state == "running" and quiet >= self.settings.stall_timeout_s and self._proc is not None:
                if self._proc.returncode is None:
                    log.warning("%s: no video for %.0f s: source stalled", self.name, quiet)
                    self._stderr.append("source stalled")
                    self._proc.terminate()
                video.last_at = time.monotonic()  # one kill per stall; the restart gets its own time

    async def _report(self) -> None:
        """[STATS]: per period, what FFmpeg produced and what the TV took, and the backlog."""
        assert self.started_at is not None
        last = {k: (o.total, o.taken) for k, o in self.outputs.items()}
        at = time.monotonic()
        while not self._stopping:
            since = time.monotonic() - self.started_at
            await asyncio.sleep(1.0 if since < STATS_DETAILED_S else self.settings.stats_period_s)
            now = time.monotonic()
            parts = []
            for kind, out in self.outputs.items():
                made, taken = out.total - last[kind][0], out.taken - last[kind][1]
                last[kind] = (out.total, out.taken)
                parts.append(f"{kind} out {made * 8 / 1000 / (now - at):.0f} kbps sent "
                             f"{taken * 8 / 1000 / (now - at):.0f} kbps backlog {out.backlog // 1024} KB")
            at = now
            stats_log.info("%s t=%.0fs %s %s restarts %d", self.name, now - self.started_at,
                           self.settings.profile, " ".join(parts), self.restarts)

    def _fail(self, reason: str) -> None:
        if self._stopping:
            return
        self.failure = reason
        log.warning("%s: %s", self.name, reason)
        self.close()
        self.state = "failed"

    # ---------------------------------------------------------------------------------------
    async def read(self, kind: str):
        """The TV's stream: chunks until the session stops or fails."""
        out = self.outputs[kind]
        while (chunk := await out.get()) is not None:
            yield chunk

    def close(self) -> None:
        """Stops everything; safe to call twice. The process is reaped in the background."""
        if self._stopping:
            return
        self._stopping = True
        self.state = "stopped"
        for out in self.outputs.values():
            out.end()
        for task in list(self._tasks):
            if task is not asyncio.current_task():
                task.cancel()
        proc = self._proc
        if proc is not None and proc.returncode is None:
            proc.terminate()
        self._reaper = asyncio.ensure_future(self._reap(proc))

    async def _reap(self, proc: asyncio.subprocess.Process | None) -> None:
        if proc is None:
            return
        try:
            await asyncio.wait_for(proc.wait(), self.settings.stop_timeout_s)
        except TimeoutError:
            log.warning("%s: did not stop in %.0f s, killing", self.name, self.settings.stop_timeout_s)
            proc.kill()
            await proc.wait()
        log.info("process %d terminated (%s)", proc.pid, proc.returncode)

    async def aclose(self) -> None:
        self.close()
        if self._reaper is not None:
            await self._reaper

    def stats(self) -> dict:
        uptime = time.monotonic() - self.started_at if self.started_at else 0.0

        def kbps(kind: str) -> int | None:
            out = self.outputs.get(kind)
            return int(out.total * 8 / 1000 / uptime) if out and uptime > 1 else None

        return {
            "ffmpeg": self.state,
            "pid": self.pid,
            "uptime_s": int(uptime),
            "restarts": self.restarts,
            "profile": self.settings.profile,
            "video_output_kbps": kbps("video"),
            "video_backlog_bytes": self.outputs["video"].backlog,
            "audio_output_kbps": kbps("audio"),
            "source_latency_ms": self.source.source_latency_ms,
            "failure": self.failure,
        }
