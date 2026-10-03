#include "app/App.h"

#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <time.h>

#include <new>

#include "board_config.h"
#include "config.h"
#include "power/Standby.h"

void App::begin() {
  PLOG("BOOT", "RETROTV %s, reset reason %d", PAUTV_VERSION, static_cast<int>(esp_reset_reason()));
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) PLOG("POWER", "switched on from standby");
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {  // button-less, asleep with a flat battery
    const uint32_t mv = Diagnostics::readBatteryMv();
    if (!flatCheckSwitchOn(mv, FLAT_RESUME_MV)) {
      PLOG("POWER", "flat battery check: %lu mV, back to sleep", static_cast<unsigned long>(mv));
      sleepUntilWoken();  // the pins are still latched off: nothing lit up
    }
    PLOG("POWER", "battery back (%lu mV): switching on", static_cast<unsigned long>(mv));
  }
  // Standby latched these pins (backlight off, amplifier off, LED off): let go of them.
  gpio_deep_sleep_hold_dis();
  for (const int pin : {PIN_LCD_BL, PIN_AMP_EN, PIN_LED_FRONT}) gpio_hold_dis(static_cast<gpio_num_t>(pin));

  settings_.begin();
  display_.setBrightness(settings_.brightness());
  lcdOk_ = display_.begin(UIManager::render, &ui_);
  if (!lcdOk_) {
    // Nothing can be shown; the serial log is the only witness.
    fail("DISPLAY INIT FAILED", "CHECK LCD WIRING");
    return;
  }
  enter(AppState::Boot);

  // Shared bus: ES8311 codec, touch controller (B only), external I2C connector.
  if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ)) PLOG("BOOT", "i2c bus init failed");
  touch_.resetController();  // before the I2C scan in DIAGNOSTICS decides whether touch exists
  buttons_.begin();
  pinMode(PIN_LED_FRONT, OUTPUT);  // the pilot light: on while the TV runs
  digitalWrite(PIN_LED_FRONT, HIGH);

  if (!psramFound()) {
    fail("PSRAM NOT FOUND", "VIDEO NEEDS 8MB PSRAM");
    return;
  }

  storage_.begin();  // a missing card is not fatal: the TV falls back to the test card
  loadChannels();
  wifi_.begin(WifiManager::loadNetworks(storage_));  // runs in the background from here
  logoMask_ = loadWebLogos();
  web_.setChannels(channels_, logoMask_);
  if (!web_.begin()) PLOG("WEB", "remote server did not start");
  audio_.begin(settings_.volume());
  startVoice();  // nothing unless PAUTV_MIC_ENABLED; a missing microphone changes nothing else
  if (!player_.begin(display_, audio_)) fail("MEDIA INIT FAILED", "OUT OF MEMORY");
  if (!remote_.begin()) PLOG("REMOTE", "out of memory: remote channels show NO SIGNAL");
  if (!videoAhead_.begin()) fail("MEDIA INIT FAILED", "OUT OF MEMORY");
  if (!builder_.begin(storage_)) PLOG("CHANNEL", "no schedule task: the guides stay empty");
  guideRows_ = static_cast<GuideRow*>(heap_caps_calloc(MAX_CHANNELS, sizeof(GuideRow), MALLOC_CAP_SPIRAM));
  if (void* p = heap_caps_malloc(sizeof(RemoteGuide), MALLOC_CAP_SPIRAM)) remoteGuide_ = new (p) RemoteGuide();
  if (state_ == AppState::Boot && !startIntro()) {
    ui_.publish(UiState(Screen::Boot));  // no intro: the boot card, for BOOT_SCREEN_MS from now
    stateSinceMs_ = millis();
  }
}

// The power-on video (sdpath::INTRO_VIDEO, with its .aac when there is one) replaces the boot
// card, the diagnostics screen and the home card; the checks still run, off screen, when it
// ends. Any key skips it. Without the file, the start-up screens are as before.
bool App::startIntro() {
  if (!storage_.mounted() || !storage_.exists(sdpath::INTRO_VIDEO)) return false;
  if (!openMedia(sdpath::INTRO_VIDEO)) return false;
  if (!startLocalPlayer()) return false;
  introPlaying_ = true;
  PLOG("BOOT", "intro %s%s", sdpath::INTRO_VIDEO, audioFile_ ? "" : " (no audio)");
  return true;
}

