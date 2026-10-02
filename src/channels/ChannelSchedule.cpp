#include "channels/ChannelSchedule.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>

#include "app_types.h"
#include "media/EpisodeIndex.h"
#include "storage/SdLayout.h"
#include "storage/StorageManager.h"

namespace {

// One look-up in the folder and one sector: POSIX open (no exists() first, no 4 KB stdio fill).
bool readLength(const StorageManager& storage, const char* videoPath, uint32_t& durationMs) {
  char idxPath[MEDIA_PATH_MAX];
  if (!indexPathFor(videoPath, idxPath, sizeof(idxPath))) return false;
  SdFile f;
  if (!storage.open(idxPath, f)) return false;
  uint8_t head[INDEX_HEADER_SIZE];
  const bool read = f.read(head, sizeof(head)) == sizeof(head);
  f.close();
  IndexHeader h;
  if (!read || !parseIndexHeader(head, sizeof(head), h) || h.durationMs == 0) return false;
  durationMs = h.durationMs;
  return true;
}

void formatLength(uint64_t ms, char* out, size_t len) {
  const uint64_t s = ms / 1000;
  snprintf(out, len, "%u:%02u:%02u", static_cast<unsigned>(s / 3600), static_cast<unsigned>(s / 60 % 60),
           static_cast<unsigned>(s % 60));
}

}  // namespace

void ChannelSchedule::release() {
  heap_caps_free(paths_);
  heap_caps_free(durations_);
  paths_ = nullptr;
  durations_ = nullptr;
  count_ = 0;
}

const char* ChannelSchedule::path(size_t i) const { return paths_ + i * MEDIA_PATH_MAX; }

void ChannelSchedule::build(const StorageManager& storage, const Channel& ch) {
  release();
  state_ = State::NoIndex;
  const uint32_t t0 = millis();

  const bool single = isEpisodeFile(ch.source);
  size_t wanted = single ? 1 : storage.countEpisodes(ch.source);
  if (wanted == 0) return;  // playLocal reports NO EPISODES
  if (wanted > MAX_EPISODES_PER_CHANNEL) {
    PLOG("CHANNEL", "CH%02u: %u episodes, only the first %u are scheduled", ch.number,
         static_cast<unsigned>(wanted), static_cast<unsigned>(MAX_EPISODES_PER_CHANNEL));
    wanted = MAX_EPISODES_PER_CHANNEL;
  }

  paths_ = static_cast<char*>(heap_caps_malloc(wanted * MEDIA_PATH_MAX, MALLOC_CAP_SPIRAM));
  durations_ = static_cast<uint32_t*>(heap_caps_malloc(wanted * sizeof(uint32_t), MALLOC_CAP_SPIRAM));
  if (paths_ == nullptr || durations_ == nullptr) {
    PLOG("CHANNEL", "CH%02u: no memory for the schedule", ch.number);
    release();
    return;
  }
  if (single) {
    snprintf(paths_, MEDIA_PATH_MAX, "%s", ch.source);
    count_ = 1;
  } else {
    count_ = storage.listEpisodes(ch.source, paths_, MEDIA_PATH_MAX, wanted);
    qsort(paths_, count_, MEDIA_PATH_MAX, [](const void* a, const void* b) {
      return compareEpisodeNames(static_cast<const char*>(a), static_cast<const char*>(b));
    });
  }
  if (count_ == 0) {  // e.g. every name too long for MEDIA_PATH_MAX: never an empty schedule
    release();
    return;
  }

  uint64_t total = 0;
  for (size_t i = 0; i < count_; ++i) {
    if (!readLength(storage, path(i), durations_[i])) {
      PLOG("CHANNEL", "CH%02u: no usable .idx for %s -> episodes start from the beginning "
           "(run tools/make_index.py)", ch.number, path(i));
      release();
      return;
    }
    total += durations_[i];
  }
  state_ = State::OnAir;
  char length[16];
  formatLength(total, length, sizeof(length));
  PLOG("CHANNEL", "CH%02u schedule: %u episodes, %s on air loop (built in %lu ms)", ch.number,
       static_cast<unsigned>(count_), length, millis() - t0);
}
