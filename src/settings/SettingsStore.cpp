#include "settings/SettingsStore.h"

#include <Arduino.h>

#include "app_types.h"
#include "config.h"

namespace {

constexpr const char* NVS_NAMESPACE = "pautv";
constexpr const char* KEY_VOLUME = "volume";
constexpr const char* KEY_LAST_CHANNEL = "last_ch";
constexpr const char* KEY_BRIGHTNESS = "bright";

}  // namespace

void SettingsStore::begin() {
  open_ = prefs_.begin(NVS_NAMESPACE, false);
  if (!open_) PLOG("SETTINGS", "NVS unavailable, using defaults (nothing will be saved)");

  current_.volume = min<uint8_t>(open_ ? prefs_.getUChar(KEY_VOLUME, VOLUME_DEFAULT) : VOLUME_DEFAULT, 100);
  current_.lastChannel = open_ ? prefs_.getUShort(KEY_LAST_CHANNEL, CHANNEL_DEFAULT) : CHANNEL_DEFAULT;
  current_.brightness = constrain(
      open_ ? prefs_.getUChar(KEY_BRIGHTNESS, BRIGHTNESS_DEFAULT_PCT) : BRIGHTNESS_DEFAULT_PCT,
      BRIGHTNESS_MIN_PCT, 100);
  saved_ = current_;
  PLOG("SETTINGS", "loaded volume=%u last_ch=%u bright=%u", current_.volume, current_.lastChannel,
       current_.brightness);
}

void SettingsStore::touch() {
  dirty_ = true;
  changedMs_ = millis();
}

void SettingsStore::setVolume(uint8_t v) {
  v = min<uint8_t>(v, 100);
  if (v == current_.volume) return;
  current_.volume = v;
  touch();
}

void SettingsStore::setLastChannel(uint16_t number) {
  if (number == current_.lastChannel) return;
  current_.lastChannel = number;
  touch();
}

void SettingsStore::setBrightness(uint8_t percent) {
  percent = constrain(percent, BRIGHTNESS_MIN_PCT, 100);
  if (percent == current_.brightness) return;
  current_.brightness = percent;
  touch();
}

void SettingsStore::loop(uint32_t nowMs) {
  if (dirty_ && nowMs - changedMs_ >= SETTINGS_SAVE_DELAY_MS) flush();
}

void SettingsStore::flush() {
  dirty_ = false;
  if (!open_) return;
  const bool volume = current_.volume != saved_.volume;
  const bool channel = current_.lastChannel != saved_.lastChannel;
  const bool brightness = current_.brightness != saved_.brightness;
  if (!volume && !channel && !brightness) return;  // changed and changed back

  // Only keys whose value really changed are written.
  if (volume) prefs_.putUChar(KEY_VOLUME, current_.volume);
  if (channel) prefs_.putUShort(KEY_LAST_CHANNEL, current_.lastChannel);
  if (brightness) prefs_.putUChar(KEY_BRIGHTNESS, current_.brightness);
  saved_ = current_;
  PLOG("SETTINGS", "saved volume=%u last_ch=%u bright=%u", current_.volume, current_.lastChannel,
       current_.brightness);
}
