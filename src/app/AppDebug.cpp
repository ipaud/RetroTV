// Serial-monitor tools, compiled only with PAUTV_DEBUG_STATS and idle until a command is typed.
//
//   n p + - x o M q stand-ins for the knobs: channel +/-, volume +/-, mute, OSD, menu, standby
//   m               memory snapshot
//   d               dithering on/off (video: 32-level panel)
//   w               the battery warning, to see it without a flat cell
//   z               quick stress: STRESS_ZAP_COUNT channel changes, one every 1.5 s
//   S<seconds>      soak test: zap every <seconds> (S0 = never, S = SOAK_DEFAULT_ZAP_S) and log
//                   a snapshot every SOAK_SNAPSHOT_MS; s stops it
//   T <path|url>    test tune: play a folder, an .mjpeg or a remote channel
//                   (http://<server>/channel/<n>) as a temporary channel (device tests);
//                   any channel change returns to channels.json
//   g<page>         teletext: go to page 100, 101 or 2NN
//   B <n> <url>     network bench: n parallel connections to a server's /api/bench for 10 s
//   D <s> <folder> [<url>] [scan]  SD stress: read an episode of <folder> flat out for <s>
//                   seconds, 4 KB at a time like the player; meanwhile one /api/bench
//                   connection receiving (<url>) and/or a Wi-Fi scan every 10 s (scan: the radio
//                   transmitting on every channel); counts failed reads, each followed by a
//                   remount of the card
//
// S, T, g, B and D take an argument and run on Enter; everything else acts on the keystroke.

#include "app/App.h"
#include "config.h"

#if PAUTV_DEBUG_STATS

#include <stdlib.h>

#include <WiFi.h>

#include "network/RemoteProtocol.h"

namespace {

InputEvent knobFor(char c) {
  switch (c) {
    case 'n': return InputEvent::ChNext;
    case 'p': return InputEvent::ChPrev;
    case '+': return InputEvent::VolUp;
    case '-': return InputEvent::VolDown;
    case 'x': return InputEvent::Mute;
    case 'q': return InputEvent::Power;
    case 'o': return InputEvent::ToggleOsd;
    case 'M': return InputEvent::Menu;
    default: return InputEvent::None;
  }
}

}  // namespace

void App::pollSerialCommands(uint32_t nowMs) {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (serialLen_ > 0 || c == 'T' || c == 'S' || c == 'g' || c == 'B' || c == 'D') {  // a command with an argument: collect the line
      if (c == '\n' || c == '\r') {
        serialLine_[serialLen_] = '\0';
        serialLen_ = 0;
        const char* arg = serialLine_ + 1;
        while (*arg == ' ') ++arg;
        runSerialCommand(serialLine_[0], arg, nowMs);
      } else if (serialLen_ + 1 < sizeof(serialLine_)) {
        serialLine_[serialLen_++] = c;
      }
      continue;
    }
    runSerialCommand(c, "", nowMs);
  }
  updateSoak(nowMs);
}

void App::runSerialCommand(char command, const char* argument, uint32_t nowMs) {
  switch (command) {
    case 'T':
      tuneTestPath(argument);
      return;
    case 'g':
      teletextGoTo(static_cast<uint16_t>(atoi(argument)));
      return;
    case 'B':
      runBench(argument);
      return;
    case 'D':
      runDiskStress(argument);
      return;
    case 'S':
      startSoak((argument[0] != '\0' ? static_cast<uint32_t>(atoi(argument)) : SOAK_DEFAULT_ZAP_S) * 1000u,
                0, nowMs);
      return;
    case 's':
      if (soakActive_) {
        logSoakSnapshot("stop", nowMs);
        soakActive_ = false;
      }
      return;
    case 'z':
      startSoak(STRESS_ZAP_INTERVAL_MS, STRESS_ZAP_COUNT, nowMs);
      return;
    case 'm':
      logMemory("snapshot");
      return;
    case 'v':  // MIC TEST: microphone levels in the log for MIC_TEST_MS (voice builds)
      startMicTest(nowMs);
      return;
    case 'V':  // pause / resume the microphone capture
      toggleMicCapture();
      return;
    case 'R':  // GRABADORA: start a message, or stop the one being recorded (= a key)
      if (state_ == AppState::Recorder) {
        onRecorderInput(InputEvent::VolUp);
      } else {
        startRecorder();
      }
      return;
    case 'Y':  // a synthetic test message (a tone), saved like a recording, without the microphone
      saveTestMessage();
      return;
    case 'E':  // delete every saved message
      deleteMessages();
      return;
    case 'F':  // the standby a flat battery gets (button-less: it wakes itself every FLAT_CHECK_S)
      PLOG("POWER", "serial F: flat-battery standby");
      enterStandby(false);
      return;
    case 'w':
      showBatteryWarning();
      return;
    case 'd': {
      player_.setDither(!player_.dither());
      PLOG("VIDEO", "dither %s", player_.dither() ? "ON" : "OFF");
      OsdState o;  // on screen too: whoever compares sees which is which
      o.visible = true;
      snprintf(o.title, sizeof(o.title), "DITHER");
      snprintf(o.subtitle, sizeof(o.subtitle), "%s", player_.dither() ? "ON" : "OFF");
      publishOsd(o, OSD_CHANNEL_MS);
      return;
    }
    default: {
      const InputEvent e = knobFor(command);
      if (e != InputEvent::None) onInput(e);
    }
  }
}

