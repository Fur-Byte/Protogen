#include "ActionQueue.h"
#include "PairingIndicator.h"
#include "WirelessLink.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <random>
#include <vector>
using namespace Wireless;
using HelmetLink::Packet;
namespace {
Mac paw{{2, 3, 4, 5, 6, 1}}, helmet{{2, 3, 4, 5, 6, 0}};
Packet request() {
  Packet p;
  p.kind = HelmetLink::Status;
  return p;
}
Packet command(uint32_t seq = 1) {
  Packet p;
  p.kind = HelmetLink::Command;
  p.length = 8;
  p.sequence = seq;
  p.epoch = 1;
  p.ticket = 2;
  HelmetLink::put32(p.payload, 3);
  p.payload[4] = HelmetLink::Expression;
  return p;
}
Packet status() {
  Packet p;
  p.kind = HelmetLink::Status;
  p.length = HelmetLink::StatusBytes;
  p.epoch = 1;
  p.ticket = 2;
  HelmetLink::put32(p.payload, 3);
  return p;
}
struct FakeRadio : Transport {
  FakeRadio *other = nullptr;
  Mac own{}, peer{};
  uint8_t c = 6;
  bool encrypted = false, blocked = false, stuck = false;
  unsigned packets = 0, dropFinish = 0;
  uint16_t deniedChannels = 0;
  std::deque<Received> incoming;
  std::vector<Frame> captured;
  bool configure(const Mac &m, const Mac *p, const uint8_t *lmk,
                 const uint8_t *pmk, uint8_t channel) override {
    own = m;
    peer = p ? *p : Mac{};
    encrypted = p != nullptr;
    c = channel;
    incoming.clear();
    stuck = false;
    assert(unicast(m) && (m.bytes[0] & 2));
    if (p)
      assert(lmk && pmk && !same(m, *p));
    return true;
  }
  bool channel(uint8_t v) override {
    c = v;
    return true;
  }
  bool send(const Frame &f, const Mac *p) override {
    ++packets;
    captured.push_back(f);
    assert(f.bytes[2] != Data || (p && encrypted));
    if (f.bytes[2] == SessionFinish && dropFinish) {
      --dropFinish;
      return true;
    }
    if (other && !blocked && !other->blocked && !(deniedChannels & (1 << c)) &&
        !(other->deniedChannels & (1 << c)) && other->c == c &&
        (!p || same(*p, other->own)) && incoming.size() < RxSlots) {
      if (f.bytes[2] != Data || (other->encrypted && same(own, other->peer)))
        other->incoming.push_back({f, own, p == nullptr});
    }
    return true;
  }
  bool receive(Received &out) override {
    if (incoming.empty())
      return false;
    out = incoming.front();
    incoming.pop_front();
    return true;
  }
  bool busy() const override { return stuck; }
};
struct DeviceTest {
  FakeRadio radio;
  uint8_t role;
  uint8_t secret[32]{};
  bool stored = false, allowStorage = true;
  unsigned actions = 0, connections = 0, losses = 0;
  std::unique_ptr<Link> link;
  explicit DeviceTest(uint8_t r) : role(r) { reset(0); }
  static void connection(void *p, bool online) {
    auto &d = *static_cast<DeviceTest *>(p);
    if (online)
      ++d.connections;
    else
      ++d.losses;
  }
  static void packet(void *p, const Packet &v) {
    auto &d = *static_cast<DeviceTest *>(p);
    if (v.kind == HelmetLink::Command)
      ++d.actions;
    if (d.role == Helmet && v.kind == HelmetLink::Status && !v.length)
      d.link->queue(status());
  }
  static bool store(void *p, const uint8_t key[32]) {
    auto &d = *static_cast<DeviceTest *>(p);
    if (!d.allowStorage)
      return false;
    memcpy(d.secret, key, 32);
    d.stored = true;
    return true;
  }
  void reset(uint32_t now) {
    link = std::make_unique<Link>(radio, role,
                                  Link::Hooks{this, connection, packet, store});
    assert(link->begin(stored, secret, now));
  }
};
struct Rig {
  DeviceTest p{Paw}, h{Helmet};
  uint32_t now = 100;
  Rig(bool paired = true) {
    p.radio.other = &h.radio;
    h.radio.other = &p.radio;
    if (paired) {
      p.secret[0] = h.secret[0] = 9;
      p.stored = h.stored = true;
      p.reset(now);
      h.reset(now);
    }
  }
  void step(unsigned duration) {
    for (unsigned t = 0; t < duration; t += 5) {
      p.link->tick(now);
      h.link->tick(now);
      now += 5;
    }
  }
  bool online() const { return p.link->online() && h.link->online(); }
  void connect(unsigned limit = 40000) {
    for (unsigned t = 0; t < limit && !online(); t += 5)
      step(5);
    assert(online());
    step(100);
  }
  void pair() {
    assert(p.link->startPairing(now));
    assert(h.link->startPairing(now));
    uint32_t pc = 0, hc = 0;
    for (unsigned t = 0; t < 10000; t += 5) {
      step(5);
      if (p.link->pairingCode(pc) && h.link->pairingCode(hc))
        break;
    }
    assert(p.link->pairingCode(pc) && h.link->pairingCode(hc) && pc == hc);
    p.link->confirmPairing();
    h.link->confirmPairing();
    step(5000);
    assert(p.stored && h.stored && Crypto::equal(p.secret, h.secret, 32));
    connect();
  }
};
void testPairing() {
  Rig r(false);
  r.pair();
  Rig absent(false);
  assert(absent.p.link->startPairing(absent.now));
  absent.step(PairingMs + 5);
  assert(!absent.p.stored && !absent.p.link->pairingActive());
  Rig interrupted(false);
  assert(interrupted.p.link->startPairing(interrupted.now));
  assert(interrupted.h.link->startPairing(interrupted.now));
  interrupted.step(200);
  interrupted.p.reset(interrupted.now);
  interrupted.step(PairingMs);
  assert(!interrupted.p.stored && !interrupted.h.stored);
  interrupted.pair();
  Rig oneConfirmation(false);
  assert(oneConfirmation.p.link->startPairing(oneConfirmation.now));
  assert(oneConfirmation.h.link->startPairing(oneConfirmation.now));
  oneConfirmation.step(5000);
  oneConfirmation.p.link->confirmPairing();
  oneConfirmation.step(PairingMs);
  assert(!oneConfirmation.p.stored && !oneConfirmation.h.stored);
  Pairing a, b;
  assert(a.start(Paw, paw, 1) && b.start(Helmet, helmet, 1));
  Frame f;
  Mac m;
  bool broadcast = false;
  assert(a.tick(1, f, m, broadcast));
  Frame corrupt = f;
  corrupt.bytes[4] ^= 1;
  assert(b.receive(corrupt, paw, 1));
  assert(b.tick(1, f, m, broadcast));
  assert(a.receive(f, helmet, 1));
  for (uint32_t t = 401; t < 3000; t += 400) {
    if (a.tick(t, f, m, broadcast))
      b.receive(f, paw, t);
    if (b.tick(t, f, m, broadcast))
      a.receive(f, helmet, t);
  }
  assert(
      !b.awaitingConfirmation()); // commitment mismatch blocks establishment.
  Rig failure(false);
  failure.h.allowStorage = false;
  assert(failure.p.link->startPairing(failure.now));
  assert(failure.h.link->startPairing(failure.now));
  failure.step(5000);
  failure.p.link->confirmPairing();
  failure.h.link->confirmPairing();
  failure.step(10000);
  assert(!failure.h.stored && !failure.online());
}
void testVectors() {
  const auto bytes = [](const char *hex) {
    std::vector<uint8_t> v;
    for (size_t i = 0; hex[i]; i += 2) {
      const auto digit = [](char c) {
        return c <= '9' ? c - '0' : c - 'a' + 10;
      };
      v.push_back(uint8_t(digit(hex[i]) * 16 + digit(hex[i + 1])));
    }
    return v;
  };
  uint8_t secret[22], salt[13], out[42];
  memset(secret, 0x0b, 22);
  for (unsigned i = 0; i < 13; ++i)
    salt[i] = i;
  const char info[] = {char(0xf0), char(0xf1), char(0xf2), char(0xf3),
                       char(0xf4), char(0xf5), char(0xf6), char(0xf7),
                       char(0xf8), char(0xf9), 0};
  assert(Crypto::hkdf(secret, 22, salt, 13, info, out, 42));
  const auto expected = bytes("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c"
                              "5db02d56ecc4c5bf34007208d5b887185865");
  assert(Crypto::equal(out, expected.data(), 42));
  uint8_t key[32]{}, nonce[12]{}, plain[16]{}, encrypted[32]{};
  assert(Crypto::gcm(true, key, nonce, nullptr, 0, plain, 16, encrypted, 32));
  const auto gcm =
      bytes("cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919");
  assert(Crypto::equal(encrypted, gcm.data(), 32));
}
void testSession() {
  uint8_t key[32] = {1}, pn[32] = {2}, hn[32] = {3};
  Material m;
  assert(deriveMaterial(key, pn, hn, paw, helmet, m));
  assert(!Crypto::equal(m.pawKey, m.helmetKey, 32) && unicast(m.pawMac) &&
         unicast(m.helmetMac) && !same(m.pawMac, m.helmetMac));
  Session p, h;
  p.begin(Paw, m);
  h.begin(Helmet, m);
  Frame f;
  Packet decoded;
  assert(p.seal(command(), f));
  Frame original = f;
  assert(h.open(f.bytes, PacketBytes, decoded));
  assert(!h.open(f.bytes, PacketBytes, decoded));
  assert(p.seal(request(), f));
  f.bytes[PacketBytes - 1] ^= 1;
  assert(!h.open(f.bytes, PacketBytes, decoded));
  f.bytes[PacketBytes - 1] ^= 1;
  f.bytes[30] ^= 1;
  assert(!h.open(f.bytes, PacketBytes, decoded));
  hn[0]++;
  Material next;
  assert(deriveMaterial(key, pn, hn, paw, helmet, next));
  assert(!same(next.pawMac, m.pawMac) &&
         !Crypto::equal(next.pawKey, m.pawKey, 32));
  h.begin(Helmet, next);
  assert(!h.open(original.bytes, PacketBytes, decoded));
  key[0]++;
  Material wrong;
  assert(deriveMaterial(key, pn, hn, paw, helmet, wrong));
  memcpy(wrong.id, m.id,
         8); // Known session identity and MAC still cannot replace the keys.
  h.begin(Helmet, wrong);
  assert(!h.open(original.bytes, PacketBytes, decoded));
  h.begin(Helmet, m);
  p.begin(Paw, m); // Tests only: fresh object with reused test material, no
                   // production API reuse.
  Packet invalid = command();
  invalid.payload[4] = 200;
  assert(!p.seal(invalid, f));
  invalid = command();
  invalid.payload[5] = 6;
  assert(!p.seal(invalid, f));
  Frame old;
  for (unsigned i = 0; i < 100; ++i) {
    assert(p.seal(request(), f));
    if (i == 0)
      old = f;
    if (i >= 1)
      assert(h.open(f.bytes, PacketBytes, decoded));
  }
  assert(!h.open(old.bytes, PacketBytes, decoded));
  for (size_t n = 0; n < PacketBytes; ++n)
    assert(!h.open(f.bytes, n, decoded));
  Frame mutated = f;
  mutated.bytes[1]++;
  assert(!h.open(mutated.bytes, PacketBytes, decoded));
  std::mt19937 random(11);
  for (unsigned i = 0; i < 50000; ++i) {
    for (auto &v : mutated.bytes)
      v = random();
    assert(!h.open(mutated.bytes, random() % 140, decoded));
  }
  // Valid header + corrupt tag drives the real GCM parser, not just prefilter.
  for (unsigned i = 0; i < 2000; ++i) {
    assert(p.seal(request(), f));
    f.bytes[20 + random() % 108] ^= 1;
    assert(!h.open(f.bytes, PacketBytes, decoded));
  }
  Session hp, ph;
  hp.begin(Helmet, next);
  ph.begin(Paw, next);
  assert(hp.seal(status(), f) && ph.open(f.bytes, PacketBytes, decoded));
  Packet battery;
  battery.kind = HelmetLink::Status;
  battery.length = 4;
  HelmetLink::put16(battery.payload, 4100);
  Session bp, bh;
  bp.begin(Paw, next);
  bh.begin(Helmet, next);
  assert(bp.seal(battery, f) && bh.open(f.bytes, PacketBytes, decoded));
  battery.length = 24;
  battery.epoch = 1;
  HelmetLink::put32(battery.payload, 3);
  battery.payload[4] = 0;
  assert(hp.seal(battery, f) && ph.open(f.bytes, PacketBytes, decoded));
  Packet ack;
  ack.sequence = 1;
  ack.epoch = 1;
  ack.kind = HelmetLink::Ack;
  ack.length = 5;
  HelmetLink::put32(ack.payload, 3);
  assert(hp.seal(ack, f) && ph.open(f.bytes, PacketBytes, decoded));
  Replay replay;
  assert(!replay.allows(0) && !replay.allows(UINT64_MAX));
  assert(replay.allows(MaxSequence));
  replay.accept(MaxSequence);
  assert(!replay.allows(MaxSequence));
  assert(replay.allows(MaxSequence - 63));
  assert(!replay.allows(MaxSequence - 64));
  replay.accept(MaxSequence - 63);
  assert(!replay.allows(MaxSequence - 63));
  replay.reset();
  replay.accept(65);
  assert(replay.allows(2));
  replay.accept(2);
  assert(!replay.allows(1));
  // A well-tagged but unsupported command must not reach the dispatcher.
  header(f, Data, Paw);
  memcpy(f.bytes + 4, m.id, 8);
  put64(f.bytes + 12, 20000);
  uint8_t plain[PlainBytes]{};
  invalid = command();
  invalid.payload[4] = 200;
  size_t n = HelmetLink::pack(invalid, plain + 2, 90);
  HelmetLink::put16(plain, n);
  uint8_t nonce[12] = {0x70, Version, Paw, 0};
  put64(nonce + 4, 20000);
  assert(Crypto::gcm(true, m.pawKey, nonce, f.bytes, HeaderBytes, plain,
                     PlainBytes, f.bytes + HeaderBytes,
                     PacketBytes - HeaderBytes));
  assert(!h.open(f.bytes, PacketBytes, decoded));
}
void testRecovery() {
  Rig r;
  r.connect();
  Mac previous = r.p.radio.own;
  unsigned delivered = r.h.actions;
  assert(r.p.link->queue(command()));
  r.step(100);
  assert(r.h.actions == delivered + 1);
  r.step(2000);
  unsigned count = r.p.radio.packets + r.h.radio.packets;
  r.step(20000);
  assert(r.online());
  assert(r.p.radio.packets + r.h.radio.packets - count < 30); // quiet idle
  r.p.radio.blocked = true;
  r.step(DisconnectMs + 100);
  assert(!r.online() && r.p.losses && r.h.losses);
  r.p.radio.blocked = false;
  r.connect();
  assert(!same(previous, r.p.radio.own));
  Mac renewalMac = r.p.radio.own;
  r.step(SessionLifetimeMs + SessionJitterMs + 40000);
  r.connect();
  assert(!same(renewalMac, r.p.radio.own));
  r.p.reset(r.now);
  r.connect();
  r.h.reset(r.now);
  r.connect();
  r.p.reset(r.now);
  r.h.reset(r.now);
  r.connect();
  // Current-channel interference must rendezvous on a different usable one.
  const auto oldChannel = r.p.radio.c;
  r.p.radio.deniedChannels = r.h.radio.deniedChannels =
      uint16_t(1 << oldChannel);
  r.step(DisconnectMs + 100);
  r.connect();
  assert(r.p.radio.c != oldChannel && r.p.radio.c == r.h.radio.c);
  r.p.radio.deniedChannels = r.h.radio.deniedChannels = 0;
  Rig lostFinish;
  lostFinish.p.radio.dropFinish = 1;
  lostFinish.connect();
  assert(lostFinish.p.connections >= 2);
  // Queue saturation is bounded and disconnect clears actions.
  for (unsigned i = 0; i < TxSlots; ++i)
    assert(r.p.link->queue(command(100 + i)));
  assert(!r.p.link->queue(command(104)));
  r.step(200);
  assert(r.h.actions >= delivered + 1);
  // Known MAC with wrong keys cannot establish or inject commands.
  Rig outage;
  outage.connect();
  outage.p.radio.blocked = true;
  outage.step(DisconnectMs + 100);
  const Mac offlineMac = outage.p.radio.own;
  outage.step(RecoveryIdentityMs + HandshakeMs + 100);
  assert(!same(offlineMac, outage.p.radio.own));
  outage.p.radio.blocked = false;
  outage.connect();
  Rig wrong;
  wrong.h.secret[0] = 22;
  wrong.h.reset(wrong.now);
  wrong.step(30000);
  assert(!wrong.online() && !wrong.h.actions);
  // Capture previous ciphertext; inject after rekey under observed current MAC.
  Frame capture{};
  for (const Frame &f : r.p.radio.captured)
    if (f.bytes[2] == Data)
      capture = f;
  r.h.reset(r.now);
  r.connect();
  delivered = r.h.actions;
  r.h.radio.incoming.push_back({capture, r.p.radio.own, false});
  r.step(10);
  assert(r.h.actions == delivered);
  r.p.radio.stuck = true;
  r.step(HandshakeMs + 100);
  assert(r.p.losses >= 2);
  r.connect();
}
void testIdentityAndActions() {
  uint8_t key[32] = {7};
  Recovery p, h;
  assert(p.start(key, Paw, 100) && h.start(key, Helmet, 999));
  for (uint32_t t = 100; t < 10000; t += 100)
    assert(channelAllowed(p.channel(t)));
  for (unsigned i = 0; i < 1000; ++i) {
    Mac m;
    assert(randomIdentity(m, Paw) && unicast(m) && (m.bytes[0] & 2));
  }
  Actions q;
  for (unsigned i = 0; i < 4; ++i)
    assert(q.push(Actions::Next, 0, 0, 100));
  assert(!q.push(Actions::Next, 0, 0, 100));
  Intent intent;
  for (unsigned i = 0; i < 4; ++i)
    assert(q.pop(110, intent));
  assert(!q.pop(110, intent));
  assert(q.push(HelmetLink::Boop, 0, 1200, UINT32_MAX - 20));
  assert(q.pop(30, intent));
  assert(q.push(HelmetLink::Boop, 0, 1200, 100));
  assert(!q.pop(400, intent));
  q.clear();
  assert(!q.pending());
  Indicator led;
  assert(!led.level(0, false, false, 0));
  assert(led.level(0, true, false, 0));
  assert(!led.level(300, true, false, 0));
  assert(led.level(1000, true, true, 123456));
  assert(!led.level(2500, true, true, 123456));
}
} // namespace
int main() {
  assert(Crypto::init());
  testVectors();
  testPairing();
  testSession();
  testRecovery();
  testIdentityAndActions();
  std::cout
      << "PASS: wireless pairing, physical confirmations, interruption/storage "
         "failures, AEAD, replay, 52000 fuzz/corruption cases, fresh "
         "sessions/MACs, reboot/loss/channel recovery, quiet keepalives, "
         "bounded rapid actions\n";
}
