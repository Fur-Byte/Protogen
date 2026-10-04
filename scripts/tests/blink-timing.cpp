#include <cassert>
#include <iostream>
#include <random>
#include "BlinkTiming.h"

int main() {
  RuntimeConfig config;
  std::mt19937 random(0x172804);
  uint64_t pauseSum = 0, durationSum = 0, cycleSum = 0;
  unsigned doubles = 0, center = 0, extremes = 0;
  constexpr unsigned count = 100000;
  for (unsigned i = 0; i < count; ++i) {
    const auto timing = BlinkTiming::sample(config, random(), random());
    assert(timing.pauseMs >= 2000 && timing.pauseMs <= 6000);
    assert(timing.closeMs >= 60 && timing.closeMs <= 90);
    assert(timing.holdMs >= 20 && timing.holdMs <= 35);
    assert(timing.openMs >= 130 && timing.openMs <= 180);
    assert(timing.openMs > timing.closeMs);
    assert(timing.duration() >= 210 && timing.duration() <= 305);
    assert(timing.followupMs >= 180 && timing.followupMs <= 320);
    uint8_t previous = 0;
    for (uint32_t ms = 0; ms < timing.closeMs; ++ms) {
      assert(timing.closure(ms) >= previous && timing.closure(ms) <= 8);
      previous = timing.closure(ms);
    }
    const uint32_t closed = timing.closeMs + timing.holdMs;
    for (uint32_t ms = timing.closeMs; ms < closed; ++ms)
      assert(timing.closure(ms) == 8);
    previous = 8;
    for (uint32_t ms = timing.closeMs + timing.holdMs; ms < timing.duration(); ++ms) {
      assert(timing.closure(ms) <= previous);
      previous = timing.closure(ms);
    }
    assert(timing.closure(timing.duration()) == 0);
    pauseSum += timing.pauseMs; durationSum += timing.duration(); doubles += timing.doubleBlink;
    cycleSum += timing.pauseMs + timing.duration();
    if (timing.doubleBlink) cycleSum += timing.followupMs + timing.duration();
    center += timing.pauseMs >= 3500 && timing.pauseMs <= 4500;
    extremes += timing.pauseMs <= 2500 || timing.pauseMs >= 5500;
  }
  const double averagePause = static_cast<double>(pauseSum) / count;
  assert(averagePause > 3950 && averagePause < 4050);
  assert(doubles > 7000 && doubles < 8500 && center > extremes * 3);
  const double blinksPerMinute = 60000.0 * (count + doubles) / cycleSum;
  assert(blinksPerMinute > 14.5 && blinksPerMinute < 15.5);
  config.blinkInterval = 1000; config.blinkJitter = 999;
  assert(BlinkTiming::sample(config, 0, 0).pauseMs == 1);
  config.blinkInterval = 60000; config.blinkJitter = 5000;
  assert(BlinkTiming::sample(config, 0xffffffff, 0xffffffff).pauseMs <= 65000);
  config.blinkJitter = 0;
  assert(BlinkTiming::sample(config, 0xffffffff, 0).pauseMs == 60000);
  std::cout << "PASS: 100000 blink schedules, pause bounds/distribution, phase motion, double-blink probability; "
            << "mean pause " << averagePause << " ms, mean duration "
            << static_cast<double>(durationSum) / count << " ms, doubles " << doubles << "/" << count
            << ", modeled rate " << blinksPerMinute << "/min\n";
}
