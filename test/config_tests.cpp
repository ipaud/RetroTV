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
  const char* add = R"({"ssid": "Oficina-2G", "password": "secret12"})";
  CHECK(parseWifiAdd(add, strlen(add), ssid, pass) && strcmp(ssid, "Oficina-2G") == 0 && strcmp(pass, "secret12") == 0);
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

static void testVoiceBodies() {
  VoiceChange v;
  const char* all = R"({"mic": true, "claps": false, "sensitivity": 70, "standby": "voice", "led": false})";
  CHECK(parseVoice(all, strlen(all), v) && v.mic == 1 && v.claps == 0 && v.sensitivity == 70 && v.standbyVoice == 1 &&
        v.led == 0);
  const char* one = R"({"standby": "deep"})";  // only what was sent changes
  CHECK(parseVoice(one, strlen(one), v) && v.standbyVoice == 0 && v.mic == -1 && v.claps == -1 && v.sensitivity == -1 &&
        v.led == -1);
  for (const char* bad : {R"({})", R"({"mic": 1})", R"({"sensitivity": 101})", R"({"sensitivity": -1})",
                          R"({"sensitivity": "60"})", R"({"standby": "off"})", R"({"standby": true})", "not json"}) {
    CHECK(!parseVoice(bad, strlen(bad), v));
  }
  char out[160];
  VoiceConfig c;
  CHECK(writeVoiceJson(c, out, sizeof(out)) > 0 && strcmp(out, R"({"available":false})") == 0);
  c = VoiceConfig{true, true, true, 60, false, true, true};
  CHECK(writeVoiceJson(c, out, sizeof(out)) > 0 &&
        strcmp(out, R"({"available":true,"mic":true,"claps":true,"sensitivity":60,"standby":"deep","standby_fixed":true,"led":true})") == 0);
  CHECK(writeVoiceJson(c, out, 20) == 0);  // never a cut-off JSON
  // The _ww builds: the two wake words, each on its own.
  const char* ww = R"({"hola_esp": false, "hey_retro": true})";
  CHECK(parseVoice(ww, strlen(ww), v) && v.holaEsp == 0 && v.heyRetro == 1 && v.mic == -1 && v.led == -1);
  CHECK(parseVoice(one, strlen(one), v) && v.holaEsp == -1 && v.heyRetro == -1);
  for (const char* bad : {R"({"hola_esp": 1})", R"({"hey_retro": "on"})"}) CHECK(!parseVoice(bad, strlen(bad), v));
  c.wakeWords = true;
  c.holaEsp = false;
  c.heyRetro = true;
  char big[200];
  CHECK(writeVoiceJson(c, big, sizeof(big)) > 0 &&
        strcmp(big, R"({"available":true,"mic":true,"claps":true,"sensitivity":60,"standby":"deep","standby_fixed":true,"led":true,"hola_esp":false,"hey_retro":true})") == 0);
}

static void testAddMessagesChannel() {
  JsonDocument doc;
  deserializeJson(doc, R"({"channels": [{"id": "a", "number": 3, "name": "A", "type": "local", "source": "/x"},
                                         {"id": "qr", "number": 33, "name": "MANDO", "type": "internal", "source": "mando"}]})");
  uint16_t n = 0;
  CHECK(channelsJsonAddMessages(doc, n) && n == 34);  // after the highest
  JsonObjectConst added = doc["channels"][2];
  CHECK(std::string(added["source"] | "") == "messages" && std::string(added["type"] | "") == "internal" &&
        std::string(added["name"] | "") == "MENSAJES" && added["enabled"] == true);
  CHECK(!channelsJsonAddMessages(doc, n) && doc["channels"].size() == 3);  // never twice
  JsonDocument full;
  deserializeJson(full, R"({"channels": [{"id": "z", "number": 999, "name": "Z", "type": "local", "source": "/z"},
                                          {"id": "u", "number": 1, "name": "U", "type": "local", "source": "/u"}]})");
  CHECK(channelsJsonAddMessages(full, n) && n == 2);  // 999 taken: the first free one
  JsonDocument named;
  deserializeJson(named, R"({"channels": [{"id": "mensajes", "number": 7, "name": "Notas", "type": "local", "source": "/n"}]})");
  CHECK(!channelsJsonAddMessages(named, n));  // the id is taken: leave the user's file alone
  JsonDocument broken;
  deserializeJson(broken, R"({"canales": []})");
  CHECK(!channelsJsonAddMessages(broken, n));
}

void runConfigTests() {
  testPairing();
  testPairingLimits();
  testBodies();
  testWifiJsonEdits();
  testWifiListNeverShowsPasswords();
  testChannelsJsonToggle();
  testVoiceBodies();
  testAddMessagesChannel();
}
