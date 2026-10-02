#pragma once

// Touch gesture classification and axis mapping. Pure C++: no Arduino, tested on the host
// (test/host_tests.cpp). Unsigned time maths survives the millis() wrap.

#include <stdint.h>

#include "app_types.h"
#include "config.h"

enum class Gesture : uint8_t { None, Tap, SwipeLeft, SwipeRight, SwipeUp, SwipeDown, LongPress };

struct GestureConfig {
  int tapMaxPx = TOUCH_TAP_MAX_PX;
  int swipeMinPx = TOUCH_SWIPE_MIN_PX;
  uint32_t longPressMs = TOUCH_LONG_PRESS_MS;
};

class GestureDetector {
 public:
  explicit GestureDetector(GestureConfig config = GestureConfig{}) : c_(config) {}

  // Feed every poll with the current contact (screen coordinates). Tap and swipes are
  // reported on release; a long press fires once while the finger is still down.
  Gesture update(bool down, int x, int y, uint32_t nowMs) {
    if (down) {
      if (!active_) {
        active_ = true;
        longFired_ = false;
        x0_ = x_ = x;
        y0_ = y_ = y;
        t0_ = nowMs;
        return Gesture::None;
      }
      x_ = x;
      y_ = y;
      if (!longFired_ && isStill() && nowMs - t0_ >= c_.longPressMs) {
        longFired_ = true;
        return Gesture::LongPress;
      }
      return Gesture::None;
    }

    if (!active_) return Gesture::None;
    active_ = false;
    if (longFired_) return Gesture::None;
    if (isStill()) return Gesture::Tap;

    const int dx = x_ - x0_;
    const int dy = y_ - y0_;
    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;
    if ((adx > ady ? adx : ady) < c_.swipeMinPx) return Gesture::None;
    if (adx >= ady) return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;  // screen y grows downwards
  }

 private:
  bool isStill() const {
    const int dx = x_ - x0_;
    const int dy = y_ - y0_;
    return dx <= c_.tapMaxPx && dx >= -c_.tapMaxPx && dy <= c_.tapMaxPx && dy >= -c_.tapMaxPx;
  }

  GestureConfig c_;
  bool active_ = false;
  bool longFired_ = false;
  int x0_ = 0, y0_ = 0, x_ = 0, y_ = 0;
  uint32_t t0_ = 0;
};

// Tap = OSD, left/right = channel, up/down = volume, hold = menu.
inline InputEvent gestureToEvent(Gesture g) {
  switch (g) {
    case Gesture::Tap: return InputEvent::ToggleOsd;
    case Gesture::SwipeLeft: return InputEvent::ChPrev;
    case Gesture::SwipeRight: return InputEvent::ChNext;
    case Gesture::SwipeUp: return InputEvent::VolUp;
    case Gesture::SwipeDown: return InputEvent::VolDown;
    case Gesture::LongPress: return InputEvent::Menu;
    case Gesture::None: break;
  }
  return InputEvent::None;
}

// How the controller's native portrait frame maps onto the landscape screen.
// A capacitive panel needs no calibration: only these three flags.
struct TouchAxes {
  bool swapXY;
  bool invertX;
  bool invertY;
};

struct TouchPoint {
  int16_t x;
  int16_t y;
};

// Board flags describe rotation 1; rotation 3 is the same picture turned 180 degrees.
inline TouchAxes axesForRotation(TouchAxes rotation1, uint8_t rotation) {
  if (rotation != 3) return rotation1;
  return TouchAxes{rotation1.swapXY, !rotation1.invertX, !rotation1.invertY};
}

inline TouchPoint mapTouch(int rawX, int rawY, TouchAxes axes, int screenW, int screenH) {
  int x = axes.swapXY ? rawY : rawX;
  int y = axes.swapXY ? rawX : rawY;
  if (axes.invertX) x = screenW - 1 - x;
  if (axes.invertY) y = screenH - 1 - y;
  x = x < 0 ? 0 : (x >= screenW ? screenW - 1 : x);
  y = y < 0 ? 0 : (y >= screenH ? screenH - 1 : y);
  return TouchPoint{static_cast<int16_t>(x), static_cast<int16_t>(y)};
}
