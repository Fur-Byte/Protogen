#include "Core.hpp"
#include <cassert>
#include <cstring>
namespace ProtogenV4 {
namespace {
constexpr uint8_t Classical = 1, PQ = 2, Hybrid = 4, OwnFinished = 8,
                  PeerFinished = 16, OwnCommit = 32, PeerCommit = 64;
constexpr uint8_t Required = 127;
bool address(const Identity &a) {
  return !zero(a.bytes, 6) && !(a.bytes[0] & 1);
}
unsigned index(Object o) { return unsigned(o); }
} // namespace
Core::Core(uint8_t r, Crypto &c, Clock &time, Storage &s, Transport &t,
           Worker &w, Hooks &h)
    : crypto(c), clock(time), storage(s), transport(t), worker(w), hooks(h),
      role_(r), reassembly_(c), traffic_(c, r) {}
Core::~Core() {
  clearCandidate();
  traffic_.clear();
  crypto.wipe(psk_, 32);
  crypto.wipe(recovery_, 32);
}
bool Core::pairing() const {
  return state_ >= State::PairingDiscovery &&
         state_ <= State::PairingStoredGrace;
}
bool Core::candidate() const {
  return pairing() ||
         (state_ >= State::Rendezvous && state_ <= State::Committing);
}
void Core::transition(State next) {
  state_ = next;
  assert(invariant());
}
bool Core::invariant() const {
  if (online())
    return proofs_ == Required && traffic_.active() && !privateHandle_ &&
           !jobPending_ && zero(privateKem_, 3168) && zero(ecdh_, 32) &&
           zero(kemSecret_, 32) &&
           zero(&result_.buffers.pk[0], sizeof(KemBuffers));
  return !traffic_.active();
}
void Core::clearCandidate() {
  if (jobPending_) {
    worker.cancel(generation_);
    count(telemetry_.cancellations);
  }
  jobPending_ = false;
  crypto.destroyP256(privateHandle_);
  crypto.wipe(privateKem_, 3168);
  crypto.wipe(ecdh_, 32);
  crypto.wipe(kemSecret_, 32);
  crypto.wipe(nonce_, 16);
  crypto.wipe(transcriptHash_, 32);
  crypto.wipe(candidatePair_, 32);
  crypto.wipe(preAuth_, 32);
  crypto.wipe(preFragment_, 32);
  crypto.wipe(&material_, sizeof(material_));
  crypto.wipe(&exchange_, sizeof(exchange_));
  crypto.wipe(&workspace_, sizeof(workspace_));
  crypto.wipe(&result_, sizeof(result_));
  reassembly_.clear();
  proofs_ = confirmations_ = txObject_ = txFragment_ = 0;
  sas_ = 0;
  job_ = {};
  transmission_ = {};
  transmitting_ = false;
  controlAt_ = nextSend_ = 0;
  peer_ = {};
  if (generation_ != UINT64_MAX)
    ++generation_;
  else
    state_ = State::RecoveryRequired;
}
bool Core::freshIdentity() {
  Identity next;
  for (unsigned i = 0; i < 4; ++i) {
    if (!crypto.random(next.bytes, 6))
      return false;
    next.bytes[0] = (next.bytes[0] & 0xfc) | 2;
    next.bytes[5] = uint8_t((next.bytes[5] & 0xfe) | (role_ == Paw));
    if (!same(next, own_)) {
      own_ = next;
      return true;
    }
  }
  return false;
}
bool Core::begin() {
  if (role_ != Paw && role_ != Helmet)
    return false;
  reset();
  crypto.wipe(psk_, 32);
  crypto.wipe(recovery_, 32);
  paired_ = false;
  uint8_t bytes[PairRecordBytes]{};
  size_t n = 0;
  const auto loaded = storage.read(bytes, sizeof(bytes), n);
  provenance_ = loaded == Load::Error ? PairStatus::RecoveryRequired
                : loaded == Load::Empty
                    ? PairStatus::Unpaired
                    : decodeRecord(crypto, role_, bytes, n, psk_);
  crypto.wipe(bytes, sizeof(bytes));
  paired_ = provenance_ == PairStatus::PairedV4;
  if (provenance_ == PairStatus::V3_REPAIR_REQUIRED)
    count(telemetry_.v3RepairRequired);
  if (paired_) {
    recover();
    return candidate();
  }
  transition(provenance_ == PairStatus::V3_REPAIR_REQUIRED
                 ? State::LegacyRepairRequired
             : provenance_ == PairStatus::RecoveryRequired
                 ? State::RecoveryRequired
                 : State::Unpaired);
  return provenance_ != PairStatus::RecoveryRequired;
}
void Core::reset() {
  const bool active = online();
  traffic_.clear();
  clearCandidate();
  if (active)
    hooks.connection(false);
  transition(paired_ ? State::PairedOffline
             : provenance_ == PairStatus::RecoveryRequired
                 ? State::RecoveryRequired
             : provenance_ == PairStatus::V3_REPAIR_REQUIRED
                 ? State::LegacyRepairRequired
                 : State::Unpaired);
}
bool Core::openPairing() {
  if (role_ != Paw && role_ != Helmet)
    return false;
  const auto now = clock.nowMs();
  if (now > UINT64_MAX - 60000 || generation_ == UINT64_MAX)
    return false;
  reset();
  pairDeadline_ = now + 60000;
  count(telemetry_.pairingAttempts);
  if (!beginCandidate(Mode::Pairing)) {
    fail();
    return false;
  }
  return true;
}
uint8_t Core::rendezvousChannel() {
  if (!paired_)
    return 6;
  uint8_t digest[32]{};
  constexpr char label[] = "protogen/v4/rendezvous/channels";
  const uint8_t *key =
      !zero(recovery_, 32) && clock.nowMs() < fastUntil_ ? recovery_ : psk_;
  if (!crypto.hmac(key, label, sizeof(label), digest))
    return 0;
  const uint8_t permutations[6][3] = {{1, 6, 11}, {1, 11, 6}, {6, 1, 11},
                                      {6, 11, 1}, {11, 1, 6}, {11, 6, 1}};
  const uint8_t result =
      permutations[digest[0] % 6]
                  [(clock.nowMs() / (role_ == Paw ? 400 : 2400)) % 3];
  crypto.wipe(digest, 32);
  return result;
}
bool Core::beginCandidate(Mode mode) {
  const auto now = clock.nowMs();
  if (now > UINT64_MAX - 1020000 || generation_ == UINT64_MAX ||
      !creation_.take(now)) {
    count(telemetry_.rateDrops);
    return false;
  }
  clearCandidate();
  if (generation_ == UINT64_MAX)
    return false;
  exchange_.mode = mode;
  deadline_ = mode == Mode::Pairing ? pairDeadline_ : now + 8000;
  identityAt_ = now + 30000;
  if (!freshIdentity())
    return false;
  currentChannel_ = mode == Mode::Pairing ? 6 : rendezvousChannel();
  if (!currentChannel_ || !transport.rendezvous(own_, currentChannel_))
    return false;
  transition(mode == Mode::Pairing ? State::PairingDiscovery
                                   : State::Rendezvous);
  if (mode == Mode::Session &&
      (!paired_ ||
       !crypto.hkdf(psk_, 32, nullptr, 0, "protogen/v4/handshake/auth",
                    preAuth_, 32) ||
       !crypto.hkdf(psk_, 32, nullptr, 0, "protogen/v4/handshake/fragments",
                    preFragment_, 32)))
    return false;
  if (role_ == Paw) {
    if (!crypto.random(&exchange_.wire, 8) || !exchange_.wire)
      return false;
    exchange_.paw = own_;
    if (!makeOffer())
      return false;
  } else
    exchange_.helmet = own_;
  count(telemetry_.hybridStarted);
  return true;
}
void Core::recover() {
  count(telemetry_.recoveries);
  reset();
  if (!paired_ || generation_ == UINT64_MAX)
    return;
  if (!beginCandidate(Mode::Session)) {
    clearCandidate();
    transition(State::PairedOffline);
    blockedUntil_ = clock.nowMs() + 250;
  }
}
void Core::fail(bool timeout) {
  if (timeout)
    count(telemetry_.handshakeTimeout);
  const bool wasPair = pairing();
  if (wasPair) {
    count(telemetry_.pairingFailures);
    if (timeout)
      count(telemetry_.pairingTimeout);
  }
  reset();
  blockedUntil_ =
      clock.nowMs() <= UINT64_MAX - 250 ? clock.nowMs() + 250 : UINT64_MAX;
}
bool Core::makeOffer() {
  if (!ecc_.take(clock.nowMs())) {
    count(telemetry_.rateDrops);
    return false;
  }
  const Object object = role_ == Paw ? Object::PawOffer : Object::HelmetOffer;
  auto *offer = exchange_.objects[index(object)];
  if (!crypto.generateP256(privateHandle_, offer) ||
      !crypto.random(nonce_, 16) || zero(nonce_, 16))
    return false;
  if (role_ == Helmet &&
      !crypto.hash(exchange_.objects[index(Object::EncapsulationKey)], 1568,
                   offer + 97))
    return false;
  if (!nonceCommit(crypto, exchange_.mode, exchange_.wire, role_, own_, offer,
                   nonce_, offer + 65))
    return false;
  exchange_.complete |= uint8_t(1u << index(object));
  return true;
}
const uint8_t *Core::fragmentKey(Object object) const {
  if (exchange_.mode == Mode::Pairing)
    return nullptr;
  return object == Object::PawOffer || object == Object::HelmetOffer
             ? preAuth_
             : preFragment_;
}
bool Core::schedule(Operation op) {
  if (jobPending_)
    return false;
  Budget &budget = op == Operation::KeyGen   ? keygen_
                   : op == Operation::Encaps ? encaps_
                                             : decaps_;
  if (!budget.take(clock.nowMs())) {
    count(telemetry_.rateDrops);
    return false;
  }
  Job request;
  request.operation = op;
  request.generation = generation_;
  if (op == Operation::Encaps) {
    request.input = exchange_.objects[index(Object::EncapsulationKey)];
    request.inputSize = 1568;
  }
  if (op == Operation::Decaps) {
    request.input = privateKem_;
    request.inputSize = 3168;
    request.ciphertext = exchange_.objects[index(Object::Ciphertext)];
    request.ciphertextSize = 1568;
  }
  if (!worker.submit(request, job_))
    return false;
  pendingOperation_ = op;
  jobPending_ = true;
  count(telemetry_.pqStarted);
  if (op == Operation::Decaps)
    crypto.wipe(privateKem_, 3168);
  transition(pairing() ? State::PairingCryptoPending : State::PQCryptoPending);
  return true;
}
void Core::result() {
  if (!worker.poll(result_))
    return;
  if (!jobPending_ || !same(result_.ticket, job_) ||
      result_.ticket.generation != generation_ ||
      result_.operation != pendingOperation_) {
    count(telemetry_.staleResults);
    crypto.wipe(&result_, sizeof(result_));
    return;
  }
  jobPending_ = false;
  bool ok = result_.status == 0;
  const auto op = result_.operation;
  if (ok && op == Operation::KeyGen) {
    ok = !zero(result_.buffers.pk, 1568) && !zero(result_.buffers.sk, 3168);
    if (ok) {
      std::memcpy(exchange_.objects[index(Object::EncapsulationKey)],
                  result_.buffers.pk, 1568);
      std::memcpy(privateKem_, result_.buffers.sk, 3168);
      exchange_.complete |= 1u << index(Object::EncapsulationKey);
      ok = makeOffer();
    }
  } else if (ok) {
    ok = !zero(result_.buffers.ss, 32);
    if (op == Operation::Encaps) {
      ok = ok && !zero(result_.buffers.ct, 1568);
      if (ok) {
        std::memcpy(exchange_.objects[index(Object::Ciphertext)],
                    result_.buffers.ct, 1568);
        exchange_.complete |= 1u << index(Object::Ciphertext);
      }
    }
    if (ok) {
      std::memcpy(kemSecret_, result_.buffers.ss, 32);
      proofs_ |= PQ;
      const Object object =
          role_ == Paw ? Object::PawNonce : Object::HelmetNonce;
      std::memcpy(exchange_.objects[index(object)], nonce_, 16);
      exchange_.complete |= 1u << index(object);
      crypto.wipe(nonce_, 16);
    }
  }
  crypto.wipe(&result_, sizeof(result_));
  if (!ok) {
    count(op == Operation::KeyGen   ? telemetry_.keygenFailures
          : op == Operation::Encaps ? telemetry_.encapsFailures
                                    : telemetry_.decapsFailures);
    fail();
    return;
  }
  count(telemetry_.pqCompleted);
  transition(pairing() ? State::PairingPQTransfer : State::PQKeyTransfer);
  derive();
}
bool Core::acceptedObject(Object object) {
  auto *data = exchange_.objects[index(object)];
  if (object == Object::PawOffer || object == Object::HelmetOffer) {
    if (data[0] != 4 || (object == Object::PawOffer && !zero(data + 97, 32)))
      return false;
    if (role_ == Helmet) {
      uint8_t hash[32]{};
      Frame header;
      objectHeader(header, exchange_.mode, exchange_.wire, peer_, object);
      uint8_t input[157];
      std::memcpy(input, header.bytes, 28);
      std::memcpy(input + 28, data, 129);
      const bool hashed = crypto.hash(input, sizeof(input), hash);
      crypto.wipe(input, sizeof(input));
      if (!hashed)
        return false;
      for (unsigned i = 0; i < 4; ++i)
        if (clock.nowMs() >= recentAt_[i] &&
            clock.nowMs() - recentAt_[i] < 16000 &&
            equal(hash, recent_[i], 32)) {
          crypto.wipe(hash, 32);
          count(telemetry_.replayRejects);
          return false;
        }
      std::memcpy(recent_[recentWrite_ % 4], hash, 32);
      recentAt_[recentWrite_ % 4] = clock.nowMs();
      ++recentWrite_;
      crypto.wipe(hash, 32);
      transition(pairing() ? State::PairingPQTransfer
                           : State::AuthenticatedHello);
      return schedule(Operation::KeyGen);
    }
    transition(pairing() ? State::PairingPQTransfer : State::PQKeyTransfer);
    return true;
  }
  if (object == Object::EncapsulationKey) {
    uint8_t hash[32]{};
    const bool valid =
        crypto.hash(data, 1568, hash) &&
        equal(hash, exchange_.objects[index(Object::HelmetOffer)] + 97, 32);
    crypto.wipe(hash, 32);
    if (!valid)
      return false;
  }
  if (object == Object::EncapsulationKey || object == Object::Ciphertext) {
    if (!ecc_.take(clock.nowMs())) {
      count(telemetry_.rateDrops);
      return false;
    }
    const auto *pub = exchange_.objects[index(role_ == Paw ? Object::HelmetOffer
                                                           : Object::PawOffer)];
    const bool valid =
        crypto.ecdh(privateHandle_, pub, ecdh_) && !zero(ecdh_, 32);
    crypto.destroyP256(privateHandle_);
    if (!valid)
      return false;
    proofs_ |= Classical;
    return schedule(role_ == Paw ? Operation::Encaps : Operation::Decaps);
  }
  uint8_t hash[32]{};
  const auto *offer = exchange_.objects[index(role_ == Paw ? Object::HelmetOffer
                                                           : Object::PawOffer)];
  const bool valid =
      nonceCommit(crypto, exchange_.mode, exchange_.wire,
                  role_ == Paw ? Helmet : Paw, peer_, offer, data, hash) &&
      equal(hash, offer + 65, 32);
  crypto.wipe(hash, 32);
  if (valid)
    derive();
  return valid;
}
void Core::derive() {
  if (exchange_.complete != 0x3f ||
      (proofs_ & (Classical | PQ)) != (Classical | PQ) || (proofs_ & Hybrid) ||
      jobPending_)
    return;
  const bool pair = pairing();
  transition(pair ? State::PairingDerivation : State::HybridDerivation);
  uint8_t root[32]{}, sasBytes[32]{};
  const size_t length = canonical(crypto, exchange_, workspace_.transcript,
                                  sizeof(workspace_.transcript));
  bool ok = derivation_.take(clock.nowMs()) && length &&
            crypto.hash(workspace_.transcript, length, transcriptHash_) &&
            hybridRoot(crypto, ecdh_, kemSecret_, workspace_.transcript, length,
                       root, workspace_);
  if (ok && pair)
    ok = crypto.hkdf(root, 32, transcriptHash_, 32,
                     "protogen/v4/pairing/secret", candidatePair_, 32) &&
         crypto.hkdf(root, 32, transcriptHash_, 32, "protogen/v4/pairing/sas",
                     sasBytes, 32) &&
         crypto.hkdf(root, 32, transcriptHash_, 32, "protogen/v4/pairing/id",
                     material_.id, 8) &&
         crypto.hkdf(root, 32, transcriptHash_, 32,
                     "protogen/v4/pairing/confirm/paw", material_.pawFinished,
                     32) &&
         crypto.hkdf(root, 32, transcriptHash_, 32,
                     "protogen/v4/pairing/confirm/helmet",
                     material_.helmetFinished, 32) &&
         !zero(candidatePair_, 32) && !zero(material_.id, 8);
  else if (ok)
    ok = deriveMaterial(crypto, root, transcriptHash_, material_);
  sas_ = (uint32_t(sasBytes[0]) | (uint32_t(sasBytes[1]) << 8) |
          (uint32_t(sasBytes[2]) << 16)) %
         1000000;
  crypto.wipe(root, 32);
  crypto.wipe(sasBytes, 32);
  crypto.wipe(ecdh_, 32);
  crypto.wipe(kemSecret_, 32);
  crypto.wipe(privateKem_, 3168);
  crypto.destroyP256(privateHandle_);
  crypto.wipe(&workspace_, sizeof(workspace_));
  if (!ok) {
    count(telemetry_.hybridFailures);
    fail();
    return;
  }
  proofs_ |= Hybrid;
  transition(pair ? State::PairingConfirmation : State::KeyConfirmation);
}
bool Core::sas(uint32_t &value) const {
  if (state_ != State::PairingConfirmation)
    return false;
  value = sas_;
  return true;
}
bool Core::confirmPairing() {
  if (state_ != State::PairingConfirmation)
    return false;
  confirmations_ |= 1;
  if (confirmations_ == 3 && !storePair()) {
    fail();
    return false;
  }
  return true;
}
bool Core::storePair() {
  if (state_ != State::PairingConfirmation || confirmations_ != 3 ||
      (proofs_ & 7) != 7)
    return false;
  uint8_t bytes[PairRecordBytes]{}, check[PairRecordBytes]{};
  size_t n = 0;
  const bool ok =
      encodeRecord(crypto, role_, candidatePair_, transcriptHash_, bytes) &&
      storage.write(bytes, sizeof(bytes)) &&
      storage.read(check, sizeof(check), n) == Load::Found &&
      n == sizeof(check) && equal(bytes, check, n);
  crypto.wipe(bytes, sizeof(bytes));
  crypto.wipe(check, sizeof(check));
  if (!ok) {
    paired_ = false;
    provenance_ = PairStatus::RecoveryRequired;
    crypto.wipe(psk_, 32);
    return false;
  }
  crypto.wipe(psk_, 32);
  std::memcpy(psk_, candidatePair_, 32);
  crypto.wipe(candidatePair_, 32);
  sas_ = 0;
  paired_ = true;
  provenance_ = PairStatus::PairedV4;
  count(telemetry_.hybridCompleted);
  graceAt_ = clock.nowMs() + 2000;
  transition(State::PairingStoredGrace);
  return true;
}
bool Core::control(Kind kind, Frame &f) {
  f = Frame{};
  auto *b = f.bytes;
  b[0] = 0x70;
  b[1] = Version;
  b[2] = uint8_t(kind);
  b[3] = role_;
  b[4] = uint8_t(exchange_.mode);
  b[5] = 0xff;
  const uint8_t n = kind == Kind::PairConfirm ? 41 : 40;
  write16(b + 8, n);
  b[10] = n;
  write64(b + 12, exchange_.wire);
  std::memcpy(b + 20, own_.bytes, 6);
  std::memcpy(b + 28, transcriptHash_, 32);
  std::memcpy(b + 60, material_.id, 8);
  if (kind == Kind::PairConfirm)
    b[68] = 1;
  const uint8_t *key =
      kind == Kind::PawFinished || (kind == Kind::PairConfirm && role_ == Paw)
          ? material_.pawFinished
      : kind == Kind::HelmetFinished || kind == Kind::PairConfirm
          ? material_.helmetFinished
      : kind == Kind::Commit ? material_.commit
                             : material_.commitAck;
  return !zero(key, 32) && crypto.hmac(key, b, 96, b + 96);
}
bool Core::verifyControl(const Frame &f, Kind kind) {
  const auto *b = f.bytes;
  const unsigned n = kind == Kind::PairConfirm ? 41 : 40;
  if (b[0] != 0x70 || b[1] != Version || b[2] != uint8_t(kind) ||
      b[3] != (role_ == Paw ? Helmet : Paw) ||
      b[4] != uint8_t(exchange_.mode) || b[5] != 0xff || b[6] || b[7] ||
      read16(b + 8) != n || b[10] != n || b[11] || !zero(b + 26, 2) ||
      read64(b + 12) != exchange_.wire || !equal(b + 20, peer_.bytes, 6) ||
      !equal(b + 28, transcriptHash_, 32) || !equal(b + 60, material_.id, 8) ||
      (kind == Kind::PairConfirm && b[68] != 1) || !zero(b + 28 + n, 68 - n))
    return false;
  const auto *key = kind == Kind::PawFinished ||
                            (kind == Kind::PairConfirm && role_ == Helmet)
                        ? material_.pawFinished
                    : kind == Kind::HelmetFinished || kind == Kind::PairConfirm
                        ? material_.helmetFinished
                    : kind == Kind::Commit ? material_.commit
                                           : material_.commitAck;
  uint8_t tag[32]{};
  const bool ok =
      !zero(key, 32) && crypto.hmac(key, b, 96, tag) && equal(tag, b + 96, 32);
  crypto.wipe(tag, 32);
  return ok;
}
void Core::receive(const Received &packet) {
  const auto &f = packet.frame;
  const auto *b = f.bytes;
  if (b[1] != Version) {
    if (b[1] == 3)
      count(telemetry_.downgradeAttempts);
    return;
  }
  if (b[0] != 0x70 || b[3] != (role_ == Paw ? Helmet : Paw) ||
      !address(packet.source))
    return;
  if (online()) {
    if (!packet.nativeEncrypted || !same(packet.source, peer_) ||
        (b[2] != uint8_t(Kind::Data) && b[2] != uint8_t(Kind::Renew)))
      return;
    HelmetLink::Packet p{};
    bool app = false, renew = false;
    if (!traffic_.open(f, p, app, renew)) {
      count(telemetry_.replayRejects);
      return;
    }
    lastRx_ = clock.nowMs();
    if (renew) {
      count(telemetry_.rekeys);
      recover();
      return;
    }
    if (app)
      hooks.packet(p);
    return;
  }
  if (!candidate() || packet.nativeEncrypted)
    return;
  if (b[2] == uint8_t(Kind::Fragment)) {
    if (b[5] >= ObjectCount) {
      count(telemetry_.fragmentRejects);
      return;
    }
    const auto object = Object(b[5]);
    const unsigned bit = 1u << index(object);
    if (objectRole(object) == role_) {
      count(telemetry_.fragmentRejects);
      return;
    }
    const bool first =
        role_ == Helmet && !exchange_.wire && object == Object::PawOffer;
    const uint64_t wire = first ? read64(b + 12) : exchange_.wire;
    if (!structuralFragment(f, packet.source, exchange_.mode, wire, object) ||
        (!zero(peer_.bytes, 6) && !same(packet.source, peer_))) {
      count(telemetry_.fragmentRejects);
      return;
    }
    const bool expected =
        object == Object::PawOffer || object == Object::HelmetOffer
            ? !(exchange_.complete & bit)
        : object == Object::EncapsulationKey
            ? (exchange_.complete & (1u << index(Object::HelmetOffer)))
        : object == Object::Ciphertext
            ? (exchange_.complete & (1u << index(Object::EncapsulationKey)))
            : (exchange_.complete & (1u << index(Object::Ciphertext)));
    if (!expected && (exchange_.complete & bit) == 0) {
      count(telemetry_.fragmentRejects);
      return;
    }
    if (!authenticationFailures_.available(clock.nowMs()) ||
        !authentication_.take(clock.nowMs())) {
      count(telemetry_.rateDrops);
      return;
    }
    if (exchange_.complete & bit) {
      // Authenticate even completed-object duplicates; a conflicting duplicate
      // invalidates this exchange rather than modifying or rescheduling it.
      Frame original;
      if (!makeFragment(crypto, exchange_, object, b[6], fragmentKey(object),
                        original) ||
          !equal(original.bytes, b, sizeof(Frame))) {
        count(telemetry_.conflictingFragments);
        authenticationFailures_.take(clock.nowMs());
        fail();
      }
      return;
    }
    if (!reassembly_.active() && !fragments_.take(clock.nowMs())) {
      count(telemetry_.rateDrops);
      return;
    }
    const auto status =
        reassembly_.accept(f, packet.source, exchange_.mode, wire, object,
                           fragmentKey(object), clock.nowMs());
    if (status == FragmentStatus::Rejected) {
      authenticationFailures_.take(clock.nowMs());
      count(telemetry_.fragmentRejects);
      if (!first && !pairing())
        fail();
      return;
    }
    if (status == FragmentStatus::Conflict ||
        status == FragmentStatus::Expired) {
      count(telemetry_.conflictingFragments);
      fail();
      return;
    }
    if (first) {
      exchange_.wire = wire;
      peer_ = packet.source;
      exchange_.paw = peer_;
    }
    if (zero(peer_.bytes, 6)) {
      peer_ = packet.source;
      if (role_ == Paw)
        exchange_.helmet = peer_;
    }
    if (status != FragmentStatus::Complete)
      return;
    std::memcpy(exchange_.objects[index(object)], reassembly_.bytes(),
                objectSize(object));
    reassembly_.clear();
    exchange_.complete |= uint8_t(bit);
    if (!acceptedObject(object))
      fail();
    return;
  }
  if (!same(packet.source, peer_) || !(proofs_ & Hybrid))
    return;
  const auto kind = Kind(b[2]);
  bool expected = false;
  if (kind == Kind::PairConfirm)
    expected = state_ == State::PairingConfirmation ||
               state_ == State::PairingStoredGrace;
  else if (!pairing())
    expected = role_ == Helmet
                   ? ((kind == Kind::PawFinished &&
                       (state_ == State::KeyConfirmation ||
                        state_ == State::Committing)) ||
                      (kind == Kind::Commit && state_ == State::Committing))
                   : ((kind == Kind::HelmetFinished &&
                       (state_ == State::KeyConfirmation ||
                        state_ == State::Committing)) ||
                      (kind == Kind::CommitAck && state_ == State::Committing));
  if (!expected || !authenticationFailures_.available(clock.nowMs()) ||
      !authentication_.take(clock.nowMs()))
    return;
  if (!verifyControl(f, kind)) {
    authenticationFailures_.take(clock.nowMs());
    count(kind == Kind::Commit || kind == Kind::CommitAck
              ? telemetry_.commitFailures
              : telemetry_.finishedFailures);
    fail();
    return;
  }
  if (kind == Kind::PairConfirm) {
    confirmations_ |= 2;
    if (state_ == State::PairingConfirmation && confirmations_ == 3 &&
        !storePair())
      fail();
    return;
  }
  if (kind == Kind::PawFinished || kind == Kind::HelmetFinished) {
    proofs_ |= PeerFinished;
    transition(State::Committing);
  } else {
    proofs_ |= PeerCommit;
    if (role_ == Paw)
      install();
  }
}
bool Core::install() {
  if (state_ != State::Committing || proofs_ != Required || privateHandle_ ||
      jobPending_ || !zero(ecdh_, 32) || !zero(kemSecret_, 32) ||
      !zero(privateKem_, 3168))
    return false;
  // Reject invalid/reused traffic material before altering native radio state.
  if (!traffic_.begin(material_) ||
      !transport.install(role_, material_, currentChannel_)) {
    fail();
    return false;
  }
  std::memcpy(recovery_, material_.recovery, 32);
  transmitting_ = false;
  transmission_ = {};
  own_ = role_ == Paw ? material_.pawIdentity : material_.helmetIdentity;
  peer_ = role_ == Paw ? material_.helmetIdentity : material_.pawIdentity;
  const auto now = clock.nowMs();
  uint32_t jitter = 0;
  if (!crypto.random(&jitter, sizeof(jitter))) {
    fail();
    return false;
  }
  renewAt_ = now + 900000 + jitter % 120001;
  lastRx_ = now;
  keepaliveAt_ = now + 3000 + jitter % 2001;
  fastUntil_ = now + 15000;
  crypto.wipe(&material_, sizeof(material_));
  crypto.wipe(&exchange_, sizeof(exchange_));
  crypto.wipe(transcriptHash_, 32);
  crypto.wipe(preAuth_, 32);
  crypto.wipe(preFragment_, 32);
  reassembly_.clear();
  sas_ = 0;
  transition(State::Online);
  count(telemetry_.hybridCompleted);
  hooks.connection(true);
  return true;
}
void Core::transmit() {
  const auto now = clock.nowMs();
  if (transmitting_ || now < nextSend_ || serial_ == UINT64_MAX)
    return;
  Frame frame;
  Kind kind = Kind::Fragment;
  if (online()) {
    if (state_ == State::Rekeying) {
      kind = Kind::Renew;
      if (!traffic_.seal(nullptr, true, frame)) {
        recover();
        return;
      }
    } else {
      if (now < keepaliveAt_)
        return;
      if (!traffic_.seal(nullptr, false, frame)) {
        recover();
        return;
      }
      kind = Kind::Data;
      uint32_t jitter = 0;
      if (!crypto.random(&jitter, 4)) {
        recover();
        return;
      }
      keepaliveAt_ = now + 3000 + jitter % 2001;
    }
  } else if (candidate()) {
    if (state_ == State::PairingStoredGrace && now < controlAt_)
      return;
    if (now >= controlAt_ &&
        (pairing() || role_ == Paw || (proofs_ & PeerFinished)) &&
        (state_ == State::PairingStoredGrace ||
         (state_ == State::PairingConfirmation && (confirmations_ & 1)) ||
         state_ == State::KeyConfirmation || state_ == State::Committing)) {
      if (pairing())
        kind = Kind::PairConfirm;
      else if (role_ == Paw)
        kind =
            state_ == State::KeyConfirmation ? Kind::PawFinished : Kind::Commit;
      else {
        if (!(proofs_ & PeerFinished))
          return;
        kind = proofs_ & PeerCommit ? Kind::CommitAck : Kind::HelmetFinished;
      }
      if (!control(kind, frame)) {
        fail();
        return;
      }
      controlAt_ = now + 250;
    } else {
      bool found = false;
      for (unsigned i = 0; i < ObjectCount; ++i) {
        const auto object = Object(txObject_);
        // A later peer object proves receipt of these earlier public objects.
        // Nonce reveals remain eligible until peer Finished proves full T.
        const bool acknowledged =
            object == Object::PawOffer
                ? (exchange_.complete & (1u << index(Object::HelmetOffer))) != 0
            : object == Object::HelmetOffer ||
                    object == Object::EncapsulationKey
                ? (exchange_.complete & (1u << index(Object::Ciphertext))) != 0
            : object == Object::Ciphertext
                ? (exchange_.complete & (1u << index(Object::HelmetNonce))) != 0
                : (proofs_ & PeerFinished) != 0;
        if (objectRole(object) == role_ &&
            (exchange_.complete & (1u << txObject_)) && !acknowledged) {
          found = true;
          break;
        }
        txObject_ = uint8_t((txObject_ + 1) % ObjectCount);
        txFragment_ = 0;
      }
      if (!found)
        return;
      const auto object = Object(txObject_);
      if (!makeFragment(crypto, exchange_, object, txFragment_,
                        fragmentKey(object), frame)) {
        fail();
        return;
      }
      if (++txFragment_ == fragments(object)) {
        txFragment_ = 0;
        txObject_ = uint8_t((txObject_ + 1) % ObjectCount);
      }
    }
  } else
    return;
  transmission_ = {generation_, ++serial_};
  transmittedKind_ = kind;
  transmitting_ = transport.send(frame, zero(peer_.bytes, 6) ? nullptr : &peer_,
                                 transmission_);
  transmitDeadline_ = now + 500;
  // Stay below the peer's bounded authentication budget, including duplicate
  // retransmissions; faster periodic sends can repeatedly drop the same index.
  nextSend_ = now + 10;
}
void Core::completion(Ticket token, bool success) {
  if (!transmitting_ || !same(token, transmission_) ||
      token.generation != generation_)
    return;
  transmitting_ = false;
  if (!success)
    return; // Bounded retransmission/handshake deadline; no key installation on
            // failure.
  if (transmittedKind_ == Kind::Renew) {
    count(telemetry_.rekeys);
    recover();
    return;
  }
  if (transmittedKind_ == Kind::PawFinished ||
      transmittedKind_ == Kind::HelmetFinished) {
    proofs_ |= OwnFinished;
    install();
  }
  if (transmittedKind_ == Kind::Commit || transmittedKind_ == Kind::CommitAck) {
    proofs_ |= OwnCommit;
    install();
  }
}
bool Core::send(const HelmetLink::Packet &packet) {
  if (!online() || state_ == State::Rekeying || transmitting_ ||
      traffic_.needsRekey() || serial_ == UINT64_MAX)
    return false;
  Frame frame;
  if (!traffic_.seal(&packet, false, frame))
    return false;
  transmission_ = {generation_, ++serial_};
  transmittedKind_ = Kind::Data;
  transmitting_ = transport.send(frame, &peer_, transmission_);
  transmitDeadline_ = clock.nowMs() + 500;
  return transmitting_;
}
void Core::requestRekey() {
  if (state_ == State::Online)
    transition(State::Rekeying);
}
void Core::identityChanged() {
  if (pairing())
    fail();
  else
    recover();
}
void Core::channelLost() {
  if (pairing())
    fail();
  else
    recover();
}
void Core::tick() {
  const auto now = clock.nowMs();
  if (now < lastNow_ || now > UINT64_MAX - 1020000) {
    reset();
    transition(State::RecoveryRequired);
    return;
  }
  lastNow_ = now;
  if (transmitting_ && now >= transmitDeadline_) {
    if (pairing())
      fail();
    else
      recover();
    return;
  }
  if (now >= fastUntil_)
    crypto.wipe(recovery_, 32);
  if (online() && (now - lastRx_ >= 12000)) {
    recover();
    return;
  }
  if (online() && (now >= renewAt_ || traffic_.needsRekey()))
    requestRekey();
  if (state_ == State::PairingStoredGrace && now >= graceAt_) {
    recover();
    return;
  }
  if (candidate() && state_ != State::PairingStoredGrace && now >= deadline_) {
    fail(true);
    return;
  }
  if (reassembly_.expired(now)) {
    count(telemetry_.objectTimeout);
    fail(true);
    return;
  }
  if (!online() && !pairing() && paired_ && now >= blockedUntil_ &&
      state_ == State::PairedOffline) {
    recover();
    return;
  }
  if (candidate() && !pairing()) {
    if (now >= identityAt_) {
      recover();
      return;
    }
    if (zero(peer_.bytes, 6) && !transmitting_) {
      const auto channel = rendezvousChannel();
      if (channel && channel != currentChannel_) {
        if (!transport.rendezvous(own_, channel)) {
          fail();
          return;
        }
        currentChannel_ = channel;
      }
    }
  }
  result();
  Received packet;
  for (unsigned i = 0; i < 4 && transport.receive(packet); ++i)
    receive(packet);
  transmit();
  assert(invariant());
}
} // namespace ProtogenV4
