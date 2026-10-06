#pragma once
// EXPERIMENTAL HOST CORE. Not registered in any production ESP-IDF component.
#if defined(ESP_PLATFORM)
#error EXPERIMENTAL_V4_PLATFORM_ACTIVATION_BLOCKED_UNTIL_SUPPORTED_S31_SDK
#endif
#include "HelmetProtocol.h"
#include <cstddef>
#include <cstdint>

namespace ProtogenV4 {
constexpr uint8_t Version = 4, Helmet = 1, Paw = 2;
constexpr size_t PacketBytes = 128, PublicKeyBytes = 1568,
                 PrivateKeyBytes = 3168, SharedBytes = 32, FragmentPayload = 68,
                 ObjectCount = 6;
constexpr uint64_t RecordLimit = 1000000, SequenceLimit = 0x7fffffffffffffffULL;
struct Identity {
  uint8_t bytes[6]{};
};
struct Frame {
  uint8_t bytes[PacketBytes]{};
};
struct Ticket {
  uint64_t generation = 0, serial = 0;
};
inline bool same(Ticket a, Ticket b) {
  return a.generation == b.generation && a.serial == b.serial;
}
inline bool same(const Identity &a, const Identity &b) {
  uint8_t d = 0;
  for (size_t i = 0; i < 6; ++i)
    d |= a.bytes[i] ^ b.bytes[i];
  return d == 0;
}
inline uint64_t read64(const uint8_t *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < 8; ++i)
    v |= uint64_t(p[i]) << (8 * i);
  return v;
}
inline void write64(uint8_t *p, uint64_t v) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = uint8_t(v >> (8 * i));
}
inline uint16_t read16(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline void write16(uint8_t *p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}
inline bool zero(const uint8_t *p, size_t n) {
  uint8_t v = 0;
  while (n--)
    v |= *p++;
  return v == 0;
}
enum class Mode : uint8_t { Pairing = 1, Session = 2 };
enum class Kind : uint8_t {
  Fragment = 10,
  PairConfirm = 20,
  PawFinished = 21,
  HelmetFinished = 22,
  Commit = 23,
  CommitAck = 24,
  Data = 32,
  Renew = 33
};
enum class Object : uint8_t {
  PawOffer,
  HelmetOffer,
  EncapsulationKey,
  Ciphertext,
  PawNonce,
  HelmetNonce
};
constexpr size_t objectSize(Object o) {
  return o == Object::PawOffer || o == Object::HelmetOffer          ? 129
         : o == Object::EncapsulationKey || o == Object::Ciphertext ? 1568
                                                                    : 16;
}
constexpr uint8_t objectRole(Object o) {
  return o == Object::PawOffer || o == Object::Ciphertext ||
                 o == Object::PawNonce
             ? Paw
             : Helmet;
}
constexpr uint8_t fragments(Object o) {
  return uint8_t((objectSize(o) + FragmentPayload - 1) / FragmentPayload);
}
struct Crypto {
  virtual ~Crypto() = default;
  virtual void wipe(void *, size_t) = 0;
  virtual bool random(void *, size_t) = 0;
  virtual bool hash(const void *, size_t, uint8_t[32]) = 0;
  virtual bool sha3(const void *, size_t, uint8_t[32]) = 0;
  virtual bool hmac(const uint8_t[32], const void *, size_t, uint8_t[32]) = 0;
  virtual bool hkdf(const uint8_t *, size_t, const uint8_t *, size_t,
                    const char *, uint8_t *, size_t) = 0;
  virtual bool gcm(bool, const uint8_t[32], const uint8_t[12], const uint8_t *,
                   size_t, const uint8_t *, size_t, uint8_t *, size_t) = 0;
  virtual bool generateP256(uint64_t &, uint8_t[65]) = 0;
  virtual bool ecdh(uint64_t, const uint8_t[65], uint8_t[32]) = 0;
  virtual void destroyP256(uint64_t &) = 0;
};
inline bool equal(const void *a, const void *b, size_t n) {
  const auto *x = static_cast<const uint8_t *>(a);
  const auto *y = static_cast<const uint8_t *>(b);
  uint8_t v = 0;
  while (n--)
    v |= *x++ ^ *y++;
  return v == 0;
}
struct Clock {
  virtual ~Clock() = default;
  virtual uint64_t nowMs() const = 0;
};
enum class Load { Empty, Found, Error };
struct Storage {
  virtual ~Storage() = default;
  virtual Load read(uint8_t *, size_t, size_t &) = 0;
  virtual bool write(const uint8_t *, size_t) = 0;
};
struct Material {
  uint8_t pawKey[32]{}, helmetKey[32]{}, id[8]{}, recovery[32]{}, lmk[16]{},
      pmk[16]{}, pawFinished[32]{}, helmetFinished[32]{}, commit[32]{},
      commitAck[32]{}, handshakeAuth[32]{}, fragmentAuth[32]{};
  Identity pawIdentity{}, helmetIdentity{};
};
struct Received {
  Frame frame;
  Identity source;
  bool nativeEncrypted = false;
};
struct Transport {
  virtual ~Transport() = default;
  // Adapter must purge old identities, pending frames and native peer keys.
  virtual bool rendezvous(const Identity &, uint8_t channel) = 0;
  // Adapter must purge pending handshake frames and use encrypted unicast
  // with these native LMK/PMK keys; no application traffic before success.
  virtual bool install(uint8_t role, const Material &, uint8_t channel) = 0;
  virtual bool send(const Frame &, const Identity *destination, Ticket) = 0;
  virtual bool receive(Received &) = 0;
};
struct Hooks {
  virtual ~Hooks() = default;
  virtual void connection(bool) = 0;
  virtual void packet(const HelmetLink::Packet &) = 0;
};
enum class Operation : uint8_t { KeyGen, Encaps, Decaps };
struct KemBuffers {
  uint8_t pk[1568]{}, sk[3168]{}, ct[1568]{}, ss[32]{};
};
struct Job {
  Operation operation = Operation::KeyGen;
  uint64_t generation = 0;
  const uint8_t *input = nullptr;
  size_t inputSize = 0;
  const uint8_t *ciphertext = nullptr;
  size_t ciphertextSize = 0;
};
struct Result {
  Ticket ticket;
  Operation operation = Operation::KeyGen;
  int status = -1;
  KemBuffers buffers;
};
struct Worker {
  virtual ~Worker() = default;
  virtual bool submit(const Job &, Ticket &) = 0;
  virtual bool poll(Result &) = 0;
  virtual void cancel(uint64_t generation) = 0;
};
struct Kem {
  virtual ~Kem() = default;
  virtual int execute(Operation, KemBuffers &) = 0;
};
struct Telemetry {
  uint64_t pairingAttempts = 0, pairingFailures = 0, pairingTimeout = 0,
           fragmentRejects = 0, conflictingFragments = 0, objectTimeout = 0,
           keygenFailures = 0, encapsFailures = 0, decapsFailures = 0,
           hybridFailures = 0, finishedFailures = 0, commitFailures = 0,
           replayRejects = 0, downgradeAttempts = 0, staleResults = 0,
           cancellations = 0, rateDrops = 0, rekeys = 0, recoveries = 0,
           pqStarted = 0, pqCompleted = 0, hybridStarted = 0,
           hybridCompleted = 0, handshakeTimeout = 0, v3RepairRequired = 0;
};
inline void count(uint64_t &v) {
  if (v != UINT64_MAX)
    ++v;
}
struct Budget {
  uint8_t credits, capacity;
  uint64_t period, last = 0;
  Budget(uint8_t n, uint64_t p) : credits(n), capacity(n), period(p) {}
  bool available(uint64_t now) {
    if (!period || !capacity)
      return false;
    if (now < last)
      return false;
    const auto n = (now - last) / period;
    if (n) {
      credits = n >= capacity
                    ? capacity
                    : uint8_t(credits + n > capacity ? capacity : credits + n);
      last = now;
    }
    if (!credits)
      return false;
    return true;
  }
  bool take(uint64_t now) {
    if (!available(now))
      return false;
    --credits;
    return true;
  }
};
} // namespace ProtogenV4
