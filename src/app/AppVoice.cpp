// RETROTV Voice in the App (docs/VOICE.md): settings, claps, the microphone screen, MIC TEST.
// Every function is empty in the normal build (PAUTV_MIC_ENABLED = 0): the TV behaves as before.
// Claps: three switch the TV off into STANDBY VOZ (and, there, on again); two do nothing. Builds
// without clap wake keep the old meaning: two = InputEvent::Mute, three = InputEvent::ChNext.

#include "app/App.h"

#include <algorithm>

#include "board_config.h"
#include "voice/VoiceStandby.h"

#if PAUTV_WAKEWORD_ENABLED
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <string.h>
#endif

#if PAUTV_MIC_ENABLED

namespace {

// Tenths of dB -> "-24.3"; never "-0.0".
void db10Text(int16_t db10, char* out, size_t len) {
  const int a = db10 < 0 ? -db10 : db10;
  snprintf(out, len, "%s%d.%d", db10 < 0 && a != 0 ? "-" : "", a / 10, a % 10);
}

#if PAUTV_WAKEWORD_ENABLED
// docs/WAKEWORD.md: STANDBY VOZ with «Hola ESP» runs in the standby app in app1 (ESP-IDF 5 + ESP-SR),
// which makes app0 the boot partition again as it starts. Returns only if that app is not there (or
// the switch failed): then this firmware's own STANDBY VOZ listens for claps as ever.
void handOverToStandbyApp() {
  const esp_partition_t* app1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
  esp_app_desc_t desc = {};
  if (app1 == nullptr || esp_ota_get_partition_description(app1, &desc) != ESP_OK ||
      strncmp(desc.project_name, "retrotv_standby", sizeof(desc.project_name)) != 0) {
    PLOG("STANDBY", "no standby app in app1: claps only");
    return;
  }
  Preferences prefs;  // the standby app cannot know which case this is
  if (prefs.begin("pautv", false)) {
    prefs.putBool("ww_keys", PAUTV_HAS_KEYS != 0);
    prefs.end();
  }
  const esp_err_t err = esp_ota_set_boot_partition(app1);
  if (err != ESP_OK) {
    PLOG("STANDBY", "cannot boot the standby app (%s): claps only", esp_err_to_name(err));
    return;
  }
  PLOG("STANDBY", "handing over to the standby app %s («Hola ESP» + claps)", desc.version);
  Serial.flush();
  ESP.restart();
}
#endif

}  // namespace

void App::startVoice() {
  if (!mic_.begin(audio_)) {
    PLOG("MIC", "not available: the TV works as before, without it");
    return;
  }
  applyVoiceSettings();
}

void App::applyVoiceSettings() {
  if (!mic_.running()) return;
  mic_.setPaused(!settings_.micOn());
  mic_.setClapEnabled(PAUTV_CLAP_ENABLED && settings_.micOn() && settings_.clapOn());
  mic_.setClapSensitivity(settings_.clapSensitivity());
}

