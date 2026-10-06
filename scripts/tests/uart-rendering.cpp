#include "sketch.h"
#include <random>
using HelmetLink::Packet;
UartSecurity::MbedMac clientMac;
bool clientAuthenticate(void *, const uint8_t *p, size_t n, uint8_t tag[32]) {
  return clientMac.authenticate(p, n, tag);
}
bool clientRandom(void *, uint8_t *p, size_t n) {
  while (n) {
    uint32_t word = Entropy.random();
    size_t take = n < 4 ? n : 4;
    memcpy(p, &word, take);
    p += take;
    n -= take;
  }
  return true;
}
UartSecurity::Endpoint client(UartSecurity::Esp, nullptr, clientAuthenticate,
                              clientRandom);
Packet latest;
void run(uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) {
    advanceTime(1000);
    loop();
  }
}
void receiveStatus() {
  Packet p;
  for (uint8_t b : Serial1.output)
    if (client.feed(b, millis(), p) && p.kind == HelmetLink::Status)
      latest = p;
  Serial1.output.clear();
}
void inject(const uint8_t *bytes, size_t n) {
  Serial1.input.append(reinterpret_cast<const char *>(bytes), n);
  run(12);
  receiveStatus();
}
int main() {
  setup();
  run(200);
  config.transitionMs = 0;
  config.naturalBlinks = 0;
  assert(client.begin(0, millis()));
  uint8_t encoded[UartSecurity::MaxEncoded];
  size_t n = client.poll(millis(), encoded, sizeof(encoded));
  assert(n);
  inject(encoded, n);
  n = client.poll(millis(), encoded, sizeof(encoded));
  assert(n);
  inject(encoded, n);
  run(110);
  receiveStatus();
  assert(client.online() && latest.epoch && latest.ticket);
  Packet p;
  p.kind = HelmetLink::Hello;
  p.epoch = latest.epoch;
  p.ticket = latest.ticket;
  p.length = 4;
  HelmetLink::put32(p.payload, 100);
  n = client.seal(p, encoded, sizeof(encoded));
  assert(n);
  inject(encoded, n);
  assert(pawIsOnline(millis()));
  p.kind = HelmetLink::Command;
  p.length = 8;
  p.sequence = 1;
  p.payload[4] = HelmetLink::Expression;
  p.payload[5] = 2;
  n = client.seal(p, encoded, sizeof(encoded));
  assert(n);
  inject(encoded, n);
  assert(state == 2);
  selectExpression(0, millis());
  inject(encoded, n);
  assert(state == 0);
  p.sequence = 2;
  p.payload[5] = 4;
  n = client.seal(p, encoded, sizeof(encoded));
  assert(n);
  uint8_t raw[UartSecurity::MaxRaw];
  size_t length = HelmetLink::cobsDecode(encoded, n - 1, raw, sizeof(raw));
  assert(length);
  raw[UartSecurity::Header + 21] ^= 1;
  HelmetLink::put16(raw + length - 2, HelmetLink::crc16(raw, length - 2));
  n = HelmetLink::cobsEncode(raw, length, encoded, sizeof(encoded));
  inject(encoded, n);
  assert(state == 0); // Valid CRC, invalid HMAC.
  length = HelmetLink::pack(p, raw, sizeof(raw));
  n = HelmetLink::cobsEncode(raw, length, encoded, sizeof(encoded));
  inject(encoded, n);
  assert(state == 0);
  p.payload[4] = 200;
  p.payload[5] = 0;
  p.sequence = 3;
  n = client.seal(p, encoded, sizeof(encoded));
  assert(n);
  inject(encoded, n);
  assert(state == 0);
  run(2200);
  assert(!pawIsOnline(millis()));
  // Local behavior keeps servicing controls even if secure UART/RF never
  // returns.
  const auto before = state;
  buttonInput = LOW;
  run(35);
  buttonInput = HIGH;
  run(35);
  assert(state == (before + 1) % ExpressionCount);
  std::mt19937 rng(0x4c4f4341);
  const auto rendered = SPI.bytes;
  for (unsigned i = 0; i < 1000; ++i) {
    for (auto &b : encoded)
      b = rng();
    encoded[sizeof(encoded) - 1] = 0;
    inject(encoded, sizeof(encoded));
  }
  assert(SPI.bytes > rendered && !sleeping);
  std::cout
      << "PASS: actual Teensy authenticated command dispatch, invalid MAC with "
         "valid CRC, replay/legacy/unsupported-action rejection and local "
         "rendering/buttons after 1000 malformed UART streams\n";
}
