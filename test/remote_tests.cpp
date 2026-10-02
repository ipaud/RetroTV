// Host tests: remote channels (RETROTV Server protocol, byte ring, blocking read, retries).

#include <cstring>
#include <string>
#include <vector>

#include "channels/ChannelManager.h"
#include "check.h"
#include "network/ByteRing.h"
#include "network/RemoteProtocol.h"

namespace {

void testParseHttpUrl() {
  HttpUrl u;
  CHECK(parseHttpUrl("http://retrotv-server.local:8080/channel/10", u));
  CHECK(std::string(u.host) == "retrotv-server.local" && u.port == 8080 && std::string(u.path) == "/channel/10");
  CHECK(parseHttpUrl("http://192.168.1.10/channel/1", u) && u.port == 80 && std::string(u.host) == "192.168.1.10");
  CHECK(parseHttpUrl("HTTP://host", u) && std::string(u.path) == "/");
  CHECK(!parseHttpUrl("https://host/channel/1", u));  // no TLS on the TV
  CHECK(!parseHttpUrl("http://:8080/channel/1", u));
  CHECK(!parseHttpUrl("http://host:0/x", u));
  CHECK(!parseHttpUrl("http://host:65536/x", u));
  CHECK(!parseHttpUrl("http://host:80a/x", u));
  CHECK(!parseHttpUrl("/retrotv/media", u));
  CHECK(!parseHttpUrl(std::string("http://" + std::string(80, 'h') + "/x").c_str(), u));
}

void testSessionPath() {
  char out[REMOTE_PATH_LEN];
  CHECK(sessionPathFor("/channel/10", out, sizeof(out)) && std::string(out) == "/api/sessions/10");
  CHECK(sessionPathFor("/ch/1", out, sizeof(out)) && std::string(out) == "/api/sessions/1");
  CHECK(!sessionPathFor("/channel/", out, sizeof(out)));
  CHECK(!sessionPathFor("/channel/sx3", out, sizeof(out)));
  CHECK(!sessionPathFor("/", out, sizeof(out)));
  char tiny[8];
  CHECK(!sessionPathFor("/channel/1", tiny, sizeof(tiny)));  // never overflows
  CHECK(isRemoteChannelUrl("http://retrotv-server.local:8080/channel/1"));
  CHECK(!isRemoteChannelUrl("http://retrotv-server.local:8080/"));
}

void testRemoteChannelValidation() {
  ChannelManager cm;
  LoadReport r;
  JsonDocument doc;
  deserializeJson(doc, R"({"channels":[
    {"id":"ok","number":20,"name":"Remote Demo","type":"remote","source":"http://retrotv-server.local:8080/channel/1"},
    {"id":"tls","number":21,"name":"TLS","type":"remote","source":"https://x/channel/1"},
    {"id":"nonum","number":22,"name":"No number","type":"remote","source":"http://x/channel/demo"},
    {"id":"hls","number":23,"name":"Later","type":"hls-proxy","source":"http://x/whatever"}]})");
  CHECK(cm.load(doc.as<JsonVariantConst>(), r));
  CHECK(cm.count() == 2 && r.skipped == 2);  // hls-proxy keeps its V0.2 placeholder rules
  CHECK(cm.at(0).type == ChannelType::Remote && std::string(cm.at(0).name) == "REMOTE DEMO");
  CHECK(std::string(r.firstProblem).find("remote source") != std::string::npos);
}

