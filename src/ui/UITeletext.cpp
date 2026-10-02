// Teletext page renderer. Each row is built pixel by pixel in a band buffer and sent in one
// transfer per band, so a page change paints top to bottom like a real set, without flicker.

#include <font/glcdfont.h>  // the classic 5x7 font Arduino_GFX prints with (a private copy)

#include "ui/UIDraw.h"

namespace ui {

namespace {

constexpr int CELL_W = 12;  // 5x7 glyph + spacing, doubled
constexpr int CELL_H = 16;
constexpr int GLYPH_COLS = 5;
constexpr int GLYPH_ROWS = 8;
constexpr int MARGIN_X = (SCREEN_W - tt::COLS * CELL_W) / 2;

constexpr uint16_t PALETTE[8] = {
    RGB565(0, 0, 0),     RGB565(255, 0, 0),   RGB565(0, 255, 0),   RGB565(255, 255, 0),
    RGB565(0, 0, 255),   RGB565(255, 0, 255), RGB565(0, 255, 255), RGB565(255, 255, 255)};

uint16_t colorOf(tt::Color c) { return PALETTE[static_cast<uint8_t>(c)]; }

bool isDouble(const char* row) {
  tt::Cell cells[tt::COLS];
  return tt::decodeRow(row, cells);
}

// One pixel row of a text row: `glyphRow` is 0..7 of the 5x7 font.
void renderLine(const tt::Cell* cells, int glyphRow, uint16_t* px) {
  for (int x = 0; x < MARGIN_X; ++x) px[x] = colorOf(cells[0].bg);
  uint16_t* out = px + MARGIN_X;
  for (int col = 0; col < tt::COLS; ++col) {
    const tt::Cell& cell = cells[col];
    const uint16_t fg = colorOf(cell.fg);
    const uint16_t bg = colorOf(cell.bg);
    const uint8_t c = static_cast<uint8_t>(cell.c);
    for (int x = 0; x < CELL_W; ++x) {
      const int gx = x / 2;
      bool on;
      if (c == static_cast<uint8_t>(tt::BLOCK)) {
        on = true;
      } else {
        on = gx < GLYPH_COLS && ((font[c * GLYPH_COLS + gx] >> glyphRow) & 1);
      }
      *out++ = on ? fg : bg;
    }
  }
  for (int x = MARGIN_X + tt::COLS * CELL_W; x < SCREEN_W; ++x) px[x] = colorOf(cells[tt::COLS - 1].bg);
}

// A row `height` pixels tall (CELL_H, or 2 * CELL_H when double) at panel row y.
void drawRow(Arduino_GFX& gfx, const char* row, int y, int height, uint16_t* band, int bandRows) {
  tt::Cell cells[tt::COLS];
  tt::decodeRow(row, cells);
  const int pxPerGlyphRow = height / GLYPH_ROWS;
  for (int y0 = 0; y0 < height; y0 += bandRows) {
    for (int r = 0; r < bandRows; ++r) renderLine(cells, (y0 + r) / pxPerGlyphRow, band + r * SCREEN_W);
    gfx.draw16bitRGBBitmap(0, y + y0, band, SCREEN_W, bandRows);
  }
}

}  // namespace

void drawTeletext(Arduino_GFX& gfx, const tt::Page& page, tt::Page& drawn, bool force, uint16_t* band,
                  int bandRows) {
  bool repaintNext = false;  // a row that stopped being double height left its lower half behind
  for (int r = 0; r < tt::ROWS; ++r) {
    const char* row = page.rows[r];
    const bool doubled = r + 1 < tt::ROWS && isDouble(row);
    const bool dirty = force || repaintNext || strcmp(row, drawn.rows[r]) != 0;
    repaintNext = false;
    if (dirty) {
      repaintNext = !doubled && isDouble(drawn.rows[r]);
      drawRow(gfx, row, r * CELL_H, doubled ? 2 * CELL_H : CELL_H, band, bandRows);
      memcpy(drawn.rows[r], row, tt::ROW_BYTES);
    }
    if (doubled) {  // the row below is covered
      memcpy(drawn.rows[r + 1], page.rows[r + 1], tt::ROW_BYTES);
      ++r;
    }
  }
}

}  // namespace ui
