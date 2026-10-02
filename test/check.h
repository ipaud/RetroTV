#pragma once

// Minimal assertion helpers shared by the host test files.

#include <cstdio>

inline int g_failures = 0;
inline int g_checks = 0;

#define CHECK(cond)                                                \
  do {                                                             \
    ++g_checks;                                                    \
    if (!(cond)) {                                                 \
      ++g_failures;                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                              \
  } while (0)

void runChannelTests();
void runOverlayTests();
void runOnAirTests();
void runTeletextTests();
void runRemoteTests();
void runWifiTests();
void runWebTests();
void runConfigTests();
void runBatteryTests();
void runVoiceTests();
