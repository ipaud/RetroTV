#pragma once

#include "app_types.h"
#include "audio/AudioManager.h"
#include "channels/ChannelManager.h"
#include "channels/ChannelSchedule.h"
#include "channels/ScheduleBuilder.h"
#include "diagnostics/Diagnostics.h"
#include "display/DisplayManager.h"
#include "input/Buttons.h"
#include "input/TouchManager.h"
#include "media/EpisodeIndex.h"
#include "media/MediaPlayer.h"
#include "media/SdPrefetch.h"
#include "network/RemoteSource.h"
#include "network/WifiManager.h"
#include "power/Battery.h"
#include "settings/SettingsStore.h"
#include "storage/SdLayout.h"
#include "storage/StorageManager.h"
#include "teletext/Teletext.h"
#include "ui/UIManager.h"
#include "voice/AudioCapture.h"
#include "voice/MessagePlayer.h"
#include "web/WebRemote.h"

// Top-level state machine on the Arduino loopTask. It never draws: it publishes UiState and
// OsdState, and the display task renders them. Split by concern:
//   App.cpp          boot, states, input dispatch, errors, serial debug commands
//   AppPlayback.cpp  channels, programmes, channel switching, volume, OSD
//   AppScreens.cpp   diagnostics, home card, test card, settings menu
//   AppTeletext.cpp  teletext channel: index, what is on now, a guide page per channel
//   AppVoice.cpp     RETROTV Voice (PAUTV_MIC_ENABLED builds): microphone screen, MIC TEST
class App {
 public:
  void begin();
  void loop();

 private:
  // Tuning: a live channel's static and hiss while it connects; SignalFlash: the CRT flash once
  // its picture is there, just before it shows.
  enum class PlayMode : uint8_t { None, Video, Tuning, SignalFlash, TestCard, Teletext, RemoteQr, NoSignal, Messages };
  enum class SwitchPhase : uint8_t { Static, Flash };
  enum class SettingsItem : uint8_t {
    Wifi,
    Brightness,
    Volume,
#if PAUTV_MIC_ENABLED
    Voice,  // AJUSTES > VOZ
#endif
    Diagnostics,
    Restart,
    Count
  };
  enum class VoiceItem : uint8_t { Mic, Claps, Sensitivity, PowerMode, ListenLed, Record, Count };

  // App.cpp
  void enter(AppState next);
  void fail(const char* headline, const char* detail);
  void showStartupError();
  void pollInput(uint32_t nowMs);
  void onInput(InputEvent e);
  bool bootDiagnosticsDone(uint32_t elapsedMs) const;
  // AppDebug.cpp (PAUTV_DEBUG_STATS only)
  void pollSerialCommands(uint32_t nowMs);
  void runSerialCommand(char command, const char* argument, uint32_t nowMs);
  void tuneTestPath(const char* path);
  void startSoak(uint32_t zapIntervalMs, uint16_t zapLimit, uint32_t nowMs);
  void updateSoak(uint32_t nowMs);
  void logSoakSnapshot(const char* label, uint32_t nowMs);
  void logMemory(const char* label) const;
  void logRemoteStats(uint32_t nowMs);
  void trackRemotePlayback(uint32_t nowMs);
  void logWifiWarmUp(uint32_t nowMs);
  void runBench(const char* argument);
  void runDiskStress(const char* argument);
  static bool clockText(char* out, size_t len, const char* format);