// Only while a programme is on (the TV "switched on"): in the menus and diagnostics claps do
// nothing. A sequence that finished while the TV was busy elsewhere is dropped, not queued.
void App::pollClaps() {
  if (mic_.dull() != dullSeen_) {  // for tuning CLAP_HF_MIN_SHARE: how bright the dropped ones were
    dullSeen_ = mic_.dull();
    PLOG("CLAP", "too dull (bright %u%%)", static_cast<unsigned>(mic_.snapshot().clapHfPct));
  }
  if (mic_.fromTv() != fromTvSeen_) {  // for tuning the programme filter: what was dropped, and why
    fromTvSeen_ = mic_.fromTv();
    const AudioCapture::Snapshot m = mic_.snapshot();
    char peak[8], floor[8], rise[8];
    db10Text(m.tvPeakDb10, peak, sizeof(peak));
    db10Text(m.tvFloorDb10, floor, sizeof(floor));
    db10Text(m.clapTvRiseDb10, rise, sizeof(rise));
    PLOG("CLAP", "taken for the TV's own sound: peak %s dB, floor %s dB, the programme rose %s dB", peak, floor, rise);
  }
  const uint8_t claps = mic_.takeSequence();
  if (claps == 0) return;
  const AudioCapture::Snapshot m = mic_.snapshot();
  char peak[8];
  char floor[8];
  db10Text(m.clapPeakDb10, peak, sizeof(peak));
  db10Text(m.floorDb10, floor, sizeof(floor));
  const bool act = state_ == AppState::Playing && settings_.clapOn();
  snprintf(lastClap_, sizeof(lastClap_), "PALMADAS %u  %s DB", claps, peak);
  char tvRise[8];
  db10Text(m.clapTvRiseDb10, tvRise, sizeof(tvRise));
  PLOG("CLAP", "%s (gap %lu ms, peak %s dB, floor %s dB, tv rise %s dB, bright %u%%) in %s%s",
       claps == 1 ? "single" : (claps == 2 ? "double" : "triple"), static_cast<unsigned long>(m.clapGapMs),
       peak, floor, tvRise, static_cast<unsigned>(m.clapHfPct), appStateName(state_),
       act && (PAUTV_CLAP_WAKE_ENABLED ? claps == CLAP_POWER_CLAPS : claps > 1) ? "" : ": ignored");
  if (!act) return;
#if PAUTV_CLAP_WAKE_ENABLED
  // Three claps: off into STANDBY VOZ whatever APAGADO says, so three claps switch it on again. Two do
  // nothing: that is what household noises pass for.
  if (claps == CLAP_POWER_CLAPS) {
    PLOG("POWER", "standby: %u claps", static_cast<unsigned>(claps));
    blinkLed();
    powerDown();
    voiceStandby();  // does not return
  }
  return;
#endif
  if (claps == 2) {  // builds without clap wake: 2 = mute, 3 = next channel, as before
    onInput(InputEvent::Mute);  // the same path as the MUTE key
    OsdState o;
    o.visible = true;
    snprintf(o.title, sizeof(o.title), "%s", muted_ ? "SILENCIO" : "SONIDO");
    snprintf(o.subtitle, sizeof(o.subtitle), "CLAP! CLAP!");
    publishOsd(o, OSD_VOLUME_MS);
  } else if (claps == 3) {
    onInput(InputEvent::ChNext);  // the zap's static, flash and channel OSD say the rest
  }
}

void App::publishVoiceSettings() {
  settingsPublishedMs_ = millis();
  UiState s(Screen::Settings, "VOZ");
  const bool claps = settings_.micOn() && settings_.clapOn();
  s.addLine("MICROFONO  %s", settings_.micOn() ? "ON" : "OFF");
  s.addLine(settings_.micOn() ? Tone::Normal : Tone::Dim, "PALMADAS   %s", settings_.clapOn() ? "ON" : "OFF");
  s.addLine(claps ? Tone::Normal : Tone::Dim, "SENSIBLE   %u", settings_.clapSensitivity());
  const bool voiceOff = settings_.voiceStandby() || !PAUTV_HAS_KEYS;  // without keys, the only way off
  s.addLine(claps && PAUTV_CLAP_WAKE_ENABLED && PAUTV_HAS_KEYS ? Tone::Normal : Tone::Dim, "APAGADO    %s",
            voiceOff ? "STANDBY VOZ" : "AHORRO MAX");
  s.addLine(claps && voiceOff ? Tone::Normal : Tone::Dim, "LED ESCUCHA %s",
            settings_.listenLed() ? "ON" : "OFF");
  s.addLine(settings_.micOn() && PAUTV_RECORDER_ENABLED ? Tone::Normal : Tone::Dim, "GRABAR MENSAJE");
  s.selected = voiceIndex_;
  ui_.publish(s);
}

// CH-/CH+ move, VOL-/VOL+ change, MENU back to AJUSTES. Saved like the other settings.
void App::onVoiceSettingsInput(InputEvent e) {
  constexpr uint8_t count = static_cast<uint8_t>(VoiceItem::Count);
  const int step = (e == InputEvent::VolUp || e == InputEvent::ToggleOsd) ? 1 : (e == InputEvent::VolDown ? -1 : 0);
  switch (e) {
    case InputEvent::ChNext:
      voiceIndex_ = (voiceIndex_ + 1) % count;
      break;
    case InputEvent::ChPrev:
      voiceIndex_ = (voiceIndex_ + count - 1) % count;
      break;
    case InputEvent::Menu:
      voiceMenu_ = false;
      publishSettings();
      return;
    default:
      if (step == 0) break;
      switch (static_cast<VoiceItem>(voiceIndex_)) {
        case VoiceItem::Mic:
          settings_.setMicOn(!settings_.micOn());
          break;
        case VoiceItem::Claps:
          settings_.setClapOn(!settings_.clapOn());
          break;
        case VoiceItem::Sensitivity: {
          const int v = settings_.clapSensitivity() + step * CLAP_SENSITIVITY_STEP;
          settings_.setClapSensitivity(static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v)));
          break;
        }
        case VoiceItem::PowerMode:
          if (PAUTV_HAS_KEYS) settings_.setVoiceStandby(!settings_.voiceStandby());  // without keys: STANDBY VOZ only
          break;
        case VoiceItem::ListenLed:
          settings_.setListenLed(!settings_.listenLed());
          break;
        case VoiceItem::Record:
          if (step > 0) {
            startRecorder();
            return;
          }
          break;
        case VoiceItem::Count:
          break;
      }
      applyVoiceSettings();
      break;
  }
  publishVoiceSettings();
}

