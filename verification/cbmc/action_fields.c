#include "abi.h"
#include "PacketCore.h"
#include <assert.h>
extern uint8_t nondet_byte(void);
extern uint16_t nondet_word(void);
int main(void) {
  uint8_t action = nondet_byte(), value = nondet_byte();
  uint16_t duration = nondet_word();
  bool expected = false;
  switch (action) {
    case 1: expected = value <= 5 && duration == 0; break;
    case 2: expected = !value && duration >= 100 && duration <= 6000; break;
    case 3: case 4: expected = value <= 1 && duration == 0; break;
  }
  assert(protogenActionValid(action, value, duration) == expected);
}
