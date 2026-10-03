#pragma once

// The web remote's settings, pure C++ (tested on the host): pairing a phone with a code shown on
// the TV, reading request bodies, and editing wifi.json and channels.json. The HTTP side is
// src/web/WebRemote; the edits run in the App's loop.

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "network/WifiNetworks.h"

constexpr uint32_t PAIR_CODE_MS = 60 * 1000;        // the code on the TV screen lasts this long
constexpr uint32_t PAIR_TOKEN_MS = 30 * 60 * 1000;  // a paired phone, since its last request
constexpr uint8_t PAIR_MAX_TRIES = 5;               // wrong codes before a new one is needed
constexpr size_t PAIR_TOKEN_LEN = 32;               // hex characters

// One paired phone at a time; pairing another one replaces it. Not thread-safe: the HTTP
// server calls it from its single task.
class PairingGate {
 public:
  using Random = uint32_t (*)();
  explicit PairingGate(Random random) : random_(random) {}

  uint16_t start(uint32_t nowMs);  // a new 4-digit code for the TV to show (1000-9999)
  bool codeActive(uint32_t nowMs) const;
  // The code the phone typed; on success writes the session token (PAIR_TOKEN_LEN + 1 bytes).
  bool verify(const char* code, uint32_t nowMs, char* token, size_t tokenCap);
  bool allowed(const char* token, uint32_t nowMs);  // extends the session on success

 private:
  Random random_;
  uint16_t code_ = 0;
  uint32_t codeAtMs_ = 0;
  uint8_t tries_ = 0;
  char token_[PAIR_TOKEN_LEN + 1] = "";
  uint32_t tokenAtMs_ = 0;
};

// Request bodies (small JSON). All validate their values.
bool parseWifiAdd(const char* body, size_t len, char* ssid, char* password);  // 33 / 65 bytes
bool parseSsid(const char* body, size_t len, char* ssid);
bool parseDisplay(const char* body, size_t len, int& brightness, int& volume);  // -1 = not given
bool parseChannelToggle(const char* body, size_t len, uint16_t& number, bool& enabled);
bool parseCode(const char* body, size_t len, char* code, size_t cap);

// AJUSTES > VOZ from the phone (voice builds). A change carries only what the phone sent: -1 = not
// given. "standby" is "voice" (STANDBY VOZ) or "deep" (AHORRO MAX). No recorder here, on purpose:
// the remote has no PIN, and anyone on the Wi-Fi could record the room.
struct VoiceChange {
  int8_t mic = -1;
  int8_t claps = -1;
  int sensitivity = -1;  // 0-100
  int8_t standbyVoice = -1;
  int8_t led = -1;
};
bool parseVoice(const char* body, size_t len, VoiceChange& change);  // false if empty or invalid
struct VoiceConfig {
  bool available = false;     // a voice build with a working microphone
  bool mic = false;
  bool claps = false;
  uint8_t sensitivity = 0;
  bool standbyVoice = false;  // APAGADO = STANDBY VOZ
  bool standbyFixed = false;  // button-less: always STANDBY VOZ, the phone cannot change it
  bool led = false;
};
size_t writeVoiceJson(const VoiceConfig& config, char* out, size_t cap);

// wifi.json: always written in the {"networks": [...]} format; the same SSID replaces its
// password; at most WIFI_MAX_NETWORKS.
bool wifiJsonAdd(JsonDocument& doc, const char* ssid, const char* password, char* error, size_t errorCap);
bool wifiJsonRemove(JsonDocument& doc, const char* ssid);
size_t writeWifiJson(const JsonDocument& doc, char* out, size_t cap);
// For the phone: SSID, where it comes from, whether it is the one in use. Never a password.
size_t writeWifiListJson(const WifiNetworks& networks, const char* connectedSsid, char* out, size_t cap);

// channels.json: flips "enabled" of one channel, every other field untouched; written back one
// channel per line, like the hand-written file. Writers return 0 when the output does not fit.
bool channelsJsonSetEnabled(JsonDocument& doc, uint16_t number, bool enabled);
size_t writeChannelsFile(const JsonDocument& doc, char* out, size_t cap);
