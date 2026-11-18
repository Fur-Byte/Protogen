#pragma once
#include "WirelessConfig.h"

namespace Wireless {
// A reconnect never turns a pre-held button into a fresh gesture.
// Require stable raw AND debounced release followed by a full post-link
// release interval, even when a caller polls during the debounce window.
class ReleaseGate {
  bool waiting_ = false;
  uint32_t releasedAt_ = 0;
public:
  void reset() { waiting_ = false; }
  bool ready(uint32_t now, bool completelyReleased) {
    if (!completelyReleased) {
      reset();
      return false;
    }
    if (!waiting_) {
      waiting_ = true;
      releasedAt_ = now;
      return false;
    }
    return now - releasedAt_ >= DebounceMs;
  }
};
} // namespace Wireless
