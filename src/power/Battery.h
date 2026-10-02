#pragma once

// Battery indicator: how much charge the LiPo has left, from its voltage (GPIO9 through the
// 200K/200K divider), and when to warn. Pure C++: tested on the host (test/battery_tests.cpp).
// With the USB-C plugged in, the TP4054 charges the cell and the reading is the charging
// voltage; with no cell at all it is the charger's output (~4.1 V), so a bare board on USB
// shows a nearly full battery.

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t BATTERY_UNKNOWN = 255;  // no reading yet
constexpr uint8_t BATTERY_LOW_PCT = 15;
constexpr uint8_t BATTERY_CRITICAL_PCT = 5;
constexpr uint8_t BATTERY_HYSTERESIS_PCT = 5;  // back above a threshold by this much to clear it
// Flat (~3.42 V): the TV goes to standby so the cell is not drained further. Only after this many
// readings in a row (~30 s): a sag under load does not trigger it.
constexpr uint8_t BATTERY_EMPTY_PCT = 2;
constexpr uint8_t BATTERY_EMPTY_READINGS = 6;
// Charging. Nothing on the board tells the USB cable apart: the TP4054's CHRG pin goes to no GPIO.
// The reading jumps ~100 mV when the cable goes in or out (measured 2026-10-01: 3942 <-> 4043 mV);
// a climb over a few minutes means charging too (switched on with the cable already in).
constexpr uint32_t BATTERY_PLUG_STEP_MV = 60;    // one odd reading moves a 2-reading mean only half
constexpr uint32_t BATTERY_TREND_READINGS = 36;  // 3 min at BATTERY_READ_MS
constexpr uint32_t BATTERY_TREND_MV = 12;

// A typical one-cell LiPo under a light load (the TV draws ~0.3 A), linear between points.
inline uint8_t batteryPercent(uint32_t mv) {
  struct Point {
    uint32_t mv;
    uint32_t pct;
  };
  static constexpr Point CURVE[] = {{3300, 0},  {3500, 5},  {3600, 10}, {3700, 25}, {3750, 35}, {3800, 45},
                                    {3850, 55}, {3900, 62}, {4000, 75}, {4100, 88}, {4200, 100}};
  constexpr size_t N = sizeof(CURVE) / sizeof(CURVE[0]);
  if (mv <= CURVE[0].mv) return 0;
  for (size_t i = 1; i < N; ++i) {
    if (mv <= CURVE[i].mv) {
      const Point& a = CURVE[i - 1];
      const Point& b = CURVE[i];
      return static_cast<uint8_t>(a.pct + (mv - a.mv) * (b.pct - a.pct) / (b.mv - a.mv));
    }
  }
  return 100;
}

enum class BatteryLevel : uint8_t { Ok, Low, Critical };

// Smooths the readings (the voltage dips with the load) and keeps the warning level. A level
// is entered at once and left only BATTERY_HYSTERESIS_PCT above its threshold, so a reading
// wobbling around 15 % warns once, not every few seconds.
class BatteryMonitor {
 public:
  // One reading in mV. True when the level just got worse: time to warn.
  bool update(uint32_t mv) {
    filteredMv_ = filteredMv_ == 0 ? mv : (filteredMv_ * 3 + mv) / 4;
    const uint8_t pct = batteryPercent(filteredMv_);
    emptyRun_ = pct > BATTERY_EMPTY_PCT ? 0 : emptyRun_ < BATTERY_EMPTY_READINGS ? emptyRun_ + 1 : emptyRun_;
    trackCharging(mv);
    const BatteryLevel target = pct <= BATTERY_CRITICAL_PCT ? BatteryLevel::Critical
                                : pct <= BATTERY_LOW_PCT   ? BatteryLevel::Low
                                                           : BatteryLevel::Ok;
    if (target > level_) {
      level_ = target;
      return true;
    }
    if (target < level_) {
      const uint8_t bound = level_ == BatteryLevel::Critical ? BATTERY_CRITICAL_PCT : BATTERY_LOW_PCT;
      if (pct > bound + BATTERY_HYSTERESIS_PCT) level_ = target;
    }
    return false;
  }

  uint8_t percent() const { return filteredMv_ == 0 ? BATTERY_UNKNOWN : batteryPercent(filteredMv_); }
  uint32_t millivolts() const { return filteredMv_; }
  BatteryLevel level() const { return level_; }
  // Flat for BATTERY_EMPTY_READINGS readings in a row: time for standby.
  bool empty() const { return emptyRun_ >= BATTERY_EMPTY_READINGS; }
  bool charging() const { return charging_; }
  // True once after charging started (the cable went in): time for the notice.
  bool takePlugged() {
    const bool p = plugged_;
    plugged_ = false;
    return p;
  }

 private:
  uint32_t filteredMv_ = 0;  // 0 = no reading yet
  // A step between the mean of the last two raw readings and the two before; or the trend of the
  // filtered reading over BATTERY_TREND_READINGS.
  void trackCharging(uint32_t mv) {
    raw_[rawCount_++ % 4] = mv;
    if (rawCount_ >= 4) {
      const uint32_t newer = (raw_[(rawCount_ - 1) % 4] + raw_[(rawCount_ - 2) % 4]) / 2;
      const uint32_t older = (raw_[(rawCount_ - 3) % 4] + raw_[(rawCount_ - 4) % 4]) / 2;
      if (newer >= older + BATTERY_PLUG_STEP_MV) setCharging(true);
      if (older >= newer + BATTERY_PLUG_STEP_MV) setCharging(false);
    }
    if (trendReadings_ == 0) trendBaseMv_ = filteredMv_;
    if (++trendReadings_ >= BATTERY_TREND_READINGS) {
      if (filteredMv_ >= trendBaseMv_ + BATTERY_TREND_MV) setCharging(true);
      if (trendBaseMv_ >= filteredMv_ + BATTERY_TREND_MV) setCharging(false);
      trendReadings_ = 0;
    }
  }
  void setCharging(bool on) {
    if (on && !charging_) plugged_ = true;
    charging_ = on;
  }

  BatteryLevel level_ = BatteryLevel::Ok;
  uint8_t emptyRun_ = 0;
  uint32_t raw_[4] = {};
  uint32_t rawCount_ = 0;
  uint32_t trendBaseMv_ = 0;
  uint32_t trendReadings_ = 0;
  bool charging_ = false;
  bool plugged_ = false;
};
