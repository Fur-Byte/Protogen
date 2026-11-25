#pragma once
#include "HelmetProtocol.h"
#include "WirelessConfig.h"
#include "ActionQueueCore.h"
namespace Wireless {
struct Intent {
  uint8_t action = 0, value = 0;
  uint16_t duration = 0;
  uint32_t at = 0;
};
class Actions {
  Intent slots[TxSlots]{};
  static_assert(TxSlots == ProtogenActionSlots, "Action queue capacity mismatch");
  ProtogenActionCursor cursor{};

  void removeFront() { protogenActionRemove(&cursor); }

public:
  enum : uint8_t { Next = 5, Previous = 6, ToggleSleep = 7 };
  void clear() { cursor = {}; }
  bool pending() const { return cursor.count != 0; }
  bool push(uint8_t action, uint8_t value, uint16_t duration, uint32_t now) {
    unsigned slot;
    if (!protogenActionReserve(&cursor, &slot)) return false;
    slots[slot] = {action, value, duration, now};
    return true;
  }
  bool pop(uint32_t now, Intent &out) {
    while (cursor.count) {
      out = slots[cursor.read];
      removeFront();
      if (now - out.at < HelmetLink::FreshMs)
        return true;
    }
    return false;
  }

  // A temporary transport rejection must not consume a fresh intent or
  // restart its age. Stale entries can still be discarded at the head.
  template <typename Send> bool dispatch(uint32_t now, Send send) {
    while (cursor.count) {
      const Intent &intent = slots[cursor.read];
      if (now - intent.at < HelmetLink::FreshMs) {
        if (!send(intent)) return false;
        removeFront();
        return true;
      }
      removeFront();
    }
    return false;
  }
};
} // namespace Wireless
