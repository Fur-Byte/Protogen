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
  uint8_t spectrum[64]{};
  bool valid = false;
  bool unchanged(const Pose &pose) const {
    if (!valid || !samePose(previous, pose)) return false;
    return !pose.speaking || pose.boop || pose.mouthMode == 1 ||
        (!memcmp(spectrum, fftIcon1, 16) && !memcmp(spectrum + 16, fftIcon1L, 16) &&
         !memcmp(spectrum + 32, fftIcon2, 16) && !memcmp(spectrum + 48, fftIcon2L, 16));
  }
  void remember(const Pose &pose) {
    previous = pose; valid = true;
    if (pose.speaking && !pose.boop && pose.mouthMode == 0) {
      memcpy(spectrum, fftIcon1, 16); memcpy(spectrum + 16, fftIcon1L, 16);
      memcpy(spectrum + 32, fftIcon2, 16); memcpy(spectrum + 48, fftIcon2L, 16);
    }
  }
};
}
