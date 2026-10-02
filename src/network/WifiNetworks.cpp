#include "network/WifiNetworks.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr size_t SSID_MAX = 32;
constexpr size_t WPA_PASS_MIN = 8;
constexpr size_t WPA_PASS_MAX = 63;
constexpr int NOT_SEEN = -1000;

}  // namespace

bool validWifiCredentials(const char* ssid, const char* password) {
  const size_t s = strlen(ssid);
  const size_t p = strlen(password);
  return s >= 1 && s <= SSID_MAX && (p == 0 || (p >= WPA_PASS_MIN && p <= WPA_PASS_MAX));
}

bool WifiNetworks::add(const char* ssid, const char* password, const char* source) {
  if (count >= WIFI_MAX_NETWORKS || !validWifiCredentials(ssid, password)) return false;
  for (uint8_t i = 0; i < count; ++i) {
    if (strcmp(items[i].ssid, ssid) == 0) return false;
  }
  WifiCredentials& c = items[count++];
  snprintf(c.ssid, sizeof(c.ssid), "%s", ssid);
  snprintf(c.password, sizeof(c.password), "%s", password);
  c.source = source;
  return true;
}

void parseWifiJson(const JsonDocument& doc, WifiNetworks& out, const char* source, uint8_t& skipped) {
  auto take = [&](JsonVariantConst entry) {
    const char* ssid = entry["ssid"] | "";
    const char* password = entry["password"] | "";
    if (!entry.is<JsonObjectConst>() || !out.add(ssid, password, source)) ++skipped;
  };
  JsonArrayConst list = doc["networks"].as<JsonArrayConst>();
  if (list.isNull()) {
    take(doc.as<JsonVariantConst>());
    return;
  }
  for (JsonVariantConst entry : list) take(entry);
}

uint8_t orderWifiAttempts(const WifiNetworks& known, const SeenNetwork* seen, size_t seenCount, uint8_t* order) {
  int best[WIFI_MAX_NETWORKS];
  for (uint8_t i = 0; i < known.count; ++i) {
    best[i] = NOT_SEEN;
    for (size_t j = 0; j < seenCount; ++j) {
      if (strcmp(seen[j].ssid, known.items[i].ssid) == 0 && seen[j].rssi > best[i]) best[i] = seen[j].rssi;
    }
    order[i] = i;
  }
  // Insertion sort, stable: equal signal (or both unseen) keeps the file order.
  for (uint8_t i = 1; i < known.count; ++i) {
    const uint8_t k = order[i];
    uint8_t j = i;
    while (j > 0 && best[order[j - 1]] < best[k]) {
      order[j] = order[j - 1];
      --j;
    }
    order[j] = k;
  }
  return known.count;
}
