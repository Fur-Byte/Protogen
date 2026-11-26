#pragma once
#include <stdint.h>

namespace Recovery {
constexpr uint32_t Magic = 0x50524752u, Version = 1, Commit = 0x444f4e45u;
constexpr uint32_t RetryLimit = 3, HealthyMs = 30000, FeedMs = 100, HealthyGapMs = 200;
constexpr uint32_t PowerOn = 1, Software = 2, Watchdog = 16;
enum Stage : uint32_t { Boot, Running, Saving };

// The journal contains only numeric health metadata. Each slot owns two
// complete cache lines; no pointers, settings, or command authority survive.
struct alignas(32) Slot {
  uint32_t magic, version, buildLow, buildHigh, sequence, attempts;
  uint32_t stage, uptime, passes, renders, drained, cause, reserved[2];
  uint32_t checksum, marker;
};
static_assert(sizeof(Slot) == 64 && alignof(Slot) == 32, "Retained slot layout");

inline uint32_t checksum(const Slot &s) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(&s);
  uint32_t value = 0xffffffffu;
  for (unsigned i = 0; i < 56; ++i) {
    value ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
  }
  return ~value;
}
inline bool valid(const Slot &s, uint64_t build) {
  return s.magic == Magic && s.version == Version && s.marker == Commit && s.buildLow == uint32_t(build) &&
         s.buildHigh == uint32_t(build >> 32) && s.attempts <= RetryLimit && s.stage <= Saving &&
         !s.reserved[0] && !s.reserved[1] && s.checksum == checksum(s);
}
inline int newest(const Slot &a, const Slot &b, uint64_t build) {
  const bool av = valid(a, build), bv = valid(b, build);
  if (!av) return bv ? 1 : -1;
  if (!bv) return 0;
  if (a.sequence + 1u == b.sequence) return 1;
  if (b.sequence + 1u == a.sequence) return 0;
  return -1;
}
inline uint32_t bootAttempts(uint32_t cause, bool trusted, uint32_t prior) {
  if (cause == PowerOn) return 0;
  if (!trusted || prior >= RetryLimit) return RetryLimit;
  if (cause == Watchdog) return prior + 1;
  if (cause == Software) return prior;
  return RetryLimit;
}
// A recovered stall must not be credited as continuously healthy time.
struct HealthyWindow {
  uint32_t since = 0, last = 0;
  bool active = false;

  bool observe(uint32_t now, bool progressing) {
    if (!progressing) {
      active = false;
      return false;
    }
    if (!active || uint32_t(now - last) > HealthyGapMs) {
      since = now;
      active = true;
    }
    last = now;
    return uint32_t(now - since) >= HealthyMs;
  }
};

inline Slot make(uint64_t build, uint32_t sequence, uint32_t attempts, Stage stage, uint32_t now,
                 uint32_t passes, uint32_t renders, uint32_t drained, uint32_t cause) {
  Slot s{Magic,    Version,  uint32_t(build), uint32_t(build >> 32),
         sequence, attempts, stage,           now,
         passes,   renders,  drained,         cause,
         {0, 0},   0,        Commit};
  s.checksum = checksum(s);
  return s;
}
}
