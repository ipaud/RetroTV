// Battery indicator: charge from the LiPo voltage, and warnings that do not flap. Run:
// tools/run_host_tests.sh

#include "check.h"
#include "power/Battery.h"
#include "power/LedPattern.h"
#include "power/Standby.h"

static void testPercent() {
  CHECK(batteryPercent(4250) == 100 && batteryPercent(4200) == 100);
  CHECK(batteryPercent(3300) == 0 && batteryPercent(2900) == 0);
  CHECK(batteryPercent(3800) == 45);
  const uint8_t mid = batteryPercent(3825);  // between 3800 (45 %) and 3850 (55 %)
  CHECK(mid == 50);
  uint8_t last = 0;
  bool monotonic = true;
  for (uint32_t mv = 3300; mv <= 4200; mv += 10) {
    monotonic = monotonic && batteryPercent(mv) >= last;
    last = batteryPercent(mv);
  }
  CHECK(monotonic);
}

static void testWarnings() {
  BatteryMonitor b;
  CHECK(b.percent() == BATTERY_UNKNOWN);
  CHECK(!b.update(4000) && b.level() == BatteryLevel::Ok && b.percent() == 75);  // first reading: no filter lag
  // Slowly down: one warning when it goes low, one when critical, nothing in between.
  int warnings = 0;
  for (uint32_t mv = 4000; mv >= 3400; mv -= 5) {
    if (b.update(mv)) ++warnings;
  }
  CHECK(warnings == 2 && b.level() == BatteryLevel::Critical);
  // Noise around a threshold does not warn again: it must come back well above it first.
  BatteryMonitor n;
  n.update(3630);  // 14 %: low at once
  CHECK(n.level() == BatteryLevel::Low);
  int again = 0;
  for (int i = 0; i < 50; ++i) {
    if (n.update(i % 2 ? 3660 : 3630)) ++again;  // wobbling around 15 %
  }
  CHECK(again == 0 && n.level() == BatteryLevel::Low);
  for (int i = 0; i < 30; ++i) n.update(3900);  // charged
  CHECK(n.level() == BatteryLevel::Ok);
  // A low battery at power-on is a warning too.
  BatteryMonitor boot;
  CHECK(boot.update(3500) && boot.level() == BatteryLevel::Critical);
}

// A flat cell sends the TV to standby, but only after a run of readings at the bottom: a sag
// under load, or a cable plugged in meanwhile, starts the run again.
static void testEmpty() {
  BatteryMonitor flat;
  for (int i = 0; i < BATTERY_EMPTY_READINGS - 1; ++i) flat.update(3300);
  CHECK(!flat.empty());
  flat.update(3300);
  CHECK(flat.empty());
  BatteryMonitor plugged;
  for (int i = 0; i < BATTERY_EMPTY_READINGS - 1; ++i) plugged.update(3300);
  plugged.update(4150);  // USB-C in: the charger lifts the reading
  plugged.update(3300);
  CHECK(!plugged.empty());
  BatteryMonitor fine;
  for (int i = 0; i < 100; ++i) fine.update(3700);
  CHECK(!fine.empty());
}

// The board cannot see the USB cable: the charger lifts the reading ~100 mV at once (measured
// 2026-10-01: 3942 -> 4043 mV), unplugging drops it as much. A slow climb means charging too
// (the TV was switched on already plugged in).
static void testCharging() {
  BatteryMonitor b;
  for (int i = 0; i < 6; ++i) b.update(3940);
  CHECK(!b.charging() && !b.takePlugged());
  for (int i = 0; i < 6; ++i) b.update(4040);  // cable in
  CHECK(b.charging() && b.takePlugged() && !b.takePlugged());  // the notice once
  for (int i = 0; i < 6; ++i) b.update(3940);  // cable out
  CHECK(!b.charging() && !b.takePlugged());
  BatteryMonitor spike;  // one odd reading is not a cable
  for (int i = 0; i < 6; ++i) spike.update(3940);
  spike.update(4040);
  for (int i = 0; i < 6; ++i) spike.update(3940);
  CHECK(!spike.charging() && !spike.takePlugged());
  BatteryMonitor rising;  // switched on with the cable in: no step, a steady climb
  for (uint32_t i = 0; i < 2 * BATTERY_TREND_READINGS; ++i) rising.update(3900 + i / 2);
  CHECK(rising.charging());
  for (uint32_t i = 0; i < 2 * BATTERY_TREND_READINGS; ++i) rising.update(3970 - i / 2);  // on battery again
  CHECK(!rising.charging());
}

