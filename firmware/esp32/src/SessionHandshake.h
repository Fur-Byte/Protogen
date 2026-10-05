#pragma once
#include "WirelessSession.h"
namespace Wireless {
class Handshake {
  uint8_t role = 0, auth[32]{}, secret[32]{}, pawNonce[32]{}, helmetNonce[32]{};
  Mac local{}, peer{};
  uint32_t started = 0, nextSend = 0, lastAuth = 0;
  bool active = false, responding = false, established = false;
  Frame cached{};
  Material material{};
  bool sign(Frame &f);

public:
  ~Handshake() { clear(); }
  void clear();
  bool start(const uint8_t pairingSecret[32], uint8_t ownRole, const Mac &own,
             uint32_t now);
  bool receive(const Frame &f, const Mac &source, uint32_t now);
  bool tick(uint32_t now, Frame &f, Mac &destination, bool &broadcast);
  void sent(const Frame &f) {
    if (role == Paw && responding && f.bytes[2] == SessionFinish)
      established = true;
  }
  bool ready() const { return established; }
  const Material &keys() const { return material; }
  bool pending() const { return responding; }
};
} // namespace Wireless
