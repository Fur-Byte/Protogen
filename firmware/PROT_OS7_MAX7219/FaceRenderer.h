#pragma once
#include <stdint.h>
#include <string.h>
#include "Configuration.h"

// Logical canvas, before the existing physical tile rotations. No hardware I/O.
// Icons.h is included by the caller, keeping the original artwork byte-for-byte.
namespace Face {
constexpr int Width = 112, Height = 8, RowBytes = Width / 8;
constexpr int EyeLeft = 96, MouthLeft = 64, NoseLeft = 56;
constexpr int NoseRight = 48, MouthRight = 16, EyeRight = 0;
struct Frame { uint8_t bytes[RowBytes * Height] = {}; };
struct Pose {
  uint8_t expression = ExpressionHappy;
  bool boop = false, speaking = false;
  uint8_t closure = 0, glitch = 0, mouthMode = 0, aperture = 0;
  int8_t eyeOffset = 0;
};
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
inline void eyes(Frame &frame, int x, const uint8_t *data, uint8_t closure, int offset) {
  const int cut = closure > 8 ? 8 : closure;
  for (int y = cut; y < Height; ++y)
    for (int i = 0; i < 16; ++i) {
      const int shifted = i + offset;
      if (shifted >= 0 && shifted < 16 && (data[y * 2 + i / 8] & (0x80 >> (i & 7))))
        put(frame, x + shifted, y);
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
inline void compose(Frame &frame, const Pose &pose) {
  memset(frame.bytes, 0, sizeof(frame.bytes));
  bitmap(frame, NoseLeft, 8, noseL);
  bitmap(frame, NoseRight, 8, nose);
  if (pose.speaking && !pose.boop) {
    if (pose.mouthMode == 1) {
      amplitudeMouth(frame, MouthLeft, pose.aperture, true);
      amplitudeMouth(frame, MouthRight, pose.aperture, false);
    } else {
      bitmap(frame, MouthLeft + 16, 16, fftIcon2L);
      bitmap(frame, MouthLeft, 16, fftIcon1L);
      bitmap(frame, MouthRight + 16, 16, fftIcon1);
      bitmap(frame, MouthRight, 16, fftIcon2);
    }
  } else {
    const uint8_t *left = pose.glitch == 1 ? Glitch1L : pose.glitch == 2 ? Glitch2L : mawL;
    const uint8_t *right = pose.glitch == 1 ? Glitch1 : pose.glitch == 2 ? Glitch2 : maw;
    bitmap(frame, MouthLeft, 32, pose.boop ? mawL : left);
    bitmap(frame, MouthRight, 32, pose.boop ? maw : right);
  }
  const uint8_t expression = pose.boop ? ExpressionVwv : pose.expression;
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
}
}
