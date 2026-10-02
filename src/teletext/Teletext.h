#pragma once

// Teletext channel: a page of 26 x 15 character cells (12 x 16 px on the 320 x 240 panel), the
// order its pages come in, and the episode titles it prints. Pure C++: tested on the host.
//
// A row is a C string. Printable ASCII takes one cell each; the control bytes below take none
// and change what follows, like teletext's colour codes. Every row starts white on black.

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace tt {

constexpr int COLS = 26;
constexpr int ROWS = 15;
constexpr size_t ROW_BYTES = 64;  // cells + control bytes + NUL
constexpr int NOW_PER_PAGE = 5;   // channels per "ara en emissio" subpage, two rows each

constexpr uint16_t PAGE_INDEX = 100;
constexpr uint16_t PAGE_NOW = 101;
constexpr uint16_t PAGE_CHANNEL_BASE = 200;  // P2NN: guide of channel NN

enum class Color : uint8_t { Black, Red, Green, Yellow, Blue, Magenta, Cyan, White };

constexpr uint8_t CODE_FG = 0x10;      // 0x10..0x17: foreground colour
constexpr uint8_t CODE_BG = 0x18;      // 0x18..0x1F: background colour, to the end of the row
constexpr uint8_t CODE_DOUBLE = 0x0E;  // double height: the row also covers the one below
constexpr char BLOCK = 0x7F;           // a solid cell in the foreground colour

struct Page {
  char rows[ROWS][ROW_BYTES];
};

struct Cell {
  char c;
  Color fg;
  Color bg;
};

// Appends to one row, never past COLS cells or ROW_BYTES bytes.
class Row {
 public:
  explicit Row(char* buf) : buf_(buf) { buf_[0] = '\0'; }

  Row& fg(Color c) { return code(static_cast<char>(CODE_FG + static_cast<uint8_t>(c))); }
  Row& bg(Color c) { return code(static_cast<char>(CODE_BG + static_cast<uint8_t>(c))); }
  Row& doubleHeight() { return code(static_cast<char>(CODE_DOUBLE)); }

  // At most maxCols cells of `s`; anything but printable ASCII shows as '?'.
  Row& text(const char* s, int maxCols = COLS) {
    for (int n = 0; *s != '\0' && n < maxCols; ++s, ++n) {
      const char c = (*s >= 0x20 && *s < 0x7F) ? *s : '?';
      if (!cell(c)) break;
    }
    return *this;
  }
  Row& repeat(char c, int n) {
    for (int i = 0; i < n && cell(c); ++i) {
    }
    return *this;
  }
  Row& padTo(int col) { return repeat(' ', col - cols_); }
  Row& right(const char* s) { return padTo(COLS - static_cast<int>(strlen(s))).text(s); }
  int cols() const { return cols_; }

 private:
  Row& code(char c) {
    if (len_ + 1 < ROW_BYTES) append(c);
    return *this;
  }
  bool cell(char c) {
    if (cols_ >= COLS || len_ + 1 >= ROW_BYTES) return false;
    append(c);
    ++cols_;
    return true;
  }
  void append(char c) {
    buf_[len_++] = c;
    buf_[len_] = '\0';
  }

  char* buf_;
  size_t len_ = 0;
  int cols_ = 0;
};

// Row string -> COLS cells; the colours in force at its end fill the rest. True if double height.
inline bool decodeRow(const char* row, Cell* cells) {
  Color fg = Color::White;
  Color bg = Color::Black;
  bool doubled = false;
  int col = 0;
  for (const char* p = row; *p != '\0' && col < COLS; ++p) {
    const uint8_t b = static_cast<uint8_t>(*p);
    if (b == CODE_DOUBLE) {
      doubled = true;
    } else if (b >= CODE_FG && b < CODE_FG + 8) {
      fg = static_cast<Color>(b - CODE_FG);
    } else if (b >= CODE_BG && b < CODE_BG + 8) {
      bg = static_cast<Color>(b - CODE_BG);
    } else if (b >= 0x20) {
      cells[col++] = Cell{static_cast<char>(b), fg, bg};
    }
  }
  for (; col < COLS; ++col) cells[col] = Cell{' ', fg, bg};
  return doubled;
}

// Time left, rounded up to the minute: "12'", "1H05'".
inline void minutesLabel(uint64_t ms, char* out, size_t len) {
  const unsigned minutes = static_cast<unsigned>((ms + 59999) / 60000);
  if (minutes >= 60) {
    snprintf(out, len, "%uH%02u'", minutes / 60, minutes % 60);
  } else {
    snprintf(out, len, "%u'", minutes);
  }
}

// Uploader and site names depend on each collection: list yours in include/title_tags.h (git-ignored),
// e.g. `#define LOCAL_TITLE_TAGS "someone", "somesite"`.
#if __has_include("title_tags.h")
#include "title_tags.h"
#endif
#ifndef LOCAL_TITLE_TAGS
#define LOCAL_TITLE_TAGS
#endif

// ponytail: generic rip tags plus the local ones; extend when new ones show up.
inline bool isRipTag(const char* word, size_t n) {
  static const char* const TAGS[] = {"per", "by", "dvdrip", "dvbrip", "tdtrip", "cat", LOCAL_TITLE_TAGS};
  for (const char* t : TAGS) {
    if (strlen(t) == n && strncasecmp(word, t, n) == 0) return true;
  }
  return false;
}

// "/retrotv/media/x/la_serie_-02-_el_gos_del_carrer_per_algu.mjpeg"
//   -> "02 EL GOS DEL CARRER"
// The first number is the episode, the words after it are the title, up to the first rip tag.
// Without a number: the file name. Uppercase ASCII, one space between words.
inline void episodeTitle(const char* path, char* out, size_t len) {
  if (len == 0) return;
  const char* name = strrchr(path, '/');
  name = name != nullptr ? name + 1 : path;
  const char* dot = strrchr(name, '.');
  const char* end = dot != nullptr && dot > name ? dot : name + strlen(name);
  auto digit = [](char c) { return isdigit(static_cast<unsigned char>(c)) != 0; };
  auto alnum = [](char c) { return isalnum(static_cast<unsigned char>(c)) != 0; };

  size_t o = 0;
  auto put = [&](char c) {
    if (o + 1 < len) out[o++] = c;
  };
  const char* p = name;
  while (p < end && !digit(*p)) ++p;
  if (p < end) {
    unsigned number = 0;
    for (; p < end && digit(*p); ++p) {
      if (number < 100000) number = number * 10 + static_cast<unsigned>(*p - '0');
    }
    char digits[12];
    snprintf(digits, sizeof(digits), "%02u", number);
    for (const char* d = digits; *d != '\0'; ++d) put(*d);
  } else {
    p = name;
  }
  for (;;) {
    while (p < end && !alnum(*p)) ++p;
    if (p >= end) break;
    const char* word = p;
    while (p < end && alnum(*p)) ++p;
    if (isRipTag(word, static_cast<size_t>(p - word))) break;
    if (o > 0) put(' ');
    for (const char* c = word; c < p; ++c) put(static_cast<char>(toupper(static_cast<unsigned char>(*c))));
  }
  out[o] = '\0';
}

// The pages in the order they turn: P100 index, P101 "ara en emissio" (one subpage per
// NOW_PER_PAGE channels), then P2NN for every guide channel.
struct View {
  uint16_t page;
  uint8_t sub;   // 1-based
  uint8_t subs;
  int channel;   // position in the guide list for P2NN, else -1
};

inline int nowSubpages(size_t channels) {
  return channels == 0 ? 1 : static_cast<int>((channels + NOW_PER_PAGE - 1) / NOW_PER_PAGE);
}

inline size_t viewCount(size_t channels) { return 1 + static_cast<size_t>(nowSubpages(channels)) + channels; }

inline View viewAt(size_t i, const uint16_t* numbers, size_t channels) {
  const int subs = nowSubpages(channels);
  i %= viewCount(channels);
  if (i == 0) return View{PAGE_INDEX, 1, 1, -1};
  if (i <= static_cast<size_t>(subs)) return View{PAGE_NOW, static_cast<uint8_t>(i), static_cast<uint8_t>(subs), -1};
  const size_t c = i - 1 - static_cast<size_t>(subs);
  return View{static_cast<uint16_t>(PAGE_CHANNEL_BASE + numbers[c]), 1, 1, static_cast<int>(c)};
}

// First view showing `page`, or -1.
inline int viewForPage(uint16_t page, const uint16_t* numbers, size_t channels) {
  for (size_t i = 0; i < viewCount(channels); ++i) {
    if (viewAt(i, numbers, channels).page == page) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace tt
