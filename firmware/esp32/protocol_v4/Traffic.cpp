#include "Traffic.hpp"
#include <cstring>
namespace ProtogenV4 {
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
    return p.kind == Status && !p.sequence && !p.epoch && !p.ticket &&
           (p.length == 0 || (p.length == 1 && p.payload[0] == 1) ||
            (p.length == 4 && u16(p.payload) <= 6000));
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
void Traffic::clear() {
  crypto.wipe(tx_, 32);
  crypto.wipe(rx_, 32);
  crypto.wipe(id_, 8);
  sequence_ = records_ = top_ = window_ = 0;
  active_ = false;
}
bool Traffic::begin(const Material &m) {
  uint8_t a[32]{}, b[32]{};
  const auto *tx = role_ == Paw ? m.pawKey : m.helmetKey;
  const auto *rx = role_ == Paw ? m.helmetKey : m.pawKey;
  const bool ok = (role_ == Paw || role_ == Helmet) && !zero(tx, 32) &&
                  !zero(rx, 32) && !equal(tx, rx, 32) && !zero(m.id, 8) &&
                  crypto.hash(tx, 32, a) && crypto.hash(rx, 32, b) &&
                  !equal(a, previousTx_, 32) && !equal(b, previousRx_, 32);
  clear();
  if (ok) {
    std::memcpy(tx_, tx, 32);
    std::memcpy(rx_, rx, 32);
    std::memcpy(id_, m.id, 8);
    std::memcpy(previousTx_, a, 32);
    std::memcpy(previousRx_, b, 32);
    active_ = true;
  }
  crypto.wipe(a, 32);
  crypto.wipe(b, 32);
  return ok;
}
bool Traffic::seal(const HelmetLink::Packet *p, bool renew, Frame &f) {
  if (!active_ || records_ >= RecordLimit || sequence_ >= SequenceLimit ||
      (p && (!applicationValid(*p, role_) || renew)))
    return false;
  uint8_t plain[92]{};
  if (!crypto.random(plain, 92)) {
    crypto.wipe(plain, 92);
    return false;
  }
  const size_t n = p ? HelmetLink::pack(*p, plain + 2, 90) : 0;
  if (p && !n) {
    crypto.wipe(plain, 92);
    return false;
  }
  write16(plain, uint16_t(n));
  f = Frame{};
  f.bytes[0] = 0x70;
  f.bytes[1] = Version;
  f.bytes[2] = uint8_t(renew ? Kind::Renew : Kind::Data);
  f.bytes[3] = role_;
  std::memcpy(f.bytes + 4, id_, 8);
  write64(f.bytes + 12, ++sequence_);
  uint8_t nonce[12] = {0x70, Version, role_, 0};
  write64(nonce + 4, sequence_);
  const bool ok =
      crypto.gcm(true, tx_, nonce, f.bytes, 20, plain, 92, f.bytes + 20, 108);
  crypto.wipe(plain, 92);
  crypto.wipe(nonce, 12);
  if (ok)
    ++records_;
  else
    crypto.wipe(&f, sizeof(f));
  return ok;
}
bool Traffic::open(const Frame &f, HelmetLink::Packet &packet,
                   bool &application, bool &renew) {
  application = renew = false;
  const auto *b = f.bytes;
  const uint8_t peer = role_ == Paw ? Helmet : Paw;
  const uint64_t seq = read64(b + 12);
  if (!active_ || records_ >= RecordLimit || b[0] != 0x70 || b[1] != Version ||
      b[3] != peer ||
      (b[2] != uint8_t(Kind::Data) && b[2] != uint8_t(Kind::Renew)) ||
      !equal(b + 4, id_, 8) || !seq || seq > SequenceLimit ||
      (seq <= top_ &&
       (top_ - seq >= 64 || (window_ & (uint64_t(1) << (top_ - seq))))))
    return false;
  uint8_t plain[92]{}, nonce[12] = {0x70, Version, peer, 0};
  write64(nonce + 4, seq);
  bool ok = crypto.gcm(false, rx_, nonce, b, 20, b + 20, 108, plain, 92);
  if (ok)
    ++records_;
  const size_t n = read16(plain);
  HelmetLink::Packet decoded{};
  ok = ok && n <= HelmetLink::MaxRaw &&
       (n == 0 || (n >= HelmetLink::HeaderBytes + 2 &&
                   HelmetLink::unpack(plain + 2, n, decoded) &&
                   applicationValid(decoded, peer))) &&
       (b[2] != uint8_t(Kind::Renew) || n == 0);
  crypto.wipe(plain, 92);
  crypto.wipe(nonce, 12);
  if (!ok)
    return false;
  if (seq > top_) {
    const auto delta = seq - top_;
    window_ = delta >= 64 ? 1 : (window_ << delta) | 1;
    top_ = seq;
  } else
    window_ |= uint64_t(1) << (top_ - seq);
  application = n != 0;
  renew = b[2] == uint8_t(Kind::Renew);
  if (application)
    packet = decoded;
  return true;
}
} // namespace ProtogenV4
