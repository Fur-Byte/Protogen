#include "abi.h"
#include "Cobs.h"
#include <assert.h>
extern size_t nondet_size(void);
extern uint8_t nondet_byte(void);
int main(void) {
  uint8_t in[91], out[90];
  size_t n = nondet_size(), cap = nondet_size();
  size_t i = nondet_size(), used = nondet_size();
  __CPROVER_assume(n <= sizeof(in) && cap <= sizeof(out) && i <= n && used <= cap);
  uint8_t code = nondet_byte();
  const bool fits = protogenCobsBlockFits(code, n-i, cap-used);
  assert(fits == (code > 0 && (size_t)(code-1) <= n-i && (size_t)(code-1) <= cap-used));
  if (fits) {
    size_t offset = nondet_size();
    __CPROVER_assume(offset < (size_t)(code-1));
    size_t readAt = i + offset, writeAt = used + offset;
    assert(readAt < n && writeAt < cap);
    uint8_t byte = nondet_byte();
    in[readAt] = byte;
    assert(protogenCobsCopyByte(in, &readAt, out, &writeAt) == (byte != 0));
    if (byte) {
      assert(readAt == i + offset + 1 && writeAt == used + offset + 1);
      assert(out[writeAt-1] == byte);
    } else assert(readAt == i + offset && writeAt == used + offset);
    assert(readAt <= n && writeAt <= cap);
  }
}
