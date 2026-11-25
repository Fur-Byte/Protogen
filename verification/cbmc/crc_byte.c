#include "abi.h"
#include "PacketCore.h"
#include <assert.h>
extern uint16_t nondet_word(void);
extern uint8_t nondet_byte(void);
int main(void) {
  uint16_t crc = nondet_word();
  uint8_t byte = nondet_byte();
  // Polynomial long division of the 24-bit dividend, using the full
  // generator x^16 + x^12 + x^5 + 1 rather than a 16-bit recurrence.
  uint32_t dividend = ((uint32_t)crc ^ ((uint32_t)byte << 8)) << 8;
  for (int bit = 23; bit >= 16; --bit)
    if (dividend & (UINT32_C(1) << bit))
      dividend ^= UINT32_C(0x11021) << (bit - 16);
  assert(dividend <= UINT16_MAX);
  assert(protogenCrc16Byte(crc, byte) == dividend);
}
