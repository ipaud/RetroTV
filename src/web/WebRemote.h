#pragma once

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <atomic>

#include "web/ConfigApi.h"
#include "web/RemoteApi.h"

struct RemoteCommand {
  InputEvent key = InputEvent::None;  // a button, or
  bool tune = false;                  // a channel to tune, or
  uint16_t channel = 0;
  uint16_t pairCode = 0;              // a settings code to show on screen, or
  bool paired = false;                // the phone typed it: take the code off the screen
};

// A settings request, run by the App's loop (it owns the SD, the Wi-Fi and the settings).
enum class ConfigOp : uint8_t {
  Info, WifiList, WifiAdd, WifiRemove, WifiScanStart, WifiScanResults,
  DisplayGet, DisplaySet, ChannelsList, ChannelSet, Reboot,
};
struct ConfigRequest {
  ConfigOp op = ConfigOp::Info;
  uint32_t seq = 0;
  char ssid[33] = "";
  char password[65] = "";  // wiped as soon as it has been used
  int brightness = -1;
  int volume = -1;
  uint16_t number = 0;
  bool enabled = false;
};

// Web remote served by the TV on port 80, http://retrotv.local on the same Wi-Fi: the page,
// the state it shows and the commands it sends. HTTP runs in the server's own low-priority
// task; commands reach the App's loop through a queue, like the knobs, so nothing here touches
// the player or the screen.
//
// No PIN (the user's choice): anyone on the same Wi-Fi can zap. Commands need an X-RETROTV
// header, so another website open on the phone cannot send them (a cross-site request with a
// custom header needs a CORS preflight that this server never grants).
class WebRemote {
 public:
  WebRemote();
  // Before begin(): neither ever changes. A logo is a PNG in PSRAM, kept for good.
  void addLogo(uint16_t number, const uint8_t* png, size_t len);
  void setChannels(const ChannelManager& channels, uint64_t logoMask);
  bool begin();
  void announce(const char* ip);  // mDNS name and _http service, once the Wi-Fi is up
  bool announced() const { return announced_; }

  // Loop task.
  bool poll(RemoteCommand& command);
  void publish(const RemoteState& state);
  // The channel list changed (a channel enabled or disabled from the settings).
  void updateChannels(const ChannelManager& channels, uint64_t logoMask);
  // Different after every boot and every change of the list: in /api/state, so an open page
  // notices and fetches /api/channels again.
  uint32_t channelsVersion() const { return channelsVersion_.load(); }
  // Settings requests: take one, write the answer (JSON) into reply(), then finishConfig().
  bool pollConfig(ConfigRequest& request);
  char* reply() { return reply_; }
  size_t replyCap() const;
  void finishConfig(uint32_t seq, int status, size_t len);
  // The guide (GET /api/guide), made by App while a phone looks at it: write it into
  // guideScratch() (the buffer not being served), then publishGuide() serves it.
  char* guideScratch(size_t& cap);
  void publishGuide();
  // A phone asked for the guide in the last WEB_GUIDE_IDLE_MS.
  bool guideWanted(uint32_t nowMs) const;

 private:
  static esp_err_t onPage(httpd_req_t* req);
  static esp_err_t onState(httpd_req_t* req);
  static esp_err_t onChannels(httpd_req_t* req);
  static esp_err_t onKey(httpd_req_t* req);
  static esp_err_t onChannel(httpd_req_t* req);
  static esp_err_t onLogo(httpd_req_t* req);
  static esp_err_t onGuide(httpd_req_t* req);
  static esp_err_t queue(httpd_req_t* req, const RemoteCommand& command);
  static esp_err_t onPairStart(httpd_req_t* req);
  static esp_err_t onPair(httpd_req_t* req);
  static esp_err_t onConfig(httpd_req_t* req);  // every /api/config/... route below pairing
  esp_err_t runConfig(httpd_req_t* req, ConfigRequest& request);
  void writeChannels(const ChannelManager& channels, uint64_t logoMask, char* into);

  httpd_handle_t server_ = nullptr;
  QueueHandle_t commands_ = nullptr;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  RemoteState state_;
  // PSRAM, two buffers: an update is written into the one not being served, then swapped in.
  // ponytail: two updates within one send could overwrite a buffer mid-send; they come at the
  // pace of a finger on a phone, a send takes milliseconds.
  char* channelsBuf_[2] = {nullptr, nullptr};
  std::atomic<char*> channelsJson_{nullptr};
  std::atomic<uint32_t> channelsVersion_{0};
  char* guideBuf_[2] = {nullptr, nullptr};  // the same, for the guide (PSRAM)
  std::atomic<char*> guideJson_{nullptr};
  std::atomic<uint32_t> guideAskedMs_{0};  // millis() of the last GET /api/guide, 0 = never
  PairingGate gate_;
  QueueHandle_t configQueue_ = nullptr;
  SemaphoreHandle_t configDone_ = nullptr;
  char* reply_ = nullptr;  // PSRAM
  size_t replyLen_ = 0;
  int replyStatus_ = 0;
  std::atomic<uint32_t> replySeq_{0};
  uint32_t nextSeq_ = 0;
  struct Logo {
    uint16_t number;
    const uint8_t* png;
    size_t len;
  };
  Logo logos_[MAX_CHANNELS] = {};
  size_t logoCount_ = 0;
  bool announced_ = false;
};
