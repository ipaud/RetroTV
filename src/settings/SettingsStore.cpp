#include "settings/SettingsStore.h"

#include <Arduino.h>

#include "app_types.h"
#include "config.h"

namespace {

constexpr const char* NVS_NAMESPACE = "pautv";
constexpr const char* KEY_VOLUME = "volume";
constexpr const char* KEY_LAST_CHANNEL = "last_ch";
constexpr const char* KEY_BRIGHTNESS = "bright";
#if PAUTV_MIC_ENABLED
constexpr const char* KEY_MIC_ON = "mic_on";
constexpr const char* KEY_CLAP_ON = "clap_on";
constexpr const char* KEY_CLAP_SENS = "clap_sens";
constexpr const char* KEY_VOICE_STANDBY = "v_standby";
constexpr const char* KEY_LISTEN_LED = "listen_led";
constexpr const char* KEY_HOLA_ESP = "ww_hola";    // read by the standby app too (standby/src/main.cpp)
constexpr const char* KEY_HEY_RETRO = "ww_retro";
constexpr const char* KEY_MSG_CHANNEL = "msg_ch";
#endif

}  // namespace

void SettingsStore::begin() {
  open_ = prefs_.begin(NVS_NAMESPACE, false);
  if (!open_) PLOG("SETTINGS", "NVS unavailable, using defaults (nothing will be saved)");

  current_.volume = min<uint8_t>(open_ ? prefs_.getUChar(KEY_VOLUME, VOLUME_DEFAULT) : VOLUME_DEFAULT, 100);
  current_.lastChannel = open_ ? prefs_.getUShort(KEY_LAST_CHANNEL, CHANNEL_DEFAULT) : CHANNEL_DEFAULT;
  current_.brightness = constrain(
      open_ ? prefs_.getUChar(KEY_BRIGHTNESS, BRIGHTNESS_DEFAULT_PCT) : BRIGHTNESS_DEFAULT_PCT,
      BRIGHTNESS_MIN_PCT, 100);
#if PAUTV_MIC_ENABLED
  current_.micOn = open_ ? prefs_.getBool(KEY_MIC_ON, true) : true;
  current_.clapOn = open_ ? prefs_.getBool(KEY_CLAP_ON, PAUTV_CLAP_ENABLED != 0) : PAUTV_CLAP_ENABLED != 0;
  current_.clapSensitivity =
      min<uint8_t>(open_ ? prefs_.getUChar(KEY_CLAP_SENS, CLAP_SENSITIVITY_DEFAULT) : CLAP_SENSITIVITY_DEFAULT, 100);
  current_.voiceStandby = open_ ? prefs_.getBool(KEY_VOICE_STANDBY, false) : false;  // default: deep sleep, as before
  current_.listenLed = open_ ? prefs_.getBool(KEY_LISTEN_LED, true) : true;
  current_.holaEsp = open_ ? prefs_.getBool(KEY_HOLA_ESP, true) : true;
  current_.heyRetro = open_ ? prefs_.getBool(KEY_HEY_RETRO, true) : true;
  PLOG("SETTINGS", "voice: mic=%u claps=%u sensitivity=%u standby=%s led=%u", current_.micOn, current_.clapOn,
       current_.clapSensitivity, current_.voiceStandby ? "voice" : "deep sleep", current_.listenLed);
#endif
  saved_ = current_;
  PLOG("SETTINGS", "loaded volume=%u last_ch=%u bright=%u", current_.volume, current_.lastChannel,
       current_.brightness);
}

#if PAUTV_MIC_ENABLED
bool SettingsStore::messagesChannelAdded() const {
  return open_ && const_cast<Preferences&>(prefs_).getBool(KEY_MSG_CHANNEL, false);
}

void SettingsStore::setMessagesChannelAdded() {
  if (open_) prefs_.putBool(KEY_MSG_CHANNEL, true);
}

void SettingsStore::setMicOn(bool on) {
  if (on == current_.micOn) return;
  current_.micOn = on;
  touch();
}

void SettingsStore::setClapOn(bool on) {
  if (on == current_.clapOn) return;
  current_.clapOn = on;
  touch();
}

void SettingsStore::setVoiceStandby(bool on) {
  if (on == current_.voiceStandby) return;
  current_.voiceStandby = on;
  touch();
}

void SettingsStore::setListenLed(bool on) {
  if (on == current_.listenLed) return;
  current_.listenLed = on;
  touch();
}

void SettingsStore::setHolaEsp(bool on) {
  if (on == current_.holaEsp) return;
  current_.holaEsp = on;
  touch();
}

void SettingsStore::setHeyRetro(bool on) {
  if (on == current_.heyRetro) return;
  current_.heyRetro = on;
  touch();
}

void SettingsStore::setClapSensitivity(uint8_t s) {
  s = min<uint8_t>(s, 100);
  if (s == current_.clapSensitivity) return;
  current_.clapSensitivity = s;
  touch();
}
#endif

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
#if PAUTV_MIC_ENABLED
  const bool voice = current_.micOn != saved_.micOn || current_.clapOn != saved_.clapOn ||
                     current_.clapSensitivity != saved_.clapSensitivity ||
                     current_.voiceStandby != saved_.voiceStandby || current_.listenLed != saved_.listenLed ||
                     current_.holaEsp != saved_.holaEsp || current_.heyRetro != saved_.heyRetro;
  if (voice) {
    prefs_.putBool(KEY_MIC_ON, current_.micOn);
    prefs_.putBool(KEY_CLAP_ON, current_.clapOn);
    prefs_.putUChar(KEY_CLAP_SENS, current_.clapSensitivity);
    prefs_.putBool(KEY_VOICE_STANDBY, current_.voiceStandby);
    prefs_.putBool(KEY_LISTEN_LED, current_.listenLed);
    prefs_.putBool(KEY_HOLA_ESP, current_.holaEsp);
    prefs_.putBool(KEY_HEY_RETRO, current_.heyRetro);
    saved_.micOn = current_.micOn;
    saved_.clapOn = current_.clapOn;
    saved_.clapSensitivity = current_.clapSensitivity;
    saved_.voiceStandby = current_.voiceStandby;
    saved_.listenLed = current_.listenLed;
    saved_.holaEsp = current_.holaEsp;
    saved_.heyRetro = current_.heyRetro;
  }
#endif
  if (!volume && !channel && !brightness) return;  // changed and changed back

  // Only keys whose value really changed are written.
  if (volume) prefs_.putUChar(KEY_VOLUME, current_.volume);
  if (channel) prefs_.putUShort(KEY_LAST_CHANNEL, current_.lastChannel);
  if (brightness) prefs_.putUChar(KEY_BRIGHTNESS, current_.brightness);
  saved_ = current_;
  PLOG("SETTINGS", "saved volume=%u last_ch=%u bright=%u", current_.volume, current_.lastChannel,
       current_.brightness);
}
