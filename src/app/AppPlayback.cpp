// Channels, programmes, channel switching, volume and OSD.

#include "app/App.h"
#include <sys/time.h>

#include "config.h"
#include "media/EpisodeIndex.h"
#include "media/OnAir.h"
#include "network/RemoteProtocol.h"
#include "storage/SdLayout.h"

namespace {

constexpr int OSD_VOLUME_SEGMENTS = 20;

void formatMinSec(uint32_t ms, char* out, size_t len) {
  snprintf(out, len, "%u:%02u", static_cast<unsigned>(ms / 60000), static_cast<unsigned>(ms / 1000 % 60));
}

// Reads the 3 bytes at `offset` and leaves the file positioned there. False past the end.
bool headAt(SdFile& f, uint32_t offset, uint8_t* head) {
  return f.seek(offset) && f.read(head, 3) == 3 && f.seek(offset);
}

int wifiBarsFor(int rssi) {
  if (rssi >= -60) return 3;
  if (rssi >= -70) return 2;
  if (rssi >= -80) return 1;
  return 0;
}

}  // namespace

// Wall-clock milliseconds for the on-air position. Before NTP has set the clock, a session
// clock stands in: a random origin drawn once per boot plus uptime. Channels then still keep
// running while you zap away and back; only a reboot moves them. True with the real time.
bool App::onAirNow(uint64_t& nowMs) {
  timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < PAUTV_VALID_EPOCH) {
    static const uint64_t sessionOriginMs = static_cast<uint64_t>(esp_random()) * 1000u;
    nowMs = sessionOriginMs + millis();
    return false;
  }
  nowMs = static_cast<uint64_t>(tv.tv_sec) * 1000u + static_cast<uint64_t>(tv.tv_usec) / 1000u;
  return true;
}

void App::loadChannels() {
  auto problem = [this](const char* headline, const char* detail) {
    snprintf(startupError_, sizeof(startupError_), "%s", headline);
    snprintf(startupDetail_, sizeof(startupDetail_), "%s", detail);
  };

  JsonDocument doc;
  char error[48];
  if (!storage_.mounted()) {
    problem("SD NOT FOUND", "INSERT A FAT32 CARD");
  } else if (!storage_.loadJson(sdpath::CHANNELS_JSON, doc, error, sizeof(error))) {
    problem("CHANNELS.JSON", error);
  } else {
    addMessagesChannel(doc);  // voice builds, once: MENSAJES in the list without editing the file
    LoadReport report;
    const bool ok = channels_.load(doc.as<JsonVariantConst>(), report);
    if (report.skipped > 0) {
      PLOG("CHANNEL", "%u entries skipped, first: %s", report.skipped, report.firstProblem);
    }
    if (!ok) problem("CHANNELS.JSON", report.firstProblem);
  }

  if (startupError_[0] != '\0') {
    PLOG("CHANNEL", "%s: %s -> test card only", startupError_, startupDetail_);
    channels_.loadFallback();
  }
  for (size_t i = 0; i < channels_.count(); ++i) {
    const Channel& c = channels_.at(i);
    PLOG("CHANNEL", "%02u %-16s %-9s %s%s", c.number, c.name, channelTypeName(c.type), c.source,
         c.enabled ? "" : " (disabled)");
  }
}

// The channel on screen: the one from channels.json, or a debug test tune (serial 'T').
const Channel* App::tunedChannel() const { return testTune_ ? &testChannel_ : channels_.current(); }

