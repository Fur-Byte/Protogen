#pragma once
#include <stddef.h>
#include <stdint.h>

static inline uint64_t protogenBigEndian(const uint8_t *bytes, size_t count) {
  uint64_t value = 0;
  for (size_t i = 0; i < count; ++i) value = (value << 8) | bytes[i];
  return value;
}

static inline int32_t protogenSignedCurrent(const uint8_t bytes[3]) {
  const int32_t value = (int32_t)(protogenBigEndian(bytes, 3) >> 4);
  return (value & 0x80000) ? value - 0x100000 : value;
}
