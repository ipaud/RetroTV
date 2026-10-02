#include <qrcode.h>  // ESP-IDF's QR encoder (Nayuki's qrcodegen), already in the Arduino core
#include <string.h>

#include "ui/UIDraw.h"

namespace ui {

namespace {

constexpr int GLCD_CHAR_W = 6;  // 5x7 font + 1 px spacing, times text size
constexpr uint16_t COLOR_SHADOW = COLOR_BLACK;

// Test card: colour bars, a castellation row, then a black info panel.
constexpr int TEST_BARS_H = 160;
constexpr int TEST_CASTLE_H = 16;
constexpr int TEST_INFO_Y = TEST_BARS_H + TEST_CASTLE_H;
constexpr int TEST_BAR_COUNT = 7;
constexpr uint16_t TEST_BARS[TEST_BAR_COUNT] = {
    RGB565(255, 255, 255), RGB565(255, 255, 0), RGB565(0, 255, 255), RGB565(0, 255, 0),
    RGB565(255, 0, 255),   RGB565(255, 0, 0),   RGB565(0, 0, 255)};
constexpr uint16_t TEST_CASTLE[TEST_BAR_COUNT] = {
    RGB565(0, 0, 255), COLOR_BLACK, RGB565(255, 0, 255), COLOR_BLACK,
    RGB565(0, 255, 255), COLOR_BLACK, RGB565(255, 255, 255)};

constexpr int DIAG_FIRST_LINE_Y = 38;
constexpr int DIAG_LINE_STEP = 20;

constexpr int SETTINGS_FIRST_Y = 52;
constexpr int SETTINGS_STEP = 30;
constexpr const char* SETTINGS_HINT = "CH-/CH+ MOVER  VOL-/VOL+ CAMBIAR  MANTEN CH+ SALIR";

// OSD geometry.
constexpr int OSD_MARGIN = 8;     // also a multiple of the static band height
constexpr int OSD_ROW_ALIGN = 8;  // = UIManager static band rows
constexpr int OSD_PAD = 6;
constexpr int OSD_GAP = 5;
constexpr int OSD_MIN_W = 120;
constexpr int OSD_TITLE_SIZE = 3;
constexpr int OSD_TEXT_SIZE = 2;
constexpr int ALERT_TITLE_SIZE = 4;  // an alert's title: 32 px high, centred on the screen
constexpr int VOLUME_SEGMENTS = 20;
constexpr int VOLUME_SEG_W = 6;
constexpr int VOLUME_SEG_H = 10;
constexpr int WIFI_BAR_W = 4;
constexpr int WIFI_BAR_STEP = 6;
constexpr int BATTERY_ICON_W = 16;  // outline; the nub and the percentage go after it
constexpr int BATTERY_ICON_H = 8;
constexpr int BATTERY_NUB_W = 2;
constexpr int STATUS_GAP = 8;

int textWidth(const char* text, int size) { return static_cast<int>(strlen(text)) * GLCD_CHAR_W * size; }

}  // namespace

uint16_t toneColor(Tone t) {
  switch (t) {
    case Tone::Good: return COLOR_GREEN;
    case Tone::Bad: return COLOR_BAD;
    case Tone::Dim: return COLOR_GREY;
    case Tone::Normal: break;
  }
  return COLOR_WHITE;
}

// GLCD text with a 1-unit drop shadow, the way a 90s OSD looked.
void drawText(Arduino_GFX& gfx, int x, int y, const char* text, uint8_t size, uint16_t color) {
  gfx.setTextWrap(false);
  gfx.setTextSize(size);
  gfx.setTextColor(COLOR_SHADOW);
  gfx.setCursor(x + size, y + size);
  gfx.print(text);
  gfx.setTextColor(color);
  gfx.setCursor(x, y);
  gfx.print(text);
}

void drawCentered(Arduino_GFX& gfx, int y, const char* text, uint8_t size, uint16_t color) {
  drawText(gfx, (SCREEN_W - textWidth(text, size)) / 2, y, text, size, color);
}

void drawBootCard(Arduino_GFX& gfx) {
  gfx.fillRect(0, BOOT_CARD_Y0, SCREEN_W, BOOT_CARD_Y1 - BOOT_CARD_Y0, COLOR_BLACK);
  drawCentered(gfx, 72, "RETROTV", 6, COLOR_WHITE);
  drawCentered(gfx, 128, "TELEVISION", 2, COLOR_WHITE);
  drawCentered(gfx, 158, "SYSTEM START", 1, COLOR_GREEN);
}

void drawNoSignalCard(Arduino_GFX& gfx, const UiState& s) {
  gfx.fillRect(0, NO_SIGNAL_Y0, SCREEN_W, NO_SIGNAL_Y1 - NO_SIGNAL_Y0, COLOR_BLACK);
  drawCentered(gfx, 98, "NO SIGNAL", 3, COLOR_WHITE);
  if (s.lineCount > 0) drawCentered(gfx, 130, s.lines[0], 2, COLOR_GREY);
}

void drawHomeCard(Arduino_GFX& gfx, const UiState& s) {
  gfx.fillScreen(COLOR_BLACK);
  gfx.drawRect(24, 48, SCREEN_W - 48, SCREEN_H - 96, COLOR_GREEN);
  drawCentered(gfx, 72, s.title, 4, COLOR_GREEN);
  if (s.lineCount > 0) drawCentered(gfx, 128, s.lines[0], 2, COLOR_WHITE);
  if (s.lineCount > 1) drawCentered(gfx, 156, s.lines[1], 2, COLOR_GREY);
}

void drawDiagnostics(Arduino_GFX& gfx, const UiState& s) {
  gfx.fillScreen(COLOR_BLACK);
  drawText(gfx, 12, 10, s.title, 2, COLOR_GREEN);
  for (int i = 0; i < s.lineCount; ++i) {
    drawText(gfx, 12, DIAG_FIRST_LINE_Y + i * DIAG_LINE_STEP, s.lines[i], 2, toneColor(s.tones[i]));
  }
}

void drawTestCardBars(Arduino_GFX& gfx) {
  for (int i = 0; i < TEST_BAR_COUNT; ++i) {
    const int x0 = i * SCREEN_W / TEST_BAR_COUNT;
    const int w = (i + 1) * SCREEN_W / TEST_BAR_COUNT - x0;
    gfx.fillRect(x0, 0, w, TEST_BARS_H, TEST_BARS[i]);
    gfx.fillRect(x0, TEST_BARS_H, w, TEST_CASTLE_H, TEST_CASTLE[i]);
  }
}

// Only the info panel changes every second, so the bars are not redrawn.
void drawTestCardInfo(Arduino_GFX& gfx, const UiState& s) {
  gfx.fillRect(0, TEST_INFO_Y, SCREEN_W, SCREEN_H - TEST_INFO_Y, COLOR_BLACK);
  for (int i = 0; i < s.lineCount && i < 3; ++i) {
    drawText(gfx, 12, TEST_INFO_Y + 6 + i * 20, s.lines[i], 2, toneColor(s.tones[i]));
  }
}

// The web remote's channel: a QR code with the TV's own address, and the address in words.
namespace {
constexpr int QR_Y = 6;
constexpr int QR_MAX_PX = 180;   // the text lines fit under it
constexpr int QR_QUIET = 3;      // white modules around the code (4 in the spec; black around it helps)
Arduino_GFX* qrGfx = nullptr;    // esp_qrcode_generate's callback takes no context; DisplayTask only

void drawQrModules(esp_qrcode_handle_t qr) {
  const int size = esp_qrcode_get_size(qr);
  const int scale = QR_MAX_PX / (size + 2 * QR_QUIET);
  const int total = scale * (size + 2 * QR_QUIET);
  const int x0 = (SCREEN_W - total) / 2;
  qrGfx->fillRect(x0, QR_Y, total, total, COLOR_WHITE);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (esp_qrcode_get_module(qr, x, y)) {
        qrGfx->fillRect(x0 + (QR_QUIET + x) * scale, QR_Y + (QR_QUIET + y) * scale, scale, scale, COLOR_BLACK);
      }
    }
  }
}

}  // namespace

