#pragma once

// RETROTV Voice recorder, pure parts (host-tested in test/voice_tests.cpp; docs/VOICE.md):
// resampling to 16 kHz, the WAV header, message file names and the REC flow on screen.
// Nothing here touches the card or the microphone: AppVoice.cpp does, and only with REC visible.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "voice/Biquad.h"

// A voice a metre away reaches the microphone 25-40 dB under full scale, and much of it is bass the
// TV's small speaker cannot play: saved as heard, it plays back too quietly. Before saving: a
// high-pass at REC_HIGHPASS_HZ (rumble, DC and that bass), then a gain that brings the voice (the
// 90th-percentile 20 ms block, so pauses and a single click do not count) to targetDb, at most
// maxGainDb (a quiet room is not turned into hiss). Peaks that would pass full scale are rounded
// off by a soft limiter instead of clipping.
struct Normalized {
  float levelDb;  // the voice before, dBFS
  float gainDb;
};
inline Normalized normalizeRecording(int16_t* pcm, size_t n, float sampleRate, float targetDb, float maxGainDb) {
  if (n == 0) return {-99.5f, 0.0f};
  Biquad hp = Biquad::highPass(REC_HIGHPASS_HZ, sampleRate);
  const size_t block = static_cast<size_t>(sampleRate) / 50;  // 20 ms
  uint16_t hist[100] = {};  // blocks by level: [k] = -k..-(k+1) dBFS, [99] = -99 or less
  size_t blocks = 0;
  size_t inBlock = 0;
  float sum = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    const float y = hp.step(pcm[i]);
    pcm[i] = static_cast<int16_t>(lrintf(fmaxf(-32768.0f, fminf(32767.0f, y))));
    sum += y * y;
    if (++inBlock < block && i + 1 < n) continue;
    const int k = static_cast<int>(-10.0f * log10f(sum / inBlock / (32768.0f * 32768.0f) + 1e-12f));
    ++hist[k < 0 ? 0 : (k > 99 ? 99 : k)];
    ++blocks;
    sum = 0.0f;
    inBlock = 0;
  }
  int k = 0;  // from the loudest bin until a tenth of the blocks are behind
  for (size_t seen = hist[0]; k < 99 && seen * 10 < blocks; seen += hist[++k]) {}
  const float levelDb = -static_cast<float>(k) - 0.5f;
  const float gainDb = fminf(targetDb - levelDb, maxGainDb);
  const float g = powf(10.0f, gainDb / 20.0f) / 32768.0f;
  constexpr float KNEE = 0.5f;
  for (size_t i = 0; i < n; ++i) {
    const float v = pcm[i] * g;
    float a = fabsf(v);
    if (a > KNEE) a = KNEE + (1.0f - KNEE) * tanhf((a - KNEE) / (1.0f - KNEE));
    pcm[i] = static_cast<int16_t>(lrintf(copysignf(a, v) * 32767.0f));
  }
  return {levelDb, gainDb};
}

// 44.1 kHz -> 16 kHz for speech: a 6th-order low-pass at 6 kHz (aliasing out), then linear
// interpolation at a fractional step. Streams block by block.
class Downsampler {
 public:
  Downsampler(float inRate, float outRate, float cutoffHz)
      : step_(inRate / outRate),
        lp_{Biquad::lowPass(cutoffHz, inRate), Biquad::lowPass(cutoffHz, inRate), Biquad::lowPass(cutoffHz, inRate)} {}

  // Feeds n input samples; writes up to `max` output samples and returns how many.
  size_t feed(const int16_t* in, size_t n, int16_t* out, size_t max) {
    size_t o = 0;
    for (size_t i = 0; i < n; ++i) {
      float y = static_cast<float>(in[i]);
      for (Biquad& f : lp_) y = f.step(y);
      while (pos_ <= 0.0f) {  // the next output falls between prev_ and y
        if (o == max) return o;
        const float v = prev_ + (y - prev_) * (1.0f + pos_);
        out[o++] = static_cast<int16_t>(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : v));
        pos_ += step_;
      }
      pos_ -= 1.0f;
      prev_ = y;
    }
    return o;
  }

 private:
  float step_;
  Biquad lp_[3];
  float pos_ = 0.0f;  // where the next output sample is, in input samples from the current one
  float prev_ = 0.0f;
};

// The 44-byte header of a PCM WAV file.
constexpr size_t WAV_HEADER_BYTES = 44;

