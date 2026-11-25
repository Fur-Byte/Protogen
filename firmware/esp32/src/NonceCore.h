#pragma once
#include <stdint.h>

static inline void protogenWrite64(uint8_t *p, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (8 * i));
}
static inline uint64_t protogenRead64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (8 * i);
  return value;
}
// The caller owns key/session uniqueness and the sequence lifecycle.
static inline void protogenNonce(uint8_t *nonce, uint8_t version, uint8_t role, uint64_t sequence) {
  nonce[0] = 0x70;
  nonce[1] = version;
  nonce[2] = role;
  nonce[3] = 0;
  protogenWrite64(nonce + 4, sequence);
}
