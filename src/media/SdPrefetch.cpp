#include "media/SdPrefetch.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "app_types.h"
#include "config.h"

namespace {

constexpr EventBits_t RUN = BIT0;
constexpr EventBits_t IDLE = BIT1;

}  // namespace

bool SdPrefetch::begin() {
  auto* ring = static_cast<uint8_t*>(heap_caps_malloc(SD_PREFETCH_RING_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  chunk_ = static_cast<uint8_t*>(heap_caps_malloc(SD_PREFETCH_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  events_ = xEventGroupCreate();
  if (ring == nullptr || chunk_ == nullptr || events_ == nullptr) {
    PLOG("MEDIA", "cannot allocate the SD read-ahead");
    return false;
  }
  ring_.attach(ring, SD_PREFETCH_RING_SIZE);
  xEventGroupSetBits(events_, IDLE);
  if (xTaskCreatePinnedToCore(taskEntry, "sdread", SD_PREFETCH_TASK_STACK, this, SD_PREFETCH_TASK_PRIO, nullptr,
                              SD_PREFETCH_TASK_CORE) != pdPASS) {
    PLOG("MEDIA", "cannot create the SD read-ahead task");
    return false;
  }
  return true;
}

bool SdPrefetch::start(SdFile& file) {
  if (!stop()) return false;
  file_ = &file;
  ring_.clear();  // the task is idle: nobody else touches the ring
  ended_ = false;
  cancelled_ = false;
  xEventGroupClearBits(events_, IDLE);  // before RUN, so stop() never sees a stale IDLE
  xEventGroupSetBits(events_, RUN);
  return true;
}

bool SdPrefetch::stop() {
  if (events_ == nullptr) return true;
  cancelled_ = true;  // also ends a readBytes() that waits for data
  const EventBits_t bits = xEventGroupWaitBits(events_, IDLE, pdFALSE, pdTRUE, pdMS_TO_TICKS(MEDIA_STOP_TIMEOUT_MS));
  return (bits & IDLE) != 0;
}

size_t SdPrefetch::readBytes(char* buf, size_t len) {
  uint32_t got = 0;
  const RingRead r = readWaiting(
      ring_, reinterpret_cast<uint8_t*>(buf), static_cast<uint32_t>(len), got, ended_, cancelled_, SD_PREFETCH_STALL_MS,
      [] { return static_cast<uint32_t>(millis()); }, [] { vTaskDelay(1); });
  return r == RingRead::Data ? got : 0;
}

void SdPrefetch::taskEntry(void* self) { static_cast<SdPrefetch*>(self)->run(); }

void SdPrefetch::run() {
  for (;;) {
    xEventGroupWaitBits(events_, RUN, pdTRUE, pdTRUE, portMAX_DELAY);
    fill();
    xEventGroupSetBits(events_, IDLE);
  }
}

// Whole chunks, whenever the ring has room for one. A read of 0 (the end of the file, or the
// card refused it) ends the stream; App tells the two apart by the file's position.
void SdPrefetch::fill() {
  while (!cancelled_.load()) {
    if (ring_.space() < SD_PREFETCH_CHUNK) {
      vTaskDelay(1);  // the player is a ring ahead: nothing to do yet
      continue;
    }
    const size_t n = file_->read(chunk_, SD_PREFETCH_CHUNK);
    if (n == 0) {
      ended_ = true;
      return;
    }
    ring_.push(chunk_, static_cast<uint32_t>(n));
  }
}
