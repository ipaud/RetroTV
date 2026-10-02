#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stddef.h>
#include <stdint.h>

#include "audio/Synth.h"

// ES8311 codec over legacy I2S (44.1 kHz, 16 bit, MCLK x256) + SC8002B amplifier.
// A synth task, created once on core 1 at audio priority, plays beeps, tones and static;
// the media player writes decoded PCM. Both share the I2S port one block at a time.
class AudioManager {
 public:
  // After Wire.begin(). Call from loopTask (it owns the I2C bus).
  bool begin(uint8_t userVolume);
  bool ready() const { return ready_; }
  const char* status() const;

  // loopTask only: both talk to the codec over I2C.
  void setVolume(uint8_t userVolume);
  void setMuted(bool muted);

  // Any task, never blocks. The newest sound replaces the one playing.
  void beep();
  void tone(uint16_t hz, uint32_t ms, uint8_t levelPct);
  void noise(uint32_t ms, uint8_t levelPct);
  void stopSound();  // cuts a tone or noise short

  // Media player's audio task. Blocks in i2s_write, which paces the caller to the DAC.
  // Returns the frames actually accepted (fewer on timeout).
  size_t writePcm(const int16_t* pcm, size_t frames, uint8_t channels);
  // Frames the DMA ring holds: what was written but not heard yet.
  static constexpr uint32_t latencyFrames() { return AUDIO_DMA_BUF_COUNT * AUDIO_DMA_BUF_LEN; }

 private:
  static void taskEntry(void* self);
  void run();
  bool initI2s();
  bool initCodec();
  void play(const SoundRequest& r);
  size_t writeStereo(const int16_t* stereo, size_t frames);

  void* codec_ = nullptr;  // es8311_handle_t, kept out of this header
  QueueHandle_t requests_ = nullptr;
  SemaphoreHandle_t i2sLock_ = nullptr;
  bool i2sOk_ = false;
  bool codecOk_ = false;
  bool ready_ = false;
};
