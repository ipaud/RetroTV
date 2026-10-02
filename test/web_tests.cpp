// Web remote API: key names, channel numbers, state and channel list JSON.
// Run: tools/run_host_tests.sh

#include <ArduinoJson.h>

#include <cstring>

#include "channels/ChannelManager.h"
#include "check.h"
#include "web/RemoteApi.h"

static void testKeys() {
  InputEvent e = InputEvent::None;
  CHECK(parseRemoteKey("next", e) && e == InputEvent::ChNext);
  CHECK(parseRemoteKey("prev", e) && e == InputEvent::ChPrev);
  CHECK(parseRemoteKey("volup", e) && e == InputEvent::VolUp);
  CHECK(parseRemoteKey("voldown", e) && e == InputEvent::VolDown);
  CHECK(parseRemoteKey("mute", e) && e == InputEvent::Mute);
  CHECK(parseRemoteKey("power", e) && e == InputEvent::Power);  // standby
  CHECK(parseRemoteKey("info", e) && e == InputEvent::ToggleOsd);
  CHECK(!parseRemoteKey("menu", e));  // the settings menu is for the knobs only
  CHECK(!parseRemoteKey("", e));
  CHECK(!parseRemoteKey("NEXT", e));
  CHECK(!parseRemoteKey(nullptr, e));
}

static void testChannelNumbers() {
  uint16_t n = 0;
  CHECK(parseChannelNumber("22", n) && n == 22);
  CHECK(parseChannelNumber("1", n) && n == 1);
  CHECK(parseChannelNumber("999", n) && n == 999);
  CHECK(parseChannelNumber("0", n) && n == 0);  // the tutorial channel
  CHECK(!parseChannelNumber("1000", n));
  CHECK(!parseChannelNumber("2a", n));
  CHECK(!parseChannelNumber("-1", n));
  CHECK(!parseChannelNumber("", n));
  CHECK(!parseChannelNumber(" 5", n));
  CHECK(!parseChannelNumber("00000000000000000007", n));  // bounded length
  CHECK(!parseChannelNumber(nullptr, n));
}

static void testStateJson() {
  RemoteState s;
  s.tuned = true;
  s.channel = 22;
  snprintf(s.name, sizeof(s.name), "SX3 \"LIVE\"");
  s.volume = 55;
  s.muted = true;
  s.screen = "playing";
  char out[256];  // = STATE_JSON_CAP in WebRemote.cpp
  const size_t len = writeStateJson(s, out, sizeof(out));
  CHECK(len > 0 && len == strlen(out));
  JsonDocument doc;
  CHECK(!deserializeJson(doc, out));
  CHECK(doc["channel"] == 22 && doc["volume"] == 55 && doc["muted"] == true);
  CHECK(strcmp(doc["name"] | "", "SX3 \"LIVE\"") == 0);  // escaped, survives the round trip
  CHECK(strcmp(doc["screen"] | "", "playing") == 0);
  char tiny[8];
  CHECK(writeStateJson(s, tiny, sizeof(tiny)) == 0);  // never a cut-off JSON

  s.channel = 0;  // channel 0 is a channel; nothing tuned yet is null
  CHECK(writeStateJson(s, out, sizeof(out)) > 0 && !deserializeJson(doc, out) && doc["channel"] == 0);
  RemoteState none;
  CHECK(writeStateJson(none, out, sizeof(out)) > 0 && !deserializeJson(doc, out) && doc["channel"].isNull());
  CHECK(doc["battery"].isNull() && doc["battery_mv"].isNull() && doc["battery_low"] == false);  // no reading yet
  CHECK(doc["charging"] == false);
  s.battery = 12;
  s.batteryLow = true;
  s.batteryMv = 3640;
  s.charging = true;
  CHECK(writeStateJson(s, out, sizeof(out)) > 0 && !deserializeJson(doc, out));
  CHECK(doc["battery"] == 12 && doc["battery_mv"] == 3640 && doc["battery_low"] == true && doc["charging"] == true);
  s.channelsVersion = 0x8badf00du;  // the phone refetches the channel list when this changes
  CHECK(writeStateJson(s, out, sizeof(out)) > 0 && !deserializeJson(doc, out) && doc["list"] == 0x8badf00du);
  RemoteState worst;  // every field at its longest still fits the board's buffer
  worst.tuned = true;
  worst.channel = 999;
  memset(worst.name, '"', sizeof(worst.name) - 1);  // escaped: twice as long
  worst.volume = 100;
  worst.screen = "switching";
  worst.battery = 100;
  worst.batteryMv = 4200;
  worst.batteryLow = true;
  worst.charging = true;
  worst.channelsVersion = 0xFFFFFFFFu;
  CHECK(writeStateJson(worst, out, sizeof(out)) > 0);
}

