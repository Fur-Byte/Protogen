#pragma once
#include <stdbool.h>
#include <stdint.h>

static inline uint32_t protogenRemaining(uint32_t now, uint32_t at, uint32_t interval) {
  const uint32_t elapsed = now - at;
  return elapsed >= interval ? 0 : interval - elapsed;
}
static inline bool protogenRetryPending(bool pending, uint32_t now, uint32_t at, uint32_t interval) {
  return pending && protogenRemaining(now, at, interval) != 0;
}