void drawRemoteQr(Arduino_GFX& gfx, const UiState& s) {
  gfx.fillScreen(COLOR_BLACK);
  if (s.title[0] != '\0') {
    qrGfx = &gfx;
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = drawQrModules;
    cfg.max_qrcode_version = 6;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    esp_qrcode_generate(&cfg, s.title);
  }
  const int y = s.title[0] != '\0' ? QR_Y + QR_MAX_PX + 6 : 90;
  if (s.lineCount > 0) drawCentered(gfx, y, s.lines[0], 1, COLOR_GREY);
  if (s.lineCount > 1) drawCentered(gfx, y + 14, s.lines[1], 2, COLOR_WHITE);
  if (s.lineCount > 2) drawCentered(gfx, y + 36, s.lines[2], 1, COLOR_GREY);
}

// The picture collapsing into a bright horizontal line, like a CRT between channels.
void drawFlash(Arduino_GFX& gfx) {
  constexpr int mid = SCREEN_H / 2;
  gfx.fillScreen(COLOR_BLACK);
  gfx.fillRect(0, mid - 6, SCREEN_W, 12, COLOR_DARK_GREY);
  gfx.fillRect(0, mid - 3, SCREEN_W, 6, COLOR_GREY);
  gfx.fillRect(0, mid - 1, SCREEN_W, 2, COLOR_WHITE);
}