void App::tuneTestPath(const char* path) {
  const bool fromStart = path[0] == '!';  // `T !/retrotv/...`: from the beginning, e.g. an intro song
  if (fromStart) ++path;
  const bool internal = strcmp(path, INTERNAL_MESSAGES) == 0 || strcmp(path, INTERNAL_TELETEXT) == 0 ||
                        strcmp(path, INTERNAL_TESTCARD) == 0;  // `T messages`: an internal channel
  const bool remote = strncmp(path, "http://", 7) == 0;
  if (!internal && ((remote ? !isRemoteChannelUrl(path) : path[0] != '/') || strlen(path) >= CHANNEL_SOURCE_LEN)) {
    PLOG("CHANNEL", "test tune needs an absolute path or http://<server>/channel/<n>, shorter than %u",
         static_cast<unsigned>(CHANNEL_SOURCE_LEN));
    return;
  }
  if (!stopProgramme()) return;
  testChannel_ = Channel{};
  snprintf(testChannel_.id, sizeof(testChannel_.id), "test");
  snprintf(testChannel_.name, sizeof(testChannel_.name), "TEST");
  snprintf(testChannel_.source, sizeof(testChannel_.source), "%s", path);
  testChannel_.type = internal ? ChannelType::Internal : (remote ? ChannelType::Remote : ChannelType::Local);
  testChannel_.enabled = true;
  testSchedule_.reset();  // rebuilt from the card: fixtures may have changed
  testTune_ = true;
  advanceEpisode_ = false;
  currentEpisode_ = 0;
  lastEpisode_ = -1;
  remoteRetryDue_ = false;
  remoteAttempt_ = 0;
  testFromStart_ = fromStart;
  PLOG("CHANNEL", "test tune %s%s", path, fromStart ? " from 0:00" : "");
  enter(AppState::Playing);
}

// zapIntervalMs 0 = never zap; zapLimit 0 = no limit.
void App::startSoak(uint32_t zapIntervalMs, uint16_t zapLimit, uint32_t nowMs) {
  soakActive_ = true;
  soakZapIntervalMs_ = zapIntervalMs;
  soakZapsLeft_ = zapLimit;
  soakZaps_ = 0;
  soakStartMs_ = soakLastZapMs_ = soakLastSnapMs_ = nowMs;
  soakBase_ = soakPrev_ = player_.counters();
  soakDisplayBase_ = display_.heartbeat();
  player_.takeDriftMax(DriftReader::Soak);
  soakStartHeap_ = ESP.getFreeHeap();
  soakStartPsram_ = ESP.getFreePsram();
  PLOG("SOAK", "start: zap every %u.%u s%s, snapshot every %u s", zapIntervalMs / 1000, zapIntervalMs % 1000 / 100,
       zapLimit > 0 ? " (limited run)" : "", SOAK_SNAPSHOT_MS / 1000);
  logSoakSnapshot("start", nowMs);
}

