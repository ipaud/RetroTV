// Web remote settings: pairing with a code shown on the TV, request bodies, and the edits to
// wifi.json and channels.json. Run: tools/run_host_tests.sh

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "check.h"
#include "web/ConfigApi.h"

static uint32_t g_random = 0;
static uint32_t fakeRandom() { return g_random += 0x9E3779B9u; }

static void testPairing() {
  PairingGate gate(fakeRandom);
  char token[PAIR_TOKEN_LEN + 1];
  CHECK(!gate.allowed("", 0));
  const uint16_t code = gate.start(1000);
  CHECK(code >= 1000 && code <= 9999);
  CHECK(gate.codeActive(1000) && !gate.codeActive(1000 + PAIR_CODE_MS));
  char text[8];
  snprintf(text, sizeof(text), "%04u", static_cast<unsigned>(code));
  CHECK(gate.verify(text, 2000, token, sizeof(token)));
  CHECK(strlen(token) == PAIR_TOKEN_LEN);
  CHECK(gate.allowed(token, 2000 + PAIR_TOKEN_MS - 1));
  CHECK(gate.allowed(token, 2000 + 2 * PAIR_TOKEN_MS - 2));  // sliding: each use extends it
  CHECK(!gate.allowed(token, 2000 + 4 * PAIR_TOKEN_MS));
  CHECK(!gate.verify(text, 3000, token, sizeof(token)));  // a code works once
  CHECK(!gate.allowed("0000000000000000000000000000000", 3000));
}

static void testPairingLimits() {
  PairingGate gate(fakeRandom);
  char token[PAIR_TOKEN_LEN + 1];
  const uint16_t code = gate.start(0);
  char right[8], wrong[8];
  snprintf(right, sizeof(right), "%04u", static_cast<unsigned>(code));
  snprintf(wrong, sizeof(wrong), "%04u", static_cast<unsigned>(code == 9999 ? 1000 : code + 1));
  CHECK(!gate.verify(right, PAIR_CODE_MS + 1, token, sizeof(token)));  // expired
  gate.start(0);
  const uint16_t code2 = gate.start(0);
  snprintf(right, sizeof(right), "%04u", static_cast<unsigned>(code2));
  snprintf(wrong, sizeof(wrong), "%04u", static_cast<unsigned>(code2 == 9999 ? 1000 : code2 + 1));
  for (uint8_t i = 0; i < PAIR_MAX_TRIES; ++i) CHECK(!gate.verify(wrong, 10, token, sizeof(token)));
  CHECK(!gate.verify(right, 10, token, sizeof(token)));  // too many tries: a new code is needed
  CHECK(!gate.verify("12a4", 10, token, sizeof(token)));
  CHECK(!gate.verify(nullptr, 10, token, sizeof(token)));
}

static void testBodies() {
  char ssid[33], pass[65];
  const char* add = R"({"ssid": "Vera_200461", "password": "secret12"})";
  CHECK(parseWifiAdd(add, strlen(add), ssid, pass) && strcmp(ssid, "Vera_200461") == 0 && strcmp(pass, "secret12") == 0);
  const char* shortPass = R"({"ssid": "x", "password": "1234567"})";
  CHECK(!parseWifiAdd(shortPass, strlen(shortPass), ssid, pass));
  const char* open = R"({"ssid": "cafe"})";
  CHECK(parseWifiAdd(open, strlen(open), ssid, pass) && pass[0] == '\0');
  CHECK(!parseWifiAdd("not json", 8, ssid, pass));

  int brightness = 0, volume = 0;
  const char* disp = R"({"brightness": 70})";
  CHECK(parseDisplay(disp, strlen(disp), brightness, volume) && brightness == 70 && volume == -1);
  const char* bad = R"({"brightness": 5, "volume": 50})";
  CHECK(!parseDisplay(bad, strlen(bad), brightness, volume));  // brightness below 10
  const char* vol = R"({"volume": 0})";
  CHECK(parseDisplay(vol, strlen(vol), brightness, volume) && volume == 0 && brightness == -1);

  uint16_t n = 0;
  bool enabled = true;
  const char* toggle = R"({"n": 22, "enabled": false})";
  CHECK(parseChannelToggle(toggle, strlen(toggle), n, enabled) && n == 22 && !enabled);
  const char* zero = R"({"n": 0, "enabled": true})";
  CHECK(parseChannelToggle(zero, strlen(zero), n, enabled) && n == 0 && enabled);
  const char* noFlag = R"({"n": 22})";
  CHECK(!parseChannelToggle(noFlag, strlen(noFlag), n, enabled));

  char code[5];
  const char* pair = R"({"code": "0421"})";
  CHECK(parseCode(pair, strlen(pair), code, sizeof(code)) && strcmp(code, "0421") == 0);
}

