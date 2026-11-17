#include "BehaviorCore.h"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace Behavior;
using namespace Behavior::Animation;

void curves() {
  struct Sample { Easing easing; uint16_t quarter, middle, threeQuarters; };
  for (auto sample : {Sample{Easing::Linear,64,128,192}, {Easing::In,16,64,144},
                       {Easing::Out,112,192,240}, {Easing::InOut,40,128,216},
                       {Easing::Settle,148,224,252}}) {
    assert(ease(sample.easing, 0, 0) == 256);
    assert(ease(sample.easing, 0, 256) == 0);
    assert(ease(sample.easing, 64, 256) == sample.quarter);
    assert(ease(sample.easing, 128, 256) == sample.middle);
    assert(ease(sample.easing, 192, 256) == sample.threeQuarters);
    assert(ease(sample.easing, 256, 256) == 256);
    uint16_t previous = 0;
    for (uint32_t time = 0; time <= MaxDurationMs; ++time) {
      const auto current = ease(sample.easing, time, MaxDurationMs);
      assert(current >= previous && current <= 256);
      previous = current;
    }
  }
}

void arbitration() {
  Table table(UINT32_MAX - 2);
  assert(table.count() == 0);
  const uint32_t start = UINT32_MAX - 25;
  const Request wide{Region::FullFace, Motion::Gaze, 3, Easing::Linear, 100, -2, 2};
  const Request left{Region::LeftEye, Motion::Gaze, 3, Easing::Linear, 50, 2, -2};
  const auto first = table.start(start, wide);
  assert(table.count() == 1 && first.handle.slot == 0);
  const auto second = table.start(start, left);
  assert(second.handle.slot == 1);
  // Same timestamp. The later admission wins only for the target eye.
  assert(table.resolve(Region::LeftEye, start).value == 2);
  assert(table.resolve(Region::RightEye, start).value == -2);
  assert(!table.resolve(Region::Mouth, start).handle);
  const auto third = table.start(start, {Region::LeftEye, Motion::Lid, 4, Easing::Linear, 20, 6, 6});
  assert(third.handle.slot == 2);
  assert(table.resolve(Region::LeftEye, start + 20).value == 6);
  assert(table.resolve(Region::LeftEye, start + 21).handle.slot == second.handle.slot);
  assert(table.resolve(Region::LeftEye, start + 50).value == -2);
  assert(table.resolve(Region::LeftEye, start + 51).handle.slot == first.handle.slot);
  assert(table.resolve(Region::LeftEye, start + 100).value == 2);
  assert(!table.resolve(Region::LeftEye, start + 101).handle);
  assert(outranks(3, 7, 1, 3, 7, 4));
  assert(!outranks(3, 7, 4, 3, 7, 1));
  assert(!outranks(2, 9, 0, 3, 1, 7));
  table.expire(start + 101);
  assert(table.count() == 0);

  for (unsigned i = 0; i < 8; ++i) {
    const auto a = table.start(start + 200, wide);
    assert(a.status == Admission::Started && a.handle.slot == i && table.count() == i + 1);
  }
  const auto rejected = table.start(start + 200, left);
  assert(rejected.status == Admission::Full && !rejected.handle && table.count() == 8);
  assert(!table.cancel(first.handle)); // First slot is occupied by a new generation.
  assert(!table.cancel({255, 1}));
  const Handle current{0, first.handle.generation + 1};
  assert(table.cancel(current));
  assert(table.count() == 7);
  const auto reused = table.start(start + 200, left);
  assert(reused.handle.slot == 0 && reused.handle.generation == current.generation + 1);
  assert(table.resolve(Region::LeftEye, start + 200).handle.slot == 0);
  assert(!table.cancel(current));
  assert(table.start(start + 200, {Region::Mouth, Motion::Lid, 7, Easing::Linear, 10, 0, 8}).status == Admission::Invalid);
  assert(table.start(start + 200, {Region::LeftEye, Motion::Gaze, 0, Easing::Linear, 1, 0, 3}).status == Admission::Invalid);
  assert(table.start(start + 200, {Region::LeftEye, Motion::Vertical, 0, Easing::Linear, 1, 0, -2}).status == Admission::Invalid);
  assert(table.start(start + 200, {Region::LeftEye, Motion::Expression, 0, Easing::Linear, 1, 0, 1}).status == Admission::Invalid);
  assert(table.start(start + 200, {Region::LeftEye, Motion::Lid, 0, Easing::Linear, 5001, 0, 8}).status == Admission::Invalid);
  assert(table.count() == 8);

  Table immediate;
  immediate.start(start, {Region::Mouth, Motion::MouthAperture, 0, Easing::Out, 0, 0, 3});
  assert(immediate.resolve(Region::Mouth, start).value == 3);
  assert(!immediate.resolve(Region::Mouth, start + 1).handle);
}

