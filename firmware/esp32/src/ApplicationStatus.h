#pragma once
#include "HelmetProtocol.h"

namespace Wireless {
inline HelmetLink::Packet extendHelmetStatus(const HelmetLink::Packet &p,
                                             int remainingPercent,
                                             uint16_t faults) {
  auto extended = p;
  if (p.kind == HelmetLink::Status && p.length == HelmetLink::StatusBytes) {
    extended.length = HelmetLink::ExtendedStatusBytes;
    HelmetLink::put16(extended.payload + HelmetLink::StatusBytes,
                      remainingPercent < 0 ? 0xffff : remainingPercent);
    HelmetLink::put16(extended.payload + HelmetLink::StatusBytes + 2, faults);
  }
  return extended;
}

struct HelmetStatus {
  HelmetLink::Packet packet{};
  uint32_t bridge = 0, at = 0;
  bool available = false;

  bool receive(const HelmetLink::Packet &p, uint32_t now, bool &changed) {
    changed = false;
    if (!HelmetLink::validHelmetStatus(p))
      return false;
    const uint32_t next = HelmetLink::u32(p.payload);
    changed = (available && p.epoch != packet.epoch) || (bridge && next != bridge);
    bridge = next;
    packet = p;
    at = now;
    available = true;
    return true;
  }
};
} // namespace Wireless