void App::finishIntro() {
  introPlaying_ = false;
  if (!stopProgramme()) return;  // MEDIA STUCK is on screen
  lastReport_ = Diagnostics::collect(lcdOk_);
  Diagnostics::log(lastReport_);
  touch_.begin(lastReport_.touchFound());
  if (startupError_[0] != '\0') {
    showStartupError();
    return;
  }
  enter(AppState::ChannelSwitch);  // static, hiss and the CRT flash, as on every zap
}

void App::loop() {
  const uint32_t inputMs = millis();
  pollInput(inputMs);
  pollWebRemote();
#if PAUTV_DEBUG_STATS
  pollSerialCommands(inputMs);
#endif
  // Read after the input: a handler may have entered a new state (a zap starts CHANNEL_SWITCH)
  // or restarted a timer. An older reading would skip the static and the flash of a switch.
  const uint32_t now = millis();
  const uint32_t elapsed = now - stateSinceMs_;  // unsigned: survives millis() wrap
  if (ledBlinkUntilMs_ != 0 && static_cast<int32_t>(now - ledBlinkUntilMs_) >= 0) {
    digitalWrite(PIN_LED_FRONT, HIGH);
    ledBlinkUntilMs_ = 0;
  }
  wifi_.loop(now);
  settings_.loop(now);
  pollBattery(now);
  pollWebGuide(now);
  publishWebState();
  pollConfigRequests(now);
  player_.logStats(now);
  updateVoice(now);
#if PAUTV_DEBUG_STATS
  trackRemotePlayback(now);
  logRemoteStats(now);
  logWifiWarmUp(now);
#endif

  switch (state_) {
    case AppState::Boot:
      if (introPlaying_) {
        if (player_.finished() || elapsed >= INTRO_MAX_MS) finishIntro();
      } else if (elapsed >= BOOT_SCREEN_MS) {
        enter(AppState::Diagnostics);
      }
      break;
    case AppState::Diagnostics:
      if (!micScreen_ && wifi_.state() != shownWifiState_) publishDiagnostics();  // live network lines
      if (!diagFromSettings_ && bootDiagnosticsDone(elapsed)) {
        if (startupError_[0] != '\0') {
          showStartupError();
        } else {
          enter(AppState::Home);
        }
      }
      break;
    case AppState::Home:
      if (elapsed >= HOME_CARD_MS) enter(AppState::ChannelSwitch);  // into the first channel as on every zap
      break;
    case AppState::Playing:
      updatePlaying(now);
      break;
    case AppState::ChannelSwitch:
      updateSwitch(elapsed);
      break;
    case AppState::Settings:
      if (now - settingsPublishedMs_ >= SETTINGS_REFRESH_MS) publishSettings();  // live Wi-Fi
      break;
    case AppState::Recorder:
      updateRecorder(now);
      break;
    case AppState::Error:
      if (errorRecoverable_ && elapsed >= STARTUP_ERROR_MS) {
        errorRecoverable_ = false;
        startupError_[0] = '\0';
        enter(AppState::Home);
      }
      break;
  }

  vTaskDelay(pdMS_TO_TICKS(APP_LOOP_PERIOD_MS));
}

// HOME must state ONLINE or LOCAL for real, so the boot diagnostics wait for the first Wi-Fi
// attempt to resolve. That attempt is capped, so this never waits more than a few seconds.
bool App::bootDiagnosticsDone(uint32_t elapsedMs) const {
  if (elapsedMs < BOOT_DIAG_MS) return false;
  return wifi_.firstAttemptDone() || elapsedMs >= BOOT_DIAG_MS + PAUTV_WIFI_FIRST_TIMEOUT_MS;
}

