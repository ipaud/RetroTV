#include "media/MediaPlayer.h"

#include <Arduino.h>

#include "app_types.h"
#include "audio/AudioManager.h"
#include "config.h"
#include "display/DisplayManager.h"
#include "libhelix-aac/aacdec.h"

namespace {

constexpr EventBits_t VIDEO_RUN = BIT0;
constexpr EventBits_t AUDIO_RUN = BIT1;
constexpr EventBits_t VIDEO_IDLE = BIT2;
constexpr EventBits_t AUDIO_IDLE = BIT3;
constexpr EventBits_t ALL_IDLE = VIDEO_IDLE | AUDIO_IDLE;

constexpr uint32_t PACING_SLICE_MS = 10;  // early frames wait in short slices so stop() is quick
constexpr size_t PCM_MAX_SAMPLES = AAC_MAX_NCHANS * AAC_MAX_NSAMPS * 2;  // x2: SBR doubles output
constexpr int AAC_REFILL_BELOW = AAC_MAINBUF_SIZE;  // keep at least one worst-case frame buffered

static_assert(VIDEO_BLOCK_PIXELS >= MAX_BUFFERED_PIXELS, "a JPEGDEC block must fit a VideoBlock");

template <typename T>
T* allocate(size_t count, uint32_t caps) {
  return static_cast<T*>(heap_caps_malloc(count * sizeof(T), caps | MALLOC_CAP_8BIT));
}

}  // namespace

bool MediaPlayer::begin(DisplayManager& display, AudioManager& audio) {
  display_ = &display;
  audio_ = &audio;

  frameBuf_ = allocate<uint8_t>(JPEG_FRAME_BUF_SIZE, MALLOC_CAP_SPIRAM);
  readChunk_ = allocate<uint8_t>(VIDEO_READ_CHUNK, MALLOC_CAP_INTERNAL);
  aacIn_ = allocate<uint8_t>(AAC_INPUT_BUF_SIZE, MALLOC_CAP_INTERNAL);
  pcm_ = allocate<int16_t>(PCM_MAX_SAMPLES, MALLOC_CAP_INTERNAL);
  aac_ = AACInitDecoder();
  events_ = xEventGroupCreate();
  if (!frameBuf_ || !readChunk_ || !aacIn_ || !pcm_ || !aac_ || !events_) {
    PLOG("MEDIA", "cannot allocate player buffers");
    return false;
  }
  splitter_.attach(frameBuf_, JPEG_FRAME_BUF_SIZE, readChunk_, VIDEO_READ_CHUNK);
  display_->setFrameShownHook(onFrameShown, this);
  xEventGroupSetBits(events_, ALL_IDLE);

  if (xTaskCreatePinnedToCore(videoEntry, "video", VIDEO_TASK_STACK, this, VIDEO_TASK_PRIO,
                              nullptr, VIDEO_TASK_CORE) != pdPASS ||
      xTaskCreatePinnedToCore(audioEntry, "media-audio", MEDIA_AUDIO_TASK_STACK, this,
                              AUDIO_TASK_PRIO, nullptr, AUDIO_TASK_CORE) != pdPASS) {
    PLOG("MEDIA", "cannot create player tasks");
    return false;
  }
  PLOG("MEDIA", "ready: %u KB frame buffer in PSRAM, %d video blocks", JPEG_FRAME_BUF_SIZE / 1024,
       VIDEO_BLOCK_COUNT);
  return true;
}

bool MediaPlayer::start(Stream* video, Stream* audio, uint8_t fps) {
  if (running_ && !stop()) return false;  // never retarget tasks that are still running
  if (audio != nullptr && !audio_->ready()) {
    PLOG("MEDIA", "audio not ready: playing silent on the wall clock");
    audio = nullptr;
  }
  videoIn_ = video;
  audioIn_ = audio;
  fps_ = fps > 0 ? fps : VIDEO_FPS;
  finished_ = false;
  splitter_.reset();  // the video task is idle, so its state can be touched here
  clock_.start(millis(), audio != nullptr, AUDIO_SAMPLE_RATE, AudioManager::latencyFrames());

  statsBase_ = counters();  // the 5 s stats start from this programme
  statsSinceMs_ = millis();
  takeDriftMax(DriftReader::Stats);
  display_->takeDrawMicros();

  running_ = true;
  const EventBits_t run = VIDEO_RUN | (audio != nullptr ? AUDIO_RUN : 0);
  const EventBits_t busy = VIDEO_IDLE | (audio != nullptr ? AUDIO_IDLE : 0);
  xEventGroupClearBits(events_, busy);  // before RUN, so stop() never sees a stale IDLE
  xEventGroupSetBits(events_, run);
  return true;
}

