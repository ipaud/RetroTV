#include "network/RemoteSource.h"

#include <esp_heap_caps.h>
#include "network/Mdns.h"
#include <strings.h>

#include "app_types.h"
#include "config.h"

namespace {

constexpr uint32_t IDLE_POLL_MS = 10;
constexpr size_t IO_CHUNK = 4096;
constexpr size_t SESSION_JSON_MAX = 512;
constexpr size_t GUIDE_JSON_MAX = 8192;  // the server sends 3 airings of <= 64 characters per channel
constexpr const char* GUIDE_PATH = "/api/guide";
constexpr const char* LOCAL_SUFFIX = ".local";

bool isLocalName(const char* host) {
  const size_t len = strlen(host);
  const size_t suffix = strlen(LOCAL_SUFFIX);
  return len > suffix && strcasecmp(host + len - suffix, LOCAL_SUFFIX) == 0;
}

}  // namespace

bool RemoteSource::begin() {
  io_ = static_cast<uint8_t*>(heap_caps_malloc(IO_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  guideJson_ = static_cast<char*>(heap_caps_malloc(GUIDE_JSON_MAX, MALLOC_CAP_SPIRAM));
  if (io_ == nullptr || guideJson_ == nullptr || !video_.begin(REMOTE_VIDEO_RING) || !audio_.begin(REMOTE_AUDIO_RING)) {
    return false;
  }
  started_ = xTaskCreatePinnedToCore(taskEntry, "net", NET_TASK_STACK, this, NET_TASK_PRIO, nullptr, NET_TASK_CORE) ==
             pdPASS;
  return started_;
}

void RemoteSource::tune(const char* url, bool retry) {
  portENTER_CRITICAL(&lock_);
  snprintf(url_, sizeof(url_), "%s", url);
  wanted_ = wanted_ + 1;
  portEXIT_CRITICAL(&lock_);
  if (retry) ++reconnects_;
}

void RemoteSource::cancel() {
  video_.cancel();
  audio_.cancel();
}

void RemoteSource::close() {
  portENTER_CRITICAL(&lock_);
  url_[0] = '\0';
  wanted_ = wanted_ + 1;
  portEXIT_CRITICAL(&lock_);
}

void RemoteSource::requestGuide(const char* channelUrl) {
  portENTER_CRITICAL(&lock_);
  snprintf(guideUrl_, sizeof(guideUrl_), "%s", channelUrl);
  portEXIT_CRITICAL(&lock_);
  guideReady_ = false;
  guideWanted_ = true;
}

bool RemoteSource::takeGuide(const char*& json, size_t& len) {
  if (!guideReady_.exchange(false)) return false;
  json = guideJson_;
  len = guideLen_;
  return true;
}

RemoteSource::State RemoteSource::state() const {
  if (stateGeneration_.load() != wanted_.load()) return State::Connecting;  // not picked up yet
  return static_cast<State>(state_.load());
}

const char* RemoteSource::failureText(Failure f) {
  switch (f) {
    case Failure::NoServer: return "NO SERVER";
    case Failure::NoChannel: return "NO CHANNEL";
    case Failure::ChannelOff: return "CHANNEL OFF";
    case Failure::BadReply: return "SERVER ERROR";
    case Failure::NoData: return "NO DATA";
    case Failure::None: break;
  }
  return "NO SIGNAL";
}

RemoteSource::Stats RemoteSource::stats() const {
  Stats s;
  s.bytes = bytes_.load();
  s.waitedMs = video_.waitedMs() + audio_.waitedMs();
  s.stalls = video_.stalls() + audio_.stalls();
  s.sessions = sessions_.load();
  s.reconnects = reconnects_.load();
  s.videoBuffered = video_.buffered();
  s.audioBuffered = audio_.buffered();
  s.videoCapacity = video_.capacity();
  s.audioCapacity = audio_.capacity();
  s.underruns = video_.underruns() + audio_.underruns();
  s.passesData = passesData_.load();
  s.passesIdle = passesIdle_.load();
  s.passesFull = passesFull_.load();
  return s;
}

void RemoteSource::taskEntry(void* self) { static_cast<RemoteSource*>(self)->run(); }

void RemoteSource::publish(uint32_t generation, State s, Failure f) {
  failure_ = static_cast<uint8_t>(f);
  state_ = static_cast<uint8_t>(s);
  stateGeneration_ = generation;  // last: the loop task reads the generation first
}

// Only this task touches the sockets. A new tune() or a close() is picked up between steps, so
// zapping never waits for more than one bounded step (a connect or a reply timeout).
void RemoteSource::run() {
  for (;;) {
    const uint32_t wanted = wanted_.load();
    if (wanted != active_) {
      hangUp();
      active_ = wanted;
      char url[CHANNEL_SOURCE_LEN];
      portENTER_CRITICAL(&lock_);
      memcpy(url, url_, sizeof(url));
      portEXIT_CRITICAL(&lock_);
      if (url[0] == '\0') {
        publish(wanted, State::Idle, Failure::None);
        continue;
      }
      publish(wanted, State::Connecting, Failure::None);
      const Failure f = connectAll(wanted, url);
      if (superseded(wanted)) continue;
      if (f != Failure::None) {
        hangUp();
        publish(wanted, State::Failed, f);
        continue;
      }
      streaming_ = true;
      streamingSinceMs_ = millis();
      lastVideoDataMs_ = streamingSinceMs_;
      continue;
    }
    if (!streaming_) {
      if (guideWanted_.exchange(false)) {
        fetchGuide();
      } else {
        vTaskDelay(pdMS_TO_TICKS(IDLE_POLL_MS));
      }
      continue;
    }
    pump(videoConn_);
    pump(audioConn_);
    if (!ready_ && primed()) {
      ready_ = true;
      PLOG("REMOTE", "buffered %u KB video (%u frames = %u ms at %u fps), %u B audio in %lu ms",
           static_cast<unsigned>(video_.buffered() / 1024), static_cast<unsigned>(videoFrames_.count()),
           static_cast<unsigned>(videoFrames_.count() * 1000u / fps_.load()), static_cast<unsigned>(fps_.load()),
           static_cast<unsigned>(audio_.buffered()), millis() - streamingSinceMs_);
      publish(active_, State::Ready, Failure::None);
    } else if (!ready_ && millis() - streamingSinceMs_ >= primeTimeoutMs()) {
      PLOG("REMOTE", "not enough data within %u ms (%u frames)", static_cast<unsigned>(primeTimeoutMs()),
           static_cast<unsigned>(videoFrames_.count()));
      hangUp();
      publish(active_, State::Failed, Failure::NoData);
    }
    vTaskDelay(1);  // at least one tick per pass: IDLE0 and the decoder always get the core
  }
}

RemoteSource::Failure RemoteSource::connectAll(uint32_t generation, const char* url) {
  HttpUrl u;
  if (!parseHttpUrl(url, u)) return Failure::NoChannel;  // channels.json already checked it
  IPAddress ip;
  if (!resolve(u.host, ip)) {
    PLOG("SERVER", "cannot resolve %s", u.host);
    return Failure::NoServer;
  }
  if (superseded(generation)) return Failure::None;

  RemoteSession session;
  const Failure f = createSession(generation, ip, u, session);
  if (f != Failure::None || superseded(generation)) {
    if (f == Failure::NoServer) cachedHost_[0] = '\0';  // resolve again next time
    return f;
  }
  hasAudio_ = session.audio[0] != '\0';
  positionMs_ = session.positionMs;
  fps_ = session.fps;
  prebufferMs_ = session.prebufferMs;
  const Failure fv = openStream(generation, videoConn_, ip, u, session.video);
  if (fv != Failure::None || superseded(generation)) return fv;
  if (hasAudio_) return openStream(generation, audioConn_, ip, u, session.audio);
  return Failure::None;
}

// IP literals as they are; "<name>.local" over mDNS; anything else through DNS. The last answer
// is kept, so zapping between remote channels does not ask again each time.
bool RemoteSource::resolve(const char* host, IPAddress& ip) {
  if (ip.fromString(host)) return true;
  if (strcmp(host, cachedHost_) == 0) {
    ip = cachedIp_;
    return true;
  }
  if (isLocalName(host)) {
    char name[REMOTE_HOST_LEN];
    snprintf(name, sizeof(name), "%.*s", static_cast<int>(strlen(host) - strlen(LOCAL_SUFFIX)), host);
    esp_ip4_addr_t addr{};
    if (!mdnsStarted() || mdns_query_a(name, REMOTE_MDNS_TIMEOUT_MS, &addr) != ESP_OK) return false;
    ip = IPAddress(addr.addr);
  } else if (WiFi.hostByName(host, ip) != 1) {
    return false;
  }
  snprintf(cachedHost_, sizeof(cachedHost_), "%s", host);
  cachedIp_ = ip;
  return true;
}

RemoteSource::Failure RemoteSource::createSession(uint32_t generation, const IPAddress& ip, const HttpUrl& u,
                                                  RemoteSession& s) {
  char path[REMOTE_PATH_LEN];
  sessionPathFor(u.path, path, sizeof(path));
  WiFiClient client;
  if (!client.connect(ip, u.port, REMOTE_CONNECT_TIMEOUT_MS)) {
    PLOG("SERVER", "%s:%u unreachable", ip.toString().c_str(), u.port);
    return Failure::NoServer;
  }
  client.printf("POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", path, u.host,
                u.port);
  HttpHead head;
  if (!readHead(generation, client, head, REMOTE_SESSION_TIMEOUT_MS)) return Failure::BadReply;
  if (head.status() == 404) return Failure::NoChannel;
  if (head.status() == 503) return Failure::ChannelOff;
  if (head.status() != 200 || head.chunked()) return Failure::BadReply;

  char json[SESSION_JSON_MAX];
  const size_t len = readBody(generation, client, head, json, sizeof(json));
  client.stop();
  if (!parseSession(json, len, s)) return Failure::BadReply;
  ++sessions_;
  PLOG("SERVER", "connected %s:%u", ip.toString().c_str(), u.port);
  PLOG("REMOTE", "session %s created, channel at %lu ms, %u fps, prebuffer %lu ms", s.id,
       static_cast<unsigned long>(s.positionMs), static_cast<unsigned>(s.fps), static_cast<unsigned long>(s.prebufferMs));
  return Failure::None;
}

RemoteSource::Failure RemoteSource::openStream(uint32_t generation, Conn& c, const IPAddress& ip, const HttpUrl& u,
                                               const char* path) {
  if (!c.client.connect(ip, u.port, REMOTE_CONNECT_TIMEOUT_MS)) return Failure::NoServer;
  c.client.setNoDelay(true);
  c.client.printf("GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n", path, u.host, u.port);
  HttpHead head;
  if (!readHead(generation, c.client, head, REMOTE_REPLY_TIMEOUT_MS) || head.status() != 200) return Failure::BadReply;
  c.chunked = ChunkedDecoder{};
  c.isChunked = head.chunked();
  c.left = head.contentLength();
  c.open = true;
  PLOG("REMOTE", "%s connected%s", c.name, c.isChunked ? " (chunked)" : "");
  return Failure::None;
}

// A small body (not chunked) after its head, into out; 0 when it does not fit or does not come.
size_t RemoteSource::readBody(uint32_t generation, WiFiClient& client, const HttpHead& head, char* out, size_t cap) {
  const long declared = head.contentLength();
  if (declared >= static_cast<long>(cap)) return 0;
  const size_t want = declared >= 0 ? static_cast<size_t>(declared) : cap - 1;
  size_t len = 0;
  const uint32_t t0 = millis();
  while (len < want && len < cap - 1 && millis() - t0 < REMOTE_REPLY_TIMEOUT_MS && !superseded(generation)) {
    const int n = client.read(reinterpret_cast<uint8_t*>(out) + len, cap - 1 - len);
    if (n > 0) {
      len += static_cast<size_t>(n);
    } else if (!client.connected()) {
      break;
    } else {
      vTaskDelay(1);
    }
  }
  return declared >= 0 && len != want ? 0 : len;
}

// Network task, nothing streaming. Any tune that comes in meanwhile cuts it short.
void RemoteSource::fetchGuide() {
  char url[CHANNEL_SOURCE_LEN];
  portENTER_CRITICAL(&lock_);
  memcpy(url, guideUrl_, sizeof(url));
  portEXIT_CRITICAL(&lock_);
  const uint32_t generation = active_;
  HttpUrl u;
  IPAddress ip;
  if (!parseHttpUrl(url, u) || !resolve(u.host, ip)) return;
  WiFiClient client;
  if (superseded(generation) || !client.connect(ip, u.port, REMOTE_CONNECT_TIMEOUT_MS)) return;
  client.printf("GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n", GUIDE_PATH, u.host, u.port);
  HttpHead head;
  size_t len = 0;
  if (readHead(generation, client, head, REMOTE_REPLY_TIMEOUT_MS) && head.status() == 200 && !head.chunked()) {
    len = readBody(generation, client, head, guideJson_, GUIDE_JSON_MAX);
  }
  client.stop();
  if (len == 0 || superseded(generation) || guideWanted_.load()) {
    PLOG("SERVER", "no guide from %s:%u", u.host, u.port);
    return;
  }
  guideLen_ = len;
  guideReady_ = true;
}

// One byte at a time, so nothing of the body is read here. Gives up at once if a new tune or a
// close came in meanwhile.
bool RemoteSource::readHead(uint32_t generation, WiFiClient& client, HttpHead& head, uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs && !superseded(generation)) {
    if (client.available() > 0) {
      if (head.feed(static_cast<char>(client.read())) != HttpHead::State::Reading) break;
    } else if (!client.connected()) {
      break;
    } else {
      vTaskDelay(1);
    }
  }
  return head.state() == HttpHead::State::Done;
}

// Moves what the socket has into the ring, never more than fits: a full ring leaves the data in
// the socket, and TCP flow control slows the server down.
void RemoteSource::pump(Conn& c) {
  if (!c.open) return;
  const bool video = &c == &videoConn_;
  const uint32_t space = c.stream->ring().space();
  if (space == 0) {
    if (video) {
      ++passesFull_;
      lastVideoDataMs_ = millis();  // waiting on the player, not on the network: no gap
    }
    return;
  }
  const int available = c.client.available();
  if (available <= 0) {
    if (video) {
      ++passesIdle_;
      const uint32_t gap = millis() - lastVideoDataMs_;
      if (gap > gapMaxMs_.load()) gapMaxMs_ = gap;
    }
    if (!c.client.connected()) finish(c, "closed by the server");
    return;
  }
  size_t want = static_cast<size_t>(available);
  if (want > space) want = space;  // chunked framing only shrinks: the payload always fits
  if (want > IO_CHUNK) want = IO_CHUNK;
  if (c.left >= 0 && want > static_cast<size_t>(c.left)) want = static_cast<size_t>(c.left);
  const int n = c.client.read(io_, want);
  if (n <= 0) return;
  bytes_ += static_cast<uint32_t>(n);
  size_t payload = static_cast<size_t>(n);
  if (c.isChunked) payload = c.chunked.feed(io_, payload);
  c.stream->ring().push(io_, static_cast<uint32_t>(payload));
  if (video) {
    ++passesData_;
    lastVideoDataMs_ = millis();
    videoFrames_.feed(io_, payload);
  }
  if (c.left >= 0) c.left -= n;
  if (c.chunked.error()) {
    finish(c, "bad chunk");
  } else if (c.chunked.done() || c.left == 0) {
    finish(c, "complete");
  }
}

void RemoteSource::finish(Conn& c, const char* why) {
  c.open = false;
  c.client.stop();
  c.stream->end();
  PLOG("REMOTE", "%s ended: %s", c.name, why);
}

// Enough video to ride out the Wi-Fi: prebuffer_ms of frames (the server's setting), or a ring
// that is nearly full (a stream too heavy for the prebuffer to fit).
bool RemoteSource::primed() const {
  const uint32_t frames = static_cast<uint32_t>(prebufferMs_.load()) * fps_.load() / 1000u;
  const bool videoIn = video_.ended() || videoFrames_.count() >= (frames > 0 ? frames : 1) ||
                       video_.capacity() - video_.buffered() < REMOTE_RING_HEADROOM;
  const bool audioIn = !hasAudio_ || audio_.ended() || audio_.buffered() >= REMOTE_PRIME_AUDIO;
  return videoIn && audioIn;
}

// Longer prebuffers get longer to fill, over a Wi-Fi that may be slow for a while.
uint32_t RemoteSource::primeTimeoutMs() const {
  const uint32_t ms = REMOTE_PRIME_TIMEOUT_MS + 2u * prebufferMs_.load();
  return ms < REMOTE_PRIME_TIMEOUT_MAX_MS ? ms : REMOTE_PRIME_TIMEOUT_MAX_MS;
}

// Closing the sockets is what tells the server the session is over.
void RemoteSource::hangUp() {
  for (Conn* c : {&videoConn_, &audioConn_}) {
    c->client.stop();
    c->open = false;
    c->stream->reset();
  }
  streaming_ = false;
  ready_ = false;
  videoFrames_.reset();
}
