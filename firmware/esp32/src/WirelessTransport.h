#pragma once
#include "WirelessConfig.h"
namespace Wireless {
// Cheap callback admission before consuming the shared bounded receive queue.
inline bool admitIngress(const uint8_t *bytes, size_t size, bool broadcast,
                         bool operational) {
  if (!bytes || size != PacketBytes)
    return false;
  // Native session peers can send only unicast DATA. Public control traffic
  // must not crowd valid records out of the eight-slot callback queue.
  return !operational ||
         (!broadcast && bytes[0] == 0x70 && bytes[1] == Version &&
          bytes[2] == Data && (bytes[3] == Paw || bytes[3] == Helmet));
}
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