PlaybackCounters MediaPlayer::counters() const {
  PlaybackCounters c;
  c.shown = shown_.load();
  c.dropped = dropped_.load();
  c.bad = badFrames_.load();
  c.decoded = decoded_.load();
  c.decodeUs = decodeUs_.load();
  c.driftSumMs = driftSum_.load();
  c.sdBytes = sdBytes_.load();
  c.audioErrors = audioErrors_.load();
  c.videoBeats = videoBeats_.load();
  c.audioBeats = audioBeats_.load();
  return c;
}

bool MediaPlayer::stop() {
  running_ = false;
  const EventBits_t bits = xEventGroupWaitBits(events_, ALL_IDLE, pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(MEDIA_STOP_TIMEOUT_MS));
  display_->discardPendingBlocks();
  if ((bits & ALL_IDLE) != ALL_IDLE) {
    PLOG("MEDIA", "stop timeout: video %s, audio %s", (bits & VIDEO_IDLE) ? "idle" : "STUCK",
         (bits & AUDIO_IDLE) ? "idle" : "STUCK");
    return false;
  }
  return true;
}

void MediaPlayer::videoEntry(void* self) { static_cast<MediaPlayer*>(self)->videoLoop(); }
void MediaPlayer::audioEntry(void* self) { static_cast<MediaPlayer*>(self)->audioLoop(); }

void MediaPlayer::videoLoop() {
  for (;;) {
    xEventGroupWaitBits(events_, VIDEO_RUN, pdTRUE, pdTRUE, portMAX_DELAY);
    playVideo();
    xEventGroupSetBits(events_, VIDEO_IDLE);
  }
}

void MediaPlayer::audioLoop() {
  for (;;) {
    xEventGroupWaitBits(events_, AUDIO_RUN, pdTRUE, pdTRUE, portMAX_DELAY);
    playAudio();
    xEventGroupSetBits(events_, AUDIO_IDLE);
  }
}

// ---------------------------------------------------------------------------------------------
// Video

void MediaPlayer::playVideo() {
  auto read = [this](uint8_t* dst, size_t max) -> size_t {
    const size_t n = videoIn_->readBytes(reinterpret_cast<char*>(dst), max);
    sdBytes_ += n;
    return n;
  };

  for (uint32_t index = 0; running_; ++index) {
    ++videoBeats_;
    size_t len = 0;
    if (splitter_.next(read, len) == JpegFrameSplitter::Result::EndOfStream) {
      finished_ = true;
      return;
    }
    bool drop = false;
    if (!waitUntilDue(static_cast<uint32_t>(static_cast<uint64_t>(index) * 1000 / fps_), drop)) return;
    if (drop) {
      ++dropped_;
    } else {
      decodeFrame(len, index);
    }
    vTaskDelay(1);  // even when every frame is late, IDLE0 runs and feeds the task watchdog
  }
}

// False when stopped while waiting.
bool MediaPlayer::waitUntilDue(uint32_t dueMs, bool& drop) {
  const uint32_t frameMs = 1000 / fps_;
  for (;;) {
    if (!running_) return false;
    const uint32_t pos = clock_.positionMs(millis()) + VIDEO_PIPELINE_LEAD_MS;
    switch (decideFrame(pos, dueMs, frameMs)) {
      case FrameAction::Show:
        drop = false;
        return true;
      case FrameAction::Drop:
        drop = true;
        return true;
      case FrameAction::Wait: {
        const uint32_t ms = dueMs - pos < PACING_SLICE_MS ? dueMs - pos : PACING_SLICE_MS;
        vTaskDelay(pdMS_TO_TICKS(ms > 0 ? ms : 1));
        break;
      }
    }
  }
}

