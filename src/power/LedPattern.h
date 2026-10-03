#pragma once

// What the front LED shows besides "on while the TV runs" and "out for a moment with each order" (pure C++,
// host-tested in test/battery_tests.cpp). The callers drive the pin; these only say on or off at a time.
//
//   GRABADORA recording   blinks at 2 Hz: the microphone records only with the LED blinking
//   STANDBY WI-FI         a short heartbeat every few seconds: the web remote can switch it on (deep sleep: dark)
//   battery low, standby  a double flash every LED_LOW_BATTERY_MS, whatever the standby
//   woken                 on at once, before the restart (a word or claps: "I heard you")

#include <stdint.h>

#include "config.h"

inline bool recordingLed(uint32_t ms) { return (ms / LED_REC_HALF_MS) % 2 == 0; }

// heartbeat: STANDBY WI-FI. batteryLow: the monitor's level is Low or Critical.
inline bool standbyLed(uint32_t ms, bool heartbeat, bool batteryLow) {
  if (batteryLow) {
    const uint32_t t = ms % LED_LOW_BATTERY_MS;
    if (t < LED_LOW_FLASH_MS || (t >= 2 * LED_LOW_FLASH_MS && t < 3 * LED_LOW_FLASH_MS)) return true;
  }
  return heartbeat && ms % LED_HEARTBEAT_MS < LED_HEARTBEAT_ON_MS;
}
