#include "abi.h"
#include "NonceCore.h"
#include <assert.h>
#include <string.h>
extern uint64_t nondet_sequence(void);
extern uint8_t nondet_byte(void);
int main(void) {
  uint8_t a[14], b[14];
  a[0] = a[13] = b[0] = b[13] = 0xa5;
  uint64_t seqA = nondet_sequence(), seqB = nondet_sequence();
  uint8_t roleA = nondet_byte(), roleB = nondet_byte();
  uint8_t versionA = nondet_byte(), versionB = nondet_byte();
  protogenNonce(a + 1, versionA, roleA, seqA);
  protogenNonce(b + 1, versionB, roleB, seqB);
  assert(a[0] == 0xa5 && a[13] == 0xa5 && b[0] == 0xa5 && b[13] == 0xa5);
  assert(a[1] == 0x70 && a[2] == versionA && a[3] == roleA && a[4] == 0);
  assert(protogenRead64(a + 5) == seqA);
  assert((memcmp(a + 1, b + 1, 12) == 0) ==
         (seqA == seqB && roleA == roleB && versionA == versionB));
}
