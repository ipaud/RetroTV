#pragma once

// Splits a video block into the parts that are not under the OSD, so DisplayTask never paints
// over it. Pure C++: tested pixel by pixel on the host.

struct ClipRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
};

// Calls fn(span, offset) for every rectangle of `block` outside `overlay`. `offset` is the index
// of the span's first pixel in the block's pixel array (rows are block.w apart). Spans taller
// than one row always span the whole block width, so their pixels are contiguous.
template <typename Fn>
void forEachVisibleSpan(const ClipRect& block, const ClipRect& overlay, Fn&& fn) {
  const int bx1 = block.x + block.w;
  const int by1 = block.y + block.h;
  const int ox1 = overlay.x + overlay.w;
  const int oy1 = overlay.y + overlay.h;
  if (overlay.empty() || overlay.x >= bx1 || ox1 <= block.x || overlay.y >= by1 || oy1 <= block.y) {
    fn(block, 0);
    return;
  }

  const int top = overlay.y > block.y ? overlay.y : block.y;
  const int bottom = oy1 < by1 ? oy1 : by1;
  const int left = overlay.x > block.x ? overlay.x : block.x;
  const int right = ox1 < bx1 ? ox1 : bx1;

  if (top > block.y) fn(ClipRect{block.x, block.y, block.w, top - block.y}, 0);
  for (int y = top; y < bottom; ++y) {  // rows beside the OSD: left and right pieces
    const int row = (y - block.y) * block.w;
    if (left > block.x) fn(ClipRect{block.x, y, left - block.x, 1}, row);
    if (right < bx1) fn(ClipRect{right, y, bx1 - right, 1}, row + (right - block.x));
  }
  if (bottom < by1) fn(ClipRect{block.x, bottom, block.w, by1 - bottom}, (bottom - block.y) * block.w);
}
