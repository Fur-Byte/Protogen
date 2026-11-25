#pragma once
#include <stdbool.h>

enum { ProtogenActionSlots = 4 };
typedef struct {
  unsigned read, write, count;
} ProtogenActionCursor;

static inline bool protogenActionReserve(ProtogenActionCursor *cursor, unsigned *slot) {
  if (cursor->count == ProtogenActionSlots) return false;
  *slot = cursor->write;
  cursor->write = (cursor->write + 1) % ProtogenActionSlots;
  ++cursor->count;
  return true;
}
static inline void protogenActionRemove(ProtogenActionCursor *cursor) {
  cursor->read = (cursor->read + 1) % ProtogenActionSlots;
  --cursor->count;
}
