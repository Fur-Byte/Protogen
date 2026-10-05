#include "IdentityRecovery.h"
namespace Wireless {
bool randomIdentity(Mac &m, uint8_t role) {
  if (!Crypto::random(m.bytes, 6))
    return false;
  m.bytes[0] = (m.bytes[0] & 0xfc) | 2;
  m.bytes[5] = (m.bytes[5] & 0xfe) | (role == Paw ? 1 : 0);
  return true;
}
bool Recovery::start(const uint8_t key[32], uint8_t r, uint32_t now) {
  uint8_t derived[32];
  if (!Crypto::hkdf(key, 32, nullptr, 0, "protogen/v2/recovery/channel-order",
                    derived, 32))
    return false;
  for (unsigned i = 0; i < ChannelCount; ++i)
    order[i] = Channels[i];
  // Keyed permutation, no shared wall-clock required. Asymmetric dwell makes
  // the paw sweep every channel while the helmet listens on a single one.
  for (unsigned i = ChannelCount - 1; i > 0; --i) {
    const unsigned j = derived[i] % (i + 1);
    const auto tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
  Crypto::wipe(derived, sizeof(derived));
  role = r;
  started = now;
  initialized = true;
  return true;
}
uint8_t Recovery::channel(uint32_t now) const {
  return initialized ? order[((now - started) /
                              (role == Paw ? PawDwellMs : HelmetDwellMs)) %
                             ChannelCount]
                     : PairChannel;
}
} // namespace Wireless
