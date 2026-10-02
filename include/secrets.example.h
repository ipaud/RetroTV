#pragma once

// Development-only Wi-Fi credentials. Copy to include/secrets.h (git-ignored) and edit.
// The networks in /retrotv/config/wifi.json on the SD card are tried before these.
#define PAUTV_WIFI_SSID "your-ssid"
#define PAUTV_WIFI_PASSWORD "your-password"
// More networks (optional): the TV joins whichever is in range, strongest first.
// #define PAUTV_WIFI_NETWORKS {"office-ssid", "office-password"}, {"another-ssid", "another-password"}
