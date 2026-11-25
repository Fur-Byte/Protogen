#pragma once
#include <stdint.h>

#ifdef __cplusplus
#define PROTOGEN_CONSTEXPR constexpr
#else
#define PROTOGEN_CONSTEXPR
#endif
static inline PROTOGEN_CONSTEXPR uint8_t protogenBrightness(uint32_t requested,
                                                           uint32_t approval,
                                                           uint8_t ceiling) {
  const uint32_t limit = requested < approval ? requested : approval;
  return limit > ceiling ? ceiling : (uint8_t)limit;
}
#undef PROTOGEN_CONSTEXPR