void App::updateSoak(uint32_t nowMs) {
  if (!soakActive_) return;
  if (soakZapIntervalMs_ > 0 && state_ == AppState::Playing && nowMs - soakLastZapMs_ >= soakZapIntervalMs_) {
    soakLastZapMs_ = nowMs;
    ++soakZaps_;
    onInput(soakZaps_ % 3 == 0 ? InputEvent::ChPrev : InputEvent::ChNext);  // mostly forward
    if (soakZapsLeft_ > 0 && --soakZapsLeft_ == 0) {
      logSoakSnapshot("done", nowMs);
      soakActive_ = false;
      return;
    }
  }
  if (nowMs - soakLastSnapMs_ >= SOAK_SNAPSHOT_MS) logSoakSnapshot("snapshot", nowMs);
}

// Totals since the soak started; heartbeats as deltas since the previous snapshot, so a task
// that stopped moving while video plays shows up as STALLED.
void App::logSoakSnapshot(const char* label, uint32_t nowMs) {
  const bool interval = nowMs != soakLastSnapMs_;  // the start line has no heartbeats to judge yet
  soakLastSnapMs_ = nowMs;
  const PlaybackCounters c = player_.counters();
  const uint32_t display = display_.heartbeat();
  const uint32_t shown = c.shown - soakBase_.shown;
  const int32_t driftSum = static_cast<int32_t>(c.driftSumMs - soakBase_.driftSumMs);
  const uint32_t beatsV = c.videoBeats - soakPrev_.videoBeats;
  const uint32_t beatsA = c.audioBeats - soakPrev_.audioBeats;
  const uint32_t beatsD = display - soakDisplayBase_;
  const bool stalled =
      interval && playMode_ == PlayMode::Video && soakPrev_.shown != 0 && (beatsV == 0 || beatsD == 0);
  const uint32_t s = (nowMs - soakStartMs_) / 1000;

  PLOG("SOAK",
       "%s %02u:%02u:%02u zaps %u | heap %u KB (start %u, min %u, largest %u) psram %u KB (start %u) | "
       "shown %u dropped_frames %u bad %u av_drift_ms avg %d max %d | aerr %u | beats +v%u +a%u +d%u%s",
       label, s / 3600, s / 60 % 60, s % 60, soakZaps_, ESP.getFreeHeap() / 1024, soakStartHeap_ / 1024,
       ESP.getMinFreeHeap() / 1024, ESP.getMaxAllocHeap() / 1024, ESP.getFreePsram() / 1024,
       soakStartPsram_ / 1024, shown, c.dropped - soakBase_.dropped, c.bad - soakBase_.bad,
       shown ? static_cast<int>(driftSum / static_cast<int32_t>(shown)) : 0,
       static_cast<int>(player_.takeDriftMax(DriftReader::Soak)), c.audioErrors - soakBase_.audioErrors,
       beatsV, beatsA, beatsD, stalled ? " STALLED" : "");
  soakPrev_ = c;
  soakDisplayBase_ = display;
}

// [TUNE]: time to the first picture and to stable playback (STABLE_SECONDS clean seconds in a
// row: no dropped frame, no read stall, a full second of frames), from the viewer's tune.
void App::trackRemotePlayback(uint32_t nowMs) {
  if (!remoteActive_ || playMode_ != PlayMode::Video) return;
  const PlaybackCounters c = player_.counters();
  if (!firstVideoLogged_ && c.shown != shownAtStart_) {
    firstVideoLogged_ = true;
    PLOG("TUNE", "time_to_first_video_ms %lu", static_cast<unsigned long>(nowMs - tuneStartMs_));
  }
  if (static_cast<int32_t>(nowMs - secondMarkMs_) < 1000) return;
  const uint32_t underruns = remote_.stats().underruns;
  const uint32_t shown = c.shown - secondBase_.shown;
  const bool clean = c.dropped == secondBase_.dropped && underruns == secondUnderruns_ && shown + 2 >= remote_.fps();
  cleanSeconds_ = clean ? static_cast<uint8_t>(cleanSeconds_ < UINT8_MAX ? cleanSeconds_ + 1 : cleanSeconds_) : 0;
  if (!stableLogged_ && cleanSeconds_ >= STABLE_SECONDS) {
    stableLogged_ = true;
    PLOG("TUNE", "time_to_stable_playback_ms %lu",
         static_cast<unsigned long>(nowMs - STABLE_SECONDS * 1000u - tuneStartMs_));
  }
  secondMarkMs_ = nowMs;
  secondBase_ = c;
  secondUnderruns_ = underruns;
}

