#pragma once

// Folder layout of the RETROTV microSD card, episode file naming, and the channels.json written
// when none exists. Pure header: tested on the host.

#include <stddef.h>
#include <string.h>
#include <strings.h>

namespace sdpath {

constexpr const char* ROOT = "/retrotv";
constexpr const char* CHANNELS_JSON = "/retrotv/config/channels.json";
constexpr const char* LOGOS = "/retrotv/logos";  // <channel id>.png, shown by the web remote
constexpr const char* WIFI_JSON = "/retrotv/config/wifi.json";
constexpr const char* DEMO_VIDEO = "/retrotv/media/demo/demo.mjpeg";  // tools/make_demo_clip.sh
constexpr const char* CHANNEL01_DIR = "/retrotv/media/channel01";
// Played at power-on when present (+ intro.aac for sound), instead of the start-up screens.
constexpr const char* INTRO_VIDEO = "/retrotv/system/intro.mjpeg";

// Created on boot when missing, parents first.
constexpr const char* LAYOUT_DIRS[] = {
    "/retrotv",
    "/retrotv/config",
    "/retrotv/media",
    "/retrotv/media/channel01",
    "/retrotv/media/channel02",
    "/retrotv/media/demo",
    "/retrotv/logos",
    "/retrotv/sounds",
    "/retrotv/system",
};

}  // namespace sdpath

constexpr const char* VIDEO_EXT = ".mjpeg";
constexpr const char* AUDIO_EXT = ".aac";
constexpr const char* INDEX_EXT = ".idx";   // tools/make_index.py
constexpr size_t MEDIA_PATH_MAX = 160;      // longest episode path the firmware handles

// An episode is "<name>.mjpeg" (any case). Hidden files are skipped, which covers the "._name"
// AppleDouble files macOS scatters over FAT cards.
inline bool isEpisodeFile(const char* name) {
  const size_t len = strlen(name);
  const size_t ext = strlen(VIDEO_EXT);
  return len > ext && name[0] != '.' && strcasecmp(name + len - ext, VIDEO_EXT) == 0;
}

// "/dir/name.mjpeg" -> "/dir/name<ext>" (the episode's audio or index). False if it does not fit.
inline bool siblingPathFor(const char* videoPath, const char* ext, char* out, size_t outLen) {
  const size_t len = strlen(videoPath);
  const size_t videoExt = strlen(VIDEO_EXT);
  if (len <= videoExt || strcasecmp(videoPath + len - videoExt, VIDEO_EXT) != 0) return false;
  const size_t stem = len - videoExt;
  if (stem + strlen(ext) + 1 > outLen) return false;
  memcpy(out, videoPath, stem);
  memcpy(out + stem, ext, strlen(ext) + 1);
  return true;
}

// Broadcast order for episode files. Separators ('_', '-', ' ', '.', '/') are ignored, letters
// compare case-insensitively and digit runs by numeric value, so episodes named in different
// styles still line up: "harlock_-01-" < "harlock_02", "hattori_-001" < "hattori_007",
// "ep_9" < "ep_10". Equal keys fall back to strcmp, so the order is always strict.
inline int compareEpisodeNames(const char* a, const char* b) {
  auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
  auto isAlnum = [&](char c) { return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
  const char* p = a;
  const char* q = b;
  for (;;) {
    while (*p != '\0' && !isAlnum(*p)) ++p;
    while (*q != '\0' && !isAlnum(*q)) ++q;
    if (*p == '\0' || *q == '\0') break;
    if (isDigit(*p) && isDigit(*q)) {  // numbers: by value, compared as digit strings (no overflow)
      while (*p == '0') ++p;
      while (*q == '0') ++q;
      const char* ps = p;
      const char* qs = q;
      while (isDigit(*p)) ++p;
      while (isDigit(*q)) ++q;
      if (p - ps != q - qs) return (p - ps) < (q - qs) ? -1 : 1;
      const int c = strncmp(ps, qs, static_cast<size_t>(p - ps));
      if (c != 0) return c;
      continue;
    }
    const char cp = lower(*p);
    const char cq = lower(*q);
    if (cp != cq) return cp < cq ? -1 : 1;
    ++p;
    ++q;
  }
  if (*p != '\0' || *q != '\0') return *p != '\0' ? 1 : -1;  // the shorter key first
  return strcmp(a, b);
}

inline bool audioPathFor(const char* videoPath, char* out, size_t outLen) {
  return siblingPathFor(videoPath, AUDIO_EXT, out, outLen);
}

inline bool indexPathFor(const char* videoPath, char* out, size_t outLen) {
  return siblingPathFor(videoPath, INDEX_EXT, out, outLen);
}

// Same content as data/example-config/channels.json.
constexpr const char* DEFAULT_CHANNELS_JSON = R"json({
  "channels": [
    { "id": "demo", "number": 1, "name": "RETROTV DEMO", "type": "local",
      "source": "/retrotv/media/demo/demo.mjpeg", "enabled": true },
    { "id": "channel01", "number": 2, "name": "CANAL 1", "type": "local",
      "source": "/retrotv/media/channel01", "enabled": true },
    { "id": "channel02", "number": 3, "name": "CANAL 2", "type": "local",
      "source": "/retrotv/media/channel02", "enabled": true },
    { "id": "teletext", "number": 8, "name": "TELETEXT", "type": "internal",
      "source": "teletext", "enabled": true },
    { "id": "testcard", "number": 9, "name": "CARTA DE AJUSTE", "type": "internal",
      "source": "testcard", "enabled": true },
    { "id": "remote", "number": 10, "name": "RETROTV REMOTE", "type": "remote",
      "source": "http://retrotv-server.local:8080/channel/1", "enabled": false }
  ]
}
)json";
