#pragma once

// The web remote's API, pure C++ (tested on the host): what a request may ask for and the JSON
// the page reads. The HTTP side is src/web/WebRemote.

#include <stddef.h>
#include <stdint.h>

#include "app_types.h"
#include "channels/ChannelManager.h"
#include "power/Battery.h"

// POST /api/key?k=<name>: next, prev, volup, voldown, mute, info. The settings menu is not
// offered: it is driven by the knobs.
bool parseRemoteKey(const char* name, InputEvent& out);

// POST /api/channel?n=<number>: digits only, 1-999.
bool parseChannelNumber(const char* text, uint16_t& out);

struct RemoteState {
  bool tuned = false;  // a channel is on (channel 0 exists)
  uint16_t channel = 0;
  char name[CHANNEL_NAME_LEN] = "";
  uint8_t volume = 0;
  bool muted = false;
  const char* screen = "starting";  // starting, playing, switching, menu, error
  uint8_t battery = BATTERY_UNKNOWN;  // percent
  uint32_t batteryMv = 0;  // filtered LiPo voltage, 0 = no reading yet
  bool batteryLow = false;
  bool charging = false;  // BatteryMonitor's guess: the board cannot see the cable
  uint32_t channelsVersion = 0;  // changes with the channel list: the phone fetches it again
};

// Both return the length written, or 0 when it does not fit (never a cut-off JSON).
size_t writeStateJson(const RemoteState& state, char* out, size_t cap);
// {"channels": [{"n", "name", "type", "logo"}]}: enabled channels, in zapping order. Bit i of
// logoMask says that channel i (ChannelManager index) has a logo at /api/logo?n=<number>.
size_t writeChannelsJson(const ChannelManager& channels, uint64_t logoMask, char* out, size_t cap);

// GET /api/guide: per channel, the programme on now and what follows, or a note instead
// ("EN DIRECTO", "BUSCANDO..."). Times in epoch seconds of the TV's clock: the page places them
// against the TV's "now", so the guide reads right even before NTP.
constexpr size_t GUIDE_ITEMS = 4;  // on now + the next three
constexpr size_t WEB_GUIDE_TITLE_LEN = 48;

struct GuideItem {
  uint32_t startS = 0;
  uint32_t endS = 0;
  char title[WEB_GUIDE_TITLE_LEN] = "";
};

struct GuideRow {
  uint16_t number = 0;
  char name[CHANNEL_NAME_LEN] = "";
  const char* type = "local";  // local, remote, internal
  bool logo = false;
  const char* note = nullptr;  // instead of a programme
  GuideItem items[GUIDE_ITEMS];
  uint8_t count = 0;
};

size_t writeGuideJson(const GuideRow* rows, size_t n, uint32_t nowS, char* out, size_t cap);