// [NET] while a remote channel plays: every second for the first NET_VERBOSE_MS after the tune,
// then next to the [MEDIA] line. Rates, stalls and drops cover the last period; buffers are now.
//   link: share of network-task passes that found data / nothing (waiting on the Wi-Fi) / a full
//   ring (waiting on the player); gap: longest wait for a video byte with room in the ring.
void App::logRemoteStats(uint32_t nowMs) {
  if (!remoteActive_ || playMode_ != PlayMode::Video) return;
  const bool verbose = nowMs - tuneStartMs_ < NET_VERBOSE_MS;
  const int32_t periodMs = static_cast<int32_t>(nowMs - netStatsSinceMs_);
  if (periodMs < static_cast<int32_t>(verbose ? 1000 : MEDIA_STATS_PERIOD_MS)) return;
  const RemoteSource::Stats n = remote_.stats();
  const PlaybackCounters p = player_.counters();
  const uint32_t shown = p.shown - netPlayerBase_.shown;
  const int32_t driftSum = static_cast<int32_t>(p.driftSumMs - netPlayerBase_.driftSumMs);
  const uint32_t passes = (n.passesData - netBase_.passesData) + (n.passesIdle - netBase_.passesIdle) +
                          (n.passesFull - netBase_.passesFull);
  auto share = [passes](uint32_t part) { return passes ? static_cast<unsigned>(part * 100u / passes) : 0u; };
  auto fill = [](uint32_t used, uint32_t capacity) { return capacity ? static_cast<unsigned>(used * 100u / capacity) : 0u; };
  PLOG("NET",
       "t=%lus network_kbps %u buffer_bytes v%u a%u buffer_fill v%u%% a%u%% read_stalls +%u wait_ms %u timeouts %u "
       "reconnects %u rssi %d dropped_frames +%u fps %u av_drift_ms avg %d max %d link data %u%% idle %u%% full %u%% "
       "gap_max_ms %u",
       static_cast<unsigned long>((nowMs - tuneStartMs_) / 1000),
       static_cast<unsigned>((n.bytes - netBase_.bytes) * 8u / static_cast<uint32_t>(periodMs)),
       static_cast<unsigned>(n.videoBuffered), static_cast<unsigned>(n.audioBuffered),
       fill(n.videoBuffered, n.videoCapacity), fill(n.audioBuffered, n.audioCapacity),
       static_cast<unsigned>(n.underruns - netBase_.underruns), static_cast<unsigned>(n.waitedMs - netBase_.waitedMs),
       static_cast<unsigned>(n.stalls), static_cast<unsigned>(n.reconnects), wifi_.rssi(),
       static_cast<unsigned>(p.dropped - netPlayerBase_.dropped),
       static_cast<unsigned>(shown * 1000u / static_cast<uint32_t>(periodMs)),
       shown ? static_cast<int>(driftSum / static_cast<int32_t>(shown)) : 0,
       static_cast<int>(player_.takeDriftMax(DriftReader::Net)), share(n.passesData - netBase_.passesData),
       share(n.passesIdle - netBase_.passesIdle), share(n.passesFull - netBase_.passesFull),
       static_cast<unsigned>(remote_.takeGapMaxMs()));
  netBase_ = n;
  netPlayerBase_ = p;
  netStatsSinceMs_ = nowMs;
}

// [WIFI] +Ns rssi, every second for the first 30 s after joining: the link's warm-up.
void App::logWifiWarmUp(uint32_t nowMs) {
  if (!wifi_.online()) {
    wifiOnlineMs_ = 0;
    return;
  }
  if (wifiOnlineMs_ == 0) {
    wifiOnlineMs_ = nowMs;
    wifiLoggedS_ = 0;
  }
  const uint32_t s = (nowMs - wifiOnlineMs_) / 1000;
  if (s < WIFI_WARMUP_LOG_S && s >= wifiLoggedS_) {
    PLOG("WIFI", "+%lus rssi %d dBm", static_cast<unsigned long>(s), wifi_.rssi());
    wifiLoggedS_ = s + 1;
  }
}

