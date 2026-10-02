// Host tests: channels.json loading/validation, zapping order, OSD text.

#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "channels/ChannelManager.h"
#include "check.h"
#include "storage/SdLayout.h"

namespace {

bool loadText(ChannelManager& cm, const char* json, LoadReport& report) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  return cm.load(doc.as<JsonVariantConst>(), report);
}

std::string osd(const char* utf8, size_t len = 32) {
  char out[64];
  toOsdText(utf8, out, len);
  return out;
}

void testDefaultChannelsJson() {
  ChannelManager cm;
  LoadReport r;
  CHECK(loadText(cm, DEFAULT_CHANNELS_JSON, r));  // the file the firmware writes must load
  CHECK(cm.count() == 6);
  CHECK(cm.enabledCount() == 5);
  CHECK(r.skipped == 0);
  CHECK(cm.at(0).number == 1 && cm.at(5).number == 10);
  CHECK(cm.at(3).type == ChannelType::Internal && strcmp(cm.at(3).source, INTERNAL_TELETEXT) == 0);
  CHECK(cm.at(4).type == ChannelType::Internal && strcmp(cm.at(4).source, INTERNAL_TESTCARD) == 0);
  CHECK(cm.at(5).type == ChannelType::Remote && !cm.at(5).enabled);
}

void testSortedAndZapping() {
  ChannelManager cm;
  LoadReport r;
  const char* json = R"({"channels":[
    {"id":"c","number":9,"name":"Nueve","type":"internal","source":"testcard"},
    {"id":"a","number":1,"name":"Uno","type":"local","source":"/retrotv/media/a"},
    {"id":"x","number":5,"name":"Off","type":"local","source":"/x","enabled":false},
    {"id":"b","number":3,"name":"Tres","type":"local","source":"/retrotv/media/b.mjpeg"}]})";
  CHECK(loadText(cm, json, r));
  CHECK(cm.at(0).number == 1 && cm.at(1).number == 3 && cm.at(2).number == 5 && cm.at(3).number == 9);

  CHECK(cm.select(3) && cm.current()->number == 3);
  CHECK(cm.next()->number == 9);  // 5 is disabled: skipped
  CHECK(cm.next()->number == 1);  // wraps
  CHECK(cm.prev()->number == 9);
  CHECK(cm.prev()->number == 3);
  CHECK(cm.select(5) == nullptr);  // disabled cannot be selected
  CHECK(cm.select(42) == nullptr);
  CHECK(cm.current()->number == 3);  // a failed select keeps the current channel
  CHECK(cm.selectOrFirst(42)->number == 1);  // remembered channel gone: first one
  CHECK(cm.selectOrFirst(9)->number == 9);
}

void testValidation() {
  ChannelManager cm;
  LoadReport r;
  const char* json = R"({"channels":[
    {"id":"a","number":1,"name":"Uno","type":"local","source":"/a"},
    {"id":"dup","number":1,"name":"Dup","type":"local","source":"/b"},
    {"id":"noname","number":2,"type":"local","source":"/c"},
    {"id":"cable","number":3,"name":"Cable","type":"cable","source":"/d"},
    {"id":"rel","number":4,"name":"Rel","type":"local","source":"relative/path"},
    {"id":"tc","number":5,"name":"TC","type":"internal","source":"colorbars"},
    {"id":"big","number":1000,"name":"Big","type":"local","source":"/e"},
    {"id":"en","number":6,"name":"En","type":"local","source":"/f","enabled":"yes"},
    "not an object",
    {"id":"rem","number":7,"name":"Rem","type":"remote","source":"http://x/1"},
    {"id":"hls","number":8,"name":"Hls","type":"hls-proxy","source":"http://x/2"},
    {"id":"tun","number":11,"name":"Tun","type":"tunarr","source":"http://x/3"},
    {"id":"str","number":12,"name":"Str","type":"stream","source":"http://x/4"}]})";
  CHECK(loadText(cm, json, r));
  CHECK(cm.count() == 5);  // a + the four recognised V0.2 types
  CHECK(r.skipped == 8);
  CHECK(std::string(r.firstProblem).find("#2") != std::string::npos);  // duplicate number reported first
  CHECK(!isImplemented(cm.at(1).type));
  CHECK(cm.at(2).type == ChannelType::HlsProxy && cm.at(3).type == ChannelType::Tunarr &&
        cm.at(4).type == ChannelType::Stream);
}

