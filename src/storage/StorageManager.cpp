#include "storage/StorageManager.h"

#include <SD_MMC.h>

#include "app_types.h"
#include "board_config.h"
#include "config.h"
#include "storage/SdLayout.h"

namespace {

constexpr const char* MOUNT_POINT = "/sdcard";
constexpr bool MODE_1BIT = false;             // the board wires all four data lines
constexpr bool FORMAT_IF_MOUNT_FAILED = false;
// 40 MHz first. 20 MHz does not avoid the read timeouts under Wi-Fi scans (2026-09-30: 7 in
// 4.5 min at 20 MHz, 22 in 20 min at 40): the fix is mounting the card again (App::recoverSd).
constexpr int MOUNT_FREQS_KHZ[] = {SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_DEFAULT};  // 40, then 20 MHz

const char* cardTypeName(sdcard_type_t t) {
  switch (t) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    case CARD_NONE:
    case CARD_UNKNOWN: break;
  }
  return "unknown";
}

}  // namespace

bool StorageManager::begin() {
  if (!mount()) {
    PLOG("SD", "NOT FOUND: no card, not FAT32, or bad contact");
    return false;
  }
  createLayout();
  for (const char* path : {sdpath::CHANNELS_JSON, sdpath::WIFI_JSON}) {  // a replace cut by power loss
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!SD_MMC.exists(path) && SD_MMC.exists(tmp) && SD_MMC.rename(tmp, path)) PLOG("SD", "recovered %s", path);
  }
  return true;
}

bool StorageManager::writeFileAtomic(const char* path, const char* data, size_t len) {
  if (!mounted_) return false;
  char tmp[96];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) return false;
  const bool written = f.write(reinterpret_cast<const uint8_t*>(data), len) == len;
  f.close();
  if (!written || (SD_MMC.exists(path) && !SD_MMC.remove(path))) {
    SD_MMC.remove(tmp);
    return false;
  }
  return SD_MMC.rename(tmp, path);
}

bool StorageManager::ensureMounted() { return mounted_ || begin(); }

bool StorageManager::remount() {
  SD_MMC.end();
  mounted_ = false;
  return mount();
}

bool StorageManager::mount() {
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3)) {
    PLOG("SD", "cannot assign SD_MMC pins");
    return false;
  }
  for (const int freq : MOUNT_FREQS_KHZ) {
    for (uint8_t attempt = 1; attempt <= SD_MOUNT_ATTEMPTS_PER_FREQ; ++attempt) {
      if (SD_MMC.begin(MOUNT_POINT, MODE_1BIT, FORMAT_IF_MOUNT_FAILED, freq, SD_MAX_OPEN_FILES)) {
        mounted_ = true;
        freqKhz_ = freq;
        PLOG("SD", "mounted 4-bit at %d kHz, %s, %llu MB, %llu MB used", freq,
             cardTypeName(SD_MMC.cardType()), SD_MMC.totalBytes() >> 20, SD_MMC.usedBytes() >> 20);
        return true;
      }
      SD_MMC.end();
      PLOG("SD", "mount failed at %d kHz (attempt %u)", freq, attempt);
    }
  }
  return false;
}

void StorageManager::createLayout() {
  for (const char* dir : sdpath::LAYOUT_DIRS) {
    if (SD_MMC.exists(dir)) continue;
    PLOG("SD", "%s %s", SD_MMC.mkdir(dir) ? "created" : "CANNOT CREATE", dir);
  }
  if (SD_MMC.exists(sdpath::CHANNELS_JSON)) return;

  File f = SD_MMC.open(sdpath::CHANNELS_JSON, FILE_WRITE);
  const size_t len = strlen(DEFAULT_CHANNELS_JSON);
  const bool ok = f && f.print(DEFAULT_CHANNELS_JSON) == len;
  if (f) f.close();
  PLOG("SD", "%s default %s", ok ? "wrote" : "CANNOT WRITE", sdpath::CHANNELS_JSON);
}

uint64_t StorageManager::totalBytes() const { return mounted_ ? SD_MMC.totalBytes() : 0; }
uint64_t StorageManager::usedBytes() const { return mounted_ ? SD_MMC.usedBytes() : 0; }
bool StorageManager::exists(const char* path) const { return mounted_ && SD_MMC.exists(path); }

fs::File StorageManager::open(const char* path) const {
  if (!exists(path)) return fs::File();
  return SD_MMC.open(path, FILE_READ);
}

bool StorageManager::open(const char* path, SdFile& f) const {
  char full[MEDIA_PATH_MAX + 8];
  if (!mounted_ || snprintf(full, sizeof(full), "%s%s", MOUNT_POINT, path) >= static_cast<int>(sizeof(full))) {
    f.close();
    return false;
  }
  return f.open(full);
}

size_t StorageManager::countEpisodes(const char* dir) const {
  size_t count = 0;
  fs::File folder = open(dir);
  if (!folder || !folder.isDirectory()) return 0;
  for (fs::File f = folder.openNextFile(); f; f = folder.openNextFile()) {
    if (!f.isDirectory() && isEpisodeFile(f.name())) ++count;
  }
  return count;
}

size_t StorageManager::listEpisodes(const char* dir, char* paths, size_t pathLen, size_t max) const {
  fs::File folder = open(dir);
  if (!folder || !folder.isDirectory()) return 0;
  size_t n = 0;
  for (fs::File f = folder.openNextFile(); f && n < max; f = folder.openNextFile()) {
    if (f.isDirectory() || !isEpisodeFile(f.name())) continue;
    const int w = snprintf(paths + n * pathLen, pathLen, "%s/%s", dir, f.name());
    if (w > 0 && static_cast<size_t>(w) < pathLen) ++n;  // too long: skipped, never truncated
  }
  return n;
}

bool StorageManager::episodePath(const char* dir, size_t index, char* out, size_t outLen) const {
  fs::File folder = open(dir);
  if (!folder || !folder.isDirectory()) return false;
  size_t i = 0;
  for (fs::File f = folder.openNextFile(); f; f = folder.openNextFile()) {
    if (f.isDirectory() || !isEpisodeFile(f.name())) continue;
    if (i++ < index) continue;
    const int n = snprintf(out, outLen, "%s/%s", dir, f.name());
    return n > 0 && static_cast<size_t>(n) < outLen;
  }
  return false;
}

bool StorageManager::loadJson(const char* path, JsonDocument& doc, char* error,
                              size_t errorLen) const {
  if (!mounted_) {
    snprintf(error, errorLen, "no SD card");
    return false;
  }
  if (!SD_MMC.exists(path)) {
    snprintf(error, errorLen, "missing");
    return false;
  }
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    snprintf(error, errorLen, "cannot open");
    return false;
  }
  if (f.size() > JSON_FILE_MAX_BYTES) {
    f.close();
    snprintf(error, errorLen, "too big (> %u bytes)", static_cast<unsigned>(JSON_FILE_MAX_BYTES));
    return false;
  }
  const DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    snprintf(error, errorLen, "invalid JSON: %s", e.c_str());
    return false;
  }
  return true;
}