// Only what changed since the previous step is drawn: rows going dark while the picture
// squeezes (its bright edges drawn on top), then the line on the middle rows, then the dot.
void drawPowerOff(Arduino_GFX& gfx, const poweroff::Frame& f, int drawnBand) {
  constexpr int cy = SCREEN_H / 2;
  constexpr int cx = SCREEN_W / 2;
  if (f.lineHalfW == poweroff::FULL_HALF_W && f.bandHalf > poweroff::LINE_HALF_H) {
    const int gone = drawnBand - f.bandHalf;
    if (gone > 0) {
      gfx.fillRect(0, cy - drawnBand, SCREEN_W, gone, COLOR_BLACK);
      gfx.fillRect(0, cy + f.bandHalf + 1, SCREEN_W, gone, COLOR_BLACK);
    }
    gfx.drawFastHLine(0, cy - f.bandHalf, SCREEN_W, COLOR_WHITE);
    gfx.drawFastHLine(0, cy + f.bandHalf, SCREEN_W, COLOR_WHITE);
    return;
  }
  if (drawnBand > poweroff::LINE_HALF_H) gfx.fillRect(0, cy - drawnBand, SCREEN_W, 2 * drawnBand + 1, COLOR_BLACK);
  constexpr int lineH = 2 * poweroff::LINE_HALF_H + 1;
  gfx.fillRect(0, cy - poweroff::LINE_HALF_H, SCREEN_W, lineH, COLOR_BLACK);
  if (f.lineHalfW > 0) {
    gfx.fillRect(cx - f.lineHalfW, cy - poweroff::LINE_HALF_H, 2 * f.lineHalfW, lineH, COLOR_WHITE);
    return;
  }
  const uint16_t glow = RGB565(f.dot, f.dot, f.dot);
  gfx.fillRect(cx - poweroff::DOT_HALF, cy - poweroff::DOT_HALF, 2 * poweroff::DOT_HALF, 2 * poweroff::DOT_HALF, glow);
}

void drawSettings(Arduino_GFX& gfx, const UiState& s) {
  gfx.fillScreen(COLOR_BLACK);
  drawText(gfx, 12, 10, s.title, 3, COLOR_GREEN);
  for (int i = 0; i < s.lineCount; ++i) {
    const int y = SETTINGS_FIRST_Y + i * SETTINGS_STEP;
    const bool selected = i == s.selected;
    if (selected) gfx.fillRect(6, y - 5, SCREEN_W - 12, 26, COLOR_GREEN);
    drawText(gfx, 16, y, s.lines[i], 2, selected ? COLOR_BLACK : toneColor(s.tones[i]));
  }
  drawText(gfx, 4, SCREEN_H - 12, SETTINGS_HINT, 1, COLOR_GREY);
}

