#include "web/ConfigApi.h"

#include "channels/ChannelManager.h"

#include <stdio.h>
#include <string.h>

#include <utility>

namespace {

constexpr int BRIGHTNESS_MIN = 10;
constexpr size_t CODE_DIGITS = 4;

// Appends to a fixed buffer; once something does not fit, the whole write fails (returns 0).
struct Out {
  char* p;
  size_t cap;
  size_t len = 0;
  bool ok = true;

  void text(const char* s) {
    const size_t n = strlen(s);
    if (!ok || len + n + 1 > cap) {
      ok = false;
      return;
    }
    memcpy(p + len, s, n + 1);
    len += n;
  }
  void json(JsonVariantConst v) {
    const size_t n = measureJson(v);
    if (!ok || len + n + 1 > cap) {
      ok = false;
      return;
    }
    serializeJson(v, p + len, cap - len);
    len += n;
  }
  size_t done() const { return ok ? len : 0; }
};

bool parse(const char* body, size_t len, JsonDocument& doc) {
  return body != nullptr && !deserializeJson(doc, body, len) && doc.is<JsonObject>();
}

bool digitsOnly(const char* s, size_t n) {
  if (s == nullptr || strlen(s) != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

}  // namespace

uint16_t PairingGate::start(uint32_t nowMs) {
  code_ = static_cast<uint16_t>(1000 + random_() % 9000);
  codeAtMs_ = nowMs;
  tries_ = 0;
  return code_;
}

bool PairingGate::codeActive(uint32_t nowMs) const { return code_ != 0 && nowMs - codeAtMs_ < PAIR_CODE_MS; }

bool PairingGate::verify(const char* code, uint32_t nowMs, char* token, size_t tokenCap) {
  if (!codeActive(nowMs) || tries_ >= PAIR_MAX_TRIES || !digitsOnly(code, CODE_DIGITS) ||
      tokenCap < PAIR_TOKEN_LEN + 1) {
    return false;
  }
  ++tries_;
  if (static_cast<uint16_t>(atoi(code)) != code_) return false;
  code_ = 0;  // a code works once
  for (size_t i = 0; i < PAIR_TOKEN_LEN / 8; ++i) snprintf(token_ + i * 8, 9, "%08lx", static_cast<unsigned long>(random_()));
  tokenAtMs_ = nowMs;
  memcpy(token, token_, PAIR_TOKEN_LEN + 1);
  return true;
}

bool PairingGate::allowed(const char* token, uint32_t nowMs) {
  if (token == nullptr || token_[0] == '\0' || strlen(token) != PAIR_TOKEN_LEN || nowMs - tokenAtMs_ >= PAIR_TOKEN_MS) {
    return false;
  }
  uint8_t diff = 0;  // the same time whatever matches: no hint from how long a wrong token takes
  for (size_t i = 0; i < PAIR_TOKEN_LEN; ++i) diff |= static_cast<uint8_t>(token[i] ^ token_[i]);
  if (diff != 0) return false;
  tokenAtMs_ = nowMs;
  return true;
}

bool parseWifiAdd(const char* body, size_t len, char* ssid, char* password) {
  JsonDocument doc;
  if (!parse(body, len, doc) || !doc["ssid"].is<const char*>()) return false;
  const char* s = doc["ssid"];
  const char* p = doc["password"] | "";
  if (!validWifiCredentials(s, p)) return false;
  snprintf(ssid, 33, "%s", s);
  snprintf(password, 65, "%s", p);
  return true;
}

bool parseSsid(const char* body, size_t len, char* ssid) {
  JsonDocument doc;
  if (!parse(body, len, doc) || !doc["ssid"].is<const char*>()) return false;
  const char* s = doc["ssid"];
  if (strlen(s) == 0 || strlen(s) > 32) return false;
  snprintf(ssid, 33, "%s", s);
  return true;
}

bool parseDisplay(const char* body, size_t len, int& brightness, int& volume) {
  JsonDocument doc;
  if (!parse(body, len, doc)) return false;
  brightness = doc["brightness"].is<int>() ? doc["brightness"].as<int>() : -1;
  volume = doc["volume"].is<int>() ? doc["volume"].as<int>() : -1;
  if (brightness != -1 && (brightness < BRIGHTNESS_MIN || brightness > 100)) return false;
  if (volume != -1 && (volume < 0 || volume > 100)) return false;
  return brightness != -1 || volume != -1;
}

bool parseVoice(const char* body, size_t len, VoiceChange& change) {
  JsonDocument doc;
  if (!parse(body, len, doc)) return false;
  change = VoiceChange{};
  bool any = false;
  for (const auto& [key, field] : {std::pair<const char*, int8_t*>{"mic", &change.mic}, {"claps", &change.claps},
                                   {"led", &change.led}}) {
    if (doc[key].isNull()) continue;
    if (!doc[key].is<bool>()) return false;
    *field = doc[key].as<bool>() ? 1 : 0;
    any = true;
  }
  if (!doc["sensitivity"].isNull()) {
    if (!doc["sensitivity"].is<int>()) return false;
    change.sensitivity = doc["sensitivity"];
    if (change.sensitivity < 0 || change.sensitivity > 100) return false;
    any = true;
  }
  if (!doc["standby"].isNull()) {
    const char* s = doc["standby"].is<const char*>() ? doc["standby"].as<const char*>() : "";
    if (strcmp(s, "voice") != 0 && strcmp(s, "deep") != 0) return false;
    change.standbyVoice = strcmp(s, "voice") == 0 ? 1 : 0;
    any = true;
  }
  return any;
}

size_t writeVoiceJson(const VoiceConfig& c, char* out, size_t cap) {
  JsonDocument doc;
  doc["available"] = c.available;
  if (c.available) {
    doc["mic"] = c.mic;
    doc["claps"] = c.claps;
    doc["sensitivity"] = c.sensitivity;
    doc["standby"] = c.standbyVoice ? "voice" : "deep";
    doc["standby_fixed"] = c.standbyFixed;
    doc["led"] = c.led;
  }
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

bool parseChannelToggle(const char* body, size_t len, uint16_t& number, bool& enabled) {
  JsonDocument doc;
  if (!parse(body, len, doc) || !doc["n"].is<uint16_t>() || !doc["enabled"].is<bool>()) return false;
  number = doc["n"];
  enabled = doc["enabled"];
  return number <= CHANNEL_NUMBER_MAX;
}

bool parseCode(const char* body, size_t len, char* code, size_t cap) {
  JsonDocument doc;
  if (!parse(body, len, doc) || !doc["code"].is<const char*>()) return false;
  const char* c = doc["code"];
  if (!digitsOnly(c, CODE_DIGITS) || cap < CODE_DIGITS + 1) return false;
  snprintf(code, cap, "%s", c);
  return true;
}

bool wifiJsonAdd(JsonDocument& doc, const char* ssid, const char* password, char* error, size_t errorCap) {
  if (!validWifiCredentials(ssid, password)) {
    snprintf(error, errorCap, "SSID 1-32 characters, password empty or 8-63");
    return false;
  }
  if (!doc["networks"].is<JsonArray>()) {  // the old single-network format, or an empty file
    const char* oldSsid = doc["ssid"] | "";
    const char* oldPass = doc["password"] | "";
    JsonDocument fresh;
    JsonArray list = fresh["networks"].to<JsonArray>();
    if (validWifiCredentials(oldSsid, oldPass)) {
      JsonObject o = list.add<JsonObject>();
      o["ssid"] = oldSsid;
      o["password"] = oldPass;
    }
    doc = fresh;
  }
  JsonArray list = doc["networks"].as<JsonArray>();
  for (JsonObject o : list) {
    if (strcmp(o["ssid"] | "", ssid) == 0) {
      o["password"] = password;
      return true;
    }
  }
  if (list.size() >= WIFI_MAX_NETWORKS) {
    snprintf(error, errorCap, "at most %u networks", static_cast<unsigned>(WIFI_MAX_NETWORKS));
    return false;
  }
  JsonObject o = list.add<JsonObject>();
  o["ssid"] = ssid;
  o["password"] = password;
  return true;
}

bool wifiJsonRemove(JsonDocument& doc, const char* ssid) {
  JsonArray list = doc["networks"].as<JsonArray>();
  for (size_t i = 0; i < list.size(); ++i) {
    if (strcmp(list[i]["ssid"] | "", ssid) == 0) {
      list.remove(i);
      return true;
    }
  }
  return false;
}

size_t writeWifiJson(const JsonDocument& doc, char* out, size_t cap) {
  Out o{out, cap};
  o.text("{\n  \"networks\": [\n");
  JsonArrayConst list = doc["networks"].as<JsonArrayConst>();
  for (size_t i = 0; i < list.size(); ++i) {
    o.text("    ");
    o.json(list[i]);
    o.text(i + 1 < list.size() ? ",\n" : "\n");
  }
  o.text("  ]\n}\n");
  return o.done();
}

size_t writeWifiListJson(const WifiNetworks& networks, const char* connectedSsid, char* out, size_t cap) {
  JsonDocument doc;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (uint8_t i = 0; i < networks.count; ++i) {
    const WifiCredentials& c = networks.items[i];
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = c.ssid;
    o["source"] = c.source;
    o["connected"] = connectedSsid != nullptr && strcmp(connectedSsid, c.ssid) == 0;
    o["removable"] = strcmp(c.source, "wifi.json") == 0;  // secrets.h is compiled in
  }
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

bool channelsJsonSetEnabled(JsonDocument& doc, uint16_t number, bool enabled) {
  for (JsonObject c : doc["channels"].as<JsonArray>()) {
    if (c["number"] == number) {
      c["enabled"] = enabled;
      return true;
    }
  }
  return false;
}

size_t writeChannelsFile(const JsonDocument& doc, char* out, size_t cap) {
  Out o{out, cap};
  o.text("{\n  \"channels\": [\n");
  JsonArrayConst list = doc["channels"].as<JsonArrayConst>();
  for (size_t i = 0; i < list.size(); ++i) {
    o.text("    ");
    o.json(list[i]);
    o.text(i + 1 < list.size() ? ",\n" : "\n");
  }
  o.text("  ]\n}\n");
  return o.done();
}