void testParseSession() {
  RemoteSession s;
  const char* ok =
      R"({"session_id":"8cf02a1b","channel":1,"position_ms":123000,"video":"/session/8cf02a1b/video","audio":"/session/8cf02a1b/audio"})";
  CHECK(parseSession(ok, strlen(ok), s));
  CHECK(std::string(s.id) == "8cf02a1b" && s.positionMs == 123000);
  CHECK(std::string(s.video) == "/session/8cf02a1b/video" && std::string(s.audio) == "/session/8cf02a1b/audio");
  CHECK(s.fps == REMOTE_DEFAULT_FPS && s.prebufferMs == REMOTE_DEFAULT_PREBUFFER_MS);  // older server
  const char* silent = R"({"session_id":"a","position_ms":0,"video":"/v","audio":null})";
  CHECK(parseSession(silent, strlen(silent), s) && s.audio[0] == '\0');
  const char* profile = R"({"session_id":"a","position_ms":0,"video":"/v","fps":20,"prebuffer_ms":2500})";
  CHECK(parseSession(profile, strlen(profile), s) && s.fps == 20 && s.prebufferMs == 2500);
  const char* extremes = R"({"session_id":"a","position_ms":0,"video":"/v","prebuffer_ms":99999})";
  CHECK(parseSession(extremes, strlen(extremes), s) && s.prebufferMs == REMOTE_MAX_PREBUFFER_MS);
  const char* tiny = R"({"session_id":"a","position_ms":0,"video":"/v","prebuffer_ms":10})";
  CHECK(parseSession(tiny, strlen(tiny), s) && s.prebufferMs == REMOTE_MIN_PREBUFFER_MS);
  const char* bad[] = {
      "not json",
      R"({"position_ms":0,"video":"/v"})",                            // no id
      R"({"session_id":"a","position_ms":-5,"video":"/v"})",          // negative position
      R"({"session_id":"a","position_ms":0,"video":"http://x/v"})",   // not a path on this server
      R"({"session_id":"a","position_ms":0})",                        // no video
      R"({"session_id":"a","position_ms":0,"video":"/v","fps":0})",   // no frame rate
      R"({"session_id":"a","position_ms":0,"video":"/v","fps":120})", // not a TV frame rate
      R"({"session_id":"a","position_ms":0,"video":"/v","fps":"24"})",
      R"({"session_id":"a","position_ms":0,"video":"/v","prebuffer_ms":-1})",
  };
  for (const char* b : bad) CHECK(!parseSession(b, strlen(b), s));
}

HttpHead feedAll(const char* text) {
  HttpHead h;
  for (const char* p = text; *p != '\0' && h.state() == HttpHead::State::Reading; ++p) h.feed(*p);
  return h;
}

void testHttpHead() {
  HttpHead h = feedAll("HTTP/1.1 200 OK\r\ncontent-type: video/x-motion-jpeg\r\nTransfer-Encoding: chunked\r\n\r\nBODY");
  CHECK(h.state() == HttpHead::State::Done && h.status() == 200 && h.chunked() && h.contentLength() == -1);
  h = feedAll("HTTP/1.0 404 Not Found\nContent-Length: 42\n\n");
  CHECK(h.state() == HttpHead::State::Done && h.status() == 404 && !h.chunked() && h.contentLength() == 42);
  CHECK(feedAll("HTTP/1.1 200 OK\r\nx: 1\r\n").state() == HttpHead::State::Reading);  // not over yet
  CHECK(feedAll("SSH-2.0-OpenSSH\r\n").state() == HttpHead::State::Error);
  CHECK(feedAll("HTTP/1.1 abc\r\n").state() == HttpHead::State::Error);
  const std::string longHeader = "HTTP/1.1 200 OK\r\nx-long: " + std::string(500, 'a') + "\r\n\r\n";
  CHECK(feedAll(longHeader.c_str()).state() == HttpHead::State::Done);  // cut, never overflows
}

std::string decode(ChunkedDecoder& d, std::string raw, size_t piece) {
  std::string out;
  for (size_t at = 0; at < raw.size(); at += piece) {
    std::vector<uint8_t> buf(raw.begin() + at, raw.begin() + std::min(raw.size(), at + piece));
    const size_t n = d.feed(buf.data(), buf.size());
    out.append(buf.begin(), buf.begin() + n);
  }
  return out;
}

void testChunked() {
  const std::string raw = "4\r\nWiki\r\n6;ext=1\r\npedia \r\nE\r\nin \r\n\r\nchunks.\r\n0\r\n\r\n";
  for (size_t piece : {1u, 3u, 7u, 1000u}) {  // any split of the socket reads
    ChunkedDecoder d;
    CHECK(decode(d, raw, piece) == "Wikipedia in \r\n\r\nchunks.");
    CHECK(d.done() && !d.error());
  }
  ChunkedDecoder bad;
  decode(bad, "zz\r\n", 100);
  CHECK(bad.error());
  ChunkedDecoder missingCrlf;
  decode(missingCrlf, "2\r\nabX", 100);
  CHECK(missingCrlf.error());
  ChunkedDecoder huge;
  decode(huge, "FFFFFFFFFF\r\n", 100);
  CHECK(huge.error());  // never overflows the size
}

