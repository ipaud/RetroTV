#include "voice/AudioCapture.h"

#if PAUTV_MIC_ENABLED  // the normal build carries none of this

#include <Arduino.h>

#include "app_types.h"
#include "audio/AudioManager.h"
#include "config.h"

namespace {
int16_t stereoBlock[MIC_BLOCK_FRAMES * 2];  // the capture task's only buffers
int16_t monoBlock[MIC_BLOCK_FRAMES];
}  // namespace

bool AudioCapture::begin(AudioManager& audio) {
  audio_ = &audio;
  if (!audio.micReady()) return false;
#if PAUTV_CLAP_ENABLED
  clap_.setPlayback(&audio.playback());
#endif
  if (xTaskCreatePinnedToCore(taskEntry, "mic", MIC_TASK_STACK, this, MIC_TASK_PRIO, &task_, MIC_TASK_CORE) != pdPASS) {
    task_ = nullptr;
    PLOG("MIC", "capture task failed: microphone off");
    return false;
  }
  return true;
}

AudioCapture::Snapshot AudioCapture::snapshot() const {
  Snapshot s;
  s.rmsDb10 = rmsDb10_.load();
  s.peakDb10 = peakDb10_.load();
  s.holdDb10 = holdOut_.load();
  s.clipped = clipped_.load();
  s.overruns = overruns_.load();
  s.blocks = blocks_.load();
  s.floorDb10 = floorDb10_.load();
  s.thresholdDb10 = thresholdDb10_.load();
  s.clapPeakDb10 = clapPeakDb10_.load();
  s.clapGapMs = clapGapMs_.load();
  s.clapTvRiseDb10 = clapTvRiseDb10_.load();
  s.clapHfPct = clapHfPct_.load();
  s.dull = dull_.load();
  s.badSequences = badSequences_.load();
  s.claps = claps_.load();
  s.sustained = sustained_.load();
  s.suppressed = suppressed_.load();
  s.fromTv = fromTv_.load();
  return s;
}

void AudioCapture::taskEntry(void* self) { static_cast<AudioCapture*>(self)->run(); }

void AudioCapture::run() {
  for (;;) {
    const size_t frames = audio_->readMic(stereoBlock, MIC_BLOCK_FRAMES, MIC_READ_TIMEOUT_MS);
    if (frames < MIC_BLOCK_FRAMES) overruns_.fetch_add(1);
    if (frames == 0) continue;
    for (size_t i = 0; i < frames; ++i) monoBlock[i] = stereoBlock[2 * i];
    const uint32_t now = millis();
    publish(meter_.block(monoBlock, frames), now);
    detectClaps(monoBlock, frames, now);
    record(monoBlock, frames);
  }
}

// The detector runs here, on every block, so no clap falls between two loop passes. Only the
// finished sequence crosses to the loop task (sequence_), plus numbers for the logs.
void AudioCapture::detectClaps(const int16_t* mono, size_t frames, uint32_t nowMs) {
#if PAUTV_CLAP_ENABLED
  if (!clapOn_.load()) return;
  const uint8_t s = sensitivity_.load();
  if (s != appliedSensitivity_) {
    clap_.setSensitivity(s);
    appliedSensitivity_ = s;
  }
  clap_.setExtraMarginDb10(playbackLoud_.load() ? CLAP_PLAYBACK_EXTRA_DB10 : 0);
  clap_.setMinDb10(clapMinDb10_.load());
  const uint32_t fx = audio_->fxUntilMs();
  if (fx != lastFxMs_) {  // static or a beep from the TV itself: not a clap
    lastFxMs_ = fx;
    clap_.suppressUntil(fx + CLAP_SELF_SOUND_TAIL_MS);
  }
  clap_.feed(mono, frames, nowMs);
  if (const uint8_t i = clap_.takeClap()) clapIndex_.store(i);  // voice standby's LED and wake
  bangs_.store(clap_.bangs());
  if (const uint8_t q = clap_.takeSequence()) {
    seqQuietMs_.store(clap_.lastQuietBeforeMs());  // details first, then the sequence that publishes them
    sequence_.store(q);
  }
  const ClapDetector::Stats& st = clap_.stats();
  floorDb10_.store(clap_.floorDb10());
  thresholdDb10_.store(clap_.thresholdDb10());
  clapPeakDb10_.store(clap_.lastPeakDb10());
  clapGapMs_.store(clap_.lastGapMs());
  clapTvRiseDb10_.store(clap_.lastTvRiseDb10());
  clapHfPct_.store(static_cast<uint8_t>(clap_.lastHfShare() * 100.0f + 0.5f));
  dull_.store(st.dull);
  badSequences_.store(st.badSequences);
  claps_.store(st.claps);
  sustained_.store(st.sustained);
  suppressed_.store(st.suppressed);
  fromTv_.store(st.fromTv);
#else
  (void)mono;
  (void)frames;
  (void)nowMs;
#endif
}

void AudioCapture::startRecording(int16_t* pcm, size_t maxSamples) {
  recDown_ = Downsampler(static_cast<float>(AUDIO_SAMPLE_RATE), static_cast<float>(REC_SAMPLE_RATE), REC_LOWPASS_HZ);
  recBuf_ = pcm;
  recMax_ = maxSamples;
  recCount_.store(0);
  recording_.store(true);
}

size_t AudioCapture::stopRecording() {
  recording_.store(false);
  while (recWriting_.load()) vTaskDelay(1);  // the block in hand finishes first (~0.1 ms)
  return recCount_.load();
}

// recWriting_ goes up before recording_ is read, so stopRecording() (which clears recording_ and
// then waits for recWriting_ to drop) never returns while a block is still being written.
void AudioCapture::record(const int16_t* mono, size_t frames) {
  recWriting_.store(true);
  if (!recording_.load()) {
    recWriting_.store(false);
    return;
  }
  const size_t done = recCount_.load();
  if (done < recMax_) recCount_.store(done + recDown_.feed(mono, frames, recBuf_ + done, recMax_ - done));
  recWriting_.store(false);
}

void AudioCapture::setPaused(bool paused) {
  if (task_ == nullptr || paused == paused_) return;
  paused_ = paused;
  if (paused) {
    vTaskSuspend(task_);
  } else {
    vTaskResume(task_);
  }
  PLOG("MIC", "capture %s", paused ? "paused" : "running");
}

void AudioCapture::publish(const MicLevels& levels, uint32_t nowMs) {
  if (levels.peakDb10 >= holdDb10_ || nowMs - holdSinceMs_ >= MIC_PEAK_HOLD_MS) {
    holdDb10_ = levels.peakDb10;
    holdSinceMs_ = nowMs;
  }
  rmsDb10_.store(levels.rmsDb10);
  peakDb10_.store(levels.peakDb10);
  holdOut_.store(holdDb10_);
  if (levels.clipped > 0) clipped_.fetch_add(levels.clipped);
  blocks_.fetch_add(1);
}

#endif  // PAUTV_MIC_ENABLED
