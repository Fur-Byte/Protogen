#pragma once
#include "Interfaces.hpp"
namespace ProtogenV4 {
// Host reference adapter only. Never linked into a production firmware target.
class HostCrypto : public Crypto {
public:
  uint64_t wipes = 0, wipedBytes = 0;
  bool init();
  void wipe(void *, size_t) override;
  bool random(void *, size_t) override;
  bool hash(const void *, size_t, uint8_t[32]) override;
  bool sha3(const void *, size_t, uint8_t[32]) override;
  bool hmac(const uint8_t[32], const void *, size_t, uint8_t[32]) override;
  bool hkdf(const uint8_t *, size_t, const uint8_t *, size_t, const char *,
            uint8_t *, size_t) override;
  bool gcm(bool, const uint8_t[32], const uint8_t[12], const uint8_t *, size_t,
           const uint8_t *, size_t, uint8_t *, size_t) override;
  bool generateP256(uint64_t &, uint8_t[65]) override;
  bool ecdh(uint64_t, const uint8_t[65], uint8_t[32]) override;
  void destroyP256(uint64_t &) override;
};
class NativeKem final : public Kem {
  Crypto &crypto;

public:
  explicit NativeKem(Crypto &c) : crypto(c) {}
  int execute(Operation, KemBuffers &) override;
};
} // namespace ProtogenV4
