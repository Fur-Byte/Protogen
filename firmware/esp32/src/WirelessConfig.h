#pragma once
#include <stddef.h>
#include <stdint.h>
#include "NonceCore.h"
namespace Wireless {
constexpr uint8_t Version = 3, Helmet = 1, Paw = 2;
constexpr size_t PacketBytes = 128, HeaderBytes = 20, PlainBytes = 92,
                 TagBytes = 16, ControlTagAt = 96;
constexpr uint64_t MaxSequence = 0x7fffffffffffffffULL;
constexpr unsigned ReplayWindow = 64, RxSlots = 8, TxSlots = 4,
                   ReceiveBudget = 4;
constexpr uint32_t PairingMs = 60000, PairRetryMs = 400, HandshakeMs = 4000,
                   HandshakeRetryMs = 250;
constexpr uint32_t KeepaliveMinMs = 3000, KeepaliveMaxMs = 5000,
                   DisconnectMs = 12000;
constexpr uint32_t ServicePollMs = 2; // Two ticks at the configured 1 kHz.
// Production v3 data/pairing stay unchanged; both roles require this explicit
// handshake revision. Legacy Hello has no live receiver freshness and is denied.
constexpr uint8_t HandshakeRevision = 2;
constexpr uint32_t HandshakeServiceMs = 2, ChallengeReplyMs = 5;
constexpr unsigned ChallengeReplies = 4;
constexpr uint32_t ActiveMs = 1500, ActivePollMs = 100, FreshStatusMs = 200,
                   FastRecoveryMs = 15000;
constexpr uint32_t HelmetPairHoldMs = 2000, PawPairHoldMs = 3000,
                   PairConfirmHoldMs = 1000, EraseHoldMs = 12000;
constexpr uint32_t DebounceMs = 30, SleepComboMs = 1200, BoopDurationMs = 1200,
                   CommandRetryMs = 100, UartHeartbeatMs = 500;
constexpr uint32_t IndicatorMarkerMs = 1000, IndicatorMarkerGapMs = 1000,
                   IndicatorPulseMs = 120, IndicatorPulsePeriodMs = 300,
                   IndicatorDigitGapMs = 700;
constexpr uint32_t RecoveryIdentityMs = 30000;
constexpr uint32_t SessionLifetimeMs = 900000, SessionJitterMs = 120000;
constexpr uint32_t PawDwellMs = 400, HelmetDwellMs = 2400, AuthIntervalMs = 50,
                   EcdhIntervalMs = 1000;
constexpr uint64_t RekeyRecords = 1000000, SequenceRenewMargin = 1024;
constexpr unsigned CryptoBurst = 4, AuthBurst = 8, CreationBurst = 2;
constexpr uint32_t CryptoRefillMs = 500, AuthRefillMs = 50,
                   CreationRefillMs = 1000, InvalidBackoffMs = 250;
constexpr uint32_t PairStoredGraceMs = 2000;
constexpr uint8_t Channels[] = {1, 6, 11};
constexpr size_t ChannelCount = sizeof(Channels);
constexpr uint8_t PairChannel = 6;
enum Type : uint8_t {
  SessionHello = 1,
  SessionResponse = 2,
  SessionFinish = 3,
  PairCommit = 4,
  PairReveal = 5,
  PairConfirm = 6,
  Data = 7,
  SessionCommit = 8,
  SessionRequest = 9,
  SessionChallenge = 10,
  SessionProof = 11
};
inline uint64_t get64(const uint8_t *p) {
  return protogenRead64(p);
}
inline void put64(uint8_t *p, uint64_t v) {
  protogenWrite64(p, v);
}
inline bool due(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) >= 0;
}
inline bool channelAllowed(uint8_t channel) {
  for (auto c : Channels)
    if (c == channel)
      return true;
  return false;
}
struct Frame {
  uint8_t bytes[PacketBytes]{};
};
struct Mac {
  uint8_t bytes[6]{};
};
inline bool same(const Mac &a, const Mac &b) {
  uint8_t v = 0;
  for (unsigned i = 0; i < 6; ++i)
    v |= a.bytes[i] ^ b.bytes[i];
  return v == 0;
}
inline bool unicast(const Mac &a) {
  uint8_t v = 0;
  for (auto b : a.bytes)
    v |= b;
  return v && !(a.bytes[0] & 1);
}
inline void header(Frame &f, Type t, uint8_t role) {
  f = Frame{};
  f.bytes[0] = 0x70;
  f.bytes[1] = Version;
  f.bytes[2] = t;
  f.bytes[3] = role;
}
inline bool valid(const uint8_t *b, size_t n, uint8_t role) {
  return b && n == PacketBytes && b[0] == 0x70 && b[1] == Version &&
         b[2] >= SessionHello && b[2] <= SessionProof && b[3] == role;
}
} // namespace Wireless