void App::startProgramme() {
  const Channel* ch = tunedChannel();
  if (ch == nullptr) ch = channels_.selectOrFirst(settings_.lastChannel());
  if (ch == nullptr) {
    showNoSignal("NO CHANNELS", false);
    return;
  }
  if (!testTune_) settings_.setLastChannel(ch->number);
  modeSinceMs_ = millis();

  switch (ch->type) {
    case ChannelType::Internal:
      failedInARow_ = 0;
      if (strcmp(ch->source, INTERNAL_TELETEXT) == 0) {
        PLOG("CHANNEL", "CH%02u %s: teletext", ch->number, ch->name);
        startTeletext();
        return;
      }
      if (strcmp(ch->source, INTERNAL_MESSAGES) == 0) {
        PLOG("CHANNEL", "CH%02u %s: messages", ch->number, ch->name);
        startMessages();
        return;
      }
      if (strcmp(ch->source, INTERNAL_REMOTE_QR) == 0) {
        PLOG("CHANNEL", "CH%02u %s: remote QR", ch->number, ch->name);
        playMode_ = PlayMode::RemoteQr;
        remoteQrShown_ = false;
        publishRemoteQr();
        return;
      }
      playMode_ = PlayMode::TestCard;
      PLOG("CHANNEL", "CH%02u %s: test card", ch->number, ch->name);
      publishTestCard();
      return;
    case ChannelType::Local: {
      const char* reason = "";
      if (playLocal(*ch, reason) || (recoverSd() && playLocal(*ch, reason))) {
        playMode_ = PlayMode::Video;
        failedInARow_ = 0;
        return;
      }
      showNoSignal(reason, true);
      return;
    }
    case ChannelType::Remote:
      playRemote(*ch, false);
      return;
    case ChannelType::HlsProxy:
    case ChannelType::Tunarr:
    case ChannelType::Stream:
      PLOG("CHANNEL", "CH%02u %s: type %s arrives in V0.2", ch->number, ch->name,
           channelTypeName(ch->type));
      showNoSignal("V0.2", false);
      return;
  }
}

// With an index for every episode the channel is "already on air" (playOnAir). Without, V0.1
// behaviour: a file channel plays its file, a folder channel a random episode, never the one
// that just ended, from the beginning. `reason` says why not, for the NO SIGNAL screen.
bool App::playLocal(const Channel& ch, const char*& reason) {
  if (!storage_.ensureMounted()) {
    reason = "NO SD CARD";
    return false;
  }
  const int slot = channels_.currentIndex();
  ChannelSchedule* schedule = testTune_ ? &testSchedule_ : (slot >= 0 ? &schedules_[slot] : nullptr);
  if (schedule != nullptr) {
    if (schedule->state() == ChannelSchedule::State::Unbuilt) schedule->build(storage_, ch);
    if (schedule->onAir()) return playOnAir(ch, *schedule, reason);
  }
  advanceEpisode_ = false;
  char path[MEDIA_PATH_MAX];
  if (isEpisodeFile(ch.source)) {
    snprintf(path, sizeof(path), "%s", ch.source);
  } else {
    const size_t count = storage_.countEpisodes(ch.source);
    if (count == 0) {
      reason = "NO EPISODES";
      return false;
    }
    size_t index = esp_random() % count;
    if (count > 1 && static_cast<int32_t>(index) == lastEpisode_) index = (index + 1) % count;
    lastEpisode_ = static_cast<int32_t>(index);
    if (!storage_.episodePath(ch.source, index, path, sizeof(path))) {
      reason = "FILE ERROR";
      return false;
    }
  }
  if (!openMedia(path)) {
    reason = "FILE ERROR";
    return false;
  }
  if (!startLocalPlayer()) return false;
  PLOG("CHANNEL", "CH%02u %s: %s%s", ch.number, ch.name, path, audioFile_ ? "" : " (no audio)");
  return true;
}

// Tuning in lands where the programme is right now (UTC now modulo programme length); an
// episode that ends hands over to the next one in order, from its start.
bool App::playOnAir(const Channel& ch, const ChannelSchedule& schedule, const char*& reason) {
  uint64_t nowMs = 0;
  const bool ntp = onAirNow(nowMs);
  const char* how = advanceEpisode_ ? "next episode" : (ntp ? "on air" : "on air (session clock, no NTP yet)");
  const OnAirSlot slot =
      nextOnAirSlot(advanceEpisode_, currentEpisode_, nowMs, schedule.durations(), schedule.count());
  const size_t episode = slot.episode;
  const uint32_t offsetMs = testFromStart_ ? 0 : slot.offsetMs;  // serial `T !path`: from 0:00
  testFromStart_ = false;
  advanceEpisode_ = false;

  const char* path = schedule.path(episode);
  if (!openMedia(path)) {
    reason = "FILE ERROR";
    return false;
  }
  const uint32_t startMs = offsetMs > 0 ? seekMedia(path, offsetMs) : 0;
  currentEpisode_ = episode;
  if (!startLocalPlayer()) return false;

  char at[12];  // "mmmmm:ss": room for any uint32 of milliseconds
  char length[12];
  formatMinSec(startMs, at, sizeof(at));
  formatMinSec(schedule.durations()[episode], length, sizeof(length));
  PLOG("CHANNEL", "CH%02u %s: %s, episode %u/%u at %s of %s: %s", ch.number, ch.name, how,
       static_cast<unsigned>(episode + 1), static_cast<unsigned>(schedule.count()), at, length, path);
  return true;
}