// B <n> <url>: raw throughput from a RETROTV Server bench endpoint over n parallel connections,
// nothing decoded. One connection is bound by its TCP window; if n connections carry about n
// times as much, the window is the limit, not the Wi-Fi. Blocks the loop for BENCH_MS (debug).
void App::runBench(const char* argument) {
  const int n = atoi(argument);
  const char* url = strchr(argument, ' ');
  HttpUrl u;
  IPAddress ip;
  if (n < 1 || n > BENCH_MAX_CONNECTIONS || url == nullptr || !parseHttpUrl(url + 1, u) ||
      (!ip.fromString(u.host) && WiFi.hostByName(u.host, ip) != 1)) {
    PLOG("BENCH", "usage: B <1-%d> http://<ip>:<port>/api/bench", BENCH_MAX_CONNECTIONS);
    return;
  }
  if (!stopProgramme()) return;
  static WiFiClient clients[BENCH_MAX_CONNECTIONS];
  static uint8_t buf[4096];
  uint32_t bytes[BENCH_MAX_CONNECTIONS] = {};
  for (int i = 0; i < n; ++i) {
    if (!clients[i].connect(ip, u.port, REMOTE_CONNECT_TIMEOUT_MS)) {
      PLOG("BENCH", "connection %d failed", i);
      continue;
    }
    clients[i].printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", u.path, u.host);
  }
  const uint32_t t0 = millis();
  uint32_t second = t0;
  uint32_t lastTotal = 0;
  while (millis() - t0 < BENCH_MS) {
    bool moved = false;
    for (int i = 0; i < n; ++i) {
      const int got = clients[i].available() > 0 ? clients[i].read(buf, sizeof(buf)) : 0;
      if (got > 0) {
        bytes[i] += static_cast<uint32_t>(got);
        moved = true;
      }
    }
    if (millis() - second >= 1000) {
      uint32_t total = 0;
      for (int i = 0; i < n; ++i) total += bytes[i];
      PLOG("BENCH", "+%lus %u kbps rssi %d", static_cast<unsigned long>((millis() - t0) / 1000),
           static_cast<unsigned>((total - lastTotal) * 8u / (millis() - second)), wifi_.rssi());
      lastTotal = total;
      second = millis();
    }
    if (!moved) vTaskDelay(1);
  }
  uint32_t total = 0;
  char per[64] = "";
  size_t at = 0;
  for (int i = 0; i < n; ++i) {
    total += bytes[i];
    at += snprintf(per + at, sizeof(per) - at, " %u", static_cast<unsigned>(bytes[i] / 1024 / (BENCH_MS / 1000)));
    clients[i].stop();
  }
  PLOG("BENCH", "result n=%d total %u KB/s per connection KB/s:%s", n,
       static_cast<unsigned>(total / 1024 / (BENCH_MS / 1000)), per);
  enter(AppState::Playing);
}