  // AppPlayback.cpp
  void loadChannels();
  void startProgramme();
  bool stopProgramme();
  bool playLocal(const Channel& ch, const char*& reason);
  bool playOnAir(const Channel& ch, const ChannelSchedule& schedule, const char*& reason);
  void playRemote(const Channel& ch, bool retry);
  void updateTuning(uint32_t nowMs);
  void remoteFailed(const char* reason, bool fromStart);
  void retryRemote(uint32_t nowMs);
  bool openIndex(const char* videoPath, fs::File& idx, IndexHeader& h);
  uint32_t seekMedia(const char* videoPath, uint32_t offsetMs);
  bool openMedia(const char* videoPath);
  bool startLocalPlayer();
  bool recoverSd();
  void enterStandby(bool voiceAllowed = true);  // a flat battery passes false: always deep sleep
  void powerDown();                             // the CRT goes off; screen, Wi-Fi, speaker, LED off
  [[noreturn]] void deepSleep();                // AHORRO MAXIMO: only a key wakes the TV
  void startLive();
  bool startIntro();
  void finishIntro();
  void blinkLed();
  bool startPlayer(Stream* video, Stream* audio, uint8_t fps);
  const Channel* tunedChannel() const;
  void showNoSignal(const char* reason, bool autoSkip);
  void beginSwitch(int direction);
  void tuneNumber(uint16_t number);
  void switchToSelected();
  void pollWebRemote();
  uint64_t loadWebLogos();
  // Web remote settings (AppConfig.cpp).
  void showPairCode(uint16_t code);
  void pollConfigRequests(uint32_t nowMs);
  size_t configInfoJson(char* out, size_t cap);
  size_t configChannelsJson(char* out, size_t cap);
  int saveWifi(const ConfigRequest& r, char* error, size_t errorCap);
  int saveChannelEnabled(uint16_t number, bool enabled, char* error, size_t errorCap);
  void publishWebState();
  void pollBattery(uint32_t nowMs);
  void showBatteryWarning();
  void updateSwitch(uint32_t elapsedMs);
  void updatePlaying(uint32_t nowMs);
  static bool onAirNow(uint64_t& nowMs);
  void onPlayingInput(InputEvent e);
  void onSwitchInput(InputEvent e);
  void changeVolume(int delta);
  void toggleMute();
  void showChannelOsd();
  void showVolumeOsd();
  void refreshOsd();
  void hideOsd();
  void publishOsd(const OsdState& o, uint32_t durationMs);
  void fillOsdStatus(OsdState& o) const;

  // AppScreens.cpp
  void runDiagnostics();
  void publishDiagnostics();
  void publishHome();
  void publishTestCard();
  void publishRemoteQr();
  void publishSettings();
  void onSettingsInput(InputEvent e);
  void onDiagnosticsInput(InputEvent e);

  // AppVoice.cpp: empty unless PAUTV_MIC_ENABLED.
  void startVoice();
  void updateVoice(uint32_t nowMs);
  bool onMicScreenInput(InputEvent e);  // DIAGNOSTICO: CH- opens MICROFONO, MENU leaves it
  void publishMic();
  void startMicTest(uint32_t nowMs);
  void toggleMicCapture();  // serial `V`: pause / resume the capture (A/B of its cost)
  void pollClaps();         // a finished clap sequence -> the same InputEvents as the keys
  void applyVoiceSettings();
  void publishVoiceSettings();
  void onVoiceSettingsInput(InputEvent e);
  bool voiceStandbyChosen() const;
  // Recorder (PAUTV_RECORDER_ENABLED): GRABADORA, 3-2-1, REC, MENSAJE GUARDADO.
  void startRecorder();
  void updateRecorder(uint32_t nowMs);
  void onRecorderInput(InputEvent e);
  void publishRecorder(uint32_t nowMs);
  bool saveMessage(size_t samples, int& id, const char*& error);
  void saveTestMessage();  // serial `Y`: a synthetic tone through the same save path (no microphone)
  void deleteMessages();   // serial `E`: every msg_NNNN.wav goes (nothing else in the folder)
  // MENSAJES channel (internal source "messages").
  void startMessages();
  void updateMessages(uint32_t nowMs);
  void stopMessages();
  void publishMessages(uint32_t nowMs);
  [[noreturn]] void voiceStandby();  // STANDBY VOZ: listening; two claps (or a key) restart the TV

  // AppTeletext.cpp
  void startTeletext();
  void updateTeletext(uint32_t nowMs);
  bool onTeletextInput(InputEvent e);
  void teletextGoTo(uint16_t page);
  void turnPage(int step, bool byHand);
  bool buildNextSchedule();
  void pollWebGuide(uint32_t nowMs);
  void publishWebGuide();
  bool pollRemoteGuide(uint32_t nowMs);
  size_t liveAirings(const Channel& c, const GuideAiring** out, size_t max, uint64_t& nowMs) const;
  size_t guideChannels(uint8_t* slots, uint16_t* numbers) const;
  void publishTeletext(bool turned);
  void composeIndexPage(tt::Page& page, const uint16_t* numbers, size_t count, bool ntp) const;
  void composeNowPage(tt::Page& page, const tt::View& v, const uint8_t* slots, size_t count,
                      uint64_t nowMs) const;
  void composeNowLine(tt::Row& row, uint8_t slot, uint64_t nowMs) const;
  void composeChannelPage(tt::Page& page, uint8_t slot, uint64_t nowMs, bool ntp) const;

