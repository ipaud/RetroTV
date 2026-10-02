#include "display/DisplayManager.h"

#include "app_types.h"
#include "board_config.h"

DisplayManager::DisplayManager()
    : bus_(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, PIN_LCD_MISO, FSPI),
      gfx_(&bus_, GFX_NOT_DEFINED, PAUTV_ROTATION, LCD_IPS_INVERT),
      brightnessPct_(BRIGHTNESS_DEFAULT_PCT) {}

bool DisplayManager::begin(RenderFn render, void* ctx) {
  render_ = render;
  ctx_ = ctx;

  freeBlocks_ = xQueueCreate(VIDEO_BLOCK_COUNT, sizeof(VideoBlock*));
  readyBlocks_ = xQueueCreate(VIDEO_BLOCK_COUNT, sizeof(VideoBlock*));
  if (freeBlocks_ == nullptr || readyBlocks_ == nullptr) {
    PLOG("DISPLAY", "cannot create video queues");
    return false;
  }
  for (VideoBlock& b : blocks_) {
    VideoBlock* p = &b;
    xQueueSend(freeBlocks_, &p, 0);
  }

  // Backlight stays dark until the panel has been cleared, so power-on garbage is never seen.
  ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS);
  ledcAttachPin(PIN_LCD_BL, BACKLIGHT_PWM_CHANNEL);
  ledcWrite(BACKLIGHT_PWM_CHANNEL, 0);

  creator_ = xTaskGetCurrentTaskHandle();
  if (xTaskCreatePinnedToCore(taskEntry, "display", DISPLAY_TASK_STACK, this, DISPLAY_TASK_PRIO,
                              &task_, DISPLAY_TASK_CORE) != pdPASS) {
    PLOG("DISPLAY", "cannot create display task");
    return false;
  }
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DISPLAY_INIT_TIMEOUT_MS)) == 0) {
    PLOG("DISPLAY", "panel init timed out");
    return false;
  }
  if (!ready_) {
    PLOG("DISPLAY", "panel init failed");
    return false;
  }
  PLOG("DISPLAY", "ILI9341 %dx%d rotation %u spi %ld Hz", gfx_.width(), gfx_.height(),
       PAUTV_ROTATION, static_cast<long>(PAUTV_SPI_HZ));
  return true;
}

void DisplayManager::setBrightness(uint8_t percent) {
  brightnessPct_ = constrain(percent, BRIGHTNESS_MIN_PCT, 100);
  if (ready_) applyBrightness();
}

void DisplayManager::applyBrightness() {
  constexpr uint32_t maxDuty = (1u << BACKLIGHT_PWM_BITS) - 1;
  ledcWrite(BACKLIGHT_PWM_CHANNEL, maxDuty * brightnessPct_ / 100);
}

VideoBlock* DisplayManager::acquireBlock(TickType_t wait) {
  VideoBlock* b = nullptr;
  return xQueueReceive(freeBlocks_, &b, wait) == pdTRUE ? b : nullptr;
}

void DisplayManager::submitBlock(VideoBlock* block) {
  xQueueSend(readyBlocks_, &block, portMAX_DELAY);  // never full: pool size = queue size
}

void DisplayManager::discardPendingBlocks() {
  VideoBlock* b = nullptr;
  while (xQueueReceive(readyBlocks_, &b, 0) == pdTRUE) xQueueSend(freeBlocks_, &b, 0);
}

void DisplayManager::setFrameShownHook(FrameShownFn fn, void* ctx) {
  frameShownCtx_ = ctx;
  frameShown_ = fn;
}

void DisplayManager::drawBlock(VideoBlock* block) {
  const uint32_t t0 = micros();
  forEachVisibleSpan(ClipRect{block->x, block->y, block->w, block->h}, overlay_,
                     [this, block](const ClipRect& s, int offset) {
                       gfx_.draw16bitBeRGBBitmap(s.x, s.y, block->pixels + offset, s.w, s.h);
                     });
  drawMicros_.fetch_add(micros() - t0);

  const bool last = block->lastOfFrame;
  const uint32_t frame = block->frame;
  xQueueSend(freeBlocks_, &block, 0);
  if (last && frameShown_ != nullptr) frameShown_(frame, frameShownCtx_);
}

void DisplayManager::taskEntry(void* self) { static_cast<DisplayManager*>(self)->run(); }

void DisplayManager::run() {
  ready_ = gfx_.begin(PAUTV_SPI_HZ);
  if (ready_) {
    gfx_.fillScreen(RGB565_BLACK);
    applyBrightness();
  }
  xTaskNotifyGive(creator_);
  if (!ready_) {
    vTaskDelete(nullptr);
    return;
  }

  uint32_t frames = 0;
  uint32_t windowStartMs = millis();
  uint32_t lastRenderMs = millis() - DISPLAY_TICK_MS;
  for (;;) {
    const uint32_t now = millis();
    if (now - lastRenderMs >= DISPLAY_TICK_MS) {
      render_(gfx_, now, ctx_, overlay_);
      lastRenderMs = now;
      ++frames;
      renderTicks_ = renderTicks_ + 1;
      if (now - windowStartMs >= 1000) {
        fps_ = frames;
        frames = 0;
        windowStartMs = now;
      }
    }
    // Sleep until the next UI tick unless a video block arrives first. Blocking here is
    // what gives loopTask (lower priority, same core) its time.
    const uint32_t sinceRender = millis() - lastRenderMs;
    const uint32_t waitMs = sinceRender >= DISPLAY_TICK_MS ? 1 : DISPLAY_TICK_MS - sinceRender;
    VideoBlock* block = nullptr;
    if (xQueueReceive(readyBlocks_, &block, pdMS_TO_TICKS(waitMs)) == pdTRUE) drawBlock(block);
    if (TaskHandle_t waiter = sleepWaiter_.load()) {  // standby: only this task talks to the panel
      ledcWrite(BACKLIGHT_PWM_CHANNEL, 0);
      gfx_.displayOff();
      xTaskNotifyGive(waiter);
      vTaskSuspend(nullptr);
    }
  }
}

void DisplayManager::sleep() {
  if (!ready_) return;
  sleepWaiter_ = xTaskGetCurrentTaskHandle();
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(300));
}
