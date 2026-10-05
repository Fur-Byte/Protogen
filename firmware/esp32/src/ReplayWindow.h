#pragma once
#include "WirelessConfig.h"
namespace Wireless {
class Replay {
  uint64_t top = 0, bits = 0;

public:
  void reset() { top = bits = 0; }
  bool allows(uint64_t sequence) const {
    if (!sequence || sequence > MaxSequence)
      return false;
    if (sequence > top)
      return true;
    const uint64_t age = top - sequence;
    return age < ReplayWindow && !(bits & (uint64_t(1) << age));
  }
  // Call only after successful authentication AND application decoding.
  void accept(uint64_t sequence) {
    if (sequence > top) {
      const uint64_t delta = sequence - top;
      bits = delta >= ReplayWindow ? 1 : (bits << delta) | 1;
      top = sequence;
    } else
      bits |= uint64_t(1) << (top - sequence);
  }
};
} // namespace Wireless