void App::enter(AppState next) {
  if (next != state_) PLOG("BOOT", "state %s -> %s", appStateName(state_), appStateName(next));
  if (state_ == AppState::Diagnostics && next != AppState::Diagnostics) Diagnostics::ledOff();
  if (next != AppState::Diagnostics) micScreen_ = false;
  voiceMenu_ = false;  // AJUSTES always opens on its main list
  if (next != AppState::Playing) hideOsd();  // the OSD only lives over a programme
  state_ = next;
  stateSinceMs_ = millis();

  switch (next) {
    case AppState::Boot:  // black until begin() knows whether an intro plays (startIntro)
      ui_.publish(UiState(Screen::Black));
      break;
    case AppState::Diagnostics:
      runDiagnostics();
      break;
    case AppState::Home:
      publishHome();
      break;
    case AppState::Playing:
      startProgramme();
      showChannelOsd();
      break;
    case AppState::ChannelSwitch:  // steps 3-4 of a switch: static + hiss, then the CRT flash
      switchPhase_ = SwitchPhase::Static;
      ui_.publish(UiState(Screen::Static));
      audio_.noise(CHANNEL_STATIC_MS + CHANNEL_FLASH_MS, STATIC_NOISE_LEVEL_PCT);
      break;
    case AppState::Settings:
      publishSettings();
      break;
    case AppState::Error:
      break;  // published by fail() / showStartupError()
    case AppState::Recorder:
      break;  // published by startRecorder()
  }
}

void App::fail(const char* headline, const char* detail) {
  PLOG("BOOT", "fatal: %s (%s)", headline, detail);
  errorRecoverable_ = false;
  enter(AppState::Error);
  UiState s(Screen::Error, headline);
  s.addLine("%s", detail);
  ui_.publish(s);
}

void App::showStartupError() {
  errorRecoverable_ = true;
  enter(AppState::Error);
  UiState s(Screen::Error, startupError_);
  s.addLine("%s", startupDetail_);
  s.addLine("USING THE TEST CARD");
  ui_.publish(s);
}

void App::pollInput(uint32_t nowMs) {
  InputEvent e = buttons_.poll(nowMs);
  const InputEvent t = touch_.poll(nowMs);
  if (e == InputEvent::None) e = t;
  if (e != InputEvent::None) onInput(e);

  if (state_ == AppState::Diagnostics && touch_.touching()) {
    ui_.showTouchPoint(touch_.point().x, touch_.point().y);
  }
}

// One logo file into PSRAM, or null (missing, empty, too big, unreadable).
static uint8_t* readLogo(const StorageManager& storage, const char* path, size_t& len) {
  len = 0;
  if (!storage.exists(path)) return nullptr;
  fs::File f = storage.open(path);
  const size_t size = f ? f.size() : 0;
  uint8_t* png = size > 0 && size <= WEB_LOGO_MAX_BYTES
                     ? static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM))
                     : nullptr;
  if (png != nullptr && f.read(png, size) == size) {
    len = size;
    return png;
  }
  PLOG("WEB", "logo %s skipped (%u bytes, max %u)", path, static_cast<unsigned>(size),
       static_cast<unsigned>(WEB_LOGO_MAX_BYTES));
  heap_caps_free(png);
  return nullptr;
}

// Channel logos for the web remote, /retrotv/logos/<channel id>.png plus its .black.png and
// .white.png versions: read once into PSRAM, so serving them never competes with the video for the
// card. Returns the channels that have one (in colour; the one-ink versions are optional).
uint64_t App::loadWebLogos() {
  uint64_t mask = 0;
  size_t loaded = 0;
  size_t bytes = 0;
  for (size_t i = 0; i < channels_.count() && i < 64; ++i) {
    const Channel& c = channels_.at(i);
    if (!c.enabled) continue;
    for (size_t ink = 0; ink < LOGO_INKS; ++ink) {
      if (ink > 0 && !((mask >> i) & 1ull)) break;  // no colour logo: no versions either
      char path[80];
      snprintf(path, sizeof(path), "%s/%s%s.png", sdpath::LOGOS, c.id, LOGO_INK_SUFFIX[ink]);
      size_t len = 0;
      const uint8_t* png = readLogo(storage_, path, len);
      if (png == nullptr) continue;
      web_.addLogo(c.number, static_cast<LogoInk>(ink), png, len);
      if (ink == 0) mask |= 1ull << i;
      ++loaded;
      bytes += len;
    }
  }
  if (loaded > 0) PLOG("WEB", "%u channel logo files, %u KB in PSRAM", static_cast<unsigned>(loaded), static_cast<unsigned>(bytes / 1024));
  return mask;
}

