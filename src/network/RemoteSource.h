#pragma once

// Remote channels: tunes in to RETROTV Server and keeps two HttpStreams full, so the App can
// hand them to MediaPlayer exactly like two files. All network work happens on one task
// (core 0), so a slow server or a dead Wi-Fi never blocks the loop, the display or the audio:
//
//   tune(url) -> [net task] resolve host (mDNS for *.local) -> POST /api/sessions/<n>
//             -> GET video, GET audio (same t0) -> pump both sockets into PSRAM rings
//             -> Ready once ~250 ms are buffered -> the App starts MediaPlayer
//
// WiFiClient directly, not HTTPClient: each connection needs a connect timeout, a bounded wait
// for its head, incremental non-blocking reads that the stop can interrupt, and chunked bodies
// decoded as they stream. HTTPClient's stream access does not give all of those together.
//
// While no channel streams, the same task also fetches the server's guide for the teletext
// (GET /api/guide): a small JSON answer, dropped as soon as a tune comes in.
//
// A live channel's session answers only once the server's FFmpeg produces data (seconds): that
// wait is long (REMOTE_SESSION_TIMEOUT_MS) but never blocks a zap, it stops at the next tune.

#include <WiFi.h>

#include <atomic>

#include "channels/ChannelManager.h"
#include "network/HttpStream.h"
#include "network/RemoteProtocol.h"

class RemoteSource {
 public:
  enum class State : uint8_t { Idle, Connecting, Ready, Failed };
  enum class Failure : uint8_t { None, NoServer, NoChannel, ChannelOff, BadReply, NoData };

  // Counters are monotonic since boot (the stats line diffs two snapshots); buffers are now.
  struct Stats {
    uint32_t bytes = 0;       // received from the server, HTTP framing included
    uint32_t waitedMs = 0;    // the player waiting for network data
    uint32_t stalls = 0;      // reads that gave up: network_timeouts
    uint32_t underruns = 0;   // the player found a buffer empty (read stalls)
    uint32_t sessions = 0;
    uint32_t reconnects = 0;  // automatic re-tunes after a failure or a lost signal
    uint32_t videoBuffered = 0;
    uint32_t audioBuffered = 0;
    uint32_t videoCapacity = 0;
    uint32_t audioCapacity = 0;
    // Network task passes over the video socket: data read / nothing there while the ring had
    // room (waiting on the network) / ring full (waiting on the player).
    uint32_t passesData = 0;
    uint32_t passesIdle = 0;
    uint32_t passesFull = 0;
  };

  bool begin();  // once, at boot: rings in PSRAM and the network task
  bool started() const { return started_; }

  // Loop task. Starts tuning in to "http://<server>/channel/<n>", dropping any previous
  // session; `retry` counts it as a reconnect. Call with the player stopped.
  void tune(const char* url, bool retry);
  // Loop task, before MediaPlayer::stop(): wakes a read that is waiting for data.
  void cancel();
  // Loop task, after MediaPlayer::stop(): hang up. The network task closes the sockets.
  void close();

  // Loop task: fetch the guide of the server that `channelUrl` (any of its channels) is on.
  // Done only while nothing streams; a new request replaces one not started yet.
  void requestGuide(const char* channelUrl);
  // Loop task: the answer, once, when it arrived. The text stays valid until the next request.
  bool takeGuide(const char*& json, size_t& len);

  // Of the latest tune().
  State state() const;
  Failure failure() const { return static_cast<Failure>(failure_.load()); }
  static const char* failureText(Failure f);  // the NO SIGNAL line
  uint32_t positionMs() const { return positionMs_.load(); }
  uint8_t fps() const { return fps_.load(); }                  // of the latest session
  uint32_t prebufferMs() const { return prebufferMs_.load(); }
  // Longest wait for video bytes (ring not full) since the previous call.
  uint32_t takeGapMaxMs() { return gapMaxMs_.exchange(0); }

  Stream* video() { return &video_; }
  Stream* audio() { return hasAudio_.load() ? &audio_ : nullptr; }

  Stats stats() const;

 private:
  struct Conn {
    Conn(const char* label, HttpStream* target) : name(label), stream(target) {}
    const char* name;
    HttpStream* stream;
    WiFiClient client;
    ChunkedDecoder chunked;
    bool isChunked = false;
    bool open = false;
    long left = -1;  // Content-Length still to come; -1: until the server closes
  };

  static void taskEntry(void* self);
  void run();
  void publish(uint32_t generation, State s, Failure f);
  bool superseded(uint32_t generation) const { return wanted_.load() != generation; }
  Failure connectAll(uint32_t generation, const char* url);
  bool resolve(const char* host, IPAddress& ip);
  Failure createSession(uint32_t generation, const IPAddress& ip, const HttpUrl& u, RemoteSession& s);
  Failure openStream(uint32_t generation, Conn& c, const IPAddress& ip, const HttpUrl& u, const char* path);
  bool readHead(uint32_t generation, WiFiClient& client, HttpHead& head, uint32_t timeoutMs);
  size_t readBody(uint32_t generation, WiFiClient& client, const HttpHead& head, char* out, size_t cap);
  void fetchGuide();
  void pump(Conn& c);
  void finish(Conn& c, const char* why);
  bool primed() const;
  uint32_t primeTimeoutMs() const;
  void hangUp();

  HttpStream video_;
  HttpStream audio_;

  // Loop task -> network task.
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  char url_[CHANNEL_SOURCE_LEN] = "";
  std::atomic<uint32_t> wanted_{0};  // generation of the latest tune() / close()
  char guideUrl_[CHANNEL_SOURCE_LEN] = "";
  std::atomic<bool> guideWanted_{false};

  // Network task -> loop task.
  std::atomic<uint32_t> stateGeneration_{0};
  std::atomic<uint8_t> state_{static_cast<uint8_t>(State::Idle)};
  std::atomic<uint8_t> failure_{static_cast<uint8_t>(Failure::None)};
  std::atomic<bool> hasAudio_{false};
  std::atomic<uint32_t> positionMs_{0};
  std::atomic<uint8_t> fps_{REMOTE_DEFAULT_FPS};
  std::atomic<uint32_t> prebufferMs_{REMOTE_DEFAULT_PREBUFFER_MS};
  std::atomic<uint32_t> passesData_{0};
  std::atomic<uint32_t> passesIdle_{0};
  std::atomic<uint32_t> passesFull_{0};
  std::atomic<uint32_t> gapMaxMs_{0};
  std::atomic<uint32_t> bytes_{0};
  std::atomic<uint32_t> sessions_{0};
  std::atomic<uint32_t> reconnects_{0};
  std::atomic<bool> guideReady_{false};
  char* guideJson_ = nullptr;  // PSRAM, GUIDE_JSON_MAX
  size_t guideLen_ = 0;

  // Network task only.
  Conn videoConn_{"video", &video_};
  Conn audioConn_{"audio", &audio_};
  uint32_t active_ = 0;
  bool streaming_ = false;
  bool ready_ = false;
  uint32_t streamingSinceMs_ = 0;
  uint32_t lastVideoDataMs_ = 0;
  JpegStartCounter videoFrames_;  // frames received this session: the prebuffer counts them
  char cachedHost_[REMOTE_HOST_LEN] = "";  // last name resolved, dropped when it stops answering
  IPAddress cachedIp_;
  uint8_t* io_ = nullptr;  // one socket read, internal RAM
  bool started_ = false;
};
