#include "Derivation.hpp"
#include <cstring>
namespace ProtogenV4 {
void objectHeader(Frame &f, Mode mode, uint64_t wire, const Identity &source,
                  Object o) {
  f = Frame{};
  f.bytes[0] = 0x70;
  f.bytes[1] = Version;
  f.bytes[2] = uint8_t(Kind::Fragment);
  f.bytes[3] = objectRole(o);
  f.bytes[4] = uint8_t(mode);
  f.bytes[5] = uint8_t(o);
  f.bytes[7] = fragments(o);
  write16(f.bytes + 8, uint16_t(objectSize(o)));
  write64(f.bytes + 12, wire);
  std::memcpy(f.bytes + 20, source.bytes, 6);
}
size_t canonical(Crypto &c, const Exchange &x, uint8_t *out, size_t capacity) {
  if (!out || capacity < TranscriptCapacity || x.complete != 0x3f || !x.wire)
    return 0;
  constexpr char domain[] = "protogen/v4/transcript";
  size_t at = 0;
  std::memcpy(out, domain, sizeof(domain));
  at += sizeof(domain);
  const uint8_t context[] = {0x70, Version, uint8_t(x.mode), Paw, Helmet, 1,
                             0,    4};
  std::memcpy(out + at, context, sizeof(context));
  at += sizeof(context);
  write64(out + at, x.wire);
  at += 8;
  for (unsigned i = 0; i < ObjectCount; ++i) {
    const auto o = Object(i);
    Frame f;
    objectHeader(f, x.mode, x.wire, objectRole(o) == Paw ? x.paw : x.helmet, o);
    std::memcpy(out + at, f.bytes, 28);
    at += 28;
    if (!c.hash(x.objects[i], objectSize(o), out + at)) {
      c.wipe(out, capacity);
      return 0;
    }
    at += 32;
    std::memcpy(out + at, x.objects[i], objectSize(o));
    at += objectSize(o);
  }
  return at;
}
bool nonceCommit(Crypto &c, Mode mode, uint64_t wire, uint8_t role,
                 const Identity &id, const uint8_t offer[129],
                 const uint8_t nonce[16], uint8_t out[32]) {
  Frame f;
  objectHeader(f, mode, wire, id,
               role == Paw ? Object::PawNonce : Object::HelmetNonce);
  uint8_t input[141]{};
  std::memcpy(input, f.bytes, 28);
  std::memcpy(input + 28, offer, 65);
  std::memcpy(input + 93, offer + 97, 32);
  std::memcpy(input + 125, nonce, 16);
  const bool ok = c.hash(input, sizeof(input), out);
  c.wipe(input, sizeof(input));
  return ok;
}
bool hybridRoot(Crypto &c, const uint8_t ecdh[32], const uint8_t kem[32],
                const uint8_t *transcript, size_t size, uint8_t root[32],
                DerivationWorkspace &work) {
  c.wipe(root, 32);
  c.wipe(work.combiner, sizeof(work.combiner));
  if (!transcript || !size || size > TranscriptCapacity || zero(ecdh, 32) ||
      zero(kem, 32))
    return false;
  // SP 800-56C r2 section 4.1, option 1, L=256; SP 800-227 equation (14).
  constexpr char context[] = "protogen/v4/hybrid/p256-mlkem1024/sha3-256";
  auto *b = work.combiner;
  b[3] = 1;
  std::memcpy(b + 4, ecdh, 32);
  std::memcpy(b + 36, kem, 32);
  size_t at = 68;
  std::memcpy(b + at, context, sizeof(context));
  at += sizeof(context);
  b[at + 2] = 1;
  at += 4; // BE32 L=256.
  b[at] = uint8_t(size >> 24);
  b[at + 1] = uint8_t(size >> 16);
  b[at + 2] = uint8_t(size >> 8);
  b[at + 3] = uint8_t(size);
  at += 4;
  std::memcpy(b + at, transcript, size);
  at += size;
  const bool ok = c.sha3(b, at, root) && !zero(root, 32);
  c.wipe(b, sizeof(work.combiner));
  if (!ok)
    c.wipe(root, 32);
  return ok;
}
bool deriveMaterial(Crypto &c, const uint8_t root[32],
                    const uint8_t transcript[32], Material &out) {
  Material m;
  const auto k = [&](const char *label, uint8_t *p, size_t n) {
    return c.hkdf(root, 32, transcript, 32, label, p, n);
  };
  const bool ok =
      !zero(root, 32) &&
      k("protogen/v4/session/paw-to-helmet/aes-gcm", m.pawKey, 32) &&
      k("protogen/v4/session/helmet-to-paw/aes-gcm", m.helmetKey, 32) &&
      k("protogen/v4/session/id", m.id, 8) &&
      k("protogen/v4/session/recovery", m.recovery, 32) &&
      k("protogen/v4/session/esp-now/lmk", m.lmk, 16) &&
      k("protogen/v4/session/esp-now/pmk", m.pmk, 16) &&
      k("protogen/v4/session/identity/paw", m.pawIdentity.bytes, 6) &&
      k("protogen/v4/session/identity/helmet", m.helmetIdentity.bytes, 6) &&
      k("protogen/v4/session/finished/paw", m.pawFinished, 32) &&
      k("protogen/v4/session/finished/helmet", m.helmetFinished, 32) &&
      k("protogen/v4/session/finished/paw-commit", m.commit, 32) &&
      k("protogen/v4/session/finished/helmet-commit-ack", m.commitAck, 32) &&
      k("protogen/v4/session/handshake/auth", m.handshakeAuth, 32) &&
      k("protogen/v4/session/handshake/fragments", m.fragmentAuth, 32);
  m.pawIdentity.bytes[0] = (m.pawIdentity.bytes[0] & 0xfc) | 2;
  m.pawIdentity.bytes[5] |= 1;
  m.helmetIdentity.bytes[0] = (m.helmetIdentity.bytes[0] & 0xfc) | 2;
  m.helmetIdentity.bytes[5] &= 0xfe;
  const bool valid = ok && !equal(m.pawKey, m.helmetKey, 32) && !zero(m.id, 8);
  if (valid)
    out = m;
  else
    c.wipe(&out, sizeof(out));
  c.wipe(&m, sizeof(m));
  return valid;
}
} // namespace ProtogenV4
