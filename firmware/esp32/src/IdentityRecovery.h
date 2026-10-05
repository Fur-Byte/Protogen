#pragma once
#include "WirelessCrypto.h"
namespace Wireless {
bool randomIdentity(Mac &m, uint8_t role);
class Recovery {
  uint8_t order[ChannelCount]{};
  uint8_t role = 0;
  uint32_t started = 0;
  bool initialized = false;

public:
  bool start(const uint8_t key[32], uint8_t ownRole, uint32_t now);
  uint8_t channel(uint32_t now) const;
};
} // namespace Wireless
