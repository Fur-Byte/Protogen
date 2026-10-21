#include "SessionHandshake.h"
#include <string.h>
namespace Wireless {
namespace {
bool nonzero(const uint8_t *bytes, size_t n) {
  uint8_t value = 0;
  while (n--) value |= *bytes++;
  return value != 0;
}
bool structuralKey(const Frame &f, const Mac &source) {
  if (f.bytes[4] != 4 || !Crypto::equal(f.bytes + 85, source.bytes, 6) ||
      f.bytes[91] != HandshakeRevision || !nonzero(f.bytes + 69, 16))
    return false;
  for (unsigned i = 92; i < ControlTagAt; ++i)
    if (f.bytes[i]) return false;
  return true;
}
bool transcriptHash(const Frame &challenge, const Frame &proof,
                    const Frame &response, uint8_t out[32]) {
  uint8_t canonical[ControlTagAt * 3]{};
  memcpy(canonical, challenge.bytes, ControlTagAt);
  memcpy(canonical + ControlTagAt, proof.bytes, ControlTagAt);
  memcpy(canonical + ControlTagAt * 2, response.bytes, ControlTagAt);
  const bool okay = Crypto::hash(canonical, sizeof(canonical), out);
  Crypto::wipe(canonical, sizeof(canonical));
  return okay;
}
bool proofTag(const Frame &challenge, Frame &proof, const uint8_t key[32]) {
  uint8_t canonical[ControlTagAt * 2]{};
  memcpy(canonical, challenge.bytes, ControlTagAt);
  memcpy(canonical + ControlTagAt, proof.bytes, ControlTagAt);
  const bool okay = Crypto::hmac(key, canonical, sizeof(canonical),
                                proof.bytes + ControlTagAt);
  Crypto::wipe(canonical, sizeof(canonical));
  return okay;
}
} // namespace

void Handshake::clearCandidate() {
  agreement.clear();
  Crypto::wipe(transcript, sizeof(transcript));
  Crypto::wipe(&material, sizeof(material));
  Crypto::wipe(&hello, sizeof(hello));
  Crypto::wipe(&challenge, sizeof(challenge));
  Crypto::wipe(&response, sizeof(response));
  Crypto::wipe(&cached, sizeof(cached));
  peer = Mac{};
}
void Handshake::retireAdmission() {
  Crypto::wipe(helloAuth, sizeof(helloAuth));
  Crypto::wipe(challengeAuth, sizeof(challengeAuth));
  Crypto::wipe(proofAuth, sizeof(proofAuth));
  Crypto::wipe(responseAuth, sizeof(responseAuth));
  Crypto::wipe(admissionNonce, sizeof(admissionNonce));
  Crypto::wipe(replies, sizeof(replies));
  replyRead = replyWrite = replyCount = 0;
}
void Handshake::clear() {
  clearCandidate();
  retireAdmission();
  phase = Phase::Idle;
}
void Handshake::fail(uint32_t, bool confirmation) {
  increment(confirmation ? budget->diagnostics.finishedFailures
                         : budget->diagnostics.handshakeFailures);
  clear();
  phase = Phase::Failed;
}
bool Handshake::generate(Frame &f, Type type, uint32_t now, bool prepaid) {
  if (!prepaid && !budget->spend(budget->ecc, now)) return false;
  header(f, type, role);
  if (!agreement.generate(f.bytes + 4) ||
      !Crypto::random(f.bytes + 69, 16) || !nonzero(f.bytes + 69, 16)) {
    agreement.clear();
    Crypto::wipe(&f, sizeof(f));
    return false;
  }
  memcpy(f.bytes + 85, local.bytes, 6);
  f.bytes[91] = HandshakeRevision;
  return true;
}
void Handshake::challengePrefix(const uint8_t requestHash[32], Frame &out) const {
  header(out, SessionChallenge, Helmet);
  memcpy(out.bytes + 4, requestHash, 32);
  memcpy(out.bytes + 36, admissionNonce, 16);
  memcpy(out.bytes + 52, local.bytes, 6);
  out.bytes[58] = HandshakeRevision;
}
bool Handshake::queueChallenge(const uint8_t hash[32], const Mac &source) {
  for (unsigned i = 0; i < replyCount; ++i) {
    const auto &entry = replies[(replyRead + i) % ChallengeReplies];
    if (same(entry.source, source) &&
        Crypto::equal(entry.requestHash, hash, 32)) return true;
  }
  if (replyCount == ChallengeReplies) {
    increment(budget->diagnostics.rateDrops);
    return false; // Preserve FIFO entries; the new request may retry.
  }
  auto &entry = replies[replyWrite];
  memcpy(entry.requestHash, hash, 32);
  entry.source = source;
  replyWrite = (replyWrite + 1) % ChallengeReplies;
  ++replyCount;
  return true;
}
void Handshake::removeChallenge(const uint8_t hash[32], const Mac &source) {
  const unsigned count = replyCount;
  for (unsigned i = 0; i < count; ++i) {
    const ChallengeReply entry = replies[replyRead];
    replies[replyRead] = ChallengeReply{};
    replyRead = (replyRead + 1) % ChallengeReplies;
    --replyCount;
    if (!same(entry.source, source) ||
        !Crypto::equal(entry.requestHash, hash, 32)) {
      replies[replyWrite] = entry;
      replyWrite = (replyWrite + 1) % ChallengeReplies;
      ++replyCount;
    }
  }
}
bool Handshake::start(const uint8_t psk[32], uint8_t r, const Mac &own,
                      uint32_t now) {
  clear();
  role = r;
  local = own;
  started = nextSend = nextChallengeSend = now;
  if ((r != Paw && r != Helmet) || !unicast(own) ||
      !budget->spend(budget->creation, now) ||
      !Crypto::hkdf(psk, 32, nullptr, 0,
                    "protogen/v3/handshake/r2/request", helloAuth, 32) ||
      !Crypto::hkdf(psk, 32, nullptr, 0,
                    "protogen/v3/handshake/r2/challenge", challengeAuth, 32) ||
      !Crypto::hkdf(psk, 32, nullptr, 0,
                    "protogen/v3/handshake/r2/proof", proofAuth, 32) ||
      !Crypto::hkdf(psk, 32, nullptr, 0,
                    "protogen/v3/handshake/r2/response", responseAuth, 32)) {
    clear(); phase = Phase::Failed; return false;
  }
  if (role == Helmet) {
    if (!Crypto::random(admissionNonce, sizeof(admissionNonce)) ||
        !nonzero(admissionNonce, sizeof(admissionNonce))) {
      clear(); phase = Phase::Failed; return false;
    }
  } else if (!generate(hello, SessionRequest, now) ||
             !Crypto::hmac(helloAuth, hello.bytes, ControlTagAt,
                           hello.bytes + ControlTagAt)) {
    clear(); phase = Phase::Failed; return false;
  }
  phase = Phase::Rendezvous;
  return true;
}
bool Handshake::authenticate(const Frame &f, const uint8_t key[32],
                             const uint8_t *data, size_t n, uint32_t,
                             bool confirmation) {
  // The owner bounds service batches. A bad tag consumes no scarce ECC/start
  // credit and cannot lock out a later legitimate authentication.
  uint8_t tag[32]{};
  const bool okay = Crypto::hmac(key, data, n, tag) &&
                    Crypto::equal(tag, f.bytes + ControlTagAt, 32);
  Crypto::wipe(tag, sizeof(tag));
  if (!okay)
    increment(confirmation ? budget->diagnostics.finishedFailures
                           : budget->diagnostics.handshakeFailures);
  return okay;
}
bool Handshake::derive(const Frame &remote, uint32_t now, bool prepaid) {
  if (!prepaid && !budget->spend(budget->ecc, now)) return false;
  phase = Phase::AuthenticatedEcdh;
  uint8_t shared[32]{};
  const bool okay = agreement.derive(remote.bytes + 4, shared) &&
                    deriveMaterial(shared, transcript, material);
  agreement.clear();
  Crypto::wipe(shared, sizeof(shared));
  if (!okay) { fail(now); return false; }
  // Admission authentication stays only until establishment/clear so a live
  // receiver proof can replace a stale pending candidate. Link wipes it on use.
  return true;
}
bool Handshake::finished(Type type) {
  header(cached, type, role);
  memcpy(cached.bytes + 4, transcript, 32);
  memcpy(cached.bytes + 36, material.id, 8);
  return Crypto::hmac(type == SessionCommit ? material.pawCommit
                      : role == Paw ? material.pawFinished : material.helmetFinished,
                      cached.bytes, ControlTagAt, cached.bytes + ControlTagAt);
}
bool Handshake::tick(uint32_t now, Frame &f, Mac &destination,
                     bool &broadcast) {
  if (phase == Phase::Idle || phase == Phase::Failed || ready()) return false;
  if (uint32_t(now - started) >= HandshakeMs) {
    fail(now, pending()); return false;
  }
  // Accepted exchanges always get their due response/confirmation first.
  if (phase != Phase::Rendezvous && due(now, nextSend)) {
    nextSend = now + HandshakeRetryMs;
    destination = peer; broadcast = false; f = cached; return true;
  }
  if (role == Paw) {
    if (phase != Phase::Rendezvous || !due(now, nextSend)) return false;
    nextSend = now + HandshakeRetryMs;
    destination = Mac{}; broadcast = true; f = hello; return true;
  }
  if (!replyCount || !due(now, nextChallengeSend)) return false;
  const ChallengeReply entry = replies[replyRead];
  replies[replyRead] = ChallengeReply{};
  replyRead = (replyRead + 1) % ChallengeReplies;
  --replyCount;
  challengePrefix(entry.requestHash, f);
  if (!Crypto::hmac(challengeAuth, f.bytes, ControlTagAt,
                    f.bytes + ControlTagAt)) {
    fail(now); return false;
  }
  nextChallengeSend = now + ChallengeReplyMs;
  destination = entry.source; broadcast = false;
  return true;
}
bool Handshake::receive(const Frame &f, const Mac &source, uint32_t now) {
  if (phase == Phase::Idle || phase == Phase::Failed || ready() ||
      uint32_t(now - started) >= HandshakeMs ||
      !valid(f.bytes, PacketBytes, role == Paw ? Helmet : Paw) ||
      !unicast(source) || same(source, local)) return false;
  const uint8_t type = f.bytes[2];
  if (role == Helmet && type == SessionRequest) {
    if (!structuralKey(f, source) ||
        !authenticate(f, helloAuth, f.bytes, ControlTagAt, now)) return false;
    uint8_t hash[32]{};
    const bool okay = Crypto::hash(f.bytes, ControlTagAt, hash) &&
                      queueChallenge(hash, source);
    Crypto::wipe(hash, sizeof(hash));
    return okay;
  }
  if (role == Paw && type == SessionChallenge) {
    if (phase != Phase::Rendezvous)
      return same(source, peer) && Crypto::equal(&f, &challenge, sizeof(f));
    if (f.bytes[58] != HandshakeRevision ||
        !Crypto::equal(f.bytes + 52, source.bytes, 6) ||
        !nonzero(f.bytes + 36, 16)) return false;
    for (unsigned i = 59; i < ControlTagAt; ++i)
      if (f.bytes[i]) return false;
    uint8_t hash[32]{};
    const bool matches = Crypto::hash(hello.bytes, ControlTagAt, hash) &&
                         Crypto::equal(hash, f.bytes + 4, 32);
    Crypto::wipe(hash, sizeof(hash));
    if (!matches ||
        !authenticate(f, challengeAuth, f.bytes, ControlTagAt, now)) return false;
    challenge = f; peer = source;
    hello.bytes[2] = SessionProof;
    if (!proofTag(challenge, hello, proofAuth)) { fail(now); return false; }
    cached = hello;
    phase = Phase::AwaitResponse;
    started = nextSend = now;
    return true;
  }
  if (role == Helmet && type == SessionProof) {
    if ((phase == Phase::AwaitPawFinished || phase == Phase::AwaitCommit) &&
        same(source, peer) && Crypto::equal(&f, &hello, sizeof(f))) return true;
    if (!structuralKey(f, source)) return false;
    Frame request = f, offeredChallenge;
    request.bytes[2] = SessionRequest;
    uint8_t hash[32]{};
    if (!Crypto::hash(request.bytes, ControlTagAt, hash)) return false;
    challengePrefix(hash, offeredChallenge);
    uint8_t canonical[ControlTagAt * 2]{};
    memcpy(canonical, offeredChallenge.bytes, ControlTagAt);
    memcpy(canonical + ControlTagAt, f.bytes, ControlTagAt);
    const bool authenticated =
        authenticate(f, proofAuth, canonical, sizeof(canonical), now);
    Crypto::wipe(canonical, sizeof(canonical));
    if (!authenticated || !budget->spend(budget->ecc, now, 2)) {
      Crypto::wipe(hash, sizeof(hash)); return false;
    }
    uint8_t nextNonce[16]{};
    if (!Crypto::random(nextNonce, sizeof(nextNonce)) ||
        !nonzero(nextNonce, sizeof(nextNonce))) {
      Crypto::wipe(nextNonce, sizeof(nextNonce));
      Crypto::wipe(hash, sizeof(hash)); return false;
    }
    // Consume freshness before any heavy work. Never debit half an ECC pair
    // or displace the previous candidate on insufficient credits.
    memcpy(admissionNonce, nextNonce, sizeof(admissionNonce));
    Crypto::wipe(nextNonce, sizeof(nextNonce));
    removeChallenge(hash, source);
    Crypto::wipe(hash, sizeof(hash));
    clearCandidate();
    hello = f; challenge = offeredChallenge; peer = source;
    if (!generate(response, SessionResponse, now, true) ||
        !transcriptHash(challenge, hello, response, transcript) ||
        !Crypto::hmac(responseAuth, transcript, sizeof(transcript),
                      response.bytes + ControlTagAt) ||
        !derive(hello, now, true)) {
      if (!failed()) fail(now);
      return false;
    }
    cached = response;
    phase = Phase::AwaitPawFinished;
    started = nextSend = now;
    return true;
  }
  if (role == Paw && type == SessionResponse) {
    if (phase != Phase::AwaitResponse)
      return same(source, peer) && Crypto::equal(&f, &response, sizeof(f));
    if (!same(source, peer) || !structuralKey(f, source)) return false;
    uint8_t hash[32]{};
    const bool okay = transcriptHash(challenge, hello, f, hash) &&
                      authenticate(f, responseAuth, hash, sizeof(hash), now);
    if (!okay || !budget->spend(budget->ecc, now)) {
      Crypto::wipe(hash, sizeof(hash)); return false;
    }
    memcpy(transcript, hash, sizeof(transcript));
    Crypto::wipe(hash, sizeof(hash));
    response = f;
    if (!derive(response, now, true) || !finished(SessionFinish)) {
      if (!failed()) fail(now);
      return false;
    }
    phase = Phase::AwaitHelmetFinished;
    started = nextSend = now;
    return true;
  }
  const bool expected =
      (role == Helmet && type == SessionFinish &&
       (phase == Phase::AwaitPawFinished || phase == Phase::AwaitCommit)) ||
      (role == Paw && type == SessionFinish &&
       (phase == Phase::AwaitHelmetFinished || phase == Phase::CommitReady)) ||
      (role == Helmet && type == SessionCommit && phase == Phase::AwaitCommit);
  if (!expected || !same(source, peer) ||
      !Crypto::equal(f.bytes + 4, transcript, 32) ||
      !Crypto::equal(f.bytes + 36, material.id, 8)) return false;
  for (unsigned i = 44; i < ControlTagAt; ++i)
    if (f.bytes[i]) return false;
  if (!authenticate(f, type == SessionCommit ? material.pawCommit
                       : role == Paw ? material.helmetFinished : material.pawFinished,
                    f.bytes, ControlTagAt, now, true)) return false;
  if (role == Helmet && type == SessionCommit) {
    phase = Phase::Complete; retireAdmission(); return true;
  }
  if (!finished(role == Paw ? SessionCommit : SessionFinish)) {
    fail(now, true); return false;
  }
  phase = role == Paw ? Phase::CommitReady : Phase::AwaitCommit;
  nextSend = now;
  return true;
}
} // namespace Wireless