void testUnusableFiles() {
  ChannelManager cm;
  LoadReport r;
  CHECK(!loadText(cm, R"({"canales":[]})", r));  // no "channels" array
  CHECK(r.firstProblem[0] != '\0');
  CHECK(!loadText(cm, R"({"channels":[{"id":"a","number":1,"name":"A","type":"local","source":"/a","enabled":false}]})", r));
  CHECK(!loadText(cm, R"({"channels":[]})", r));

  cm.loadFallback();  // what the TV falls back to
  CHECK(cm.enabledCount() == 1 && cm.at(0).type == ChannelType::Internal);
  CHECK(cm.selectOrFirst(1) != nullptr);
}

void testTooManyChannels() {
  JsonDocument doc;
  JsonArray arr = doc["channels"].to<JsonArray>();
  for (int i = 1; i <= static_cast<int>(MAX_CHANNELS) + 3; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = "c" + std::to_string(i);
    o["number"] = i;
    o["name"] = "C";
    o["type"] = "internal";
    o["source"] = "testcard";
  }
  ChannelManager cm;
  LoadReport r;
  CHECK(cm.load(doc.as<JsonVariantConst>(), r));
  CHECK(cm.count() == MAX_CHANNELS);
  CHECK(r.skipped == 3);
}

void testIndexPath() {
  char out[64];
  CHECK(indexPathFor("/retrotv/media/c1/ep01.mjpeg", out, sizeof(out)));
  CHECK(std::string(out) == "/retrotv/media/c1/ep01.idx");
  char tiny[10];
  CHECK(!indexPathFor("/retrotv/media/c1/ep01.mjpeg", tiny, sizeof(tiny)));
}

// Episode order ignores separators and compares numbers as numbers, so mixed naming styles
// from different sources still play in broadcast order.
bool inOrder(std::vector<std::string> shuffled, const std::vector<std::string>& expected) {
  std::sort(shuffled.begin(), shuffled.end(), [](const std::string& a, const std::string& b) {
    return compareEpisodeNames(a.c_str(), b.c_str()) < 0;
  });
  return shuffled == expected;
}

void testEpisodeOrder() {
  const std::vector<std::string> harlock = {
      "/m/h/el_capita_harlock_-01-_la_bandera_pirata_de_l_espai.mjpeg",
      "/m/h/el_capita_harlock_02_un_objecte_arriba_d_espais_desconeguts.mjpeg",
      "/m/h/el_capita_harlock_-03-_una_dona_que_es_crema_com_el_paper.mjpeg",
      "/m/h/el_capita_harlock_-04-_sota_la_bandera_de_la_llibertat.mjpeg",
      "/m/h/el_capita_harlock_-05-_alla_on_s_acaben_les_estrelles.mjpeg"};
  CHECK(inOrder({harlock[4], harlock[1], harlock[0], harlock[3], harlock[2]}, harlock));

  const std::vector<std::string> hattori = {
      "hattori_-001.mjpeg", "hattori_007_beisbol_a_l_estil_ninja_tdtrip_by_someone.mjpeg",
      "hattori_-008-_l_alumne_nou_tdtrip_by_someone.mjpeg", "hattori_-009-_arriba_en_shinzo.mjpeg",
      "hattori_010_l_hattori_recupera_el_seu_honor.mjpeg", "hattori_-011-_en_shinzo_fa_un_encarrec.mjpeg",
      "hattori_-012-_una_visita_tota_estranya.mjpeg"};
  CHECK(inOrder({hattori[6], hattori[1], hattori[4], hattori[0], hattori[5], hattori[2], hattori[3]}, hattori));

  CHECK(inOrder({"ep_10.mjpeg", "ep_9.mjpeg", "ep_100.mjpeg"}, {"ep_9.mjpeg", "ep_10.mjpeg", "ep_100.mjpeg"}));
  CHECK(inOrder({"dragon_quest_-_fly_02.mjpeg", "dragon_quest_-_fly_01_cat_-_jap.mjpeg"},
                {"dragon_quest_-_fly_01_cat_-_jap.mjpeg", "dragon_quest_-_fly_02.mjpeg"}));
  CHECK(compareEpisodeNames("a_01.mjpeg", "a_1.mjpeg") != 0);  // same number: still a strict order
  CHECK(compareEpisodeNames("abc", "abc") == 0);
  CHECK(compareEpisodeNames("ep_00000000000000000000000002", "ep_3") < 0);  // no overflow on long runs
}

