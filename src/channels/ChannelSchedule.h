#pragma once

// A local channel's programme for "already on air": its episodes in broadcast order
// (compareEpisodeNames: natural order of the converted names, which keep the episode numbers)
// with each length read from its .idx. Built once per channel per boot, on first tune-in, in PSRAM; never while playing.

#include <stddef.h>
#include <stdint.h>

#include "channels/ChannelManager.h"

class StorageManager;

constexpr size_t MAX_EPISODES_PER_CHANNEL = 256;

class ChannelSchedule {
 public:
  enum class State : uint8_t {
    Unbuilt,
    OnAir,    // every episode has a usable .idx: tune in mid-programme
    NoIndex,  // at least one .idx missing or broken: V0.1 behaviour (random, from the start)
  };

  ChannelSchedule() = default;
  ChannelSchedule(const ChannelSchedule&) = delete;  // owns PSRAM buffers
  ChannelSchedule& operator=(const ChannelSchedule&) = delete;
  ~ChannelSchedule() { release(); }

  void build(const StorageManager& storage, const Channel& ch);
  // Forget the programme; the next tune-in builds it again.
  void reset() {
    release();
    state_ = State::Unbuilt;
  }

  State state() const { return state_; }
  bool onAir() const { return state_ == State::OnAir; }
  size_t count() const { return count_; }
  const char* path(size_t i) const;
  const uint32_t* durations() const { return durations_; }
  // Takes over what `other` built (the background builder's), leaving it Unbuilt and empty.
  void adopt(ChannelSchedule& other) {
    release();
    state_ = other.state_;
    count_ = other.count_;
    paths_ = other.paths_;
    durations_ = other.durations_;
    other.state_ = State::Unbuilt;
    other.count_ = 0;
    other.paths_ = nullptr;
    other.durations_ = nullptr;
  }

 private:
  void release();

  State state_ = State::Unbuilt;
  size_t count_ = 0;
  char* paths_ = nullptr;          // count_ rows of MEDIA_PATH_MAX, PSRAM
  uint32_t* durations_ = nullptr;  // ms, PSRAM
};
