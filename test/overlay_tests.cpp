// Host tests: video blocks drawn around the OSD, never under it.

#include <vector>

#include "check.h"
#include "display/BlockClip.h"

namespace {

// Paints every span of a block onto a canvas, then checks each pixel.
bool clipIsExact(ClipRect block, ClipRect overlay) {
  constexpr int W = 64, H = 48;
  std::vector<int> canvas(W * H, 0);
  std::vector<int> used(static_cast<size_t>(block.w * block.h), 0);
  forEachVisibleSpan(block, overlay, [&](const ClipRect& span, int offset) {
    for (int r = 0; r < span.h; ++r) {
      for (int c = 0; c < span.w; ++c) {
        ++canvas[(span.y + r) * W + span.x + c];
        ++used[static_cast<size_t>(offset + r * block.w + c)];  // pixel read from the block
      }
    }
  });
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const bool inBlock = x >= block.x && x < block.x + block.w && y >= block.y && y < block.y + block.h;
      const bool inOverlay = !overlay.empty() && x >= overlay.x && x < overlay.x + overlay.w &&
                             y >= overlay.y && y < overlay.y + overlay.h;
      const int expected = inBlock && !inOverlay ? 1 : 0;
      if (canvas[y * W + x] != expected) return false;
      // The block pixel painted here must be the one at the same position.
      if (expected && used[static_cast<size_t>((y - block.y) * block.w + (x - block.x))] != 1) return false;
    }
  }
  return true;
}

}  // namespace

void runOverlayTests() {
  const ClipRect block{8, 16, 32, 16};
  CHECK(clipIsExact(block, ClipRect{}));                  // no OSD: whole block
  CHECK(clipIsExact(block, ClipRect{50, 0, 10, 10}));     // no overlap
  CHECK(clipIsExact(block, ClipRect{20, 20, 8, 4}));      // hole in the middle
  CHECK(clipIsExact(block, ClipRect{30, 10, 30, 30}));    // covers the right edge
  CHECK(clipIsExact(block, ClipRect{0, 0, 64, 20}));      // covers the top rows
  CHECK(clipIsExact(block, ClipRect{0, 0, 64, 48}));      // covers everything: nothing drawn
  CHECK(clipIsExact(block, ClipRect{0, 24, 12, 30}));     // bottom-left corner
}