void drawError(Arduino_GFX& gfx, const UiState& s) {
  gfx.fillScreen(COLOR_BLACK);
  gfx.fillRect(0, 0, SCREEN_W, 40, COLOR_RED);
  drawText(gfx, 12, 8, "ERROR", 3, COLOR_WHITE);
  drawText(gfx, 12, 60, s.title, 2, COLOR_WHITE);
  for (int i = 0; i < s.lineCount; ++i) drawText(gfx, 12, 92 + i * 22, s.lines[i], 2, COLOR_GREY);
}

int wifiWidth(const OsdState& o) { return o.wifiBars < 0 ? textWidth("LOCAL", 1) : 3 * WIFI_BAR_STEP; }

int batteryWidth() { return BATTERY_ICON_W + BATTERY_NUB_W + 3 + textWidth("100%", 1); }

// Clock on the left; battery and Wi-Fi on the right.
int statusWidth(const OsdState& o) {
  int w = textWidth("00:00", OSD_TEXT_SIZE) + STATUS_GAP + wifiWidth(o);
  if (o.batteryPct != BATTERY_UNKNOWN) w += batteryWidth() + STATUS_GAP;
  return w;
}

// A small cell: outline, charge filled in (red when low), and the percentage. Charging: a green
// outline and a yellow bolt across it (it pokes out above and below, into the line's spacing).
void drawBattery(Arduino_GFX& gfx, int x, int y, uint8_t pct, bool low, bool charging) {
  const uint16_t fill = low ? COLOR_BAD : COLOR_GREEN;
  const uint16_t frame = charging ? COLOR_GREEN : COLOR_GREY;
  gfx.drawRect(x, y, BATTERY_ICON_W, BATTERY_ICON_H, frame);
  gfx.fillRect(x + BATTERY_ICON_W, y + 2, BATTERY_NUB_W, BATTERY_ICON_H - 4, frame);
  const int inner = BATTERY_ICON_W - 4;
  const int filled = (inner * pct + 99) / 100;
  if (charging) {
    const int cx = x + BATTERY_ICON_W / 2;
    const int top = y - 2;
    gfx.fillTriangle(cx + 2, top, cx - 3, top + 6, cx + 1, top + 6, COLOR_YELLOW);
    gfx.fillTriangle(cx - 1, top + 5, cx + 3, top + 5, cx - 2, top + 11, COLOR_YELLOW);
  } else if (filled > 0) {
    gfx.fillRect(x + 2, y + 2, filled, BATTERY_ICON_H - 4, fill);
  }
  char text[6];
  snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(pct));
  drawText(gfx, x + BATTERY_ICON_W + BATTERY_NUB_W + 3, y, text, 1,
           charging ? COLOR_GREEN : low ? COLOR_BAD : COLOR_GREY);
}

ClipRect osdBox(const OsdState& o) {
  const int lineH = 8 * OSD_TEXT_SIZE;
  if (o.alert) {  // title and one line, centred; whole static bands, like the corner box
    const int tw = textWidth(o.title, ALERT_TITLE_SIZE);
    const int sw = textWidth(o.subtitle, OSD_TEXT_SIZE);
    int w = (tw > sw ? tw : sw) + 4 * OSD_PAD;
    if (w > SCREEN_W - 2 * OSD_MARGIN) w = SCREEN_W - 2 * OSD_MARGIN;
    int h = 2 * OSD_PAD + 8 * ALERT_TITLE_SIZE + (o.subtitle[0] != '\0' ? OSD_GAP + lineH : 0) + 2 * OSD_PAD;
    h = (h + OSD_ROW_ALIGN - 1) / OSD_ROW_ALIGN * OSD_ROW_ALIGN;
    const int y = (SCREEN_H - h) / 2 / OSD_ROW_ALIGN * OSD_ROW_ALIGN;
    return ClipRect{(SCREEN_W - w) / 2, y, w, h};
  }
  int w = textWidth(o.title, OSD_TITLE_SIZE);
  int h = OSD_PAD + 8 * OSD_TITLE_SIZE;
  if (o.subtitle[0] != '\0') {
    const int sw = textWidth(o.subtitle, OSD_TEXT_SIZE);
    w = sw > w ? sw : w;
    h += OSD_GAP + lineH;
  }
  if (o.volumeBars >= 0) {
    const int vw = VOLUME_SEGMENTS * VOLUME_SEG_W;
    w = vw > w ? vw : w;
    h += OSD_GAP + VOLUME_SEG_H;
  }
  if (o.showStatus) {
    h += OSD_GAP + lineH;
    const int sw = statusWidth(o);
    w = sw > w ? sw : w;
  }
  w += 2 * OSD_PAD;
  if (w < OSD_MIN_W) w = OSD_MIN_W;
  if (w > SCREEN_W - 2 * OSD_MARGIN) w = SCREEN_W - 2 * OSD_MARGIN;
  h += OSD_PAD;
  h = (h + OSD_ROW_ALIGN - 1) / OSD_ROW_ALIGN * OSD_ROW_ALIGN;  // whole static bands beside it
  return ClipRect{SCREEN_W - OSD_MARGIN - w, OSD_MARGIN, w, h};
}