static void testStandbyMode() {
  // With keys: as before. APAGADO picks; STANDBY VOZ only if it can listen; a flat battery sleeps deep.
  CHECK(standbyMode(true, true, true, false, true) == StandbyMode::Deep);
  CHECK(standbyMode(true, true, true, true, true) == StandbyMode::Voice);
  CHECK(standbyMode(true, true, false, true, true) == StandbyMode::Deep);
  CHECK(standbyMode(true, false, true, true, true) == StandbyMode::Deep);
  // Button-less: never a deep sleep the user asked for. STANDBY VOZ whatever APAGADO says; without
  // claps, the remote standby while the web remote can reach it; with neither, stay on.
  CHECK(standbyMode(false, true, true, false, false) == StandbyMode::Voice);
  CHECK(standbyMode(false, true, true, false, true) == StandbyMode::Voice);
  CHECK(standbyMode(false, true, false, false, true) == StandbyMode::Remote);
  CHECK(standbyMode(false, true, false, true, true) == StandbyMode::Remote);
  CHECK(standbyMode(false, true, false, false, false) == StandbyMode::StayOn);
  CHECK(standbyMode(false, false, true, true, true) == StandbyMode::Deep);  // flat: sleeps, waking itself to check
  // The timer check: a resting flat cell sleeps on, a charging one switches on.
  CHECK(!flatCheckSwitchOn(3550, 3700));
  CHECK(flatCheckSwitchOn(3700, 3700));
  CHECK(flatCheckSwitchOn(4120, 3700));
}

static void testLedPatterns() {
  CHECK(recordingLed(0) && recordingLed(LED_REC_HALF_MS - 1));
  CHECK(!recordingLed(LED_REC_HALF_MS) && recordingLed(2 * LED_REC_HALF_MS));
  // Dark standby: nothing to show.
  CHECK(!standbyLed(0, false, false) && !standbyLed(12345, false, false));
  // STANDBY WI-FI: one short heartbeat per period.
  CHECK(standbyLed(0, true, false) && standbyLed(LED_HEARTBEAT_ON_MS - 1, true, false));
  CHECK(!standbyLed(LED_HEARTBEAT_ON_MS, true, false) && standbyLed(LED_HEARTBEAT_MS + 1, true, false));
  // Battery low: two flashes with a gap, then dark until the next period.
  CHECK(standbyLed(0, false, true) && !standbyLed(LED_LOW_FLASH_MS, false, true));
  CHECK(standbyLed(2 * LED_LOW_FLASH_MS, false, true) && !standbyLed(3 * LED_LOW_FLASH_MS, false, true));
  CHECK(!standbyLed(LED_LOW_BATTERY_MS / 2, false, true) && standbyLed(LED_LOW_BATTERY_MS, false, true));
  int on = 0;
  for (uint32_t ms = 0; ms < LED_LOW_BATTERY_MS; ms += 10) on += standbyLed(ms, false, true) ? 1 : 0;
  CHECK(on == 2 * static_cast<int>(LED_LOW_FLASH_MS / 10));  // dark almost all the time: the battery is low
}

void runBatteryTests() {
  testLedPatterns();
  testPercent();
  testWarnings();
  testEmpty();
  testCharging();
  testStandbyMode();
}
