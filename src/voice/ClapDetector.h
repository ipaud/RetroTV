#pragma once

// Claps from the microphone, without machine learning (pure C++, host-tested in
// test/voice_tests.cpp). docs/VOICE.md has the reasoning and the numbers.
//
//   PCM -> DC blocker -> 5 ms energy windows -> noise floor (slow EMA) -> threshold
//       -> transient: a sharp rise over the threshold that dies away within CLAP_MAX_MS
//       -> a bright one (CLAP_HF_MIN_SHARE of its energy above CLAP_HF_HZ): a clap, not a knock
//       -> sequence: claps up to CLAP_GAP_MAX_MS apart, checked and reported once the window
//          closes: 2 or 3 claps in a hand rhythm and of similar loudness; 4 or more is not one
//
// A level over the threshold alone is never a clap: speech, music and a door closing rise too
// slowly or last too long. After a clap its echo is ignored for CLAP_ECHO_MS. The sequence is
// reported only when CLAP_GAP_MAX_MS pass with no new clap, so two claps never act before a
// third one could still come. Times are millis(): differences only, so the wrap is harmless.

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "voice/Biquad.h"
#include "voice/MicMeter.h"
#include "voice/PlaybackEnvelope.h"

class ClapDetector {
 public:
  struct Stats {
    uint32_t claps = 0;       // single transients accepted
    uint32_t sequences[4] = {};  // [1] singles, [2] doubles, [3] triples and longer
    uint32_t sustained = 0;   // loud but too long: not a clap
    uint32_t suppressed = 0;  // claps while the TV made its own sound (ignored)
    uint32_t fromTv = 0;      // claps that matched a bang in the programme's own sound (ignored)
    uint32_t dull = 0;        // transients without a clap's brightness (a knock, a door)
    uint32_t badSequences = 0;  // sequences that were not claps: rhythm, loudness or too many
  };

  explicit ClapDetector(uint32_t sampleRate = AUDIO_SAMPLE_RATE)
      : rate_(sampleRate),
        window_(sampleRate * CLAP_WINDOW_MS / 1000 > 0 ? sampleRate * CLAP_WINDOW_MS / 1000 : 1),
        hf_(Biquad::highPass(CLAP_HF_HZ, static_cast<float>(sampleRate))) {
    setSensitivity(CLAP_SENSITIVITY_DEFAULT);
  }

  // 0..100: 100 hears a soft clap over the room noise, 0 only a very loud one.
  void setSensitivity(uint8_t s) {
    if (s > 100) s = 100;
    sensitivity_ = s;
    marginDb10_ = static_cast<int16_t>(CLAP_MARGIN_MAX_DB10 -
                                       (CLAP_MARGIN_MAX_DB10 - CLAP_MARGIN_MIN_DB10) * s / 100);
  }
  uint8_t sensitivity() const { return sensitivity_; }
  // The lowest level a clap may have (CLAP_MIN_DB10; STANDBY VOZ asks for more).
  void setMinDb10(int16_t db10) { minDb10_ = db10; }
  // While the TV plays sound the threshold goes up by this much (its own speaker is close).
  void setExtraMarginDb10(int16_t db10) { extraDb10_ = db10 < 0 ? 0 : db10; }
  // The TV is making a sound of its own (static, a beep) until then: no claps are taken.
  void suppressUntil(uint32_t ms) { suppressUntilMs_ = ms; suppressing_ = true; }
  // What the speaker plays: a clap that lines up with a sharp rise in it is the programme.
  void setPlayback(const PlaybackEnvelope* playback) { playback_ = playback; }

  // Feeds a block of mono samples whose last sample was taken at endMs.
  void feed(const int16_t* mono, size_t n, uint32_t endMs) {
    for (size_t i = 0; i < n; ++i) {
      const float y = dc_.step(mono[i]);
      const float h = hf_.step(y);
      sumSquares_ += y * y;
      hfSquares_ += h * h;
      if (++inWindow_ < window_) continue;
      const float power = sumSquares_ / static_cast<float>(inWindow_);
      const float energy = sumSquares_;
      const float hfEnergy = hfSquares_;
      sumSquares_ = 0.0f;
      hfSquares_ = 0.0f;
      inWindow_ = 0;
      const uint32_t t = endMs - static_cast<uint32_t>((static_cast<uint64_t>(n - 1 - i) * 1000u) / rate_);
      onWindow(power, energy, hfEnergy, t);
    }
    closeSequence(endMs);
  }

