#pragma once
#include "HelmetProtocol.h"
#include "ReplayWindow.h"
#include "WirelessCrypto.h"
namespace Wireless {
struct Material {
  uint8_t pawKey[32]{}, helmetKey[32]{}, id[8]{}, recovery[32]{}, lmk[16]{},
      pmk[16]{};
  Mac pawMac{}, helmetMac{};
};
bool deriveMaterial(const uint8_t secret[32], const uint8_t pawNonce[32],
                    const uint8_t helmetNonce[32], const Mac &paw,
                    const Mac &helmet, Material &out);
class Session {
  Material keys{};
  Replay replay;
  uint64_t sequence = 0;
  uint8_t role = 0;
  bool active = false;

public:
  ~Session() { clear(); }
  void clear();
  void begin(uint8_t ownRole, const Material &material);
  bool seal(const HelmetLink::Packet &p, Frame &out);
  bool open(const uint8_t *bytes, size_t n, HelmetLink::Packet &p);
  bool ready() const { return active; }
};
// Directional allowlist, checked BEFORE forwarding anything to the Teensy.
bool applicationValid(const HelmetLink::Packet &p, uint8_t sender);
} // namespace Wireless
