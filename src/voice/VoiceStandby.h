#pragma once

// What claps mean while the TV is in voice standby (pure C++, host-tested in test/voice_tests.cpp).
// docs/VOICE.md: every clap heard flashes the front LED ("I heard you"). The TV wakes on a sequence
// of exactly CLAP_POWER_CLAPS (3) claps that passed the detector's checks (hand rhythm, even loudness, a
// clap's brightness) with quiet around it: no bang in the STANDBY_QUIET_BEFORE_MS before the first clap,
// and none in the STANDBY_QUIET_AFTER_MS after the sequence closed. Household noises come in
// clusters (a pair of bangs once passed every other check); a person claps after a moment of quiet.
// A wake that cannot happen (the battery is flat) is cancelled and the TV keeps listening.

#include <stdint.h>

#include "config.h"

enum class StandbyAction : uint8_t { None, Blink, Wake };

class VoiceStandby {
 public:
  // A clap was heard; index is its place in the current sequence (1, 2, 3...).
  StandbyAction onClap(uint8_t index) const {
    return index >= 1 && !waking_ ? StandbyAction::Blink : StandbyAction::None;
  }
  // A sequence closed and passed the checks at nowMs; bangs = the detector's count of every bang so
  // far. CLAP_POWER_CLAPS claps after enough quiet start the wait for quiet after.
  void onSequence(uint8_t claps, uint32_t quietBeforeMs, uint32_t bangs, uint32_t nowMs) {
    pending_ = !waking_ && claps == CLAP_POWER_CLAPS && quietBeforeMs >= STANDBY_QUIET_BEFORE_MS;
    pendingBangs_ = bangs;
    pendingSinceMs_ = nowMs;
  }
  // Every pass of the standby loop: Wake once the quiet after has lasted; any bang cancels.
  StandbyAction update(uint32_t bangs, uint32_t nowMs) {
    if (!pending_) return StandbyAction::None;
    if (bangs != pendingBangs_) {
      pending_ = false;
      return StandbyAction::None;
    }
    if (nowMs - pendingSinceMs_ < STANDBY_QUIET_AFTER_MS) return StandbyAction::None;
    pending_ = false;
    waking_ = true;
    return StandbyAction::Wake;
  }
  bool pending() const { return pending_; }
  bool waking() const { return waking_; }
  void cancelWake() { waking_ = false; }

 private:
  bool pending_ = false;
  bool waking_ = false;
  uint32_t pendingBangs_ = 0;
  uint32_t pendingSinceMs_ = 0;
};