void precedence() {
  Core core;
  RuntimeConfig config;
  const auto gaze = core.animations.start(10, {Region::FullFace, Motion::Gaze, 4, Easing::Linear, 500, 2, 2});
  core.animations.start(10, {Region::FullFace, Motion::MouthAperture, 4, Easing::Linear, 500, 3, 3});
  assert(gaze.handle);
  auto draw = [&](uint32_t now, bool speaking = false, bool pending = false) {
    return core.poseAt(now, config, speaking, 2, false, pending);
  };
  assert(draw(20).leftEye.x == 2 && draw(20).mouthOverride == 3);
  assert(draw(20, true).mouthOverride < 0);
  assert(draw(20, false, true).leftEye.x == 0);
  core.blinkState = true;
  core.blinkClosure = 4;
  const auto blink = draw(20);
  assert(blink.closure == 4 && blink.leftEye.x == 0 && blink.mouthOverride == 3);
  core.resetAnimationAt(21);
  core.beginTransition(21, 1, 0);
  assert(draw(30).rightEye.x == 0);
  assert(draw(190).rightEye.x == 2);

  core.beginRemoteBoop(200, 100);
  const auto boop = draw(210, true);
  assert(boop.boop && boop.leftEye.x == 0 && boop.mouthOverride < 0);
  core.expireBoop(300, config.boopDuration);
  assert(!draw(300).boop && draw(300).leftEye.x == 2);
  // Advance while safety policy prevents composition.
  core.advanceMotion(511);
  assert(core.animations.count() == 0);
  core.requestSettle(512);
  assert(draw(520, false, true).leftEye.closure == 0);
  assert(draw(536).leftEye.closure == 3);
  assert(draw(608).leftEye.closure < 3);
  assert(draw(700).leftEye.closure == 0);
  config.idleMotion = 1;
  core.blinkTime = 0;
  assert(draw(2200).leftEye.x == 0);
  assert(draw(2272).leftEye.x == 1);
  assert(draw(2344).leftEye.x == 2);
  assert(draw(2488).leftEye.x == 0);
  assert(draw(4544).leftEye.x == -2);
  core.commitFrameTime(UINT32_MAX - 4);
  assert(core.nextVisualDeadline() == UINT32_MAX - 2);
  core.transitionVisiblePending = false;
  assert(core.nextVisualDeadline() == 11);
  assert(!core.frameDue(10, false) && core.frameDue(11, false));

  Core full;
  for (unsigned i = 0; i < Capacity; ++i)
    full.animations.start(10, {Region::FullFace, Motion::Gaze, 1, Easing::Linear, 500, 1, 1});
  full.requestSettle(20);
  const auto stillGazing = full.poseAt(30, config, false, 0, false);
  assert(stillGazing.leftEye.x == 1 && !full.settleTrack && full.animations.count() == Capacity);
}

int main() {
  curves();
  arbitration();
  precedence();
  Table worstCase;
  for (unsigned i = 0; i < Capacity; ++i)
    worstCase.start(0, {Region::FullFace, Motion::Lid, 1, Easing::InOut, 5000, 0, 8});
  using Clock = std::chrono::steady_clock;
  int64_t worstNs = 0;
  unsigned checksum = 0;
  for (uint32_t now = 0; now < 5000; ++now) {
    const auto begin = Clock::now();
    checksum += worstCase.resolve(Region::LeftEye, now).value;
    checksum += worstCase.resolve(Region::RightEye, now).value;
    checksum += worstCase.resolve(Region::Mouth, now).value;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
    if (ns > worstNs) worstNs = ns;
  }
  assert(checksum > 0);
  std::cout << "{\"table_bytes\":" << sizeof(Table) << ",\"core_bytes\":" << sizeof(Core)
            << ",\"host_worst_three_region_ns\":" << worstNs << "}\n";
}
