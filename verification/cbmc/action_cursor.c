#include "abi.h"
#include "ActionQueueCore.h"
#include <assert.h>
extern unsigned nondet_unsigned(void);
int main(void) {
  ProtogenActionCursor c = {nondet_unsigned(), nondet_unsigned(), nondet_unsigned()};
  __CPROVER_assume(c.read < 4 && c.write < 4 && c.count <= 4 &&
                   c.write == (c.read + c.count) % 4);
  ProtogenActionCursor before = c;
  unsigned slot = 99;
  bool okay = protogenActionReserve(&c, &slot);
  assert(okay == (before.count < 4));
  if (okay) {
    assert(slot == before.write && slot < 4 && c.count == before.count + 1 &&
           c.read == before.read && c.write == (before.write + 1) % 4);
    // Reserving cannot reuse a still occupied position.
    unsigned occupied = nondet_unsigned();
    if (occupied < before.count) assert(slot != (before.read + occupied) % 4);
  } else {
    assert(c.read == before.read && c.write == before.write && c.count == before.count && slot == 99);
  }
  assert(c.count <= 4 && c.write == (c.read + c.count) % 4);
  c = before;
  if (c.count) {
    protogenActionRemove(&c);
    assert(c.count == before.count - 1 && c.read == (before.read + 1) % 4 && c.write == before.write);
    assert(c.write == (c.read + c.count) % 4);
  }
}
