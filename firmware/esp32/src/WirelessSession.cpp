#include "WirelessSession.h"
#include <string.h>
namespace Wireless {
static_assert(PacketBytes == HeaderBytes + PlainBytes + TagBytes &&
              PacketBytes <= 250);
static_assert(PlainBytes >= HelmetLink::MaxRaw + 2);
bool deriveMaterial(const uint8_t secret[32], const uint8_t pn[32],
                    const uint8_t hn[32], const Mac &paw, const Mac &helmet,
                    Material &out) {
  uint8_t context[78] = {Version, 0};
  memcpy(context + 2, pn, 32);
  memcpy(context + 34, hn, 32);
  memcpy(context + 66, paw.bytes, 6);
  memcpy(context + 72, helmet.bytes, 6);
  uint8_t salt[32];
  Material m;
  bool okay = Crypto::hash(context, sizeof(context), salt);
  const auto derive = [&](const char *label, uint8_t *p, size_t n) {
    return Crypto::hkdf(secret, 32, salt, 32, label, p, n);
  };
  okay = okay &&
         derive("protogen/v2/session/paw-to-helmet/aes-gcm", m.pawKey, 32) &&
         derive("protogen/v2/session/helmet-to-paw/aes-gcm", m.helmetKey, 32) &&
         derive("protogen/v2/session/id", m.id, 8) &&
         derive("protogen/v2/session/recovery", m.recovery, 32) &&
         derive("protogen/v2/session/esp-now/lmk", m.lmk, 16) &&
         derive("protogen/v2/session/esp-now/pmk", m.pmk, 16) &&
         derive("protogen/v2/session/identity/paw", m.pawMac.bytes, 6) &&
         derive("protogen/v2/session/identity/helmet", m.helmetMac.bytes, 6);
  m.pawMac.bytes[0] = (m.pawMac.bytes[0] & 0xfc) | 2;
  m.helmetMac.bytes[0] = (m.helmetMac.bytes[0] & 0xfc) | 2;
  // Role bit reserves disjoint address spaces for the pair.
  m.pawMac.bytes[5] |= 1;
  m.helmetMac.bytes[5] &= 0xfe;
  if (okay)
    out = m;
  Crypto::wipe(&m, sizeof(m));
  Crypto::wipe(salt, sizeof(salt));
  return okay;
}
bool applicationValid(const HelmetLink::Packet &p, uint8_t sender) {
  using namespace HelmetLink;
  if (sender == Paw) {
    if (p.kind == Heartbeat)
      return p.length == 4 && p.epoch && p.ticket && u32(p.payload);
    if (p.kind == Command && p.length == 8 && p.sequence && p.epoch &&
        p.ticket && u32(p.payload)) {
      const uint8_t a = p.payload[4], v = p.payload[5];
      const uint16_t d = u16(p.payload + 6);
      return a >= Expression && a <= MicControl && (a != Expression || v < 6) &&
             ((a != Sleep && a != MicControl) || v < 2) &&
             (a != Boop || (v == 0 && d >= 100 && d <= 6000)) &&
             (a == Boop || d == 0);
    }
    // REQUEST_STATUS is a ticket-free request, never forwarded as an action.
    return p.kind == Status && !p.sequence && !p.epoch && !p.ticket &&
           (p.length == 0 || (p.length == 1 && p.payload[0] == 1) ||
            (p.length == 4 && HelmetLink::u16(p.payload) <= 6000));
  }
  return sender == Helmet &&
         ((p.kind == Status &&
           (p.length == StatusBytes ||
            (p.length == StatusBytes + 4 &&
             (u16(p.payload + StatusBytes) == 0xffff ||
              u16(p.payload + StatusBytes) <= 100))) &&
           p.epoch && u32(p.payload) && p.payload[4] < 6 &&
           !(p.payload[5] & 0x80) && p.payload[19] == 0) ||
          (p.kind == Ack && p.length == 5 && p.sequence && p.epoch &&
           u32(p.payload) && p.payload[4] <= Unavailable));
}
void Session::clear() {
  Crypto::wipe(&keys, sizeof(keys));
  replay.reset();
  sequence = 0;
  active = false;
}
void Session::begin(uint8_t r, const Material &m) {
  clear();
  role = r;
  keys = m;
  active = true;
}
bool Session::seal(const HelmetLink::Packet &p, Frame &f) {
  if (!active || sequence >= MaxSequence || !applicationValid(p, role))
    return false;
  uint8_t plain[PlainBytes]{};
  if (!Crypto::random(plain, sizeof(plain)))
    return false;
  const size_t n = HelmetLink::pack(p, plain + 2, PlainBytes - 2);
  if (!n)
    return false;
  HelmetLink::put16(plain, n);
  header(f, Data, role);
  memcpy(f.bytes + 4, keys.id, 8);
  put64(f.bytes + 12, ++sequence);
  uint8_t nonce[12] = {0x70, Version, role, 0};
  put64(nonce + 4, sequence);
  const bool okay =
      Crypto::gcm(true, role == Paw ? keys.pawKey : keys.helmetKey, nonce,
                  f.bytes, HeaderBytes, plain, sizeof(plain),
                  f.bytes + HeaderBytes, PacketBytes - HeaderBytes);
  Crypto::wipe(plain, sizeof(plain));
  return okay;
}
bool Session::open(const uint8_t *b, size_t n, HelmetLink::Packet &p) {
  const uint8_t sender = role == Paw ? Helmet : Paw;
  if (!active || !valid(b, n, sender) || b[2] != Data ||
      !Crypto::equal(b + 4, keys.id, 8))
    return false;
  const uint64_t seq = get64(b + 12);
  if (!replay.allows(seq))
    return false;
  uint8_t nonce[12] = {0x70, Version, sender, 0}, plain[PlainBytes]{};
  put64(nonce + 4, seq);
  bool okay = Crypto::gcm(false, sender == Paw ? keys.pawKey : keys.helmetKey,
                          nonce, b, HeaderBytes, b + HeaderBytes,
                          n - HeaderBytes, plain, sizeof(plain));
  HelmetLink::Packet decoded;
  const size_t length = HelmetLink::u16(plain);
  okay = okay && length >= HelmetLink::HeaderBytes + 2 &&
         length <= HelmetLink::MaxRaw &&
         HelmetLink::unpack(plain + 2, length, decoded) &&
         applicationValid(decoded, sender);
  Crypto::wipe(plain, sizeof(plain));
  if (!okay)
    return false;
  replay.accept(seq);
  p = decoded;
  return true;
}
} // namespace Wireless
