#pragma once

#include <Arduino_GFX_Library.h>
#include <freertos/queue.h>

#include <atomic>

#include "config.h"
#include "display/BlockClip.h"

// One decoded piece of a video frame, big-endian RGB565, ready to be copied to the panel.
struct VideoBlock {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  uint32_t frame;
  bool lastOfFrame;
  uint16_t pixels[VIDEO_BLOCK_PIXELS];
};

// Owns the panel. A single DisplayTask is the only code that ever calls Arduino_GFX:
// it initialises the ILI9341, draws queued video blocks as they arrive and calls the
// registered RenderFn every tick for everything else (UI, OSD, static, test card).
// Other tasks never draw; they publish state or hand over video blocks.
class DisplayManager {
 public:
  // Draws the UI and declares in `overlay` the area it keeps for itself (the OSD box):
  // video blocks are clipped around it. Empty overlay = video may use the whole panel.
  using RenderFn = void (*)(Arduino_GFX& gfx, uint32_t nowMs, void* ctx, ClipRect& overlay);
  using FrameShownFn = void (*)(uint32_t frame, void* ctx);

  DisplayManager();

  // Starts DisplayTask and waits until the panel init finished. False = no display.
  bool begin(RenderFn render, void* ctx);

  // Backlight PWM (GPIO45). Not SPI, so any task may call it.
  void setBrightness(uint8_t percent);
  uint8_t brightness() const { return brightnessPct_; }

  // Standby: DisplayTask turns the backlight off, puts the ILI9341 to sleep and stops. Waits up
  // to 300 ms for it. Waking is a new boot.
  void sleep();

  // Render ticks during the last full second (UI refresh rate).
  uint32_t fps() const { return fps_; }
  // Render ticks since boot: the display task's heartbeat.
  uint32_t heartbeat() const { return renderTicks_; }

  // --- Video path (fixed pool, nothing allocated while playing) ---------------------------
  // Decoder side: take a free block, fill it, submit it. Null if none frees up in time.
  VideoBlock* acquireBlock(TickType_t wait);
  void submitBlock(VideoBlock* block);
  // Drops blocks not drawn yet (after the player stopped).
  void discardPendingBlocks();
  // Called on DisplayTask right after the last block of a frame is on the panel.
  void setFrameShownHook(FrameShownFn fn, void* ctx);
  // Microseconds spent drawing video since the previous call.
  uint32_t takeDrawMicros() { return drawMicros_.exchange(0); }

 private:
  static void taskEntry(void* self);
  void run();
  void applyBrightness();
  void drawBlock(VideoBlock* block);

  Arduino_ESP32SPI bus_;
  Arduino_ILI9341 gfx_;
  RenderFn render_ = nullptr;
  void* ctx_ = nullptr;
  TaskHandle_t task_ = nullptr;
  TaskHandle_t creator_ = nullptr;
  bool ready_ = false;
  uint8_t brightnessPct_;
  volatile uint32_t fps_ = 0;
  volatile uint32_t renderTicks_ = 0;

  VideoBlock blocks_[VIDEO_BLOCK_COUNT];
  QueueHandle_t freeBlocks_ = nullptr;
  QueueHandle_t readyBlocks_ = nullptr;
  FrameShownFn frameShown_ = nullptr;
  void* frameShownCtx_ = nullptr;
  std::atomic<uint32_t> drawMicros_{0};
  std::atomic<TaskHandle_t> sleepWaiter_{nullptr};
  ClipRect overlay_;  // DisplayTask only
};
