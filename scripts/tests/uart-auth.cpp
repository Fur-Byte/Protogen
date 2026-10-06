#include "UartAuthentication.h"
extern bool productionTeensyMac(const uint8_t *, size_t, uint8_t[32]);
#include "WirelessCrypto.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
namespace {
uint8_t key[32]{13};
bool psaMac(void *, const uint8_t *p, size_t n, uint8_t out[32]) {
  return Wireless::Crypto::hmac(key, p, n, out);
}
bool teensyMac(void *, const uint8_t *p, size_t n, uint8_t out[32]) {
  return productionTeensyMac(p,n,out);
}
bool random(void *, uint8_t *p, size_t n) {
  return Wireless::Crypto::random(p, n);
}
using HelmetLink::Packet;
using UartSecurity::Endpoint;
bool deliver(Endpoint &target, const uint8_t *bytes, size_t n, uint32_t now,
             Packet &p) {
  bool result = false;
  for (size_t i = 0; i < n; ++i)
    result = target.feed(bytes[i], now, p) || result;
  return result;
}
void connect(Endpoint &esp, Endpoint &teensy, uint32_t epoch, uint32_t now) {
  uint8_t frame[UartSecurity::MaxEncoded];
  Packet p;
  size_t n = esp.poll(now, frame, sizeof(frame));
  assert(n);
  assert(!deliver(teensy, frame, n, now, p));
  n = teensy.poll(now, frame, sizeof(frame));
  assert(n);
  assert(!deliver(esp, frame, n, now, p));
  n = esp.poll(now, frame, sizeof(frame));
  assert(n);
  assert(!deliver(teensy, frame, n, now, p));
  assert(teensy.online());
  p.kind = HelmetLink::Status;
  p.epoch = epoch;
  p.length = 20;
  n = teensy.seal(p, frame, sizeof(frame));
  assert(n && deliver(esp, frame, n, now, p));
  assert(esp.online());
}
} // namespace
int main() {
  assert(Wireless::Crypto::init());
  // Compare the production Teensy upstream Mbed TLS MAC with the PSA adapter.
  uint8_t a[32], b[32];
  const uint8_t input[] = {1, 2, 3, 4};
  assert(psaMac(nullptr, input, 4, a) && teensyMac(nullptr, input, 4, b));
  assert(!memcmp(a, b, 32));
  Endpoint esp(UartSecurity::Esp, nullptr, psaMac, random),
      teensy(UartSecurity::Teensy, nullptr, teensyMac, random);
  assert(esp.begin(0, 100) && teensy.begin(9, 100));
  connect(esp, teensy, 9, 100);
  Packet p;
  p.kind = HelmetLink::Command;
  p.epoch = 9;
  p.sequence = 1;
  p.ticket = 2;
  p.length = 8;
  HelmetLink::put32(p.payload, 3);
  p.payload[4] = HelmetLink::Expression;
  uint8_t encoded[UartSecurity::MaxEncoded];
  size_t n = esp.seal(p, encoded, sizeof(encoded));
  assert(n);
  Packet decoded;
  assert(deliver(teensy, encoded, n, 110, decoded));
  assert(!deliver(teensy, encoded, n, 120, decoded));
  uint8_t old[sizeof(encoded)];
  memcpy(old, encoded, n);
  size_t oldLength = n;
  assert(esp.begin(0, 1300));
  connect(esp, teensy, 9, 1300);
  assert(!deliver(teensy, old, oldLength, 1310, decoded));
  n = esp.seal(p, encoded, sizeof(encoded));
  assert(n);
  assert(deliver(teensy, encoded, n, 1320, decoded));
  // Replay a recorded authenticated Request without possessing the key. It
  // cannot reset/deactivate the established receiver session without Confirm.
  Endpoint recording(UartSecurity::Esp, nullptr, psaMac, random);
  assert(recording.begin(0, 2500));
  n = recording.poll(2500, encoded, sizeof(encoded));
  assert(n);
  assert(!deliver(teensy, encoded, n, 2500, decoded));
  assert(teensy.online());
  n = esp.seal(p, encoded, sizeof(encoded));
  assert(n && deliver(teensy, encoded, n, 2510, decoded));
  // UART reboot epochs and session IDs are independent from RF reconnects.
  assert(teensy.begin(10, 4000));
  esp.poll(4000, encoded, sizeof(encoded));
  assert(esp.begin(0, 4000));
  connect(esp, teensy, 10, 4000);
  assert(!deliver(teensy, old, oldLength, 4010, decoded));
  assert(!esp.seal(p, encoded, sizeof(encoded)));
  p.epoch = 10;
  n = esp.seal(p, encoded, sizeof(encoded));
  assert(n);
  encoded[n - 2] ^= 1;
  assert(!deliver(teensy, encoded, n, 4020, decoded));
  // Legacy CRC-only commands are rejected in the authenticated parser.
  uint8_t raw[HelmetLink::MaxRaw];
  size_t rawLength = HelmetLink::pack(p, raw, sizeof(raw));
  n = HelmetLink::cobsEncode(raw, rawLength, encoded, sizeof(encoded));
  assert(!deliver(teensy, encoded, n, 4030, decoded));
  std::mt19937 rng(0x55415254);
  for (unsigned i = 0; i < 100000; ++i) {
    n = 1 + rng() % sizeof(encoded);
    for (size_t j = 0; j < n; ++j)
      encoded[j] = rng();
    encoded[n - 1] = 0;
    assert(!deliver(teensy, encoded, n, 4040 + i, decoded));
  }
  std::cout << "PASS: PSA/Mbed TLS UART interoperability, "
               "reset/replay/epoch/session/CRC-only rejection, 100000 "
               "malformed streams\n";
}
