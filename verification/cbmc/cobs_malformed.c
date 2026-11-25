#include "abi.h"
#include "Cobs.h"
#include <assert.h>
#ifndef INPUT_LIMIT
#define INPUT_LIMIT 4
#endif
extern size_t nondet_size(void);
extern uint8_t nondet_byte(void);
int main(void) {
  uint8_t in[INPUT_LIMIT], out[INPUT_LIMIT + 2];
  for (size_t i = 0; i < sizeof(in); ++i) in[i] = nondet_byte();
  for (size_t i = 0; i < sizeof(out); ++i) out[i] = 0xa5;
  size_t n = nondet_size(), capacity = nondet_size();
  __CPROVER_assume(n <= sizeof(in) && capacity <= INPUT_LIMIT);
  size_t used = protogenCobsDecode(in, n, out + 1, capacity);
  assert(used <= capacity);
  assert(out[0] == 0xa5);
  for (size_t i = capacity + 1; i < sizeof(out); ++i) assert(out[i] == 0xa5);
  assert(protogenCobsDecode(0, 1, out, sizeof(out)) == 0);
  assert(protogenCobsDecode(in, n, 0, 0) == 0);
}
