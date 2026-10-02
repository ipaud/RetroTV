#pragma once

// Channel list from /retrotv/config/channels.json: validation, order, zapping.
// Pure C++ + ArduinoJson (no Arduino core): tested on the host. Fixed-size storage, so
// loading never fragments the heap and zapping allocates nothing.
//
//   { "channels": [ { "id", "number", "name", "type", "source", "enabled" } ] }
//
// type "local": source is an .mjpeg file (audio = same name .aac) or a folder of episodes.
// type "internal": source "testcard" or "teletext" (guide pages). Types remote, hls-proxy, tunarr, stream are recognised
// for V0.2 but not played yet (NO SIGNAL).

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

enum class ChannelType : uint8_t { Local, Internal, Remote, HlsProxy, Tunarr, Stream };

constexpr size_t MAX_CHANNELS = 48;
constexpr size_t CHANNEL_ID_LEN = 24;
constexpr size_t CHANNEL_NAME_LEN = 24;
constexpr size_t CHANNEL_SOURCE_LEN = 128;
constexpr int CHANNEL_NUMBER_MAX = 999;  // 0 is a channel too (a tutorial before 1)
constexpr const char* INTERNAL_TESTCARD = "testcard";
constexpr const char* INTERNAL_TELETEXT = "teletext";
constexpr const char* INTERNAL_REMOTE_QR = "mando";  // a QR code that opens the web remote

struct Channel {
  char id[CHANNEL_ID_LEN];
  uint16_t number;
  char name[CHANNEL_NAME_LEN];  // uppercase ASCII, ready for the GLCD font
  ChannelType type;
  char source[CHANNEL_SOURCE_LEN];
  bool enabled;
};

struct LoadReport {
  uint8_t loaded = 0;
  uint8_t skipped = 0;
  char firstProblem[64] = "";  // "#3: unknown type", for the log and the error screen
};

const char* channelTypeName(ChannelType t);
inline bool isImplemented(ChannelType t) { return t == ChannelType::Local || t == ChannelType::Internal; }

// UTF-8 -> uppercase ASCII for the GLCD font, which has no accents: A with any accent -> A,
// N tilde -> N, C cedilla -> C, "l·l" -> "L.L", curly apostrophe -> ', anything else -> '?'.
// Always NUL-terminated, never longer than outLen - 1.
void toOsdText(const char* utf8, char* out, size_t outLen);

class ChannelManager {
 public:
  // Invalid entries are skipped and counted in the report; false when no enabled channel is
  // left (the caller then uses loadFallback()).
  bool load(JsonVariantConst root, LoadReport& report);
  // A single internal TEST CARD channel: no SD card, or a channels.json that cannot be used.
  void loadFallback();

  size_t count() const { return count_; }
  size_t enabledCount() const;
  const Channel& at(size_t i) const { return channels_[i]; }
  const Channel* current() const { return current_ >= 0 ? &channels_[current_] : nullptr; }
  int currentIndex() const { return current_; }  // -1 when none; stable for the whole boot

  // Enabled channel with that number, or null (the current channel does not change).
  const Channel* select(uint16_t number);
  // Web remote settings: zapping skips a disabled channel; the one on screen keeps playing.
  bool setEnabled(uint16_t number, bool enabled);
  // The remembered channel, or the first enabled one if it is gone.
  const Channel* selectOrFirst(uint16_t number);
  // Next / previous enabled channel by number, wrapping around.
  const Channel* next() { return step(1); }
  const Channel* prev() { return step(-1); }

 private:
  const Channel* step(int direction);
  int indexOf(uint16_t number) const;

  Channel channels_[MAX_CHANNELS];
  size_t count_ = 0;
  int current_ = -1;
};
