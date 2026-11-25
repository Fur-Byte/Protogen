#include "abi.h"
#include "PacketCore.h"
#include <assert.h>
extern size_t nondet_size(void);
extern uint8_t nondet_byte(void);
int main(void) {
  uint8_t input[ProtogenMaxRaw];
  for (unsigned i = 0; i < 4; ++i) input[i] = nondet_byte();
  size_t n = nondet_size();
  __CPROVER_assume(n <= sizeof(input));
  const bool expected = n >= 18 && n <= 90 && input[0] == 0x50 &&
    input[1] == 1 && input[2] >= 1 && input[2] <= 5 &&
    input[3] <= 72 && n == (size_t)input[3] + 18;
  assert(protogenPacketHeaderValid(input, n) == expected);
  uint8_t kind = nondet_byte(), length = nondet_byte();
  size_t capacity = nondet_size();
  size_t result = protogenPacketSize(kind, length, capacity);
  assert(result <= capacity);
  assert((result != 0) == (kind >= 1 && kind <= 5 && length <= 72 && capacity >= (size_t)length + 18));
  if (result) assert(result == (size_t)length + 18 && result >= 18 && result <= 90);
}
