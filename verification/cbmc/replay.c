#include "abi.h"
#include "ReplayCore.h"
#include <assert.h>
extern uint64_t nondet_sequence(void);
int main(void) {
  uint64_t top = 0, bits = 0;
  uint64_t accepted[3];
  unsigned count = 0;
  for (unsigned i = 0; i < 3; ++i) {
    uint64_t sequence = nondet_sequence();
    assert(!protogenReplayAllows(top, bits, 0));
    assert(!protogenReplayAllows(top, bits, PROTOGEN_MAX_SEQUENCE + 1));
    if (protogenReplayAllows(top, bits, sequence)) {
      uint64_t before = top;
      protogenReplayAccept(&top, &bits, sequence);
      assert(top >= before && top <= PROTOGEN_MAX_SEQUENCE);
      accepted[count++] = sequence;
    }
    for (unsigned j = 0; j < count; ++j) {
      assert(!protogenReplayAllows(top, bits, accepted[j]));
    }
  }
}
