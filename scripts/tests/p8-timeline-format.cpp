#include "Timeline.h"
#include <cassert>

int main() {
  using namespace Behavior::Timeline;
  uint8_t asset[AssetBytes]{};
  asset[0] = Version;
  asset[4] = 16;
  assert(validAsset(asset, sizeof(asset)));
  asset[14] = 1;
  assert(!validAsset(asset, sizeof(asset)));
}
