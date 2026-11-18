#include "ActionQueue.h"
#include "PairingIndicator.h"
#include "ControlArming.h"
#include "UartAuthentication.h"
#include "WirelessLink.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
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
  unsigned packets = 0, dropFinish = 0, failConfigurations = 0;
  std::array<unsigned, SessionProof + 1> dropControl{};
  uint16_t deniedChannels = 0;
  std::deque<Received> incoming;
  std::vector<Frame> captured;
  bool configure(const Mac &m, const Mac *p, const uint8_t *lmk,
                 const uint8_t *pmk, uint8_t channel) override {
    if (failConfigurations) { --failConfigurations; return false; }
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
    if (captured.size() == 256)
      captured.erase(captured.begin());
    captured.push_back(f);
    assert(f.bytes[2] != Data || (p && encrypted));
    if (f.bytes[2] < dropControl.size() && dropControl[f.bytes[2]]) {
      --dropControl[f.bytes[2]]; return true;
    }
    if (f.bytes[2] == SessionFinish && dropFinish) {
      --dropFinish;
      return true;
    }
    if (other && !blocked && !other->blocked && !(deniedChannels & (1 << c)) &&
        !(other->deniedChannels & (1 << c)) && other->c == c &&
        (!p || same(*p, other->own)) && other->incoming.size() < RxSlots) {
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
  unsigned actions = 0, connections = 0, losses = 0, statusRequests = 0;
  bool properties = false;
  std::map<uint32_t, Mac> authority;
  std::set<uint32_t> executed;
  std::set<std::array<uint8_t, 6>> identities;
  std::unique_ptr<Link> link;
  explicit DeviceTest(uint8_t r) : role(r) { reset(0); }
  static void connection(void *p, bool online) {
    auto &d = *static_cast<DeviceTest *>(p);
    if (online) {
      std::array<uint8_t, 6> address{};
      memcpy(address.data(), d.radio.own.bytes, 6);
      assert(d.identities.insert(address).second);
      ++d.connections;
    } else
      ++d.losses;
  }
  static void packet(void *p, const Packet &v) {
    auto &d = *static_cast<DeviceTest *>(p);
    assert(d.link->online());
    if (v.kind == HelmetLink::Command) {
      if (d.properties) {
        const auto found = d.authority.find(v.sequence);
        assert(found != d.authority.end() && same(found->second, d.radio.own));
        assert(d.executed.insert(v.sequence).second);
      }
      ++d.actions;
    }
    if (d.role == Helmet && v.kind == HelmetLink::Status && !v.length) {
      ++d.statusRequests;d.link->queue(status());
    }
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
void testSasMismatchAndReplacement() {
  Pairing victimPaw, victimHelmet, rogueHelmet, roguePaw;
  Mac rogueH{{2, 8, 9, 10, 11, 0}}, rogueP{{2, 8, 9, 10, 11, 1}};
  assert(victimPaw.start(Paw, paw, 100) &&
         victimHelmet.start(Helmet, helmet, 100));
  assert(rogueHelmet.start(Helmet, rogueH, 100) &&
         roguePaw.start(Paw, rogueP, 100));
  const auto exchange = [](Pairing &from, Pairing &to, const Mac &source,
                           uint32_t now) {
    Frame f;
    Mac dest;
    bool broadcast = false;
    if (from.tick(now, f, dest, broadcast))
      to.receive(f, source, now);
  };
  for (uint32_t now = 100; now < 5000; now += 100) {
    exchange(victimPaw, rogueHelmet, paw, now);
    exchange(rogueHelmet, victimPaw, rogueH, now);
    exchange(victimHelmet, roguePaw, helmet, now);
    exchange(roguePaw, victimHelmet, rogueP, now);
  }
  assert(victimPaw.awaitingConfirmation() &&
         victimHelmet.awaitingConfirmation());
  // Model the documented human policy: unequal six-digit codes are not
  // confirmed. A rare accidental 1/1,000,000 collision is the stated SAS limit.
  if (victimPaw.authenticationCode() != victimHelmet.authenticationCode()) {
    rogueHelmet.confirm();
    roguePaw.confirm();
    for (uint32_t now = 5000; now < 10000; now += 100) {
      exchange(rogueHelmet, victimPaw, rogueH, now);
      exchange(roguePaw, victimHelmet, rogueP, now);
    }
    assert(!victimPaw.ready() && !victimHelmet.ready());
    uint8_t secret[32]{};
    assert(!victimPaw.getSecret(secret) && !victimHelmet.getSecret(secret));
  }
  Frame f;
  Mac dest;
  bool broadcast = false;
  victimPaw.tick(PairingMs + 100, f, dest, broadcast);
  victimHelmet.tick(PairingMs + 100, f, dest, broadcast);
  assert(!victimPaw.active() && !victimHelmet.active());
  Rig old;
  old.connect();
  uint8_t saved[32];
  memcpy(saved, old.p.secret, 32);
  assert(old.p.link->startPairing(old.now));
  old.step(PairingMs + 5);
  old.connect();
  assert(Crypto::equal(saved, old.p.secret, 32) &&
         Crypto::equal(saved, old.h.secret, 32));
  Crypto::wipe(saved, 32);
}
void testInvalidPairingPointRecovery() {
  Rig r;
  r.connect();
  assert(r.h.link->startPairing(r.now));
  const Mac attacker{{2, 9, 8, 7, 6, 1}};
  uint8_t commitInput[89] = {Version, Paw};
  memcpy(commitInput + 2, attacker.bytes, 6);
  commitInput[8] = 4;
  commitInput[73] = 1; // SEC1 (0,0) is NOT on P-256.
  Frame f;
  header(f, PairCommit, Paw);
  assert(Crypto::hash(commitInput, 89, f.bytes + 4));
  r.h.radio.incoming.push_back({f, attacker, true});
  r.step(5);
  header(f, PairReveal, Paw);
  f.bytes[4] = 4;
  f.bytes[69] = 1;
  memcpy(f.bytes + 85, attacker.bytes, 6);
  r.h.radio.incoming.push_back({f, attacker, false});
  r.step(5);
  assert(!r.h.link->pairingActive() && r.h.link->hasPair() &&
         r.h.link->invariant());
  assert(r.h.link->securityStatus(r.now).counters.pairingFailures > 0);
  r.connect(); // Invalid peer points cannot leave a persistent remote lockout.
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
  uint8_t key[32] = {1}, hn[32] = {3};
  Material m;
  assert(deriveMaterial(key, hn, m));
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
  f.bytes[30] ^= 1;
  // Authentication failure must leave the authentic record eligible.
  const bool authenticatedAfterReject = h.open(f.bytes, PacketBytes, decoded);
  assert(authenticatedAfterReject);
  assert(!h.open(f.bytes, PacketBytes, decoded));
  hn[0]++;
  Material next;
  assert(deriveMaterial(key, hn, next));
  assert(!same(next.pawMac, m.pawMac) &&
         !Crypto::equal(next.pawKey, m.pawKey, 32));
  h.begin(Helmet, next);
  assert(!h.open(original.bytes, PacketBytes, decoded));
  key[0]++;
  Material wrong;
  assert(deriveMaterial(key, hn, wrong));
  memcpy(wrong.id, m.id,
         8); // Known session identity and MAC still cannot replace the keys.
  h.begin(Helmet, wrong);
  assert(!h.open(original.bytes, PacketBytes, decoded));
  assert(!p.begin(Paw, m)); // Same traffic keys cannot reset the TX counter.
  Material partial = m;
  partial.helmetKey[0] ^= 1;
  assert(!p.begin(
      Paw, partial)); // Changing only the other direction is insufficient.
  partial = m;
  partial.pawKey[0] ^= 1;
  assert(!p.begin(Paw, partial));
  Crypto::wipe(&partial, sizeof(partial));
  key[0]++;
  hn[0]++;
  assert(deriveMaterial(key, hn, m));
  assert(h.begin(Helmet, m));
  assert(p.begin(Paw, m));
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
void testReceiverHardExpiry() {
  // A noncooperating peer can leave a valid, authenticated command queued
  // until after the hard session deadline. The helmet must expire *before*
  // processing that record, even when no renewal request was sent.
  for (uint32_t start : {100U, UINT32_MAX - 4000U}) {
    Rig r;
    if (start != 100U) {
      r.now = start;
      r.p.reset(r.now);
      r.h.reset(r.now);
    }
    r.connect();
    const unsigned before = r.h.actions;
    assert(r.p.link->queue(command()));
    r.p.link->tick(r.now);
    assert(!r.h.radio.incoming.empty());
    r.now += SessionLifetimeMs + SessionJitterMs + ActiveMs + 10;
    r.h.link->tick(r.now);
    assert(r.h.actions == before);
    assert(!r.h.link->online());
  }
}
void testRadioStartupConfigureRetry() {
  for (uint32_t start : {100U, UINT32_MAX - 100U}) {
    DeviceTest device(Helmet);
    device.radio.failConfigurations = 2;
    device.reset(start);
    assert(!device.link->startPairing(start));
    device.link->tick(start + InvalidBackoffMs - 1);
    assert(device.radio.failConfigurations == 1);
    device.link->tick(start + InvalidBackoffMs);
    assert(device.radio.failConfigurations == 0);
    assert(!device.link->startPairing(start + InvalidBackoffMs));
    device.link->tick(start + InvalidBackoffMs * 2);
    assert(device.link->startPairing(start + InvalidBackoffMs * 2));
  }
}
void testRadioConfigureFaultRetries() {
  Rig r;
  r.connect();
  const unsigned previousConnections = r.h.connections;
  r.h.radio.failConfigurations = 2;
  r.h.radio.stuck = true;
  r.step(HandshakeMs + InvalidBackoffMs * 3 + 100);
  assert(r.h.radio.failConfigurations == 0);
  r.connect();
  assert(r.h.connections > previousConnections);
  const unsigned delivered = r.h.actions;
  assert(r.p.link->queue(command()));
  r.step(100);
  assert(r.h.actions == delivered + 1);
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
  assert(lostFinish.p.connections == 1);
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
void testHandshakeAndForwardSecrecy() {
  uint8_t psk[32] = {19};
  Handshake p, h;
  assert(p.start(psk, Paw, paw, 100) && h.start(psk, Helmet, helmet, 100));
  Frame requestFrame, challenge, hello, response, finish, helmetFinish, commit;
  Mac dest;
  bool broadcast = false;
  assert(p.tick(100, requestFrame, dest, broadcast) && broadcast);
  assert(h.receive(requestFrame, paw, 100) && !h.pending());
  assert(h.tick(100, challenge, dest, broadcast));
  assert(p.receive(challenge, helmet, 100));
  assert(p.tick(100, hello, dest, broadcast));
  assert(h.receive(hello, paw, 100));
  assert(!h.ready());
  assert(h.tick(100, response, dest, broadcast) && !broadcast);
  assert(p.receive(response, helmet, 100));
  assert(!p.ready());
  assert(p.tick(100, finish, dest, broadcast));
  assert(h.receive(finish, paw, 100));
  assert(!h.ready());
  assert(h.tick(100, helmetFinish, dest, broadcast));
  assert(p.receive(helmetFinish, helmet, 100));
  assert(!p.ready());
  assert(p.tick(100, commit, dest, broadcast));
  p.sent(commit);
  assert(h.receive(commit, paw, 100));
  assert(p.ready() && h.ready());
  Material old = p.keys();
  assert(Crypto::equal(old.pawKey, h.keys().pawKey, 32));
  // The public transcript + extracted PSK can authenticate this recording,
  // but using the PSK as the KDF input cannot reproduce the ECDH traffic key.
  uint8_t canonical[288], hash[32];
  memcpy(canonical, challenge.bytes, 96);
  memcpy(canonical + 96, hello.bytes, 96);
  memcpy(canonical + 192, response.bytes, 96);
  assert(Crypto::hash(canonical, sizeof(canonical), hash));
  Material attacker;
  assert(deriveMaterial(psk, hash, attacker));
  assert(!Crypto::equal(attacker.pawKey, old.pawKey, 32));
  Session sender, receiver;
  assert(sender.begin(Paw, old));
  assert(receiver.begin(Helmet, attacker));
  Frame captured;
  Packet decoded;
  assert(sender.seal(command(), captured));
  assert(!receiver.open(captured.bytes, 128, decoded));
  assert(p.start(psk, Paw, paw, 1100) && h.start(psk, Helmet, helmet, 1100));
  Frame newHello, newChallenge, newProof, newResponse, newFinished;
  assert(p.tick(1100, newHello, dest, broadcast));
  assert(!Crypto::equal(newHello.bytes + 4, hello.bytes + 4, 65));
  assert(h.receive(newHello, paw, 1100) && !h.pending());
  assert(h.tick(1100, newChallenge, dest, broadcast));
  assert(p.receive(newChallenge, helmet, 1100));
  assert(p.tick(1100, newProof, dest, broadcast));
  assert(h.receive(newProof, paw, 1100));
  assert(h.tick(1100, newResponse, dest, broadcast));
  assert(p.receive(newResponse, helmet, 1100));
  assert(!Crypto::equal(old.pawKey, p.keys().pawKey, 32) &&
         !same(old.pawMac, p.keys().pawMac));
  assert(!h.receive(finish, paw, 1100));
  assert(!h.ready());
  assert(p.tick(1100, newFinished, dest, broadcast));
  Frame badFinished = newFinished; badFinished.bytes[127] ^= 1;
  assert(!h.receive(badFinished, paw, 1100));
  assert(!h.failed() && !h.ready() && h.pending());
  assert(h.receive(newFinished, paw, 1100));
  assert(h.tick(1100, helmetFinish, dest, broadcast));
  assert(p.receive(helmetFinish, helmet, 1100));
  assert(p.tick(1100, commit, dest, broadcast)); p.sent(commit);
  assert(h.receive(commit, paw, 1100) && p.ready() && h.ready());
  Crypto::wipe(&old, sizeof(old));
  Crypto::wipe(&attacker, sizeof(attacker));
  CryptoBudget budget;
  unsigned allowed = 0;
  for (unsigned i = 0; i < 1000; ++i)
    if (budget.spend(budget.ecc, UINT32_MAX - 100))
      ++allowed;
  assert(allowed == CryptoBurst);
  assert(budget.spend(budget.ecc, 600));
  for (unsigned i = 0; i < AuthBurst; ++i)
    budget.invalid(UINT32_MAX - 100);
  assert(!budget.verify(UINT32_MAX - 50));
  assert(budget.verify(200));
  Diagnostics d;
  d.aeadFailures = UINT32_MAX;
  increment(d.aeadFailures);
  assert(d.aeadFailures == UINT32_MAX);
}
void testHelloAdmission() {
  Frame ingress;
  header(ingress, SessionHello, Paw);
  assert(admitIngress(ingress.bytes, PacketBytes, true, false, paw, paw));
  assert(!admitIngress(ingress.bytes, PacketBytes, true, true, paw, paw));
  assert(!admitIngress(ingress.bytes, PacketBytes, false, true, paw, paw));
  header(ingress, Data, Paw);
  assert(admitIngress(ingress.bytes, PacketBytes, false, true, paw, paw));
  assert(!admitIngress(ingress.bytes, PacketBytes, true, true, paw, paw));
  assert(!admitIngress(ingress.bytes, PacketBytes - 1, false, true, paw, paw));
  assert(!admitIngress(nullptr, PacketBytes, false, true, paw, paw));
  uint8_t psk[32] = {29};
  Handshake p, h;
  assert(p.start(psk, Paw, paw, 100) && h.start(psk, Helmet, helmet, 100));
  Frame requestFrame, hello, challenge, response;
  Mac destination;
  bool broadcast = false;
  assert(p.tick(100, requestFrame, destination, broadcast) && broadcast);
  Frame forged = requestFrame; forged.bytes[127] ^= 1;
  assert(!h.receive(forged, paw, 100));
  assert(h.state() == Handshake::Phase::Rendezvous);
  assert(h.receive(requestFrame, paw, 100) && !h.pending());
  assert(h.tick(100, challenge, destination, broadcast));
  assert(p.receive(challenge, helmet, 100));
  assert(p.tick(100, hello, destination, broadcast));
  assert(h.receive(hello, paw, 100));
  assert(h.tick(100, response, destination, broadcast));
  for (uint32_t now = 101; now < 100 + HandshakeRetryMs; ++now) {
    assert(h.receive(hello, paw, now));
    assert(!h.tick(now, response, destination, broadcast));
    assert(h.state() == Handshake::Phase::AwaitPawFinished);
  }
  assert(h.tick(100 + HandshakeRetryMs, response, destination, broadcast));
  assert(p.receive(response, helmet, 350));
  Frame finished;
  assert(p.tick(350, finished, destination, broadcast));
  for (uint32_t now = 351; now < 350 + HandshakeRetryMs; ++now) {
    assert(p.receive(response, helmet, now));
    assert(!p.tick(now, finished, destination, broadcast));
  }
  assert(p.tick(350 + HandshakeRetryMs, finished, destination, broadcast));
  CryptoBudget attackBudget;
  Handshake attacked(&attackBudget);
  assert(attacked.start(psk, Helmet, helmet, 100));
  // Direct fixture calls are intentionally adversarial. Production Link bounds
  // these cheap operations per service interval (separately regression-tested).
  for (unsigned i = 0; i < 1000; ++i)
    assert(!attacked.receive(forged, paw, 100));
  assert(attacked.state() == Handshake::Phase::Rendezvous);
  assert(attackBudget.diagnostics.handshakeFailures == 1000);
  assert(attackBudget.diagnostics.rateDrops == 0);
  assert(attacked.receive(requestFrame, paw, 101));
  assert(attackBudget.spend(attackBudget.ecc,101,CryptoBurst));
  // Restart creates fresh receiver admission. An old Request only earns a cheap
  // reply; a captured Proof cannot recover its spent freshness or block a peer.
  assert(h.start(psk, Helmet, helmet, 1100));
  assert(!h.receive(hello, paw, 1100) && !h.pending());
  assert(h.receive(requestFrame, paw, 1100) && !h.pending());
  assert(p.start(psk, Paw, paw, 1100));
  assert(p.tick(1100, hello, destination, broadcast));
  assert(h.receive(hello, paw, 1100) && !h.pending());

  Rig rig;
  rig.connect();
  Frame captured{};
  Mac originalSource{};
  // The direct handshake's valid old Hello can be spoofed with its original
  // source or with the current peer MAC, as broadcast or unicast.
  captured = hello;
  originalSource = paw;
  const auto connections = rig.h.connections, losses = rig.h.losses;
  const Mac onlineIdentity = rig.h.radio.own;
  for (unsigned i = 0; i < 1000; ++i) {
    rig.h.radio.incoming.push_back({captured, i % 2 ? originalSource : rig.p.radio.own,
                                  (i % 3) == 0});
    assert(rig.p.link->queue(command(i + 100)));
    rig.step(10);
    assert(rig.online() && rig.h.connections == connections && rig.h.losses == losses);
    assert(same(onlineIdentity, rig.h.radio.own));
  }
  assert(rig.h.actions > 800);
  rig.p.reset(rig.now);
  rig.connect();
  std::cout << "PASS: Hello spoof/replay admission, bounded cached responses, online isolation and reconnect\n";
}
void testStatusRefreshLatency() {
  Rig rig;rig.connect();
  const unsigned initial=rig.h.statusRequests;
  while(rig.h.statusRequests==initial)rig.step(5);
  rig.step(10);
  const unsigned before=rig.h.statusRequests;
  // A fresh status is required after command ACK; waiting for the next100ms
  // active poll alone can consume the entire response budget for queued input.
  for(unsigned i=0;i<10;++i)assert(rig.p.link->refreshStatus(rig.now));
  assert(!rig.h.link->refreshStatus(rig.now));
  rig.step(10);
  assert(rig.h.statusRequests==before+1);
}
void testQueuedReceivePacing() {
  Rig r;r.connect();r.step(20);
  r.h.radio.incoming.clear();r.p.radio.incoming.clear();
  const auto before=r.h.actions;
  assert(r.p.link->queue(command(10001))&&r.p.link->queue(command(10002)));
  r.p.link->tick(r.now);r.now+=5;r.p.link->tick(r.now);
  assert(r.h.radio.incoming.size()>=2);
  r.h.link->tick(r.now);r.now+=5;r.h.link->tick(r.now);
  assert(r.h.actions==before+2);
}
void testRecordBudget() {
  uint8_t ecdh[32]{42}, transcript[32]{43};
  Material m;
  assert(deriveMaterial(ecdh, transcript, m));
  Session pawSession, helmetSession;
  assert(pawSession.begin(Paw, m) && helmetSession.begin(Helmet, m));
  Frame f;
  Packet decoded;
  for (uint64_t i = 0; i < RekeyRecords; ++i) {
    assert(pawSession.seal(request(), f));
    assert(get64(f.bytes + 12) == i + 1);
    assert(helmetSession.open(f.bytes, 128, decoded));
  }
  assert(pawSession.needsRekey() && helmetSession.needsRekey());
  assert(!pawSession.seal(request(), f) &&
         !helmetSession.open(f.bytes, 128, decoded));
  pawSession.clear();
  assert(!pawSession.begin(Paw, m));
  ecdh[0]++;
  assert(deriveMaterial(ecdh, transcript, m));
  assert(pawSession.begin(Paw, m));
  assert(pawSession.seal(request(), f) && get64(f.bytes + 12) == 1);
  std::cout << "PASS: 1000000 authenticated AES-GCM records, hard traffic "
               "rekey limit and same-key sequence-reset rejection\n";
}
void testStateCorpus() {
  const uint32_t seeds[] = {0x50524f33, 0x46494e49, 0x52535433, 0xdeadbeef};
  uint64_t transitions = 0, localTicks = 0;
  for (auto seed : seeds) {
    std::mt19937 rng(seed);
    Rig r;
    r.connect();
    r.h.properties = true;
    if (seed == 0xdeadbeef) {
      r.now = UINT32_MAX - 10000;
      r.p.reset(r.now);
      r.h.reset(r.now);
      r.connect();
    }
    uint32_t commandSequence = 100000;
    for (unsigned i = 0; i < 250000; ++i) {
      const unsigned event = rng() % 1024;
      // Frequent replay, corrupt ciphertext, random ordering and bounded
      // bursts.
      if (event < 128 && r.online()) {
        if (r.p.link->queue(command(++commandSequence)))
          r.h.authority[commandSequence] = r.h.radio.own;
      } else if (event < 400 && !r.p.radio.captured.empty()) {
        Frame f = r.p.radio.captured[rng() % r.p.radio.captured.size()];
        if (event < 300)
          f.bytes[rng() % 128] ^= uint8_t(1 + (rng() % 255));
        if (r.h.radio.incoming.size() < RxSlots)
          r.h.radio.incoming.push_back({f, r.p.radio.own, false});
      } else if (event == 400)
        r.p.radio.dropFinish = 1;
      else if (event == 401) {
        r.p.radio.blocked = true;
        r.step(100);
        r.p.radio.blocked = false;
      } else if (event == 402) {
        r.p.radio.deniedChannels = uint16_t(1 << r.p.radio.c);
        r.h.radio.deniedChannels = r.p.radio.deniedChannels;
      } else if (event == 403) {
        r.p.radio.deniedChannels = r.h.radio.deniedChannels = 0;
      } else if (event == 404 && i % 16 == 0)
        r.p.reset(r.now);
      else if (event == 405 && i % 16 == 0)
        r.h.reset(r.now);
      else if (event == 406 && i % 16 == 0) {
        r.p.reset(r.now);
        r.h.reset(r.now);
      } else if (event == 407 && i % 32 == 0) {
        r.p.allowStorage = r.h.allowStorage = (rng() % 2) != 0;
        r.p.link->startPairing(r.now);
        r.h.link->startPairing(r.now);
      } else if (event == 408)
        r.p.link->confirmPairing();
      else if (event == 409)
        r.h.link->confirmPairing();
      else if (event == 410)
        r.p.allowStorage = r.h.allowStorage = true;
      else if (event == 411 && i % 64 == 0)
        r.step(PairingMs + 5);
      else if (event == 412 && i % 64 == 0)
        r.step(SessionLifetimeMs + SessionJitterMs + 5);
      else if (event == 413 && i % 32 == 0) {
        r.p.radio.stuck = true;
        r.step(HandshakeMs + 5);
      }
      if (event == 414 && i % 64 == 0) {
        r.h.secret[0] ^= 1;
        r.h.reset(r.now);
      }
      if (event == 415 && i % 32 == 0) {
        memcpy(r.h.secret, r.p.secret, 32);
        r.h.stored = r.p.stored;
        r.h.reset(r.now);
      }
      r.step(5);
      localTicks++;
      assert(r.p.link->invariant() && r.h.link->invariant());
      assert(r.p.radio.incoming.size() <= RxSlots &&
             r.h.radio.incoming.size() <= RxSlots);
      assert(r.p.link->queued() <= TxSlots + 1 &&
             r.h.link->queued() <= TxSlots + 1);
      ++transitions;
    }
    r.p.radio.blocked = r.h.radio.blocked = false;
    r.p.radio.deniedChannels = r.h.radio.deniedChannels = 0;
    r.p.allowStorage = r.h.allowStorage = true;
    r.p.secret[0] = r.h.secret[0] = 9;
    memset(r.p.secret + 1, 0, 31);
    memset(r.h.secret + 1, 0, 31);
    r.p.stored = r.h.stored = true;
    r.p.reset(r.now);
    r.h.reset(r.now);
    r.connect();
  }
  assert(localTicks == transitions);
  std::cout
      << "PASS: " << transitions
      << " deterministic protocol-event corpus transitions, four retained "
         "seeds; bounded queues and online dispatch invariants\n";
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
  ReleaseGate release;
  assert(!release.ready(100, false)); // Pre-held during reconnect.
  assert(!release.ready(120, true));
  assert(!release.ready(149, true));
  assert(release.ready(150, true));
  release.reset(); // New connection always disarms.
  assert(!release.ready(160, true));
  assert(!release.ready(170, false)); // Bounce resets the interval.
  assert(!release.ready(UINT32_MAX - 10, true));
  assert(!release.ready(10, true));
  assert(release.ready(20, true)); // Rollover-safe timing.
  Indicator led;
  assert(!led.level(0, false, false, 0));
  assert(led.level(0, true, false, 0));
  assert(!led.level(300, true, false, 0));
  assert(led.level(1000, true, true, 123456));
  assert(!led.complete(1000, 123456));
  assert(!led.level(2500, true, true, 123456));
  // Marker, six digit pulses, and all separator gaps total 12,500 ms.
  assert(!led.complete(13499, 123456));
  assert(led.complete(13500, 123456));
  assert(!led.complete(13500, 123457));
  led.level(13505, true, true, 123457);
  assert(!led.complete(13505, 123457));
  led.level(14000, false, false, 0);
  assert(!led.complete(15000, 123457));
  led.level(UINT32_MAX - 1000, true, true, 123456);
  assert(!led.complete(0, 123456));
  assert(led.complete(12000, 123456));
}
} // namespace
int main(int argc,char **argv) {
  std::cout.setf(std::ios::unitbuf);
  assert(Crypto::init());
  if(argc==2 && !strcmp(argv[1],"--service-regressions")) {
    testStatusRefreshLatency();testQueuedReceivePacing();return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--session-only")) {
    testSession();
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--startup-retry")) {
    testRadioStartupConfigureRetry();
    return 0;
  }
  assert(argc==1);
  testVectors();
  testPairing();
  testSasMismatchAndReplacement();
  testInvalidPairingPointRecovery();
  testSession();
  testReceiverHardExpiry();
  testRadioStartupConfigureRetry();
  testRadioConfigureFaultRetries();
  testRecovery();
  testIdentityAndActions();
  testHandshakeAndForwardSecrecy();
  testHelloAdmission();
  testStatusRefreshLatency();
  testQueuedReceivePacing();
  testRecordBudget();
  testStateCorpus();
  std::cout
      << "PASS: wireless pairing, physical confirmations, interruption/storage "
         "failures, AEAD, replay, 52000 fuzz/corruption cases, fresh "
         "sessions/MACs, reboot/loss/channel recovery, quiet keepalives, "
         "bounded rapid actions\n";
  return 0;
}
