#pragma once
#include <stdint.h>
#include "Configuration.h"

// Human-inspired animation parameters, not a physiological model. The caller
// supplies fresh hardware entropy; there is no seed or PRNG in this sampler.
namespace BlinkTiming {
struct Timing {
  uint32_t pauseMs = Settings::BlinkIntervalMs;
  uint16_t closeMs = 70, holdMs = 25, openMs = 155, followupMs = 250;
  bool doubleBlink = false;
  uint32_t duration() const { return closeMs + holdMs + openMs; }
  uint8_t closure(uint32_t elapsed) const {
    if (elapsed < closeMs) return static_cast<uint8_t>(elapsed * 8 / closeMs);
    const uint32_t closed = closeMs + holdMs;
    if (elapsed < closed) return 8;
    const uint32_t opening = elapsed - closed;
    return opening >= openMs ? 0 : static_cast<uint8_t>(8 - opening * 8 / openMs);
  }
};
inline uint16_t range(uint8_t bits, uint16_t minimum, uint16_t maximum) {
  return minimum + ((static_cast<uint32_t>(bits) * (maximum - minimum + 1)) >> 8);
}
inline Timing sample(const RuntimeConfig &config, uint32_t pauseBits, uint32_t motionBits) {
  Timing timing;
  // Sum two independent uniform halves: pauses cluster near the chosen interval
  // instead of giving extreme short/long delays the same weight as normal ones.
  const uint32_t sum = (pauseBits & 0xffff) + (pauseBits >> 16);
  const uint32_t span = config.blinkJitter * 2UL + 1;
  timing.pauseMs = config.blinkInterval - config.blinkJitter + ((sum * span) >> 17);
  timing.closeMs = range(static_cast<uint8_t>(motionBits), 60, 90);
  timing.holdMs = range(static_cast<uint8_t>(motionBits >> 8), 20, 35);
  timing.openMs = range(static_cast<uint8_t>(motionBits >> 16), 130, 180);
  // About 8% of sequences contain a second blink; it never chains into a third.
  timing.doubleBlink = (motionBits >> 24) < 20;
  timing.followupMs = 180 + (((pauseBits & 0xffff) * 141UL) >> 16);
  return timing;
}
}
