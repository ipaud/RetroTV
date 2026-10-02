#pragma once

// Volume curve and a tiny sound generator (sine or white noise with a fade in/out).
// Pure C++: no Arduino, tested on the host (test/host_tests.cpp).

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

// User volume (0 = silent, 1..100) -> ES8311 volume register scale.
inline int codecVolumeFor(int userVolume) {
  if (userVolume <= 0) return 0;
  if (userVolume > 100) userVolume = 100;
  return CODEC_VOLUME_MIN + (userVolume - 1) * (CODEC_VOLUME_MAX - CODEC_VOLUME_MIN) / 99;
}

enum class SoundKind : uint8_t { Tone, Noise };

struct SoundRequest {
  SoundKind kind;
  uint16_t freqHz;      // Tone only
  uint32_t durationMs;
  uint8_t levelPct;     // of full scale; the codec volume comes on top
};

class Synth {
 public:
  void start(const SoundRequest& r, uint32_t sampleRate) {
    req_ = r;
    total_ = static_cast<uint32_t>(static_cast<uint64_t>(r.durationMs) * sampleRate / 1000);
    index_ = 0;
    fade_ = static_cast<uint32_t>(static_cast<uint64_t>(SYNTH_FADE_MS) * sampleRate / 1000);
    amplitude_ = 32767.0f * (r.levelPct > 100 ? 100 : r.levelPct) / 100.0f;
    phase_ = 0.0f;
    step_ = 2.0f * static_cast<float>(M_PI) * r.freqHz / sampleRate;
  }

  bool active() const { return index_ < total_; }

  // Writes up to n mono samples; returns how many (0 once the sound has finished).
  size_t render(int16_t* out, size_t n) {
    size_t written = 0;
    while (written < n && index_ < total_) {
      const float value = req_.kind == SoundKind::Tone ? sinf(phase_) : whiteNoise();
      out[written++] = static_cast<int16_t>(value * amplitude_ * envelope());
      phase_ += step_;
      if (phase_ >= 2.0f * static_cast<float>(M_PI)) phase_ -= 2.0f * static_cast<float>(M_PI);
      ++index_;
    }
    return written;
  }

 private:
  // Linear ramp over the first and last SYNTH_FADE_MS.
  float envelope() const {
    if (fade_ == 0) return 1.0f;
    const uint32_t fromEnd = total_ - 1 - index_;
    const uint32_t edge = index_ < fromEnd ? index_ : fromEnd;
    return edge >= fade_ ? 1.0f : static_cast<float>(edge) / fade_;
  }

  float whiteNoise() {  // xorshift32 -> [-1, 1)
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<int32_t>(rng_) / 2147483648.0f;
  }

  SoundRequest req_{SoundKind::Tone, 0, 0, 0};
  uint32_t total_ = 0;
  uint32_t index_ = 0;
  uint32_t fade_ = 0;
  float amplitude_ = 0.0f;
  float phase_ = 0.0f;
  float step_ = 0.0f;
  uint32_t rng_ = 0x9E3779B9u;
};
