#include "WirelessSession.h"
#include <string.h>
namespace Wireless {
static_assert(PacketBytes == HeaderBytes + PlainBytes + TagBytes &&
              PacketBytes <= 250);
static_assert(PlainBytes >= HelmetLink::MaxRaw + 2);
bool deriveMaterial(const uint8_t ecdh[32], const uint8_t transcript[32],
                    Material &out) {
  Material m;
  const auto derive = [&](const char *label, uint8_t *p, size_t n) {
    return Crypto::hkdf(ecdh, 32, transcript, 32, label, p, n);
  };
  const bool okay =
      derive("protogen/v3/session/ecdh/paw-to-helmet/aes-gcm", m.pawKey, 32) &&
      derive("protogen/v3/session/ecdh/helmet-to-paw/aes-gcm", m.helmetKey,
             32) &&
      derive("protogen/v3/session/ecdh/id", m.id, 8) &&
      derive("protogen/v3/session/ecdh/recovery", m.recovery, 32) &&
      derive("protogen/v3/session/ecdh/esp-now/lmk", m.lmk, 16) &&
      derive("protogen/v3/session/ecdh/esp-now/pmk", m.pmk, 16) &&
      derive("protogen/v3/session/ecdh/identity/paw", m.pawMac.bytes, 6) &&
      derive("protogen/v3/session/ecdh/identity/helmet", m.helmetMac.bytes,
             6) &&
      derive("protogen/v3/session/finished/paw", m.pawFinished, 32) &&
      derive("protogen/v3/session/finished/helmet", m.helmetFinished, 32) &&
      derive("protogen/v3/session/finished/paw-commit", m.pawCommit, 32);
  m.pawMac.bytes[0] = (m.pawMac.bytes[0] & 0xfc) | 2;
  m.helmetMac.bytes[0] = (m.helmetMac.bytes[0] & 0xfc) | 2;
  m.pawMac.bytes[5] |= 1;
  m.helmetMac.bytes[5] &= 0xfe;
  if (okay)
    out = m;
  else
    Crypto::wipe(&out, sizeof(out));
  Crypto::wipe(&m, sizeof(m));
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
      return protogenActionValid(a, v, d);
    }
    // REQUEST_STATUS is a ticket-free request, never forwarded as an action.
    return p.kind == Status && !p.sequence && !p.epoch && !p.ticket &&
           (p.length == 0 || (p.length == 1 && p.payload[0] == 1) ||
            (p.length == 4 && HelmetLink::u16(p.payload) <= 6000));
  }
  return sender == Helmet &&
         (validHelmetStatus(p) ||
          (p.kind == Ack && p.length == 5 && p.sequence && p.epoch &&
           u32(p.payload) && p.payload[4] <= Unavailable));
}
void Session::clear() {
  Crypto::wipe(&keys, sizeof(keys));
  replay.reset();
  sequence = records = 0;
  phase = Phase::Empty;
}
bool Session::begin(uint8_t r, const Material &m) {
  uint8_t pawFingerprint[32]{}, helmetFingerprint[32]{};
  const bool okay =
      (r == Paw || r == Helmet) && Crypto::hash(m.pawKey, 32, pawFingerprint) &&
      Crypto::hash(m.helmetKey, 32, helmetFingerprint) &&
      !Crypto::equal(pawFingerprint, previousPawFingerprint, 32) &&
      !Crypto::equal(helmetFingerprint, previousHelmetFingerprint, 32);
  clear();
  if (okay) {
    role = r;
    keys = m;
    phase = Phase::Active;
    Crypto::wipe(keys.pawFinished, 32);
    Crypto::wipe(keys.helmetFinished, 32);
    Crypto::wipe(keys.pawCommit, 32);
    Crypto::wipe(keys.lmk, 16);
    Crypto::wipe(keys.pmk, 16);
    Crypto::wipe(keys.recovery, 32);
    memcpy(previousPawFingerprint, pawFingerprint, 32);
    memcpy(previousHelmetFingerprint, helmetFingerprint, 32);
  }
  Crypto::wipe(pawFingerprint, 32);
  Crypto::wipe(helmetFingerprint, 32);
  return okay;
}
bool Session::seal(const HelmetLink::Packet &p, Frame &f) {
  if (!protogenMaySeal(ready(), records, sequence) || !applicationValid(p, role))
    return false;
  uint8_t plain[PlainBytes]{};
  if (!Crypto::random(plain, sizeof(plain))) {
    Crypto::wipe(plain, sizeof(plain));
    return false;
  }
  const size_t n = HelmetLink::pack(p, plain + 2, PlainBytes - 2);
  if (!n) {
    Crypto::wipe(plain, sizeof(plain));
    return false;
  }
  HelmetLink::put16(plain, n);
  header(f, Data, role);
  memcpy(f.bytes + 4, keys.id, 8);
  put64(f.bytes + 12, protogenReserveSequence(&sequence));
  uint8_t nonce[12];
  protogenNonce(nonce, Version, role, sequence);
  const bool okay =
      Crypto::gcm(true, role == Paw ? keys.pawKey : keys.helmetKey, nonce,
                  f.bytes, HeaderBytes, plain, sizeof(plain),
                  f.bytes + HeaderBytes, PacketBytes - HeaderBytes);
  Crypto::wipe(plain, sizeof(plain));
  protogenAccountRecord(&records, okay);
  return okay;
}
bool Session::open(const uint8_t *b, size_t n, HelmetLink::Packet &p) {
  const uint8_t sender = role == Paw ? Helmet : Paw;
  if (!protogenMayOpen(ready(), records) || !valid(b, n, sender) ||
      b[2] != Data) {
    if (diagnostics)
      increment(diagnostics->malformed);
    return false;
  }
  if (!Crypto::equal(b + 4, keys.id, 8)) {
    if (diagnostics)
      increment(diagnostics->replayRejects);
    return false;
  }
  const uint64_t seq = get64(b + 12);
  if (!replay.allows(seq)) {
    if (diagnostics)
      increment(diagnostics->replayRejects);
    return false;
  }
  uint8_t nonce[12], plain[PlainBytes]{};
  protogenNonce(nonce, Version, sender, seq);
  bool okay = Crypto::gcm(false, sender == Paw ? keys.pawKey : keys.helmetKey,
                          nonce, b, HeaderBytes, b + HeaderBytes,
                          n - HeaderBytes, plain, sizeof(plain));
  if (!okay && diagnostics)
    increment(diagnostics->aeadFailures);
  protogenAccountRecord(&records, okay);
  HelmetLink::Packet decoded;
  const size_t length = HelmetLink::u16(plain);
  const bool parsed = okay && length >= HelmetLink::HeaderBytes + 2 &&
                      length <= HelmetLink::MaxRaw &&
                      HelmetLink::unpack(plain + 2, length, decoded);
  okay = protogenMayCommitReplay(okay, parsed, parsed && applicationValid(decoded, sender));
  Crypto::wipe(plain, sizeof(plain));
  if (!okay) {
    if (diagnostics)
      increment(diagnostics->malformed);
    return false;
  }
  replay.accept(seq);
  p = decoded;
  return true;
}
} // namespace Wireless