static void testWifiJsonEdits() {
  JsonDocument doc;
  deserializeJson(doc, R"({"ssid": "home", "password": "12345678"})");  // the old single format
  char error[64];
  CHECK(wifiJsonAdd(doc, "office", "abcdefgh", error, sizeof(error)));
  CHECK(doc["networks"].size() == 2 && doc["ssid"].isNull());  // converted to the list format
  CHECK(wifiJsonAdd(doc, "office", "newpass99", error, sizeof(error)));  // same SSID: new password
  CHECK(doc["networks"].size() == 2 && strcmp(doc["networks"][1]["password"] | "", "newpass99") == 0);
  CHECK(wifiJsonRemove(doc, "home") && doc["networks"].size() == 1);
  CHECK(!wifiJsonRemove(doc, "nothere"));
  char out[512];
  const size_t len = writeWifiJson(doc, out, sizeof(out));
  CHECK(len > 0);
  JsonDocument back;
  CHECK(!deserializeJson(back, out) && strcmp(back["networks"][0]["ssid"] | "", "office") == 0);
  for (int i = 0; i < 12; ++i) {
    char ssid[12];
    snprintf(ssid, sizeof(ssid), "net%d", i);
    wifiJsonAdd(doc, ssid, "12345678", error, sizeof(error));
  }
  CHECK(doc["networks"].size() == WIFI_MAX_NETWORKS);  // bounded
}

static void testWifiListNeverShowsPasswords() {
  WifiNetworks nets;
  nets.add("home", "supersecret1", "secrets.h");
  nets.add("office", "anothersecret", "wifi.json");
  char out[512];
  CHECK(writeWifiListJson(nets, "office", out, sizeof(out)) > 0);
  CHECK(strstr(out, "supersecret1") == nullptr && strstr(out, "anothersecret") == nullptr);
  JsonDocument doc;
  CHECK(!deserializeJson(doc, out));
  CHECK(doc["networks"][1]["connected"] == true && doc["networks"][0]["removable"] == false);
  CHECK(doc["networks"][1]["removable"] == true);
}

static void testChannelsJsonToggle() {
  JsonDocument doc;
  deserializeJson(doc, R"({"channels": [
    {"id": "a", "number": 1, "name": "A", "type": "local", "source": "/a", "enabled": true, "extra": 5},
    {"id": "b", "number": 2, "name": "B", "type": "local", "source": "/b"}]})");
  CHECK(channelsJsonSetEnabled(doc, 2, false));
  CHECK(!channelsJsonSetEnabled(doc, 9, false));
  char out[1024];
  const size_t len = writeChannelsFile(doc, out, sizeof(out));
  CHECK(len > 0);
  JsonDocument back;
  CHECK(!deserializeJson(back, out));
  CHECK(back["channels"][1]["enabled"] == false && back["channels"][0]["extra"] == 5);  // other fields kept
  // One channel per line, like the hand-written file.
  const std::string text(out);
  CHECK(text.find("\n    {\"id\":\"a\"") != std::string::npos && text.find("\n    {\"id\":\"b\"") != std::string::npos);
  char tiny[16];
  CHECK(writeChannelsFile(doc, tiny, sizeof(tiny)) == 0);
}

void runConfigTests() {
  testPairing();
  testPairingLimits();
  testBodies();
  testWifiJsonEdits();
  testWifiListNeverShowsPasswords();
  testChannelsJsonToggle();
}
