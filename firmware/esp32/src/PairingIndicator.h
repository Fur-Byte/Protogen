#pragma once
#include "WirelessConfig.h"
namespace Wireless {
class Indicator {
  uint32_t began = 0, lastCode = 0;
  bool showing = false;

public:
  bool level(uint32_t now, bool active, bool hasCode, uint32_t code);
};
} // namespace Wireless
