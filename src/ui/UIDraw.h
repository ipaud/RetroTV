#pragma once

// Drawing helpers shared by UIManager.cpp and UIScreens.cpp. Display task only.

#include <Arduino_GFX_Library.h>

#include "display/BlockClip.h"
#include "ui/UIManager.h"

namespace ui {

constexpr uint16_t COLOR_BLACK = RGB565(0, 0, 0);
constexpr uint16_t COLOR_WHITE = RGB565(255, 255, 255);
constexpr uint16_t COLOR_GREEN = RGB565(64, 255, 96);  // CRT phosphor
constexpr uint16_t COLOR_DARK_GREEN = RGB565(16, 72, 24);
constexpr uint16_t COLOR_GREY = RGB565(150, 150, 150);
constexpr uint16_t COLOR_DARK_GREY = RGB565(60, 60, 60);
constexpr uint16_t COLOR_RED = RGB565(200, 0, 0);
constexpr uint16_t COLOR_BAD = RGB565(255, 90, 90);  // readable red for text on black
constexpr uint16_t COLOR_YELLOW = RGB565(255, 220, 0);  // the charging bolt

// Black bands kept free of static (multiples of the static band height).
constexpr int BOOT_CARD_Y0 = 64;
constexpr int BOOT_CARD_Y1 = 176;
constexpr int NO_SIGNAL_Y0 = 88;
constexpr int NO_SIGNAL_Y1 = 152;
constexpr int TOUCH_DOT_R = 3;

uint16_t toneColor(Tone t);
void drawText(Arduino_GFX& gfx, int x, int y, const char* text, uint8_t size, uint16_t color);
void drawCentered(Arduino_GFX& gfx, int y, const char* text, uint8_t size, uint16_t color);

void drawBootCard(Arduino_GFX& gfx);
void drawNoSignalCard(Arduino_GFX& gfx, const UiState& s);
void drawHomeCard(Arduino_GFX& gfx, const UiState& s);
void drawDiagnostics(Arduino_GFX& gfx, const UiState& s);
void drawTestCardBars(Arduino_GFX& gfx);
void drawTestCardInfo(Arduino_GFX& gfx, const UiState& s);
void drawRemoteQr(Arduino_GFX& gfx, const UiState& s);
void drawFlash(Arduino_GFX& gfx);
// One step of the switch-off; `drawnBand` is the band left by the previous step.
void drawPowerOff(Arduino_GFX& gfx, const poweroff::Frame& f, int drawnBand);
void drawSettings(Arduino_GFX& gfx, const UiState& s);
void drawError(Arduino_GFX& gfx, const UiState& s);

// Redraws the rows of `page` that differ from `drawn` (all of them with `force`) and updates
// `drawn`. `band` is a SCREEN_W x bandRows scratch buffer.
void drawTeletext(Arduino_GFX& gfx, const tt::Page& page, tt::Page& drawn, bool force, uint16_t* band,
                  int bandRows);

// OSD box in the top-right corner, sized to its content.
ClipRect osdBox(const OsdState& o);
void drawOsd(Arduino_GFX& gfx, const OsdState& o, const ClipRect& box);

}  // namespace ui
