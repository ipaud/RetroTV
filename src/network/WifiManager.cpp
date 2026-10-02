#include "network/WifiManager.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>

#include "app_types.h"
#include "config.h"
#include "storage/SdLayout.h"
#include "storage/StorageManager.h"

// Development fallback, git-ignored; wifi.json's networks come first. Either one network
// (PAUTV_WIFI_SSID / PAUTV_WIFI_PASSWORD) or more with
//   #define PAUTV_WIFI_NETWORKS {"home", "password"}, {"office", "password"}
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef PAUTV_WIFI_SSID
#define PAUTV_WIFI_SSID ""
#endif
#ifndef PAUTV_WIFI_PASSWORD
#define PAUTV_WIFI_PASSWORD ""
#endif
#ifndef PAUTV_WIFI_NETWORKS
#define PAUTV_WIFI_NETWORKS
#endif

namespace {

struct SecretNetwork {
  const char* ssid;
  const char* password;
};
constexpr SecretNetwork SECRET_NETWORKS[] = {{PAUTV_WIFI_SSID, PAUTV_WIFI_PASSWORD}, PAUTV_WIFI_NETWORKS};

// The core gives an async scan max_ms_per_chan x 20 before calling it failed: 120 ms (2.4 s)
// was too short in a busy office (0 results). 300 ms: up to ~4 s, cut at 6 s.
constexpr uint32_t SCAN_MS_PER_CHANNEL = 300;
constexpr uint32_t SCAN_TIMEOUT_MS = 8000;     // then try every known network anyway
constexpr size_t SCAN_MATCHES_MAX = 24;        // one SSID can show from several access points
constexpr int SCAN_LOG_MAX = 8;                // visible networks listed when none is known

const char* failureReason(wl_status_t s) {
  switch (s) {
    case WL_NO_SSID_AVAIL: return "network not visible (5 GHz only? out of range?)";
    case WL_CONNECT_FAILED: return "connect failed (wrong password?)";
    case WL_CONNECTION_LOST: return "connection lost";
    default: return "timeout";
  }
}

}  // namespace

WifiNetworks WifiManager::loadNetworks(const StorageManager& storage) {
  WifiNetworks nets;
  if (storage.exists(sdpath::WIFI_JSON)) {
    JsonDocument doc;
    char error[48];
    uint8_t skipped = 0;
    if (!storage.loadJson(sdpath::WIFI_JSON, doc, error, sizeof(error))) {
      PLOG("WIFI", "wifi.json ignored: %s", error);
    } else {
      parseWifiJson(doc, nets, "wifi.json", skipped);
    }
    if (skipped > 0) {
      PLOG("WIFI", "wifi.json: %u network(s) ignored (ssid 1-32 chars, password empty or 8-63, no repeats, max %u)",
           skipped, static_cast<unsigned>(WIFI_MAX_NETWORKS));
    }
  }
  for (const SecretNetwork& s : SECRET_NETWORKS) nets.add(s.ssid, s.password, "secrets.h");
  return nets;
}

void WifiManager::begin(const WifiNetworks& networks) {
  // The clock can already be valid before any NTP: the RTC keeps the time across a reset. Local
  // time must not wait for Wi-Fi, or it shows UTC until the network comes up.
  setenv("TZ", PAUTV_TZ, 1);
  tzset();
  networks_ = networks;
  if (networks_.count == 0) {
    state_ = WifiState::NoConfig;
    firstAttemptDone_ = true;
    PLOG("WIFI", "no credentials (wifi.json / secrets.h): LOCAL MODE");
    return;
  }
  WiFi.persistent(false);        // credentials never copied into the Wi-Fi NVS area
  WiFi.setAutoReconnect(false);  // this state machine owns every retry
  WiFi.setHostname(PAUTV_HOSTNAME);
  WiFi.mode(WIFI_STA);
  // No modem sleep: with it the radio naps between beacons (~100 ms ping on the real router)
  // and a remote channel gets ~150 KB/s, less than an MJPEG stream needs. The TV is on mains.
  WiFi.setSleep(false);
  for (uint8_t i = 0; i < networks_.count; ++i) {
    PLOG("WIFI", "known network \"%s\" (from %s)", networks_.items[i].ssid, networks_.items[i].source);
  }
  startRound(PAUTV_WIFI_FIRST_TIMEOUT_MS);
}

// One round: every known network once, in the order a scan suggests. A single network needs
// no scan: it is tried at once, as before.
void WifiManager::startRound(uint32_t firstTimeoutMs) {
  state_ = WifiState::Connecting;
  firstTimeoutMs_ = firstTimeoutMs;
  next_ = 0;
  if (networks_.count == 1) {
    order_[0] = 0;
    tryNext(firstTimeoutMs_);
    return;
  }
  scanning_ = true;
  scanStarted_ = false;  // loop() starts it once the station is up
  scanStartMs_ = millis();
}