// A remote channel: the network task tunes in (session + two streams) while the loop keeps
// running. A first tune shows static, like a set searching; a retry leaves NO SIGNAL up until
// the picture is back.
void App::playRemote(const Channel& ch, bool retry) {
  remoteRetryDue_ = false;
  if (!remote_.started()) {
    showNoSignal("NO MEMORY", false);
    return;
  }
  if (!wifi_.online()) {
    remoteFailed("OFFLINE", false);  // local channels keep working; this one waits for Wi-Fi
    return;
  }
  remote_.tune(ch.source, retry);
  remoteActive_ = true;
  if (!retry) {  // the viewer's tune: the [TUNE] clocks start here, retries included
    tuneStartMs_ = millis();
    firstVideoLogged_ = false;
    stableLogged_ = false;
  }
  playMode_ = PlayMode::Tuning;
  modeSinceMs_ = millis();
  if (!retry) {  // the zap's static and hiss go on until the picture is there
    ui_.publish(UiState(Screen::Static));
    audio_.noise(REMOTE_TUNE_TIMEOUT_MS, STATIC_NOISE_LEVEL_PCT);
  }
  PLOG("REMOTE", "CH%02u %s: tuning %s%s", ch.number, ch.name, ch.source, retry ? " (reconnect)" : "");
}

void App::updateTuning(uint32_t nowMs) {
  switch (remote_.state()) {
    case RemoteSource::State::Ready:
      audio_.stopSound();  // before the channel's own sound
      ui_.publish(UiState(Screen::Flash));
      playMode_ = PlayMode::SignalFlash;
      modeSinceMs_ = nowMs;
      return;
    case RemoteSource::State::Failed:
      remoteFailed(RemoteSource::failureText(remote_.failure()), false);
      return;
    case RemoteSource::State::Idle:
    case RemoteSource::State::Connecting:
      // Signed: the tune may have been stamped after nowMs in this loop pass.
      if (static_cast<int32_t>(nowMs - modeSinceMs_) >= static_cast<int32_t>(REMOTE_TUNE_TIMEOUT_MS)) {
        remoteFailed("NO SERVER", false);
      }
      return;
  }
}

// The live picture, once the CRT flash has shown.
void App::startLive() {
  if (!startPlayer(remote_.video(), remote_.audio(), remote_.fps())) return;
  playMode_ = PlayMode::Video;
  failedInARow_ = 0;
  remoteAttempt_ = 0;
  netBase_ = remote_.stats();
  netPlayerBase_ = player_.counters();
  netStatsSinceMs_ = millis();
  secondMarkMs_ = netStatsSinceMs_;
  secondBase_ = netPlayerBase_;
  secondUnderruns_ = netBase_.underruns;
  cleanSeconds_ = 0;
  player_.takeDriftMax(DriftReader::Net);
  PLOG("REMOTE", "playback started at %lu ms%s", static_cast<unsigned long>(remote_.positionMs()),
       remote_.audio() ? "" : " (no audio)");
  showChannelOsd();  // the tune may have outlasted the OSD of the switch
}

// NO SIGNAL on a remote channel, and another tune later: 2 s, 4 s, 8 s, then every 15 s while
// the viewer stays. It never zaps on by itself, so the channel comes back with the server.
// `lost`: a picture was playing, so the first retry comes quickly.
void App::remoteFailed(const char* reason, bool lost) {
  if (!stopProgramme()) return;
  if (lost) remoteAttempt_ = 0;
  const uint32_t delayMs = remoteRetryDelayMs(remoteAttempt_);
  if (remoteAttempt_ < UINT8_MAX) ++remoteAttempt_;
  showNoSignal(reason, false);
  remoteRetryDue_ = true;
  remoteRetryAtMs_ = millis() + delayMs;
  PLOG("REMOTE", "%s: retry in %lu s", reason, static_cast<unsigned long>(delayMs / 1000));
}

void App::retryRemote(uint32_t nowMs) {
  if (static_cast<int32_t>(nowMs - remoteRetryAtMs_) < 0) return;
  const Channel* ch = tunedChannel();
  if (ch == nullptr || ch->type != ChannelType::Remote) {
    remoteRetryDue_ = false;
    return;
  }
  PLOG("REMOTE", "reconnect #%u", remoteAttempt_);
  playRemote(*ch, true);
}

