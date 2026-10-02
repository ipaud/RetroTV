#pragma once

#include <mdns.h>

// mdns_init() exactly once for the whole firmware, whichever task asks first: the remote
// channels query names from the network task, the web remote announces its own from the loop.
// A function-local static is initialised once and thread-safely (C++11).
inline bool mdnsStarted() {
  static const bool started = mdns_init() == ESP_OK;
  return started;
}