// Web remote: its commands go through the same paths as the knobs, from this task only.
// While the TV is still starting they wait in the queue: a MUTE sent during BOOT or
// DIAGNOSTICS used to be lost (or, in DIAGNOSTICS, play the test tone).
void App::pollWebRemote() {
  if (!web_.announced() && wifi_.online()) {
    char ip[16];
    wifi_.ipString(ip, sizeof(ip));
    web_.announce(ip);
  }
  if (state_ != AppState::Playing && state_ != AppState::ChannelSwitch && state_ != AppState::Settings) return;
  RemoteCommand c;
  while (web_.poll(c)) {
    if (c.pairCode != 0) {
      showPairCode(c.pairCode);
    } else if (c.paired) {
      pairCode_ = 0;  // the code comes off the screen
      if (playMode_ == PlayMode::Teletext) publishTeletext(false);
      refreshOsd();
    } else if (c.tune) {
      blinkLed();
      PLOG("WEB", "channel %u", c.channel);
      tuneNumber(c.channel);
    } else {
      onInput(c.key);
    }
  }
}

void App::publishWebState() {
  RemoteState s;
  if (const Channel* ch = tunedChannel()) {
    s.tuned = true;
    s.channel = ch->number;
    snprintf(s.name, sizeof(s.name), "%s", ch->name);
  }
  s.volume = settings_.volume();
  s.muted = muted_;
  s.battery = battery_.percent();
  s.batteryMv = battery_.millivolts();
  s.batteryLow = battery_.level() != BatteryLevel::Ok;
  s.charging = battery_.charging();
  s.channelsVersion = web_.channelsVersion();
  switch (state_) {
    case AppState::Playing: s.screen = "playing"; break;
    case AppState::ChannelSwitch: s.screen = "switching"; break;
    case AppState::Settings: s.screen = "menu"; break;
    case AppState::Error: s.screen = "error"; break;
    default: s.screen = "starting"; break;
  }
  web_.publish(s);
}

// The LiPo every BATTERY_READ_MS. A warning when the level gets worse (low, then critical),
// shown once a programme is on screen. A flat cell (Battery.h: empty()) says so on screen and
// sends the TV to standby, from any screen: running on until the chip browns out would drain
// the cell further. A key wakes it; on a cell still flat it goes back to standby.
void App::pollBattery(uint32_t nowMs) {
  if (!batteryRead_ || nowMs - batteryReadMs_ >= BATTERY_READ_MS) {
    batteryRead_ = true;
    batteryReadMs_ = nowMs;
    if (battery_.update(Diagnostics::readBatteryMv())) batteryWarning_ = true;
    if (battery_.takePlugged()) {
      chargeNotice_ = true;
      PLOG("BATTERY", "charging: %lu mV", static_cast<unsigned long>(battery_.millivolts()));
    }
    if (battery_.empty() && batteryEmptyAtMs_ == 0) {
      batteryEmptyAtMs_ = nowMs | 1;  // never 0
      OsdState o;
      o.visible = true;
      snprintf(o.title, sizeof(o.title), "BATERIA");
      snprintf(o.subtitle, sizeof(o.subtitle), "AGOTADA: SE APAGA");
      o.alert = true;
      publishOsd(o, BATTERY_EMPTY_NOTICE_MS);
      PLOG("BATTERY", "empty: %lu mV, standby", static_cast<unsigned long>(battery_.millivolts()));
    }
  }
  if (batteryEmptyAtMs_ != 0 && nowMs - batteryEmptyAtMs_ >= BATTERY_EMPTY_NOTICE_MS) enterStandby(false);
  if (batteryWarning_ && state_ == AppState::Playing) {
    batteryWarning_ = false;
    showBatteryWarning();
  }
  if (chargeNotice_ && state_ == AppState::Playing) {  // the cable went in: say so, with the bolt
    chargeNotice_ = false;
    OsdState o;
    o.visible = true;
    snprintf(o.title, sizeof(o.title), "CARGANDO");
    snprintf(o.subtitle, sizeof(o.subtitle), "BATERIA %u%%", static_cast<unsigned>(battery_.percent()));
    fillOsdStatus(o);
    publishOsd(o, BATTERY_CHARGE_NOTICE_MS);
  }
}