void App::publishMic() {
  micPublishedMs_ = millis();
  const AudioCapture::Snapshot m = mic_.snapshot();
  char rms[8];
  char peak[8];
  db10Text(m.rmsDb10, rms, sizeof(rms));
  db10Text(m.holdDb10, peak, sizeof(peak));

  UiState s(Screen::Microphone, "MICROFONO");
  s.meterPct = micMeterPct(m.rmsDb10);
  s.meterPeakPct = micMeterPct(m.holdDb10);
  s.addLine("RMS   %s DB", rms);
  s.addLine("PEAK  %s DB", peak);
  s.addLine(m.clipped > 0 ? Tone::Bad : Tone::Dim, "CLIP %lu  ERR %lu", static_cast<unsigned long>(m.clipped),
            static_cast<unsigned long>(m.overruns));
  if (lastClap_[0] != '\0') s.addLine(Tone::Good, "%s", lastClap_);
  ui_.publish(s);
}

bool App::onMicScreenInput(InputEvent e) {
  if (!micScreen_) {
    if (e != InputEvent::ChPrev || !diagFromSettings_ || !mic_.running()) return false;
    micScreen_ = true;
    publishMic();
    return true;
  }
  if (e == InputEvent::Menu) {
    micScreen_ = false;
    publishDiagnostics();
  }
  return true;  // the rest does nothing here: a test tone would only measure itself
}

// The web remote's VOZ section: the same settings as AJUSTES > VOZ on the TV, saved the same way.
void App::applyVoiceChange(const VoiceChange& c) {
  if (c.mic >= 0) settings_.setMicOn(c.mic == 1);
  if (c.claps >= 0) settings_.setClapOn(c.claps == 1);
  if (c.sensitivity >= 0) {
    const int s = (c.sensitivity + CLAP_SENSITIVITY_STEP / 2) / CLAP_SENSITIVITY_STEP * CLAP_SENSITIVITY_STEP;
    settings_.setClapSensitivity(static_cast<uint8_t>(s > 100 ? 100 : s));
  }
  if (c.standbyVoice >= 0 && PAUTV_HAS_KEYS) settings_.setVoiceStandby(c.standbyVoice == 1);
  if (c.led >= 0) settings_.setListenLed(c.led == 1);
  applyVoiceSettings();
  PLOG("WEB", "voice settings: mic=%u claps=%u sensitivity=%u standby=%s led=%u", settings_.micOn(), settings_.clapOn(),
       settings_.clapSensitivity(), settings_.voiceStandby() ? "voice" : "deep", settings_.listenLed());
  if (state_ == AppState::Settings && voiceMenu_) publishVoiceSettings();  // the TV shows the same menu
}

size_t App::voiceConfigJson(char* out, size_t cap) {
  VoiceConfig c;
  c.available = mic_.running();
  c.mic = settings_.micOn();
  c.claps = settings_.clapOn();
  c.sensitivity = settings_.clapSensitivity();
  c.standbyVoice = settings_.voiceStandby() || !PAUTV_HAS_KEYS;
  c.standbyFixed = !PAUTV_HAS_KEYS;
  c.led = settings_.listenLed();
  return writeVoiceJson(c, out, cap);
}

bool App::voiceStandbyPossible() const {
  return PAUTV_CLAP_WAKE_ENABLED && mic_.running() && settings_.micOn() && settings_.clapOn();
}

