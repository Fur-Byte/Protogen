#pragma once
#include "SecurityState.h"
#include "WirelessCrypto.h"
namespace Wireless {
// Commit/reveal binds the public key, role, random contribution and temporary
// identity before either side can learn the other's random contribution.
class Pairing {
  Crypto::Agreement agreement;
  uint8_t role = 0;
  Mac local{}, peer{};
  uint8_t publicKey[65]{}, nonce[16]{}, peerPublic[65]{}, peerNonce[16]{},
      commitment[32]{}, peerCommitment[32]{}, transcript[32]{}, secret[32]{};
  uint32_t started = 0, nextSend = 0, lastEcdh = 0, lastAuth = 0,
           completedAt = 0, code = 0;
  enum class Phase : uint8_t {
    Idle,
    Discovery,
    Locked,
    Sas,
    LocalConfirmed,
    PeerConfirmed,
    Mutual,
    Stored
  };
  Phase phase = Phase::Idle;
  CryptoBudget ownedBudget;
  CryptoBudget *budget;
  uint8_t confirmKey[32]{};
  bool locked() const {
    return phase != Phase::Idle && phase != Phase::Discovery;
  }
  bool derived() const { return phase >= Phase::Sas; }
  bool confirmed() const {
    return phase == Phase::LocalConfirmed || phase == Phase::Mutual ||
           phase == Phase::Stored;
  }
  bool completed() const { return phase == Phase::Stored; }
  bool makeCommit(uint8_t r, const Mac &m, const uint8_t pub[65],
                  const uint8_t random[16], uint8_t out[32]);

public:
  explicit Pairing(CryptoBudget *b = nullptr) : budget(b ? b : &ownedBudget) {}
  ~Pairing() { clear(); }
  void clear();
  bool start(uint8_t ownRole, const Mac &own, uint32_t now);
  bool tick(uint32_t now, Frame &out, Mac &destination, bool &broadcast);
  bool receive(const Frame &f, const Mac &source, uint32_t now);
  bool confirm() {
    if (!derived() || completed())
      return false;
    phase = phase == Phase::PeerConfirmed ? Phase::Mutual
            : confirmed()                 ? phase
                                          : Phase::LocalConfirmed;
    nextSend = 0;
    return true;
  }
  bool active() const { return phase != Phase::Idle; }
  bool awaitingConfirmation() const { return derived() && !completed(); }
  uint32_t authenticationCode() const { return code; }
  bool ready() const { return phase == Phase::Mutual; }
  bool getSecret(uint8_t out[32]) const;
  void stored(uint32_t now) {
    phase = Phase::Stored;
    completedAt = now;
    // Grace-period retransmission only needs the dedicated confirmation key.
    Crypto::wipe(secret, sizeof(secret));
  }
};
} // namespace Wireless
