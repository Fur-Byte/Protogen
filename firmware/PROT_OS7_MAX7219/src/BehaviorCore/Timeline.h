#pragma once
#include "AnimationV2.h"
#include <stddef.h>

#if PROTOGEN_ANIMATION_TIMELINES
namespace Behavior::Timeline {
constexpr unsigned Version = 1, MaxEvents = 32, HeaderBytes = 16, EventBytes = 10;
constexpr unsigned AssetBytes = HeaderBytes + MaxEvents * EventBytes;
constexpr unsigned EventsPerLoop = Animation::Capacity;
static_assert(AssetBytes == 336 && sizeof(uint8_t) == 1 && sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4,
              "Timeline v1 wire size");

constexpr uint16_t word(const uint8_t *data) {
  return uint16_t(data[0]) | (uint16_t(data[1]) << 8);
}

constexpr int8_t signedByte(uint8_t value) {
  return static_cast<int8_t>(value < 128 ? int(value) : int(value) - 256);
}

struct Event {
  uint16_t at;
  Animation::Request motion;
};

constexpr Event readEvent(const uint8_t *data, unsigned index) {
  const auto *p = data + HeaderBytes + index * EventBytes;
  return {word(p),
          {static_cast<Animation::Region>(p[4]), static_cast<Animation::Motion>(p[5]), p[6],
           static_cast<Animation::Easing>(p[7]), word(p + 2), signedByte(p[8]), signedByte(p[9])}};
}

namespace Detail {
// Callers must supply a non-null pointer; the length is checked before reading.
constexpr bool validAssetFields(const uint8_t *data, size_t bytes) {
  if (bytes != AssetBytes || word(data) != Version || data[2] > MaxEvents || data[3] > 1 || !word(data + 4) ||
      word(data + 4) > 60000 || data[10] > 5 || data[11] > 1 || data[12] > 1 ||
      ((unsigned(data[3]) << 8) | data[13]) > 500 || data[14] || data[15])
    return false;
  uint16_t previous = 0;
  for (unsigned i = 0; i < data[2]; ++i) {
    const auto e = readEvent(data, i);
    if (e.at < previous || !Animation::valid(e.motion) || uint32_t(e.at) + e.motion.duration > word(data + 4))
      return false;
    previous = e.at;
    unsigned active = 0;
    for (unsigned j = 0; j <= i; ++j) {
      const auto earlier = readEvent(data, j);
      if (uint32_t(earlier.at) + earlier.motion.duration >= e.at && ++active > Animation::Capacity)
        return false;
    }
  }
  for (unsigned i = HeaderBytes + data[2] * EventBytes; i < AssetBytes; ++i)
    if (data[i]) return false;
  return true;
}

}

template <size_t N> constexpr bool validAsset(const uint8_t (&data)[N]) {
  return Detail::validAssetFields(data, N);
}

constexpr bool validAsset(const uint8_t *data, size_t bytes) {
  return data && Detail::validAssetFields(data, bytes);
}

enum class State : uint8_t { Idle, Running, Complete, Cancelled };
enum class Outcome : uint8_t { None, Started, Full, Skipped, Invalid };
struct Progress {
  State state = State::Idle;
  unsigned next = 0, started = 0, rejected = 0, skipped = 0;
  Outcome last = Outcome::None;
  Outcome outcomes[MaxEvents]{};
};

// Asset storage is immutable and must outlive playback. The owner of the
// behavior core is also the exclusive owner of playback and its handles.
class Player {
  const uint8_t *asset = nullptr;
  uint32_t origin = 0;
  Animation::Handle owned[Animation::Capacity]{};
  Progress status;

  void cancelTracks(Animation::Table &table);

public:
  bool start(Animation::Table &table, const uint8_t *data, size_t size, uint32_t now);
  void cancel(Animation::Table &table);
  void advance(Animation::Table &table, uint32_t now);
  const Progress &progress() const {
    return status;
  }
};
}
#endif