void MediaPlayer::decodeFrame(size_t len, uint32_t index) {
  if (!jpeg_.openRAM(frameBuf_, static_cast<int>(len), onJpegBlock)) {
    ++badFrames_;
    return;
  }
  jpeg_.setUserPointer(this);
  dithering_ = dither_.load();
  jpeg_.setPixelType(dithering_ ? RGB8888 : RGB565_BIG_ENDIAN);
  frameW_ = jpeg_.getWidth();
  frameH_ = jpeg_.getHeight();
  if (frameW_ > SCREEN_W || frameH_ > SCREEN_H) {  // the converter always outputs 320x240
    jpeg_.close();
    ++badFrames_;
    return;
  }
  frameX_ = (SCREEN_W - frameW_) / 2;
  frameY_ = (SCREEN_H - frameH_) / 2;
  currentFrame_ = index;
  blockWaitUs_ = 0;

  const uint32_t t0 = micros();
  const bool ok = jpeg_.decode(frameX_, frameY_, 0) == 1;
  const uint32_t us = micros() - t0;
  jpeg_.close();
  if (!ok && running_) ++badFrames_;
  decodeUs_ += us > blockWaitUs_ ? us - blockWaitUs_ : 0;  // pure decode, not display back-pressure
  ++decoded_;
}

// Video task: one decoded block -> a pool block -> DisplayTask. Returning 0 aborts the frame.
int MediaPlayer::onJpegBlock(JPEGDRAW* draw) {
  auto* self = static_cast<MediaPlayer*>(draw->pUser);
  if (!self->running_) return 0;  // stop() does not wait for the rest of the frame

  const uint32_t t0 = micros();
  VideoBlock* block = self->display_->acquireBlock(pdMS_TO_TICKS(VIDEO_BLOCK_WAIT_MS));
  self->blockWaitUs_ += micros() - t0;
  if (block == nullptr) return 0;

  const int w = draw->iWidthUsed;  // rows in pPixels are iWidth apart; iWidthUsed are valid
  const int h = draw->iHeight;
  if (self->dithering_) {
    // RGB8888 (R, G, B, A bytes) -> RGB565 big endian, with a 4x4 Bayer threshold added first
    // so the missing low bits turn into a fine pattern instead of visible steps.
    static constexpr uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int row = 0; row < h; ++row) {
      const uint8_t* in = reinterpret_cast<const uint8_t*>(draw->pPixels) + row * draw->iWidth * 4;
      const uint8_t* threshold = BAYER[(draw->y + row) & 3];
      uint16_t* out = &block->pixels[row * w];
      for (int col = 0; col < w; ++col, in += 4) {
        const int t = threshold[(draw->x + col) & 3];  // 0-15
        const int r = in[0] + (t >> 1), g = in[1] + (t >> 2), b = in[2] + (t >> 1);  // steps of 8, 4, 8
        const uint16_t px = static_cast<uint16_t>(((r > 255 ? 255 : r) >> 3) << 11 | ((g > 255 ? 255 : g) >> 2) << 5 |
                                                  (b > 255 ? 255 : b) >> 3);
        out[col] = __builtin_bswap16(px);
      }
    }
  } else {
    for (int row = 0; row < h; ++row) {
      memcpy(&block->pixels[row * w], &draw->pPixels[row * draw->iWidth], w * sizeof(uint16_t));
    }
  }
  block->x = draw->x;
  block->y = draw->y;
  block->w = w;
  block->h = h;
  block->frame = self->currentFrame_;
  block->lastOfFrame = draw->y + h >= self->frameY_ + self->frameH_ &&
                       draw->x + draw->iWidth >= self->frameX_ + self->frameW_;
  self->display_->submitBlock(block);
  return 1;
}

// DisplayTask, when a whole frame is on the panel: how far it is from the clock.
void MediaPlayer::onFrameShown(uint32_t frame, void* p) {
  auto* self = static_cast<MediaPlayer*>(p);
  if (!self->running_ || self->fps_ == 0) return;
  const int32_t due = static_cast<int32_t>(static_cast<uint64_t>(frame) * 1000 / self->fps_);
  const int32_t drift = static_cast<int32_t>(self->clock_.positionMs(millis())) - due;
  ++self->shown_;
  self->driftSum_ += static_cast<uint32_t>(drift);  // two's complement: diffs stay signed
  const int32_t magnitude = drift < 0 ? -drift : drift;
  for (auto& worst : self->driftMax_) {  // single writer (display task): no CAS needed
    if (magnitude > worst.load()) worst.store(magnitude);
  }
}

// ---------------------------------------------------------------------------------------------
// Audio: ADTS AAC through Helix's own public API (aacdec.h). The decoder was created once in
// begin(); AACFlushCodec resets it between programmes without allocating.