inline void putLe(uint8_t* p, uint32_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint32_t getLe(const uint8_t* p, int bytes) {
  uint32_t v = 0;
  for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

inline void wavHeader(uint8_t* h, uint32_t sampleRate, uint16_t channels, uint16_t bits, uint32_t dataBytes) {
  const uint16_t blockAlign = static_cast<uint16_t>(channels * bits / 8);
  memcpy(h, "RIFF", 4);
  putLe(h + 4, 36 + dataBytes, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  putLe(h + 16, 16, 4);  // fmt chunk size
  putLe(h + 20, 1, 2);   // PCM
  putLe(h + 22, channels, 2);
  putLe(h + 24, sampleRate, 4);
  putLe(h + 28, sampleRate * blockAlign, 4);
  putLe(h + 32, blockAlign, 2);
  putLe(h + 34, bits, 2);
  memcpy(h + 36, "data", 4);
  putLe(h + 40, dataBytes, 4);
}

struct WavInfo {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bits = 0;
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;
  uint32_t durationMs() const {
    const uint32_t frame = channels * bits / 8;
    return frame && sampleRate ? static_cast<uint32_t>(static_cast<uint64_t>(dataBytes / frame) * 1000 / sampleRate) : 0;
  }
};

// A PCM 16-bit WAV we can play: walks the chunks up to "data". fileBytes caps the data size.
inline bool parseWav(const uint8_t* p, size_t len, uint32_t fileBytes, WavInfo& w) {
  if (len < 12 || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) return false;
  bool fmt = false;
  for (size_t at = 12; at + 8 <= len;) {
    const uint32_t size = getLe(p + at + 4, 4);
    if (memcmp(p + at, "fmt ", 4) == 0 && at + 8 + 16 <= len) {
      if (getLe(p + at + 8, 2) != 1) return false;  // not PCM
      w.channels = static_cast<uint16_t>(getLe(p + at + 10, 2));
      w.sampleRate = getLe(p + at + 12, 4);
      w.bits = static_cast<uint16_t>(getLe(p + at + 22, 2));
      fmt = w.bits == 16 && (w.channels == 1 || w.channels == 2) && w.sampleRate >= 8000 && w.sampleRate <= 48000;
    } else if (memcmp(p + at, "data", 4) == 0) {
      if (!fmt) return false;
      w.dataOffset = static_cast<uint32_t>(at + 8);
      if (w.dataOffset > fileBytes) return false;
      w.dataBytes = size < fileBytes - w.dataOffset ? size : fileBytes - w.dataOffset;  // a short file plays what it has
      return true;
    }
    at += 8 + size + (size & 1);
  }
  return false;
}

// "msg_0004.wav" -> 4; anything else -> -1.
inline int messageIdOf(const char* name) {
  if (strlen(name) != 12 || strncmp(name, "msg_", 4) != 0 || strcmp(name + 8, ".wav") != 0) return -1;
  int id = 0;
  for (int i = 4; i < 8; ++i) {
    if (name[i] < '0' || name[i] > '9') return -1;
    id = id * 10 + (name[i] - '0');
  }
  return id > 0 ? id : -1;
}

inline void messageName(int id, char* out, size_t len) { snprintf(out, len, "msg_%04d.wav", id); }

// The REC flow on screen: 3-2-1, then REC until the user stops it or REC_MAX_MS, then it is saved
// and the result shown for REC_RESULT_MS. A key during the countdown cancels; during REC it stops.
enum class RecPhase : uint8_t { Idle, Countdown, Recording, Saving, Saved, Failed };

class RecorderFlow {
 public:
  void start(uint32_t now) {
    phase_ = RecPhase::Countdown;
    since_ = now;
  }
  // A key or MENU.
  void stop(uint32_t now) {
    if (phase_ == RecPhase::Countdown) {
      phase_ = RecPhase::Idle;
    } else if (phase_ == RecPhase::Recording) {
      phase_ = RecPhase::Saving;
      since_ = now;
    }
  }
  void saved(uint32_t now, bool ok) {
    phase_ = ok ? RecPhase::Saved : RecPhase::Failed;
    since_ = now;
  }
  RecPhase update(uint32_t now) {
    const uint32_t t = now - since_;
    if (phase_ == RecPhase::Countdown && t >= REC_COUNTDOWN_MS) {
      phase_ = RecPhase::Recording;
      since_ += REC_COUNTDOWN_MS;
    } else if (phase_ == RecPhase::Recording && t >= REC_MAX_MS) {
      phase_ = RecPhase::Saving;
      since_ += REC_MAX_MS;
    } else if ((phase_ == RecPhase::Saved || phase_ == RecPhase::Failed) && t >= REC_RESULT_MS) {
      phase_ = RecPhase::Idle;
    }
    return phase_;
  }
  RecPhase phase() const { return phase_; }
  // 3, 2, 1 during the countdown.
  uint8_t countdown(uint32_t now) const {
    const uint32_t left = REC_COUNTDOWN_MS - (now - since_);
    return static_cast<uint8_t>((left + 999) / 1000);
  }
  uint32_t recordedMs(uint32_t now) const { return phase_ == RecPhase::Recording ? now - since_ : 0; }

 private:
  RecPhase phase_ = RecPhase::Idle;
  uint32_t since_ = 0;
};