// Right after WiFi.mode(), before the station has started, an async scan never finished (it
// stayed stuck and even broke the next connection); 60 s later the same scan took 3.2 s.
bool WifiManager::startScanWhenReady() {
  if (!(WiFi.getStatusBits() & STA_STARTED_BIT)) return false;
  scanStarted_ = true;
  scanStartMs_ = millis();
  if (WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHANNEL) == WIFI_SCAN_FAILED) finishScan(WIFI_SCAN_FAILED);
  return true;
}

void WifiManager::finishScan(int found) {
  scanning_ = false;
  SeenNetwork seen[SCAN_MATCHES_MAX];
  size_t matches = 0;
  for (int i = 0; i < found && matches < SCAN_MATCHES_MAX; ++i) {
    const String ssid = WiFi.SSID(i);
    for (uint8_t k = 0; k < networks_.count; ++k) {
      if (ssid == networks_.items[k].ssid) {
        seen[matches++] = SeenNetwork{networks_.items[k].ssid, WiFi.RSSI(i)};
        PLOG("WIFI", "in range: \"%s\" %d dBm, channel %d", networks_.items[k].ssid, WiFi.RSSI(i),
             static_cast<int>(WiFi.channel(i)));
        break;
      }
    }
  }
  if (found < 0) {
    esp_wifi_scan_stop();  // a stuck scan left running makes the next connection fail
    PLOG("WIFI", "scan failed after %lu ms: trying every known network", static_cast<unsigned long>(millis() - scanStartMs_));
  } else {
    PLOG("WIFI", "scan: %d network(s) in %lu ms, %u known", found, static_cast<unsigned long>(millis() - scanStartMs_),
         static_cast<unsigned>(matches));
  }
  if (matches == 0) {  // what is there instead: a 2.4 GHz name to add, or none at all
    for (int i = 0; i < found && i < SCAN_LOG_MAX; ++i) {
      PLOG("WIFI", "  visible: \"%s\" %d dBm, channel %d", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
           static_cast<int>(WiFi.channel(i)));
    }
  }
  WiFi.scanDelete();
  orderWifiAttempts(networks_, seen, matches, order_);
  if (!tryNext(firstTimeoutMs_)) goOffline(millis(), "no known network");
}

bool WifiManager::tryNext(uint32_t timeoutMs) {
  if (next_ >= networks_.count) return false;
  if (next_ > 0) WiFi.disconnect();  // drop the previous attempt before the next network
  creds_ = networks_.items[order_[next_++]];
  PLOG("WIFI", "connecting to \"%s\" (from %s)", creds_.ssid, creds_.source);
  attemptStartMs_ = millis();
  attemptTimeoutMs_ = timeoutMs;
  WiFi.begin(creds_.ssid, creds_.password[0] ? creds_.password : nullptr);
  return true;
}

bool WifiManager::addNetwork(const char* ssid, const char* password) {
  if (!validWifiCredentials(ssid, password)) return false;
  bool known = false;
  for (uint8_t i = 0; i < networks_.count; ++i) {
    WifiCredentials& c = networks_.items[i];
    if (strcmp(c.ssid, ssid) == 0) {
      snprintf(c.password, sizeof(c.password), "%s", password);
      c.source = "wifi.json";
      known = true;
    }
  }
  if (!known && !networks_.add(ssid, password, "wifi.json")) return false;
  PLOG("WIFI", "network \"%s\" %s from the web remote", ssid, known ? "updated" : "added");
  if (state_ == WifiState::NoConfig) {
    begin(WifiNetworks(networks_));  // the first network ever: Wi-Fi starts now
  } else if (state_ == WifiState::Offline) {
    retryNow();
  }
  return true;
}

bool WifiManager::removeNetwork(const char* ssid) {
  for (uint8_t i = 0; i < networks_.count; ++i) {
    if (strcmp(networks_.items[i].ssid, ssid) != 0) continue;
    for (uint8_t j = i; j + 1 < networks_.count; ++j) networks_.items[j] = networks_.items[j + 1];
    networks_.items[--networks_.count] = WifiCredentials{};
    PLOG("WIFI", "network \"%s\" removed from the web remote", ssid);
    return true;  // a network in use stays connected until the link drops
  }
  return false;
}

bool WifiManager::startUserScan() {
  if (state_ != WifiState::Online && state_ != WifiState::Offline) return false;
  if (WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHANNEL) == WIFI_SCAN_FAILED) return false;
  userScan_ = true;
  return true;
}