// The SD read timeouts (0x107) seen when a channel opened just after Wi-Fi activity: SD reads
// with the Wi-Fi busy, as fast as the card goes. A failed read reopens the file where it was.
void App::runDiskStress(const char* argument) {
  char args[CHANNEL_SOURCE_LEN + MEDIA_PATH_MAX];
  snprintf(args, sizeof(args), "%s", argument);
  char* rest = nullptr;
  const char* secondsText = strtok_r(args, " ", &rest);
  const char* folder = strtok_r(nullptr, " ", &rest);
  const char* url = nullptr;
  bool scan = false;
  for (const char* t = strtok_r(nullptr, " ", &rest); t != nullptr; t = strtok_r(nullptr, " ", &rest)) {
    if (strcmp(t, "scan") == 0) {
      scan = true;
    } else {
      url = t;
    }
  }
  const uint32_t seconds = secondsText != nullptr ? static_cast<uint32_t>(atoi(secondsText)) : 0;
  char path[MEDIA_PATH_MAX];
  HttpUrl u;
  IPAddress ip;
  const bool net = url != nullptr && parseHttpUrl(url, u) && ip.fromString(u.host);
  if (seconds < 1 || seconds > DISK_STRESS_MAX_S || folder == nullptr || (url != nullptr && !net) ||
      storage_.listEpisodes(folder, path, sizeof(path), 1) == 0) {
    PLOG("DISK", "usage: D <1-%u s> <folder with episodes> [http://<ip>:<port>/api/bench?seconds=30] [scan]",
         static_cast<unsigned>(DISK_STRESS_MAX_S));
    return;
  }
  if (!stopProgramme()) return;
  // The read-ahead's read size, file type and RAM: the bench reads the card like playback does.
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(SD_PREFETCH_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    PLOG("DISK", "no %u bytes of DMA-capable RAM for the read buffer", static_cast<unsigned>(SD_PREFETCH_CHUNK));
    enter(AppState::Playing);
    return;
  }
  static uint8_t netBuf[4096];
  SdFile f;
  storage_.open(path, f);
  WiFiClient client;
  uint64_t sdBytes = 0, lastSd = 0;  // GB in a long run
  uint32_t netBytes = 0, lastNet = 0, errors = 0, reopens = 0, benches = 0, scans = 0;
  PLOG("DISK", "reading %s (%u KB) for %u s%s%s at %u kHz", path, static_cast<unsigned>(f.size() / 1024),
       static_cast<unsigned>(seconds), net ? " with the Wi-Fi receiving" : "", scan ? " and scanning" : "",
       static_cast<unsigned>(storage_.freqKhz()));
  const uint32_t t0 = millis();
  uint32_t second = t0;
  uint32_t scanAt = t0;
  for (uint32_t reads = 0; f && millis() - t0 < seconds * 1000u; ++reads) {
    if (scan && millis() - scanAt >= DISK_STRESS_SCAN_MS) {
      scanAt = millis();
      if (wifi_.startUserScan()) ++scans;
    }
    if (net && !client.connected() && client.available() == 0) {
      client.stop();
      if (client.connect(ip, u.port, REMOTE_CONNECT_TIMEOUT_MS)) {
        client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", u.path, u.host);
        ++benches;
      }
    }
    if (client.available() > 0) {
      const int got = client.read(netBuf, sizeof(netBuf));
      if (got > 0) netBytes += static_cast<uint32_t>(got);
    }
    const size_t at = f.position();
    const size_t got = f.read(buf, SD_PREFETCH_CHUNK);
    sdBytes += got;
    if (got < SD_PREFETCH_CHUNK && at + got < f.size()) {
      ++errors;
      PLOG("DISK", "read failed at %u KB (got %u B): mounting the card again", static_cast<unsigned>(at / 1024),
           static_cast<unsigned>(got));
      f.close();
      for (int tries = 0; tries < SD_REMOUNTS_PER_WINDOW && !(f && f.size() > 0 && f.seek(at + got)); ++tries) {
        storage_.remount();  // it can fail again right after
        storage_.open(path, f);
      }
      if (f && f.position() == at + got) ++reopens;
    } else if (got < SD_PREFETCH_CHUNK) {
      f.seek(0);
    }
    if (millis() - second >= 1000) {
      const uint32_t ms = millis() - second;
      PLOG("DISK", "+%lus sd %u KB/s net %u KB/s errors %u rssi %d", static_cast<unsigned long>((millis() - t0) / 1000),
           static_cast<unsigned>((sdBytes - lastSd) / ms), static_cast<unsigned>((netBytes - lastNet) / ms),
           static_cast<unsigned>(errors), wifi_.rssi());
      lastSd = sdBytes;
      lastNet = netBytes;
      second = millis();
    }
    if (reads % 16 == 0) vTaskDelay(1);  // IDLE and the other tasks get the core
  }
  const uint32_t ms = millis() - t0;
  PLOG("DISK", "result %u s: sd %u KB/s, net %u KB/s (%u bench connections, %u scans), %u failed reads, %u reopened%s",
       static_cast<unsigned>(ms / 1000), static_cast<unsigned>(sdBytes / ms), static_cast<unsigned>(netBytes / ms),
       static_cast<unsigned>(benches), static_cast<unsigned>(scans), static_cast<unsigned>(errors), static_cast<unsigned>(reopens),
       f ? "" : ", FILE LOST");
  client.stop();
  heap_caps_free(buf);
  enter(AppState::Playing);
}

void App::logMemory(const char* label) const {
  PLOG("BOOT", "%s: heap %u KB free (min %u, largest block %u), psram %u KB free", label,
       ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024, ESP.getMaxAllocHeap() / 1024,
       ESP.getFreePsram() / 1024);
}

#endif  // PAUTV_DEBUG_STATS
