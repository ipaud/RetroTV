// Diagnostics, home card, test card and the settings menu.

#include "app/App.h"
#include "config.h"

namespace {

constexpr int I2C_LIST_MAX_SHOWN = 6;  // what fits on one diagnostics line

}  // namespace

void App::runDiagnostics() {
  lastReport_ = Diagnostics::collect(lcdOk_);
  Diagnostics::log(lastReport_);
  touch_.begin(lastReport_.touchFound());
  Diagnostics::showStatusLed(lastReport_.ok());
  publishDiagnostics();
  audio_.beep();
}

void App::publishDiagnostics() {
  const DiagReport& r = lastReport_;
  shownWifiState_ = wifi_.state();

  UiState s(Screen::Diagnostics, "RETROTV / HARDWARE TEST");
  s.addLine(r.lcdOk ? Tone::Good : Tone::Bad, "LCD    %s", r.lcdOk ? "OK" : "FAIL");
  s.addLine(r.touchFound() ? Tone::Good : Tone::Dim, "TOUCH  %s",
            r.touchFound() ? "OK" : "NOT FOUND");
  if (!r.codecFound()) {
    s.addLine(Tone::Bad, "AUDIO  NOT FOUND");
  } else {
    s.addLine(audio_.ready() ? Tone::Good : Tone::Bad, "AUDIO  %s", audio_.status());
  }

  if (storage_.mounted()) {
    const unsigned totalGb = static_cast<unsigned>(storage_.totalBytes() >> 30);
    const unsigned freeGb =
        static_cast<unsigned>((storage_.totalBytes() - storage_.usedBytes()) >> 30);
    s.addLine(Tone::Good, "SD     OK %uG FREE %uG", totalGb, freeGb);
  } else {
    s.addLine(Tone::Bad, "SD     NOT FOUND");
  }

  if (wifi_.online()) {
    char ip[16];
    wifi_.ipString(ip, sizeof(ip));
    s.addLine(Tone::Good, "WIFI   OK %dDBM", wifi_.rssi());
    s.addLine("IP     %s", ip);
  } else {
    s.addLine(Tone::Dim, "WIFI   %s", wifi_.stateName());
    s.addLine(Tone::Dim, "IP     --");
  }

  s.addLine(r.psramTotal > 0 ? Tone::Normal : Tone::Bad, "PSRAM  %uK FREE %uK",
            r.psramTotal / 1024, r.psramFree / 1024);
  s.addLine("FLASH  %uMB HEAP %uK", r.flashPhysical >> 20, r.heapFree / 1024);

  char i2c[UI_LINE_LEN] = "NONE";
  int pos = 0;
  for (int i = 0; i < r.i2cCount && i < I2C_LIST_MAX_SHOWN; ++i) {
    pos += snprintf(i2c + pos, sizeof(i2c) - pos, "%02X ", r.i2c[i]);
  }
  if (r.i2cCount > I2C_LIST_MAX_SHOWN) snprintf(i2c + pos, sizeof(i2c) - pos, "+");
  s.addLine(r.i2cCount > 0 ? Tone::Normal : Tone::Bad, "I2C    %s", i2c);

  s.addLine("BAT    %u.%02uV", r.batteryMv / 1000, (r.batteryMv % 1000) / 10);
  ui_.publish(s);
}

// VOLUME click or a tap plays the 440 Hz test tone. CH_NEXT too, so a bare board with only
// BOOT can run the audio test. MENU goes back to the settings menu it was opened from.
void App::onDiagnosticsInput(InputEvent e) {
  switch (e) {
    case InputEvent::VolUp:
    case InputEvent::ToggleOsd:
    case InputEvent::ChNext:
      PLOG("AUDIO", "test tone %u Hz, %u ms", TEST_TONE_HZ, TEST_TONE_MS);
      audio_.tone(TEST_TONE_HZ, TEST_TONE_MS, TEST_TONE_LEVEL_PCT);
      break;
    case InputEvent::Menu:
      if (!diagFromSettings_) break;
      diagFromSettings_ = false;
      enter(AppState::Settings);
      break;
    case InputEvent::ChPrev:
    case InputEvent::VolDown:
    case InputEvent::Mute:
    case InputEvent::Power:  // App::onInput: standby, before any state
    case InputEvent::None:
      break;
  }
}

void App::publishHome() {
  UiState s(Screen::Home, "RETROTV");
  s.addLine(wifi_.online() ? "ONLINE MODE" : "LOCAL MODE");
  const size_t n = channels_.enabledCount();
  s.addLine("%u CHANNEL%s", static_cast<unsigned>(n), n == 1 ? "" : "S");
  ui_.publish(s);
}

