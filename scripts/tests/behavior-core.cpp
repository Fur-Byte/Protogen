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
}
