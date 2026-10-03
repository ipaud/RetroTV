#pragma once

// RETROTV Voice: reads the microphone and keeps its levels (docs/VOICE.md). Only gets PCM and
// measures it; the clap detector and the voice state machine sit on top.
//
// One task (MIC_TASK_*), created only when the microphone came up. It sleeps in i2s_read, takes
// the left slot of each stereo frame (the ES8311 puts the same ADC sample in both) and measures
// it. Buffers are static: nothing is allocated
// after begin(), and nothing is ever written anywhere: samples live for one block.

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "voice/ClapDetector.h"
#include "voice/MicMeter.h"
#include "voice/Recorder.h"

class AudioManager;

class AudioCapture {
 public:
  struct Snapshot {
    int16_t rmsDb10 = MIC_FLOOR_DB10;       // the last block
    int16_t peakDb10 = MIC_FLOOR_DB10;      // the loudest sample of the last block
    int16_t holdDb10 = MIC_FLOOR_DB10;      // the loudest block peak of the last MIC_PEAK_HOLD_MS
    uint32_t clipped = 0;                   // samples at the rails since begin()
    uint32_t overruns = 0;                  // reads that came back short or late
    uint32_t blocks = 0;
    // Claps (PAUTV_CLAP_ENABLED): for the logs and the false-positive count.
    int16_t floorDb10 = MIC_FLOOR_DB10;      // the room, as the detector hears it
    int16_t thresholdDb10 = MIC_FLOOR_DB10;  // what a clap has to reach now
    int16_t clapPeakDb10 = MIC_FLOOR_DB10;   // the last clap's loudest window
    uint32_t clapGapMs = 0;                  // between the last two claps
    int16_t clapTvRiseDb10 = 0;              // the programme's own rise around the last clap
    uint8_t clapHfPct = 0;                   // the last transient's energy above CLAP_HF_HZ, %
    uint32_t dull = 0;                       // transients too dull for a clap (knocks)
    uint32_t badSequences = 0;               // sequences that were not claps (rhythm, loudness, too many)
    uint32_t claps = 0;
    uint32_t sustained = 0;
    uint32_t suppressed = 0;
    uint32_t fromTv = 0;
    int16_t tvPeakDb10 = MIC_FLOOR_DB10;   // the last one dropped as the programme: its peak...
    int16_t tvFloorDb10 = MIC_FLOOR_DB10;  // ...and the floor then
  };

  // Loop task, after AudioManager::begin(). False (and no task) without a working microphone.
  bool begin(AudioManager& audio);
  bool running() const { return task_ != nullptr; }
  // Loop task. A paused capture leaves the RX DMA running (the driver drops what nobody reads).
  void setPaused(bool paused);
  bool paused() const { return paused_; }
  Snapshot snapshot() const;  // any task

  // Loop task: what the clap detector should do. Read by the capture task on its next block.
  void setClapEnabled(bool on) { clapOn_.store(on); }
  void setClapSensitivity(uint8_t s) { sensitivity_.store(s); }
  void setPlaybackLoud(bool loud) { playbackLoud_.store(loud); }  // the TV's speaker is on
  void setClapMinDb10(int16_t db10) { clapMinDb10_.store(db10); }  // STANDBY VOZ asks for a louder clap
  // Loop task: the clap count of a finished sequence (1, 2, 3 = three or more), or 0.
  uint8_t takeSequence() { return sequence_.exchange(0); }
  // How long it was quiet before the sequence takeSequence() just returned, and every bang heard so far.
  uint32_t sequenceQuietBeforeMs() const { return seqQuietMs_.load(); }
  uint32_t bangs() const { return bangs_.load(); }
  uint32_t dull() const { return dull_.load(); }
  uint32_t fromTv() const { return fromTv_.load(); }  // transients dropped as the programme's own sound  // transients dropped as too dull (snapshot().clapHfPct: how bright)
  // Loop task: the place in its sequence (1, 2...) of the newest clap, as it happened, or 0.
  uint8_t takeClapIndex() { return clapIndex_.exchange(0); }

  // Loop task, only with REC on screen (AppVoice.cpp): from now on the capture task resamples to
  // 16 kHz into `pcm` until stopRecording() or until it is full. Nothing else is ever kept.
  void startRecording(int16_t* pcm, size_t maxSamples);
  size_t stopRecording();  // the samples recorded; waits for the block being written
  size_t recordedSamples() const { return recCount_.load(); }
  bool recordingFull() const { return recCount_.load() >= recMax_; }

 private:
  static void taskEntry(void* self);
  void run();
  void publish(const MicLevels& levels, uint32_t nowMs);
  void detectClaps(const int16_t* mono, size_t frames, uint32_t nowMs);
  void record(const int16_t* mono, size_t frames);

  AudioManager* audio_ = nullptr;
  TaskHandle_t task_ = nullptr;
  MicMeter meter_;
  bool paused_ = false;
  int16_t holdDb10_ = MIC_FLOOR_DB10;  // capture task only
  uint32_t holdSinceMs_ = 0;

  std::atomic<int16_t> rmsDb10_{MIC_FLOOR_DB10};
  std::atomic<int16_t> peakDb10_{MIC_FLOOR_DB10};
  std::atomic<int16_t> holdOut_{MIC_FLOOR_DB10};
  std::atomic<uint32_t> clipped_{0};
  std::atomic<uint32_t> overruns_{0};
  std::atomic<uint32_t> blocks_{0};

#if PAUTV_CLAP_ENABLED
  ClapDetector clap_;  // capture task only
  uint32_t lastFxMs_ = 0;
  uint8_t appliedSensitivity_ = 0xFF;
#endif
  std::atomic<bool> clapOn_{false};
  std::atomic<uint8_t> sensitivity_{CLAP_SENSITIVITY_DEFAULT};
  std::atomic<bool> playbackLoud_{false};
  std::atomic<uint8_t> sequence_{0};
  std::atomic<uint8_t> clapIndex_{0};
  std::atomic<uint32_t> seqQuietMs_{UINT32_MAX};
  std::atomic<uint32_t> bangs_{0};
  int16_t* recBuf_ = nullptr;  // PSRAM, owned by the app
  size_t recMax_ = 0;
  std::atomic<bool> recording_{false};
  std::atomic<bool> recWriting_{false};
  std::atomic<size_t> recCount_{0};
  Downsampler recDown_{static_cast<float>(AUDIO_SAMPLE_RATE), static_cast<float>(REC_SAMPLE_RATE), REC_LOWPASS_HZ};
  std::atomic<int16_t> floorDb10_{MIC_FLOOR_DB10};
  std::atomic<int16_t> thresholdDb10_{MIC_FLOOR_DB10};
  std::atomic<int16_t> clapPeakDb10_{MIC_FLOOR_DB10};
  std::atomic<uint32_t> clapGapMs_{0};
  std::atomic<int16_t> clapTvRiseDb10_{0};
  std::atomic<uint8_t> clapHfPct_{0};
  std::atomic<uint32_t> dull_{0};
  std::atomic<uint32_t> badSequences_{0};
  std::atomic<int16_t> clapMinDb10_{CLAP_MIN_DB10};
  std::atomic<uint32_t> claps_{0};
  std::atomic<uint32_t> sustained_{0};
  std::atomic<uint32_t> suppressed_{0};
  std::atomic<uint32_t> fromTv_{0};
  std::atomic<int16_t> tvPeakDb10_{MIC_FLOOR_DB10};
  std::atomic<int16_t> tvFloorDb10_{MIC_FLOOR_DB10};
};
