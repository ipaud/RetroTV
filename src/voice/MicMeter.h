#pragma once

// Levels of the microphone, pure C++ (host-tested in test/voice_tests.cpp). Levels are in tenths
// of dBFS: 0 is full scale (32768), -240 is -24.0 dB, MIC_FLOOR_DB10 is silence.

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

struct MicLevels {
  int16_t rmsDb10 = MIC_FLOOR_DB10;
  int16_t peakDb10 = MIC_FLOOR_DB10;
  uint16_t clipped = 0;  // samples at the rails: the gain is too high or the sound too loud
};

// amplitude (0..1 of full scale) -> tenths of dBFS, never below the floor.
inline int16_t micDb10(float amplitude) {
  if (amplitude <= 0.0f) return MIC_FLOOR_DB10;
  const float db10 = 200.0f * log10f(amplitude);
  if (db10 <= MIC_FLOOR_DB10) return MIC_FLOOR_DB10;
  if (db10 >= 0.0f) return 0;
  return static_cast<int16_t>(lroundf(db10));
}

// Tenths of dBFS -> 0..100 % of the VU bar (MIC_METER_MIN_DB10..0 dB).
inline uint8_t micMeterPct(int16_t db10) {
  if (db10 <= MIC_METER_MIN_DB10) return 0;
  if (db10 >= 0) return 100;
  return static_cast<uint8_t>((static_cast<int32_t>(db10 - MIC_METER_MIN_DB10) * 100) / -MIC_METER_MIN_DB10);
}

// One-pole high-pass (~35 Hz at 44.1 kHz): the codec's DC offset would otherwise read as level.
class DcBlocker {
 public:
  float step(int16_t x) {
    const float in = static_cast<float>(x);
    const float out = in - x1_ + R * y1_;
    x1_ = in;
    y1_ = out;
    return out;
  }

 private:
  static constexpr float R = 0.995f;
  float x1_ = 0.0f;
  float y1_ = 0.0f;
};

class MicMeter {
 public:
  // The levels of one block of mono samples. The filter state runs on across blocks.
  MicLevels block(const int16_t* mono, size_t n) {
    MicLevels l;
    if (n == 0) return l;
    float sumSquares = 0.0f;
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
      if (mono[i] >= CLIP || mono[i] <= -CLIP) ++l.clipped;
      const float y = dc_.step(mono[i]);
      sumSquares += y * y;
      const float a = y < 0 ? -y : y;
      if (a > peak) peak = a;
    }
    l.rmsDb10 = micDb10(sqrtf(sumSquares / static_cast<float>(n)) / FULL_SCALE);
    l.peakDb10 = micDb10(peak / FULL_SCALE);
    return l;
  }

 private:
  static constexpr float FULL_SCALE = 32768.0f;
  static constexpr int16_t CLIP = 32700;
  DcBlocker dc_;
};