  DisplayManager display_;
  UIManager ui_;
  Buttons buttons_;
  TouchManager touch_;
  StorageManager storage_;
  SettingsStore settings_;
  WifiManager wifi_;
  AudioManager audio_;
  MediaPlayer player_;
  RemoteSource remote_;
  WebRemote web_;
  uint64_t logoMask_ = 0;     // channels with a logo for the web remote (bit = channel index)
  uint32_t rebootAtMs_ = 0;   // a restart asked from the web remote, once its answer is out
  ChannelManager channels_;
  SdFile videoFile_;
  SdPrefetch videoAhead_;  // the player reads videoFile_ through it
  ScheduleBuilder builder_;  // the guides' programmes, off the loop
  SdFile audioFile_;
  uint8_t mediaFps_ = VIDEO_FPS;  // of the open episode, from its .idx

  AppState state_ = AppState::Boot;
  uint32_t stateSinceMs_ = 0;
  bool lcdOk_ = false;
  InputEvent lastInput_ = InputEvent::None;

  // Diagnostics
  DiagReport lastReport_;
  WifiState shownWifiState_ = WifiState::NoConfig;
  bool diagFromSettings_ = false;

  // A startup problem (no SD, unusable channels.json) is shown before HOME; the TV then goes
  // on with the fallback list (test card). A fatal error stays on screen.
  char startupError_[24] = "";
  char startupDetail_[UI_LINE_LEN] = "";
  bool errorRecoverable_ = false;

  // Playback
  PlayMode playMode_ = PlayMode::None;
  SwitchPhase switchPhase_ = SwitchPhase::Static;
  uint32_t modeSinceMs_ = 0;
  bool autoSkip_ = false;      // NO SIGNAL from a broken channel: zap on after a while
  uint8_t failedInARow_ = 0;   // stops auto-skipping when every channel is broken
  int32_t lastEpisode_ = -1;   // folder channels never repeat the episode that just ended
  uint32_t sdRemountWindowMs_ = 0;  // recoverSd: remounts in the current window
  uint8_t sdRemounts_ = 0;
  uint32_t ledBlinkUntilMs_ = 0;  // front LED off until then (0 = on)
  bool introPlaying_ = false;       // the power-on video, during BOOT
  BatteryMonitor battery_;
  uint32_t batteryReadMs_ = 0;
  bool batteryRead_ = false;
  bool batteryWarning_ = false;   // to show once a programme is on
  bool chargeNotice_ = false;     // "CARGANDO", the same way
  GuideRow* guideRows_ = nullptr; // PSRAM, MAX_CHANNELS: the web remote's guide is made here
  uint32_t webGuideMs_ = 0;       // when it was last made (0 = never)
  uint32_t webGuideStepMs_ = 0;
  uint32_t batteryEmptyAtMs_ = 0;  // flat cell noticed: standby BATTERY_EMPTY_NOTICE_MS later (0 = no)
  ChannelSchedule schedules_[MAX_CHANNELS];  // "on air" programme per channel, built on first tune
  size_t currentEpisode_ = 0;
  bool advanceEpisode_ = false;  // the episode ended: play the next one from its start
  uint32_t testCardPublishedMs_ = 0;
  char remoteQrUrl_[32] = "";  // what the QR channel shows; republished when the address changes
  bool remoteQrShown_ = false;
  uint32_t shownAtStart_ = 0;  // frames on screen when the programme started

  // Remote channels (RETROTV Server)
  bool remoteActive_ = false;     // remote_ holds the programme's session
  bool remoteRetryDue_ = false;   // NO SIGNAL on a remote channel: tune again at remoteRetryAtMs_
  uint32_t remoteRetryAtMs_ = 0;
  uint8_t remoteAttempt_ = 0;     // failures in a row, for the retry delay
  RemoteSource::Stats netBase_;   // [NET] stats line
  PlaybackCounters netPlayerBase_;
  uint32_t netStatsSinceMs_ = 0;
  // Tune metrics, PAUTV_DEBUG_STATS ([TUNE] / [NET] lines): from the viewer's tune, retries included.
  uint32_t tuneStartMs_ = 0;
  bool firstVideoLogged_ = false;
  bool stableLogged_ = false;
  uint8_t cleanSeconds_ = 0;
  uint32_t secondMarkMs_ = 0;
  PlaybackCounters secondBase_;
  uint32_t secondUnderruns_ = 0;
  uint32_t wifiOnlineMs_ = 0;  // [WIFI] second by second after joining
  uint32_t wifiLoggedS_ = 0;

