#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WireIntegers.h"

enum { ProtogenHeaderBytes = 16, ProtogenMaxPayload = 72,
       ProtogenMaxRaw = ProtogenHeaderBytes + ProtogenMaxPayload + 2 };

static inline uint16_t protogenCrc16Byte(uint16_t crc, uint8_t byte) {
  crc ^= (uint16_t)byte << 8;
  for (unsigned bit = 0; bit < 8; ++bit)
    crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
  return crc;
}

static inline uint16_t protogenCrc16(const uint8_t *p, size_t n) {
  uint16_t crc = 0xffff;
  while (n--) crc = protogenCrc16Byte(crc, *p++);
  return crc;
}

static inline size_t protogenPacketSize(uint8_t kind, uint8_t length, size_t capacity) {
  const size_t n = ProtogenHeaderBytes + length + 2;
  return kind >= 1 && kind <= 5 && length <= ProtogenMaxPayload && capacity >= n ? n : 0;
}

static inline bool protogenPacketHeaderValid(const uint8_t *in, size_t n) {
  return n >= ProtogenHeaderBytes + 2 && n <= ProtogenMaxRaw &&
         in[0] == 0x50 && in[1] == 1 &&
         protogenPacketSize(in[2], in[3], n) == n;
}

static inline bool protogenPacketValid(const uint8_t *in, size_t n) {
  return protogenPacketHeaderValid(in, n) &&
         protogenRead16(in + n - 2) == protogenCrc16(in, n - 2);
}

// Identical command constraints at the RF application boundary and the UART
// admission gate. Epoch, sequence, ticket, and authentication remain separate.
static inline bool protogenActionValid(uint8_t action, uint8_t value, uint16_t duration) {
  return action >= 1 && action <= 4 &&
         (action != 1 || value < 6) &&
         ((action != 3 && action != 4) || value < 2) &&
         (action != 2 || (value == 0 && duration >= 100 && duration <= 6000)) &&
         (action == 2 || duration == 0);
}
