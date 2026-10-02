#pragma once

// One clock for audio and video. Pure C++: tested on the host.
// Audio is the master once playback starts: position = samples accepted by I2S minus the
// constant DMA latency, so video follows what is actually heard and the two never drift
// apart on a long episode. With no audio (or after it ends) a wall clock takes over from the
// last audio position. Writer: audio task. Readers: video task and display task.

#include <stdint.h>

#include <atomic>

class PlaybackClock {
 public:
  void start(uint32_t nowMs, bool hasAudio, uint32_t sampleRate, uint32_t latencySamples) {
    rate_ = sampleRate;
    latency_ = latencySamples;
    samples_.store(0);
    anchorPosMs_ = 0;
    anchorWallMs_ = nowMs;
    audioMaster_.store(hasAudio, std::memory_order_release);
  }

  // Audio task, after each successful i2s_write.
  void onAudioWritten(uint32_t frames) { samples_.fetch_add(frames, std::memory_order_relaxed); }

  // Audio task, when the audio stream ends or fails: the wall clock continues from here.
  void onAudioEnded(uint32_t nowMs) {
    anchorPosMs_ = audioPositionMs();
    anchorWallMs_ = nowMs;
    audioMaster_.store(false, std::memory_order_release);
  }

  uint32_t positionMs(uint32_t nowMs) const {
    if (audioMaster_.load(std::memory_order_acquire)) return audioPositionMs();
    return anchorPosMs_ + (nowMs - anchorWallMs_);  // unsigned: survives millis() wrap
  }

 private:
  uint32_t audioPositionMs() const {
    const uint32_t written = samples_.load(std::memory_order_relaxed);
    const uint32_t heard = written > latency_ ? written - latency_ : 0;
    return static_cast<uint32_t>(static_cast<uint64_t>(heard) * 1000 / rate_);
  }

  std::atomic<uint32_t> samples_{0};
  std::atomic<bool> audioMaster_{false};
  uint32_t rate_ = 44100;
  uint32_t latency_ = 0;
  uint32_t anchorPosMs_ = 0;
  uint32_t anchorWallMs_ = 0;
};

enum class FrameAction : uint8_t { Wait, Show, Drop };

// Frame n is due at n * 1000 / fps. Early: wait for it. Over one frame late: drop it
// without decoding, so a slow patch catches up instead of lagging forever.
inline FrameAction decideFrame(uint32_t positionMs, uint32_t dueMs, uint32_t frameMs) {
  if (positionMs < dueMs) return FrameAction::Wait;
  if (positionMs - dueMs > frameMs) return FrameAction::Drop;
  return FrameAction::Show;
}
