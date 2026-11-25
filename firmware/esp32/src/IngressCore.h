#pragma once
#include <stdbool.h>
#include <stdint.h>

// The caller must obtain a coherent identity snapshot under the ingress lock.
static inline bool protogenCurrentIngress(bool enabled, uint32_t received,
                                           uint32_t current) {
  return enabled && received == current;
}
