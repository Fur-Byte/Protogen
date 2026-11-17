#include "AnimationV2.h"
#include <cassert>
int main() {
  using namespace Behavior::Animation;
  assert(ease(Easing::Linear, 10, 20) == 128);
  assert(ease(Easing::In, 10, 20) == 64);
  Table table;
  const Request gaze{Region::LeftEye, Motion::Gaze, 1, Easing::Linear, 100, 0, 2};
  const auto a = table.start(0, gaze);
  assert(a.status == Admission::Started);
  assert(table.resolve(Region::LeftEye, 50).value == 1);
}