void testOsdText() {
  CHECK(osd("Bola de Drac") == "BOLA DE DRAC");
  CHECK(osd("Ñandú Àlex Çà") == "NANDU ALEX CA");
  CHECK(osd("Col·lecció") == "COL.LECCIO");      // Catalan middle dot
  CHECK(osd("L’Olla") == "L'OLLA");               // curly apostrophe
  CHECK(osd("TV \xF0\x9F\x93\xBA ok") == "TV ? OK");  // emoji: one '?'
  CHECK(osd("abcdefgh", 5) == "ABCD");             // truncated, always terminated
  CHECK(osd("bad \xC3") == "BAD ?");               // cut UTF-8 sequence
}

}  // namespace

// Channel 0 exists (a tutorial before channel 1); negative numbers do not.
static void testChannelZero() {
  ChannelManager cm;
  LoadReport r;
  CHECK(loadText(cm, R"({"channels": [
    {"id": "a", "number": 1, "name": "A", "type": "internal", "source": "testcard"},
    {"id": "t", "number": 0, "name": "T", "type": "local", "source": "/retrotv/media/tutorial"},
    {"id": "n", "number": -1, "name": "N", "type": "internal", "source": "testcard"}]})", r));
  CHECK(cm.count() == 2 && r.skipped == 1);
  CHECK(cm.at(0).number == 0);  // first in zapping order
  CHECK(cm.select(0) != nullptr && cm.current()->number == 0);
  CHECK(cm.prev()->number == 1 && cm.next()->number == 0);
}

// RETROTV Voice: the recorded messages are an internal channel with any number (docs/VOICE.md).
static void testMessagesChannel() {
  ChannelManager cm;
  LoadReport r;
  CHECK(loadText(cm, R"({"channels": [
    {"id": "a", "number": 1, "name": "A", "type": "internal", "source": "testcard"},
    {"id": "m", "number": 98, "name": "Mensajes", "type": "internal", "source": "messages"}]})", r));
  CHECK(cm.count() == 2 && r.skipped == 0);
  CHECK(cm.select(98) != nullptr && std::string(cm.current()->source) == INTERNAL_MESSAGES);
  CHECK(std::string(cm.current()->name) == "MENSAJES");
}

static void testSetEnabled() {
  ChannelManager cm;
  LoadReport r;
  CHECK(loadText(cm, R"({"channels": [
    {"id": "a", "number": 1, "name": "A", "type": "internal", "source": "testcard"},
    {"id": "b", "number": 2, "name": "B", "type": "internal", "source": "testcard"},
    {"id": "c", "number": 3, "name": "C", "type": "internal", "source": "testcard"}]})", r));
  cm.select(1);
  CHECK(cm.setEnabled(2, false));
  CHECK(cm.next()->number == 3);  // zapping skips the disabled one
  CHECK(cm.select(2) == nullptr);
  CHECK(cm.setEnabled(2, true) && cm.select(2) != nullptr);
  CHECK(!cm.setEnabled(9, true));
}

void runChannelTests() {
  testMessagesChannel();
  testChannelZero();
  testSetEnabled();
  testDefaultChannelsJson();
  testSortedAndZapping();
  testValidation();
  testUnusableFiles();
  testTooManyChannels();
  testOsdText();
  testIndexPath();
  testEpisodeOrder();
}
