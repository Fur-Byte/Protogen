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
  bool paired = false, haveRecovery = false, fallback = false, started = false;
  bool initialized = false;
  State phase = State::Unpaired;
  CryptoBudget budget;
  uint32_t sessionAt = 0, retryAt = 0, lastRecoveryRx = 0;
  bool recoveryRxSeen = false;
  bool connected() const {
    return phase == State::Online || phase == State::Rekeying;
  }
  bool recovering() const {
    return phase == State::Recovering || phase == State::Rendezvous ||
           phase == State::AuthenticatedEcdh || phase == State::KeyConfirmation;
  }
  void transition(State next);
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
      : radio(transport), hooks(callbacks), role(ownRole), pairing(&budget),
        handshake(&budget), session(&budget.diagnostics) {}
  ~Link() {
    Crypto::wipe(psk, 32);
    Crypto::wipe(recoveryKey, 32);
  }
  // Returns true when service may start, including if configuration is retrying.
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
  // After a matching authenticated command ACK, coalesce a fresh status request
  // so queued input need not wait for the regular 100 ms poll.
  bool refreshStatus(uint32_t now) {
    if(role!=Paw || !online())return false;
    activity(now);nextPoll=now;return true;
  }
  bool online() const {
    return phase == State::Online && paired && session.ready() &&
           !pairing.active();
  }
  SecurityStatus securityStatus(uint32_t now) const {
    SecurityStatus out;
    out.state = phase;
    out.channel = currentChannel;
    out.counters = budget.diagnostics;
    if (connected()) {
      out.ageMs = now - sessionAt;
      out.records = session.recordCount();
      out.renewalInMs = due(now, renewAt) ? 0 : renewAt - now;
    }
    return out;
  }
  unsigned queued() const { return count + (txPending ? 1 : 0); }
  // Bench scheduler only: security constants and tick semantics are unchanged.
  // Complex handshake/pairing/rekey paths retain the original service bound.
  uint32_t serviceIn(uint32_t now) const {
    if (!started || phase == State::Unpaired) return 20;
    if (!online() || queued() || radio.busy() || session.needsRekey()) return 5;
    uint32_t wait = 20; // Console has no input notification; bounded fallback.
    const auto deadline = [&](uint32_t at) {
      const uint32_t left = due(now, at) ? 0 : at - now;
      if (left < wait) wait = left;
    };
    deadline(lastAuthenticated + DisconnectMs);
    deadline(renewAt + ActiveMs);
    if (role == Paw) {
      if (!due(now, activeUntil)) deadline(nextPoll);
      deadline(nextKeepalive);
      if (!due(now, renewAt)) deadline(renewAt);
      else if (!due(now, activeUntil)) deadline(activeUntil);
    }
    return wait;
  }
  bool invariant() const {
    return count <= TxSlots &&
           (!(phase == State::Online || phase == State::Rekeying) ||
            (paired && session.ready() && !pairing.active())) &&
           (!pairing.active() || !session.ready()) &&
           (!recovering() || !session.ready());
  }
  bool hasPair() const { return paired; }
};
} // namespace Wireless
