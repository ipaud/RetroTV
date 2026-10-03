// Web remote settings, run in the loop task: the HTTP server only queues them (src/web).
// Wi-Fi networks and channels are saved to the SD (wifi.json, channels.json) and applied at once;
// brightness and volume go to NVS like the knobs'.

#include <WiFi.h>
#include <esp_heap_caps.h>

#include "app/App.h"
#include "config.h"
#include "storage/SdLayout.h"
#include "web/ConfigApi.h"

namespace {

constexpr size_t FILE_CAP = 12288;          // channels.json: 48 channels, one line each
constexpr uint32_t REBOOT_DELAY_MS = 1000;  // time for the answer to reach the phone

// Editing a file must not eat internal RAM while a channel plays: JSON documents live in PSRAM.
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t size) override { return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM); }
};
SpiRamAllocator spiRam;

struct Reply {
  char* out;
  size_t cap;
  int status = 200;
  size_t len = 0;

  void json(size_t written) { len = written; }
  void ok() { len = static_cast<size_t>(snprintf(out, cap, "{\"ok\":true}")); }
  void error(int code, const char* message) {
    status = code;
    len = static_cast<size_t>(snprintf(out, cap, "{\"error\":\"%s\"}", message));
  }
};

}  // namespace

void App::showPairCode(uint16_t code) {
  OsdState o;
  o.visible = true;
  snprintf(o.title, sizeof(o.title), "CODIGO %04u", static_cast<unsigned>(code % 10000u));  // 1000-9999
  snprintf(o.subtitle, sizeof(o.subtitle), "AJUSTES DEL MANDO");
  publishOsd(o, PAIR_CODE_MS);
  pairCode_ = code;
  pairCodeUntilMs_ = millis() + PAIR_CODE_MS;
  if (playMode_ == PlayMode::Teletext) publishTeletext(false);
}

void App::pollConfigRequests(uint32_t nowMs) {
  if (rebootAtMs_ != 0 && static_cast<int32_t>(nowMs - rebootAtMs_) >= 0) {
    PLOG("WEB", "restart requested from the web remote");
    ESP.restart();
  }
  ConfigRequest r;
  if (!web_.pollConfig(r)) return;
  Reply reply{web_.reply(), web_.replyCap()};
  switch (r.op) {
    case ConfigOp::Info:
      reply.json(configInfoJson(reply.out, reply.cap));
      break;
    case ConfigOp::WifiList:
      reply.json(writeWifiListJson(wifi_.networks(), wifi_.connectedSsid(), reply.out, reply.cap));
      break;
    case ConfigOp::WifiAdd:
    case ConfigOp::WifiRemove: {
      char error[64] = "";
      const int status = saveWifi(r, error, sizeof(error));
      status == 200 ? reply.ok() : reply.error(status, error);
      break;
    }
    case ConfigOp::WifiScanStart:
      wifi_.startUserScan() ? reply.ok() : reply.error(409, "the Wi-Fi is connecting, try again in a few seconds");
      break;
    case ConfigOp::WifiScanResults:
      reply.json(wifi_.userScanJson(reply.out, reply.cap));
      break;
    case ConfigOp::DisplaySet:
      if (r.brightness >= 0) {
        settings_.setBrightness(static_cast<uint8_t>(r.brightness));
        display_.setBrightness(settings_.brightness());
      }
      if (r.volume >= 0) {
        settings_.setVolume(static_cast<uint8_t>(r.volume));
        audio_.setVolume(settings_.volume());
      }
      [[fallthrough]];
    case ConfigOp::DisplayGet:
      reply.json(static_cast<size_t>(snprintf(reply.out, reply.cap, "{\"brightness\":%u,\"volume\":%u,\"muted\":%s}",
                                              settings_.brightness(), settings_.volume(), muted_ ? "true" : "false")));
      break;
    case ConfigOp::ChannelsList:
      reply.json(configChannelsJson(reply.out, reply.cap));
      break;
    case ConfigOp::ChannelSet: {
      char error[64] = "";
      const int status = saveChannelEnabled(r.number, r.enabled, error, sizeof(error));
      status == 200 ? reply.ok() : reply.error(status, error);
      break;
    }
    case ConfigOp::VoiceSet:
      applyVoiceChange(r.voice);
      [[fallthrough]];
    case ConfigOp::VoiceGet:
      reply.json(voiceConfigJson(reply.out, reply.cap));
      break;
    case ConfigOp::Reboot:
      settings_.flush();
      rebootAtMs_ = nowMs + REBOOT_DELAY_MS;
      reply.status = 202;
      reply.ok();
      break;
  }
  memset(r.password, 0, sizeof(r.password));
  if (reply.len == 0 && reply.status == 200) reply.error(500, "answer too long");
  web_.finishConfig(r.seq, reply.status, reply.len);
}

