#pragma once

// Builds a channel's programme (ChannelSchedule) on its own task, off the loop. Reading the .idx
// headers of a channel with many episodes takes seconds while a video plays (measured
// 2026-10-01: 26 episodes, 35 s on the loop, which shares its core with the display), and the
// loop would answer neither the knobs nor the web remote meanwhile. One channel at a time: the
// loop asks, the task builds, the loop adopts the result.

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "channels/ChannelManager.h"
#include "channels/ChannelSchedule.h"

class StorageManager;

class ScheduleBuilder {
 public:
  bool begin(const StorageManager& storage);
  // Loop task: build this channel's programme. False while another one is being built.
  bool request(uint8_t slot, const Channel& ch);
  bool busy() const { return state_.load() == BUSY; }
  // Loop task: true when one is done, with its slot; adopt result(), then release().
  bool done(uint8_t& slot) const;
  ChannelSchedule& result() { return work_; }
  void release();
  // Waits up to `ms` for the task to stop touching the card (it is about to be mounted again).
  bool waitIdle(uint32_t ms) const;

 private:
  static constexpr uint8_t IDLE = 0;
  static constexpr uint8_t BUSY = 1;
  static constexpr uint8_t DONE = 2;

  static void taskEntry(void* self);
  void run();

  const StorageManager* storage_ = nullptr;
  TaskHandle_t task_ = nullptr;
  ChannelSchedule work_;
  Channel channel_ = {};
  uint8_t slot_ = 0;
  std::atomic<uint8_t> state_{IDLE};
};