// STANDBY VOZ (docs/VOICE.md). powerDown() already played the CRT switch-off and turned off the
// screen, backlight, Wi-Fi, speaker and LED. Here only the microphone keeps working, with the CPU
// slowed down. One clap flashes the LED; a second one (or any key) restarts the TV, the same boot
// as from deep sleep: intro, last channel, saved volume. A flat battery still ends in deep sleep.
void App::voiceStandby() {
  PLOG("STANDBY", "voice standby: listening for three claps (or a key); heap %u KB (largest %u KB), psram %u KB",
       static_cast<unsigned>(ESP.getFreeHeap() / 1024), static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
       static_cast<unsigned>(ESP.getFreePsram() / 1024));
  mic_.setPaused(false);
  mic_.setClapEnabled(true);
  mic_.setPlaybackLoud(false);  // the speaker is off: nothing of its own to hear
  mic_.setClapMinDb10(CLAP_STANDBY_MIN_DB10);  // only a clap near the TV switches it on
  mic_.takeClapIndex();         // whatever was heard before does not count
  mic_.takeSequence();
  const uint32_t t0 = millis();
  while (buttons_.anyKeyDown() && millis() - t0 < STANDBY_RELEASE_WAIT_MS) delay(10);  // the key that asked
  delay(BUTTON_DEBOUNCE_MS);
#if PAUTV_WAKEWORD_ENABLED
  handOverToStandbyApp();  // returns only when there is no standby app
#endif
  Serial.flush();
  setCpuFrequencyMhz(VOICE_STANDBY_CPU_MHZ);

  VoiceStandby standby;
  uint32_t ledOffAtMs = 0;
  uint32_t batteryMs = millis();
  uint32_t micBlocks = mic_.snapshot().blocks;  // a capture that stops delivering audio is logged
  for (;;) {
    const uint32_t now = millis();
    const uint8_t clap = mic_.takeClapIndex();
    const uint8_t sequence = mic_.takeSequence();
    StandbyAction a = standby.onClap(clap);
    if (clap != 0) {
      const AudioCapture::Snapshot m = mic_.snapshot();
      char peak[8];
      db10Text(m.clapPeakDb10, peak, sizeof(peak));
      PLOG("STANDBY", "clap %u: peak %s dB, bright %u%%", clap, peak, static_cast<unsigned>(m.clapHfPct));
    }
    if (sequence != 0) {
      const uint32_t quiet = mic_.sequenceQuietBeforeMs();
      PLOG("STANDBY", "sequence of %u (quiet before %ld ms)", sequence, quiet == UINT32_MAX ? -1L : static_cast<long>(quiet));
      standby.onSequence(sequence, quiet, mic_.bangs(), now);
    }
    const bool pending = standby.pending();
    if (standby.update(mic_.bangs(), now) == StandbyAction::Wake) {
      a = StandbyAction::Wake;
    } else if (pending && !standby.pending()) {
      PLOG("STANDBY", "a bang right after: not switching on");
    }
    bool wake = a == StandbyAction::Wake;
    if (a == StandbyAction::Blink) {
      PLOG("STANDBY", "clap: I heard you");
      if (settings_.listenLed()) {
        digitalWrite(PIN_LED_FRONT, HIGH);
        ledOffAtMs = (now + LISTEN_LED_FLASH_MS) | 1;
      }
    }
    if (ledOffAtMs != 0 && static_cast<int32_t>(now - ledOffAtMs) >= 0) {
      digitalWrite(PIN_LED_FRONT, LOW);
      ledOffAtMs = 0;
    }
    if (buttons_.anyKeyDown()) {
      PLOG("STANDBY", "a key");
      wake = true;
    }
#if PAUTV_DEBUG_STATS
    if (Serial.available() > 0 && Serial.read() == 'W') {
      PLOG("STANDBY", "serial W");
      wake = true;
    }
#endif
    if (now - batteryMs >= BATTERY_READ_MS) {
      batteryMs = now;
      // 2026-10-03: once a clap sequence was reported 22 s late and its third clap lost, as if no audio
      // arrived meanwhile. Say so if it happens again (blocks should advance ~86 per second).
      const AudioCapture::Snapshot m = mic_.snapshot();
      if (m.blocks == micBlocks) {
        PLOG("STANDBY", "microphone delivered no audio for %lu ms (blocks %lu, overruns %lu)",
             static_cast<unsigned long>(BATTERY_READ_MS), static_cast<unsigned long>(m.blocks),
             static_cast<unsigned long>(m.overruns));
      }
      micBlocks = m.blocks;
      battery_.update(Diagnostics::readBatteryMv());
      if (battery_.empty()) {
        PLOG("STANDBY", "battery flat (%lu mV): deep sleep instead", static_cast<unsigned long>(battery_.millivolts()));
        standby.cancelWake();
        setCpuFrequencyMhz(240);
        deepSleep();
      }
    }
    if (wake) {
      PLOG("STANDBY", "%s: switching on", a == StandbyAction::Wake ? "three claps" : "woken");
      Serial.flush();
      ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(VOICE_STANDBY_POLL_MS));
  }
}

// ---------------------------------------------------------------------------------------------
// GRABADORA (docs/VOICE.md). Only from a deliberate action (AJUSTES > VOZ > GRABAR MENSAJE, serial
// R; CH- + VOL- once the keys are fitted) and only with REC on screen: 3-2-1, then the capture task
// fills recBuf_ at 16 kHz until a key, or 15 s. Then one WAV goes to /retrotv/voice/messages/
// as msg_NNNN.wav (written as .tmp and renamed). Nothing is recorded at any other time.

