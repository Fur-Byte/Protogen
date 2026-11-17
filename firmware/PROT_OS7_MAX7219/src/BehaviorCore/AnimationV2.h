#pragma once
#include <stdint.h>

#ifndef PROTOGEN_ANIMATION_V2
#define PROTOGEN_ANIMATION_V2 0
#endif
static_assert(PROTOGEN_ANIMATION_V2 == 0 || PROTOGEN_ANIMATION_V2 == 1,
              "Animation V2 must be 0 or 1.");

#if PROTOGEN_ANIMATION_V2
namespace Behavior::Animation {

constexpr unsigned Capacity = 8;
constexpr uint16_t MaxDurationMs = 5000;
constexpr uint8_t MaxPriority = 7;
constexpr int8_t MaxHorizontal = 2, MaxVertical = 1;

enum class Region : uint8_t { LeftEye, RightEye, Mouth, FullFace };
enum class Motion : uint8_t { Gaze, Vertical, Lid, Squint, Expression, MouthAperture };
enum class Easing : uint8_t { Linear, In, Out, InOut, Settle };
enum class Admission : uint8_t { Started, Invalid, Full };

struct Request {
  Region region;
  Motion motion;
  uint8_t priority;
  Easing easing;
  uint16_t duration;
  int8_t from, to;
};

struct Handle {
  uint8_t slot = Capacity;
  uint32_t generation = 0;
  explicit operator bool() const { return slot < Capacity && generation; }
};

struct Start { Admission status; Handle handle; };
struct Selection {
  Handle handle;
  Motion motion = Motion::Gaze;
  int8_t value = 0;
};

// Q8 easing, with exact 0/256 endpoints for a nonzero duration. A
// zero-duration track has its target value at the instant it is admitted.
uint16_t ease(Easing curve, uint32_t elapsed, uint16_t duration);

constexpr bool outranks(uint8_t priority, uint32_t order, unsigned slot,
                        uint8_t otherPriority, uint32_t otherOrder, unsigned otherSlot) {
  if (priority != otherPriority) return priority > otherPriority;
  if (order != otherOrder) return order > otherOrder;
  return slot < otherSlot;
}

class Table {
  struct Track {
    uint32_t at = 0, order = 0, generation = 0;
    Request request{};
  };
  Track tracks[Capacity]{};
  uint32_t nextOrder = 0;
  uint8_t activeMask = 0;

  bool active(unsigned slot) const { return (activeMask & (1u << slot)) != 0; }
  void deactivate(unsigned slot) { activeMask &= static_cast<uint8_t>(~(1u << slot)); }
  void rebaseOrder();

public:
  // Initial order is injectable for boundary tests. Production starts at zero.
  explicit constexpr Table(uint32_t initialOrder = 0) : nextOrder(initialOrder) {}
  Start start(uint32_t now, Request request);
  bool cancel(Handle handle);
  void expire(uint32_t now);
  Selection resolve(Region region, uint32_t now) const;
  unsigned count() const;
};
}
#endif
