#include "abi.h"
#include "ConfigStorageCore.h"
#include <assert.h>
extern uint32_t nondet_u32(void);
extern bool nondet_bool(void);
int main(void) {
  uint32_t format = nondet_u32(), payload = nondet_u32();
  int crc = protogenConfigCrcOffset(format, payload);
  assert((crc >= 0) == ((format == 1 && payload == 30) ||
                       (format == 2 && payload == 34) ||
                       (format == 3 && payload == 36)));
  if (crc >= 0) {
    assert(crc >= 42 && crc + 4 < 53 && crc + 4 < 64);
    assert((uint32_t)crc == 12 + payload);
  }
  uint32_t a = nondet_u32(), b = nondet_u32();
  bool av = nondet_bool(), bv = nondet_bool();
  int selected = protogenNewestConfig(av, a, bv, b);
  assert(selected >= -1 && selected <= 1);
  assert((selected == -1) == (!av && !bv));
  if (selected == 0) assert(av);
  if (selected == 1) assert(bv);
  if (av && bv) {
    assert(selected == ((b-a) > 0 && (b-a) < UINT32_C(0x80000000) ? 1 : 0));
    if (b == a + (uint32_t)1) assert(selected == 1);
  }
}
