#include "voice/MessagePlayer.h"

#include "config.h"

#if PAUTV_RECORDER_ENABLED

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "app_types.h"
#include "audio/AudioManager.h"
#include "storage/StorageManager.h"
#include "voice/Recorder.h"

namespace {
constexpr size_t READ_SAMPLES = 512;   // 32 ms at 16 kHz
constexpr size_t OUT_SAMPLES = 1600;   // 512 x 44.1/16 = 1411, with room
int16_t* readBuf = nullptr;            // PSRAM, allocated once
int16_t* outBuf = nullptr;
}  // namespace

bool MessagePlayer::start(AudioManager& audio, StorageManager& storage, const char* paths, size_t count) {
  stop();
  audio_ = &audio;
  storage_ = &storage;
  if (paths_ == nullptr) paths_ = static_cast<char*>(heap_caps_malloc(MAX_MESSAGES * PATH_LEN, MALLOC_CAP_SPIRAM));
  if (readBuf == nullptr) readBuf = static_cast<int16_t*>(heap_caps_malloc(READ_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  if (outBuf == nullptr) outBuf = static_cast<int16_t*>(heap_caps_malloc(OUT_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  if (paths_ == nullptr || readBuf == nullptr || outBuf == nullptr || count == 0) return false;
  count_ = count < MAX_MESSAGES ? count : MAX_MESSAGES;
  memcpy(paths_, paths, count_ * PATH_LEN);
  stop_.store(false);
  done_.store(false);
  if (xTaskCreatePinnedToCore(taskEntry, "msgplay", MSG_TASK_STACK, this, AUDIO_TASK_PRIO, &task_, AUDIO_TASK_CORE) != pdPASS) {
    task_ = nullptr;
    done_.store(true);
    return false;
  }
  return true;
}

void MessagePlayer::stop() {
  if (task_ == nullptr) return;
  stop_.store(true);
  const uint32_t t0 = millis();
  while (!done_.load() && millis() - t0 < MSG_STOP_WAIT_MS) delay(5);
  task_ = nullptr;  // the task deleted itself
  index_.store(-1);
}

MessagePlayer::Status MessagePlayer::status() const {
  Status s;
  s.index = index_.load();
  s.posMs = posMs_.load();
  s.lenMs = lenMs_.load();
  return s;
}

void MessagePlayer::taskEntry(void* self) { static_cast<MessagePlayer*>(self)->run(); }

void MessagePlayer::run() {
  while (!stop_.load()) {
    for (size_t i = 0; i < count_ && !stop_.load(); ++i) {
      if (!playOne(i)) PLOG("MSG", "skipped %s", paths_ + i * PATH_LEN);
      index_.store(-1);
      for (uint32_t t = 0; t < MSG_GAP_MS && !stop_.load(); t += 20) vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  done_.store(true);
  vTaskDelete(nullptr);
}

bool MessagePlayer::playOne(size_t i) {
  const char* path = paths_ + i * PATH_LEN;
  SdFile f;
  if (!storage_->open(path, f)) return false;
  uint8_t head[64];
  const size_t got = f.read(head, sizeof(head));
  WavInfo w;
  if (!parseWav(head, got, static_cast<uint32_t>(f.size()), w) || !f.seek(w.dataOffset)) return false;
  Downsampler up(static_cast<float>(w.sampleRate), static_cast<float>(AUDIO_SAMPLE_RATE),
                 w.sampleRate * 0.45f < MSG_LOWPASS_HZ ? w.sampleRate * 0.45f : MSG_LOWPASS_HZ);
  const uint32_t frameBytes = w.channels * 2u;
  uint32_t left = w.dataBytes;
  uint32_t played = 0;
  lenMs_.store(w.durationMs());
  posMs_.store(0);
  index_.store(static_cast<int16_t>(i));
  PLOG("MSG", "playing %s (%lu ms) at %lu", path, static_cast<unsigned long>(w.durationMs()),
       static_cast<unsigned long>(millis()));
  while (left >= frameBytes && !stop_.load()) {
    const uint32_t want = (left < READ_SAMPLES * frameBytes ? left : READ_SAMPLES * frameBytes) / frameBytes * frameBytes;
    const size_t bytes = f.read(reinterpret_cast<uint8_t*>(readBuf), want);
    if (bytes < frameBytes) break;
    const size_t frames = bytes / frameBytes;
    if (w.channels == 2) {
      for (size_t k = 0; k < frames; ++k) readBuf[k] = readBuf[2 * k];  // the left side
    }
    const size_t out = up.feed(readBuf, frames, outBuf, OUT_SAMPLES);
    audio_->writePcm(outBuf, out, 1);  // paced by the DAC, like the video's audio
    left -= bytes;
    played += frames;
    posMs_.store(static_cast<uint32_t>(static_cast<uint64_t>(played) * 1000 / w.sampleRate));
  }
  return true;
}

#else

bool MessagePlayer::start(AudioManager&, StorageManager&, const char*, size_t) { return false; }
void MessagePlayer::stop() {}
MessagePlayer::Status MessagePlayer::status() const { return Status{}; }
void MessagePlayer::taskEntry(void*) {}
void MessagePlayer::run() {}
bool MessagePlayer::playOne(size_t) { return false; }

#endif
