#include "BehaviorCore.h"
#include <cassert>

int main() {
  using namespace Behavior;
  const auto midway = transitionPose(30, 160, 0, 2, 0, false);
  assert(midway.active && midway.expression == 0 && midway.closure == 4);
  assert(transitionPose(100, 160, 0, 2, 0, false).closure == 8);
  assert(transitionPose(130, 160, 0, 2, 0, false).closure == 4);
  assert(transitionPose(30, 160, 0, 2, 0, true).expression == 2);
  assert(!transitionPose(160, 160, 0, 2, 0, false).active);

  Core core;
  RuntimeConfig config;
  config.transitionMs = 160;
  const uint32_t start = UINT32_MAX - 20;
  core.expression = ExpressionAngry;
  core.resetAnimationAt(start);
  core.beginTransition(start, ExpressionHappy, 2);
  assert(!core.advanceBlink(start + 40) && !core.blinkState);
  assert(core.poseAt(start + 30, config, true, 3, false).expression == ExpressionHappy);
  const auto emitted = core.poseAt(start + 31, config, false, 0, false);
  core.expression = ExpressionHappy;
  core.resetAnimationAt(start + 32);
  core.beginTransition(start + 32, emitted.expression, emitted.closure);
  assert(core.poseAt(start + 32, config, false, 0, false).closure == emitted.closure);
  assert(core.poseAt(start + 192, config, false, 0, false).closure == 0 && !core.transitionActive);

  core.scheduleBlink(config, start);
  core.acceptBlinkWord(5);
  assert(!core.finishBlinkEntropy(config) && !core.blinkRandomPending);
  core.scheduleBlink(config, start);
  core.acceptBlinkWord(5);
  core.acceptBlinkWord(0);
  core.acceptBlinkWord(99);
  assert(core.blinkRandomCount == 2 && core.finishBlinkEntropy(config) && core.blinkDoublePending);
  const uint32_t blinkAt = core.blinkTime + core.nextBlinkInterval;
  assert(!core.advanceBlink(blinkAt));
  assert(core.blinkState && !core.advanceBlink(blinkAt + core.blinkTiming.closeMs) && core.blinkClosure == 8);
  assert(!core.advanceBlink(blinkAt + core.blinkTiming.duration()));
  assert(!core.blinkState && !core.blinkDoublePending && core.nextBlinkInterval == core.blinkTiming.followupMs);

  core.observeBoop(-1, config);
  assert(!core.wantsLocalBoop());
  core.observeBoop(config.boopRelease, config);
  core.observeBoop(config.boopThreshold, config);
  assert(core.wantsLocalBoop());
  core.beginRemoteBoop(start, 100);
  assert(!core.releaseHeldBoop(0) && !core.canTransition(true, 160));
  core.expireBoop(start + 99, config.boopDuration);
  assert(core.boopActive);
  core.expireBoop(start + 100, config.boopDuration);
  assert(!core.boopActive && !core.boopArmed && core.rising);
  core.commitFrameTime(start + 100);
  assert(!core.frameDue(start + 101, false));
  assert(core.frameDue(start + 101, true));
  core.transitionVisiblePending = true;
  assert(core.frameDue(start + 102, false));
}
