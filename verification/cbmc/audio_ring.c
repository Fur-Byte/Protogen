#include "abi.h"
#include "AudioRing.h"
#include <assert.h>
extern uint16_t nondet_counter(void);
int main(void) {
  uint16_t read = nondet_counter(), write = nondet_counter();
  uint16_t count = protogenAudioCount(write, read);
  __CPROVER_assume(count <= ProtogenAudioRingSize);
  assert(protogenAudioIndex(read) < ProtogenAudioRingSize);
  assert(protogenAudioIndex(write) < ProtogenAudioRingSize);
  assert((protogenAudioIndex(read) == protogenAudioIndex(write)) ==
         (count == 0 || count == ProtogenAudioRingSize));
  if (count < ProtogenAudioRingSize) {
    uint16_t next = protogenAudioNext(write);
    assert(protogenAudioCount(next, read) == count + 1);
    if (count) assert(protogenAudioIndex(read) != protogenAudioIndex(write));
  }
  if (count) {
    uint16_t next = protogenAudioNext(read);
    assert(protogenAudioCount(write, next) == count - 1);
  }
}
