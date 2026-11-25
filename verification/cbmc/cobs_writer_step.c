#include "abi.h"
#include "Cobs.h"
#include <assert.h>
extern size_t nondet_size(void);
extern uint8_t nondet_byte(void);
int main(void) {
  // UART's maximum raw frame is 90 bytes; no 254-byte run is possible.
  uint8_t out[92];
  size_t n = nondet_size(), i = nondet_size(), capacity = nondet_size();
  __CPROVER_assume(n <= 90 && i < n && capacity >= n + 2 && capacity <= sizeof(out));
  size_t codeAt = nondet_size();
  __CPROVER_assume(codeAt <= i);
  ProtogenCobsWriter s = {codeAt, i + 1, (uint8_t)(i + 1 - codeAt)};
  uint8_t byte = nondet_byte();
  const uint8_t oldCode = s.code;
  assert(protogenCobsWriteByte(&s, byte, out, capacity));
  assert(s.used == i + 2 && s.codeAt < s.used && s.code == s.used - s.codeAt);
  assert(s.used < capacity && s.code <= 91);
  if (!byte) {
    assert(out[codeAt] == oldCode && s.codeAt == i + 1 && s.code == 1);
  } else {
    assert(out[i + 1] == byte && s.codeAt == codeAt && s.code == oldCode + 1);
  }
  // Every reachable final state (including empty input) fits the frame.
  size_t endAt = nondet_size(), finalLength = nondet_size();
  __CPROVER_assume(finalLength <= 90 && endAt <= finalLength && finalLength + 2 <= capacity);
  ProtogenCobsWriter final = {endAt, finalLength + 1, (uint8_t)(finalLength + 1 - endAt)};
  assert(protogenCobsFinish(&final, out, capacity) == finalLength + 2);
  assert(out[finalLength + 1] == 0 && out[endAt] == final.code);
}
