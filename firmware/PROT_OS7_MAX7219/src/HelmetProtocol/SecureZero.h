#pragma once
#include <stddef.h>
#include <stdint.h>
inline void protogenSecureZero(void *p, size_t n) {
  volatile uint8_t *v = static_cast<volatile uint8_t *>(p);
  while (n--)
    *v++ = 0;
}
