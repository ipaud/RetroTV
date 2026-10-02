#pragma once

// RETROTV Server protocol, the parts that need no network: channel URLs, the session reply,
// HTTP/1.x response heads and chunked bodies. Pure C++ (+ ArduinoJson): tested on the host.
//
// A remote channel's source is "http://<server>[:port]/channel/<n>". Tuning in is
//   POST /api/sessions/<n>          -> {"session_id", "position_ms", "video", "audio"}
//   GET  <video>, GET <audio>       -> raw MJPEG and ADTS AAC from the same instant
// (server/README.md).

#include <stddef.h>
#include <stdint.h>

constexpr size_t REMOTE_HOST_LEN = 64;
constexpr size_t REMOTE_PATH_LEN = 96;
constexpr size_t REMOTE_SESSION_ID_LEN = 24;

struct HttpUrl {
  char host[REMOTE_HOST_LEN];
  uint16_t port;
  char path[REMOTE_PATH_LEN];
};

// "http://host[:port][/path]". False for anything else: https, no host, a bad port, too long.
bool parseHttpUrl(const char* url, HttpUrl& out);

// "/channel/<n>" (any path whose last segment is the channel number) -> "/api/sessions/<n>".
bool sessionPathFor(const char* channelPath, char* out, size_t len);

// Both checks at once: what channels.json validation needs.
bool isRemoteChannelUrl(const char* url);

// The server's number of a remote channel: 10 for "http://retrotv-server.local:8080/channel/10".
bool serverChannelNumber(const char* url, uint16_t& out);
// Both URLs on the same server (host, any case, and port).
bool sameServer(const char* a, const char* b);

// GET /api/guide: what is on now and next on the server's channels whose source publishes it
// (the teletext). Times are the server's epoch ms; titles uppercase ASCII, at most 39 characters.
constexpr size_t GUIDE_CHANNELS = 24;
constexpr size_t GUIDE_AIRINGS = 3;
constexpr size_t GUIDE_TITLE_LEN = 40;

struct GuideAiring {
  uint64_t startMs;
  uint64_t endMs;
  char title[GUIDE_TITLE_LEN];
};

struct GuideChannel {
  uint16_t number;  // the server's channel number
  uint8_t count;
  GuideAiring airings[GUIDE_AIRINGS];
};

struct RemoteGuide {
  uint64_t serverNowMs = 0;  // when the server answered
  size_t count = 0;
  GuideChannel channels[GUIDE_CHANNELS];

  const GuideChannel* find(uint16_t number) const;
};

// Airings without a title or with an end before their start are skipped. On false, `out` is
// left empty.
bool parseGuide(const char* json, size_t len, RemoteGuide& out);

constexpr uint8_t REMOTE_DEFAULT_FPS = 24;
constexpr uint32_t REMOTE_DEFAULT_PREBUFFER_MS = 1500;
constexpr uint32_t REMOTE_MIN_PREBUFFER_MS = 500;
constexpr uint32_t REMOTE_MAX_PREBUFFER_MS = 4000;

struct RemoteSession {
  char id[REMOTE_SESSION_ID_LEN];
  uint32_t positionMs;
  char video[REMOTE_PATH_LEN];
  char audio[REMOTE_PATH_LEN];  // "" when the channel has no audio
  uint8_t fps;                  // the video's frame rate: the server's live profile decides it
  uint32_t prebufferMs;         // video to hold before playing (clamped to 0.5-4 s)
};

// The JSON answer of POST /api/sessions/<n>. The stream paths must be absolute ("/...").
// "fps" and "prebuffer_ms" are optional (older servers): 24 fps and 1.5 s then.
bool parseSession(const char* json, size_t len, RemoteSession& out);

// Status line and headers of an HTTP/1.x response, fed one byte at a time.
class HttpHead {
 public:
  enum class State : uint8_t { Reading, Done, Error };

  State feed(char c);
  State state() const { return state_; }
  int status() const { return status_; }
  long contentLength() const { return contentLength_; }  // -1: not given
  bool chunked() const { return chunked_; }

 private:
  void endLine();

  static constexpr size_t HEAD_LINE_LEN = 128;  // longer lines are cut: only their start matters
  char line_[HEAD_LINE_LEN] = "";
  size_t len_ = 0;
  bool first_ = true;
  int status_ = 0;
  long contentLength_ = -1;
  bool chunked_ = false;
  State state_ = State::Reading;
};

// Transfer-Encoding: chunked (what uvicorn sends for a streaming response), decoded in place.
class ChunkedDecoder {
 public:
  // Decodes n raw bytes at buf and moves the payload to its front; returns the payload length.
  size_t feed(uint8_t* buf, size_t n);
  bool done() const { return phase_ == Phase::Done; }
  bool error() const { return phase_ == Phase::Error; }

 private:
  enum class Phase : uint8_t { Size, Extension, SizeLf, Data, DataCr, DataLf, Done, Error };
  Phase phase_ = Phase::Size;
  uint32_t remaining_ = 0;
  bool haveDigit_ = false;
};

// Counts JPEG starts (FF D8 FF) in a byte stream fed in arbitrary pieces, so the TV knows how
// many seconds of video it holds before playing (frames / fps), whatever the bitrate.
class JpegStartCounter {
 public:
  void feed(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      const uint8_t c = data[i];
      if (last2_ == 0xFF && last1_ == 0xD8 && c == 0xFF) ++count_;
      last2_ = last1_;
      last1_ = c;
    }
  }
  uint32_t count() const { return count_; }
  void reset() { *this = JpegStartCounter{}; }

 private:
  uint32_t count_ = 0;
  uint8_t last2_ = 0;
  uint8_t last1_ = 0;
};

// Automatic retries of a remote channel that failed: 2 s, 4 s, 8 s, then every 15 s.
inline uint32_t remoteRetryDelayMs(uint8_t attempt) {
  constexpr uint32_t first = 2000;
  constexpr uint32_t most = 15000;
  return attempt >= 3 ? most : first << attempt;
}
