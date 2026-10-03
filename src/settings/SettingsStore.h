#pragma once

#include <Preferences.h>
#include <stdint.h>

#include "config.h"

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

#if PAUTV_MIC_ENABLED  // RETROTV Voice (AJUSTES > VOZ)
  bool micOn() const { return current_.micOn; }
  bool clapOn() const { return current_.clapOn; }
  uint8_t clapSensitivity() const { return current_.clapSensitivity; }
  void setMicOn(bool on);
  void setClapOn(bool on);
  void setClapSensitivity(uint8_t s);
  bool voiceStandby() const { return current_.voiceStandby; }  // APAGADO: STANDBY VOZ, else AHORRO MAXIMO
  bool listenLed() const { return current_.listenLed; }
  void setVoiceStandby(bool on);
  void setListenLed(bool on);
  // The standby app's wake words (docs/WAKEWORD.md); only the _ww builds show them.
  bool holaEsp() const { return current_.holaEsp; }
  bool heyRetro() const { return current_.heyRetro; }
  void setHolaEsp(bool on);
  void setHeyRetro(bool on);
  // The MENSAJES channel was put in channels.json once: if the user removes it, it stays removed.
  bool messagesChannelAdded() const;
  void setMessagesChannelAdded();  // written at once
#endif

 private:
  struct Values {
    uint8_t volume;
    uint16_t lastChannel;
    uint8_t brightness;
#if PAUTV_MIC_ENABLED
    bool micOn;
    bool clapOn;
    uint8_t clapSensitivity;
    bool voiceStandby;
    bool listenLed;
    bool holaEsp;
    bool heyRetro;
#endif
  };
  void touch();

  Preferences prefs_;
  bool open_ = false;
  Values current_{};
  Values saved_{};
  bool dirty_ = false;
  uint32_t changedMs_ = 0;
};
