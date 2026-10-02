#pragma once

#include <ArduinoJson.h>
#include <FS.h>
#include <stddef.h>
#include <stdint.h>

#include "storage/SdFile.h"

// microSD over SD_MMC 4-bit. Mounts at 40 MHz, falls back to 20 MHz, never formats
// (a bad contact must not wipe the episodes). Creates the /retrotv layout on first use.
class StorageManager {
 public:
  bool begin();
  // Retries the mount when there was no card at boot. Cheap when already mounted.
  bool ensureMounted();
  // The card initialised again, as if taken out and put back. Call with no file open.
  bool remount();

  bool mounted() const { return mounted_; }
  uint32_t freqKhz() const { return freqKhz_; }
  uint64_t totalBytes() const;
  uint64_t usedBytes() const;
  bool exists(const char* path) const;
  // Read-only file; invalid (false) when missing or no card.
  fs::File open(const char* path) const;
  // The same for the player's episode files (SdFile.h: faster block reads). False when missing.
  bool open(const char* path, SdFile& f) const;

  // Episodes (*.mjpeg, hidden files skipped) in a folder, in directory order. Scans the
  // folder each call: meant for channel start, never during playback.
  size_t countEpisodes(const char* dir) const;
  // Full paths of up to `max` episodes in one pass, rows of `pathLen` bytes. Returns how many.
  size_t listEpisodes(const char* dir, char* paths, size_t pathLen, size_t max) const;
  bool episodePath(const char* dir, size_t index, char* out, size_t outLen) const;

  // Reads and parses a small JSON file. On failure `error` says why (missing, too big, invalid).
  bool loadJson(const char* path, JsonDocument& doc, char* error, size_t errorLen) const;
  // Writes <path>.tmp and then puts it in place of <path>: a power cut never leaves a half file.
  // FAT has no atomic replace; if the cut falls between the remove and the rename, begin() puts
  // the .tmp back.
  bool writeFileAtomic(const char* path, const char* data, size_t len);

 private:
  bool mount();
  void createLayout();

  bool mounted_ = false;
  uint32_t freqKhz_ = 0;
};
