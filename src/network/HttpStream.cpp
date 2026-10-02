#include "network/HttpStream.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "config.h"

bool HttpStream::begin(uint32_t capacity) {
  auto* buf = static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return false;
  ring_.attach(buf, capacity);
  return true;
}

size_t HttpStream::readBytes(char* buffer, size_t length) {
  const uint32_t t0 = millis();
  if (ring_.size() == 0 && !ended_) {  // a stall the player will feel, once per empty spell
    if (!starved_) ++underruns_;
    starved_ = true;
  } else {
    starved_ = false;
  }
  uint32_t got = 0;
  const RingRead r = readWaiting(
      ring_, reinterpret_cast<uint8_t*>(buffer), static_cast<uint32_t>(length), got, ended_, cancelled_,
      REMOTE_STALL_MS, [] { return static_cast<uint32_t>(millis()); },
      [] { vTaskDelay(pdMS_TO_TICKS(REMOTE_READ_SLICE_MS)); });
  waitedMs_ += millis() - t0;
  if (r == RingRead::Stalled) ++stalls_;
  return got;
}

int HttpStream::read() {
  char c;
  return readBytes(&c, 1) == 1 ? static_cast<uint8_t>(c) : -1;
}

void HttpStream::reset() {
  ring_.clear();
  starved_ = false;
  ended_ = false;
  cancelled_ = false;
}
