#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  size_t codeAt, used;
  uint8_t code;
} ProtogenCobsWriter;

// Each successful step preserves a pending code byte and advances used.
// The caller reserves the initial code slot and checks the minimum capacity.
static inline bool protogenCobsWriteByte(ProtogenCobsWriter *s, uint8_t byte,
                                        uint8_t *out, size_t capacity) {
  if (byte == 0) {
    out[s->codeAt] = s->code;
    if (s->used >= capacity) return false;
    s->codeAt = s->used++;
    s->code = 1;
  } else {
    if (s->used >= capacity) return false;
    out[s->used++] = byte;
    if (++s->code == 0xff) {
      out[s->codeAt] = s->code;
      if (s->used >= capacity) return false;
      s->codeAt = s->used++;
      s->code = 1;
    }
  }
  return true;
}
static inline size_t protogenCobsFinish(const ProtogenCobsWriter *s, uint8_t *out,
                                        size_t capacity) {
  out[s->codeAt] = s->code;
  if (s->used >= capacity) return 0;
  out[s->used] = 0;
  return s->used + 1;
}

static inline size_t protogenCobsEncode(const uint8_t *in, size_t n, uint8_t *out, size_t capacity) {
  // The minimum includes the initial code and delimiter. Additional code
  // bytes are checked when reserved; avoid overflowing n + overhead.
  if (!out || (!in && n) || capacity < 2 || n > capacity - 2) return 0;
  ProtogenCobsWriter writer = {0, 1, 1};
  for (size_t i = 0; i < n; ++i)
    if (!protogenCobsWriteByte(&writer, in[i], out, capacity)) return 0;
  return protogenCobsFinish(&writer, out, capacity);
}

static inline bool protogenCobsBlockFits(uint8_t code, size_t remainingInput,
                                        size_t remainingOutput) {
  return code && (size_t)(code - 1) <= remainingInput &&
         (size_t)(code - 1) <= remainingOutput;
}
static inline bool protogenCobsCopyByte(const uint8_t *in, size_t *i,
                                       uint8_t *out, size_t *used) {
  if (!in[*i]) return false;
  out[(*used)++] = in[(*i)++];
  return true;
}
static inline size_t protogenCobsDecode(const uint8_t *in, size_t n, uint8_t *out, size_t capacity) {
  if ((!in && n) || !out) return 0;
  size_t used = 0, i = 0;
  while (i < n) {
    const uint8_t code = in[i++];
    if (!protogenCobsBlockFits(code, n - i, capacity - used)) return 0;
    for (unsigned j = 1; j < code; ++j)
      if (!protogenCobsCopyByte(in, &i, out, &used)) return 0;
    if (code != 0xff && i < n) {
      if (used == capacity) return 0;
      out[used++] = 0;
    }
  }
  return used;
}
