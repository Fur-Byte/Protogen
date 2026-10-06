#include "Fragments.hpp"
#include <cstring>
namespace ProtogenV4 {
bool structuralFragment(const Frame &f, const Identity &source, Mode mode,
                        uint64_t wire, Object expected) {
  const auto *b = f.bytes;
  const unsigned i = b[6];
  if (unsigned(expected) >= ObjectCount || !wire || b[0] != 0x70 ||
      b[1] != Version || b[2] != uint8_t(Kind::Fragment) ||
      b[3] != objectRole(expected) || b[4] != uint8_t(mode) ||
      b[5] != uint8_t(expected) || b[7] != fragments(expected) ||
      i >= fragments(expected) || read16(b + 8) != objectSize(expected) ||
      b[11] || !zero(b + 26, 2) || read64(b + 12) != wire ||
      !equal(b + 20, source.bytes, 6))
    return false;
  const size_t n = objectSize(expected) - i * FragmentPayload;
  const size_t length = n < FragmentPayload ? n : FragmentPayload;
  return b[10] == length && zero(b + 28 + length, FragmentPayload - length);
}
void Reassembly::clear() {
  crypto.wipe(data_, sizeof(data_));
  crypto.wipe(&descriptor_, sizeof(descriptor_));
  crypto.wipe(commitment_, sizeof(commitment_));
  received_ = 0;
  expires_ = 0;
}
FragmentStatus Reassembly::accept(const Frame &f, const Identity &source,
                                  Mode mode, uint64_t wire, Object object,
                                  const uint8_t *key, uint64_t now) {
  if (expired(now)) {
    clear();
    return FragmentStatus::Expired;
  }
  if (!structuralFragment(f, source, mode, wire, object))
    return FragmentStatus::Rejected;
  uint8_t tag[32]{};
  if (key) {
    const bool valid =
        crypto.hmac(key, f.bytes, 96, tag) && equal(tag, f.bytes + 96, 32);
    crypto.wipe(tag, 32);
    if (!valid)
      return FragmentStatus::Rejected;
  }
  Frame descriptor = f;
  descriptor.bytes[6] = descriptor.bytes[10] = 0;
  if (active() && !equal(descriptor_.bytes, descriptor.bytes, 28)) {
    clear();
    return FragmentStatus::Conflict;
  }
  if (!active()) {
    if (now > UINT64_MAX - 1500)
      return FragmentStatus::Rejected;
    descriptor_ = descriptor;
    std::memcpy(commitment_, f.bytes + 96, 32);
    expires_ = now + 1500;
  }
  if (!key && !equal(commitment_, f.bytes + 96, 32)) {
    clear();
    return FragmentStatus::Conflict;
  }
  const unsigned index = f.bytes[6];
  const size_t offset = index * FragmentPayload, length = f.bytes[10];
  if (received_ & (uint32_t(1) << index)) {
    if (!equal(data_ + offset, f.bytes + 28, length)) {
      clear();
      return FragmentStatus::Conflict;
    }
  } else {
    std::memcpy(data_ + offset, f.bytes + 28, length);
    received_ |= uint32_t(1) << index;
  }
  if (received_ != ((uint32_t(1) << fragments(object)) - 1))
    return FragmentStatus::Accepted;
  if (!key) {
    const bool valid = crypto.hash(data_, objectSize(object), tag) &&
                       equal(tag, commitment_, 32);
    crypto.wipe(tag, 32);
    if (!valid) {
      clear();
      return FragmentStatus::Conflict;
    }
  }
  return FragmentStatus::Complete;
}
bool makeFragment(Crypto &c, const Exchange &x, Object object, unsigned index,
                  const uint8_t *key, Frame &f) {
  if (unsigned(object) >= ObjectCount || index >= fragments(object) ||
      !(x.complete & (1u << unsigned(object))))
    return false;
  objectHeader(f, x.mode, x.wire, objectRole(object) == Paw ? x.paw : x.helmet,
               object);
  f.bytes[6] = uint8_t(index);
  const size_t left = objectSize(object) - index * FragmentPayload,
               n = left < FragmentPayload ? left : FragmentPayload;
  f.bytes[10] = uint8_t(n);
  std::memcpy(f.bytes + 28,
              x.objects[unsigned(object)] + index * FragmentPayload, n);
  const bool ok = key ? c.hmac(key, f.bytes, 96, f.bytes + 96)
                      : c.hash(x.objects[unsigned(object)], objectSize(object),
                               f.bytes + 96);
  if (!ok)
    c.wipe(&f, sizeof(f));
  return ok;
}
} // namespace ProtogenV4
