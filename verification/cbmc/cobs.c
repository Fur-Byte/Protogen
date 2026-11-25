#include "abi.h"
#include "Cobs.h"
#include <assert.h>
#ifndef INPUT_LIMIT
#define INPUT_LIMIT 4
#endif
extern size_t nondet_size(void);
extern uint8_t nondet_byte(void);
int main(void) {
  uint8_t in[INPUT_LIMIT], encoded[INPUT_LIMIT + 2], decoded[INPUT_LIMIT];
  for (size_t i = 0; i < INPUT_LIMIT; ++i) in[i] = nondet_byte();
  size_t n = nondet_size();
  __CPROVER_assume(n <= INPUT_LIMIT);
  size_t capacity = nondet_size();
  __CPROVER_assume(capacity <= sizeof(encoded));
  size_t used = protogenCobsEncode(in, n, encoded, capacity);
  assert(used <= capacity);
  if (capacity >= n + 2) {
    assert(used == n + 2);
    assert(encoded[used - 1] == 0);
    assert(protogenCobsDecode(encoded, used - 1, decoded, sizeof(decoded)) == n);
    for (size_t i = 0; i < n; ++i) assert(decoded[i] == in[i]);
  } else assert(used == 0);
}
