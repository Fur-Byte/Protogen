#include "abi.h"
#include "WireIntegers.h"
#include <assert.h>
extern uint16_t nondet_u16(void);
extern uint32_t nondet_u32(void);
int main(void) {
  uint16_t a = nondet_u16();
  uint32_t b = nondet_u32();
  uint8_t bytes[6] = {0xa5, 0, 0, 0, 0, 0xa5};
  protogenWrite16(bytes + 1, a);
  assert(protogenRead16(bytes + 1) == a);
  assert(bytes[1] == a % 256u && bytes[2] == a / 256u);
  assert(bytes[0] == 0xa5 && bytes[3] == 0 && bytes[4] == 0 && bytes[5] == 0xa5);
  protogenWrite32(bytes + 1, b);
  assert(protogenRead32(bytes + 1) == b);
  for (unsigned i = 0; i < 4; ++i) assert(bytes[i + 1] == (b >> (8 * i)) % 256u);
  assert(bytes[0] == 0xa5 && bytes[5] == 0xa5);
}
