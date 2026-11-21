#include "ApplicationStatus.h"
#include <cassert>
#include <iostream>

using namespace HelmetLink;

static Packet baseStatus() {
  Packet p;
  p.kind = Status;
  p.length = StatusBytes;
  p.epoch = 99;
  p.ticket = 1;
  put32(p.payload, 12);
  p.payload[4] = 2;
  p.payload[5] = Visible | DisplayReady;
  return p;
}

int main() {
  Wireless::HelmetStatus paw;
  bool changed = true;
  auto base = baseStatus();
  assert(paw.receive(base, 100, changed) && !changed);
  for (int percent : {-1, 0, 100}) {
    const auto extended = Wireless::extendHelmetStatus(base, percent, 0x1234);
    assert(base.length == StatusBytes && extended.length == ExtendedStatusBytes);
    assert(!memcmp(base.payload, extended.payload, StatusBytes));
    assert(u16(extended.payload + StatusBytes) == (percent < 0 ? 0xffff : percent));
    assert(u16(extended.payload + StatusBytes + 2) == 0x1234);
    uint8_t raw[MaxRaw];
    const size_t size = pack(extended, raw, sizeof(raw));
    Packet decoded;
    assert(size && unpack(raw, size, decoded));
    assert(paw.receive(decoded, 101, changed) && !changed);
    assert(paw.available && paw.at == 101 && paw.bridge == 12);
    assert(paw.packet.length == ExtendedStatusBytes && paw.packet.ticket == base.ticket);
    const auto repeated = Wireless::extendHelmetStatus(extended, 50, 0);
    assert(repeated.length == extended.length &&
           !memcmp(repeated.payload, extended.payload, extended.length));
  }

  // Use status delivered through the application adapter to issue a real command.
  Gate gate;
  gate.epoch = paw.packet.epoch;
  gate.issue(paw.packet.ticket, 100);
  Packet hello;
  hello.kind = Hello;
  hello.length = 4;
  hello.epoch = paw.packet.epoch;
  hello.ticket = paw.packet.ticket;
  put32(hello.payload, paw.bridge);
  assert(gate.hello(hello, 101));
  Packet command = hello;
  command.kind = Command;
  command.length = 8;
  command.sequence = 1;
  command.payload[4] = Expression;
  command.payload[5] = (paw.packet.payload[4] + 1) % 6;
  assert(gate.check(command, 102) == Accepted);
  assert(gate.check(command, 1000) == Duplicate);
  command.sequence = 2;
  assert(gate.check(command, 401) == Expired);

  const auto good = paw.packet;
  auto rejected = [&](const Packet &bad) {
    changed = true;
    assert(!paw.receive(bad, 999, changed) && !changed);
    assert(paw.available && paw.at == 101 && paw.bridge == 12);
    assert(paw.packet.epoch == good.epoch && paw.packet.length == good.length);
    assert(!memcmp(paw.packet.payload, good.payload, good.length));
  };
  for (unsigned length = 0; length <= MaxPayload; ++length) {
    if (length == StatusBytes || length == ExtendedStatusBytes) continue;
    auto bad = good;
    bad.length = length;
    rejected(bad);
  }
  auto bad = good;
  put16(bad.payload + StatusBytes, 101);
  rejected(bad);
  bad = good;
  bad.payload[19] = 1;
  rejected(bad);
  bad = good;
  bad.payload[4] = 6;
  rejected(bad);
  bad = good;
  bad.payload[5] = 0x80;
  rejected(bad);
  bad = good;
  bad.epoch = 0;
  rejected(bad);
  bad = good;
  put32(bad.payload, 0);
  rejected(bad);
  bad = good;
  bad.kind = Ack;
  rejected(bad);
  const auto ack = Wireless::extendHelmetStatus(bad, 50, 0);
  assert(ack.kind == Ack && ack.length == bad.length && !memcmp(ack.payload, bad.payload, bad.length));

  base.epoch = 100;
  assert(paw.receive(base, 200, changed) && changed);
  assert(paw.receive(base, 201, changed) && !changed);
  put32(base.payload, 13);
  assert(paw.receive(base, 202, changed) && changed && paw.bridge == 13);
  paw.available = false; // An ACK requests a fresh status before another action.
  assert(paw.receive(base, 203, changed) && !changed && paw.available);
  paw.available = false;
  paw.bridge = 0; // A disconnected paw starts a new session.
  assert(paw.receive(base, 204, changed) && !changed && paw.available);
  std::cout
      << "PASS: base/extended status delivery, command acceptance/expiry/dedup, malformed status rejection, session changes\n";
}
