#include "abi.h"
#include "PanelGeometry.h"
#include <stdbool.h>
#include <assert.h>
extern uint8_t nondet_byte(void);
extern int16_t nondet_coordinate(void);
extern uint16_t nondet_color(void);
extern _Bool nondet_bool(void);
int main(void) {
  uint8_t bitmap[112] = {0};
  uint8_t positions[14], rotations[14];
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
  uint8_t dirty = 0;
  bool present = nondet_bool();
  protogenDrawPixel(present ? &bitmap[0] : (uint8_t *)0, positions, rotations, 14, 112, 8,
                    width, height, rotation, x, y, color, &dirty);
  bool visible = present && x >= 0 && y >= 0 && x < width && y < height;
  assert((dirty != 0) == (visible && color != 0));
  if (dirty) assert((dirty & (dirty - 1)) == 0);
  unsigned lit = 0;
  for (unsigned i = 0; i < sizeof(bitmap); ++i) {
    if (bitmap[i]) {
      ++lit;
      assert((bitmap[i] & (bitmap[i] - 1)) == 0);
    }
  }
  assert(lit == (visible && color != 0 ? 1u : 0u));
}
