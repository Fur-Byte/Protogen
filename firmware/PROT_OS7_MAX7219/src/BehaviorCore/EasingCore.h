#pragma once
#include <stdint.h>

static inline uint16_t protogenEase(uint8_t curve, uint32_t elapsed, uint16_t duration) {
  const uint32_t scale = 256;
  if (elapsed >= duration) return (uint16_t)scale;
  const uint32_t t = elapsed * scale / duration;
  const uint32_t u = scale - t;
  switch (curve) {
    case 1: return (uint16_t)(t * t / scale);
    case 2: return (uint16_t)(scale - u * u / scale);
    case 3: return (uint16_t)(t * t * (3 * scale - 2 * t) / (scale * scale));
    case 4: return (uint16_t)(scale - u * u * u / (scale * scale));
    default: return (uint16_t)t;
  }
}
