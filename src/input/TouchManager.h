#pragma once

#include "app_types.h"
#include "input/GestureLogic.h"

// FT6336U/G capacitive touch on FNK0104B. Presence is decided at runtime by the I2C scan;
// on FNK0104A everything stays idle and the knobs do the job.
// Minimal register reader instead of Freenove's library, whose readByte() sleeps 10 ms
// per byte (docs/HARDWARE.md, conflict 7). Runs on loopTask, which owns the I2C bus.
class TouchManager {
 public:
  // Reset pulse on CTP_RST. Call early at boot; the controller needs ~300 ms before it
  // answers, which the boot screen covers before the I2C scan.
  void resetController();
  // present = 0x38 answered the I2C scan.
  void begin(bool present);
  bool present() const { return present_; }

  // Call every loop; reads the controller at TOUCH_POLL_MS.
  InputEvent poll(uint32_t nowMs);

  // Current contact in screen coordinates (diagnostics dot).
  bool touching() const { return down_; }
  TouchPoint point() const { return point_; }

 private:
  bool read(bool& down, int& rawX, int& rawY);

  GestureDetector gestures_;
  TouchAxes axes_{};
  bool present_ = false;
  bool down_ = false;
  TouchPoint point_{0, 0};
  uint32_t lastPollMs_ = 0;
  uint8_t readErrors_ = 0;
};
