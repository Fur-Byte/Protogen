#pragma once
#include "WirelessConfig.h"
namespace Wireless {
struct Received {
  Frame frame;
  Mac source;
  bool broadcast = false;
};
class Transport {
public:
  virtual ~Transport() = default;
  virtual bool configure(const Mac &own, const Mac *peer, const uint8_t *lmk,
                         const uint8_t *pmk, uint8_t channel) = 0;
  virtual bool channel(uint8_t channel) = 0;
  virtual bool
  send(const Frame &f,
       const Mac *destination) = 0; // nullptr = public control broadcast
  virtual bool receive(Received &out) = 0;
  virtual bool busy() const = 0;
};
} // namespace Wireless
