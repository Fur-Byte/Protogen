#include "Storage.hpp"
#include <cstring>
namespace ProtogenV4 {
bool encodeRecord(Crypto &c, uint8_t role, const uint8_t secret[32],
                  const uint8_t transcript[32], uint8_t out[PairRecordBytes]) {
  c.wipe(out, PairRecordBytes);
  if ((role != Paw && role != Helmet) || zero(secret, 32))
    return false;
  out[0] = 'P';
  out[1] = 'V';
  out[2] = '4';
  out[3] = 0;
  out[4] = 1;
  out[5] = Version;
  out[6] = 1;
  out[7] = role;
  write16(out + 8, 1024);
  std::memcpy(out + 16, secret, 32);
  std::memcpy(out + 48, transcript, 32);
  if (!c.hmac(secret, out, 80, out + 80)) {
    c.wipe(out, PairRecordBytes);
    return false;
  }
  return true;
}
PairStatus decodeRecord(Crypto &c, uint8_t role, const uint8_t *p, size_t n,
                        uint8_t secret[32]) {
  c.wipe(secret, 32);
  if (!p || !n)
    return PairStatus::Unpaired;
  if (n == 46 && p[0] == 2 && p[1] == role && p[2] <= 1 &&
      HelmetLink::u16(p + 44) == HelmetLink::crc16(p, 44))
    return PairStatus::V3_REPAIR_REQUIRED;
  if (n != PairRecordBytes || p[0] != 'P' || p[1] != 'V' || p[2] != '4' ||
      p[3] || p[4] != 1 || p[5] != Version || p[6] != 1 || p[7] != role ||
      read16(p + 8) != 1024 || !zero(p + 10, 6) || zero(p + 16, 32))
    return PairStatus::RecoveryRequired;
  uint8_t tag[32]{};
  const bool ok = c.hmac(p + 16, p, 80, tag) && equal(tag, p + 80, 32);
  c.wipe(tag, 32);
  if (!ok)
    return PairStatus::RecoveryRequired;
  std::memcpy(secret, p + 16, 32);
  return PairStatus::PairedV4;
}
} // namespace ProtogenV4
