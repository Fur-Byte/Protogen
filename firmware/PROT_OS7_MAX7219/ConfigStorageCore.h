#pragma once
#include <stdbool.h>
#include <stdint.h>

// Return the CRC offset only for a supported record format. The caller
// checks the magic and CRC before reading payload fields.
static inline int protogenConfigCrcOffset(uint32_t format, uint32_t payload) {
  if ((format == 1 && payload == 30) ||
      (format == 2 && payload == 34) ||
      (format == 3 && payload == 36))
    return 12 + (int)payload;
  return -1;
}

static inline int protogenNewestConfig(bool aValid, uint32_t a,
                                       bool bValid, uint32_t b) {
  if (!aValid) return bValid ? 1 : -1;
  if (!bValid) return 0;
  // Half-range modular ordering, including rollover and ties.
  const uint32_t delta = b - a;
  return delta != 0 && delta < UINT32_C(0x80000000) ? 1 : 0;
}