// Moves both open files to the indexed second at or before offsetMs and returns that second.
// Any doubt (no index, or the bytes there are not a JPEG / ADTS start because the episode was
// re-converted without re-indexing) rewinds both files: returns 0.
uint32_t App::seekMedia(const char* videoPath, uint32_t offsetMs) {
  fs::File idx;
  IndexHeader h;
  if (!openIndex(videoPath, idx, h)) return 0;
  const uint32_t k = entryFor(h, offsetMs);
  uint8_t raw[INDEX_ENTRY_SIZE];
  if (!idx.seek(entryOffset(k)) || idx.read(raw, sizeof(raw)) != sizeof(raw)) return 0;
  const IndexEntry e = parseIndexEntry(raw);

  uint8_t videoHead[3];
  uint8_t audioHead[3];
  const bool read = headAt(videoFile_, e.videoOffset, videoHead) &&
                    (!audioFile_ || e.audioOffset == INDEX_NO_AUDIO || headAt(audioFile_, e.audioOffset, audioHead));
  if (!read || !entryMatches(videoHead, audioFile_ ? audioHead : nullptr, e)) {
    PLOG("CHANNEL", "%s: its .idx does not match (re-run tools/make_index.py): from the start", videoPath);
    videoFile_.seek(0);
    if (audioFile_) audioFile_.seek(0);
    return 0;
  }
  return entryTimeMs(h, k);
}

// False (and ERROR) when a player task did not stop: its file must not be closed under it.
bool App::stopProgramme() {
  if (playMode_ == PlayMode::Tuning || playMode_ == PlayMode::SignalFlash) audio_.stopSound();  // the hiss
  if (playMode_ == PlayMode::Messages) stopMessages();
  if (remoteActive_) remote_.cancel();  // a read waiting for the network returns at once
  if (!player_.stop() || !videoAhead_.stop()) {
    fail("MEDIA STUCK", "PRESS RESET");
    return false;
  }
  if (remoteActive_) {
    remote_.close();  // the network task hangs up: the server ends the session
    remoteActive_ = false;
  }
  if (videoFile_) videoFile_.close();
  if (audioFile_) audioFile_.close();
  playMode_ = PlayMode::None;
  return true;
}

// Files or network streams: to MediaPlayer both are a Stream. It refuses only when a previous
// programme could not be stopped.
bool App::startPlayer(Stream* video, Stream* audio, uint8_t fps) {
  ui_.publish(UiState(Screen::Video));
  shownAtStart_ = player_.counters().shown;
  if (player_.start(video, audio, fps)) return true;
  fail("MEDIA STUCK", "PRESS RESET");
  return false;
}

// An episode's .idx, read up to the end of its header. False without one or when it cannot be
// trusted.
bool App::openIndex(const char* videoPath, fs::File& idx, IndexHeader& h) {
  char idxPath[MEDIA_PATH_MAX];
  if (!indexPathFor(videoPath, idxPath, sizeof(idxPath))) return false;
  idx = storage_.open(idxPath);
  uint8_t head[INDEX_HEADER_SIZE];
  return idx && idx.read(head, sizeof(head)) == sizeof(head) && parseIndexHeader(head, sizeof(head), h);
}

// A local episode: the player reads the video through the read-ahead, the audio directly.
bool App::startLocalPlayer() {
  if (!videoAhead_.start(videoFile_)) {
    fail("MEDIA STUCK", "PRESS RESET");
    return false;
  }
  return startPlayer(&videoAhead_, audioFile_ ? &audioFile_ : nullptr, mediaFps_);
}

bool App::openMedia(const char* videoPath) {
  if (!storage_.open(videoPath, videoFile_)) {
    PLOG("MEDIA", "%s missing", videoPath);
    return false;
  }
  char audioPath[MEDIA_PATH_MAX];
  if (audioPathFor(videoPath, audioPath, sizeof(audioPath))) {
    storage_.open(audioPath, audioFile_);  // optional: silent clips play on the wall clock
  }
  // The converter writes the frame rate into the index. Without one (or above VIDEO_FPS, more
  // than the panel can draw) the episode plays at VIDEO_FPS.
  mediaFps_ = VIDEO_FPS;
  fs::File idx;
  IndexHeader h;
  if (openIndex(videoPath, idx, h) && h.fps <= VIDEO_FPS) mediaFps_ = static_cast<uint8_t>(h.fps);
  return true;
}

