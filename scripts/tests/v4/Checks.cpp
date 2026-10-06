#include "Simulator.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
using namespace ProtogenV4;
using namespace V4Test;
namespace {
void cryptoChecks() {
  TestCrypto c(7);
  NativeKem kem(c);
  KemBuffers b;
  assert(kem.execute(Operation::KeyGen, b) == 0);
  uint8_t sk[3168], old[32];
  std::memcpy(sk, b.sk, 3168);
  assert(kem.execute(Operation::Encaps, b) == 0);
  std::memcpy(old, b.ss, 32);
  assert(kem.execute(Operation::Decaps, b) == 0 && equal(old, b.ss, 32));
  b.ct[0] ^= 1;
  assert(kem.execute(Operation::Decaps, b) == 0 &&
         !equal(old, b.ss, 32)); // FIPS implicit rejection.
  std::memset(b.pk, 0xff, 1568);
  assert(kem.execute(Operation::Encaps, b) != 0);
  SlotWorker w(c, kem);
  Ticket ticket;
  Job j;
  j.generation = 1;
  assert(w.submit(j, ticket));
  assert(!w.submit(j, ticket));
  Ticket active;
  assert(w.claim(active));
  w.cancel(1);
  assert(w.phase() == SlotWorker::Phase::Retiring);
  assert(!w.submit(j, ticket));
  w.finish(active, w.execute(active));
  assert(w.phase() == SlotWorker::Phase::Empty);
  Result r;
  assert(!w.poll(r));
  assert(w.submit(j, ticket));
  w.cancel(1);
  assert(w.phase() == SlotWorker::Phase::Empty);
  j.operation = Operation::Encaps;
  j.input = b.pk;
  j.inputSize = 1567;
  assert(!w.submit(j, ticket));
  c.wipe(&b, sizeof(b));
  c.wipe(sk, sizeof(sk));
  c.wipe(old, 32);
  assert(c.wipes > 0);
  uint64_t a = 0, d = 0;
  uint8_t ap[65], dp[65], x[32], y[32];
  assert(c.generateP256(a, ap) && c.generateP256(d, dp) && c.ecdh(a, dp, x) &&
         c.ecdh(d, ap, y) && equal(x, y, 32));
  dp[0] = 0;
  assert(!c.ecdh(a, dp, y));
  c.destroyP256(a);
  c.destroyP256(d);
  assert(!a && !d);
  c.wipe(x, 32);
  c.wipe(y, 32);
}
void storageChecks() {
  TestCrypto c(8);
  uint8_t secret[32], oldSecret[32], hash[32], record[112], old[112], out[32];
  assert(c.random(secret, 32) && c.random(oldSecret, 32) && c.random(hash, 32));
  assert(encodeRecord(c, Paw, secret, hash, record) &&
         encodeRecord(c, Paw, oldSecret, hash, old));
  Random rng(0xe82ad17911ULL);
  for (unsigned n = 0; n < 135983; ++n) {
    uint8_t torn[112];
    std::memcpy(torn, old, 112);
    const unsigned cut = unsigned(rng.next() % 113);
    std::memcpy(torn, record, cut);
    size_t size = 112;
    if (n % 3 == 0)
      size = cut;
    else if (n % 3 == 1)
      torn[rng.next() % 112] ^= uint8_t(1u << (rng.next() % 8));
    auto status = decodeRecord(c, Paw, torn, size, out);
    if (status == PairStatus::PairedV4)
      assert(equal(out, secret, 32) || equal(out, oldSecret, 32));
    else
      assert(zero(out, 32) && (status == PairStatus::RecoveryRequired ||
                               status == PairStatus::Unpaired));
    assert(decodeRecord(c, Helmet, torn, size, out) != PairStatus::PairedV4 &&
           zero(out, 32));
    c.wipe(torn, 112);
  }
  uint8_t legacy[46]{};
  legacy[0] = 2;
  legacy[1] = Paw;
  legacy[2] = 1;
  legacy[3] = 6;
  std::memcpy(legacy + 4, oldSecret, 32);
  write16(legacy + 44, HelmetLink::crc16(legacy, 44));
  assert(decodeRecord(c, Paw, legacy, 46, out) ==
             PairStatus::V3_REPAIR_REQUIRED &&
         zero(out, 32));
  c.wipe(record, 112);
  c.wipe(old, 112);
  c.wipe(secret, 32);
  c.wipe(oldSecret, 32);
  c.wipe(out, 32);
  c.wipe(legacy, 46);
  std::puts("v4 storage interrupted-write/corruption cases: 135983");
}
void fragmentChecks() {
  TestCrypto c(9);
  Exchange x;
  x.mode = Mode::Session;
  x.wire = 0x123456789abcdef0ULL;
  x.paw.bytes[0] = 2;
  x.paw.bytes[5] = 3;
  x.helmet.bytes[0] = 2;
  x.helmet.bytes[5] = 4;
  x.complete = 0x3f;
  for (unsigned i = 0; i < 6; ++i)
    assert(c.random(x.objects[i], objectSize(Object(i))));
  uint8_t key[32];
  assert(c.random(key, 32));
  Random rng(0xa43099813579ULL);
  Reassembly r(c);
  for (unsigned iteration = 0; iteration < 500000; ++iteration) {
    r.clear();
    auto o = iteration % 2 ? Object::Ciphertext : Object::EncapsulationKey;
    const Identity source = objectRole(o) == Paw ? x.paw : x.helmet;
    const uint8_t *auth = iteration % 3 ? key : nullptr;
    if (iteration % 32 == 0) {
      std::array<unsigned, 24> order{};
      for (unsigned i = 0; i < 24; ++i)
        order[i] = i;
      for (unsigned i = 23; i > 0; --i)
        std::swap(order[i], order[rng.next() % (i + 1)]);
      for (unsigned i = 0; i < 24; ++i) {
        Frame f;
        assert(makeFragment(c, x, o, order[i], auth, f));
        const auto status =
            r.accept(f, source, x.mode, x.wire, o, auth, 100 + i);
        assert(status ==
               (i == 23 ? FragmentStatus::Complete : FragmentStatus::Accepted));
        if (i < 23)
          assert(r.accept(f, source, x.mode, x.wire, o, auth, 100 + i) ==
                 FragmentStatus::Accepted);
      }
      assert(equal(r.bytes(), x.objects[unsigned(o)], 1568));
    } else {
      Frame f;
      assert(makeFragment(c, x, o, unsigned(rng.next() % 24), auth, f));
      const uint64_t now = 100;
      assert(r.accept(f, source, x.mode, x.wire, o, auth, now) ==
             FragmentStatus::Accepted);
      switch (iteration % 20) {
      case 0:
        f.bytes[1] = 3;
        break;
      case 1:
        f.bytes[7] = 255;
        break;
      case 2:
        f.bytes[6] = 24;
        break;
      case 3:
        write16(f.bytes + 8, 1567);
        break;
      case 4:
        write16(f.bytes + 8, 1569);
        break;
      case 5:
        f.bytes[12] ^= 1;
        break;
      case 6:
        f.bytes[5] ^= 1;
        break;
      case 7:
        f.bytes[3] ^= 3;
        break;
      case 8:
        f.bytes[20] ^= 2;
        break;
      case 9:
        f.bytes[10] = 255;
        break;
      case 10:
        f.bytes[26] = 1;
        break;
      case 11:
        f.bytes[28] ^= 1;
        if (auth)
          assert(c.hmac(key, f.bytes, 96, f.bytes + 96));
        break;
      case 12:
        f.bytes[96] ^= 1;
        break;
      case 13:
        r.clear();
        f.bytes[12] ^= 1;
        break; // reset/rotation/new generation rejects stale object.
      case 14:
        assert(r.accept(f, source, x.mode, x.wire, o, auth, 1601) ==
               FragmentStatus::Expired);
        continue;
      case 15:
        std::memset(f.bytes, 0xff, 128);
        break;
      case 16:
        for (auto &b : f.bytes)
          b = uint8_t(rng.next());
        break;
      case 17:
        f.bytes[4] = 1;
        break;
      case 18:
        f.bytes[11] = 1;
        break;
      default:
        f.bytes[6] = 255;
        break;
      }
      auto status = r.accept(f, source, x.mode, x.wire, o, auth, now + 1);
      assert(status == FragmentStatus::Rejected ||
             status == FragmentStatus::Conflict);
    }
    assert(sizeof(r) <
           1800); // fixed storage independent of any fragment field.
  }
  r.clear();
  c.wipe(key, 32);
  c.wipe(&x, sizeof(x));
  std::puts("v4 PQ fragment cases: 500000 (seed a43099813579)");
}
void protocolChecks() {
  for (auto object :
       {Object::HelmetOffer, Object::EncapsulationKey, Object::Ciphertext}) {
    Simulation attack;
    attack.radio.tamperObject = int(object);
    assert(attack.paw.core.begin() && attack.helmet.core.begin());
    assert(attack.paw.core.openPairing() && attack.helmet.core.openPairing());
    uint32_t a = 0, b = 0;
    attack.until(
        [&] { return attack.paw.core.sas(a) && attack.helmet.core.sas(b); },
        10000);
    if (attack.paw.core.sas(a) && attack.helmet.core.sas(b))
      assert(a != b);
    assert(!attack.paw.core.online() && !attack.helmet.core.online());
    assert(!attack.paw.store.length && !attack.helmet.store.length);
    attack.time.ms += 60001;
    attack.step();
    assert(TestAccess::clean(attack.paw.core) &&
           TestAccess::clean(attack.helmet.core));
  }
  Simulation s;
  assert(s.pair());
  assert(TestAccess::clean(s.paw.core) && TestAccess::clean(s.helmet.core));
  assert(s.paw.store.length == 112 && s.helmet.store.length == 112);
  s.paw.port.failInstall = true;
  s.paw.core.reset();
  s.helmet.core.reset();
  s.radio.clear();
  assert(s.until([&] { return !s.paw.port.failInstall; }));
  assert(!s.paw.core.online() && TestAccess::clean(s.paw.core));
  assert(s.reconnect());
  s.paw.core.reset();
  s.helmet.core.reset();
  s.radio.clear();
  assert(s.until([&] { return s.paw.core.state() == State::KeyConfirmation; }));
  {
    DerivationWorkspace work;
    uint8_t psk[32], root[32], hash[32];
    Material guessed;
    assert(decodeRecord(s.paw.crypto, Paw, s.paw.store.bytes, 112, psk) ==
           PairStatus::PairedV4);
    const auto n = canonical(s.paw.crypto, TestAccess::exchange(s.paw.core),
                             work.transcript, sizeof(work.transcript));
    assert(n && s.paw.crypto.hash(work.transcript, n, hash));
    assert(hybridRoot(s.paw.crypto, psk, psk, work.transcript, n, root, work) &&
           deriveMaterial(s.paw.crypto, root, hash, guessed));
    assert(!equal(guessed.pawKey, TestAccess::material(s.paw.core).pawKey, 32));
    s.paw.crypto.wipe(psk, 32);
    s.paw.crypto.wipe(root, 32);
    s.paw.crypto.wipe(&guessed, sizeof(guessed));
    s.paw.crypto.wipe(&work, sizeof(work));
  }
  uint8_t publicHashes[6][32];
  for (unsigned i = 0; i < 6; ++i)
    assert(s.paw.crypto.hash(TestAccess::exchange(s.paw.core).objects[i],
                             objectSize(Object(i)), publicHashes[i]));
  assert(s.reconnect());
  s.paw.core.requestRekey();
  s.step();
  s.step();
  assert(s.until([&] { return s.paw.core.state() == State::KeyConfirmation; }));
  for (unsigned i = 0; i < 6; ++i) {
    uint8_t fresh[32];
    assert(s.paw.crypto.hash(TestAccess::exchange(s.paw.core).objects[i],
                             objectSize(Object(i)), fresh));
    assert(!equal(publicHashes[i], fresh, 32));
  }
  assert(s.reconnect());
  {
    Simulation boot;
    boot.paw.store = s.paw.store;
    boot.helmet.store = s.helmet.store;
    assert(boot.paw.core.begin() && boot.helmet.core.begin() &&
           boot.reconnect());
    assert(!equal(boot.paw.port.fingerprint, s.paw.port.fingerprint, 32));
  }
  {
    Simulation wrong;
    wrong.paw.store = s.paw.store;
    wrong.helmet.store = s.helmet.store;
    uint8_t psk[32], th[32]{};
    assert(decodeRecord(wrong.paw.crypto, Paw, wrong.paw.store.bytes, 112,
                        psk) == PairStatus::PairedV4);
    psk[0] ^= 1;
    assert(encodeRecord(wrong.paw.crypto, Paw, psk, th, wrong.paw.store.bytes));
    assert(wrong.paw.core.begin() && wrong.helmet.core.begin());
    assert(!wrong.until(
        [&] { return wrong.paw.core.online() || wrong.helmet.core.online(); },
        3000));
    assert(!wrong.paw.core.telemetry().pqStarted &&
           !wrong.helmet.core.telemetry().pqStarted);
    wrong.paw.crypto.wipe(psk, 32);
  }
  HelmetLink::Packet command{};
  command.kind = HelmetLink::Command;
  command.sequence = 1;
  command.epoch = 1;
  command.ticket = 1;
  command.length = 8;
  command.payload[0] = 1;
  command.payload[4] = HelmetLink::Expression;
  assert(applicationValid(command, Paw));
  while (!s.paw.core.send(command))
    s.step();
  s.step();
  s.step();
  assert(s.helmet.hooks.commands == 1);
  Received oldData{};
  for (const auto &p : s.radio.history)
    if (p.frame.bytes[2] == uint8_t(Kind::Data) && p.frame.bytes[3] == Paw)
      oldData = p;
  auto commands = s.helmet.hooks.commands;
  s.helmet.port.inject(oldData);
  s.step();
  assert(s.helmet.hooks.commands == commands);
  s.paw.core.requestRekey();
  s.step();
  s.step();
  assert(s.reconnect());
  s.helmet.port.inject(oldData);
  s.step();
  assert(s.helmet.hooks.commands == commands);
  const auto oldInstalls = s.paw.port.installs;
  TestAccess::nearLimit(s.paw.core);
  s.step();
  s.step();
  assert(s.until([&] {
    return s.paw.port.installs > oldInstalls && s.paw.core.online() &&
           s.helmet.core.online();
  }));
  // Real lossy/reordered/duplicating radio; transient loss is followed by
  // recovery.
  s.radio.loss = 10;
  s.radio.duplicate = 15;
  s.radio.delay = 15;
  s.workerDelay = 50;
  s.paw.core.channelLost();
  s.helmet.core.channelLost();
  assert(s.reconnect());
  s.radio.loss = s.radio.duplicate = 0;
  s.radio.delay = 5;
  s.workerDelay = 0;
  s.radio.suppressCompletions = true;
  while (!s.paw.core.send(command))
    s.step();
  assert(s.until([&] { return !s.paw.core.online(); }, 110));
  s.radio.suppressCompletions = false;
  assert(s.reconnect());
  // Failure at each expensive worker stage clears all candidate secrets.
  for (auto op : {Operation::KeyGen, Operation::Encaps, Operation::Decaps}) {
    s.paw.core.reset();
    s.helmet.core.reset();
    s.radio.clear();
    Node &n = op == Operation::Encaps ? s.paw : s.helmet;
    assert(s.until([&] {
      return TestAccess::pending(n.core) && TestAccess::operation(n.core) == op;
    }));
    n.worker.fail = true;
    s.step();
    assert(!n.core.online() && TestAccess::clean(n.core));
    assert(s.reconnect());
  }
  // Reset during pending KEM, delayed/stale completion, and no application
  // authority.
  for (auto op : {Operation::KeyGen, Operation::Encaps, Operation::Decaps}) {
    s.paw.core.reset();
    s.helmet.core.reset();
    s.radio.clear();
    Node &n = op == Operation::Encaps ? s.paw : s.helmet;
    assert(s.until([&] {
      return TestAccess::pending(n.core) && TestAccess::operation(n.core) == op;
    }));
    n.core.reset();
    assert(TestAccess::clean(n.core));
    n.worker.stale = true;
    s.step();
    s.step();
    assert(n.core.telemetry().staleResults && !n.core.online());
    assert(s.reconnect());
  }
  // Authentication failures at Finished/Commit cannot install keys.
  for (auto kind : {Kind::PawFinished, Kind::HelmetFinished, Kind::Commit,
                    Kind::CommitAck}) {
    s.paw.core.reset();
    s.helmet.core.reset();
    s.radio.clear();
    s.radio.corruptKind = int(kind);
    s.radio.corruptOnce = true;
    assert(s.until([&] { return !s.radio.corruptOnce; }));
    s.step();
    const auto &receiver =
        kind == Kind::PawFinished || kind == Kind::Commit ? s.helmet : s.paw;
    assert(!receiver.core.online() && TestAccess::clean(receiver.core));
    s.step();
    assert(s.paw.core.telemetry().finishedFailures +
               s.helmet.core.telemetry().finishedFailures +
               s.paw.core.telemetry().commitFailures +
               s.helmet.core.telemetry().commitFailures >
           0);
    assert(s.reconnect());
  }
  // Storage failures cannot turn confirmation into operational authority.
  for (int cut : {0, 1, 16, 48, 80, 111}) {
    Simulation fault;
    fault.paw.store.cut = cut;
    assert(fault.paw.core.begin() && fault.helmet.core.begin());
    assert(fault.paw.core.openPairing() && fault.helmet.core.openPairing());
    uint32_t a = 0, b = 0;
    assert(fault.until(
        [&] { return fault.paw.core.sas(a) && fault.helmet.core.sas(b); }));
    assert(a == b);
    assert(fault.paw.core.confirmPairing() &&
           fault.helmet.core.confirmPairing());
    fault.step();
    fault.step();
    assert(fault.paw.core.state() == State::RecoveryRequired &&
           !fault.paw.core.online() && TestAccess::clean(fault.paw.core));
  }
  // A one-million event trace runs the actual core, with real KEM/ECDH paths.
  Random rng(0x713eddcca019ULL);
  const uint64_t target = s.transitions + 1000000;
  uint64_t fallback = 0;
  while (s.transitions < target) {
    const auto choice = rng.next();
    if (choice % 4096 == 0)
      s.paw.core.requestRekey();
    if (choice % 4096 == 1) {
      s.paw.core.reset();
      s.helmet.core.reset();
      s.radio.clear();
    }
    if (choice % 4096 == 2) {
      s.paw.worker.stale = true;
      s.helmet.worker.stale = true;
    }
    if (choice % 4096 == 3) {
      s.paw.core.channelLost();
      s.helmet.core.identityChanged();
    }
    if (choice % 4096 == 4) {
      s.radio.corruptKind = int(Kind::Fragment);
      s.radio.corruptOnce = true;
    }
    if (choice % 4096 == 5)
      s.workerDelay = 50;
    if (choice % 4096 == 6)
      s.workerDelay = 0;
    if (choice % 4096 == 7)
      s.radio.loss = 10;
    if (choice % 4096 == 8)
      s.radio.loss = 0;
    if (choice % 4096 == 9)
      s.radio.duplicate = 15;
    if (choice % 4096 == 10)
      s.radio.duplicate = 0;
    if (choice % 4096 == 11) {
      s.radio.enabled = false;
    }
    if (choice % 4096 == 12) {
      s.radio.enabled = true;
    }
    if (choice % 64 == 0) {
      Received random;
      for (auto &b : random.frame.bytes)
        b = uint8_t(rng.next());
      random.source = s.paw.port.own;
      s.helmet.port.inject(random);
    }
    if (choice % 64 == 1) {
      Received replay = s.radio.history[rng.next() % 64];
      s.helmet.port.inject(replay);
    }
    if (choice % 64 == 2) {
      Received old = oldData;
      old.frame.bytes[1] = 3;
      s.helmet.port.inject(old);
    }
    if (s.paw.core.online() && s.helmet.core.online() && choice % 128 == 0)
      s.paw.core.send(command);
    if (!s.helmet.core.online())
      ++fallback;
    s.step();
    assert(s.helmet.hooks.connected == s.helmet.core.online());
    if (!s.paw.core.online())
      assert(s.paw.hooks.connected == false);
  }
  s.radio.enabled = true;
  s.radio.loss = s.radio.duplicate = 0;
  s.radio.delay = 5;
  s.workerDelay = 0;
  s.paw.core.reset();
  s.helmet.core.reset();
  s.radio.clear();
  assert(s.reconnect());
  assert(fallback && s.helmet.hooks.localTicks == s.transitions / 2 &&
         s.paw.port.installs > 30);
  std::printf("v4 protocol events: 1000000 (seed 713eddcca019); successful "
              "installations=%llu; local-service steps=%llu\n",
              (unsigned long long)s.paw.port.installs,
              (unsigned long long)s.helmet.hooks.localTicks);
}
} // namespace
void checks() {
  cryptoChecks();
  storageChecks();
  fragmentChecks();
  protocolChecks();
}