  // A finished sequence that passed the checks: its clap count (1, 2 or 3), or 0. Clears it.
  uint8_t takeSequence() {
    const uint8_t s = finished_;
    finished_ = 0;
    return s;
  }
  // Once for every accepted clap, as it happens: its place in the sequence (1, 2, 3...), else 0.
  // Voice standby flashes the LED on the first and wakes on the second.
  uint8_t takeClap() {
    const uint8_t c = clapNow_;
    clapNow_ = 0;
    return c;
  }
  int16_t floorDb10() const { return static_cast<int16_t>(lroundf(floorDb10f_)); }
  int16_t thresholdDb10() const { return threshold(); }
  int16_t lastPeakDb10() const { return lastPeakDb10_; }
  uint32_t lastGapMs() const { return lastGapMs_; }
  // How much the programme's own sound rose around the last accepted clap (for tuning).
  int16_t lastTvRiseDb10() const { return lastTvRiseDb10_; }
  // The last transient dropped as the programme's own sound: its peak and the floor then.
  int16_t tvPeakDb10() const { return tvPeakDb10_; }
  int16_t tvFloorDb10() const { return tvFloorDb10_; }
  // Share of the last transient's energy above CLAP_HF_HZ (claps ~0.5+, knocks less).
  float lastHfShare() const { return lastHfShare_; }
  // How long it was quiet before the last reported sequence's first clap (UINT32_MAX: nothing heard
  // before), and a count of every bang or noise so far: STANDBY VOZ wants quiet around its claps.
  uint32_t lastQuietBeforeMs() const { return lastQuietBeforeMs_; }
  uint32_t bangs() const { return bangs_; }
  // Why the last sequence was not reported ("too many", "rhythm", "loudness"), or "".
  const char* lastRejected() const { return lastRejected_; }
  const Stats& stats() const { return stats_; }

 private:
  enum class State : uint8_t { Quiet, Rising, Loud };
  static constexpr float FULL_SCALE = 32768.0f;

  static int16_t db10Of(float power) { return micDb10(sqrtf(power) / FULL_SCALE); }

  int16_t threshold() const {
    int32_t t = floorDb10() + marginDb10_ + extraDb10_;
    if (t < minDb10_) t = minDb10_;
    return static_cast<int16_t>(t > 0 ? 0 : t);
  }

  void onWindow(float power, float energy, float hfEnergy, uint32_t t) {
    const int16_t level = db10Of(power);
    if (!floorSet_) {  // the first window only tells what the room sounds like
      floorDb10f_ = level;
      floorSet_ = true;
      prevDb10_ = prev2Db10_ = level;
      return;
    }
    const int16_t over = threshold();
    switch (state_) {
      case State::Quiet: {
        // A clap may straddle two windows: the rise counts from the lower of the two before.
        const int16_t base = prevDb10_ < prev2Db10_ ? prevDb10_ : prev2Db10_;
        const bool sharp = level >= over && level - base >= CLAP_ATTACK_DB10;
        if (sharp) {  // any bang, clap or not: the quiet around a sequence is measured from it
          quietBeforeCand_ = activitySeen_ ? t - lastActivityMs_ : UINT32_MAX;
          lastActivityMs_ = t;
          activitySeen_ = true;
          ++bangs_;
        }
        if (sharp && !(clapCount_ > 0 && t - lastClapMs_ < CLAP_ECHO_MS)) {
          state_ = State::Rising;
          riseMs_ = t;
          peakDb10_ = level;
          candEnergy_ = energy;
          candHf_ = hfEnergy;
        }
        break;
      }
      case State::Rising:
        if (level > peakDb10_) peakDb10_ = level;
        candEnergy_ += energy;
        candHf_ += hfEnergy;
        if (level <= peakDb10_ - CLAP_DECAY_DB10) {
          state_ = State::Quiet;
          accept();
        } else if (t - riseMs_ > CLAP_MAX_MS) {
          state_ = State::Loud;  // speech, music, a long noise
          ++stats_.sustained;
        }
        break;
      case State::Loud:
        lastActivityMs_ = t;  // a long noise is not quiet either
        ++bangs_;
        if (level < over) state_ = State::Quiet;
        break;
    }
    // The floor follows everything but a possible clap: a steady loud room (or a long sound)
    // lifts the threshold over itself within a couple of seconds.
    if (state_ != State::Rising) learnFloor(level);
    prev2Db10_ = prevDb10_;
    prevDb10_ = level;
  }

  // In dB, not power: a clap's tail moves it by about a dB instead of several.
  void learnFloor(int16_t levelDb10) { floorDb10f_ += (static_cast<float>(levelDb10) - floorDb10f_) * CLAP_FLOOR_ALPHA; }

