#pragma once

// "Already on air": a channel's programme is its episodes back to back, looping forever,
// anchored to the wall clock. Tuning in lands wherever the programme is right now:
// position = now modulo programme length. Pure C++: tested on the host.

#include <stddef.h>
#include <stdint.h>

struct OnAirSlot {
  size_t episode;
  uint32_t offsetMs;
};

inline uint64_t programmeLengthMs(const uint32_t* durationsMs, size_t count) {
  uint64_t total = 0;
  for (size_t i = 0; i < count; ++i) total += durationsMs[i];
  return total;
}

// nowMs: milliseconds since the Unix epoch. Zero-length episodes are never on air; with nothing
// to schedule the answer is the start of the first episode.
inline OnAirSlot onAirSlot(uint64_t nowMs, const uint32_t* durationsMs, size_t count) {
  const uint64_t total = programmeLengthMs(durationsMs, count);
  if (total == 0) return OnAirSlot{0, 0};
  uint64_t pos = nowMs % total;
  for (size_t i = 0; i < count; ++i) {
    if (pos < durationsMs[i]) return OnAirSlot{i, static_cast<uint32_t>(pos)};
    pos -= durationsMs[i];
  }
  return OnAirSlot{0, 0};  // unreachable: pos < total
}

struct Airing {
  size_t episode;
  uint64_t startMs;  // same clock as nowMs
  uint32_t durationMs;
};

// The guide: out[0] is the episode on air now, then the ones after it in order (wrapping).
// Returns how many were written: n, or 0 when there is nothing to schedule.
inline size_t upcomingAirings(uint64_t nowMs, const uint32_t* durationsMs, size_t count, Airing* out, size_t n) {
  if (count == 0 || programmeLengthMs(durationsMs, count) == 0) return 0;
  const OnAirSlot now = onAirSlot(nowMs, durationsMs, count);
  size_t episode = now.episode;
  uint64_t start = nowMs - now.offsetMs;
  for (size_t k = 0; k < n; ++k) {
    out[k] = Airing{episode, start, durationsMs[episode]};
    start += durationsMs[episode];
    do {
      episode = (episode + 1) % count;
    } while (durationsMs[episode] == 0);  // never on air; the total is not 0, so this ends
  }
  return n;
}

// What an on-air channel plays next: after an episode ends, the following one from its start
// (wrapping to the first); when tuning in, wherever the programme is now.
inline OnAirSlot nextOnAirSlot(bool episodeEnded, size_t currentEpisode, uint64_t nowMs,
                               const uint32_t* durationsMs, size_t count) {
  if (count == 0) return OnAirSlot{0, 0};
  if (episodeEnded) return OnAirSlot{(currentEpisode + 1) % count, 0};
  return onAirSlot(nowMs, durationsMs, count);
}
