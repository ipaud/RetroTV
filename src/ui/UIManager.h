#pragma once

#include <Arduino_GFX_Library.h>
#include <stdarg.h>

#include <atomic>

#include "config.h"
#include "display/BlockClip.h"
#include "ui/PowerOff.h"
#include "power/Battery.h"
#include "teletext/Teletext.h"

enum class Screen : uint8_t {
  Black,     // power-on, until the TV knows whether an intro video plays
  Boot,
  Diagnostics,
  Home,
  NoSignal,
  TestCard,
  Video,     // video frames cover the panel; only the OSD is drawn on top
  Static,    // full-screen noise while changing channel
  Flash,     // CRT line flash just before the new channel appears
  PowerOff,  // the CRT switch-off (ui/PowerOff.h), over whatever was on screen
  Settings,
  Error,
  Teletext,  // the page from publishTeletext()
  RemoteQr,  // title = the URL to encode ("" = no Wi-Fi); lines = what to read under it
  Microphone,  // RETROTV Voice: the VU meter (meterPct, meterPeakPct) and its level lines
  Recorder,    // GRABADORA: lines[0] big (3 / REC / ...), lines[1] under it, meterPct = REC progress
  Messages,    // MENSAJES: the same layout: number, time, progress, "1 DE 5"
};

// Semantic colour of a text line; the renderer maps it to the palette.
enum class Tone : uint8_t { Normal, Good, Bad, Dim };

constexpr int UI_MAX_LINES = 10;
constexpr int UI_LINE_LEN = 40;
constexpr uint8_t UI_NO_SELECTION = 0xFF;

// Everything a screen needs, as plain data. App builds one and publishes it; only the
// display task turns it into pixels.
struct UiState {
  explicit UiState(Screen s = Screen::Boot, const char* title = "");
  void addLine(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  void addLine(Tone tone, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

  Screen screen;
  char title[UI_LINE_LEN];
  char lines[UI_MAX_LINES][UI_LINE_LEN];
  Tone tones[UI_MAX_LINES];
  uint8_t lineCount = 0;
  uint8_t selected = UI_NO_SELECTION;  // highlighted line (settings menu)
  uint8_t meterPct = 0;      // Screen::Microphone: the level, 0..100 % of the bar
  uint8_t meterPeakPct = 0;  // and the held peak mark

 private:
  void addLineV(Tone tone, const char* fmt, va_list args) __attribute__((format(printf, 3, 0)));
};

// On-screen display over video, test card or NO SIGNAL: top-right box, 90s style.
struct OsdState {
  bool visible = false;
  char title[12] = "";     // "CH 03", "VOL 65", "MUTE"
  char subtitle[24] = "";  // channel name
  bool alert = false;      // a warning (battery): big, centred, blinking
  int8_t volumeBars = -1;  // 0..20 segments, -1 = no bar
  bool showStatus = false; // clock + Wi-Fi line
  char clock[6] = "";      // "HH:MM" once NTP set the time
  int8_t wifiBars = -1;    // 0..3, -1 = offline (LOCAL)
  uint8_t batteryPct = BATTERY_UNKNOWN;  // on the status line, before the Wi-Fi
  bool batteryLow = false;
  bool batteryCharging = false;
};

class UIManager {
 public:
  // Any task. Copy into a mailbox; the display task picks it up next tick.
  void publish(const UiState& state);
  void publishOsd(const OsdState& osd);
  void publishTeletext(const tt::Page& page);

  // Any task. Marks a touch on the diagnostics screen (axis-mapping check); dots accumulate
  // into a trail until the screen is redrawn.
  void showTouchPoint(int16_t x, int16_t y);

  // DisplayManager::RenderFn. Runs on the display task only.
  static void render(Arduino_GFX& gfx, uint32_t nowMs, void* self, ClipRect& overlay);

 private:
  static constexpr int STATIC_BAND_ROWS = 8;

  bool takePending();
  bool takeOsd();
  bool takeTeletext();
  void renderFrame(Arduino_GFX& gfx, uint32_t nowMs, ClipRect& overlay);
  void drawStatic(Arduino_GFX& gfx, int skipY0, int skipY1, uint8_t dimShift, const ClipRect& keep);
  void fillNoise(int width, uint8_t dimShift);

  // Mailboxes, shared between publishers and the display task.
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  UiState pending_{Screen::Black};
  uint32_t pendingVersion_ = 0;
  OsdState pendingOsd_;
  uint32_t pendingOsdVersion_ = 0;
  tt::Page pendingTeletext_{};
  uint32_t pendingTeletextVersion_ = 0;
  static constexpr uint32_t NO_DOT = UINT32_MAX;
  std::atomic<uint32_t> touchDot_{NO_DOT};  // (x << 16) | y

  // Owned by the display task.
  UiState current_{Screen::Black};  // power-on: nothing until the App says
  uint32_t shownVersion_ = 0;
  OsdState osd_;
  uint32_t shownOsdVersion_ = 0;
  tt::Page teletext_{};
  uint32_t shownTeletextVersion_ = 0;
  tt::Page teletextDrawn_{};  // rows as they are on the panel: only changed rows are redrawn
  Screen drawnScreen_ = Screen::Black;
  uint32_t powerOffStartMs_ = 0;
  int powerOffBand_ = 0;  // picture rows still on (centre +- this), as drawn
  ClipRect osdDrawn_;  // where the OSD box currently is on the panel
  uint32_t osdSinceMs_ = 0;  // when this OSD came: an alert blinks from here
  bool osdBlinkOn_ = true;
  uint32_t rng_ = 0x9E3779B9u;
  uint16_t staticBand_[SCREEN_W * STATIC_BAND_ROWS];  // reusable line buffer for noise
};
