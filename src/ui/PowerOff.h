#pragma once

// The CRT switch-off, as a function of time: the picture squeezes into a bright horizontal line,
// the line shrinks to a dot in the middle and the dot fades out. Pure C++: tested on the host;
// UIManager draws it (Screen::PowerOff).

#include <stdint.h>

namespace poweroff {

constexpr uint32_t COLLAPSE_MS = 220;  // full height -> a 3-pixel line
constexpr uint32_t SHRINK_MS = 220;    // full-width line -> a dot
constexpr uint32_t FADE_MS = 320;      // the dot's afterglow
constexpr uint32_t TOTAL_MS = COLLAPSE_MS + SHRINK_MS + FADE_MS;
constexpr int FULL_HALF_H = 120;       // SCREEN_H / 2
constexpr int FULL_HALF_W = 160;       // SCREEN_W / 2
constexpr int LINE_HALF_H = 1;
constexpr int DOT_HALF = 3;

struct Frame {
  int bandHalf;    // picture rows still on: centre +- bandHalf
  int lineHalfW;   // the line, once collapsed: centre +- lineHalfW; 0 = only the dot
  uint8_t dot;     // dot brightness 0-255, once the line is gone
  bool done;
};

// Eased in: slow at first, fast at the end, like the deflection collapsing.
inline int easeIn(int from, int to, uint32_t t, uint32_t span) {
  const uint64_t q = static_cast<uint64_t>(t) * t;
  const uint64_t s = static_cast<uint64_t>(span) * span;
  return from - static_cast<int>((from - to) * q / s);
}

inline Frame frameAt(uint32_t ms) {
  if (ms < COLLAPSE_MS) return {easeIn(FULL_HALF_H, LINE_HALF_H, ms, COLLAPSE_MS), FULL_HALF_W, 0, false};
  ms -= COLLAPSE_MS;
  if (ms < SHRINK_MS) return {LINE_HALF_H, easeIn(FULL_HALF_W, DOT_HALF, ms, SHRINK_MS), 0, false};
  ms -= SHRINK_MS;
  if (ms < FADE_MS) return {LINE_HALF_H, 0, static_cast<uint8_t>(255 - 255 * ms / FADE_MS), false};
  return {LINE_HALF_H, 0, 0, true};
}

}  // namespace poweroff