  void accept() {
    lastHfShare_ = candEnergy_ > 0.0f ? candHf_ / candEnergy_ : 0.0f;
    if (lastHfShare_ < CLAP_HF_MIN_SHARE) {  // a knock, a door, a thump: not bright enough
      ++stats_.dull;
      return;
    }
    if (suppressing_ && static_cast<int32_t>(riseMs_ - suppressUntilMs_) < 0) {
      ++stats_.suppressed;
      return;
    }
    if (playback_ != nullptr) {
      lastTvRiseDb10_ = playback_->maxRise(riseMs_ - CLAP_TV_LOOKBACK_MS, riseMs_ + CLAP_TV_LOOKAHEAD_MS, CLAP_TV_MIN_DB10);
      const bool handLoud = peakDb10_ >= CLAP_TV_OVER_MIN_DB10 && peakDb10_ - floorDb10() >= CLAP_TV_OVER_FLOOR_DB10;
      if (lastTvRiseDb10_ >= CLAP_TV_RISE_DB10 && !handLoud) {
        ++stats_.fromTv;
        tvPeakDb10_ = peakDb10_;  // for the log: how loud the dropped one was, over which floor
        tvFloorDb10_ = floorDb10();
        return;
      }
    }
    suppressing_ = false;
    ++stats_.claps;
    lastPeakDb10_ = peakDb10_;
    if (clapCount_ > 0) {
      lastGapMs_ = riseMs_ - lastClapMs_;
      if (lastGapMs_ > seqMaxGapMs_) seqMaxGapMs_ = lastGapMs_;
    } else {
      seqMaxGapMs_ = 0;
      seqLoudest_ = seqQuietest_ = peakDb10_;
      seqQuietBeforeMs_ = quietBeforeCand_;
    }
    if (peakDb10_ > seqLoudest_) seqLoudest_ = peakDb10_;
    if (peakDb10_ < seqQuietest_) seqQuietest_ = peakDb10_;
    if (clapCount_ < 255) ++clapCount_;
    lastClapMs_ = riseMs_;
    clapNow_ = clapCount_;
  }

  // A closed sequence is reported only if it is what hands do: one clap; or two or three claps
  // CLAP_ECHO_MS..CLAP_RHYTHM_MAX_MS apart and within CLAP_PEAK_SPREAD_DB10 of each other.
  void closeSequence(uint32_t nowMs) {
    if (clapCount_ == 0 || state_ == State::Rising || nowMs - lastClapMs_ <= CLAP_GAP_MAX_MS) return;
    const uint8_t n = clapCount_;
    clapCount_ = 0;
    const bool rhythm = n == 1 || seqMaxGapMs_ <= CLAP_RHYTHM_MAX_MS;
    const bool even = n == 1 || seqLoudest_ - seqQuietest_ <= CLAP_PEAK_SPREAD_DB10;
    if (n > CLAP_SEQUENCE_MAX || !rhythm || !even) {
      ++stats_.badSequences;
      lastRejected_ = n > CLAP_SEQUENCE_MAX ? "too many" : (!rhythm ? "rhythm" : "loudness");
      return;
    }
    ++stats_.sequences[n];
    finished_ = n;
    lastQuietBeforeMs_ = seqQuietBeforeMs_;
  }

  uint32_t rate_;
  uint32_t window_;
  DcBlocker dc_;
  Biquad hf_;
  float sumSquares_ = 0.0f;
  float hfSquares_ = 0.0f;
  float candEnergy_ = 0.0f;
  float candHf_ = 0.0f;
  float lastHfShare_ = 0.0f;
  int16_t minDb10_ = CLAP_MIN_DB10;
  uint32_t seqMaxGapMs_ = 0;
  uint32_t bangs_ = 0;
  uint32_t lastActivityMs_ = 0;
  bool activitySeen_ = false;
  uint32_t quietBeforeCand_ = UINT32_MAX;
  uint32_t seqQuietBeforeMs_ = UINT32_MAX;
  uint32_t lastQuietBeforeMs_ = UINT32_MAX;
  int16_t seqLoudest_ = MIC_FLOOR_DB10;
  int16_t seqQuietest_ = MIC_FLOOR_DB10;
  const char* lastRejected_ = "";
  uint32_t inWindow_ = 0;
  float floorDb10f_ = MIC_FLOOR_DB10;
  bool floorSet_ = false;
  uint8_t sensitivity_ = 0;
  int16_t marginDb10_ = 0;
  int16_t extraDb10_ = 0;
  uint32_t suppressUntilMs_ = 0;
  bool suppressing_ = false;
  const PlaybackEnvelope* playback_ = nullptr;

  State state_ = State::Quiet;
  int16_t prevDb10_ = MIC_FLOOR_DB10;
  int16_t prev2Db10_ = MIC_FLOOR_DB10;
  int16_t peakDb10_ = MIC_FLOOR_DB10;
  uint32_t riseMs_ = 0;

  uint8_t clapCount_ = 0;
  uint32_t lastClapMs_ = 0;
  uint8_t finished_ = 0;
  uint8_t clapNow_ = 0;
  int16_t lastPeakDb10_ = MIC_FLOOR_DB10;
  uint32_t lastGapMs_ = 0;
  int16_t lastTvRiseDb10_ = 0;
  int16_t tvPeakDb10_ = MIC_FLOOR_DB10;
  int16_t tvFloorDb10_ = MIC_FLOOR_DB10;
  Stats stats_;
};
