#include "network/RemoteProtocol.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "channels/ChannelManager.h"

namespace {

constexpr const char* SCHEME = "http://";
constexpr const char* SESSIONS = "/api/sessions/";

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexValue(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Copies a JSON string member into out; "" for null. False if missing, too long or not a path.
bool copyPath(JsonVariantConst v, char* out, size_t len, bool optional) {
  if (v.isNull() && optional) {
    out[0] = '\0';
    return true;
  }
  const char* s = v.as<const char*>();
  if (s == nullptr || s[0] != '/' || strlen(s) >= len) return false;
  snprintf(out, len, "%s", s);
  return true;
}

}  // namespace

bool parseHttpUrl(const char* url, HttpUrl& out) {
  const size_t scheme = strlen(SCHEME);
  if (url == nullptr || strncasecmp(url, SCHEME, scheme) != 0) return false;
  const char* host = url + scheme;
  const char* hostEnd = host;
  while (*hostEnd != '\0' && *hostEnd != ':' && *hostEnd != '/') ++hostEnd;
  const size_t hostLen = static_cast<size_t>(hostEnd - host);
  if (hostLen == 0 || hostLen >= sizeof(out.host)) return false;

  long port = 80;
  const char* path = hostEnd;
  if (*hostEnd == ':') {
    const char* p = hostEnd + 1;
    port = 0;
    while (isDigit(*p) && port <= 65535) port = port * 10 + (*p++ - '0');
    if (p == hostEnd + 1 || port < 1 || port > 65535 || (*p != '\0' && *p != '/')) return false;
    path = p;
  }
  if (*path == '\0') path = "/";
  if (strlen(path) >= sizeof(out.path)) return false;

  memcpy(out.host, host, hostLen);
  out.host[hostLen] = '\0';
  out.port = static_cast<uint16_t>(port);
  snprintf(out.path, sizeof(out.path), "%s", path);
  return true;
}

bool sessionPathFor(const char* channelPath, char* out, size_t len) {
  const char* slash = strrchr(channelPath, '/');
  const char* number = slash != nullptr ? slash + 1 : channelPath;
  if (*number == '\0') return false;
  for (const char* p = number; *p != '\0'; ++p) {
    if (!isDigit(*p)) return false;
  }
  if (strlen(number) > 5) return false;
  const int n = snprintf(out, len, "%s%s", SESSIONS, number);
  return n > 0 && static_cast<size_t>(n) < len;
}

bool isRemoteChannelUrl(const char* url) {
  HttpUrl u;
  char session[REMOTE_PATH_LEN];
  return parseHttpUrl(url, u) && sessionPathFor(u.path, session, sizeof(session));
}

bool serverChannelNumber(const char* url, uint16_t& out) {
  HttpUrl u;
  char session[REMOTE_PATH_LEN];
  if (!parseHttpUrl(url, u) || !sessionPathFor(u.path, session, sizeof(session))) return false;
  const unsigned long n = strtoul(session + strlen(SESSIONS), nullptr, 10);
  if (n == 0 || n > UINT16_MAX) return false;
  out = static_cast<uint16_t>(n);
  return true;
}

bool sameServer(const char* a, const char* b) {
  HttpUrl ua, ub;
  return parseHttpUrl(a, ua) && parseHttpUrl(b, ub) && ua.port == ub.port && strcasecmp(ua.host, ub.host) == 0;
}

const GuideChannel* RemoteGuide::find(uint16_t number) const {
  for (size_t i = 0; i < count; ++i) {
    if (channels[i].number == number) return &channels[i];
  }
  return nullptr;
}

bool parseGuide(const char* json, size_t len, RemoteGuide& out) {
  out.count = 0;
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc["now_ms"].is<uint64_t>()) return false;
  out.serverNowMs = doc["now_ms"].as<uint64_t>();
  for (JsonObjectConst c : doc["channels"].as<JsonArrayConst>()) {
    if (out.count == GUIDE_CHANNELS) break;
    if (!c["number"].is<uint16_t>()) continue;
    GuideChannel& g = out.channels[out.count];
    g.number = c["number"];
    g.count = 0;
    for (JsonObjectConst a : c["airings"].as<JsonArrayConst>()) {
      if (g.count == GUIDE_AIRINGS) break;
      const char* title = a["title"] | "";
      const uint64_t start = a["start_ms"] | 0ull;
      const uint64_t end = a["end_ms"] | 0ull;
      if (title[0] == '\0' || end <= start) continue;
      GuideAiring& airing = g.airings[g.count++];
      airing.startMs = start;
      airing.endMs = end;
      toOsdText(title, airing.title, sizeof(airing.title));
    }
    if (g.count > 0) ++out.count;
  }
  return true;
}

