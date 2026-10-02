#pragma once

// The MENSAJES channel's player (docs/VOICE.md): plays the recorded WAVs one after the other and
// starts again after the last. Its task exists only while the channel is on screen; it takes the
// place of the video's audio task (stopped then) and, like it, sleeps in AudioManager::writePcm.
// Files are read in small chunks and resampled to 44.1 kHz; an unreadable one is skipped.

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>

class AudioManager;
class StorageManager;

class MessagePlayer {
 public:
  static constexpr size_t MAX_MESSAGES = 128;
  static constexpr size_t PATH_LEN = 48;

  // Loop task. Takes a copy of the paths (sorted, oldest first) and starts the task.
  bool start(AudioManager& audio, StorageManager& storage, const char* paths, size_t count);
  void stop();  // waits for the task to finish its chunk and close the file
  bool running() const { return task_ != nullptr; }

  struct Status {
    int16_t index = -1;   // the message playing (0-based), -1 = between messages
    uint32_t posMs = 0;
    uint32_t lenMs = 0;
  };
  Status status() const;  // any task

 private:
  static void taskEntry(void* self);
  void run();
  bool playOne(size_t i);

  AudioManager* audio_ = nullptr;
  StorageManager* storage_ = nullptr;
  char* paths_ = nullptr;  // PSRAM, MAX_MESSAGES x PATH_LEN, kept
  size_t count_ = 0;
  TaskHandle_t task_ = nullptr;
  std::atomic<bool> stop_{false};
  std::atomic<bool> done_{true};
  std::atomic<int16_t> index_{-1};
  std::atomic<uint32_t> posMs_{0};
  std::atomic<uint32_t> lenMs_{0};
};
