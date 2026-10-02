#include "ui/UIManager.h"

#include <stdio.h>

#include "ui/UIDraw.h"

namespace {

// Noise brightness: 0 = full, 1 = half.
constexpr uint8_t STATIC_BRIGHT = 0;
constexpr uint8_t STATIC_DIM = 1;

bool osdAllowedOn(Screen s) {
  return s == Screen::Video || s == Screen::TestCard || s == Screen::NoSignal || s == Screen::RemoteQr;
}

}  // namespace

UiState::UiState(Screen s, const char* t) : screen(s), title{}, lines{}, tones{} {
  snprintf(title, sizeof(title), "%s", t);
}

void UiState::addLine(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  addLineV(Tone::Normal, fmt, args);
  va_end(args);
}

void UiState::addLine(Tone tone, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  addLineV(tone, fmt, args);
  va_end(args);
}

void UiState::addLineV(Tone tone, const char* fmt, va_list args) {
  if (lineCount >= UI_MAX_LINES) return;
  vsnprintf(lines[lineCount], UI_LINE_LEN, fmt, args);
  tones[lineCount] = tone;
  ++lineCount;
}

void UIManager::showTouchPoint(int16_t x, int16_t y) {
  touchDot_.store((static_cast<uint32_t>(x) << 16) | static_cast<uint16_t>(y));
}

void UIManager::publish(const UiState& state) {
  portENTER_CRITICAL(&lock_);
  pending_ = state;
  ++pendingVersion_;
  portEXIT_CRITICAL(&lock_);
}

void UIManager::publishOsd(const OsdState& osd) {
  portENTER_CRITICAL(&lock_);
  pendingOsd_ = osd;
  ++pendingOsdVersion_;
  portEXIT_CRITICAL(&lock_);
}

void UIManager::publishTeletext(const tt::Page& page) {
  portENTER_CRITICAL(&lock_);
  pendingTeletext_ = page;
  ++pendingTeletextVersion_;
  portEXIT_CRITICAL(&lock_);
}

bool UIManager::takePending() {
  portENTER_CRITICAL(&lock_);
  const bool changed = pendingVersion_ != shownVersion_;
  if (changed) {
    current_ = pending_;
    shownVersion_ = pendingVersion_;
  }
  portEXIT_CRITICAL(&lock_);
  return changed;
}

bool UIManager::takeOsd() {
  portENTER_CRITICAL(&lock_);
  const bool changed = pendingOsdVersion_ != shownOsdVersion_;
  if (changed) {
    osd_ = pendingOsd_;
    shownOsdVersion_ = pendingOsdVersion_;
  }
  portEXIT_CRITICAL(&lock_);
  return changed;
}

bool UIManager::takeTeletext() {
  portENTER_CRITICAL(&lock_);
  const bool changed = pendingTeletextVersion_ != shownTeletextVersion_;
  if (changed) {
    teletext_ = pendingTeletext_;
    shownTeletextVersion_ = pendingTeletextVersion_;
  }
  portEXIT_CRITICAL(&lock_);
  return changed;
}

void UIManager::render(Arduino_GFX& gfx, uint32_t nowMs, void* self, ClipRect& overlay) {
  static_cast<UIManager*>(self)->renderFrame(gfx, nowMs, overlay);
}

