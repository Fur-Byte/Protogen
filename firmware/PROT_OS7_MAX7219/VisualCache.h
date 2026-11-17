#pragma once
#include "FaceRenderer.h"
namespace Face {
#if PROTOGEN_ANIMATION_V2
inline bool sameEye(const Behavior::EyeMotion &a, const Behavior::EyeMotion &b) {
  return a.x == b.x && a.y == b.y && a.closure == b.closure &&
      a.expressionOverride == b.expressionOverride;
}
#endif
inline bool samePose(const Pose &a, const Pose &b) {
  return a.expression == b.expression && a.boop == b.boop &&
      a.speaking == b.speaking && a.closure == b.closure &&
      a.glitch == b.glitch && a.mouthMode == b.mouthMode &&
      a.aperture == b.aperture && a.eyeOffset == b.eyeOffset
#if PROTOGEN_ANIMATION_V2
      && sameEye(a.leftEye, b.leftEye) && sameEye(a.rightEye, b.rightEye)
      && a.mouthOverride == b.mouthOverride
#endif
      ;
}
struct VisualCache {
  Pose previous;
  AudioSnapshot spectrum{};
  bool valid = false;
  bool unchanged(const Pose &pose, const AudioSnapshot &audio) const {
    if (!valid || !samePose(previous, pose)) return false;
    return !pose.speaking || pose.boop || pose.mouthMode == 1 ||
        !memcmp(&spectrum, &audio, sizeof(audio));
  }
  void remember(const Pose &pose, const AudioSnapshot &audio) {
    previous = pose; valid = true;
    if (pose.speaking && !pose.boop && pose.mouthMode == 0) spectrum = audio;
  }
};
}
