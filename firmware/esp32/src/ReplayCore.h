#pragma once
#include <stdbool.h>
#include <stdint.h>

// Shared by the C++ replay window and the bounded verification harness.
#define PROTOGEN_REPLAY_WIDTH 64u
#define PROTOGEN_MAX_SEQUENCE UINT64_C(0x7fffffffffffffff)

static inline bool protogenReplayAllows(uint64_t top, uint64_t bits, uint64_t sequence) {
  if (!sequence || sequence > PROTOGEN_MAX_SEQUENCE) return false;
  if (sequence > top) return true;
  const uint64_t age = top - sequence;
  return age < PROTOGEN_REPLAY_WIDTH && !(bits & (UINT64_C(1) << age));
}

// Precondition: protogenReplayAllows(top, bits, sequence), followed by authentication
// and application decoding. State is owned by the caller.
static inline void protogenReplayAccept(uint64_t *top, uint64_t *bits, uint64_t sequence) {
  if (sequence > *top) {
    const uint64_t delta = sequence - *top;
    *bits = delta >= PROTOGEN_REPLAY_WIDTH ? 1 : (*bits << delta) | 1;
    *top = sequence;
  } else {
    *bits |= UINT64_C(1) << (*top - sequence);
  }
}
