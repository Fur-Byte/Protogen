#pragma once
#include "WirelessConfig.h"
#include <limits.h>
namespace Wireless {
enum class State : uint8_t {
  Unpaired,
  PairingDiscovery,
  PairingConfirmation,
  PairedOffline,
  Rendezvous,
  AuthenticatedEcdh,
  KeyConfirmation,
  Online,
  Rekeying,
  Recovering
};
inline const char *stateName(State s) {
  switch (s) {
  case State::Unpaired:
    return "UNPAIRED";
  case State::PairingDiscovery:
    return "PAIRING_DISCOVERY";
  case State::PairingConfirmation:
    return "PAIRING_CONFIRMATION";
  case State::PairedOffline:
    return "PAIRED_OFFLINE";
  case State::Rendezvous:
    return "RENDEZVOUS";
  case State::AuthenticatedEcdh:
    return "AUTHENTICATED_ECDH";
  case State::KeyConfirmation:
    return "KEY_CONFIRMATION";
  case State::Online:
    return "ONLINE";
  case State::Rekeying:
    return "REKEYING";
  case State::Recovering:
    return "RECOVERING";
  }
  return "INVALID";
}
inline void increment(uint32_t &v) {
  if (v != UINT32_MAX)
    ++v;
}
struct Diagnostics {
  uint32_t aeadFailures = 0, replayRejects = 0, malformed = 0, versions = 0,
           pairingFailures = 0, pairingTimeouts = 0, handshakeFailures = 0,
           finishedFailures = 0, recoveries = 0, renewals = 0,
           channelRecoveries = 0, identityRotations = 0, rateDrops = 0,
           disconnects = 0;
};
struct SecurityStatus {
  State state = State::Unpaired;
  uint32_t ageMs = 0, renewalInMs = 0;
  uint64_t records = 0;
  uint8_t channel = PairChannel;
  Diagnostics counters{};
};
// Credits survive protocol resets. Unsigned elapsed arithmetic is wrap-safe
// while serviced at least once per 2^32 ms; all deadlines are < 2^31 ms.
class Bucket {
  unsigned capacity, credits;
  uint32_t period, at = 0;

public:
  Bucket(unsigned cap, uint32_t refill)
      : capacity(cap), credits(cap), period(refill) {}
  bool take(uint32_t now) {
    const uint32_t elapsed = now - at;
    const uint32_t add = elapsed / period;
    if (add) {
      credits = add >= capacity - credits ? capacity : credits + add;
      at = now - elapsed % period;
    }
    if (!credits)
      return false;
    --credits;
    return true;
  }
};
struct CryptoBudget {
  Bucket ecc{CryptoBurst, CryptoRefillMs}, auth{AuthBurst, AuthRefillMs},
      creation{CreationBurst, CreationRefillMs};
  Diagnostics diagnostics{};
  uint32_t backoffAt = 0;
  unsigned failures = 0;
  bool backoff = false;
  bool verify(uint32_t now) {
    if (backoff && uint32_t(now - backoffAt) < InvalidBackoffMs) {
      increment(diagnostics.rateDrops);
      return false;
    }
    if (backoff) {
      backoff = false;
      failures = 0;
    }
    return spend(auth, now);
  }
  bool spend(Bucket &b, uint32_t now) {
    if (b.take(now))
      return true;
    increment(diagnostics.rateDrops);
    return false;
  }
  void invalid(uint32_t now, bool finished = false) {
    increment(finished ? diagnostics.finishedFailures
                       : diagnostics.handshakeFailures);
    if (failures < AuthBurst)
      ++failures;
    if (failures >= AuthBurst && !backoff) {
      backoff = true;
      backoffAt = now;
    }
  }
  void accepted() { failures = 0; }
};
} // namespace Wireless
