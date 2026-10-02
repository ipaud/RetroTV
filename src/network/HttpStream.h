#pragma once

// One HTTP response body as a Stream, for MediaPlayer: to the player a remote channel reads
// like a file. The network task (RemoteSource) pushes into a PSRAM ring; the player's task
// reads with readBytes(), which waits for data the way a file read waits for the card and
// returns 0 only when the body is over, the programme is being stopped, or nothing arrived for
// REMOTE_STALL_MS (the connection is lost). MediaPlayer reads 0 as the end of the stream.

#include <Stream.h>

#include <atomic>

#include "network/ByteRing.h"

class HttpStream : public Stream {
 public:
  bool begin(uint32_t capacity);  // once, at boot: the ring in PSRAM (a power of two)

  // Player task.
  size_t readBytes(char* buffer, size_t length) override;
  int read() override;
  int available() override { return static_cast<int>(ring_.size()); }
  int peek() override { return -1; }  // MediaPlayer never peeks
  size_t write(uint8_t) override { return 0; }

  // Network task.
  ByteRing& ring() { return ring_; }
  uint32_t buffered() const { return ring_.size(); }
  uint32_t capacity() const { return ring_.capacity(); }
  void end() { ended_ = true; }  // after the last push
  bool ended() const { return ended_.load(); }

  // Loop task.
  void cancel() { cancelled_ = true; }  // before MediaPlayer::stop(): wakes a waiting read
  void reset();                         // new session; the player is stopped

  uint32_t stalls() const { return stalls_.load(); }       // reads that gave up: signal lost
  uint32_t waitedMs() const { return waitedMs_.load(); }   // time the player waited for data
  uint32_t underruns() const { return underruns_.load(); } // times the player found it empty

 private:
  ByteRing ring_;
  std::atomic<bool> ended_{false};
  std::atomic<bool> cancelled_{false};
  std::atomic<uint32_t> stalls_{0};
  std::atomic<uint32_t> waitedMs_{0};
  std::atomic<uint32_t> underruns_{0};
  bool starved_ = false;  // player task only: counts each empty spell once
};