  // Volume and OSD
  bool muted_ = false;
  bool osdPinned_ = false;     // a tap keeps the channel info on screen
  bool osdVisible_ = false;
  bool osdTimed_ = false;
  uint32_t osdUntilMs_ = 0;
  char osdClock_[6] = "";

  // Teletext
  size_t teletextView_ = 0;       // position in the page order (tt::viewAt)
  bool teletextByHand_ = false;   // the page was chosen: it stays up longer
  uint32_t teletextTurnedMs_ = 0;
  uint32_t teletextPublishedMs_ = 0;
  RemoteGuide* remoteGuide_ = nullptr;  // PSRAM: the server's now and next, for live channels
  char guideServer_[CHANNEL_SOURCE_LEN] = "";  // the channel URL it was asked through
  uint32_t guideAtMs_ = 0;              // millis() when it arrived
  uint32_t guideAskedMs_ = 0;
  bool guideAsked_ = false;
  uint16_t pairCode_ = 0;        // the web remote's code, on the page too: no OSD over teletext
  uint32_t pairCodeUntilMs_ = 0;

  // Settings menu
  uint8_t settingsIndex_ = 0;
  uint32_t settingsPublishedMs_ = 0;

  // RETROTV Voice
#if PAUTV_MIC_ENABLED
  AudioCapture mic_;
#endif
  bool micScreen_ = false;        // DIAGNOSTICO is showing MICROFONO
  bool voiceMenu_ = false;        // AJUSTES is showing VOZ
  uint8_t voiceIndex_ = 0;
  uint32_t micPublishedMs_ = 0;
  uint32_t micTestUntilMs_ = 0;   // serial `v`: levels to the log until then (0 = off)
  uint32_t micTestLogMs_ = 0;
  uint32_t dullSeen_ = 0;  // dull transients already logged
  char lastClap_[UI_LINE_LEN] = "";  // the MICROFONO screen shows the last sequence heard
#if PAUTV_RECORDER_ENABLED
  RecorderFlow recFlow_;
#endif
  RecPhase recShownPhase_ = RecPhase::Idle;
  MessagePlayer messages_;
  size_t messageCount_ = 0;
  int* messageIds_ = nullptr;  // PSRAM, MessagePlayer::MAX_MESSAGES: msg_NNNN numbers in play order
  uint32_t messagesPublishedMs_ = 0;
  int16_t* recBuf_ = nullptr;  // PSRAM: WAV header + REC_MAX_MS at 16 kHz, kept once allocated
  uint32_t recPublishedMs_ = 0;
  int recId_ = 0;
  const char* recError_ = "";

  // Debug: test tune (serial 'T <path>') and soak test (serial 'S', 'z')
  bool testTune_ = false;
  bool testFromStart_ = false;  // serial `T !path`: the test tune starts at 0:00, not on air
  Channel testChannel_{};
  ChannelSchedule testSchedule_;
  char serialLine_[MEDIA_PATH_MAX] = "";
  size_t serialLen_ = 0;
  bool soakActive_ = false;
  uint32_t soakZapIntervalMs_ = 0;  // 0 = never zap (long-run snapshots only)
  uint16_t soakZapsLeft_ = 0;       // 0 = unlimited
  uint32_t soakZaps_ = 0;
  uint32_t soakStartMs_ = 0;
  uint32_t soakLastZapMs_ = 0;
  uint32_t soakLastSnapMs_ = 0;
  PlaybackCounters soakBase_;   // totals since the soak started
  PlaybackCounters soakPrev_;   // previous snapshot, for heartbeat deltas
  uint32_t soakDisplayBase_ = 0;
  uint32_t soakStartHeap_ = 0;
  uint32_t soakStartPsram_ = 0;
};
