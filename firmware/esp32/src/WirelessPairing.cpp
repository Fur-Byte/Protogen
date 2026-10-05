#include "WirelessPairing.h"
#include <string.h>
namespace Wireless {
bool Pairing::makeCommit(uint8_t r, const Mac &m, const uint8_t pub[65],
                         const uint8_t random[16], uint8_t out[32]) {
  uint8_t b[89] = {Version, r, 0};
  memcpy(b + 2, m.bytes, 6);
  memcpy(b + 8, pub, 65);
  memcpy(b + 73, random, 16);
  return Crypto::hash(b, sizeof(b), out);
}
void Pairing::clear() {
  agreement.clear();
  Crypto::wipe(publicKey, sizeof(publicKey));
  Crypto::wipe(nonce, sizeof(nonce));
  Crypto::wipe(peerPublic, sizeof(peerPublic));
  Crypto::wipe(peerNonce, sizeof(peerNonce));
  Crypto::wipe(secret, sizeof(secret));
  running = locked = derived = confirmed = peerConfirmed = completed = false;
  code = 0;
}
bool Pairing::start(uint8_t r, const Mac &m, uint32_t now) {
  clear();
  role = r;
  local = m;
  started = now;
  nextSend = now;
  lastEcdh = now - EcdhIntervalMs;
  lastAuth = now - AuthIntervalMs;
  running = agreement.generate(publicKey) &&
            Crypto::random(nonce, sizeof(nonce)) &&
            makeCommit(role, local, publicKey, nonce, commitment);
  return running;
}
bool Pairing::tick(uint32_t now, Frame &f, Mac &destination, bool &broadcast) {
  if (!running)
    return false;
  if (now - started >= PairingMs || (completed && now - completedAt >= 2000)) {
    clear();
    return false;
  }
  if (!due(now, nextSend))
    return false;
  nextSend = now + PairRetryMs;
  destination = peer;
  broadcast = !locked;
  // Continue commitments until a matching reveal arrives: lost commitments
  // cannot strand the peer. Then repeat reveal and confirmation alternately.
  if (!locked || (!derived && ((now - started) / PairRetryMs) % 2 == 0)) {
    header(f, PairCommit, role);
    memcpy(f.bytes + 4, commitment, 32);
    broadcast = true;
  } else if (!derived || ((now - started) / PairRetryMs) % 2 == 0) {
    header(f, PairReveal, role);
    memcpy(f.bytes + 4, publicKey, 65);
    memcpy(f.bytes + 69, nonce, 16);
    memcpy(f.bytes + 85, local.bytes, 6);
  } else {
    header(f, PairConfirm, role);
    memcpy(f.bytes + 4, transcript, 32);
    f.bytes[36] = confirmed ? 1 : 0;
    if (!Crypto::hmac(secret, f.bytes, ControlTagAt, f.bytes + ControlTagAt))
      return false;
  }
  return true;
}
bool Pairing::receive(const Frame &f, const Mac &source, uint32_t now) {
  if (!running || now - started >= PairingMs ||
      !valid(f.bytes, PacketBytes, role == Paw ? Helmet : Paw) ||
      !unicast(source) || same(source, local))
    return false;
  const auto type = f.bytes[2];
  if (type == PairCommit) {
    if (!locked) {
      peer = source;
      memcpy(peerCommitment, f.bytes + 4, 32);
      locked = true;
      nextSend = now;
      return true;
    }
    return same(source, peer) && Crypto::equal(peerCommitment, f.bytes + 4, 32);
  }
  if (!locked || !same(source, peer))
    return false;
  if (type == PairReveal) {
    if (derived)
      return Crypto::equal(peerPublic, f.bytes + 4, 65) &&
             Crypto::equal(peerNonce, f.bytes + 69, 16);
    uint8_t expected[32];
    if (!Crypto::equal(source.bytes, f.bytes + 85, 6) ||
        !makeCommit(f.bytes[3], source, f.bytes + 4, f.bytes + 69, expected) ||
        !Crypto::equal(expected, peerCommitment, 32))
      return false;
    if (now - lastEcdh < EcdhIntervalMs)
      return false;
    lastEcdh = now;
    uint8_t shared[32]{};
    if (!agreement.derive(f.bytes + 4, shared)) {
      Crypto::wipe(shared, sizeof(shared));
      return false;
    }
    memcpy(peerPublic, f.bytes + 4, 65);
    memcpy(peerNonce, f.bytes + 69, 16);
    uint8_t context[176] = {Version, 0};
    const auto copy = [&](uint8_t *p, const uint8_t *pub, const uint8_t *random,
                          const Mac &m) {
      memcpy(p, pub, 65);
      memcpy(p + 65, random, 16);
      memcpy(p + 81, m.bytes, 6);
    };
    copy(context + 2, role == Paw ? publicKey : peerPublic,
         role == Paw ? nonce : peerNonce, role == Paw ? local : peer);
    copy(context + 89, role == Helmet ? publicKey : peerPublic,
         role == Helmet ? nonce : peerNonce, role == Helmet ? local : peer);
    uint8_t sas[32]{};
    derived = Crypto::hash(context, sizeof(context), transcript) &&
              Crypto::hkdf(shared, 32, transcript, 32,
                           "protogen/v2/pair/long-term", secret, 32) &&
              Crypto::hkdf(shared, 32, transcript, 32, "protogen/v2/pair/sas",
                           sas, 32);
    code = (uint32_t(sas[0]) << 16 | uint32_t(sas[1]) << 8 | sas[2]) % 1000000;
    Crypto::wipe(shared, sizeof(shared));
    Crypto::wipe(sas, sizeof(sas));
    agreement.clear();
    nextSend = now;
    return derived;
  }
  if (type == PairConfirm && derived && f.bytes[36] <= 1 &&
      Crypto::equal(f.bytes + 4, transcript, 32)) {
    if (now - lastAuth < AuthIntervalMs)
      return false;
    lastAuth = now;
    uint8_t tag[32];
    if (!Crypto::hmac(secret, f.bytes, ControlTagAt, tag) ||
        !Crypto::equal(tag, f.bytes + ControlTagAt, 32))
      return false;
    if (f.bytes[36] == 1)
      peerConfirmed = true;
    return true;
  }
  return false;
}
bool Pairing::getSecret(uint8_t out[32]) const {
  if (!ready())
    return false;
  memcpy(out, secret, 32);
  return true;
}
} // namespace Wireless
