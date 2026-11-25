#include "abi.h"
#include "SensorMathCore.h"
#include <assert.h>
extern uint8_t nondet_byte(void);
int main(void) {
  uint8_t bytes[3] = {nondet_byte(), nondet_byte(), nondet_byte()};
  int32_t raw = (int32_t)bytes[0] * 4096 + (int32_t)bytes[1] * 16 + bytes[2] / 16;
  int32_t current = protogenSignedCurrent(bytes);
  assert(current >= -524288 && current <= 524287);
  assert(current == raw - (bytes[0] >= 128 ? 1048576 : 0));
  assert(protogenBigEndian(bytes, 3) == (uint64_t)bytes[0] * 65536 + (uint64_t)bytes[1] * 256 + bytes[2]);
}