size_t App::configInfoJson(char* out, size_t cap) {
  JsonDocument doc(&spiRam);
  char ip[16] = "";
  if (wifi_.online()) wifi_.ipString(ip, sizeof(ip));
  doc["version"] = PAUTV_VERSION;
  doc["wifi"] = wifi_.stateName();
  doc["ssid"] = wifi_.connectedSsid();
  doc["ip"] = ip;
  if (wifi_.online()) doc["rssi"] = wifi_.rssi();
  doc["remote"] = "http://retrotv.local";
  doc["uptime_s"] = millis() / 1000;
  doc["sd"] = storage_.mounted();
  if (storage_.mounted()) {
    doc["sd_total_mb"] = static_cast<uint32_t>(storage_.totalBytes() >> 20);
    doc["sd_used_mb"] = static_cast<uint32_t>(storage_.usedBytes() >> 20);
  }
  doc["heap_kb"] = ESP.getFreeHeap() / 1024;
  doc["heap_min_kb"] = ESP.getMinFreeHeap() / 1024;
  doc["psram_kb"] = ESP.getFreePsram() / 1024;
  doc["channels"] = channels_.count();
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

size_t App::configChannelsJson(char* out, size_t cap) {
  JsonDocument doc(&spiRam);
  JsonArray list = doc["channels"].to<JsonArray>();
  const Channel* current = channels_.current();
  for (size_t i = 0; i < channels_.count(); ++i) {
    const Channel& c = channels_.at(i);
    JsonObject o = list.add<JsonObject>();
    o["n"] = c.number;
    o["name"] = c.name;
    o["type"] = channelTypeName(c.type);
    o["enabled"] = c.enabled;
    if (current == &c) o["current"] = true;
  }
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

// wifi.json on the SD, then the network list the Wi-Fi uses. A network compiled in from
// secrets.h cannot be removed from here.
int App::saveWifi(const ConfigRequest& r, char* error, size_t errorCap) {
  if (!storage_.mounted()) {
    snprintf(error, errorCap, "no SD card");
    return 503;
  }
  const bool add = r.op == ConfigOp::WifiAdd;
  if (!add) {
    const WifiNetworks& nets = wifi_.networks();
    for (uint8_t i = 0; i < nets.count; ++i) {
      if (strcmp(nets.items[i].ssid, r.ssid) == 0 && strcmp(nets.items[i].source, "wifi.json") != 0) {
        snprintf(error, errorCap, "compiled into the firmware (secrets.h)");
        return 409;
      }
    }
  }
  JsonDocument doc(&spiRam);
  char loadError[48];
  if (storage_.exists(sdpath::WIFI_JSON) && !storage_.loadJson(sdpath::WIFI_JSON, doc, loadError, sizeof(loadError))) {
    snprintf(error, errorCap, "wifi.json unreadable: %s", loadError);
    return 500;
  }
  if (add ? !wifiJsonAdd(doc, r.ssid, r.password, error, errorCap) : !wifiJsonRemove(doc, r.ssid)) {
    if (!add) snprintf(error, errorCap, "no such network");
    return add ? 400 : 404;
  }
  char* file = static_cast<char*>(heap_caps_malloc(FILE_CAP, MALLOC_CAP_SPIRAM));
  const size_t len = file != nullptr ? writeWifiJson(doc, file, FILE_CAP) : 0;
  const bool saved = len > 0 && storage_.writeFileAtomic(sdpath::WIFI_JSON, file, len);
  if (file != nullptr) memset(file, 0, FILE_CAP);  // it held passwords
  heap_caps_free(file);
  if (!saved) {
    snprintf(error, errorCap, "cannot write wifi.json");
    return 500;
  }
  if (add) {
    wifi_.addNetwork(r.ssid, r.password);
  } else {
    wifi_.removeNetwork(r.ssid);
  }
  return 200;
}

// channels.json on the SD (every other field untouched), then the live list and the remote's.
// Voice builds put the MENSAJES channel in channels.json once (after the highest number), so it
// shows in the web remote and the zapping without editing the card by hand. Removed by the user, it
// stays removed: an NVS flag remembers it was added.
void App::addMessagesChannel(JsonDocument& doc) {
#if PAUTV_RECORDER_ENABLED
  if (settings_.messagesChannelAdded()) return;
  uint16_t number = 0;
  if (!channelsJsonAddMessages(doc, number)) {  // already there (or no list): nothing to add, ever
    settings_.setMessagesChannelAdded();
    return;
  }
  char* file = static_cast<char*>(heap_caps_malloc(FILE_CAP, MALLOC_CAP_SPIRAM));
  const size_t len = file != nullptr ? writeChannelsFile(doc, file, FILE_CAP) : 0;
  const bool saved = len > 0 && storage_.writeFileAtomic(sdpath::CHANNELS_JSON, file, len);
  heap_caps_free(file);
  if (!saved) {  // shown this session anyway; tried again at the next boot
    PLOG("CHANNEL", "MENSAJES added as %u but channels.json could not be written", static_cast<unsigned>(number));
    return;
  }
  settings_.setMessagesChannelAdded();
  PLOG("CHANNEL", "MENSAJES added to channels.json as channel %u", static_cast<unsigned>(number));
#else
  (void)doc;
#endif
}

int App::saveChannelEnabled(uint16_t number, bool enabled, char* error, size_t errorCap) {
  if (!storage_.mounted()) {
    snprintf(error, errorCap, "no SD card");
    return 503;
  }
  JsonDocument doc(&spiRam);
  char loadError[48];
  if (!storage_.loadJson(sdpath::CHANNELS_JSON, doc, loadError, sizeof(loadError))) {
    snprintf(error, errorCap, "channels.json unreadable: %s", loadError);
    return 500;
  }
  if (!channelsJsonSetEnabled(doc, number, enabled)) {
    snprintf(error, errorCap, "no channel %u in channels.json", static_cast<unsigned>(number));
    return 404;
  }
  char* file = static_cast<char*>(heap_caps_malloc(FILE_CAP, MALLOC_CAP_SPIRAM));
  const size_t len = file != nullptr ? writeChannelsFile(doc, file, FILE_CAP) : 0;
  const bool saved = len > 0 && storage_.writeFileAtomic(sdpath::CHANNELS_JSON, file, len);
  heap_caps_free(file);
  if (!saved) {
    snprintf(error, errorCap, "cannot write channels.json");
    return 500;
  }
  channels_.setEnabled(number, enabled);
  web_.updateChannels(channels_, logoMask_);
  PLOG("WEB", "channel %u %s from the web remote", static_cast<unsigned>(number), enabled ? "enabled" : "disabled");
  return 200;
}
