#include "Timeline.h"

#if PROTOGEN_ANIMATION_TIMELINES
namespace Behavior::Timeline {
void Player::release(Animation::Table &table) {
  for (auto &handle : owned) {
    table.cancel(handle);
    handle = {};
  }
  asset = nullptr;
}

bool Player::start(Animation::Table &table, const uint8_t *data, size_t size, uint32_t now) {
  if (!validAsset(data, size)) return false;
  release(table);
  status = {};
  asset = data;
  origin = now;
  status.state = State::Running;
  return true;
}

void Player::cancel(Animation::Table &table) {
  if (!asset) return;
  release(table);
  status.state = State::Cancelled;
}

void Player::advance(Animation::Table &table, uint32_t now) {
  if (!asset) return;
  const uint32_t age = now - origin;
  if (age > INT32_MAX) return;
  const bool ended = age > word(asset + 4);
  if (ended)
    for (auto &handle : owned) { table.cancel(handle); handle = {}; }
  for (unsigned work = 0; work < EventsPerLoop && status.next < asset[2]; ++work) {
    const auto event = readEvent(asset, status.next);
    if (event.at > age) break;
    if (ended || age > uint32_t(event.at) + event.motion.duration) {
      ++status.skipped;
      status.last = Outcome::Skipped;
    } else {
      const auto result = table.startAt(now, origin + event.at, event.motion);
      if (result.status == Animation::Admission::Started) {
        owned[result.handle.slot] = result.handle;
        ++status.started;
        status.last = Outcome::Started;
      } else {
        ++status.rejected;
        status.last = result.status == Animation::Admission::Full ? Outcome::Full : Outcome::Invalid;
      }
    }
    status.outcomes[status.next++] = status.last;
  }
  if (ended && status.next == asset[2]) {
    release(table);
    status.state = State::Complete;
  }
}
}
#endif