static void testChannelsJson() {
  ChannelManager cm;
  JsonDocument src;
  deserializeJson(src, R"({"channels": [
    {"id": "b", "number": 2, "name": "Bola de Drac", "type": "local", "source": "/retrotv/media/b"},
    {"id": "t", "number": 1, "name": "Teletext", "type": "internal", "source": "teletext"},
    {"id": "off", "number": 3, "name": "Off", "type": "local", "source": "/x", "enabled": false},
    {"id": "sx3", "number": 22, "name": "SX3", "type": "remote", "source": "http://retrotv-server.local:8080/channel/10"}]})");
  LoadReport r;
  CHECK(cm.load(src.as<JsonVariantConst>(), r));
  char out[512];
  const int sx3 = [&] {
    for (size_t i = 0; i < cm.count(); ++i)
      if (cm.at(i).number == 22) return static_cast<int>(i);
    return -1;
  }();
  CHECK(sx3 >= 0);
  const size_t len = writeChannelsJson(cm, 1ull << sx3, out, sizeof(out));
  CHECK(len > 0);
  JsonDocument doc;
  CHECK(!deserializeJson(doc, out));
  JsonArrayConst list = doc["channels"].as<JsonArrayConst>();
  CHECK(list.size() == 3);  // the disabled one is not offered
  CHECK(list[0]["n"] == 1 && strcmp(list[0]["name"] | "", "TELETEXT") == 0);
  CHECK(list[2]["n"] == 22 && strcmp(list[2]["type"] | "", "remote") == 0);
  CHECK(list[2]["logo"] == true);            // only the channel whose bit is set
  CHECK(list[0]["logo"].isNull() && list[1]["logo"].isNull());
  char tiny[16];
  CHECK(writeChannelsJson(cm, 0, tiny, sizeof(tiny)) == 0);
}

// GET /api/guide: per channel, what is on and what follows (epoch seconds: the page places them
// against the TV's "now", so it works without NTP too), or a note instead.
static void testGuideJson() {
  GuideRow rows[3];
  rows[0].number = 2;
  snprintf(rows[0].name, sizeof(rows[0].name), "BOLA DE DRAC");
  rows[0].logo = true;
  rows[0].count = 2;
  rows[0].items[0] = GuideItem{1000, 2400, "06 EL \"GRAN\" TORNEIG"};
  rows[0].items[1] = GuideItem{2400, 3800, "07 GOKU"};
  rows[1].number = 19;
  snprintf(rows[1].name, sizeof(rows[1].name), "SX3");
  rows[1].type = "remote";
  rows[1].note = "EN DIRECTO";
  rows[2].number = 30;
  snprintf(rows[2].name, sizeof(rows[2].name), "CARTA DE AJUSTE");
  rows[2].type = "internal";
  rows[2].note = "CARTA DE AJUSTE";
  char out[1024];
  const size_t len = writeGuideJson(rows, 3, 1200, out, sizeof(out));
  CHECK(len > 0 && len == strlen(out));
  JsonDocument doc;
  CHECK(!deserializeJson(doc, out));
  CHECK(doc["now"] == 1200 && doc["channels"].size() == 3);
  JsonObject bola = doc["channels"][0];
  CHECK(bola["n"] == 2 && bola["logo"] == true && strcmp(bola["type"] | "", "local") == 0);
  CHECK(bola["items"].size() == 2 && bola["items"][0]["s"] == 1000 && bola["items"][0]["e"] == 2400);
  CHECK(strcmp(bola["items"][0]["t"] | "", "06 EL \"GRAN\" TORNEIG") == 0);  // escaped, survives the trip
  CHECK(bola["note"].isNull());
  JsonObject live = doc["channels"][1];
  CHECK(strcmp(live["note"] | "", "EN DIRECTO") == 0 && live["items"].isNull() && live["logo"].isNull());
  char tiny[64];
  CHECK(writeGuideJson(rows, 3, 1200, tiny, sizeof(tiny)) == 0);  // never a cut-off JSON
}

void runWebTests() {
  testKeys();
  testChannelNumbers();
  testStateJson();
  testChannelsJson();
  testGuideJson();
}
