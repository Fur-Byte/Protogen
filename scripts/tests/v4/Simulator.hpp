#pragma once
// Test-only deterministic entropy, radio and storage. Not production adapters.
#include "Core.hpp"
#include "HostCrypto.hpp"
#include "Worker.hpp"
#include "psa/crypto.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
namespace ProtogenV4 {
struct TestAccess {
  static unsigned flags(const Core &c) { return c.confirmations_; }
  static unsigned proofs(const Core &c) { return c.proofs_; }
  static uint64_t generation(const Core &c) { return c.generation_; }
  static bool pending(const Core &c) { return c.jobPending_; }
  static Operation operation(const Core &c) { return c.pendingOperation_; }
  static bool clean(const Core &c) {
    return !c.privateHandle_ && zero(c.privateKem_, 3168) &&
           zero(c.ecdh_, 32) && zero(c.kemSecret_, 32) &&
           zero(c.candidatePair_, 32) &&
           zero(reinterpret_cast<const uint8_t *>(&c.result_.buffers),
                sizeof(c.result_.buffers)) &&
           zero(reinterpret_cast<const uint8_t *>(&c.material_),
                sizeof(c.material_)) &&
           zero(reinterpret_cast<const uint8_t *>(&c.workspace_),
                sizeof(c.workspace_)) &&
           zero(c.preAuth_, 32) && zero(c.preFragment_, 32) &&
           zero(c.reassembly_.bytes(), 1568);
  }
  static void nearLimit(Core &c) {
    c.traffic_.records_ = RecordLimit - 1;
    c.traffic_.sequence_ = SequenceLimit - 1025;
  }
  static void nearLimit(Traffic &c) {
    c.records_ = RecordLimit - 1;
    c.sequence_ = SequenceLimit - 1;
  }
  static bool confirmation(Core &c, Kind k, Frame &f) {
    return c.control(k, f);
  }
  static const Exchange &exchange(const Core &c) { return c.exchange_; }
  static const Material &material(const Core &c) { return c.material_; }
  static const uint8_t *hash(const Core &c) { return c.transcriptHash_; }
  static void vectorContext(Core &c, const Exchange &x, const Material &m,
                            const uint8_t hash[32]) {
    c.exchange_ = x;
    c.material_ = m;
    c.own_ = c.role_ == Paw ? x.paw : x.helmet;
    std::memcpy(c.transcriptHash_, hash, 32);
  }
};
} // namespace ProtogenV4
namespace V4Test {
using namespace ProtogenV4;
struct Random {
  uint64_t state;
  explicit Random(uint64_t seed) : state(seed) {}
  uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
};
struct Time final : Clock {
  uint64_t ms = 100;
  uint64_t nowMs() const override { return ms; }
};
struct TestCrypto final : HostCrypto {
  uint8_t seed[32]{};
  uint64_t counter = 0;
  explicit TestCrypto(uint8_t n) {
    seed[0] = n;
    assert(init());
  }
  bool random(void *p, size_t n) override {
    auto *out = static_cast<uint8_t *>(p);
    uint8_t input[8], block[32];
    while (n) {
      write64(input, ++counter);
      if (!hmac(seed, input, 8, block))
        return false;
      const size_t take = n < 32 ? n : 32;
      std::memcpy(out, block, take);
      out += take;
      n -= take;
    }
    wipe(block, 32);
    return true;
  }
  bool generateP256(uint64_t &handle, uint8_t pub[65]) override {
    destroyP256(handle);
    uint8_t scalar[32]{};
    if (!random(scalar, 32))
      return false;
    // Reduce magnitude only for deterministic test fixtures, never production.
    scalar[0] &= 0x7f;
    scalar[31] |= 1;
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&a, 256);
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&a, PSA_ALG_ECDH);
    psa_key_id_t id = 0;
    size_t n = 0;
    const bool ok = psa_import_key(&a, scalar, 32, &id) == PSA_SUCCESS &&
                    psa_export_public_key(id, pub, 65, &n) == PSA_SUCCESS &&
                    n == 65;
    wipe(scalar, 32);
    psa_reset_key_attributes(&a);
    if (!ok) {
      psa_destroy_key(id);
      wipe(pub, 65);
      return false;
    }
    handle = id;
    return true;
  }
};
struct Store final : Storage {
  uint8_t bytes[PairRecordBytes]{};
  size_t length = 0;
  int cut = -1;
  bool readFail = false;
  Load read(uint8_t *out, size_t capacity, size_t &n) override {
    n = 0;
    if (readFail || length > capacity)
      return Load::Error;
    if (!length)
      return Load::Empty;
    std::memcpy(out, bytes, length);
    n = length;
    return Load::Found;
  }
  bool write(const uint8_t *p, size_t n) override {
    if (n != PairRecordBytes)
      return false;
    const size_t written = cut < 0 ? n
                           : static_cast<size_t>(cut) < n
                               ? static_cast<size_t>(cut)
                               : n;
    std::memcpy(bytes, p, written);
    length = written;
    return cut < 0;
  }
};
struct Hook final : Hooks {
  uint64_t commands = 0, ups = 0, downs = 0, localTicks = 0;
  bool connected = false;
  void connection(bool up) override {
    connected = up;
    if (up)
      ++ups;
    else
      ++downs;
  }
  void packet(const HelmetLink::Packet &) override {
    assert(connected);
    ++commands;
  }
};
struct Radio;
struct Port final : Transport {
  Radio &radio;
  unsigned index;
  Core *core = nullptr;
  Identity own{};
  uint8_t channel = 6;
  bool encrypted = false;
  bool failInstall = false;
  Received queue[8]{};
  unsigned head = 0, count = 0;
  uint8_t fingerprint[32]{};
  uint8_t installationHistory[512][32]{};
  uint64_t installs = 0;
  Port(Radio &r, unsigned i) : radio(r), index(i) {}
  bool rendezvous(const Identity &id, uint8_t ch) override {
    own = id;
    channel = ch;
    encrypted = false;
    head = count = 0;
    return true;
  }
  bool install(uint8_t, const Material &, uint8_t) override;
  bool send(const Frame &, const Identity *, Ticket) override;
  bool receive(Received &p) override {
    if (!count)
      return false;
    p = queue[head];
    head = (head + 1) % 8;
    --count;
    return true;
  }
  void inject(const Received &p) {
    if (count < 8) {
      queue[(head + count) % 8] = p;
      ++count;
    }
  }
};
struct Radio {
  struct Pending {
    bool used = false;
    unsigned sender = 0;
    Received packet{};
    Identity destination{};
    bool broadcast = false, drop = false;
    Ticket ticket{};
    uint64_t due = 0;
  };
  Time &time;
  Random random{0x491837acbb01ULL};
  Port *ports[2]{};
  Pending queue[32]{};
  Received history[64]{};
  unsigned written = 0;
  uint64_t sends = 0, drops = 0;
  unsigned loss = 0, duplicate = 0, delay = 5;
  bool enabled = true, trace = false, suppressCompletions = false;
  int corruptKind = -1;
  int tamperObject = -1;
  bool corruptOnce = false;
  explicit Radio(Time &t) : time(t) {}
  bool send(unsigned sender, const Frame &f, const Identity *destination,
            Ticket ticket) {
    for (auto &p : queue)
      if (!p.used) {
        p.used = true;
        p.sender = sender;
        p.packet = {f, ports[sender]->own, ports[sender]->encrypted};
        if (tamperObject >= 0 && f.bytes[2] == uint8_t(Kind::Fragment) &&
            f.bytes[5] == tamperObject) {
          // Test an active initial-pairing attacker that recomputes the
          // untrusted complete-object commitment, not just random corruption.
          uint8_t modified[1568]{};
          const auto object = Object(tamperObject);
          const auto &exchange = TestAccess::exchange(*ports[sender]->core);
          std::memcpy(modified, exchange.objects[unsigned(object)],
                      objectSize(object));
          modified[1] ^= 1;
          if (f.bytes[6] == 0)
            p.packet.frame.bytes[29] ^= 1;
          TestCrypto hash(201);
          assert(hash.hash(modified, objectSize(object),
                           p.packet.frame.bytes + 96));
          hash.wipe(modified, sizeof(modified));
        }
        p.broadcast = !destination;
        if (destination)
          p.destination = *destination;
        p.ticket = ticket;
        p.drop = !enabled || (loss && random.next() % 100 < loss);
        p.due = time.ms + delay + (delay > 5 ? random.next() % delay : 0);
        const auto receiverState = ports[1 - sender]->core->state();
        const bool confirmationReady =
            (receiverState == State::KeyConfirmation ||
             receiverState == State::Committing) &&
            TestAccess::exchange(*ports[1 - sender]->core).wire ==
                read64(f.bytes + 12);
        if (corruptOnce && int(f.bytes[2]) == corruptKind &&
            (corruptKind == int(Kind::Fragment) || confirmationReady)) {
          p.packet.frame.bytes[40] ^= 1;
          corruptOnce = false;
        }
        history[written++ % 64] = p.packet;
        ++sends;
        return true;
      }
    return false;
  }
  void deliver() {
    for (auto &p : queue)
      if (p.used && p.due <= time.ms) {
        auto *from = ports[p.sender];
        auto *to = ports[1 - p.sender];
        const bool delivered = !p.drop && from->channel == to->channel &&
                               (p.broadcast || same(p.destination, to->own)) &&
                               (!p.packet.nativeEncrypted || to->encrypted);
        if (delivered) {
          to->inject(p.packet);
          if (duplicate && random.next() % 100 < duplicate)
            to->inject(p.packet);
        } else
          ++drops;
        p.used = false;
        if (!suppressCompletions)
          from->core->completion(p.ticket, true);
      }
  }
  void clear() {
    for (auto &p : queue)
      p.used = false;
    for (auto *p : ports)
      p->head = p->count = 0;
  }
};
inline bool Port::send(const Frame &f, const Identity *d, Ticket t) {
  return radio.send(index, f, d, t);
}
inline bool Port::install(uint8_t role, const Material &m, uint8_t ch) {
  if (failInstall) {
    failInstall = false;
    return false;
  }
  uint8_t next[32]{};
  TestCrypto c(200);
  assert(c.hash(m.pawKey, 32, next));
  if (installs)
    assert(!equal(next, fingerprint, 32));
  assert(installs < 512);
  for (uint64_t i = 0; i < installs; ++i)
    assert(!equal(next, installationHistory[i], 32));
  std::memcpy(installationHistory[installs], next, 32);
  std::memcpy(fingerprint, next, 32);
  ++installs;
  own = role == Paw ? m.pawIdentity : m.helmetIdentity;
  channel = ch;
  encrypted = true;
  head = count = 0;
  return true;
}
struct TestWorker : SlotWorker {
  bool stale = false, fail = false;
  using SlotWorker::SlotWorker;
  bool poll(Result &r) override {
    if (stale) {
      stale = false;
      r.ticket = {UINT64_MAX, UINT64_MAX};
      r.status = 0;
      std::memset(r.buffers.sk, 0xa5, sizeof(r.buffers.sk));
      return true;
    }
    if (!SlotWorker::poll(r))
      return false;
    if (fail) {
      r.status = -1;
      fail = false;
    }
    return true;
  }
};
struct Node {
  TestCrypto crypto;
  Store store;
  Hook hooks;
  NativeKem kem;
  TestWorker worker;
  Port port;
  Core core;
  Node(uint8_t role, Time &time, Radio &r, unsigned i)
      : crypto(uint8_t(i + 1)), kem(crypto), worker(crypto, kem), port(r, i),
        core(role, crypto, time, store, port, worker, hooks) {
    port.core = &core;
    r.ports[i] = &port;
  }
};
struct Simulation {
  Time time;
  Radio radio;
  Node paw, helmet;
  uint64_t transitions = 0;
  unsigned workerDelay = 0;
  explicit Simulation()
      : radio(time), paw(Paw, time, radio, 0), helmet(Helmet, time, radio, 1) {}
  void step() {
    time.ms += 5;
    radio.deliver();
    if (!workerDelay || time.ms % workerDelay == 0) {
      paw.worker.advance();
      helmet.worker.advance();
    }
    auto oldP = paw.core.state(), oldH = helmet.core.state();
    paw.core.tick();
    helmet.core.tick();
    if (radio.trace &&
        (oldP != paw.core.state() || oldH != helmet.core.state()))
      std::fprintf(stderr, "t=%llu states=%u,%u\n", (unsigned long long)time.ms,
                   unsigned(paw.core.state()), unsigned(helmet.core.state()));
    transitions += 2;
    ++paw.hooks.localTicks;
    ++helmet.hooks.localTicks;
    assert(paw.core.invariant() && helmet.core.invariant());
    assert(paw.port.count <= 8 && helmet.port.count <= 8);
  }
  template <class Predicate>
  bool until(Predicate pred, unsigned steps = 24000) {
    for (unsigned i = 0; i < steps; ++i) {
      if (pred())
        return true;
      step();
    }
    return pred();
  }
  bool pair() {
    assert(paw.core.begin() && helmet.core.begin());
    assert(paw.core.openPairing() && helmet.core.openPairing());
    uint32_t a = 0, b = 0;
    if (!until([&] { return paw.core.sas(a) && helmet.core.sas(b); }, 10000))
      return false;
    assert(a == b);
    assert(paw.core.confirmPairing() && helmet.core.confirmPairing());
    return until([&] { return paw.core.online() && helmet.core.online(); });
  }
  bool reconnect() {
    return until([&] { return paw.core.online() && helmet.core.online(); });
  }
};
} // namespace V4Test