// After a read timeout the card refuses every command until it is initialised again (seen
// 2026-09-30 with the Wi-Fi scanning, docs/TEST_PLAN.md T18): mount it again. It can fail again
// right after, so a few remounts in a row are allowed, then none until SD_REMOUNT_WINDOW_MS
// passes: a channel that is really empty does not remount on every try. With the player
// stopped. Programmes built while the card failed may be wrong: they are built again.
bool App::recoverSd() {
  if (millis() - sdRemountWindowMs_ >= SD_REMOUNT_WINDOW_MS) {
    sdRemountWindowMs_ = millis();
    sdRemounts_ = 0;
  }
  if (sdRemounts_ >= SD_REMOUNTS_PER_WINDOW) return false;
  ++sdRemounts_;
  if (!videoAhead_.stop()) return false;  // never close a file under its reader
  if (!builder_.waitIdle(SD_REMOUNT_BUILDER_WAIT_MS)) return false;  // nor remount under the builder
  if (videoFile_) videoFile_.close();
  if (audioFile_) audioFile_.close();
  const bool ok = storage_.remount();
  PLOG("SD", "mounted again after a failure: %s", ok ? "ok" : "NO CARD");
  for (ChannelSchedule& s : schedules_) s.reset();
  testSchedule_.reset();
  return ok;
}

void App::showNoSignal(const char* reason, bool autoSkip) {
  playMode_ = PlayMode::NoSignal;
  modeSinceMs_ = millis();
  if (autoSkip && failedInARow_ < UINT8_MAX) ++failedInARow_;
  autoSkip_ = autoSkip && !testTune_ && failedInARow_ < channels_.enabledCount();
  UiState s(Screen::NoSignal);
  s.addLine("%s", reason);
  ui_.publish(s);
  const Channel* ch = tunedChannel();
  PLOG("CHANNEL", "CH%02u NO SIGNAL: %s%s", ch ? ch->number : 0, reason,
       autoSkip_ ? " (next channel in 3 s)" : "");
}

// Steps 1-2 of a channel switch: choose the target, stop the programme. The static, the
// flash and the start happen in CHANNEL_SWITCH without blocking the loop.
void App::beginSwitch(int direction) {
  if (!stopProgramme()) return;
  if (direction > 0) {
    channels_.next();
  } else {
    channels_.prev();
  }
  switchToSelected();
}

// Web remote: straight to a channel, with the same static and flash as zapping. During the
// static it just changes the target, like the knobs do.
void App::tuneNumber(uint16_t number) {
  if (state_ == AppState::ChannelSwitch) {
    if (channels_.select(number) != nullptr) enter(AppState::ChannelSwitch);
    return;
  }
  if (state_ != AppState::Playing) return;
  bool found = false;
  for (size_t i = 0; i < channels_.count() && !found; ++i) {
    found = channels_.at(i).number == number && channels_.at(i).enabled;
  }
  if (!found) {
    PLOG("WEB", "no channel %u", number);
    return;
  }
  if (!stopProgramme()) return;
  channels_.select(number);
  switchToSelected();
}

void App::switchToSelected() {
  testTune_ = false;  // zapping always goes back to the channel list
  lastEpisode_ = -1;
  advanceEpisode_ = false;  // tuning in: wherever the programme is now
  remoteRetryDue_ = false;
  remoteAttempt_ = 0;
  enter(AppState::ChannelSwitch);
}

void App::updateSwitch(uint32_t elapsedMs) {
  const Channel* target = tunedChannel();
  if (target != nullptr && target->type == ChannelType::Remote && elapsedMs >= CHANNEL_STATIC_MS) {
    enter(AppState::Playing);  // a live channel: its static goes on while it tunes, the flash comes later
    return;
  }
  if (switchPhase_ == SwitchPhase::Static && elapsedMs >= CHANNEL_STATIC_MS) {
    switchPhase_ = SwitchPhase::Flash;
    ui_.publish(UiState(Screen::Flash));
  }
  if (elapsedMs >= CHANNEL_STATIC_MS + CHANNEL_FLASH_MS) enter(AppState::Playing);  // 5-6: start + OSD
}

