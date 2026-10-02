#pragma once

// What the TV's own speaker played, block by block, and when it was heard (pure C++, host-tested
// in test/voice_tests.cpp). The clap detector asks it whether a "clap" coincides with a sharp rise
// in the programme's own sound: a punch in a cartoon is the TV hearing itself, not a person. Not
// echo cancellation: just "did my speaker go bang right then?".
//
// One writer (the media audio task, as it hands PCM to the DAC), one reader (the capture task). An
// entry is written before the count that publishes it; the reader only looks a few hundred ms back,
// far from the slot the writer is filling.

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "voice/Biquad.h"

// The TV's small speaker hardly plays bass, so what its microphone hears of the programme is the
// mids and highs: a snare over a bass line is a sharp rise at the microphone but a small one in
// full-band level. The envelope is taken after a high-pass (Biquad::highPass).
class HighPass {
 public:
  HighPass(float cutoffHz, float sampleRate) : f_(Biquad::highPass(cutoffHz, sampleRate)) {}
  float step(float x) { return f_.step(x); }

 private:
  Biquad f_;
};

class PlaybackEnvelope {
 public:
  static constexpr size_t SIZE = 128;  // ~740 ms of 5.8 ms blocks

  // Writer: a block of the programme's sound at db10 (tenths of dBFS), heard at heardMs.
  void push(uint32_t heardMs, int16_t db10) {
    const uint32_t n = count_.load(std::memory_order_relaxed);
    entries_[n % SIZE] = Entry{heardMs, db10};
    count_.store(n + 1, std::memory_order_release);
  }

  // Reader: did the speaker's sound jump by riseDb10 (to at least minDb10) between fromMs and toMs?
  // A block counts if it is louder than the one before and riseDb10 over the quietest of the few
  // blocks before it (so the tail of a bang, falling, is never a new one).
  bool riseBetween(uint32_t fromMs, uint32_t toMs, int16_t riseDb10, int16_t minDb10) const {
    return maxRise(fromMs, toMs, minDb10) >= riseDb10;
  }

  // The biggest such rise between fromMs and toMs, in tenths of dB (0 = none).
  int16_t maxRise(uint32_t fromMs, uint32_t toMs, int16_t minDb10) const {
    int16_t best = 0;
    const uint32_t n = count_.load(std::memory_order_acquire);
    const uint32_t span = n < SIZE - BEFORE ? n : static_cast<uint32_t>(SIZE - BEFORE);
    for (uint32_t back = 0; back < span; ++back) {
      const Entry& e = entries_[(n - 1 - back) % SIZE];
      if (static_cast<int32_t>(e.ms - fromMs) < 0) break;  // older than the window: done
      if (static_cast<int32_t>(e.ms - toMs) > 0 || e.db10 < minDb10) continue;
      if (back + 1 < n && entries_[(n - 2 - back) % SIZE].db10 >= e.db10) continue;  // falling or flat: a tail
      int16_t quietest = e.db10;
      for (uint32_t k = 1; k <= BEFORE && back + k < n; ++k) {
        const int16_t d = entries_[(n - 1 - back - k) % SIZE].db10;
        if (d < quietest) quietest = d;
      }
      if (e.db10 - quietest > best) best = static_cast<int16_t>(e.db10 - quietest);
    }
    return best;
  }

 private:
  static constexpr uint32_t BEFORE = 4;  // ~23 ms of lead-in to measure the rise against
  struct Entry {
    uint32_t ms;
    int16_t db10;
  };
  Entry entries_[SIZE] = {};
  std::atomic<uint32_t> count_{0};
};
