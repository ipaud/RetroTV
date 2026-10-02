#pragma once

#include <JPEGDEC.h>
#include <Stream.h>
#include <freertos/event_groups.h>

#include <atomic>

#include "config.h"
#include "media/JpegFrameSplitter.h"
#include "media/PlaybackClock.h"

class AudioManager;
class DisplayManager;

// Playback counters since boot. Monotonic and wrap-safe: readers diff two snapshots with
// uint32 arithmetic, so the 5 s stats and the soak test never reset each other's numbers.
struct PlaybackCounters {
  uint32_t shown = 0;       // frames on the panel
  uint32_t dropped = 0;     // late frames skipped undecoded
  uint32_t bad = 0;         // frames JPEGDEC rejected
  uint32_t decoded = 0;
  uint32_t decodeUs = 0;
  uint32_t driftSumMs = 0;  // signed sum of av drift, as uint32 (two's complement)
  uint32_t sdBytes = 0;
  uint32_t audioErrors = 0;
  uint32_t videoBeats = 0;  // task liveness
  uint32_t audioBeats = 0;
};

// Each reader of the worst A/V drift keeps its own maximum.
enum class DriftReader : uint8_t { Stats, Soak, Net, Count };

// Raw MJPEG video + ADTS AAC audio from any Stream (a File today, a network stream later).
// Tasks and buffers are created once in begin(); start()/stop() switch programmes without
// allocating and without rebooting.
//   video task (core 0): split frame -> pace against PlaybackClock -> JPEGDEC -> blocks to
//                        DisplayTask. Late frames are dropped undecoded.
//   audio task (core 1, audio priority): AAC (Helix low-level API) -> I2S; drives the clock.
class MediaPlayer {
 public:
  // The panel shows 32 levels per colour: dithering hides the steps in dark gradients.
  void setDither(bool on) { dither_ = on; }
  bool dither() const { return dither_.load(); }
  bool begin(DisplayManager& display, AudioManager& audio);

  // audio may be null (silent programme: the wall clock paces the video); it is also ignored
  // when the codec is not ready, so a dead DAC never freezes the picture. False when a
  // previous programme could not be stopped: nothing was started.
  bool start(Stream* video, Stream* audio, uint8_t fps);
  // Stops both tasks and waits for them. False = a task did not stop in time; the streams
  // may still be in use and must not be closed.
  bool stop();

  bool playing() const { return running_.load(); }
  // The video stream reached its end (the audio may still be draining).
  bool finished() const { return finished_.load(); }

  // Prints the playback stats every MEDIA_STATS_PERIOD_MS while playing (PAUTV_DEBUG_STATS).
  void logStats(uint32_t nowMs);

  PlaybackCounters counters() const;
  // Largest |av drift| (ms) seen by this reader since its previous call.
  int32_t takeDriftMax(DriftReader reader) { return driftMax_[static_cast<int>(reader)].exchange(0); }

 private:
  static void videoEntry(void* self);
  static void audioEntry(void* self);
  void videoLoop();
  void audioLoop();
  void playVideo();
  void playAudio();
  bool waitUntilDue(uint32_t dueMs, bool& drop);
  void decodeFrame(size_t len, uint32_t index);
  static int onJpegBlock(JPEGDRAW* draw);
  static void onFrameShown(uint32_t frame, void* self);

  DisplayManager* display_ = nullptr;
  AudioManager* audio_ = nullptr;
  Stream* videoIn_ = nullptr;
  Stream* audioIn_ = nullptr;
  uint8_t fps_ = 0;

  EventGroupHandle_t events_ = nullptr;
  std::atomic<bool> running_{false};
  std::atomic<bool> finished_{false};
  PlaybackClock clock_;

  // Allocated once in begin().
  uint8_t* frameBuf_ = nullptr;   // PSRAM, one compressed JPEG
  uint8_t* readChunk_ = nullptr;
  uint8_t* aacIn_ = nullptr;
  int16_t* pcm_ = nullptr;
  void* aac_ = nullptr;           // HAACDecoder
  JpegFrameSplitter splitter_;
  JPEGDEC jpeg_;

  // Geometry of the frame being decoded (video task).
  uint32_t currentFrame_ = 0;
  int frameX_ = 0;
  int frameY_ = 0;
  int frameW_ = 0;
  int frameH_ = 0;
  uint32_t blockWaitUs_ = 0;

  // Monotonic counters (see PlaybackCounters).
  std::atomic<uint32_t> shown_{0};
  std::atomic<uint32_t> dropped_{0};
  std::atomic<uint32_t> badFrames_{0};
  std::atomic<uint32_t> decodeUs_{0};
  bool dithering_ = false;  // this frame's mode (video task only)
  std::atomic<bool> dither_{VIDEO_DITHER_DEFAULT};  // RGB888 -> RGB565 with an ordered dither
  std::atomic<uint32_t> decoded_{0};
  std::atomic<uint32_t> driftSum_{0};
  std::atomic<uint32_t> sdBytes_{0};
  std::atomic<uint32_t> audioErrors_{0};
  std::atomic<uint32_t> videoBeats_{0};
  std::atomic<uint32_t> audioBeats_{0};
  std::atomic<int32_t> driftMax_[static_cast<int>(DriftReader::Count)] = {};
  PlaybackCounters statsBase_;
  uint32_t statsSinceMs_ = 0;
  bool rateWarned_ = false;
};
