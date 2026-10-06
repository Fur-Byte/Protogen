#include "WirelessCrypto.h"
#include "tf-psa-crypto/build_info.h"
#if !defined(PSA_WANT_ALG_HKDF) || !defined(PSA_WANT_ALG_GCM) ||               \
    !defined(PSA_WANT_ALG_ECDH) || !defined(PSA_WANT_ALG_HMAC) ||              \
    !defined(PSA_WANT_ALG_SHA_256)
#error "PSA HKDF-SHA256, AES-GCM, ECDH and HMAC-SHA256 are required"
#endif
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif
namespace Wireless::Crypto {
void wipe(void *p, size_t n) {
  volatile uint8_t *v = static_cast<volatile uint8_t *>(p);
  while (n--)
    *v++ = 0;
}
bool equal(const void *a, const void *b, size_t n) {
  const auto *x = static_cast<const uint8_t *>(a),
             *y = static_cast<const uint8_t *>(b);
  uint8_t v = 0;
  while (n--)
    v |= *x++ ^ *y++;
  return v == 0;
}
bool init() { return psa_crypto_init() == PSA_SUCCESS; }
bool random(void *p, size_t n) {
#ifdef ESP_PLATFORM
  // Wi-Fi must be started before this call, providing continuous hardware
  // entropy.
  esp_fill_random(p, n);
  return true;
#else
  return psa_generate_random(static_cast<uint8_t *>(p), n) == PSA_SUCCESS;
#endif
}
bool hash(const void *in, size_t n, uint8_t out[32]) {
  size_t size = 0;
  const bool okay =
      psa_hash_compute(PSA_ALG_SHA_256, static_cast<const uint8_t *>(in), n,
                       out, 32, &size) == PSA_SUCCESS &&
      size == 32;
  if (!okay)
    wipe(out, 32);
  return okay;
}
bool hmac(const uint8_t key[32], const void *in, size_t n, uint8_t out[32]) {
  psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&a, PSA_KEY_TYPE_HMAC);
  psa_set_key_bits(&a, 256);
  psa_set_key_usage_flags(&a, PSA_KEY_USAGE_SIGN_MESSAGE);
  psa_set_key_algorithm(&a, PSA_ALG_HMAC(PSA_ALG_SHA_256));
  psa_key_id_t id = 0;
  size_t size = 0;
  const bool imported = psa_import_key(&a, key, 32, &id) == PSA_SUCCESS;
  const bool okay = imported &&
                    psa_mac_compute(id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                                    static_cast<const uint8_t *>(in), n, out,
                                    32, &size) == PSA_SUCCESS &&
                    size == 32;
  psa_destroy_key(id);
  if (!okay)
    wipe(out, 32);
  return okay;
}
bool hkdf(const uint8_t *secret, size_t n, const uint8_t *salt, size_t saltSize,
          const char *label, uint8_t *out, size_t size) {
  psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
  const bool okay =
      psa_key_derivation_setup(&op, PSA_ALG_HKDF(PSA_ALG_SHA_256)) ==
          PSA_SUCCESS &&
      psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, salt,
                                     saltSize) == PSA_SUCCESS &&
      psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SECRET,
                                     secret, n) == PSA_SUCCESS &&
      psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_INFO,
                                     reinterpret_cast<const uint8_t *>(label),
                                     strlen(label)) == PSA_SUCCESS &&
      psa_key_derivation_output_bytes(&op, out, size) == PSA_SUCCESS;
  psa_key_derivation_abort(&op);
  if (!okay)
    wipe(out, size);
  return okay;
}
bool gcm(bool encrypt, const uint8_t key[32], const uint8_t nonce[12],
         const uint8_t *aad, size_t aadSize, const uint8_t *in, size_t size,
         uint8_t *out, size_t capacity) {
  if ((encrypt && (size > PlainBytes || capacity < size + TagBytes)) ||
      (!encrypt && (size < TagBytes || size > PlainBytes + TagBytes ||
                    capacity < size - TagBytes)))
    return false;
  psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&a, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&a, 256);
  psa_set_key_usage_flags(&a, encrypt ? PSA_KEY_USAGE_ENCRYPT
                                      : PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&a, PSA_ALG_GCM);
  psa_key_id_t id = 0;
  size_t written = 0;
  bool okay = psa_import_key(&a, key, 32, &id) == PSA_SUCCESS;
  if (okay)
    okay = (encrypt ? psa_aead_encrypt(id, PSA_ALG_GCM, nonce, 12, aad, aadSize,
                                       in, size, out, capacity, &written)
                    : psa_aead_decrypt(id, PSA_ALG_GCM, nonce, 12, aad, aadSize,
                                       in, size, out, capacity, &written)) ==
           PSA_SUCCESS;
  psa_destroy_key(id);
  okay = okay && written == (encrypt ? size + TagBytes : size - TagBytes);
  if (!okay)
    wipe(out, capacity);
  return okay;
}
void Agreement::clear() {
  psa_destroy_key(key);
  key = 0;
}
bool Agreement::generate(uint8_t publicKey[65]) {
  clear();
  psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&a, 256);
  psa_set_key_lifetime(&a, PSA_KEY_LIFETIME_VOLATILE);
  psa_set_key_usage_flags(&a, PSA_KEY_USAGE_DERIVE);
  psa_set_key_algorithm(&a, PSA_ALG_ECDH);
  size_t n = 0;
  const bool okay =
      psa_generate_key(&a, &key) == PSA_SUCCESS &&
      psa_export_public_key(key, publicKey, 65, &n) == PSA_SUCCESS && n == 65;
  if (!okay) {
    clear();
    wipe(publicKey, 65);
  }
  return okay;
}
bool Agreement::derive(const uint8_t publicKey[65], uint8_t shared[32]) {
  size_t n = 0;
  const bool okay = key && publicKey[0] == 4 &&
                    psa_raw_key_agreement(PSA_ALG_ECDH, key, publicKey, 65,
                                          shared, 32, &n) == PSA_SUCCESS &&
                    n == 32;
  if (!okay)
    wipe(shared, 32);
  return okay;
}
} // namespace Wireless::Crypto
