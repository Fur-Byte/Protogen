#pragma once
#include "IdentityRecovery.h"
#include "SessionHandshake.h"
#include "WirelessPairing.h"
#include "WirelessTransport.h"
namespace Wireless {
class Link {
public:
  struct Hooks {
    void *context;
    void (*connection)(void *, bool);
    void (*packet)(void *, const HelmetLink::Packet &);
    bool (*store)(void *, const uint8_t[32]);
  };

private:
  Transport &radio;
  Hooks hooks;
  uint8_t role;
  uint8_t psk[32]{}, recoveryKey[32]{};
  bool paired = false, recovering = false, connected = false,
       haveRecovery = false, fallback = false, started = false;
  bool rotateQueued = false, rotateSent = false;
  uint32_t renewAt = 0, identityAt = 0;
  uint32_t recoveryAt = 0, lastAuthenticated = 0, lastRxCheck = 0,
           nextKeepalive = 0, activeUntil = 0, nextPoll = 0, lastBusy = 0;
  Mac own{}, peer{};
  uint8_t currentChannel = PairChannel;
  Pairing pairing;
  Handshake handshake;
  Session session;
  Recovery recovery;
  HelmetLink::Packet outgoing[TxSlots]{};
  unsigned readAt = 0, writeAt = 0, count = 0;
  Frame tx{};
  bool txPending = false;
  void recover(uint32_t now);
  bool establish(uint32_t now);
  void keepalive(uint32_t now);

public:
  Link(Transport &transport, uint8_t ownRole, Hooks callbacks)
      : radio(transport), hooks(callbacks), role(ownRole) {}
  ~Link() {
    Crypto::wipe(psk, 32);
    Crypto::wipe(recoveryKey, 32);
  }
  bool begin(bool isPaired, const uint8_t secret[32], uint32_t now);
  bool startPairing(uint32_t now);
  void confirmPairing() { pairing.confirm(); }
  bool pairingActive() const { return pairing.active(); }
  bool pairingCode(uint32_t &code) const {
    if (!pairing.awaitingConfirmation())
      return false;
    code = pairing.authenticationCode();
    return true;
  }
  void tick(uint32_t now);
  bool queue(const HelmetLink::Packet &p);
  void activity(uint32_t now) {
    if (due(now, activeUntil))
      nextPoll = now;
    activeUntil = now + ActiveMs;
  }
  bool online() const { return connected; }
  bool hasPair() const { return paired; }
};
} // namespace Wireless
