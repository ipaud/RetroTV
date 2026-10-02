#include "channels/ScheduleBuilder.h"

#include <Arduino.h>

#include "config.h"
#include "storage/StorageManager.h"

bool ScheduleBuilder::begin(const StorageManager& storage) {
  storage_ = &storage;
  return xTaskCreatePinnedToCore(taskEntry, "schedule", SCHEDULE_TASK_STACK, this, SCHEDULE_TASK_PRIO, &task_,
                                 SCHEDULE_TASK_CORE) == pdPASS;
}

bool ScheduleBuilder::request(uint8_t slot, const Channel& ch) {
  if (task_ == nullptr || state_.load() != IDLE) return false;
  slot_ = slot;
  channel_ = ch;
  state_ = BUSY;
  xTaskNotifyGive(task_);
  return true;
}

bool ScheduleBuilder::done(uint8_t& slot) const {
  if (state_.load() != DONE) return false;
  slot = slot_;
  return true;
}

void ScheduleBuilder::release() {
  work_.reset();  // empty after an adopt(); otherwise what nobody wanted
  state_ = IDLE;
}

bool ScheduleBuilder::waitIdle(uint32_t ms) const {
  const uint32_t t0 = millis();
  while (busy()) {
    if (millis() - t0 >= ms) return false;
    delay(10);
  }
  return true;
}

void ScheduleBuilder::taskEntry(void* self) { static_cast<ScheduleBuilder*>(self)->run(); }

void ScheduleBuilder::run() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    work_.build(*storage_, channel_);
    state_ = DONE;
  }
}
