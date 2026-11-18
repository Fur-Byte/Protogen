#pragma once
#include "WirelessConfig.h"
namespace Wireless {
class Indicator {
  uint32_t began = 0, lastCode = 0;
  bool showing = false;
  static uint32_t cycleMs(uint32_t code);

public:
  bool level(uint32_t now, bool active, bool hasCode, uint32_t code);
  bool complete(uint32_t now, uint32_t code) const;
};
} // namespace Wireless