void testRing() {
  uint8_t mem[16];
  ByteRing ring;
  ring.attach(mem, sizeof(mem), 0xFFFFFFF0u);  // counters wrap past 2^32 during this test
  const std::string text = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string out;
  size_t in = 0;
  while (out.size() < text.size()) {
    in += ring.push(reinterpret_cast<const uint8_t*>(text.data()) + in, static_cast<uint32_t>(std::min<size_t>(5, text.size() - in)));
    CHECK(ring.size() <= ring.capacity());
    uint8_t got[3];
    const uint32_t n = ring.pop(got, sizeof(got));
    out.append(reinterpret_cast<char*>(got), n);
  }
  CHECK(out == text);
  CHECK(ring.size() == 0 && ring.space() == 16);
  uint8_t fill[20] = {};
  CHECK(ring.push(fill, sizeof(fill)) == 16);  // never past capacity
  CHECK(ring.push(fill, 1) == 0);
  ring.clear();
  CHECK(ring.size() == 0);
}

struct FakeTime {
  uint32_t now = 0;
  int sleeps = 0;
};

void testReadWaiting() {
  uint8_t mem[64];
  ByteRing ring;
  ring.attach(mem, sizeof(mem));
  std::atomic<bool> ended{false};
  std::atomic<bool> cancelled{false};
  FakeTime t;
  auto now = [&] { return t.now; };
  uint8_t out[8];
  uint32_t got = 0;

  // Data arrives while waiting.
  auto dataLater = [&] {
    t.now += 2;
    if (++t.sleeps == 3) ring.push(reinterpret_cast<const uint8_t*>("hi"), 2);
  };
  CHECK(readWaiting(ring, out, sizeof(out), got, ended, cancelled, 3000, now, dataLater) == RingRead::Data);
  CHECK(got == 2 && memcmp(out, "hi", 2) == 0 && t.sleeps == 3);

  // Cancel while waiting (stop() during a read): returns at the next slice, no data lost later.
  t = FakeTime{};
  auto cancelLater = [&] {
    t.now += 2;
    if (++t.sleeps == 2) cancelled = true;
  };
  CHECK(readWaiting(ring, out, sizeof(out), got, ended, cancelled, 3000, now, cancelLater) == RingRead::Cancelled);
  CHECK(t.sleeps == 2);
  cancelled = false;

  // Nothing for stallMs: the connection is lost.
  t = FakeTime{};
  auto idle = [&] { t.now += 2; };
  CHECK(readWaiting(ring, out, sizeof(out), got, ended, cancelled, 3000, now, idle) == RingRead::Stalled);
  CHECK(t.now >= 3000 && t.now < 3010);

  // End of body: what was pushed before the end is still read, then Ended.
  ring.push(reinterpret_cast<const uint8_t*>("xyz"), 3);
  ended = true;
  CHECK(readWaiting(ring, out, sizeof(out), got, ended, cancelled, 3000, now, idle) == RingRead::Data && got == 3);
  CHECK(readWaiting(ring, out, sizeof(out), got, ended, cancelled, 3000, now, idle) == RingRead::Ended);
}

void testJpegStartCounter() {
  std::string stream;
  for (int i = 0; i < 5; ++i) stream += std::string("\xFF\xD8\xFF\xE0", 4) + "frame" + std::string("\xFF\xD9", 2);
  stream += std::string("\xFF\xD8\xFE", 3);  // not a JPEG start (third byte)
  for (size_t piece : {1u, 2u, 3u, 7u, 1000u}) {  // markers split across socket reads
    JpegStartCounter c;
    for (size_t at = 0; at < stream.size(); at += piece) {
      c.feed(reinterpret_cast<const uint8_t*>(stream.data()) + at, std::min(piece, stream.size() - at));
    }
    CHECK(c.count() == 5);
  }
  JpegStartCounter c;
  c.feed(reinterpret_cast<const uint8_t*>("\xFF\xD8"), 2);
  c.reset();  // a new session: a half marker from the old one never counts
  c.feed(reinterpret_cast<const uint8_t*>("\xFF"), 1);
  CHECK(c.count() == 0);
}