void App::updatePlaying(uint32_t nowMs) {
  switch (playMode_) {
    case PlayMode::Video:
      if (player_.finished() && remoteActive_) {  // live streams do not end: the link went down
        remoteFailed("SIGNAL LOST", true);
        break;
      }
      if (player_.finished()) {  // next episode (in order when on air), or loop the file
        // Ended without a single frame on screen: empty or undecodable file. Restarting it
        // would loop forever. (Tuning in at an episode's last second is fine: frames show.)
        const bool broken = player_.counters().shown == shownAtStart_;
        // Ended before the end of the file: an SD read failed (a card timeout; the file then
        // refuses every read). Tuning in again lands back where the programme is.
        const size_t at = videoFile_ ? videoFile_.position() : 0;
        const size_t size = videoFile_ ? videoFile_.size() : 0;
        if (!stopProgramme()) break;
        if (broken) {
          showNoSignal("FILE ERROR", true);
          break;
        }
        advanceEpisode_ = at >= size;
        if (!advanceEpisode_) {
          PLOG("MEDIA", "SD read failed at %u of %u KB: tuning in again", static_cast<unsigned>(at / 1024),
               static_cast<unsigned>(size / 1024));
          recoverSd();
        }
        startProgramme();
      }
      break;
    case PlayMode::TestCard:
      if (nowMs - testCardPublishedMs_ >= TEST_CARD_REFRESH_MS) publishTestCard();
      break;
    case PlayMode::Teletext:
      updateTeletext(nowMs);
      break;
    case PlayMode::RemoteQr:
      publishRemoteQr();  // only redraws when the address changed (Wi-Fi came, went, new IP)
      break;
    case PlayMode::Messages:
      updateMessages(nowMs);
      break;
    case PlayMode::Tuning:
      updateTuning(nowMs);
      break;
    case PlayMode::SignalFlash:
      if (nowMs - modeSinceMs_ >= CHANNEL_FLASH_MS) startLive();
      break;
    case PlayMode::NoSignal:
      if (autoSkip_ && nowMs - modeSinceMs_ >= NO_SIGNAL_SKIP_MS) beginSwitch(1);
      if (remoteRetryDue_) retryRemote(nowMs);
      break;
    case PlayMode::None:
      break;
  }

  if (osdTimed_ && static_cast<int32_t>(nowMs - osdUntilMs_) >= 0) refreshOsd();
  if (osdVisible_ && !osdTimed_ && osdPinned_) {  // pinned info: keep the clock current
    char clock[6];
    clockText(clock, sizeof(clock), "%H:%M");
    if (strcmp(clock, osdClock_) != 0) showChannelOsd();
  }
}

void App::onPlayingInput(InputEvent e) {
  if (playMode_ == PlayMode::Teletext && onTeletextInput(e)) return;  // VOLUME turns pages
  switch (e) {
    case InputEvent::ChNext:
      beginSwitch(1);
      return;
    case InputEvent::ChPrev:
      beginSwitch(-1);
      return;
    case InputEvent::VolUp:
      changeVolume(VOLUME_STEP);
      break;
    case InputEvent::VolDown:
      changeVolume(-VOLUME_STEP);
      break;
    case InputEvent::Mute:
      toggleMute();
      break;
    case InputEvent::ToggleOsd:
      osdPinned_ = !osdPinned_;
      if (osdPinned_) {
        showChannelOsd();
      } else {
        refreshOsd();
      }
      break;
    case InputEvent::Menu:
      if (!stopProgramme()) return;
      settingsIndex_ = 0;
      enter(AppState::Settings);
      return;
    case InputEvent::Power:  // App::onInput: standby, before any state
    case InputEvent::None:
      break;
  }
  if (playMode_ == PlayMode::TestCard) publishTestCard();  // CH / VOL line
}

// Zapping on during the static: retarget and restart the static, nothing gets played.
void App::onSwitchInput(InputEvent e) {
  switch (e) {
    case InputEvent::ChNext:
    case InputEvent::ChPrev:
      if (e == InputEvent::ChNext) {
        channels_.next();
      } else {
        channels_.prev();
      }
      enter(AppState::ChannelSwitch);
      break;
    case InputEvent::VolUp:
      changeVolume(VOLUME_STEP);
      break;
    case InputEvent::VolDown:
      changeVolume(-VOLUME_STEP);
      break;
    case InputEvent::Mute:
      toggleMute();
      break;
    case InputEvent::ToggleOsd:
    case InputEvent::Menu:
    case InputEvent::Power:  // App::onInput: standby, before any state
    case InputEvent::None:
      break;
  }
}

