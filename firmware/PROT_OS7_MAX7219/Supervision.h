#pragma once
#include <stdint.h>

#ifndef PROTOGEN_SUPERVISION_OBSERVE
#define PROTOGEN_SUPERVISION_OBSERVE 0
#endif
static_assert(PROTOGEN_SUPERVISION_OBSERVE == 0 || PROTOGEN_SUPERVISION_OBSERVE == 1,
              "Supervision observe must be 0 or 1.");

namespace Supervision {
constexpr uint32_t ForegroundBudgetMs = 200;
constexpr uint32_t RenderBudgetMs = 64;
constexpr uint32_t AudioBudgetMs = 64;
constexpr uint32_t SaveAllowanceMs = 100;

struct Progress {
  uint32_t lastAt = 0, marker = 0, maxGap = 0, lateEpisodes = 0;
  bool required = false, stale = false;

  void observe(uint32_t at, bool needed, uint32_t next, uint32_t budget) {
    if (!needed) {
      required = stale = false;
      return;
    }
    if (!required) {
      required = true;
      lastAt = at;
      marker = next;
      return;
    }

    const uint32_t age = at - lastAt;
    if (age > maxGap) maxGap = age;
    if (age > budget && !stale && lateEpisodes != UINT32_MAX) ++lateEpisodes;
    stale = age > budget;
    if (marker != next) {
      lastAt = at;
      marker = next;
      stale = false;
    }
  }
};

// One writer, the existing foreground loop. Epochs indicate work observed by
// the shell. An ADC ISR publication alone cannot satisfy foreground progress.
class Observer {
  uint32_t observedAt = 0, activeClock = 0, saveAllowance = 0, foregroundEpoch = 0;
  bool initialized = false;

public:
  Progress foreground, render, audio;
  uint32_t maxSaveMs = 0, longSaves = 0;

  void storage(uint32_t elapsed) {
    if (elapsed > maxSaveMs) maxSaveMs = elapsed;
    if (elapsed > SaveAllowanceMs && longSaves != UINT32_MAX) ++longSaves;
    saveAllowance = elapsed < SaveAllowanceMs ? elapsed : SaveAllowanceMs;
  }

  void observe(uint32_t now, bool visible, uint32_t renders, bool sampling, uint32_t drained) {
    const uint32_t rawGap = initialized ? now - observedAt : 0;
    activeClock += rawGap - (saveAllowance < rawGap ? saveAllowance : rawGap);
    observedAt = now;
    saveAllowance = 0;
    initialized = true;
    foreground.observe(activeClock, visible, ++foregroundEpoch, ForegroundBudgetMs);
    render.observe(activeClock, visible, renders, RenderBudgetMs);
    audio.observe(activeClock, visible && sampling, drained, AudioBudgetMs);
  }
};

static_assert(sizeof(Observer) <= 96, "The observer must stay under 96 bytes.");
}
