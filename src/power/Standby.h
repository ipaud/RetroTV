#pragma once

// What "switch off" does (pure C++, host-tested in test/battery_tests.cpp). App::enterStandby acts on it.
//
// With front keys, off is the deep sleep (AHORRO MAX, a key wakes it) or STANDBY VOZ if chosen in
// AJUSTES > VOZ > APAGADO. Without keys (the button-less tele90 v10, PAUTV_HAS_KEYS 0) nothing could
// wake a deep sleep, BOOT being inside the case: off is STANDBY VOZ (three claps switch it on); if that
// cannot listen (no voice build, microphone or claps off), the remote standby (Wi-Fi left on: the web
// remote switches it on); with neither, the TV stays on. A flat battery always ends in a deep sleep;
// without keys that sleep wakes itself now and then to see whether the cell is charging.

#include <stdint.h>

enum class StandbyMode : uint8_t { StayOn, Voice, Remote, Deep };

// keys: a key can wake the chip. userAsked: false for a flat battery. voicePossible: STANDBY VOZ can
// listen (voice build, microphone and claps on). voiceChosen: APAGADO = STANDBY VOZ. wifiOnline: the
// web remote can reach the TV now.
inline StandbyMode standbyMode(bool keys, bool userAsked, bool voicePossible, bool voiceChosen, bool wifiOnline) {
  if (!userAsked) return StandbyMode::Deep;
  if (voicePossible && (voiceChosen || !keys)) return StandbyMode::Voice;
  if (keys) return StandbyMode::Deep;
  return wifiOnline ? StandbyMode::Remote : StandbyMode::StayOn;
}

// Button-less, woken by the timer from a flat-battery sleep: switch on only once the reading says the
// cell is charging or charged; a flat cell resting without load reads ~3.5-3.6 V and sleeps again.
inline bool flatCheckSwitchOn(uint32_t batteryMv, uint32_t resumeMv) { return batteryMv >= resumeMv; }
