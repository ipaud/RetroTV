#pragma once

#include <Preferences.h>
#include <stdint.h>

// User settings in NVS (namespace "pautv"). Setters only touch RAM; loop() writes a value
// once it has been stable for SETTINGS_SAVE_DELAY_MS, so zapping does not wear the flash.
class SettingsStore {
 public:
  void begin();
  void loop(uint32_t nowMs);
  void flush();  // write now (before a restart)

  uint8_t volume() const { return current_.volume; }
  uint16_t lastChannel() const { return current_.lastChannel; }
  uint8_t brightness() const { return current_.brightness; }

  void setVolume(uint8_t v);
  void setLastChannel(uint16_t number);
  void setBrightness(uint8_t percent);

 private:
  struct Values {
    uint8_t volume;
    uint16_t lastChannel;
    uint8_t brightness;
  };
  void touch();

  Preferences prefs_;
  bool open_ = false;
  Values current_{};
  Values saved_{};
  bool dirty_ = false;
  uint32_t changedMs_ = 0;
};
