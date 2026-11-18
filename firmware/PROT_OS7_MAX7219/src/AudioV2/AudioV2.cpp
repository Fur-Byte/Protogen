#include "AudioV2.h"
#include <cmath>

namespace AudioV2 {
namespace {
float bounded(float n, float minimum, float maximum) {
  return n < minimum ? minimum : n > maximum ? maximum : n;
}
uint16_t quantize(float n) { return static_cast<uint16_t>(bounded(n, 0, 1023)); }
}

void Detector::reset(const Settings &config) {
  *this = Detector{};
  noise = config.noiseFloor;
  value.noise = config.noiseFloor;
  value.gate = config.sensitivity > config.noiseFloor * 2u + config.noiseMargin
      ? config.sensitivity : config.noiseFloor * 2u + config.noiseMargin;
}

bool Detector::sample(uint16_t raw, const Settings &config) {
  if (raw > 1023) { reset(config); return false; }
  if (!primed) { bias = raw; primed = true; }
  bias = raw * 0.01f + bias * 0.99f;
  const float centered = raw - bias;
  const uint16_t amplitude = quantize(std::fabs(centered));
  amplitudeSum -= amplitudes[averageIndex];
  amplitudes[averageIndex] = amplitude;
  amplitudeSum += amplitude;
  if (++averageIndex == 10) averageIndex = 0;
  const float average = amplitudeSum / 10.0f;
  if (average > rollingPeak) rollingPeak = average;
  const float millis = average > envelope ? config.attackMs : config.releaseMs;
  envelope += (average - envelope) / (4.0f * (millis < 1 ? 1 : millis));

  for (unsigned i = 0; i < sizeof(Lags); ++i)
    correlation[i] += centered * history[(historyIndex + HistorySize - Lags[i]) % HistorySize];
  history[historyIndex] = centered;
  historyIndex = (historyIndex + 1) % HistorySize;
  windowSum += amplitude;
  energySum += static_cast<uint32_t>(centered * centered);
  if (raw <= 2 || raw >= 1021) ++clipped;
  if (++sampleIndex != WindowSamples) return false;
  finish(config);
  sampleIndex = 0;
  windowSum = energySum = clipped = 0;
  rollingPeak = 0;
  for (auto &n : correlation) n = 0;
  return true;
}

void Detector::finish(const Settings &config) {
  const uint16_t level = windowSum / WindowSamples;
  const bool wasSpeech = value.speech;
  bool periodic = false;
  for (const float dot : correlation) periodic |= std::fabs(dot) * 10 > energySum * 7.0f;
  periodic &= rollingPeak >= config.sensitivity;
  // Consecutive windows must remain in a narrow amplitude band without
  // periodic energy. Any voiced evidence or a new amplitude band starts a
  // fresh observation. There is no maximum permitted speech duration.
  if (periodic) {
    stationarityCount = 0;
  } else {
    if (level < stationaryLow) stationaryLow = level;
    if (level > stationaryHigh) stationaryHigh = level;
    if (!stationarityCount || stationaryHigh > stationaryLow * 1.55f + 4) {
      stationaryLow = stationaryHigh = level;
      stationarityCount = 1;
    } else if (stationarityCount < 8) {
      ++stationarityCount;
    }
  }
  const bool stationary = stationarityCount == 8;

  const float previousGate = noise * 2 + config.noiseMargin;
  const float gate = previousGate > config.sensitivity ? previousGate : config.sensitivity;
  const float onsetLevel = periodic ? rollingPeak : level;
  const bool high = onsetLevel > gate;
  if (clipped >= WindowSamples / 2) {
    // Shell rail/retry policy still owns hardware recovery. Invalid energy
    // cannot remain an accepted speech observation in the meantime.
    value.speech = false; credible = quietSamples = stationarityCount = 0;
  } else {
    if (level < noise) noise += (level - noise) * 0.125f;
    else if (stationary && !periodic) noise += (level - noise) * 0.0625f;
    else if (!value.speech && !high) noise += (level - noise) * 0.0625f;
    noise = bounded(noise, 0, 1023);
    const float adapted = noise * 2 + config.noiseMargin;
    const float threshold = adapted > config.sensitivity ? adapted : config.sensitivity;
    const bool onset = onsetLevel > threshold && !stationary;
    const bool keep = level > threshold * 0.72f && (!stationary || periodic);
    if (!value.speech) {
      credible = onset ? static_cast<uint8_t>(credible + (credible < 2)) : 0;
      if (credible >= 2) { value.speech = true; quietSamples = 0; }
    } else {
      if (keep) quietSamples = 0;
      else {
        quietSamples = quietSamples > UINT16_MAX - WindowSamples ? UINT16_MAX : quietSamples + WindowSamples;
        if (quietSamples >= config.holdMs * 4u) { value.speech = false; credible = 0; }
      }
    }
  }
  value.energy = energySum / WindowSamples;
  value.envelope = quantize(envelope);
  value.noise = quantize(noise);
  const float adapted = noise * 2 + config.noiseMargin;
  value.gate = static_cast<uint16_t>(adapted > config.sensitivity ? adapted : config.sensitivity);
  value.onset = !wasSpeech && value.speech;
  value.offset = wasSpeech && !value.speech;
  value.periodic = periodic;
  if (!value.speech) {
    value.aperture = pendingPose = pendingPoseWindows = 0;
    return;
  }
  const float ratio = (envelope - value.gate) / (value.gate * 2.0f + 1);
  uint8_t desired = value.aperture ? value.aperture : 1;
  if (ratio >= 0.72f) desired = 3;
  else if (ratio >= 0.30f && desired < 2) desired = 2;
  else if (ratio < 0.55f && desired == 3) desired = 2;
  else if (ratio < 0.20f) desired = 1;
  if (desired != pendingPose) { pendingPose = desired; pendingPoseWindows = 1; }
  else if (pendingPoseWindows < 2) ++pendingPoseWindows;
  if (!value.aperture || pendingPoseWindows >= 2) value.aperture = desired;
}
}
