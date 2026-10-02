#pragma once

// Click / double click / hold classification for one push button. Pure C++: no Arduino,
// tested on the host (test/host_tests.cpp). All time maths is unsigned subtraction, so it
// keeps working when millis() wraps after 49.7 days.

#include <stdint.h>

#include "app_types.h"
#include "config.h"

enum class ClickEvent : uint8_t { None, Single, Double, Long };

struct ClickTiming {
  uint32_t debounceMs = BUTTON_DEBOUNCE_MS;
  uint32_t doubleClickMs = BUTTON_DOUBLE_CLICK_MS;
  uint32_t longPressMs = BUTTON_LONG_PRESS_MS;
};

class ClickDetector {
 public:
  explicit ClickDetector(ClickTiming timing = ClickTiming{}) : t_(timing) {}

  // Feed the raw level every loop. Returns at most one event per call.
  // A single click is reported only once the double-click window has passed.
  // A click followed by a hold counts as one hold.
  ClickEvent update(bool rawPressed, uint32_t nowMs) {
    if (rawPressed != lastRaw_) {
      lastRaw_ = rawPressed;
      rawChangedMs_ = nowMs;
    }

    if (rawPressed != pressed_ && nowMs - rawChangedMs_ >= t_.debounceMs) {
      pressed_ = rawPressed;
      if (pressed_) {
        pressedMs_ = nowMs;
        longFired_ = false;
      } else if (!longFired_) {
        releasedMs_ = nowMs;
        if (++clicks_ >= 2) {
          clicks_ = 0;
          return ClickEvent::Double;
        }
      }
    }

    if (pressed_ && !longFired_ && nowMs - pressedMs_ >= t_.longPressMs) {
      longFired_ = true;
      clicks_ = 0;
      return ClickEvent::Long;
    }

    if (!pressed_ && clicks_ == 1 && nowMs - releasedMs_ >= t_.doubleClickMs) {
      clicks_ = 0;
      return ClickEvent::Single;
    }
    return ClickEvent::None;
  }

 private:
  ClickTiming t_;
  bool lastRaw_ = false;
  bool pressed_ = false;  // debounced level
  bool longFired_ = false;
  uint8_t clicks_ = 0;  // short clicks waiting for the double-click window
  uint32_t rawChangedMs_ = 0;
  uint32_t pressedMs_ = 0;
  uint32_t releasedMs_ = 0;
};

// The case's four keys (tele90), left to right. A press acts on release, at once: no double
// click to wait for. Holding CH+ opens the settings, holding VOL- mutes, holding CH- for
// POWER_HOLD_MS puts the TV in standby; holding VOL+ is just a slow press.
enum class FrontKey : uint8_t { ChDown, ChUp, VolDown, VolUp };

inline ClickTiming frontKeyTiming(FrontKey key) {
  ClickTiming t;
  t.doubleClickMs = 0;
  if (key == FrontKey::ChDown) t.longPressMs = POWER_HOLD_MS;  // switching off takes intent
  return t;
}

inline InputEvent frontKeyEvent(FrontKey key, ClickEvent e) {
  if (e == ClickEvent::None) return InputEvent::None;
  const bool hold = e == ClickEvent::Long;
  switch (key) {
    case FrontKey::ChDown: return hold ? InputEvent::Power : InputEvent::ChPrev;
    case FrontKey::ChUp: return hold ? InputEvent::Menu : InputEvent::ChNext;
    case FrontKey::VolDown: return hold ? InputEvent::Mute : InputEvent::VolDown;
    case FrontKey::VolUp: return InputEvent::VolUp;
  }
  return InputEvent::None;
}

// BOOT (a bare board's only button): click = next, double = previous, hold = menu.
inline InputEvent channelButtonEvent(ClickEvent e) {
  switch (e) {
    case ClickEvent::Single: return InputEvent::ChNext;
    case ClickEvent::Double: return InputEvent::ChPrev;
    case ClickEvent::Long: return InputEvent::Menu;
    case ClickEvent::None: break;
  }
  return InputEvent::None;
}
