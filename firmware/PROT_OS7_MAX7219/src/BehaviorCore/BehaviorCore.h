#pragma once

#include "../../BlinkTiming.h"
#include "../../Configuration.h"
#include "AnimationV2.h"

namespace Behavior {

#if PROTOGEN_ANIMATION_V2
struct EyeMotion {
  int8_t x = 0, y = 0;
  uint8_t closure = 0;
  int8_t expressionOverride = -1;
};
#endif
struct Pose {
  uint8_t expression = ExpressionHappy;
  bool boop = false, speaking = false;
  uint8_t closure = 0, glitch = 0, mouthMode = 0, aperture = 0;
  int8_t eyeOffset = 0;
#if PROTOGEN_ANIMATION_V2
  EyeMotion leftEye, rightEye;
  int8_t mouthOverride = -1;
  bool showsExpression(uint8_t target) const {
    return (leftEye.expressionOverride < 0 || leftEye.expressionOverride == target) &&
           (rightEye.expressionOverride < 0 || rightEye.expressionOverride == target);
  }
#endif
};

struct TransitionPose {
  uint8_t expression, closure;
  bool active;
};

TransitionPose transitionPose(uint32_t elapsed, uint16_t duration, uint8_t from,
                              uint8_t target, uint8_t initialClosure, bool earlyReveal);

struct Core {
  uint32_t blinkTime = 0, blinkSpeed = 0, nextBlinkInterval = Settings::BlinkIntervalMs;
  uint32_t glitchDelay = 0, glitchSpeed = 0, boopTime = 0, previousMillis = 0;
  uint32_t transitionStarted = 0, lastFrameTime = 0, blinkRandomStarted = 0;
  uint32_t blinkRandomWords[2] = {};
  BlinkTiming::Timing blinkTiming;
  uint16_t remoteBoopDuration = 1200;
  uint8_t expression = Settings::StartupExpression, blinkClosure = 0, glitchFrame = 0;
  uint8_t transitionFrom = 0, transitionStartClosure = 0, blinkRandomCount = 0;
  bool sleeping = false, rising = true;
  bool boopState = false, boopArmed = false, boopActive = false, boopFromRemote = false;
  bool blinkState = false, blinkRandomPending = false, blinkDoublePending = false;
  bool transitionActive = false, transitionVisiblePending = false;

#if PROTOGEN_ANIMATION_V2
  Animation::Table animations{};
  Animation::Handle settleTrack;
  uint32_t settleRequestedAt = 0;
  bool settlePending = false;
  void requestSettle(uint32_t now);
  void advanceMotion(uint32_t now);
  // V2 samples on the existing frame cadence, including the legacy feedback
  // window. It never requests a separate or faster render opportunity.
  uint32_t nextVisualDeadline() const { return lastFrameTime + (transitionVisiblePending ? 2u : 16u); }
#endif

  void resetAnimationAt(uint32_t now);
  void scheduleBlink(const RuntimeConfig &config, uint32_t sampledNow);
  void acceptBlinkWord(uint32_t word);
  bool finishBlinkEntropy(const RuntimeConfig &config);
  // True means the shell should schedule the next ordinary blink at its
  // existing clock-read point. False includes the already-scheduled follow-up.
  bool advanceBlink(uint32_t now);
  void advanceGlitch(uint32_t now, bool speaking);
  void observeBoop(int reading, const RuntimeConfig &config);
  void beginBoop(uint32_t now);
  void beginRemoteBoop(uint32_t now, uint16_t duration);
  void expireBoop(uint32_t now, uint16_t localDuration);
  bool wantsLocalBoop() const { return boopState && boopArmed && !boopActive; }
  bool releaseHeldBoop(uint16_t mode);
  bool canTransition(bool visorVisible, uint16_t duration) const;
  void beginTransition(uint32_t now, uint8_t displayedExpression, uint8_t displayedClosure);
  Pose poseAt(uint32_t now, const RuntimeConfig &config, bool speaking, uint8_t aperture,
              bool earlyReveal
#if PROTOGEN_ANIMATION_V2
              , bool legacyObservationPending = false
#endif
              );
  bool frameDue(uint32_t now, bool waking) const;
  void commitFrameTime(uint32_t now);
};
}