void App::changeVolume(int delta) {
  const int v = settings_.volume() + delta;
  settings_.setVolume(static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v)));
  muted_ = false;
  audio_.setMuted(false);
  audio_.setVolume(settings_.volume());
  showVolumeOsd();
}

void App::toggleMute() {
  muted_ = !muted_;
  audio_.setMuted(muted_);
  showVolumeOsd();
}

// ---------------------------------------------------------------------------------------------
// OSD: channel info 1.5 s after a switch, volume 1 s, MUTE while muted, pinned info on a tap.

void App::publishOsd(const OsdState& o, uint32_t durationMs) {
  ui_.publishOsd(o);
  osdVisible_ = true;
  osdTimed_ = durationMs > 0;
  osdUntilMs_ = millis() + durationMs;
}

void App::fillOsdStatus(OsdState& o) const {
  o.showStatus = true;
  if (!clockText(o.clock, sizeof(o.clock), "%H:%M")) o.clock[0] = '\0';
  o.wifiBars = wifi_.online() ? static_cast<int8_t>(wifiBarsFor(wifi_.rssi())) : -1;
  o.batteryPct = battery_.percent();
  o.batteryLow = battery_.level() != BatteryLevel::Ok;
  o.batteryCharging = battery_.charging();
}

void App::showChannelOsd() {
  const Channel* ch = tunedChannel();
  if (ch == nullptr) return;
  OsdState o;
  o.visible = true;
  snprintf(o.title, sizeof(o.title), "CH %02u", ch->number);
  snprintf(o.subtitle, sizeof(o.subtitle), "%s", ch->name);
  fillOsdStatus(o);
  snprintf(osdClock_, sizeof(osdClock_), "%s", o.clock);
  publishOsd(o, osdPinned_ ? 0 : OSD_CHANNEL_MS);
}

void App::showVolumeOsd() {
  OsdState o;
  o.visible = true;
  if (muted_) {
    snprintf(o.title, sizeof(o.title), "MUTE");
    o.volumeBars = 0;
  } else {
    snprintf(o.title, sizeof(o.title), "VOL %u", settings_.volume());
    o.volumeBars = static_cast<int8_t>(settings_.volume() * OSD_VOLUME_SEGMENTS / 100);
  }
  publishOsd(o, OSD_VOLUME_MS);
}

// What stays once a timed OSD expires: MUTE while muted, the pinned info, or nothing. Never
// MUTE over the remote's QR code: it covers a corner and phones cannot read it.
void App::refreshOsd() {
  if (muted_ && playMode_ != PlayMode::RemoteQr) {
    OsdState o;
    o.visible = true;
    snprintf(o.title, sizeof(o.title), "MUTE");
    publishOsd(o, 0);
  } else if (osdPinned_) {
    showChannelOsd();
  } else {
    hideOsd();
  }
}

void App::hideOsd() {
  if (!osdVisible_) return;
  ui_.publishOsd(OsdState{});
  osdVisible_ = false;
  osdTimed_ = false;
}

// The web remote's QR channel. The code carries the IP, not retrotv.local: some Android
// phones do not resolve .local names.
void App::publishRemoteQr() {
  char url[sizeof(remoteQrUrl_)] = "";
  if (wifi_.online()) {
    char ip[16];
    wifi_.ipString(ip, sizeof(ip));
    snprintf(url, sizeof(url), "http://%s/", ip);
  }
  if (remoteQrShown_ && strcmp(url, remoteQrUrl_) == 0) return;
  remoteQrShown_ = true;
  snprintf(remoteQrUrl_, sizeof(remoteQrUrl_), "%s", url);
  UiState s(Screen::RemoteQr, url);
  if (url[0] != '\0') {
    s.addLine("ESCANEA PARA ABRIR EL MANDO");
    s.addLine("%.*s", static_cast<int>(strlen(url) - 1), url);  // without the trailing slash
    s.addLine("o http://retrotv.local");
  } else {
    s.addLine("EL MANDO NECESITA LA WI-FI");
    s.addLine("SIN WI-FI");
    s.addLine("Pon tu red en la SD (wifi.json)");
  }
  ui_.publish(s);
}
