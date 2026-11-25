#include "abi.h"
#include "PanelGeometry.h"
#include <assert.h>
#include <stdbool.h>
extern uint8_t nondet_byte(void);
extern int16_t nondet_coordinate(void);
extern uint16_t nondet_color(void);
int main(void) {
  uint8_t bitmap[112], before[112], positions[14], rotations[14];
  for (unsigned i = 0; i < 112; ++i) before[i] = bitmap[i] = nondet_byte();
  for (unsigned i = 0; i < 14; ++i) {
    positions[i] = nondet_byte();
    rotations[i] = nondet_byte();
    __CPROVER_assume(positions[i] < 14 && rotations[i] < 4);
  }
  uint8_t rotation = nondet_byte();
  __CPROVER_assume(rotation < 4);
  int16_t x = nondet_coordinate(), y = nondet_coordinate();
  int16_t width = rotation & 1 ? 8 : 112;
  int16_t height = rotation & 1 ? 112 : 8;
  uint16_t color = nondet_color();
  uint8_t previousDirty = nondet_byte(), dirty = previousDirty;
  protogenDrawPixel(bitmap, positions, rotations, 14, 112, 8,
                    width, height, rotation, x, y, color, &dirty);
  unsigned changes = 0;
  assert((dirty & previousDirty) == previousDirty);
  for (unsigned i = 0; i < sizeof(bitmap); ++i) {
    uint8_t difference = bitmap[i] ^ before[i];
    if (difference) {
      ++changes;
      assert((difference & (difference - 1)) == 0);
      assert(dirty == (previousDirty | (1u << (i % 8))));
      if (color) assert((bitmap[i] & difference) == difference);
      else assert((bitmap[i] & difference) == 0);
    }
  }
  assert(changes <= 1);
  if (!changes) assert(dirty == previousDirty);
  if (x < 0 || y < 0 || x >= width || y >= height) assert(changes == 0);
}