void App::showBatteryWarning() {
  const bool critical = battery_.level() == BatteryLevel::Critical;
  OsdState o;
  o.visible = true;
  snprintf(o.title, sizeof(o.title), "BATERIA");
  snprintf(o.subtitle, sizeof(o.subtitle), "%u%% %s", static_cast<unsigned>(battery_.percent()),
           critical ? "CONECTA EL USB-C" : "QUEDA POCA CARGA");
  o.alert = true;
  publishOsd(o, BATTERY_WARNING_MS);
  PLOG("BATTERY", "%s: %u%% (%lu mV)", critical ? "critical" : "low", static_cast<unsigned>(battery_.percent()),
       static_cast<unsigned long>(battery_.millivolts()));
}

// Standby, like a TV's: hold CH- (or the web remote's power key). The picture squeezes into a
// line and a dot with a crackle (the CRT switch-off), the panel, sound and Wi-Fi go off, the pins that could leak are latched and the
// chip sleeps until a key (or BOOT) is pressed. Waking is a new boot: intro, last channel.
void App::enterStandby(bool voiceAllowed) {
#if PAUTV_MIC_ENABLED
  const bool voiceChosen = settings_.voiceStandby();  // AJUSTES > VOZ > APAGADO
#else
  const bool voiceChosen = false;
#endif
  const StandbyMode mode =
      standbyMode(PAUTV_HAS_KEYS, voiceAllowed, voiceStandbyPossible(), voiceChosen, wifi_.online());
  if (mode == StandbyMode::StayOn) {  // button-less, no claps, no Wi-Fi: nothing could switch it back on
    PLOG("POWER", "no keys, no voice standby, no Wi-Fi: staying on");
    OsdState o;
    o.visible = true;
    snprintf(o.title, sizeof(o.title), "NO SE APAGA");
    snprintf(o.subtitle, sizeof(o.subtitle), "SIN WI-FI NI PALMADAS");
    o.alert = true;
    publishOsd(o, NO_OFF_NOTICE_MS);
    return;
  }
  PLOG("POWER", "standby");
  powerDown(mode == StandbyMode::Remote);
  if (mode == StandbyMode::Voice) voiceStandby();    // these do not return
  if (mode == StandbyMode::Remote) remoteStandby();
  deepSleep();
}

void App::powerDown(bool keepWifi) {
  stopProgramme();  // the last picture stays on screen: it is what squeezes
  hideOsd();
  ui_.publish(UiState(Screen::PowerOff));
  audio_.noise(POWER_OFF_CRACKLE_MS, POWER_OFF_CRACKLE_PCT);
  settings_.flush();
  delay(poweroff::TOTAL_MS + 2 * DISPLAY_TICK_MS);  // let it play out; nothing else to do anymore
  display_.sleep();
  if (!keepWifi) {
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
  }

  // Backlight and LED off, amplifier shut down (deepSleep latches them through the sleep).
  ledcDetachPin(PIN_LCD_BL);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);
  digitalWrite(PIN_LED_FRONT, LOW);
  digitalWrite(PIN_AMP_EN, HIGH);  // SC8002B shutdown
}

void App::deepSleep() {
  for (const int pin : {PIN_LCD_BL, PIN_AMP_EN, PIN_LED_FRONT}) gpio_hold_en(static_cast<gpio_num_t>(pin));
  gpio_deep_sleep_hold_en();

  // The key that asked for standby is still down: sleeping now would wake at once.
  const uint32_t t0 = millis();
  while (buttons_.anyKeyDown() && millis() - t0 < STANDBY_RELEASE_WAIT_MS) delay(10);
  delay(BUTTON_DEBOUNCE_MS);
  sleepUntilWoken();
}

