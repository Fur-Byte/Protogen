#include "BehaviorCore.h"

namespace Behavior {

TransitionPose transitionPose(uint32_t elapsed, uint16_t duration, uint8_t from,
                              uint8_t target, uint8_t initialClosure, bool earlyReveal) {
  if (elapsed >= duration) return {target, 0, false};
  const uint32_t scaledRamp = duration * 3UL / 8;
  const uint32_t ramp = scaledRamp < Settings::TransitionFeedbackRampMs
      ? scaledRamp : Settings::TransitionFeedbackRampMs;
  const uint32_t remainingClosure = 8 - initialClosure;
  const uint32_t closeMs = (ramp * remainingClosure + 7) / 8;
  uint8_t closure = 8;
  if (elapsed < closeMs)
    closure = static_cast<uint8_t>(initialClosure + elapsed * remainingClosure / closeMs);
  else if (elapsed >= duration - ramp)
    closure = static_cast<uint8_t>((duration - elapsed) * 8 / (ramp ? ramp : 1));
  return {!earlyReveal && elapsed < duration / 2 ? from : target, closure, true};
}

void Core::resetAnimationAt(uint32_t now) {
  blinkClosure = 0;
  blinkState = false;
  glitchFrame = 0;
  transitionActive = false;
  transitionVisiblePending = false;
  blinkTime = blinkSpeed = glitchDelay = glitchSpeed = now;
}

void Core::scheduleBlink(const RuntimeConfig &config, uint32_t sampledNow) {
  nextBlinkInterval = config.blinkInterval;
  blinkTiming = BlinkTiming::Timing{};
  blinkRandomCount = 0;
  blinkDoublePending = false;
  blinkRandomStarted = sampledNow;
  blinkRandomPending = config.naturalBlinks != 0;
}

void Core::acceptBlinkWord(uint32_t word) {
  if (blinkRandomCount < 2) blinkRandomWords[blinkRandomCount++] = word;
}

bool Core::finishBlinkEntropy(const RuntimeConfig &config) {
  blinkRandomPending = false;
  if (blinkRandomCount < 2) return false;
  blinkTiming = BlinkTiming::sample(config, blinkRandomWords[0], blinkRandomWords[1]);
  nextBlinkInterval = blinkTiming.pauseMs;
  blinkDoublePending = blinkTiming.doubleBlink;
  return true;
}

bool Core::advanceBlink(uint32_t now) {
  if (transitionActive || expression == ExpressionVwv) return false;
  if (!blinkState && !blinkRandomPending && now - blinkTime >= nextBlinkInterval) {
    blinkState = true; blinkClosure = 0; blinkSpeed = now;
  }
  if (!blinkState) return false;
  const uint32_t elapsed = now - blinkSpeed;
  blinkClosure = blinkTiming.closure(elapsed);
  if (elapsed < blinkTiming.duration()) return false;
  blinkClosure = 0; blinkState = false; blinkTime = now;
  if (!blinkDoublePending) return true;
  nextBlinkInterval = blinkTiming.followupMs;
  blinkDoublePending = false;
  return false;
}

void Core::advanceGlitch(uint32_t now, bool speaking) {
  if (speaking || transitionActive) { glitchFrame = 0; glitchDelay = now; return; }
  if (expression != ExpressionAngry) { glitchFrame = 0; return; }
  if (now - glitchDelay >= Settings::GlitchIntervalMs && now - glitchSpeed >= Settings::GlitchStepMs) {
    ++glitchFrame; glitchSpeed = now;
    if (glitchFrame >= 3) { glitchFrame = 0; glitchDelay = now; }
  }
}

void Core::observeBoop(int reading, const RuntimeConfig &config) {
  if (reading < 0 || reading > 1023) { boopState = false; boopArmed = false; }
  else if (reading >= config.boopRelease) { boopState = false; boopArmed = true; }
  else if (reading <= config.boopThreshold) boopState = true;
}

void Core::beginBoop(uint32_t now) {
  boopActive = true;
  boopTime = previousMillis = now;
  boopFromRemote = false;
}

void Core::beginRemoteBoop(uint32_t now, uint16_t duration) {
  beginBoop(now);
  boopFromRemote = true;
  remoteBoopDuration = duration;
}

void Core::expireBoop(uint32_t now, uint16_t localDuration) {
  if (boopActive && now - boopTime >= (boopFromRemote ? remoteBoopDuration : localDuration)) {
    boopActive = false; boopFromRemote = false; boopArmed = false; rising = true;
  }
}

bool Core::releaseHeldBoop(uint16_t mode) {
  if (!boopActive || boopFromRemote || mode != 0 || boopState) return false;
  boopActive = false;
  return true;
}

bool Core::canTransition(bool visorVisible, uint16_t duration) const {
  return !sleeping && visorVisible && !boopActive && duration;
}

void Core::beginTransition(uint32_t now, uint8_t displayedExpression, uint8_t displayedClosure) {
  transitionActive = true;
  transitionVisiblePending = true;
  transitionFrom = displayedExpression;
  transitionStartClosure = displayedClosure;
  transitionStarted = now;
}

Pose Core::poseAt(uint32_t now, const RuntimeConfig &config, bool speaking, uint8_t aperture,
                  bool earlyReveal) {
  Pose pose;
  pose.expression = expression; pose.boop = boopActive;
  pose.speaking = speaking; pose.glitch = glitchFrame; pose.closure = blinkClosure;
  pose.mouthMode = static_cast<uint8_t>(config.mouthMode); pose.aperture = aperture;
  if (transitionActive) {
    const auto transition = transitionPose(now - transitionStarted, config.transitionMs,
        transitionFrom, expression, transitionStartClosure, earlyReveal);
    transitionActive = transition.active;
    if (transitionActive) {
      pose.expression = transition.expression;
      pose.closure = transition.closure;
    }
  }
  if (config.idleMotion && !blinkState && !transitionActive && !boopActive) {
    const uint32_t phase = (now - blinkTime) % 6000;
    pose.eyeOffset = phase >= 2200 && phase < 2500 ? 1 : phase >= 4400 && phase < 4700 ? -1 : 0;
  }
  return pose;
}

bool Core::frameDue(uint32_t now, bool waking) const {
  return waking || rising || now - lastFrameTime >= (transitionVisiblePending ? 2u : 16u);
}

void Core::commitFrameTime(uint32_t now) {
  lastFrameTime = now;
  rising = false;
}
}
