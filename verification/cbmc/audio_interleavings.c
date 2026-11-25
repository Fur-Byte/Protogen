#include "abi.h"
#include "AudioRing.h"
#include <assert.h>
#include <stdbool.h>
extern uint16_t nondet_counter(void);
extern bool nondet_bool(void);
int main(void) {
  uint16_t read = nondet_counter(), write = nondet_counter();
  uint16_t count = protogenAudioCount(write,read);
  __CPROVER_assume(count <= ProtogenAudioRingSize);
  uint16_t oldRead = read, oldWrite = write;
  // Model one producer and one consumer with an atomic index publication.
  // Separate cells when both can proceed; either publication order preserves
  // occupancy. If the producer observes full or consumer empty, it defers.
  bool produce = count < ProtogenAudioRingSize && nondet_bool();
  bool consume = count > 0 && nondet_bool();
  if (produce && consume)
    assert(protogenAudioIndex(read) != protogenAudioIndex(write));
  if (nondet_bool()) {
    if (produce) write = protogenAudioNext(write);
    assert(protogenAudioCount(write,read) <= ProtogenAudioRingSize);
    if (consume) read = protogenAudioNext(read);
  } else {
    if (consume) read = protogenAudioNext(read);
    assert(protogenAudioCount(write,read) <= ProtogenAudioRingSize);
    if (produce) write = protogenAudioNext(write);
  }
  assert(protogenAudioCount(write,read) == count + (unsigned)produce - (unsigned)consume);
  assert(protogenAudioCount(write,read) <= ProtogenAudioRingSize);
  assert(read == (consume ? protogenAudioNext(oldRead) : oldRead));
  assert(write == (produce ? protogenAudioNext(oldWrite) : oldWrite));
}
