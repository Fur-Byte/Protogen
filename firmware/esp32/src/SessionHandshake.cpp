#include "SessionHandshake.h"
#include <string.h>
namespace Wireless {
void Handshake::clear() {
  Crypto::wipe(auth, sizeof(auth));
  Crypto::wipe(secret, sizeof(secret));
  Crypto::wipe(pawNonce, sizeof(pawNonce));
  Crypto::wipe(helmetNonce, sizeof(helmetNonce));
  Crypto::wipe(&material, sizeof(material));
  Crypto::wipe(&cached, sizeof(cached));
  active = responding = established = false;
}
bool Handshake::sign(Frame &f) {
  return Crypto::hmac(auth, f.bytes, ControlTagAt, f.bytes + ControlTagAt);
}
bool Handshake::start(const uint8_t psk[32], uint8_t r, const Mac &own,
                      uint32_t now) {
  clear();
  role = r;
  local = own;
  started = nextSend = now;
  lastAuth = now - AuthIntervalMs;
  memcpy(secret, psk, 32);
  active = Crypto::hkdf(psk, 32, nullptr, 0,
                        "protogen/v2/rendezvous/authentication", auth, 32);
  if (role == Paw)
    active = active && Crypto::random(pawNonce, 32);
  return active;
}
bool Handshake::tick(uint32_t now, Frame &f, Mac &destination,
                     bool &broadcast) {
  if (!active || established)
    return false;
  if (now - started >= HandshakeMs) {
    uint8_t saved[32];
    memcpy(saved, secret, 32);
    const Mac own = local;
    start(saved, role, own, now);
    Crypto::wipe(saved, 32);
    return false;
  }
  if (!due(now, nextSend))
    return false;
  nextSend = now + HandshakeRetryMs;
  if (responding) {
    f = cached;
    destination = peer;
    broadcast = false;
    return true;
  }
  if (role == Helmet)
    return false;
  header(f, SessionHello, role);
  memcpy(f.bytes + 4, pawNonce, 32);
  memcpy(f.bytes + 36, local.bytes, 6);
  broadcast = true;
  return sign(f);
}
bool Handshake::receive(const Frame &f, const Mac &source, uint32_t now) {
  if (!active || established ||
      !valid(f.bytes, PacketBytes, role == Paw ? Helmet : Paw) ||
      !unicast(source) || same(source, local) ||
      now - lastAuth < AuthIntervalMs)
    return false;
  const uint8_t type = f.bytes[2];
  if (type < SessionHello || type > SessionFinish)
    return false;
  lastAuth = now;
  uint8_t tag[32];
  if (!Crypto::hmac(auth, f.bytes, ControlTagAt, tag) ||
      !Crypto::equal(tag, f.bytes + ControlTagAt, 32))
    return false;
  if (role == Helmet && type == SessionHello &&
      Crypto::equal(source.bytes, f.bytes + 36, 6)) {
    if (responding)
      return same(source, peer) && Crypto::equal(pawNonce, f.bytes + 4, 32);
    peer = source;
    memcpy(pawNonce, f.bytes + 4, 32);
    if (!Crypto::random(helmetNonce, 32))
      return false;
    header(cached, SessionResponse, Helmet);
    memcpy(cached.bytes + 4, pawNonce, 32);
    memcpy(cached.bytes + 36, helmetNonce, 32);
    memcpy(cached.bytes + 68, peer.bytes, 6);
    memcpy(cached.bytes + 74, local.bytes, 6);
    responding = sign(cached);
    nextSend = now;
    started = now;
    return responding;
  }
  if (role == Paw && type == SessionResponse &&
      Crypto::equal(pawNonce, f.bytes + 4, 32) &&
      Crypto::equal(local.bytes, f.bytes + 68, 6) &&
      Crypto::equal(source.bytes, f.bytes + 74, 6)) {
    peer = source;
    memcpy(helmetNonce, f.bytes + 36, 32);
    if (!deriveMaterial(secret, pawNonce, helmetNonce, local, peer, material))
      return false;
    cached = f;
    cached.bytes[2] = SessionFinish;
    cached.bytes[3] = Paw;
    if (!sign(cached))
      return false;
    responding = true;
    return true;
  }
  if (role == Helmet && type == SessionFinish && responding &&
      same(source, peer) && Crypto::equal(f.bytes + 4, cached.bytes + 4, 76)) {
    established =
        deriveMaterial(secret, pawNonce, helmetNonce, peer, local, material);
    return established;
  }
  return false;
}
} // namespace Wireless