#if PAUTV_RECORDER_ENABLED
namespace {
constexpr size_t REC_MAX_SAMPLES = REC_SAMPLE_RATE * REC_MAX_MS / 1000;
constexpr size_t REC_HEADER_SAMPLES = WAV_HEADER_BYTES / sizeof(int16_t);
constexpr size_t REC_LIST_MAX = MessagePlayer::MAX_MESSAGES;  // message files looked at
constexpr size_t REC_NAME_LEN = 16;
char* recNames = nullptr;  // PSRAM, REC_LIST_MAX x REC_NAME_LEN, kept (internal RAM is scarce)
char* msgPaths = nullptr;  // PSRAM, MAX_MESSAGES x PATH_LEN
bool isMessageName(const char* name) { return messageIdOf(name) > 0; }

// File names in the messages folder, into recNames (allocated on first use). How many.
size_t listMessages(const StorageManager& storage) {
  if (recNames == nullptr) recNames = static_cast<char*>(heap_caps_malloc(REC_LIST_MAX * REC_NAME_LEN, MALLOC_CAP_SPIRAM));
  if (recNames == nullptr || !storage.mounted()) return 0;
  return storage.listNames(REC_DIR, isMessageName, recNames, REC_NAME_LEN, REC_LIST_MAX);
}
}  // namespace

