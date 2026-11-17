#include "AudioV2.h"
#include <cassert>
#include <iostream>

using namespace AudioV2;
static const Settings config{65, 12, 0, 20, 120, 100};

static Features window(Detector &detector, unsigned amplitude, unsigned midpoint = 512) {
  for (unsigned i = 0; i < WindowSamples; ++i) {
    const int wave[] = {0, 1, 0, -1};
    const auto raw = static_cast<uint16_t>(int(midpoint) + wave[i % 4] * int(amplitude));
    assert(detector.sample(raw, config) == (i + 1 == WindowSamples));
  }
  return detector.snapshot();
}

int main() {
  Detector detector;
  detector.reset(config);
  for (unsigned w = 0; w < 64; ++w) {
    const auto result = window(detector, 0, 512 + w);
    assert(!result.speech && !result.onset && result.aperture == 0);
  }

  assert(!window(detector, 160).speech);
  const auto onset = window(detector, 160);
  assert(onset.speech && onset.onset && onset.periodic && onset.aperture >= 1);
  for (unsigned w = 0; w < 1000; ++w) {
    const auto result = window(detector, 380);
    assert(result.speech && !result.onset && !result.offset);
    assert(result.noise < config.sensitivity);
    if (w > 4) assert(result.aperture == 3);
  }
  for (unsigned w = 0; w < 6; ++w) assert(window(detector, 0).speech);
  const auto offset = window(detector, 0);
  assert(!offset.speech && offset.offset && offset.aperture == 0);
  assert(!window(detector, 0).offset);

  // An explicit discontinuity discards partial windows, holds and snapshots.
  for (unsigned i = 0; i < WindowSamples - 1; ++i) assert(!detector.sample(100, config));
  assert(!detector.sample(1024, config));
  assert(!detector.snapshot().speech && detector.snapshot().energy == 0);
  assert(!window(detector, 0).speech);
  assert(onset.speech && onset.onset);  // Previously copied snapshot is immutable.

  // Saturation cannot keep a presentation open, even before the rail retry.
  for (unsigned w = 0; w < 4; ++w) window(detector, 380);
  assert(detector.snapshot().speech);
  for (unsigned i = 0; i < WindowSamples; ++i) detector.sample(i % 2 ? 1023 : 0, config);
  const auto saturated = detector.snapshot();
  assert(saturated.offset && !saturated.speech && saturated.aperture == 0);
  assert(saturated.energy > 0 && saturated.energy <= 1023u * 1023u);

  Detector highFloor;
  const Settings calibrated{65, 40, 1023, 1, 2000, 100};
  highFloor.reset(calibrated);
  assert(highFloor.snapshot().gate == 2086);
  std::cout << "PASS: windows, onset, sustained speech, aperture, hold, snapshot, gap and saturation\n";
}
