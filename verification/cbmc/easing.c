#include "abi.h"
#include "EasingCore.h"
#include <assert.h>
extern uint8_t nondet_byte(void);
extern uint32_t nondet_u32(void);
int main(void) {
  // A duration of 256 visits every possible quantized phase exactly.
  uint8_t curve = nondet_byte();
  uint32_t phase = nondet_u32();
  __CPROVER_assume(phase <= 256);
  uint16_t value = protogenEase(curve, phase, 256);
  assert(value <= 256);
  assert(phase != 0 || value == 0);
  assert(phase != 256 || value == 256);
  if (phase < 256) assert(value <= protogenEase(curve, phase + 1, 256));
  assert(protogenEase(curve, phase, 0) == 256);
}
