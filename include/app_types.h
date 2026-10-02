#pragma once

#include <stdint.h>

enum class AppState : uint8_t {
  Boot,
  Diagnostics,
  Home,
  Playing,
  ChannelSwitch,
  Settings,
  Error,
  Recorder,  // RETROTV Voice: GRABADORA, 3-2-1, REC, saved (AppVoice.cpp)
};

inline const char* appStateName(AppState s) {
  switch (s) {
    case AppState::Boot: return "BOOT";
    case AppState::Diagnostics: return "DIAGNOSTICS";
    case AppState::Home: return "HOME";
    case AppState::Playing: return "PLAYING";
    case AppState::ChannelSwitch: return "CHANNEL_SWITCH";
    case AppState::Settings: return "SETTINGS";
    case AppState::Error: return "ERROR";
    case AppState::Recorder: return "RECORDER";
  }
  return "?";
}

// What the user asked for, whatever the source (touch, front buttons, BOOT). The App decides
// what each one means in the current state (e.g. CH_NEXT moves the selection in settings).
enum class InputEvent : uint8_t {
  None,
  ChNext,
  ChPrev,
  VolUp,
  VolDown,
  Mute,
  ToggleOsd,
  Menu,
  Power,  // standby
};

inline const char* inputEventName(InputEvent e) {
  switch (e) {
    case InputEvent::None: return "NONE";
    case InputEvent::ChNext: return "CH_NEXT";
    case InputEvent::ChPrev: return "CH_PREV";
    case InputEvent::VolUp: return "VOL_UP";
    case InputEvent::VolDown: return "VOL_DOWN";
    case InputEvent::Mute: return "MUTE";
    case InputEvent::ToggleOsd: return "TOGGLE_OSD";
    case InputEvent::Menu: return "MENU";
    case InputEvent::Power: return "POWER";
  }
  return "?";
}

// Prefixed serial log: PLOG("BOOT", "psram %u", n) -> "[BOOT] psram 8388608".
// Tags: BOOT DISPLAY TOUCH INPUT SD AUDIO WIFI MEDIA CHANNEL.
#ifdef ARDUINO
#include <Arduino.h>
#define PLOG(tag, fmt, ...) Serial.printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#endif
