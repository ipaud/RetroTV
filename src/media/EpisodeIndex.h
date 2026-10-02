#pragma once

// "<name>.idx": where each second of an episode starts in its .mjpeg and .aac, written by
// tools/make_index.py. Fixed-size little-endian records, so the board reads one entry with a
// seek instead of parsing the file. Pure C++: tested on the host.
//
//   0  char[8]  "PAUTVIDX"         20 u16 interval (frames per entry)
//   8  u16      version (1)        22 u16 reserved
//   10 u16      fps                24 u32 entry count
//   12 u32      frame count        28 u32 audio sample rate
//   16 u32      duration (ms)      32 + 8*k: u32 video offset, u32 audio offset (entry k)

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr char INDEX_MAGIC[] = "PAUTVIDX";  // 8 bytes on disk, no terminator
constexpr uint16_t INDEX_VERSION = 1;
constexpr size_t INDEX_HEADER_SIZE = 32;
constexpr size_t INDEX_ENTRY_SIZE = 8;
constexpr uint32_t INDEX_NO_AUDIO = 0xFFFFFFFFu;  // audio offset when the episode has no .aac

struct IndexHeader {
  uint16_t fps = 0;
  uint32_t frameCount = 0;
  uint32_t durationMs = 0;
  uint16_t interval = 0;
  uint32_t entries = 0;
  uint32_t sampleRate = 0;
};

struct IndexEntry {
  uint32_t videoOffset;
  uint32_t audioOffset;
};

namespace indexdetail {
inline uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace indexdetail

// False for anything this firmware cannot trust: wrong magic or version, zero fps, interval
// or entries, or a short read.
inline bool parseIndexHeader(const uint8_t* buf, size_t len, IndexHeader& out) {
  using namespace indexdetail;
  if (len < INDEX_HEADER_SIZE || memcmp(buf, INDEX_MAGIC, 8) != 0) return false;
  if (u16(buf + 8) != INDEX_VERSION) return false;
  IndexHeader h;
  h.fps = u16(buf + 10);
  h.frameCount = u32(buf + 12);
  h.durationMs = u32(buf + 16);
  h.interval = u16(buf + 20);
  h.entries = u32(buf + 24);
  h.sampleRate = u32(buf + 28);
  if (h.fps == 0 || h.interval == 0 || h.entries == 0) return false;
  out = h;
  return true;
}

// The entry at or before positionMs (the last one when past the end).
inline uint32_t entryFor(const IndexHeader& h, uint32_t positionMs) {
  const uint64_t frame = static_cast<uint64_t>(positionMs) * h.fps / 1000;
  const uint64_t k = frame / h.interval;
  return static_cast<uint32_t>(k < h.entries ? k : h.entries - 1);
}

inline uint32_t entryTimeMs(const IndexHeader& h, uint32_t k) {
  return static_cast<uint32_t>(static_cast<uint64_t>(k) * h.interval * 1000 / h.fps);
}

inline size_t entryOffset(uint32_t k) { return INDEX_HEADER_SIZE + static_cast<size_t>(k) * INDEX_ENTRY_SIZE; }

inline IndexEntry parseIndexEntry(const uint8_t* buf) {
  return IndexEntry{indexdetail::u32(buf), indexdetail::u32(buf + 4)};
}

// First bytes of a JPEG (FF D8 FF) and of an ADTS frame (0xFFF sync, layer 0).
inline bool isJpegStart(const uint8_t* b) { return b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF; }
inline bool isAdtsStart(const uint8_t* b) { return b[0] == 0xFF && (b[1] & 0xF6) == 0xF0; }

// Whether the bytes read at an entry's offsets are what the index promises (3 bytes each).
// audioHead is null when the episode has no .aac open. False means the index is stale: the
// episode was re-converted without re-indexing, or an .aac appeared after indexing.
inline bool entryMatches(const uint8_t* videoHead, const uint8_t* audioHead, const IndexEntry& e) {
  if (!isJpegStart(videoHead)) return false;
  if (audioHead == nullptr) return true;
  return e.audioOffset != INDEX_NO_AUDIO && isAdtsStart(audioHead);
}