void drawOsd(Arduino_GFX& gfx, const OsdState& o, const ClipRect& box) {
  const int lineH = 8 * OSD_TEXT_SIZE;
  const int x = box.x + OSD_PAD;
  gfx.fillRect(box.x, box.y, box.w, box.h, COLOR_BLACK);
  if (o.alert) {  // red double frame, both lines centred
    gfx.drawRect(box.x, box.y, box.w, box.h, COLOR_BAD);
    gfx.drawRect(box.x + 2, box.y + 2, box.w - 4, box.h - 4, COLOR_BAD);
    const int titleH = 8 * ALERT_TITLE_SIZE;
    const int textH = titleH + (o.subtitle[0] != '\0' ? OSD_GAP + lineH : 0);
    int y = box.y + (box.h - textH) / 2;
    drawText(gfx, box.x + (box.w - textWidth(o.title, ALERT_TITLE_SIZE)) / 2, y, o.title, ALERT_TITLE_SIZE, COLOR_BAD);
    y += titleH + OSD_GAP;
    if (o.subtitle[0] != '\0') {
      drawText(gfx, box.x + (box.w - textWidth(o.subtitle, OSD_TEXT_SIZE)) / 2, y, o.subtitle, OSD_TEXT_SIZE, COLOR_WHITE);
    }
    return;
  }
  gfx.drawRect(box.x, box.y, box.w, box.h, COLOR_DARK_GREEN);

  int y = box.y + OSD_PAD;
  drawText(gfx, x, y, o.title, OSD_TITLE_SIZE, COLOR_GREEN);
  y += 8 * OSD_TITLE_SIZE;
  if (o.subtitle[0] != '\0') {
    y += OSD_GAP;
    drawText(gfx, x, y, o.subtitle, OSD_TEXT_SIZE, COLOR_WHITE);
    y += lineH;
  }
  if (o.volumeBars >= 0) {
    y += OSD_GAP;
    for (int i = 0; i < VOLUME_SEGMENTS; ++i) {
      gfx.fillRect(x + i * VOLUME_SEG_W, y, VOLUME_SEG_W - 1, VOLUME_SEG_H,
                   i < o.volumeBars ? COLOR_GREEN : COLOR_DARK_GREY);
    }
    y += VOLUME_SEG_H;
  }
  if (o.showStatus) {
    y += OSD_GAP;
    if (o.clock[0] != '\0') drawText(gfx, x, y, o.clock, OSD_TEXT_SIZE, COLOR_GREY);
    const int right = box.x + box.w - OSD_PAD;
    if (o.batteryPct != BATTERY_UNKNOWN) {
      drawBattery(gfx, right - wifiWidth(o) - STATUS_GAP - batteryWidth(), y + 4, o.batteryPct, o.batteryLow,
                  o.batteryCharging);
    }
    if (o.wifiBars < 0) {
      drawText(gfx, right - textWidth("LOCAL", 1), y + 4, "LOCAL", 1, COLOR_GREY);
    } else {
      for (int i = 0; i < 3; ++i) {  // signal bars, bottom-aligned
        const int barH = 5 + 4 * i;
        gfx.fillRect(right - (3 - i) * WIFI_BAR_STEP, y + lineH - barH, WIFI_BAR_W, barH,
                     i < o.wifiBars ? COLOR_GREEN : COLOR_DARK_GREY);
      }
    }
  }
}

}  // namespace ui