bool parseSession(const char* json, size_t len, RemoteSession& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  const char* id = doc["session_id"].as<const char*>();
  if (id == nullptr || id[0] == '\0' || strlen(id) >= sizeof(out.id)) return false;
  if (!doc["position_ms"].is<uint32_t>()) return false;
  RemoteSession s;
  snprintf(s.id, sizeof(s.id), "%s", id);
  s.positionMs = doc["position_ms"].as<uint32_t>();
  if (!copyPath(doc["video"], s.video, sizeof(s.video), false)) return false;
  if (!copyPath(doc["audio"], s.audio, sizeof(s.audio), true)) return false;
  const JsonVariantConst fps = doc["fps"];
  if (!fps.isNull() && (!fps.is<int>() || fps.as<int>() < 1 || fps.as<int>() > 60)) return false;
  s.fps = fps.isNull() ? REMOTE_DEFAULT_FPS : static_cast<uint8_t>(fps.as<int>());
  const JsonVariantConst prebuffer = doc["prebuffer_ms"];
  uint32_t ms = REMOTE_DEFAULT_PREBUFFER_MS;
  if (!prebuffer.isNull()) {
    if (!prebuffer.is<uint32_t>()) return false;
    ms = prebuffer.as<uint32_t>();
  }
  s.prebufferMs = ms < REMOTE_MIN_PREBUFFER_MS ? REMOTE_MIN_PREBUFFER_MS : (ms > REMOTE_MAX_PREBUFFER_MS ? REMOTE_MAX_PREBUFFER_MS : ms);
  out = s;
  return true;
}

HttpHead::State HttpHead::feed(char c) {
  if (state_ != State::Reading) return state_;
  if (c == '\n') {
    endLine();
  } else if (c != '\r' && len_ + 1 < HEAD_LINE_LEN) {
    line_[len_++] = c;
  }
  return state_;
}

void HttpHead::endLine() {
  line_[len_] = '\0';
  const size_t len = len_;
  len_ = 0;
  if (first_) {  // "HTTP/1.1 200 OK"
    first_ = false;
    const char* space = strchr(line_, ' ');
    if (strncmp(line_, "HTTP/1.", 7) != 0 || space == nullptr) {
      state_ = State::Error;
      return;
    }
    status_ = atoi(space + 1);
    if (status_ < 100 || status_ > 599) state_ = State::Error;
    return;
  }
  if (len == 0) {  // the blank line: the body follows
    state_ = State::Done;
    return;
  }
  if (strncasecmp(line_, "content-length:", 15) == 0) {
    contentLength_ = atol(line_ + 15);
  } else if (strncasecmp(line_, "transfer-encoding:", 18) == 0) {
    for (const char* p = line_ + 18; *p != '\0'; ++p) {
      if (strncasecmp(p, "chunked", 7) == 0) chunked_ = true;
    }
  }
}

size_t ChunkedDecoder::feed(uint8_t* buf, size_t n) {
  size_t out = 0;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t c = buf[i];
    switch (phase_) {
      case Phase::Size: {
        const int v = hexValue(c);
        if (v >= 0) {
          if (remaining_ > 0x7FFFFFF) {  // one more digit would overflow: no real chunk is that big
            phase_ = Phase::Error;
            return out;
          }
          remaining_ = remaining_ * 16 + static_cast<uint32_t>(v);
          haveDigit_ = true;
        } else if (haveDigit_ && (c == ';' || c == ' ')) {
          phase_ = Phase::Extension;
        } else if (haveDigit_ && c == '\r') {
          phase_ = Phase::SizeLf;
        } else {
          phase_ = Phase::Error;
          return out;
        }
        break;
      }
      case Phase::Extension:
        if (c == '\r') phase_ = Phase::SizeLf;
        break;
      case Phase::SizeLf:
        if (c != '\n') {
          phase_ = Phase::Error;
          return out;
        }
        haveDigit_ = false;
        if (remaining_ == 0) {  // the last chunk; trailers, if any, are not needed
          phase_ = Phase::Done;
          return out;
        }
        phase_ = Phase::Data;
        break;
      case Phase::Data: {
        const size_t take = (n - i) < remaining_ ? (n - i) : remaining_;
        memmove(buf + out, buf + i, take);
        out += take;
        remaining_ -= static_cast<uint32_t>(take);
        i += take - 1;
        if (remaining_ == 0) phase_ = Phase::DataCr;
        break;
      }
      case Phase::DataCr:
        phase_ = c == '\r' ? Phase::DataLf : Phase::Error;
        if (phase_ == Phase::Error) return out;
        break;
      case Phase::DataLf:
        phase_ = c == '\n' ? Phase::Size : Phase::Error;
        if (phase_ == Phase::Error) return out;
        break;
      case Phase::Done:
      case Phase::Error:
        return out;
    }
  }
  return out;
}
