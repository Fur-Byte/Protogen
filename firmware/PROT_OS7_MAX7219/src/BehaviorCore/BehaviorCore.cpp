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
    blinkState = true;
    blinkClosure = 0;
    blinkSpeed = now;
  }
  if (!blinkState) return false;
  const uint32_t elapsed = now - blinkSpeed;
  blinkClosure = blinkTiming.closure(elapsed);
  if (elapsed < blinkTiming.duration()) return false;
  blinkClosure = 0;
  blinkState = false;
  blinkTime = now;
  if (!blinkDoublePending) return true;
  nextBlinkInterval = blinkTiming.followupMs;
  blinkDoublePending = false;
  return false;
}

void Core::advanceGlitch(uint32_t now, bool speaking) {
  if (speaking || transitionActive) {
    glitchFrame = 0;
    glitchDelay = now;
    return;
  }
  if (expression != ExpressionAngry) {
    glitchFrame = 0;
    return;
  }
  if (now - glitchDelay >= Settings::GlitchIntervalMs && now - glitchSpeed >= Settings::GlitchStepMs) {
    ++glitchFrame;
    glitchSpeed = now;
    if (glitchFrame >= 3) {
      glitchFrame = 0;
      glitchDelay = now;
    }
  }
}

void Core::observeBoop(int reading, const RuntimeConfig &config) {
  if (reading < 0 || reading > 1023) {
    boopState = false;
    boopArmed = false;
  } else if (reading >= config.boopRelease) {
    boopState = false;
    boopArmed = true;
  } else if (reading <= config.boopThreshold) {
    boopState = true;
  }
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
    boopActive = false;
    boopFromRemote = false;
    boopArmed = false;
    rising = true;
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

void Core::beginTransition(uint32_t now, uint8_t displayedExpression, uint8_t displayedClosure
#if PROTOGEN_ANIMATION_V2
                           , EyeMotion leftOrigin, EyeMotion rightOrigin
#endif
                           ) {
  transitionActive = true;
  transitionVisiblePending = true;
  transitionFrom = displayedExpression;
  transitionStartClosure = displayedClosure;
  transitionStarted = now;
#if PROTOGEN_ANIMATION_V2
  transitionLeftOrigin = leftOrigin;
  transitionRightOrigin = rightOrigin;
#endif
}

#if PROTOGEN_ANIMATION_V2
void Core::requestSettle(uint32_t now) {
  animations.cancel(settleTrack);
  settleTrack = {};
  settleRequestedAt = now;
#if PROTOGEN_ANIMATION_TIMELINES
  settlePending = timeline.progress().state != Timeline::State::Running;
#else
  settlePending = true;
#endif
}

void Core::advanceMotion(uint32_t now) {
  animations.expire(now);
  if (settlePending && now - settleRequestedAt >= Animation::MaxDurationMs)
    settlePending = false;
}

namespace {
int8_t idleGaze(uint32_t phase) {
  constexpr uint16_t Half = 144;
  constexpr uint16_t RightStart = 2200, LeftStart = 4400;
  const bool left = phase >= LeftStart;
  const uint32_t start = left ? LeftStart : RightStart;
  if (phase < start || phase >= start + 2 * Half) return 0;
  const uint32_t offset = phase - start;
  const uint32_t progress = offset < Half ? offset : 2 * Half - offset;
  const auto eased = Animation::ease(Animation::Easing::InOut, progress, Half);
  const int gaze = (2 * eased + 128) / 256;
  return static_cast<int8_t>(left ? -gaze : gaze);
}

void applyEye(EyeMotion &eye, Animation::Selection selected, int8_t idle) {
  using Animation::Motion;
  if (!selected.handle) { eye.x = idle; return; }
  switch (selected.motion) {
    case Motion::Gaze: eye.x = selected.value; break;
    case Motion::Vertical: eye.y = selected.value; break;
    case Motion::Lid:
    case Motion::Squint: eye.closure = static_cast<uint8_t>(selected.value); break;
    case Motion::Expression: eye.expressionOverride = selected.value; break;
    case Motion::MouthAperture: break;
  }
}
}
#endif

Pose Core::poseAt(uint32_t now, const RuntimeConfig &config, bool speaking, uint8_t aperture,
                  bool earlyReveal
#if PROTOGEN_ANIMATION_V2
                  , bool legacyObservationPending
#endif
                  ) {
  Pose pose;
  pose.expression = expression;
  pose.boop = boopActive;
  pose.speaking = speaking;
  pose.glitch = glitchFrame;
  pose.closure = blinkClosure;
  pose.mouthMode = static_cast<uint8_t>(config.mouthMode);
  pose.aperture = aperture;
  if (transitionActive) {
    const auto transition = transitionPose(now - transitionStarted, config.transitionMs,
        transitionFrom, expression, transitionStartClosure, earlyReveal);
    transitionActive = transition.active;
    if (transitionActive) {
      pose.expression = transition.expression;
      pose.closure = transition.closure;
#if PROTOGEN_ANIMATION_V2
      const uint32_t elapsed = now - transitionStarted;
      const bool sourceArt = !earlyReveal && elapsed < config.transitionMs / 2;
      const auto closure = [&](uint8_t floor) -> uint8_t {
        if (floor <= transitionStartClosure) return 0; // The shared eyelid closure already covers this eye.
        return transitionPose(elapsed, config.transitionMs, transitionFrom, expression, floor, earlyReveal).closure;
      };
      pose.leftEye = transitionLeftOrigin;
      pose.rightEye = transitionRightOrigin;
      pose.leftEye.closure = closure(pose.leftEye.closure);
      pose.rightEye.closure = transitionRightOrigin.closure == transitionLeftOrigin.closure
          ? pose.leftEye.closure : closure(pose.rightEye.closure);
      if (!sourceArt) pose.leftEye.expressionOverride = pose.rightEye.expressionOverride = -1;
#endif
    }
  }
#if PROTOGEN_ANIMATION_V2
  advanceMotion(now);
  if (!boopActive) {
    if (!blinkState && !transitionActive && !legacyObservationPending) {
      if (settlePending) {
        using namespace Animation;
        settleTrack = animations.start(now, {Region::FullFace, Motion::Squint, 2,
            Easing::Settle, 144, 3, 0}).handle;
        settlePending = false;
      }
      const int8_t idle = config.idleMotion ? idleGaze((now - blinkTime) % 6000) : 0;
#if PROTOGEN_ANIMATION_TIMELINES
      const auto composeEye = [&](EyeMotion &eye, Animation::Region region) {
        using Animation::Motion;
        eye.x = idle;
        constexpr Motion properties[] = {Motion::Gaze, Motion::Vertical, Motion::Lid, Motion::Expression};
        for (Motion property : properties) {
          const auto selected = animations.resolveChannel(region, property, now);
          if (selected.handle) applyEye(eye, selected, idle);
        }
      };
      composeEye(pose.leftEye, Animation::Region::LeftEye);
      composeEye(pose.rightEye, Animation::Region::RightEye);
#else
      applyEye(pose.leftEye, animations.resolve(Animation::Region::LeftEye, now), idle);
      applyEye(pose.rightEye, animations.resolve(Animation::Region::RightEye, now), idle);
#endif
    }
    if (!speaking && !glitchFrame) {
      const auto mouth = animations.resolve(Animation::Region::Mouth, now);
      if (mouth.handle) pose.mouthOverride = mouth.value;
    }
  }
#else
  if (config.idleMotion && !blinkState && !transitionActive && !boopActive) {
    const uint32_t phase = (now - blinkTime) % 6000;
    pose.eyeOffset = phase >= 2200 && phase < 2500 ? 1 : phase >= 4400 && phase < 4700 ? -1 : 0;
  }
#endif
  return pose;
}

bool Core::frameDue(uint32_t now, bool waking) const {
#if PROTOGEN_ANIMATION_V2
  return waking || rising || now - lastFrameTime >= nextVisualDeadline() - lastFrameTime;
#else
  return waking || rising || now - lastFrameTime >= (transitionVisiblePending ? 2u : 16u);
#endif
}

void Core::commitFrameTime(uint32_t now) {
  lastFrameTime = now;
  rising = false;
}
}
