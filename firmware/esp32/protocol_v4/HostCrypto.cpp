#include "HostCrypto.hpp"
#include "Native.h"
#include "WirelessCrypto.h"
namespace ProtogenV4 {
bool HostCrypto::init() { return Wireless::Crypto::init(); }
void HostCrypto::wipe(void *p, size_t n) {
  ++wipes;
  wipedBytes += n;
  Wireless::Crypto::wipe(p, n);
}
bool HostCrypto::random(void *p, size_t n) {
  return Wireless::Crypto::random(p, n);
}
bool HostCrypto::hash(const void *p, size_t n, uint8_t out[32]) {
  return Wireless::Crypto::hash(p, n, out);
}
bool HostCrypto::sha3(const void *p, size_t n, uint8_t out[32]) {
  pqv4_sha3(out, static_cast<const uint8_t *>(p), n);
  return true;
}
bool HostCrypto::hmac(const uint8_t k[32], const void *p, size_t n,
                      uint8_t out[32]) {
  return Wireless::Crypto::hmac(k, p, n, out);
}
bool HostCrypto::hkdf(const uint8_t *k, size_t n, const uint8_t *s, size_t sn,
                      const char *l, uint8_t *o, size_t on) {
  return Wireless::Crypto::hkdf(k, n, s, sn, l, o, on);
}
bool HostCrypto::gcm(bool e, const uint8_t k[32], const uint8_t iv[12],
                     const uint8_t *a, size_t an, const uint8_t *p, size_t n,
                     uint8_t *o, size_t on) {
  return Wireless::Crypto::gcm(e, k, iv, a, an, p, n, o, on);
}
bool HostCrypto::generateP256(uint64_t &handle, uint8_t pub[65]) {
  destroyP256(handle);
  psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&a, 256);
  psa_set_key_usage_flags(&a, PSA_KEY_USAGE_DERIVE);
  psa_set_key_algorithm(&a, PSA_ALG_ECDH);
  psa_key_id_t id = 0;
  size_t n = 0;
  bool ok = psa_generate_key(&a, &id) == PSA_SUCCESS &&
            psa_export_public_key(id, pub, 65, &n) == PSA_SUCCESS && n == 65;
  psa_reset_key_attributes(&a);
  if (!ok) {
    psa_destroy_key(id);
    wipe(pub, 65);
    return false;
  }
  handle = id;
  return true;
}
bool HostCrypto::ecdh(uint64_t h, const uint8_t pub[65], uint8_t out[32]) {
  size_t n = 0;
  bool ok = h && h <= UINT32_MAX &&
            psa_raw_key_agreement(PSA_ALG_ECDH, psa_key_id_t(h), pub, 65, out,
                                  32, &n) == PSA_SUCCESS &&
            n == 32;
  if (!ok)
    wipe(out, 32);
  return ok;
}
void HostCrypto::destroyP256(uint64_t &h) {
  if (h && h <= UINT32_MAX)
    psa_destroy_key(psa_key_id_t(h));
  h = 0;
}
int NativeKem::execute(Operation op, KemBuffers &b) {
  return pqv4_native(
      unsigned(op), b.pk, b.sk, b.ct, b.ss,
      [](void *c, uint8_t *p, size_t n) -> int {
        return static_cast<Crypto *>(c)->random(p, n) ? 0 : -1;
      },
      &crypto);
}
} // namespace ProtogenV4
