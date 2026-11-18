#pragma once
#include <stdint.h>

#ifndef PROTOGEN_AUDIO_V2
#define PROTOGEN_AUDIO_V2 0
#endif
static_assert(PROTOGEN_AUDIO_V2 == 0 || PROTOGEN_AUDIO_V2 == 1, "Audio V2 must be 0 or 1.");

namespace AudioV2 {
constexpr uint16_t SampleRate = 4000, WindowSamples = 64;
struct Settings {
  uint16_t sensitivity, noiseMargin, noiseFloor, attackMs, releaseMs, holdMs;
};
struct Features {
  uint32_t energy = 0;
  uint16_t envelope = 0, noise = 0, gate = 0;
  uint8_t aperture = 0;
  bool speech = false, onset = false, offset = false, periodic = false;
};

class Detector {
  float bias = 0, envelope = 0, noise = 0, rollingPeak = 0;
  static constexpr unsigned HistorySize = 32;
  float history[HistorySize]{};
  uint16_t amplitudes[10]{};
  uint32_t amplitudeSum = 0, windowSum = 0, energySum = 0;
  // Cover intermediate half-periods as well as the original three taps.
  static constexpr uint8_t Lags[] = {4, 6, 8, 9, 10, 12, 16, 18};
  float correlation[sizeof(Lags)]{};
  uint16_t quietSamples = 0, stationaryLow = 1023, stationaryHigh = 0;
  uint8_t sampleIndex = 0, averageIndex = 0, historyIndex = 0;
  uint8_t credible = 0, stationarityCount = 0;
  uint8_t pendingPose = 0, pendingPoseWindows = 0, clipped = 0;
  bool primed = false;
  Features value;
  void finish(const Settings &config);

public:
  void reset(const Settings &config);
  // Pass contiguous 10-bit samples. Reset on gaps. Returns true on a new window.
  bool sample(uint16_t raw, const Settings &config);
  Features snapshot() const { return value; }
};
}