void App::publishTestCard() {
  testCardPublishedMs_ = millis();
  const Channel* ch = channels_.current();
  const unsigned number = ch != nullptr ? ch->number : 0;

  UiState s(Screen::TestCard, "TEST CARD");
  if (muted_) {
    s.addLine("%dx%d %uFPS CH%02u MUTE", SCREEN_W, SCREEN_H, display_.fps(), number);
  } else {
    s.addLine("%dx%d %uFPS CH%02u VOL%u", SCREEN_W, SCREEN_H, display_.fps(), number,
              settings_.volume());
  }

  char clock[12];
  clockText(clock, sizeof(clock), "%H:%M:%S");
  if (lastInput_ == InputEvent::None) {
    s.addLine(Tone::Good, "%s", clock);
  } else {
    s.addLine(Tone::Good, "%s  %s", clock, inputEventName(lastInput_));
  }

  if (wifi_.online()) {
    char ip[16];
    wifi_.ipString(ip, sizeof(ip));
    s.addLine("IP %s %dDBM", ip, wifi_.rssi());
  } else {
    s.addLine(Tone::Dim, "%s", wifi_.state() == WifiState::Connecting ? "WIFI CONNECTING" : "LOCAL MODE");
  }
  ui_.publish(s);
}

// ---------------------------------------------------------------------------------------------
// Settings: CH-/CH+ move, VOL+ selects or raises, VOL- lowers, holding CH+ leaves. Touch:
// swipes do the same, a tap selects, a long press leaves.

void App::publishSettings() {
  settingsPublishedMs_ = millis();
  UiState s(Screen::Settings, "AJUSTES");
  if (wifi_.online()) {
    s.addLine("WI-FI      OK %dDBM", wifi_.rssi());
  } else {
    s.addLine("WI-FI      %s", wifi_.stateName());
  }
  s.addLine("BRILLO     %u%%", settings_.brightness());
  s.addLine(muted_ ? "VOLUMEN    MUTE" : "VOLUMEN    %u", settings_.volume());
  s.addLine("DIAGNOSTICO");
  s.addLine("REINICIAR");
  s.selected = settingsIndex_;
  ui_.publish(s);
}

void App::onSettingsInput(InputEvent e) {
  constexpr uint8_t count = static_cast<uint8_t>(SettingsItem::Count);
  const auto item = static_cast<SettingsItem>(settingsIndex_);
  const bool select = e == InputEvent::VolUp || e == InputEvent::ToggleOsd;
  const int step = e == InputEvent::VolUp ? 1 : (e == InputEvent::VolDown ? -1 : 0);

  switch (e) {
    case InputEvent::ChNext:
      settingsIndex_ = (settingsIndex_ + 1) % count;
      break;
    case InputEvent::ChPrev:
      settingsIndex_ = (settingsIndex_ + count - 1) % count;
      break;
    case InputEvent::Menu:
      enter(AppState::Playing);
      return;
    case InputEvent::VolUp:
    case InputEvent::VolDown:
    case InputEvent::ToggleOsd:
      switch (item) {
        case SettingsItem::Wifi:
          if (select) wifi_.retryNow();
          break;
        case SettingsItem::Brightness: {
          const int b = settings_.brightness() + step * BRIGHTNESS_STEP_PCT;
          settings_.setBrightness(static_cast<uint8_t>(b < BRIGHTNESS_MIN_PCT ? BRIGHTNESS_MIN_PCT : (b > 100 ? 100 : b)));
          display_.setBrightness(settings_.brightness());
          break;
        }
        case SettingsItem::Volume:
          if (step != 0) {
            changeVolume(step * VOLUME_STEP);
            audio_.beep();  // hear the new level; nothing else is playing in the menu
          }
          break;
        case SettingsItem::Diagnostics:
          if (!select) break;
          diagFromSettings_ = true;
          enter(AppState::Diagnostics);
          return;
        case SettingsItem::Restart:
          if (!select) break;
          settings_.flush();
          PLOG("BOOT", "restart requested from settings");
          delay(100);  // let the log line out
          ESP.restart();
          return;
        case SettingsItem::Count:
          break;
      }
      break;
    case InputEvent::Mute:
    case InputEvent::Power:  // App::onInput: standby, before any state
    case InputEvent::None:
      break;
  }
  publishSettings();
}
