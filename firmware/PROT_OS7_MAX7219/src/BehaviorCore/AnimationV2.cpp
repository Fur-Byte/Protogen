#include "AnimationV2.h"

#if PROTOGEN_ANIMATION_V2
#include "../../Configuration.h"

namespace Behavior::Animation {
namespace {
constexpr uint32_t Scale = 256;

bool valid(const Request &r) {
  if (r.region > Region::FullFace || r.motion > Motion::MouthAperture ||
      r.priority > MaxPriority || r.easing > Easing::Settle || r.duration > MaxDurationMs)
    return false;
  const bool mouth = r.motion == Motion::MouthAperture;
  if (r.region != Region::FullFace && (r.region == Region::Mouth) != mouth) return false;
  int low = 0, high = 0;
  switch (r.motion) {
    case Motion::Gaze: low = -MaxHorizontal; high = MaxHorizontal; break;
    case Motion::Vertical: low = -MaxVertical; high = MaxVertical; break;
    case Motion::Lid: high = 8; break;
    case Motion::Squint: high = 4; break;
    case Motion::Expression:
      if (r.from != r.to) return false;
      high = ExpressionCount - 1; break;
    case Motion::MouthAperture: high = 3; break;
  }
  return r.from >= low && r.from <= high && r.to >= low && r.to <= high;
}

bool affects(const Request &r, Region region) {
  if (region == Region::FullFace) return false;
  if ((region == Region::Mouth) != (r.motion == Motion::MouthAperture)) return false;
  return r.region == Region::FullFace || r.region == region;
}

int8_t valueAt(const Request &r, uint32_t elapsed) {
  const int delta = (r.to - r.from) * ease(r.easing, elapsed, r.duration);
  // Symmetric rounding to nearest integer with ties away from zero.
  const int rounded = delta >= 0 ? (delta + 128) / 256 : (delta - 128) / 256;
  return static_cast<int8_t>(r.from + rounded);
}
}

uint16_t ease(Easing curve, uint32_t elapsed, uint16_t duration) {
  if (elapsed >= duration) return Scale;
  const uint32_t t = elapsed * Scale / duration;
  const uint32_t u = Scale - t;
  switch (curve) {
    case Easing::In: return t * t / Scale;
    case Easing::Out: return Scale - u * u / Scale;
    case Easing::InOut: return t * t * (3 * Scale - 2 * t) / (Scale * Scale);
    case Easing::Settle: return Scale - u * u * u / (Scale * Scale);
    case Easing::Linear: return t;
  }
  return t;
}

void Table::rebaseOrder() {
  // Compact only at rollover. Old order never has to be compared modulo
  // clock time, even after arbitrarily many same-timestamp admissions.
  uint8_t rank[Capacity]{};
  for (unsigned i = 0; i < Capacity; ++i)
    if (active(i))
      for (unsigned j = 0; j < Capacity; ++j)
        if (active(j) && tracks[j].order < tracks[i].order) ++rank[i];
  for (unsigned i = 0; i < Capacity; ++i)
    if (active(i)) tracks[i].order = rank[i];
  nextOrder = Capacity;
}

Start Table::start(uint32_t now, Request request) {
  if (!valid(request)) return {Admission::Invalid, {}};
  expire(now);
  for (unsigned slot = 0; slot < Capacity; ++slot) {
    auto &track = tracks[slot];
    if (active(slot) || track.generation == UINT32_MAX) continue;
    if (nextOrder == UINT32_MAX) rebaseOrder();
    track = {now, nextOrder++, track.generation + 1, request};
    activeMask |= static_cast<uint8_t>(1u << slot);
    return {Admission::Started, {static_cast<uint8_t>(slot), track.generation}};
  }
  return {Admission::Full, {}};
}

bool Table::cancel(Handle handle) {
  if (!handle || !active(handle.slot) || tracks[handle.slot].generation != handle.generation)
    return false;
  deactivate(handle.slot);
  return true;
}

void Table::expire(uint32_t now) {
  if (!activeMask) return;
  for (unsigned i = 0; i < Capacity; ++i) {
    if (!active(i)) continue;
    const uint32_t age = now - tracks[i].at;
    // A render can still hold a loop timestamp from before a replay input.
    // Ignore a future admission instead of throwing it away as very old.
    if (age <= INT32_MAX && age > tracks[i].request.duration) deactivate(i);
  }
}

Selection Table::resolve(Region region, uint32_t now) const {
  unsigned best = Capacity;
  for (unsigned i = 0; activeMask && i < Capacity; ++i) {
    const auto &track = tracks[i];
    if (!active(i) || now - track.at > track.request.duration || !affects(track.request, region))
      continue;
    if (best == Capacity || outranks(track.request.priority, track.order, i,
        tracks[best].request.priority, tracks[best].order, best)) best = i;
  }
  if (best == Capacity) return {};
  const auto &track = tracks[best];
  return {{static_cast<uint8_t>(best), track.generation}, track.request.motion,
          valueAt(track.request, now - track.at)};
}

unsigned Table::count() const {
  unsigned total = 0;
  for (unsigned i = 0; i < Capacity; ++i) total += active(i);
  return total;
}
}
#endif
