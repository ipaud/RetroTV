// Known Wi-Fi networks: wifi.json formats, validation and the order they are tried in.
// Run: tools/run_host_tests.sh

#include <ArduinoJson.h>

#include <cstring>

#include "check.h"
#include "network/WifiNetworks.h"

static WifiNetworks parse(const char* json, uint8_t& skipped) {
  JsonDocument doc;
  WifiNetworks nets;
  skipped = 0;
  if (!deserializeJson(doc, json)) parseWifiJson(doc, nets, "wifi.json", skipped);
  return nets;
}

static void testSingleNetworkFormatStillWorks() {
  uint8_t skipped = 0;
  const WifiNetworks n = parse(R"({"ssid": "home", "password": "12345678"})", skipped);
  CHECK(n.count == 1 && skipped == 0);
  CHECK(strcmp(n.items[0].ssid, "home") == 0 && strcmp(n.items[0].password, "12345678") == 0);
  CHECK(strcmp(n.items[0].source, "wifi.json") == 0);
}

static void testNetworkList() {
  uint8_t skipped = 0;
  const WifiNetworks n = parse(R"({"networks": [
      {"ssid": "home", "password": "12345678"},
      {"ssid": "office", "password": "abcdefgh"},
      {"ssid": "cafe"}]})",
                               skipped);
  CHECK(n.count == 3 && skipped == 0);
  CHECK(strcmp(n.items[1].ssid, "office") == 0);
  CHECK(n.items[2].password[0] == '\0');  // open network
}

static void testBadEntriesAreSkipped() {
  uint8_t skipped = 0;
  const WifiNetworks n = parse(R"({"networks": [
      {"ssid": "", "password": "12345678"},
      {"ssid": "short", "password": "1234567"},
      {"ssid": "123456789012345678901234567890123", "password": "12345678"},
      {"password": "12345678"},
      "not an object",
      {"ssid": "home", "password": "12345678"},
      {"ssid": "home", "password": "different1"}]})",
                               skipped);
  CHECK(n.count == 1 && skipped == 6);  // the second "home" is a duplicate: the first wins
  CHECK(strcmp(n.items[0].password, "12345678") == 0);
}

static void testListIsBounded() {
  WifiNetworks n;
  char ssid[8];
  for (int i = 0; i < 12; ++i) {
    snprintf(ssid, sizeof(ssid), "net%d", i);
    n.add(ssid, "12345678", "test");
  }
  CHECK(n.count == WIFI_MAX_NETWORKS);
  CHECK(!n.add("another", "12345678", "test"));
}

static void testPasswordLimits() {
  CHECK(validWifiCredentials("x", ""));                       // open
  CHECK(!validWifiCredentials("x", "1234567"));               // WPA2 needs 8
  CHECK(validWifiCredentials("x", "12345678"));
  char longPass[65];
  memset(longPass, 'a', 63);
  longPass[63] = '\0';
  CHECK(validWifiCredentials("x", longPass));                 // 63 is the WPA2 maximum
  longPass[63] = 'a';
  longPass[64] = '\0';
  CHECK(!validWifiCredentials("x", longPass));
}

static void testOrderPrefersVisibleStrongest() {
  WifiNetworks n;
  n.add("home", "12345678", "t");
  n.add("office", "12345678", "t");
  n.add("hidden", "12345678", "t");
  const SeenNetwork seen[] = {{"neighbour", -40}, {"home", -80}, {"office", -50}, {"office", -70}};
  uint8_t order[WIFI_MAX_NETWORKS];
  const uint8_t k = orderWifiAttempts(n, seen, 4, order);
  CHECK(k == 3);
  CHECK(order[0] == 1 && order[1] == 0);  // office (-50) before home (-80)
  CHECK(order[2] == 2);                   // not seen (hidden SSID?) last, still tried
}

static void testOrderWithoutScanKeepsFileOrder() {
  WifiNetworks n;
  n.add("a", "12345678", "t");
  n.add("b", "12345678", "t");
  uint8_t order[WIFI_MAX_NETWORKS];
  CHECK(orderWifiAttempts(n, nullptr, 0, order) == 2);
  CHECK(order[0] == 0 && order[1] == 1);
}

void runWifiTests() {
  testSingleNetworkFormatStillWorks();
  testNetworkList();
  testBadEntriesAreSkipped();
  testListIsBounded();
  testPasswordLimits();
  testOrderPrefersVisibleStrongest();
  testOrderWithoutScanKeepsFileOrder();
}
