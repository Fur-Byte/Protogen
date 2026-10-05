#pragma once
#include "HelmetProtocol.h"
#include "WirelessConfig.h"
namespace Wireless {
struct Intent {
  uint8_t action = 0, value = 0;
  uint16_t duration = 0;
  uint32_t at = 0;
};
class Actions {
  Intent slots[TxSlots]{};
  unsigned read = 0, write = 0, count = 0;

public:
  enum : uint8_t { Next = 5, Previous = 6, ToggleSleep = 7 };
  void clear() { read = write = count = 0; }
  bool pending() const { return count != 0; }
  bool push(uint8_t action, uint8_t value, uint16_t duration, uint32_t now) {
    if (count == TxSlots)
      return false;
    slots[write] = {action, value, duration, now};
    write = (write + 1) % TxSlots;
    ++count;
    return true;
  }
  bool pop(uint32_t now, Intent &out) {
    while (count) {
      out = slots[read];
      read = (read + 1) % TxSlots;
      --count;
      if (now - out.at < HelmetLink::FreshMs)
        return true;
    }
    return false;
  }
};
} // namespace Wireless