void App::sleepUntilWoken() {
  // Any key wakes: the keys need their pull-ups kept on through the sleep (BOOT has its own).
  const uint64_t mask = Buttons::wakeMask();
  for (int pin = 0; pin < 64; ++pin) {
    if ((mask >> pin & 1) == 0) continue;
    rtc_gpio_pullup_en(static_cast<gpio_num_t>(pin));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  if (!PAUTV_HAS_KEYS) {  // no key to press: wake now and then to see whether the battery is charging
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(FLAT_CHECK_S) * 1000000ull);
    PLOG("POWER", "sleeping; checking the battery every %lu s", static_cast<unsigned long>(FLAT_CHECK_S));
  } else {
    PLOG("POWER", "sleeping until a key is pressed");
  }
  Serial.flush();
  esp_deep_sleep_start();
}

// Remote standby (button-less, claps not listening): powerDown() left only the Wi-Fi and the web server
// on, so the remote can switch the TV on again: its power key, a channel or any other key restarts it,
// the same boot as ever. The CPU slows down and the Wi-Fi naps between beacons. A flat battery still ends
// in deepSleep(), which then wakes itself to see whether the cell is charging.
void App::remoteStandby() {
  PLOG("STANDBY", "remote standby: the web remote switches it on");
  setCpuFrequencyMhz(REMOTE_STANDBY_CPU_MHZ);
  WiFi.setSleep(true);
  RemoteState s;
  s.screen = "standby";
  s.channelsVersion = web_.channelsVersion();
  uint32_t batteryMs = millis() - BATTERY_READ_MS;
  for (;;) {
    const uint32_t now = millis();
    wifi_.loop(now);
    if (now - batteryMs >= BATTERY_READ_MS) {
      batteryMs = now;
      battery_.update(Diagnostics::readBatteryMv());
      if (battery_.empty()) {
        PLOG("STANDBY", "battery flat (%lu mV): deep sleep instead", static_cast<unsigned long>(battery_.millivolts()));
        setCpuFrequencyMhz(240);
        deepSleep();
      }
      s.battery = battery_.percent();
      s.batteryMv = battery_.millivolts();
      s.charging = battery_.charging();
      web_.publish(s);
    }
    RemoteCommand c;
    while (web_.poll(c)) {
      if (c.pairCode != 0 || c.paired) continue;  // pairing waits until it is on
      PLOG("STANDBY", "web remote: switching on");
      Serial.flush();
      ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(REMOTE_STANDBY_POLL_MS));
  }
}

// The pilot light goes out for a moment with every order, like a 90s TV taking the remote's.
void App::blinkLed() {
  digitalWrite(PIN_LED_FRONT, LOW);
  ledBlinkUntilMs_ = millis() + LED_BLINK_MS;
  if (ledBlinkUntilMs_ == 0) ledBlinkUntilMs_ = 1;  // 0 means "not blinking"
}

void App::onInput(InputEvent e) {
  PLOG("INPUT", "%s", inputEventName(e));
  lastInput_ = e;
  blinkLed();
  if (e == InputEvent::Power) {
    enterStandby();  // does not return
    return;
  }

  switch (state_) {
    case AppState::Playing:
      onPlayingInput(e);
      break;
    case AppState::ChannelSwitch:
      onSwitchInput(e);
      break;
    case AppState::Settings:
      onSettingsInput(e);
      break;
    case AppState::Diagnostics:
      onDiagnosticsInput(e);
      break;
    case AppState::Boot:
      if (!introPlaying_) break;
      if (e == InputEvent::Mute) {  // silence it, keep watching
        muted_ = !muted_;
        audio_.setMuted(muted_);
      } else {
        finishIntro();  // any other key skips it
      }
      break;
    case AppState::Recorder:
      onRecorderInput(e);
      break;
    case AppState::Home:
    case AppState::Error:
      break;
  }
}

// False (and "--" text) until NTP has set the clock.
bool App::clockText(char* out, size_t len, const char* format) {
  const time_t now = time(nullptr);
  if (now < PAUTV_VALID_EPOCH) {
    snprintf(out, len, "--");
    return false;
  }
  tm local;
  localtime_r(&now, &local);
  strftime(out, len, format, &local);
  return true;
}
