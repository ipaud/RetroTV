// Host tests: episode index (.idx) parsing and the on-air schedule position.

#include <cstring>
#include <vector>

#include "check.h"
#include "media/EpisodeIndex.h"
#include "media/OnAir.h"

namespace {

void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
  b[at] = v & 0xFF;
  b[at + 1] = v >> 8;
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = (v >> (8 * i)) & 0xFF;
}

// A header as tools/make_index.py writes it.
std::vector<uint8_t> header(uint16_t fps, uint32_t frames, uint32_t durationMs, uint16_t interval,
                            uint32_t entries) {
  std::vector<uint8_t> b(INDEX_HEADER_SIZE, 0);
  memcpy(b.data(), INDEX_MAGIC, 8);
  put16(b, 8, INDEX_VERSION);
  put16(b, 10, fps);
  put32(b, 12, frames);
  put32(b, 16, durationMs);
  put16(b, 20, interval);
  put32(b, 24, entries);
  put32(b, 28, 44100);
  return b;
}

void testIndexHeader() {
  IndexHeader h;
  auto good = header(24, 33390, 1391250, 24, 1392);
  CHECK(parseIndexHeader(good.data(), good.size(), h));
  CHECK(h.fps == 24 && h.frameCount == 33390 && h.durationMs == 1391250 && h.interval == 24 &&
        h.entries == 1392 && h.sampleRate == 44100);

  auto magic = good;
  magic[0] = 'X';
  CHECK(!parseIndexHeader(magic.data(), magic.size(), h));
  auto version = good;
  put16(version, 8, 99);
  CHECK(!parseIndexHeader(version.data(), version.size(), h));
  auto noFps = header(0, 10, 1000, 24, 1);
  CHECK(!parseIndexHeader(noFps.data(), noFps.size(), h));
  auto noEntries = header(24, 10, 1000, 24, 0);
  CHECK(!parseIndexHeader(noEntries.data(), noEntries.size(), h));
  CHECK(!parseIndexHeader(good.data(), INDEX_HEADER_SIZE - 1, h));  // short read
}

void testEntryLookup() {
  IndexHeader h;
  auto b = header(24, 240, 10000, 24, 10);  // 10 s, one entry per second
  CHECK(parseIndexHeader(b.data(), b.size(), h));
  CHECK(entryFor(h, 0) == 0);
  CHECK(entryFor(h, 999) == 0);   // the entry at or before the position
  CHECK(entryFor(h, 1000) == 1);
  CHECK(entryFor(h, 9999) == 9);
  CHECK(entryFor(h, 60000) == 9);  // past the end: the last entry
  CHECK(entryTimeMs(h, 3) == 3000);
  CHECK(entryOffset(3) == INDEX_HEADER_SIZE + 3 * INDEX_ENTRY_SIZE);

  uint8_t raw[INDEX_ENTRY_SIZE];
  std::vector<uint8_t> e(INDEX_ENTRY_SIZE);
  put32(e, 0, 123456);
  put32(e, 4, 7890);
  memcpy(raw, e.data(), sizeof(raw));
  IndexEntry entry = parseIndexEntry(raw);
  CHECK(entry.videoOffset == 123456 && entry.audioOffset == 7890);
}

void testOnAirSlot() {
  const uint32_t d[] = {1000, 2000, 3000};  // a 6 s programme
  OnAirSlot s = onAirSlot(0, d, 3);
  CHECK(s.episode == 0 && s.offsetMs == 0);
  s = onAirSlot(999, d, 3);
  CHECK(s.episode == 0 && s.offsetMs == 999);
  s = onAirSlot(1000, d, 3);  // exactly at a boundary: the next episode from its start
  CHECK(s.episode == 1 && s.offsetMs == 0);
  s = onAirSlot(5999, d, 3);
  CHECK(s.episode == 2 && s.offsetMs == 2999);
  s = onAirSlot(6000 * 7 + 1500, d, 3);  // loops forever
  CHECK(s.episode == 1 && s.offsetMs == 500);

  // Real epoch in ms (2026) with 24 x 23 min episodes: no overflow, lands inside.
  std::vector<uint32_t> season(24, 1391250);
  s = onAirSlot(1790000000000ULL, season.data(), season.size());
  CHECK(s.episode < 24 && s.offsetMs < 1391250);

  const uint32_t withEmpty[] = {0, 5000, 0};  // zero-length episodes are never on air
  s = onAirSlot(123456, withEmpty, 3);
  CHECK(s.episode == 1 && s.offsetMs == 123456 % 5000);

  const uint32_t none[] = {0, 0};
  s = onAirSlot(42, none, 2);
  CHECK(s.episode == 0 && s.offsetMs == 0);  // nothing to schedule: start of the first
  CHECK(programmeLengthMs(d, 3) == 6000);
}

// An index gone stale (episode re-converted, not re-indexed) points into the middle of frames.
void testEntryValidation() {
  const uint8_t jpeg[] = {0xFF, 0xD8, 0xFF};
  const uint8_t midFrame[] = {0x12, 0xFF, 0x00};
  const uint8_t adts[] = {0xFF, 0xF1, 0x50};
  const uint8_t notAdts[] = {0xFF, 0x00, 0x50};
  const IndexEntry withAudio{1000, 200};
  const IndexEntry silent{1000, INDEX_NO_AUDIO};

  CHECK(entryMatches(jpeg, adts, withAudio));
  CHECK(!entryMatches(midFrame, adts, withAudio));  // stale video offset
  CHECK(!entryMatches(jpeg, notAdts, withAudio));   // stale audio offset
  CHECK(entryMatches(jpeg, nullptr, withAudio));    // episode without .aac: video decides
  CHECK(entryMatches(jpeg, nullptr, silent));
  CHECK(!entryMatches(jpeg, adts, silent));  // an .aac appeared after indexing: re-index first
}

void testNextOnAirSlot() {
  const uint32_t d[] = {1000, 2000, 3000};
  OnAirSlot s = nextOnAirSlot(true, 0, 999999, d, 3);  // episode 1 ended: episode 2 from the start
  CHECK(s.episode == 1 && s.offsetMs == 0);
  s = nextOnAirSlot(true, 2, 999999, d, 3);  // the last one ended: back to the first
  CHECK(s.episode == 0 && s.offsetMs == 0);
  s = nextOnAirSlot(false, 2, 1500, d, 3);  // tuning in: wherever the programme is now
  CHECK(s.episode == 1 && s.offsetMs == 500);
  s = nextOnAirSlot(true, 7, 0, d, 0);  // empty programme never divides by zero
  CHECK(s.episode == 0 && s.offsetMs == 0);
}

}  // namespace

void runOnAirTests() {
  testIndexHeader();
  testEntryLookup();
  testOnAirSlot();
  testEntryValidation();
  testNextOnAirSlot();
}
