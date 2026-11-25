#include "abi.h"
#include "ReplayCore.h"
#include <assert.h>
extern uint64_t nondet_sequence(void);
int main(void) {
  uint64_t top = nondet_sequence(), bits = nondet_sequence();
  uint64_t sequence = nondet_sequence(), old = nondet_sequence();
  __CPROVER_assume(top <= PROTOGEN_MAX_SEQUENCE);
  // For any rejected sequence, a legal accept cannot make it admissible.
  bool rejected = !protogenReplayAllows(top, bits, old);
  if (protogenReplayAllows(top, bits, sequence)) {
    uint64_t previous = top;
    protogenReplayAccept(&top, &bits, sequence);
    assert(top >= previous && top <= PROTOGEN_MAX_SEQUENCE);
    assert(!protogenReplayAllows(top, bits, sequence));
    assert(!rejected || !protogenReplayAllows(top, bits, old));
  }
}
