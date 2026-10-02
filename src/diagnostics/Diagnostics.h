#pragma once

#include <stdint.h>

constexpr int DIAG_MAX_I2C = 16;

// Snapshot of what the hardware reports at runtime. Nothing here is hard-coded.
struct DiagReport {
  bool lcdOk = false;
  uint32_t psramTotal = 0;
  uint32_t psramFree = 0;
  uint32_t flashPhysical = 0;  // from the JEDEC id, not from the image header
  uint32_t heapTotal = 0;      // internal RAM only
  uint32_t heapFree = 0;
  uint32_t heapMin = 0;
  const char* chipModel = "";
  uint32_t cpuMhz = 0;
  uint8_t i2c[DIAG_MAX_I2C] = {};
  uint8_t i2cCount = 0;
  uint32_t batteryMv = 0;  // with no battery this is the charger output, not meaningful

  bool hasI2c(uint8_t addr) const;
  bool touchFound() const;
  bool codecFound() const;
  // What every unit must have. Touch is optional (FNK0104A has none).
  bool ok() const;
};

namespace Diagnostics {
// The battery through its divider (GPIO9), averaged over a few samples. Loop task.
uint32_t readBatteryMv();

// Probes memory, flash, battery and scans the shared I2C bus. Call from loopTask only
// (it owns the I2C bus) and after Wire.begin().
DiagReport collect(bool lcdOk);
void log(const DiagReport& r);
// On-board WS2812: dim green = all good, dim red = something missing.
void showStatusLed(bool ok);
void ledOff();
}  // namespace Diagnostics