void UIManager::renderFrame(Arduino_GFX& gfx, uint32_t nowMs, ClipRect& overlay) {
  const bool changed = takePending();
  const bool osdChanged = takeOsd();
  const bool teletextChanged = takeTeletext();  // after takePending: a new screen finds its page
  const bool newScreen = changed && current_.screen != drawnScreen_;
  drawnScreen_ = current_.screen;

  if (osdChanged) osdSinceMs_ = nowMs;
  // An alert blinks: off for a while, the video (or screen) under it shows through.
  const bool blinkOn = !osd_.alert || (nowMs - osdSinceMs_) % (ALERT_BLINK_ON_MS + ALERT_BLINK_OFF_MS) < ALERT_BLINK_ON_MS;
  const bool blinkedOn = blinkOn && !osdBlinkOn_;
  osdBlinkOn_ = blinkOn;
  const bool showOsd = osd_.visible && blinkOn && osdAllowedOn(current_.screen);
  const ClipRect box = showOsd ? ui::osdBox(osd_) : ClipRect{};
  // The OSD shrank or went away: what was under it has to be painted again.
  const bool uncovered = !osdDrawn_.empty() &&
                         (!showOsd || box.x != osdDrawn_.x || box.w != osdDrawn_.w || box.h != osdDrawn_.h);
  overlay = box;  // DisplayManager keeps video blocks out of this box

  switch (current_.screen) {
    case Screen::Black:
      if (changed) gfx.fillScreen(ui::COLOR_BLACK);
      break;
    case Screen::Boot:
      if (changed) ui::drawBootCard(gfx);
      drawStatic(gfx, ui::BOOT_CARD_Y0, ui::BOOT_CARD_Y1, STATIC_BRIGHT, ClipRect{});
      break;
    case Screen::NoSignal:  // the animated static repaints whatever the OSD left behind
      if (changed) ui::drawNoSignalCard(gfx, current_);
      drawStatic(gfx, ui::NO_SIGNAL_Y0, ui::NO_SIGNAL_Y1, STATIC_DIM, box);
      break;
    case Screen::Static:
      drawStatic(gfx, 0, 0, STATIC_BRIGHT, ClipRect{});
      break;
    case Screen::Flash:
      if (changed) ui::drawFlash(gfx);
      break;
    case Screen::PowerOff: {
      if (newScreen) {
        powerOffStartMs_ = millis();
        powerOffBand_ = poweroff::FULL_HALF_H;
      }
      const poweroff::Frame f = poweroff::frameAt(millis() - powerOffStartMs_);
      ui::drawPowerOff(gfx, f, powerOffBand_);
      powerOffBand_ = f.bandHalf;
      break;
    }
    case Screen::Home:
      if (changed) ui::drawHomeCard(gfx, current_);
      break;
    case Screen::Diagnostics: {
      if (changed) ui::drawDiagnostics(gfx, current_);
      const uint32_t dot = touchDot_.exchange(NO_DOT);
      if (dot != NO_DOT) gfx.fillCircle(dot >> 16, dot & 0xFFFF, ui::TOUCH_DOT_R, ui::COLOR_GREEN);
      break;
    }
    case Screen::TestCard:
      if (newScreen || uncovered) ui::drawTestCardBars(gfx);
      if (changed || uncovered) ui::drawTestCardInfo(gfx, current_);
      break;
    case Screen::Video:
      break;  // frames cover the panel; an uncovered area is repainted by the next frame
    case Screen::Settings:
      if (changed) ui::drawSettings(gfx, current_);
      break;
    case Screen::RemoteQr:
      if (changed || uncovered) ui::drawRemoteQr(gfx, current_);
      break;
    case Screen::Error:
      if (changed) ui::drawError(gfx, current_);
      break;
    case Screen::Teletext:
      if (newScreen || teletextChanged) {
        ui::drawTeletext(gfx, teletext_, teletextDrawn_, newScreen, staticBand_, STATIC_BAND_ROWS);
      }
      break;
  }

  if (showOsd && (osdChanged || changed || uncovered || blinkedOn)) ui::drawOsd(gfx, osd_, box);
  osdDrawn_ = box;
}

// Fills `width` x STATIC_BAND_ROWS pixels of grey noise with darker odd rows (CRT scanlines).
void UIManager::fillNoise(int width, uint8_t dimShift) {
  for (int row = 0; row < STATIC_BAND_ROWS; ++row) {
    const uint8_t shift = dimShift + (row & 1);
    uint16_t* px = &staticBand_[row * width];
    for (int x = 0; x < width; x += 2) {
      rng_ ^= rng_ << 13;  // xorshift32
      rng_ ^= rng_ >> 17;
      rng_ ^= rng_ << 5;
      const uint16_t a = (rng_ & 0x1F) >> shift;
      const uint16_t b = ((rng_ >> 16) & 0x1F) >> shift;
      px[x] = (a << 11) | (a << 6) | a;  // grey: R5 = v, G6 = 2v, B5 = v
      if (x + 1 < width) px[x + 1] = (b << 11) | (b << 6) | b;
    }
  }
}

// Procedural static, one reusable band at a time. Rows in [skipY0, skipY1) are left alone
// for a text card; `keep` (the OSD box) is never painted over, so nothing flickers.
void UIManager::drawStatic(Arduino_GFX& gfx, int skipY0, int skipY1, uint8_t dimShift,
                           const ClipRect& keep) {
  for (int y = 0; y < SCREEN_H; y += STATIC_BAND_ROWS) {
    if (y < skipY1 && y + STATIC_BAND_ROWS > skipY0) continue;
    const bool besideOsd = !keep.empty() && y < keep.y + keep.h && y + STATIC_BAND_ROWS > keep.y;
    if (!besideOsd) {
      fillNoise(SCREEN_W, dimShift);
      gfx.draw16bitRGBBitmap(0, y, staticBand_, SCREEN_W, STATIC_BAND_ROWS);
      continue;
    }
    // Only the columns left and right of the OSD box.
    if (keep.x > 0) {
      fillNoise(keep.x, dimShift);
      gfx.draw16bitRGBBitmap(0, y, staticBand_, keep.x, STATIC_BAND_ROWS);
    }
    const int right = keep.x + keep.w;
    if (right < SCREEN_W) {
      fillNoise(SCREEN_W - right, dimShift);
      gfx.draw16bitRGBBitmap(right, y, staticBand_, SCREEN_W - right, STATIC_BAND_ROWS);
    }
  }
}