void MediaPlayer::playAudio() {
  const HAACDecoder dec = aac_;
  AACFlushCodec(dec);
  uint8_t* ptr = aacIn_;
  int left = 0;
  bool eof = false;
  bool needMore = false;

  while (running_) {
    ++audioBeats_;
    if (!eof && (left < AAC_REFILL_BELOW || needMore)) {
      memmove(aacIn_, ptr, left);
      ptr = aacIn_;
      const size_t n = audioIn_->readBytes(reinterpret_cast<char*>(aacIn_) + left,
                                           AAC_INPUT_BUF_SIZE - left);
      sdBytes_ += n;
      left += n;
      eof = n == 0;
      if (needMore && n == 0 && left == static_cast<int>(AAC_INPUT_BUF_SIZE)) {
        ++ptr;  // a full buffer that still underflows is garbage: move on
        --left;
      }
      needMore = false;
    }
    if (left <= 0) break;

    const int sync = AACFindSyncWord(ptr, left);
    if (sync < 0) {
      if (eof) break;
      ptr += left - 1;  // the last byte may begin the next sync word
      left = 1;
      continue;
    }
    ptr += sync;
    left -= sync;

    const int err = AACDecode(dec, &ptr, &left, pcm_);
    if (err == ERR_AAC_INDATA_UNDERFLOW) {
      if (eof) break;
      needMore = true;
      continue;
    }
    if (err != ERR_AAC_NONE) {  // corrupt frame: skip a byte and resync
      ++audioErrors_;
      ++ptr;
      --left;
      continue;
    }

    AACFrameInfo info;
    AACGetLastFrameInfo(dec, &info);
    if (info.sampRateOut != static_cast<int>(AUDIO_SAMPLE_RATE) || info.nChans < 1) {
      if (!rateWarned_) PLOG("MEDIA", "audio is %d Hz x%d, expected %u Hz: silent", info.sampRateOut,
                             info.nChans, AUDIO_SAMPLE_RATE);
      rateWarned_ = true;
      break;
    }
    const size_t frames = static_cast<size_t>(info.outputSamps / info.nChans);
    clock_.onAudioWritten(audio_->writePcm(pcm_, frames, static_cast<uint8_t>(info.nChans)));
  }
  clock_.onAudioEnded(millis());
}

// ---------------------------------------------------------------------------------------------

void MediaPlayer::logStats(uint32_t nowMs) {
#if PAUTV_DEBUG_STATS
  if (!running_) return;
  // Signed: start() may have stamped statsSinceMs_ after the caller read nowMs.
  if (static_cast<int32_t>(nowMs - statsSinceMs_) < static_cast<int32_t>(MEDIA_STATS_PERIOD_MS)) return;
  const uint32_t elapsed = nowMs - statsSinceMs_;
  statsSinceMs_ = nowMs;

  const PlaybackCounters now = counters();
  const PlaybackCounters& was = statsBase_;
  const uint32_t shown = now.shown - was.shown;
  const uint32_t decoded = now.decoded - was.decoded;
  const int32_t driftSum = static_cast<int32_t>(now.driftSumMs - was.driftSumMs);
  const uint32_t drawUs = display_->takeDrawMicros();
  const uint32_t fps10 = shown * 10000 / elapsed;

  PLOG("MEDIA",
       "fps %u.%u decode %u ms draw %u ms av_drift_ms avg %d max %d dropped_frames %u "
       "overflow %u bad %u sd %u KB/s heap %u KB psram %u KB beats v%u a%u aerr %u",
       fps10 / 10, fps10 % 10, decoded ? (now.decodeUs - was.decodeUs) / decoded / 1000 : 0,
       shown ? drawUs / shown / 1000 : 0, shown ? static_cast<int>(driftSum / static_cast<int32_t>(shown)) : 0,
       static_cast<int>(takeDriftMax(DriftReader::Stats)), now.dropped - was.dropped, splitter_.overflows(),
       now.bad - was.bad, (now.sdBytes - was.sdBytes) / elapsed * 1000 / 1024, ESP.getFreeHeap() / 1024,
       ESP.getFreePsram() / 1024, now.videoBeats, now.audioBeats, now.audioErrors - was.audioErrors);
  statsBase_ = now;
#else
  (void)nowMs;
#endif
}