size_t WifiManager::userScanJson(char* out, size_t cap) {
  JsonDocument doc;
  const int found = userScan_ ? WiFi.scanComplete() : WIFI_SCAN_FAILED;
  doc["done"] = found != WIFI_SCAN_RUNNING;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (int i = 0; i < found; ++i) {  // strongest per SSID; hidden networks have no name to show
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    JsonObject same;
    for (JsonObject o : list) {
      if (ssid == (o["ssid"] | "")) same = o;
    }
    if (!same.isNull() && (same["rssi"] | -200) >= WiFi.RSSI(i)) continue;
    JsonObject o = same.isNull() ? list.add<JsonObject>() : same;
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    bool known = false;
    for (uint8_t k = 0; k < networks_.count; ++k) known = known || ssid == networks_.items[k].ssid;
    o["known"] = known;
  }
  if (found >= 0) {
    WiFi.scanDelete();
    userScan_ = false;
  }
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

void WifiManager::retryNow() {
  if (state_ == WifiState::NoConfig) return;
  WiFi.disconnect();
  PLOG("WIFI", "retry requested");
  startRound(WIFI_ATTEMPT_TIMEOUT_MS);
}

void WifiManager::loop(uint32_t nowMs) {
  if (state_ == WifiState::NoConfig) return;
  const wl_status_t status = WiFi.status();

  switch (state_) {
    case WifiState::Connecting:
      if (scanning_ && !scanStarted_) {
        if (!startScanWhenReady() && nowMs - scanStartMs_ >= SCAN_TIMEOUT_MS) finishScan(WIFI_SCAN_FAILED);
      } else if (scanning_) {
        const int found = WiFi.scanComplete();
        if (found != WIFI_SCAN_RUNNING) {
          finishScan(found);
        } else if (nowMs - scanStartMs_ >= SCAN_TIMEOUT_MS) {
          finishScan(WIFI_SCAN_FAILED);
        }
      } else if (status == WL_CONNECTED) {
        onConnected();
      } else if (nowMs - attemptStartMs_ >= attemptTimeoutMs_) {
        if (networks_.count == 1) {
          goOffline(nowMs, failureReason(status));
        } else {
          PLOG("WIFI", "\"%s\": %s", creds_.ssid, failureReason(status));
          if (!tryNext(WIFI_ATTEMPT_TIMEOUT_MS)) goOffline(nowMs, "no known network answered");
        }
      }
      break;
    case WifiState::Online:
      if (status != WL_CONNECTED) {
        goOffline(nowMs, "connection lost");
      } else if (!clockLogged_ && time(nullptr) >= PAUTV_VALID_EPOCH) {
        const time_t now = time(nullptr);
        tm local;
        localtime_r(&now, &local);
        char text[24];
        strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
        PLOG("WIFI", "NTP time %s", text);
        clockLogged_ = true;
      }
      break;
    case WifiState::Offline:
      if (nowMs - offlineSinceMs_ >= WIFI_RETRY_INTERVAL_MS) startRound(WIFI_ATTEMPT_TIMEOUT_MS);
      break;
    case WifiState::NoConfig:
      break;
  }
}

void WifiManager::onConnected() {
  state_ = WifiState::Online;
  firstAttemptDone_ = true;
  char ip[16];
  ipString(ip, sizeof(ip));
  PLOG("WIFI", "ONLINE after %lu ms, ip %s rssi %d dBm", millis() - attemptStartMs_, ip, rssi());
  // A link-local IPv6 address, only so mDNS can answer IPv6 lookups of retrotv.local: with
  // none, a phone or a Mac asking for both waited 5 s for the IPv6 answer on every lookup.
  WiFi.enableIpV6();
  if (!ntpStarted_) {
    configTzTime(PAUTV_TZ, NTP_SERVER_1, NTP_SERVER_2);
    ntpStarted_ = true;
  }
}

void WifiManager::goOffline(uint32_t nowMs, const char* why) {
  state_ = WifiState::Offline;
  firstAttemptDone_ = true;
  offlineSinceMs_ = nowMs;
  WiFi.disconnect();
  PLOG("WIFI", "LOCAL MODE: %s; retry in %u s", why, WIFI_RETRY_INTERVAL_MS / 1000);
}

const char* WifiManager::stateName() const {
  switch (state_) {
    case WifiState::NoConfig: return "NO CONFIG";
    case WifiState::Connecting: return "CONNECTING";
    case WifiState::Online: return "OK";
    case WifiState::Offline: return "OFFLINE";
  }
  return "?";
}

void WifiManager::ipString(char* out, size_t len) const {
  const IPAddress ip = WiFi.localIP();  // formatted by hand: no String on the heap each second
  snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

int WifiManager::rssi() const { return WiFi.RSSI(); }
