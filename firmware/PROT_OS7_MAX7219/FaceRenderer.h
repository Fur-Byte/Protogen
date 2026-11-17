#pragma once
#include <stdint.h>
#include <string.h>
#include "src/BehaviorCore/BehaviorCore.h"

// Logical canvas, before the existing physical tile rotations. No hardware I/O.
// Icons.h is included by the caller, keeping the original artwork byte-for-byte.
namespace Face {
constexpr int Width = 112, Height = 8, RowBytes = Width / 8;
constexpr int EyeLeft = 96, MouthLeft = 64, NoseLeft = 56;
constexpr int NoseRight = 48, MouthRight = 16, EyeRight = 0;
struct Frame { uint8_t bytes[RowBytes * Height] = {}; };
using Pose = Behavior::Pose;
// Four mutable FFT bitmaps are captured by the shell at frame observation.
struct AudioSnapshot {
  uint8_t first[16], firstLeft[16], second[16], secondLeft[16];
};
static_assert(sizeof(AudioSnapshot) == 64, "The four legacy spectrum bitmaps are 16 bytes each.");
inline bool pixel(const Frame &frame, int x, int y) {
  return (frame.bytes[y * RowBytes + x / 8] & (0x80 >> (x & 7))) != 0;
}
inline void put(Frame &frame, int x, int y) {
  if (x >= 0 && x < Width && y >= 0 && y < Height)
    frame.bytes[y * RowBytes + x / 8] |= 0x80 >> (x & 7);
}
inline void bitmap(Frame &frame, int x, int width, const uint8_t *data) {
  for (int y = 0; y < Height; ++y)
    for (int i = 0; i < width; ++i)
      if (data[y * (width / 8) + i / 8] & (0x80 >> (i & 7))) put(frame, x + i, y);
}
inline void eyes(Frame &frame, int x, const uint8_t *data, uint8_t closure, int offset
#if PROTOGEN_ANIMATION_V2
                 , int vertical = 0
#endif
                 ) {
  const int cut = closure > 8 ? 8 : closure;
  for (int y = cut; y < Height; ++y) {
#if PROTOGEN_ANIMATION_V2
    const int destinationY = y + vertical;
    if (destinationY < 0 || destinationY >= Height) continue;
#else
    const int destinationY = y;
#endif
    for (int i = 0; i < 16; ++i) {
      const int shifted = i + offset;
      if (shifted >= 0 && shifted < 16 && (data[y * 2 + i / 8] & (0x80 >> (i & 7))))
        put(frame, x + shifted, destinationY);
    }
  }
}
inline void amplitudeMouth(Frame &frame, int x, uint8_t aperture, bool mirror) {
  const int opening = aperture > 3 ? 3 : aperture;
  for (int i = 0; i < 32; ++i) {
    const int inner = (i >= 5 && i < 27) ? opening : opening / 2;
    const int column = mirror ? 31 - i : i;
    put(frame, x + column, 3 - inner);
    put(frame, x + column, 4 + inner);
  }
}
#if PROTOGEN_ANIMATION_V2
inline void renderEye(Frame &frame, const Pose &pose, bool left, Behavior::EyeMotion motion) {
  if (pose.boop) motion = {};
  const uint8_t expression = pose.boop ? static_cast<uint8_t>(ExpressionVwv)
      : motion.expressionOverride >= 0 ? static_cast<uint8_t>(motion.expressionOverride) : pose.expression;
  const uint8_t *data = left ? EyeL : Eye;
  if (expression == ExpressionSurprised) data = left ? SpookedL : Spooked;
  if (expression == ExpressionAngry) data = left ? AngryL : Angry;
  if (expression == ExpressionVwv) data = left ? vwvL : vwv;
  uint8_t closure = pose.boop ? 0 : pose.closure;
  if (motion.closure > closure) closure = motion.closure;
  if (expression == ExpressionSleepy && closure < 4) closure = 4;
  if (expression == ExpressionWink && !left) {
    if (closure < 7) closure = 7;
    // The frozen right wink has one lit row at Y=7. A downward shift
    // would erase its entire identity, so retain its original Y position.
    if (motion.y > 0) motion.y = 0;
  }
  eyes(frame, left ? EyeLeft : EyeRight, data, closure, motion.x, motion.y);
}
#endif
inline void compose(Frame &frame, const Pose &pose, const AudioSnapshot &audio) {
  memset(frame.bytes, 0, sizeof(frame.bytes));
  bitmap(frame, NoseLeft, 8, noseL);
  bitmap(frame, NoseRight, 8, nose);
  if (pose.speaking && !pose.boop) {
    if (pose.mouthMode == 1) {
      amplitudeMouth(frame, MouthLeft, pose.aperture, true);
      amplitudeMouth(frame, MouthRight, pose.aperture, false);
    } else {
      bitmap(frame, MouthLeft + 16, 16, audio.secondLeft);
      bitmap(frame, MouthLeft, 16, audio.firstLeft);
      bitmap(frame, MouthRight + 16, 16, audio.first);
      bitmap(frame, MouthRight, 16, audio.second);
    }
#if PROTOGEN_ANIMATION_V2
  } else if (!pose.boop && !pose.glitch && pose.mouthOverride >= 0) {
    amplitudeMouth(frame, MouthLeft, static_cast<uint8_t>(pose.mouthOverride), true);
    amplitudeMouth(frame, MouthRight, static_cast<uint8_t>(pose.mouthOverride), false);
#endif
  } else {
    const uint8_t *left = pose.glitch == 1 ? Glitch1L : pose.glitch == 2 ? Glitch2L : mawL;
    const uint8_t *right = pose.glitch == 1 ? Glitch1 : pose.glitch == 2 ? Glitch2 : maw;
    bitmap(frame, MouthLeft, 32, pose.boop ? mawL : left);
    bitmap(frame, MouthRight, 32, pose.boop ? maw : right);
  }
#if PROTOGEN_ANIMATION_V2
  renderEye(frame, pose, true, pose.leftEye);
  renderEye(frame, pose, false, pose.rightEye);
#else
  const uint8_t expression = pose.boop ? static_cast<uint8_t>(ExpressionVwv) : pose.expression;
  const uint8_t *left = EyeL, *right = Eye;
  if (expression == ExpressionSurprised) { left = SpookedL; right = Spooked; }
  if (expression == ExpressionAngry) { left = AngryL; right = Angry; }
  if (expression == ExpressionVwv) { left = vwvL; right = vwv; }
  uint8_t leftCut = pose.boop ? 0 : pose.closure, rightCut = leftCut;
  if (expression == ExpressionSleepy) {
    if (leftCut < 4) leftCut = 4;
    if (rightCut < 4) rightCut = 4;
  }
  if (expression == ExpressionWink && rightCut < 7) rightCut = 7;
  eyes(frame, EyeLeft, left, leftCut, pose.boop ? 0 : pose.eyeOffset);
  eyes(frame, EyeRight, right, rightCut, pose.boop ? 0 : pose.eyeOffset);
#endif
}
}
