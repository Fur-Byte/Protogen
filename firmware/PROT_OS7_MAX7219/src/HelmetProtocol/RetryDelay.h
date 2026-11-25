#pragma once
#include <stdint.h>
#include "TimeCore.h"

namespace HelmetLink {
// An expired retry is disarmed, so an ancient timestamp cannot block work.
template <uint32_t DelayMs> class RetryDelay {
  static_assert(DelayMs > 0 && DelayMs < 0x80000000UL, "Retry delay must be bounded");
  uint32_t started = 0;
  bool pending = false;

public:
  void start(uint32_t now) { started = now; pending = true; }
  bool ready(uint32_t now) {
    pending = protogenRetryPending(pending, now, started, DelayMs);
    return !pending;
  }
  uint32_t remaining(uint32_t now) const {
    return pending ? protogenRemaining(now, started, DelayMs) : 0;
  }
};
} // namespace HelmetLink