void testRetryDelays() {
  CHECK(remoteRetryDelayMs(0) == 2000 && remoteRetryDelayMs(1) == 4000 && remoteRetryDelayMs(2) == 8000);
  CHECK(remoteRetryDelayMs(3) == 15000 && remoteRetryDelayMs(255) == 15000);
}

void testServerChannelNumber() {
  uint16_t n = 0;
  CHECK(serverChannelNumber("http://retrotv-server.local:8080/channel/10", n) && n == 10);
  CHECK(!serverChannelNumber("http://retrotv-server.local:8080/channel/", n));
  CHECK(!serverChannelNumber("http://retrotv-server.local:8080/channel/99999", n));  // above uint16
  CHECK(!serverChannelNumber("/retrotv/media/x", n));
  CHECK(sameServer("http://RETROTV-SERVER.local:8080/channel/10", "http://retrotv-server.local:8080/channel/17"));
  CHECK(!sameServer("http://retrotv-server.local:8080/channel/10", "http://retrotv-server.local/channel/10"));
  CHECK(!sameServer("http://retrotv-server.local:8080/channel/10", "http://192.168.1.29:8080/channel/10"));
}

void testParseGuide() {
  static RemoteGuide guide;  // a few KB: not on the test's stack
  const char* json = R"({"now_ms": 1790770620000, "channels": [
    {"number": 10, "airings": [
      {"title": "Bola de drac Z - Uns quarts de final d'all\u00f2 m\u00e9s sorprenents", "start_ms": 1790770273000, "end_ms": 1790771611000},
      {"title": "El combat", "start_ms": 1790771611000, "end_ms": 1790772948000}]},
    {"number": 17, "airings": [
      {"title": "One Piece - El Capit\u00e1n Usuff", "start_ms": 1790770389000, "end_ms": 1790771957000},
      {"title": "", "start_ms": 1, "end_ms": 2},
      {"title": "Backwards", "start_ms": 5, "end_ms": 4},
      {"title": "A", "start_ms": 1790771957000, "end_ms": 1790773500000},
      {"title": "B", "start_ms": 1790773500000, "end_ms": 1790775000000},
      {"title": "C", "start_ms": 1790775000000, "end_ms": 1790776000000}]}]})";
  CHECK(parseGuide(json, strlen(json), guide));
  CHECK(guide.serverNowMs == 1790770620000ull && guide.count == 2);
  const GuideChannel* sx3 = guide.find(10);
  CHECK(sx3 != nullptr && sx3->count == 2);
  CHECK(strcmp(sx3->airings[0].title, "BOLA DE DRAC Z - UNS QUARTS DE FINAL D'") == 0);  // ASCII, one row
  CHECK(sx3->airings[1].startMs == 1790771611000ull && sx3->airings[1].endMs == 1790772948000ull);
  const GuideChannel* op = guide.find(17);
  CHECK(op != nullptr && op->count == GUIDE_AIRINGS);  // bad airings skipped, the rest capped
  CHECK(strcmp(op->airings[0].title, "ONE PIECE - EL CAPITAN USUFF") == 0 && strcmp(op->airings[2].title, "B") == 0);
  CHECK(guide.find(11) == nullptr);

  CHECK(!parseGuide("not json", 8, guide) && guide.count == 0);  // a bad answer leaves no stale guide
  const char* noNow = R"({"channels": []})";
  CHECK(!parseGuide(noNow, strlen(noNow), guide));
}

}  // namespace

void runRemoteTests() {
  testParseHttpUrl();
  testSessionPath();
  testRemoteChannelValidation();
  testParseSession();
  testHttpHead();
  testChunked();
  testRing();
  testReadWaiting();
  testJpegStartCounter();
  testRetryDelays();
  testServerChannelNumber();
  testParseGuide();
}
