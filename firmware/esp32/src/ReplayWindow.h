#pragma once
#include "WirelessConfig.h"
#include "ReplayCore.h"
static_assert(Wireless::ReplayWindow == PROTOGEN_REPLAY_WIDTH, "Replay width mismatch");
static_assert(Wireless::MaxSequence == PROTOGEN_MAX_SEQUENCE, "Sequence limit mismatch");
namespace Wireless {
class Replay {
  uint64_t top = 0, bits = 0;

public:
  void reset() { top = bits = 0; }
  bool allows(uint64_t sequence) const { return protogenReplayAllows(top, bits, sequence); }
  // Call only after successful authentication AND application decoding.
  void accept(uint64_t sequence) { protogenReplayAccept(&top, &bits, sequence); }
};
} // namespace Wireless
