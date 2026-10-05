#pragma once
#include "WirelessConfig.h"
#include "psa/crypto.h"
namespace Wireless {
namespace Crypto {
void wipe(void *p, size_t n);
bool equal(const void *a, const void *b, size_t n);
bool init();
bool random(void *out, size_t n);
bool hash(const void *in, size_t n, uint8_t out[32]);
bool hmac(const uint8_t key[32], const void *in, size_t n, uint8_t out[32]);
bool hkdf(const uint8_t *secret, size_t n, const uint8_t *salt, size_t saltSize,
          const char *label, uint8_t *out, size_t size);
bool gcm(bool encrypt, const uint8_t key[32], const uint8_t nonce[12],
         const uint8_t *aad, size_t aadSize, const uint8_t *in, size_t size,
         uint8_t *out, size_t capacity);
class Agreement {
  psa_key_id_t key = 0;

public:
  ~Agreement() { clear(); }
  void clear();
  bool generate(uint8_t publicKey[65]);
  bool derive(const uint8_t publicKey[65], uint8_t shared[32]);
};
} // namespace Crypto
} // namespace Wireless
