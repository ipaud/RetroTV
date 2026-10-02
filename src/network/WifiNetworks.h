#pragma once

// Known Wi-Fi networks and the order to try them in. Pure C++ (tested on the host).

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

constexpr size_t WIFI_MAX_NETWORKS = 8;

struct WifiCredentials {
  char ssid[33] = "";
  char password[65] = "";
  const char* source = "none";
};

// SSID 1-32 characters; password empty (open network) or 8-63 (WPA2).
bool validWifiCredentials(const char* ssid, const char* password);

struct WifiNetworks {
  WifiCredentials items[WIFI_MAX_NETWORKS];
  uint8_t count = 0;

  // False when invalid, already listed (the first one wins) or the list is full.
  bool add(const char* ssid, const char* password, const char* source);
};

// wifi.json: {"ssid": "...", "password": "..."} or {"networks": [{"ssid", "password"}, ...]}.
// Adds the valid entries; `skipped` counts the rest. Never looks at a password beyond checking it.
void parseWifiJson(const JsonDocument& doc, WifiNetworks& out, const char* source, uint8_t& skipped);

struct SeenNetwork {
  const char* ssid;
  int rssi;
};

// Indexes of `known` in the order to try: the ones a scan saw, strongest first, then the rest
// in file order (a hidden SSID never shows in a scan). Returns how many (always known.count).
uint8_t orderWifiAttempts(const WifiNetworks& known, const SeenNetwork* seen, size_t seenCount, uint8_t* order);
