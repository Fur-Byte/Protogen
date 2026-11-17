#pragma once
#include "FaceRenderer.h"
namespace Face {
inline bool samePose(const Pose &a, const Pose &b) {
  return a.expression == b.expression && a.boop == b.boop &&
      a.speaking == b.speaking && a.closure == b.closure &&
      a.glitch == b.glitch && a.mouthMode == b.mouthMode &&
      a.aperture == b.aperture && a.eyeOffset == b.eyeOffset;
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
