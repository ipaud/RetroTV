#include "diagnostics/Diagnostics.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_flash.h>

#include "app_types.h"
#include "board_config.h"

namespace {

constexpr uint8_t I2C_FIRST_ADDR = 0x08;  // 0x00-0x07 and 0x78-0x7F are reserved
constexpr uint8_t I2C_LAST_ADDR = 0x77;
constexpr int BATTERY_SAMPLES = 8;
constexpr uint8_t LED_LEVEL = 24;  // WS2812 at full power is blinding

}  // namespace

uint32_t Diagnostics::readBatteryMv() {
  uint32_t sum = 0;
  for (int i = 0; i < BATTERY_SAMPLES; ++i) sum += analogReadMilliVolts(PIN_BAT_ADC);
  return sum / BATTERY_SAMPLES * BAT_ADC_DIVIDER;
}

bool DiagReport::hasI2c(uint8_t addr) const {
  for (int i = 0; i < i2cCount; ++i) {
    if (i2c[i] == addr) return true;
  }
  return false;
}

bool DiagReport::touchFound() const { return hasI2c(I2C_ADDR_TOUCH); }
bool DiagReport::codecFound() const { return hasI2c(I2C_ADDR_ES8311); }
bool DiagReport::ok() const { return lcdOk && psramTotal > 0 && codecFound(); }

namespace Diagnostics {

DiagReport collect(bool lcdOk) {
  DiagReport r;
  r.lcdOk = lcdOk;
  r.psramTotal = ESP.getPsramSize();
  r.psramFree = ESP.getFreePsram();
  r.heapTotal = ESP.getHeapSize();
  r.heapFree = ESP.getFreeHeap();
  r.heapMin = ESP.getMinFreeHeap();
  r.chipModel = ESP.getChipModel();
  r.cpuMhz = ESP.getCpuFreqMHz();

  uint32_t flash = 0;
  if (esp_flash_get_physical_size(esp_flash_default_chip, &flash) == ESP_OK) r.flashPhysical = flash;

  for (uint8_t addr = I2C_FIRST_ADDR; addr <= I2C_LAST_ADDR && r.i2cCount < DIAG_MAX_I2C; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) r.i2c[r.i2cCount++] = addr;
  }

  r.batteryMv = readBatteryMv();
  return r;
}

void log(const DiagReport& r) {
  PLOG("BOOT", "chip %s %u MHz, flash %u KB (physical)", r.chipModel, r.cpuMhz,
       r.flashPhysical / 1024);
  PLOG("BOOT", "psram %u/%u KB free, heap %u/%u KB free (min %u KB)", r.psramFree / 1024,
       r.psramTotal / 1024, r.heapFree / 1024, r.heapTotal / 1024, r.heapMin / 1024);

  char list[DIAG_MAX_I2C * 5 + 1] = "none";
  for (int i = 0; i < r.i2cCount; ++i) snprintf(list + i * 5, 6, "0x%02X ", r.i2c[i]);
  PLOG("BOOT", "i2c devices: %s", list);

  PLOG("TOUCH", "controller at 0x%02X: %s", I2C_ADDR_TOUCH, r.touchFound() ? "FOUND" : "NOT FOUND");
  PLOG("AUDIO", "ES8311 at 0x%02X: %s", I2C_ADDR_ES8311, r.codecFound() ? "FOUND" : "NOT FOUND");
  PLOG("BOOT", "battery %u mV", r.batteryMv);
}

void showStatusLed(bool ok) {
  neopixelWrite(PIN_RGB_LED, ok ? 0 : LED_LEVEL, ok ? LED_LEVEL : 0, 0);
}

void ledOff() { neopixelWrite(PIN_RGB_LED, 0, 0, 0); }

}  // namespace Diagnostics
