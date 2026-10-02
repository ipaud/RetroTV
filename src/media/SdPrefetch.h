#pragma once

// Reads a local episode's .mjpeg ahead of the player, into a PSRAM ring, on its own task. While
// the TV plays, a read from the card is mostly waiting (measured 2026-10-01: core 0 sat idle
// for as long as the reads took, 5-15 ms of each frame); reading ahead overlaps that wait with
// the JPEG decode. The player takes it as a Stream, like a remote channel's network ring.

#include <Stream.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <atomic>

#include "network/ByteRing.h"
#include "storage/SdFile.h"

class SdPrefetch : public Stream {
 public:
  bool begin();  // once, at boot: ring in PSRAM, read buffer in DMA-capable RAM, the task
  // Reads `file` from where it is now; it must stay open until stop() returns true.
  bool start(SdFile& file);
  // True once the task is idle, so the file can be closed; false if it did not stop in time.
  bool stop();

  // The player's read: what the ring holds, waiting for the card if it is empty. 0 at the
  // end of the file, when the card refused a read, or once stopped.
  size_t readBytes(char* buf, size_t len) override;
  int available() override { return static_cast<int>(ring_.size()); }
  int read() override {
    char b;
    return readBytes(&b, 1) == 1 ? static_cast<uint8_t>(b) : -1;
  }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 0; }

 private:
  static void taskEntry(void* self);
  void run();
  void fill();

  ByteRing ring_;
  uint8_t* chunk_ = nullptr;
  SdFile* file_ = nullptr;
  EventGroupHandle_t events_ = nullptr;
  std::atomic<bool> ended_{false};
  std::atomic<bool> cancelled_{false};
};
