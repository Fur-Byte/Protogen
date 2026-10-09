#pragma once
#include <stdint.h>

// Independent bench switches. Production retains its timing until physical
// energy, latency, reliability and animation qualification has been recorded.
#ifndef PROTOGEN_CACHE_FACE
#define PROTOGEN_CACHE_FACE 0
#endif
#ifndef PROTOGEN_CASE_2HZ
#define PROTOGEN_CASE_2HZ 0
#endif
#ifndef PROTOGEN_OLED_1HZ
#define PROTOGEN_OLED_1HZ 0
#endif
#ifndef PROTOGEN_OLED_PAGES
#define PROTOGEN_OLED_PAGES 0
#endif
#ifndef PROTOGEN_CHECKPOINT_5MIN
#define PROTOGEN_CHECKPOINT_5MIN 0
#endif
#ifndef PROTOGEN_WEARER_DEADLINES
#define PROTOGEN_WEARER_DEADLINES 0
#endif
#ifndef PROTOGEN_MAIN_EVENTS
#define PROTOGEN_MAIN_EVENTS 0
#endif
#ifndef PROTOGEN_OLED_PROTECTION
#define PROTOGEN_OLED_PROTECTION 0
#endif
namespace Efficiency {
constexpr unsigned Flags = PROTOGEN_CACHE_FACE | (PROTOGEN_CASE_2HZ << 1) |
    (PROTOGEN_OLED_1HZ << 2) | (PROTOGEN_OLED_PAGES << 3) |
    (PROTOGEN_CHECKPOINT_5MIN << 4) | (PROTOGEN_WEARER_DEADLINES << 5) |
    (PROTOGEN_MAIN_EVENTS << 6) | (PROTOGEN_OLED_PROTECTION << 7);
static_assert((PROTOGEN_CACHE_FACE == 0 || PROTOGEN_CACHE_FACE == 1) &&
    (PROTOGEN_CASE_2HZ == 0 || PROTOGEN_CASE_2HZ == 1) &&
    (PROTOGEN_OLED_1HZ == 0 || PROTOGEN_OLED_1HZ == 1) &&
    (PROTOGEN_OLED_PAGES == 0 || PROTOGEN_OLED_PAGES == 1) &&
    (PROTOGEN_CHECKPOINT_5MIN == 0 || PROTOGEN_CHECKPOINT_5MIN == 1) &&
    (PROTOGEN_WEARER_DEADLINES == 0 || PROTOGEN_WEARER_DEADLINES == 1) &&
    (PROTOGEN_MAIN_EVENTS == 0 || PROTOGEN_MAIN_EVENTS == 1) &&
    (PROTOGEN_OLED_PROTECTION == 0 || PROTOGEN_OLED_PROTECTION == 1), "Bench flags must be 0 or 1");
inline void increment(uint32_t &n) { if (n != UINT32_MAX) ++n; }
// Relative deadlines avoid zero-time sentinels and survive millis rollover.
inline uint32_t remaining(uint32_t now, uint32_t at, uint32_t interval) {
  const uint32_t elapsed = now - at;
  return elapsed >= interval ? 0 : interval - elapsed;
}
inline void sooner(uint32_t &wait, uint32_t candidate) {
  if (candidate < wait) wait = candidate;
}
struct Checkpoint {
  uint32_t at = 0, value = 0;
  bool known = false;
  bool due(uint32_t now, uint32_t candidate, bool valid, uint32_t interval) const {
    return valid && remaining(now, at, interval) == 0 && (!known || candidate != value);
  }
  // Failed writes are retried after an interval, never in a tight flash loop.
  void attempted(uint32_t now, uint32_t candidate, bool success) {
    at = now;
    if (success) { value = candidate; known = true; }
  }
};
}
