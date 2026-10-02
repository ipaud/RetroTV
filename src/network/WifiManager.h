#pragma once

#include <stddef.h>
#include <stdint.h>

#include "network/WifiNetworks.h"

class StorageManager;

enum class WifiState : uint8_t {
  NoConfig,    // no credentials anywhere: LOCAL MODE for good
  Connecting,
  Online,
  Offline,     // LOCAL MODE, background retry every WIFI_RETRY_INTERVAL_MS
};

// Station mode, never blocking: begin() starts an attempt and loop() drives it. The TV plays
// from the SD whatever happens here. With several known networks, each round starts with an
// asynchronous scan and tries the ones in range, strongest first, then the rest (hidden SSIDs).
class WifiManager {
 public:
  // /retrotv/config/wifi.json first, then include/secrets.h (development). Never logs a password.
  static WifiNetworks loadNetworks(const StorageManager& storage);

  void begin(const WifiNetworks& networks);
  void loop(uint32_t nowMs);
  void retryNow();

  WifiState state() const { return state_; }
  bool online() const { return state_ == WifiState::Online; }
  // True once the first attempt succeeded, timed out, or there was nothing to try.
  bool firstAttemptDone() const { return firstAttemptDone_; }
  const char* stateName() const;

  // Only meaningful while online.
  void ipString(char* out, size_t len) const;
  int rssi() const;

  // Web remote settings (loop task). A network added here (from wifi.json) replaces one with
  // the same SSID and is tried from the next round; offline, a round starts at once.
  const WifiNetworks& networks() const { return networks_; }
  const char* connectedSsid() const { return state_ == WifiState::Online ? creds_.ssid : nullptr; }
  bool addNetwork(const char* ssid, const char* password);
  bool removeNetwork(const char* ssid);
  // A scan for the phone: networks in range, strongest first. Not while a round is connecting.
  bool startUserScan();
  // JSON {"done": bool, "networks": [{"ssid", "rssi", "secure", "known"}]}; 0 when it does not fit.
  size_t userScanJson(char* out, size_t cap);

 private:
  void startRound(uint32_t firstTimeoutMs);
  bool startScanWhenReady();
  void finishScan(int found);
  bool tryNext(uint32_t timeoutMs);
  void onConnected();
  void goOffline(uint32_t nowMs, const char* why);

  WifiNetworks networks_;
  uint8_t order_[WIFI_MAX_NETWORKS] = {};
  uint8_t next_ = 0;  // into order_: the next network to try this round
  bool scanning_ = false;     // a round waiting for or running its scan
  bool userScan_ = false;     // a scan the phone asked for
  bool scanStarted_ = false;
  uint32_t scanStartMs_ = 0;
  uint32_t firstTimeoutMs_ = 0;
  WifiCredentials creds_;  // the network being tried or joined
  WifiState state_ = WifiState::NoConfig;
  bool firstAttemptDone_ = false;
  bool ntpStarted_ = false;
  bool clockLogged_ = false;
  uint32_t attemptStartMs_ = 0;
  uint32_t attemptTimeoutMs_ = 0;
  uint32_t offlineSinceMs_ = 0;
};
