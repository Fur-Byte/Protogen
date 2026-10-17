#include "SessionHandshake.h"
#include <string.h>
namespace Wireless {
namespace {
bool structuralKey(const Frame &f, const Mac &source) {
  if (f.bytes[4] != 4 || !Crypto::equal(f.bytes + 85, source.bytes, 6))
    return false;
  for (unsigned i = 91; i < ControlTagAt; ++i)
    if (f.bytes[i])
      return false;
  uint8_t nonzero = 0;
  for (unsigned i = 69; i < 85; ++i)
    nonzero |= f.bytes[i];
  return nonzero != 0;
}
bool transcriptHash(const Frame &hello, const Frame &response,
                    uint8_t out[32]) {
  uint8_t canonical[ControlTagAt * 2]{};
  memcpy(canonical, hello.bytes, ControlTagAt);
  memcpy(canonical + ControlTagAt, response.bytes, ControlTagAt);
  const bool okay = Crypto::hash(canonical, sizeof(canonical), out);
  Crypto::wipe(canonical, sizeof(canonical));
  return okay;
}
} // namespace
void Handshake::clear() {
  agreement.clear();
  Crypto::wipe(helloAuth, 32);
  Crypto::wipe(responseAuth, 32);
  Crypto::wipe(transcript, 32);
  Crypto::wipe(&material, sizeof(material));
  Crypto::wipe(&hello, sizeof(hello));
  Crypto::wipe(&response, sizeof(response));
  Crypto::wipe(&cached, sizeof(cached));
  phase = Phase::Idle;
}
void Handshake::fail(uint32_t now, bool confirmation) {
  budget->invalid(now, confirmation);
  clear();
  phase = Phase::Failed;
}
bool Handshake::generate(Frame &f, Type type, uint32_t now) {
  if (!budget->spend(budget->ecc, now))
    return false;
  header(f, type, role);
  if (!agreement.generate(f.bytes + 4) || !Crypto::random(f.bytes + 69, 16)) {
    agreement.clear();
    Crypto::wipe(&f, sizeof(f));
    return false;
  }
  memcpy(f.bytes + 85, local.bytes, 6);
  return true;
}
bool Handshake::start(const uint8_t psk[32], uint8_t r, const Mac &own,
                      uint32_t now) {
  clear();
  role = r;
  local = own;
  started = nextSend = now;
  if ((r != Paw && r != Helmet) || !unicast(own) ||
      !budget->spend(budget->creation, now) ||
      !Crypto::hkdf(psk, 32, nullptr, 0, "protogen/v3/session/auth/hello",
                    helloAuth, 32) ||
      !Crypto::hkdf(psk, 32, nullptr, 0, "protogen/v3/session/auth/transcript",
                    responseAuth, 32)) {
    clear();
    phase = Phase::Failed;
    return false;
  }
  // Helmet only allocates an ephemeral key after an authenticated Hello.
  if (role == Paw && (!generate(hello, SessionHello, now) ||
                      !Crypto::hmac(helloAuth, hello.bytes, ControlTagAt,
                                    hello.bytes + ControlTagAt))) {
    clear();
    phase = Phase::Failed;
    return false;
  }
  phase = Phase::Rendezvous;
  return true;
}
bool Handshake::authenticate(const Frame &f, const uint8_t key[32],
                             const uint8_t *data, size_t n, uint32_t now,
                             bool confirmation) {
  if (!budget->verify(now))
    return false;
  uint8_t tag[32]{};
  const bool okay = Crypto::hmac(key, data, n, tag) &&
                    Crypto::equal(tag, f.bytes + ControlTagAt, 32);
  Crypto::wipe(tag, 32);
  if (okay)
    budget->accepted();
  else
    budget->invalid(now, confirmation);
  return okay;
}
bool Handshake::derive(const Frame &remote, uint32_t now) {
  if (!budget->spend(budget->ecc, now))
    return false;
  phase = Phase::AuthenticatedEcdh;
  uint8_t shared[32]{};
  const bool okay = agreement.derive(remote.bytes + 4, shared) &&
                    deriveMaterial(shared, transcript, material);
  agreement.clear();
  Crypto::wipe(shared, 32);
  if (!okay) {
    fail(now);
    return false;
  }
  // No long-term-authentication material remains after the exchange.
  Crypto::wipe(helloAuth, 32);
  Crypto::wipe(responseAuth, 32);
  return true;
}
bool Handshake::finished(Type type) {
  header(cached, type, role);
  memcpy(cached.bytes + 4, transcript, 32);
  memcpy(cached.bytes + 36, material.id, 8);
  // Type, role and version are authenticated; commit has a distinct type.
  return Crypto::hmac(type == SessionCommit ? material.pawCommit
                      : role == Paw         ? material.pawFinished
                                            : material.helmetFinished,
                      cached.bytes, ControlTagAt, cached.bytes + ControlTagAt);
}
bool Handshake::tick(uint32_t now, Frame &f, Mac &destination,
                     bool &broadcast) {
  if (phase == Phase::Idle || phase == Phase::Failed || ready())
    return false;
  if (uint32_t(now - started) >= HandshakeMs) {
    const bool confirmation = pending();
    fail(now, confirmation);
    return false;
  }
  if (!due(now, nextSend))
    return false;
  nextSend = now + HandshakeRetryMs;
  broadcast = phase == Phase::Rendezvous && role == Paw;
  destination = peer;
  if (phase == Phase::Rendezvous) {
    if (role != Paw)
      return false;
    f = hello;
    return true;
  }
  f = cached;
  return true;
}
bool Handshake::receive(const Frame &f, const Mac &source, uint32_t now) {
  if (phase == Phase::Idle || phase == Phase::Failed || ready() ||
      uint32_t(now - started) >= HandshakeMs ||
      !valid(f.bytes, PacketBytes, role == Paw ? Helmet : Paw) ||
      !unicast(source) || same(source, local))
    return false;
  const uint8_t type = f.bytes[2];
  if (role == Helmet && type == SessionHello) {
    if (phase != Phase::Rendezvous) {
      // Identical authenticated retransmissions retain the cached response.
      // Do not let captured broadcasts bypass the bounded retry cadence.
      if (same(source, peer) && Crypto::equal(&f, &hello, sizeof(f))) {
        return true;
      }
      return false;
    }
    if (!structuralKey(f, source))
      return false;
    uint8_t digest[32]{};
    if (!Crypto::hash(f.bytes, ControlTagAt, digest))
      return false;
    for (const auto &entry : recent)
      if (entry.valid && now - entry.at < HandshakeMs * 2 &&
          Crypto::equal(entry.hash, digest, 32)) {
        Crypto::wipe(digest, 32);
        return false;
      }
    if (!authenticate(f, helloAuth, f.bytes, ControlTagAt, now)) {
      Crypto::wipe(digest, 32);
      return false;
    }
    memcpy(recent[recentAt].hash, digest, 32);
    recent[recentAt].at = now;
    recent[recentAt].valid = true;
    recentAt = (recentAt + 1) % 4;
    Crypto::wipe(digest, 32);
    if (!generate(response, SessionResponse, now))
      return false;
    hello = f;
    peer = source;
    if (!transcriptHash(hello, response, transcript) ||
        !Crypto::hmac(responseAuth, transcript, 32,
                      response.bytes + ControlTagAt) ||
        !derive(hello, now)) {
      if (!failed()) {
        fail(now);
      }
      return false;
    }
    cached = response;
    phase = Phase::AwaitPawFinished;
    started = nextSend = now;
    return true;
  }
  if (role == Paw && type == SessionResponse) {
    if (phase != Phase::Rendezvous) {
      if (same(source, peer) && Crypto::equal(&f, &response, sizeof(f))) {
        return true;
      }
      return false;
    }
    if (!structuralKey(f, source))
      return false;
    uint8_t hash[32]{};
    const bool okay = transcriptHash(hello, f, hash) &&
                      authenticate(f, responseAuth, hash, 32, now);
    if (okay)
      memcpy(transcript, hash, 32);
    Crypto::wipe(hash, 32);
    if (!okay)
      return false;
    response = f;
    peer = source;
    if (!derive(response, now) || !finished(SessionFinish)) {
      if (!failed())
        fail(now);
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
      !Crypto::equal(f.bytes + 36, material.id, 8))
    return false;
  for (unsigned i = 44; i < ControlTagAt; ++i)
    if (f.bytes[i])
      return false;
  if (!authenticate(f,
                    type == SessionCommit ? material.pawCommit
                    : role == Paw         ? material.helmetFinished
                                          : material.pawFinished,
                    f.bytes, ControlTagAt, now, true)) {
    // A failed confirmation cannot leave partially active keys.
    clear();
    phase = Phase::Failed;
    return false;
  }
  if (role == Helmet && type == SessionCommit) {
    phase = Phase::Complete;
    return true;
  }
  if (!finished(role == Paw ? SessionCommit : SessionFinish)) {
    fail(now, true);
    return false;
  }
  phase = role == Paw ? Phase::CommitReady : Phase::AwaitCommit;
  nextSend = now;
  return true;
}
} // namespace Wireless
