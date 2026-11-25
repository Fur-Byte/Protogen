#include "WirelessSession.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
bool randomFails = false, encryptFails = false, decryptFails = false;
std::array<uint8_t, 12> lastNonce{};
unsigned encryptionCalls = 0, decryptionCalls = 0;
using namespace Wireless;

Material material(uint8_t seed) {
  Material m;
  for (unsigned i = 0; i < 32; ++i) {
    m.pawKey[i] = uint8_t(seed + i);
    m.helmetKey[i] = uint8_t(seed + 64 + i);
  }
  m.id[0] = seed;
  return m;
}

HelmetLink::Packet command() {
  HelmetLink::Packet p;
  p.kind = HelmetLink::Command;
  p.length = 8;
  p.sequence = p.epoch = p.ticket = 1;
  HelmetLink::put32(p.payload, 1);
  p.payload[4] = HelmetLink::Expression;
  return p;
}
}

// Deterministic transport fault fixture, not a crypto implementation. The
// real Mbed TLS suite separately checks authenticated encryption and pairing.
namespace Wireless::Crypto {
void wipe(void *p, size_t n) { memset(p, 0, n); }
bool equal(const void *a, const void *b, size_t n) { return memcmp(a,b,n) == 0; }
bool hash(const void *in, size_t n, uint8_t out[32]) {
  assert(n == 32);
  const auto *bytes = static_cast<const uint8_t *>(in);
  for (unsigned i = 0; i < 32; ++i) out[i] = bytes[i] ^ 0x5a;
  return true;
}
bool hkdf(const uint8_t *, size_t, const uint8_t *, size_t, const char *, uint8_t *, size_t) {
  assert(false && "deriveMaterial is outside this fault fixture");
  return false;
}
bool random(void *out, size_t n) {
  if (randomFails) return false;
  memset(out, 0xa5, n);
  return true;
}
bool gcm(bool encrypt, const uint8_t *, const uint8_t nonce[12],
         const uint8_t *aad, size_t aadSize, const uint8_t *in, size_t size,
         uint8_t *out, size_t capacity) {
  assert(aad && aadSize == HeaderBytes);
  memcpy(lastNonce.data(), nonce, lastNonce.size());
  if (encrypt) {
    ++encryptionCalls;
    assert(size == PlainBytes && capacity == PlainBytes + TagBytes);
    if (encryptFails) return false;
    memcpy(out, in, size);
    memset(out + size, 0x5c, TagBytes);
  } else {
    ++decryptionCalls;
    assert(size == PlainBytes + TagBytes && capacity == PlainBytes);
    if (decryptFails) return false;
    memcpy(out, in, capacity);
  }
  return true;
}
}

int main() {
  using namespace Wireless;
  Session sender, receiver;
  const auto m = material(1);
  assert(sender.begin(Paw,m) && receiver.begin(Helmet,m));
  Frame f;
  HelmetLink::Packet decoded;
  const auto p = command();

  randomFails = true;
  assert(!sender.seal(p,f) && encryptionCalls == 0 && sender.recordCount() == 0);
  randomFails = false;
  encryptFails = true;
  assert(!sender.seal(p,f) && encryptionCalls == 1 && sender.recordCount() == 0);
  const auto rejectedNonce = lastNonce;
  assert(get64(f.bytes+12) == 1);
  encryptFails = false;
  assert(sender.seal(p,f) && encryptionCalls == 2 && sender.recordCount() == 1);
  assert(get64(f.bytes+12) == 2 && rejectedNonce != lastNonce);

  decryptFails = true;
  decoded.sequence = 777;
  assert(!receiver.open(f.bytes,PacketBytes,decoded));
  assert(receiver.recordCount() == 0 && decoded.sequence == 777);
  decryptFails = false;
  auto malformed = f;
  malformed.bytes[HeaderBytes] = 0; // invalid decoded length, authenticated mock
  malformed.bytes[HeaderBytes+1] = 0;
  assert(!receiver.open(malformed.bytes,PacketBytes,decoded));
  assert(receiver.recordCount() == 1 && decoded.sequence == 777);
  assert(receiver.open(f.bytes,PacketBytes,decoded));
  assert(receiver.recordCount() == 2 && decoded.sequence == p.sequence);
  const unsigned beforeReplay = decryptionCalls;
  assert(!receiver.open(f.bytes,PacketBytes,decoded));
  assert(decryptionCalls == beforeReplay && receiver.recordCount() == 2);

  const auto next = material(2);
  assert(!sender.begin(Paw,m) && !sender.ready());
  assert(!sender.seal(p,f));
  assert(sender.begin(Paw,next));
  assert(receiver.begin(Helmet,next));
  assert(!receiver.open(f.bytes,PacketBytes,decoded)); // old session ID
  assert(sender.seal(p,f) && get64(f.bytes+12) == 1);
  assert(receiver.open(f.bytes,PacketBytes,decoded));
  receiver.clear();
  assert(!receiver.open(f.bytes,PacketBytes,decoded) && !receiver.ready());
  std::cout << "PASS: session AEAD fault boundaries, reserved nonce on encryption failure, record accounting, replay commit after decode, session replacement and clear\n";
}

