#include "web/RemoteApi.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {

struct KeyName {
  const char* name;
  InputEvent event;
};
constexpr KeyName KEYS[] = {
    {"next", InputEvent::ChNext}, {"prev", InputEvent::ChPrev},  {"volup", InputEvent::VolUp},
    {"voldown", InputEvent::VolDown}, {"mute", InputEvent::Mute}, {"info", InputEvent::ToggleOsd},
    {"power", InputEvent::Power},
};
constexpr size_t CHANNEL_DIGITS_MAX = 3;

size_t writeJson(const JsonDocument& doc, char* out, size_t cap) {
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

}  // namespace

bool parseRemoteKey(const char* name, InputEvent& out) {
  if (name == nullptr) return false;
  for (const KeyName& k : KEYS) {
    if (strcmp(name, k.name) == 0) {
      out = k.event;
      return true;
    }
  }
  return false;
}

bool parseChannelNumber(const char* text, uint16_t& out) {
  if (text == nullptr || text[0] == '\0' || strlen(text) > CHANNEL_DIGITS_MAX) return false;
  uint16_t n = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    n = static_cast<uint16_t>(n * 10 + (*p - '0'));
  }
  out = n;
  return true;
}

LogoInk parseLogoInk(const char* text) {
  if (text != nullptr && strcmp(text, "black") == 0) return LogoInk::Black;
  if (text != nullptr && strcmp(text, "white") == 0) return LogoInk::White;
  return LogoInk::Colour;
}

size_t writeStateJson(const RemoteState& state, char* out, size_t cap) {
  JsonDocument doc;
  if (state.tuned) {
    doc["channel"] = state.channel;
  } else {
    doc["channel"] = nullptr;
  }
  doc["name"] = state.name;
  doc["volume"] = state.volume;
  doc["muted"] = state.muted;
  doc["screen"] = state.screen;
  if (state.battery == BATTERY_UNKNOWN) {
    doc["battery"] = nullptr;
    doc["battery_mv"] = nullptr;
  } else {
    doc["battery"] = state.battery;
    doc["battery_mv"] = state.batteryMv;
  }
  doc["battery_low"] = state.batteryLow;
  doc["charging"] = state.charging;
  doc["list"] = state.channelsVersion;
  return writeJson(doc, out, cap);
}

size_t writeChannelsJson(const ChannelManager& channels, uint64_t logoMask, char* out, size_t cap) {
  JsonDocument doc;
  JsonArray list = doc["channels"].to<JsonArray>();
  for (size_t i = 0; i < channels.count(); ++i) {
    const Channel& c = channels.at(i);
    if (!c.enabled) continue;
    JsonObject o = list.add<JsonObject>();
    o["n"] = c.number;
    o["name"] = c.name;
    o["type"] = channelTypeName(c.type);
    if (i < 64 && (logoMask >> i) & 1ull) o["logo"] = true;
  }
  return writeJson(doc, out, cap);
}

size_t writeGuideJson(const GuideRow* rows, size_t n, uint32_t nowS, char* out, size_t cap) {
  JsonDocument doc;
  doc["now"] = nowS;
  JsonArray list = doc["channels"].to<JsonArray>();
  for (size_t i = 0; i < n; ++i) {
    const GuideRow& r = rows[i];
    JsonObject o = list.add<JsonObject>();
    o["n"] = r.number;
    o["name"] = r.name;
    o["type"] = r.type;
    if (r.logo) o["logo"] = true;
    if (r.note != nullptr) o["note"] = r.note;
    if (r.count == 0) continue;
    JsonArray items = o["items"].to<JsonArray>();
    for (size_t k = 0; k < r.count && k < GUIDE_ITEMS; ++k) {
      JsonObject it = items.add<JsonObject>();
      it["t"] = r.items[k].title;
      it["s"] = r.items[k].startS;
      it["e"] = r.items[k].endS;
    }
  }
  return writeJson(doc, out, cap);
}
