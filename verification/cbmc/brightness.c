#include "abi.h"
#include "BrightnessCore.h"
#include <assert.h>
extern uint32_t nondet_u32(void);
extern uint8_t nondet_byte(void);
int main(void) {
  uint32_t requested = nondet_u32(), approval = nondet_u32();
  uint8_t ceiling = nondet_byte();
  __CPROVER_assume(ceiling <= 15);
  uint8_t brightness = protogenBrightness(requested, approval, ceiling);
  assert(brightness <= requested && brightness <= approval && brightness <= ceiling);
  assert(brightness == requested || brightness == approval || brightness == ceiling);
  assert(protogenBrightness(requested, approval, 1) <= 1);
}
