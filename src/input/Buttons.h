#pragma once

#include "app_types.h"
#include "input/ButtonLogic.h"

// The case's four keys (CH-, CH+, VOL-, VOL+: board_config.h) and BOOT (GPIO0: the only button
// of a bare board). Push buttons to GND, internal pull-ups.
class Buttons {
 public:
  void begin();
  // Call every loop. At most one event per call.
  InputEvent poll(uint32_t nowMs);
  bool anyKeyDown() const;  // a key or BOOT pressed right now (raw level)
  // The key GPIOs, for waking from standby.
  static uint64_t wakeMask();

 private:
  static constexpr int KEY_COUNT = 4;
  ClickDetector keys_[KEY_COUNT] = {
      ClickDetector(frontKeyTiming(FrontKey::ChDown)), ClickDetector(frontKeyTiming(FrontKey::ChUp)),
      ClickDetector(frontKeyTiming(FrontKey::VolDown)), ClickDetector(frontKeyTiming(FrontKey::VolUp))};
  bool heldAtBoot_[KEY_COUNT] = {};  // the key that woke the TV: ignored until released
  bool bootHeldAtBoot_ = false;      // the same for BOOT
  ClickDetector boot_;
};