void App::startRecorder() {
  if (state_ == AppState::Recorder) return;
  recError_ = "";
  if (!mic_.running() || !settings_.micOn()) {
    recError_ = "MICROFONO OFF";
  } else if (!storage_.mounted()) {
    recError_ = "NO SD";
  } else if (recBuf_ == nullptr) {
    recBuf_ = static_cast<int16_t*>(
        heap_caps_malloc((REC_HEADER_SAMPLES + REC_MAX_SAMPLES) * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (recBuf_ == nullptr) recError_ = "SIN MEMORIA";
  }
  stopProgramme();  // nothing of the TV's own in the message
  enter(AppState::Recorder);
  const uint32_t now = millis();
  recFlow_ = RecorderFlow();
  if (recError_[0] != '\0') {
    recFlow_.saved(now, false);
  } else {
    recFlow_.start(now);
  }
  PLOG("REC", "recorder: %s", recError_[0] != '\0' ? recError_ : "3, 2, 1");
  recShownPhase_ = RecPhase::Idle;
  publishRecorder(now);
}

void App::updateRecorder(uint32_t nowMs) {
  const RecPhase before = recFlow_.phase();
  RecPhase p = recFlow_.update(nowMs);
  if (before == RecPhase::Countdown && p == RecPhase::Recording) {
    mic_.startRecording(recBuf_ + REC_HEADER_SAMPLES, REC_MAX_SAMPLES);
    PLOG("REC", "recording");
  }
  if (p == RecPhase::Recording && mic_.recordingFull()) {
    recFlow_.stop(nowMs);
    p = recFlow_.phase();
  }
  if (p == RecPhase::Saving) {
    publishRecorder(nowMs);  // GUARDANDO while the card is written (~1 s for 15 s)
    const size_t samples = mic_.stopRecording();
    const char* error = "";
    const bool ok = saveMessage(samples, recId_, error);
    recError_ = error;
    recFlow_.saved(millis(), ok);
    p = recFlow_.phase();
  }
  if (p == RecPhase::Idle) {
    enter(AppState::Playing);
    return;
  }
  if (p != recShownPhase_ || nowMs - recPublishedMs_ >= REC_SCREEN_REFRESH_MS) publishRecorder(nowMs);
}

// Any key stops REC (and saves) or cancels the countdown. POWER still switches the TV off.
void App::onRecorderInput(InputEvent e) {
  if (e == InputEvent::None) return;
  recFlow_.stop(millis());
  updateRecorder(millis());
}

void App::publishRecorder(uint32_t nowMs) {
  recPublishedMs_ = nowMs;
  recShownPhase_ = recFlow_.phase();
  UiState s(Screen::Recorder, "GRABADORA");
  s.meterPct = 0xFF;
  switch (recFlow_.phase()) {
    case RecPhase::Countdown:
      s.addLine(Tone::Good, "%u", recFlow_.countdown(nowMs));
      s.addLine(Tone::Dim, "HABLA DESPUES DEL 1");
      break;
    case RecPhase::Recording: {
      const uint32_t ms = recFlow_.recordedMs(nowMs);
      s.addLine(Tone::Bad, "REC");
      s.addLine("%02lu:%02lu / 00:%02lu", static_cast<unsigned long>(ms / 60000), static_cast<unsigned long>(ms / 1000 % 60),
                static_cast<unsigned long>(REC_MAX_MS / 1000));
      s.meterPct = static_cast<uint8_t>(ms >= REC_MAX_MS ? 100 : ms * 100 / REC_MAX_MS);
      break;
    }
    case RecPhase::Saving:
      s.addLine("GUARDANDO");
      break;
    case RecPhase::Saved:
      s.addLine(Tone::Good, "MENSAJE GUARDADO");
      s.addLine(Tone::Good, "#%03d", recId_);
      break;
    case RecPhase::Failed:
      s.addLine(Tone::Bad, "%s", strcmp(recError_, "NO SD") == 0 ? "NO SD" : "ERROR AL GUARDAR");
      if (strcmp(recError_, "NO SD") != 0) s.addLine(Tone::Dim, "%s", recError_);
      break;
    case RecPhase::Idle:
      break;
  }
  ui_.publish(s);
}

bool App::saveMessage(size_t samples, int& id, const char*& error) {
  id = 0;
  if (samples == 0) {
    error = "NADA GRABADO";
    return false;
  }
  if (!storage_.mounted() || !storage_.makeDirs(REC_DIR)) {
    error = "NO SD";
    return false;
  }
  // No free-space check first: on a 64 GB FAT32 card it walks the whole FAT (~7 s). A full card
  // fails the write instead, and says so.
  const size_t bytes = WAV_HEADER_BYTES + samples * sizeof(int16_t);
  const uint32_t t0 = millis();
  int newest = 0;
  const size_t n = listMessages(storage_);
  for (size_t i = 0; i < n; ++i) {
    const int m = messageIdOf(recNames + i * REC_NAME_LEN);
    if (m > newest) newest = m;
  }
  if (newest >= 9999) {
    error = "MENSAJES LLENOS";
    return false;
  }
  id = newest + 1;
  const Normalized level = normalizeRecording(recBuf_ + REC_HEADER_SAMPLES, samples, REC_SAMPLE_RATE, REC_TARGET_DB, REC_MAX_GAIN_DB);
  wavHeader(reinterpret_cast<uint8_t*>(recBuf_), REC_SAMPLE_RATE, 1, 16, static_cast<uint32_t>(samples * sizeof(int16_t)));
  char name[16];
  char path[64];
  messageName(id, name, sizeof(name));
  snprintf(path, sizeof(path), "%s/%s", REC_DIR, name);
  if (!storage_.writeFileAtomic(path, reinterpret_cast<const char*>(recBuf_), bytes)) {
    error = "ESCRITURA";
    PLOG("REC", "could not write %s", path);
    return false;
  }
  PLOG("REC", "saved %s: %lu ms, %lu bytes in %lu ms; voice %.1f dBFS, gain %+.1f dB", path,
       static_cast<unsigned long>(samples * 1000 / REC_SAMPLE_RATE), static_cast<unsigned long>(bytes),
       static_cast<unsigned long>(millis() - t0), level.levelDb, level.gainDb);
  return true;
}

// Serial E: deletes the saved messages (only msg_NNNN.wav). Not while MENSAJES plays one of them.
void App::deleteMessages() {
  if (playMode_ == PlayMode::Messages) {
    PLOG("REC", "not deleting while MENSAJES is on: change channel first");
    return;
  }
  const size_t n = listMessages(storage_);
  size_t gone = 0;
  char path[64];
  for (size_t i = 0; i < n; ++i) {
    snprintf(path, sizeof(path), "%s/%s", REC_DIR, recNames + i * REC_NAME_LEN);
    if (storage_.removeFile(path)) ++gone;
  }
  PLOG("REC", "deleted %u of %u messages", static_cast<unsigned>(gone), static_cast<unsigned>(n));
}

// Serial Y: two seconds of a two-note chime, saved exactly as a recording would be. Tests the card,
// the naming and the messages channel without recording anyone.
void App::saveTestMessage() {
  if (recBuf_ == nullptr) {
    recBuf_ = static_cast<int16_t*>(
        heap_caps_malloc((REC_HEADER_SAMPLES + REC_MAX_SAMPLES) * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  }
  if (recBuf_ == nullptr) {
    PLOG("REC", "test message: out of memory");
    return;
  }
  const size_t samples = 2 * REC_SAMPLE_RATE;
  const size_t note = REC_SAMPLE_RATE / 4;  // ding-dong every 250 ms
  int16_t* pcm = recBuf_ + REC_HEADER_SAMPLES;
  for (size_t start = 0; start < samples; start += note) {
    // A recursive oscillator (sinf per sample takes seconds on the S3): y[n] = 2cos(w) y[n-1] - y[n-2].
    const float w = 6.2831853f * ((start / note) % 2 == 0 ? 880.0f : 660.0f) / REC_SAMPLE_RATE;
    const float k = 2.0f * cosf(w);
    float y1 = 0.0f;
    float y2 = -8000.0f * sinf(w);
    for (size_t i = start; i < start + note && i < samples; ++i) {
      const float y = k * y1 - y2;
      y2 = y1;
      y1 = y;
      pcm[i] = static_cast<int16_t>(y);
    }
  }
  int id = 0;
  const char* error = "";
  if (!saveMessage(samples, id, error)) PLOG("REC", "test message not saved: %s", error);
}
// MENSAJES: the messages in the folder, oldest first, one after the other, again and again.
void App::startMessages() {
  playMode_ = PlayMode::Messages;
  messageCount_ = 0;
  if (messageIds_ == nullptr) {
    messageIds_ = static_cast<int*>(heap_caps_malloc(MessagePlayer::MAX_MESSAGES * sizeof(int), MALLOC_CAP_SPIRAM));
  }
  if (msgPaths == nullptr) {
    msgPaths = static_cast<char*>(heap_caps_malloc(MessagePlayer::MAX_MESSAGES * MessagePlayer::PATH_LEN, MALLOC_CAP_SPIRAM));
  }
  const size_t n = messageIds_ != nullptr && msgPaths != nullptr ? listMessages(storage_) : 0;
  for (size_t i = 0; i < n && i < MessagePlayer::MAX_MESSAGES; ++i) {
    messageIds_[messageCount_++] = messageIdOf(recNames + i * REC_NAME_LEN);
  }
  std::sort(messageIds_, messageIds_ + messageCount_);
  for (size_t i = 0; i < messageCount_; ++i) {
    char name[16];
    messageName(messageIds_[i], name, sizeof(name));
    snprintf(msgPaths + i * MessagePlayer::PATH_LEN, MessagePlayer::PATH_LEN, "%s/%s", REC_DIR, name);
  }
  if (messageCount_ > 0 && !messages_.start(audio_, storage_, msgPaths, messageCount_)) {
    PLOG("MSG", "player did not start");
    messageCount_ = 0;
  }
  PLOG("MSG", "%u messages", static_cast<unsigned>(messageCount_));
  publishMessages(millis());
}

void App::stopMessages() { messages_.stop(); }

void App::updateMessages(uint32_t nowMs) {
  if (nowMs - messagesPublishedMs_ >= MSG_SCREEN_REFRESH_MS) publishMessages(nowMs);
}

void App::publishMessages(uint32_t nowMs) {
  messagesPublishedMs_ = nowMs;
  UiState s(Screen::Messages, "MENSAJES");
  s.meterPct = 0xFF;
  if (messageCount_ == 0) {
    s.addLine(Tone::Dim, "SIN MENSAJES");
    s.addLine(Tone::Dim, "AJUSTES > VOZ > GRABAR");
  } else {
    const MessagePlayer::Status st = messages_.status();
    if (st.index < 0) {
      s.addLine(Tone::Good, "...");
      s.addLine(Tone::Dim, "%u MENSAJE%s", static_cast<unsigned>(messageCount_), messageCount_ == 1 ? "" : "S");
    } else {
      s.addLine(Tone::Good, "%02d", messageIds_[st.index]);
      s.addLine("%02lu:%02lu / %02lu:%02lu", static_cast<unsigned long>(st.posMs / 60000),
                static_cast<unsigned long>(st.posMs / 1000 % 60), static_cast<unsigned long>(st.lenMs / 60000),
                static_cast<unsigned long>(st.lenMs / 1000 % 60));
      s.meterPct = static_cast<uint8_t>(st.lenMs ? (st.posMs >= st.lenMs ? 100 : st.posMs * 100 / st.lenMs) : 0);
      s.addLine(Tone::Dim, "%d DE %u", st.index + 1, static_cast<unsigned>(messageCount_));
    }
  }
  ui_.publish(s);
}
#else
void App::startRecorder() {}
void App::updateRecorder(uint32_t) { enter(AppState::Playing); }
void App::onRecorderInput(InputEvent) {}
void App::publishRecorder(uint32_t) {}
bool App::saveMessage(size_t, int&, const char*&) { return false; }
void App::saveTestMessage() {}
void App::deleteMessages() {}
void App::startMessages() {
  playMode_ = PlayMode::Messages;
  publishMessages(millis());
}
void App::stopMessages() {}
void App::updateMessages(uint32_t) {}
void App::publishMessages(uint32_t) {
  UiState s(Screen::Messages, "MENSAJES");
  s.meterPct = 0xFF;
  s.addLine(Tone::Dim, "SIN GRABADORA");
  ui_.publish(s);
}
#endif

void App::updateVoice(uint32_t nowMs) {
  mic_.setPlaybackLoud(state_ == AppState::Playing && !muted_ && settings_.volume() > 0);
  pollClaps();
  if (micScreen_ && state_ == AppState::Diagnostics && nowMs - micPublishedMs_ >= MIC_SCREEN_REFRESH_MS) publishMic();

  if (micTestUntilMs_ == 0 || nowMs - micTestLogMs_ < MIC_TEST_LOG_MS) return;
  micTestLogMs_ = nowMs;
  const AudioCapture::Snapshot m = mic_.snapshot();
  char rms[8];
  char peak[8];
  db10Text(m.rmsDb10, rms, sizeof(rms));
  db10Text(m.peakDb10, peak, sizeof(peak));
  char floor[8];
  char threshold[8];
  db10Text(m.floorDb10, floor, sizeof(floor));
  db10Text(m.thresholdDb10, threshold, sizeof(threshold));
  PLOG("MIC", "rms=%s peak=%s clip=%lu overrun=%lu blocks=%lu | clap floor=%s thr=%s claps=%lu long=%lu own=%lu tv=%lu dull=%lu bad=%lu",
       rms, peak, static_cast<unsigned long>(m.clipped), static_cast<unsigned long>(m.overruns),
       static_cast<unsigned long>(m.blocks), floor, threshold, static_cast<unsigned long>(m.claps),
       static_cast<unsigned long>(m.sustained), static_cast<unsigned long>(m.suppressed),
       static_cast<unsigned long>(m.fromTv), static_cast<unsigned long>(m.dull), static_cast<unsigned long>(m.badSequences));
  if (static_cast<int32_t>(nowMs - micTestUntilMs_) >= 0) {
    micTestUntilMs_ = 0;
    PLOG("MIC", "test done");
  }
}

void App::toggleMicCapture() {
  if (mic_.running()) mic_.setPaused(!mic_.paused());
}

void App::startMicTest(uint32_t nowMs) {
  if (!mic_.running()) {
    PLOG("MIC", "test: no microphone in this build or it did not start");
    return;
  }
  PLOG("MIC", "test: levels every %lu ms for %lu s", static_cast<unsigned long>(MIC_TEST_LOG_MS),
       static_cast<unsigned long>(MIC_TEST_MS / 1000));
  micTestUntilMs_ = nowMs + MIC_TEST_MS;
  if (micTestUntilMs_ == 0) micTestUntilMs_ = 1;  // 0 means off
  micTestLogMs_ = nowMs - MIC_TEST_LOG_MS;
}

#else  // PAUTV_MIC_ENABLED

void App::startVoice() {}
void App::updateVoice(uint32_t) {}
bool App::onMicScreenInput(InputEvent) { return false; }
void App::publishMic() {}
void App::startMicTest(uint32_t) { PLOG("MIC", "test: this build has no microphone (pio run -e voice)"); }
void App::toggleMicCapture() {}
void App::pollClaps() {}
void App::applyVoiceSettings() {}
void App::publishVoiceSettings() {}
void App::onVoiceSettingsInput(InputEvent) {}
bool App::voiceStandbyPossible() const { return false; }
void App::applyVoiceChange(const VoiceChange&) {}
size_t App::voiceConfigJson(char* out, size_t cap) { return writeVoiceJson(VoiceConfig{}, out, cap); }
void App::voiceStandby() { deepSleep(); }
void App::startRecorder() {}
void App::updateRecorder(uint32_t) { enter(AppState::Playing); }
void App::onRecorderInput(InputEvent) {}
void App::publishRecorder(uint32_t) {}
bool App::saveMessage(size_t, int&, const char*&) { return false; }
void App::saveTestMessage() { PLOG("REC", "this build has no recorder (pio run -e voice)"); }
void App::deleteMessages() {}
void App::startMessages() {  // a "messages" channel in a build without the voice layer
  playMode_ = PlayMode::Messages;
  publishMessages(millis());
}
void App::stopMessages() {}
void App::updateMessages(uint32_t) {}
void App::publishMessages(uint32_t) {
  UiState s(Screen::Messages, "MENSAJES");
  s.meterPct = 0xFF;
  s.addLine(Tone::Dim, "SIN VOZ");
  s.addLine(Tone::Dim, "FIRMWARE: PIO RUN -E VOICE");
  ui_.publish(s);
}

#endif  // PAUTV_MIC_ENABLED
