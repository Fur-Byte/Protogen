#include "TimelineAsset.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace Behavior::Animation;
using namespace Behavior::Timeline;

int main() {
  const auto *data = ProtogenAsset::clip;
  static_assert(sizeof(ProtogenAsset::clip) == 336);
  assert(validAsset(data, AssetBytes));
  std::array<uint8_t, AssetBytes> bad;
  std::copy(data, data + AssetBytes, bad.begin());
  bad[0] = 2;
  assert(!validAsset(bad.data(), bad.size()));
  bad[0] = 1;
  bad[14] = 1;
  assert(!validAsset(bad.data(), bad.size()));
  assert(!validAsset(data, AssetBytes - 1));

  Table table;
  Player player;
  const uint32_t origin = UINT32_MAX - 8;
  assert(player.start(table, data, AssetBytes, origin));
  player.advance(table, origin + 16);
  assert(player.progress().started == 3);
  assert(table.resolveChannel(Region::LeftEye, Motion::Gaze, origin + 16).value == -2);
  assert(table.resolveChannel(Region::LeftEye, Motion::Lid, origin + 16).value == 3);
  assert(table.resolveChannel(Region::LeftEye, Motion::Expression, origin + 16).value == 2);
  assert(!table.resolveChannel(Region::RightEye, Motion::Gaze, origin + 16).handle);

  player.advance(table, origin + 2016);
  assert(table.resolveChannel(Region::LeftEye, Motion::Gaze, origin + 2016).value == 0);
  player.cancel(table);
  assert(player.progress().state == State::Cancelled && table.count() == 0);

  assert(player.start(table, data, AssetBytes, origin));
  player.advance(table, origin + 4200);
  assert(player.progress().next == 4 && player.progress().skipped == 4);
  player.advance(table, origin + 4224);
  assert(player.progress().started == 1);
  player.advance(table, origin + 5201);
  assert(player.progress().state == State::Complete);

  Table occupied;
  for (unsigned i = 0; i < Capacity; ++i)
    occupied.start(origin, {Region::FullFace, Motion::Gaze, 7, Easing::Linear, 5000, 1, 1});
  assert(player.start(occupied, data, AssetBytes, origin));
  player.advance(occupied, origin + 16);
  assert(occupied.count() == 8 && player.progress().rejected == 3 &&
         player.progress().last == Outcome::Full);
  player.cancel(occupied);
  assert(occupied.count() == 8); // Only timeline-owned generations can be cancelled.

  // A stale owned handle may point to a slot already reused by another source.
  Table reused;
  assert(player.start(reused, data, AssetBytes, origin));
  player.advance(reused, origin + 16);
  const auto newer = reused.start(origin + 4017, {Region::RightEye, Motion::Lid, 2, Easing::Linear, 8, 1, 1});
  assert(newer.handle.slot == 0 && newer.handle.generation == 2);
  player.cancel(reused);
  assert(reused.cancel(newer.handle));

  Table layered;
  const auto a = layered.start(origin, {Region::FullFace, Motion::Lid, 4, Easing::Linear, 50, 1, 1});
  layered.start(origin, {Region::LeftEye, Motion::Squint, 4, Easing::Linear, 5, 4, 4});
  layered.start(origin, {Region::LeftEye, Motion::Gaze, 3, Easing::Linear, 10, 2, -2});
  assert(layered.resolveChannel(Region::LeftEye, Motion::Lid, origin).value == 4);
  assert(layered.resolveChannel(Region::RightEye, Motion::Lid, origin).handle.slot == a.handle.slot);
  assert(layered.resolveChannel(Region::LeftEye, Motion::Lid, origin+6).value == 1);

  std::cout << "{\"player_bytes\":" << sizeof(Player) << ",\"asset_bytes\":" << AssetBytes << "}\n";
  // An ideal service opportunity must not discard the latter half of
  // a legal eight-track, zero-duration group.
  std::array<uint8_t, AssetBytes> simultaneous{};
  simultaneous[0] = 1;
  simultaneous[2] = 8;
  simultaneous[4] = 100;
  for (unsigned i = 0; i < 8; ++i) {
    simultaneous[HeaderBytes + i*EventBytes + 6] = i;
    simultaneous[HeaderBytes + i*EventBytes + 9] = i == 7 ? 2 : 0;
  }
  Table collision;
  assert(player.start(collision, simultaneous.data(), simultaneous.size(), origin));
  player.advance(collision, origin);
  assert(player.progress().started == 8 && player.progress().skipped == 0);
  assert(collision.resolveChannel(Region::LeftEye, Motion::Gaze, origin).value == 2);

  std::array<uint8_t, AssetBytes> backlog{};
  backlog[0] = 1; backlog[2] = 32; backlog[4] = 100;
  for (unsigned i = 0; i < 32; ++i) backlog[HeaderBytes+i*EventBytes] = i;
  assert(player.start(collision, backlog.data(), backlog.size(), origin));
  player.advance(collision, origin+40);
  assert(player.progress().next == EventsPerLoop && player.progress().skipped == EventsPerLoop);

  backlog[HeaderBytes + 2] = 100;
  Table draining;
  Player drainingPlayer;
  const auto unrelated = draining.start(origin, {Region::RightEye, Motion::Lid, 0, Easing::Linear,
                                                250, 1, 1});
  assert(drainingPlayer.start(draining, backlog.data(), backlog.size(), origin));
  drainingPlayer.advance(draining, origin);
  assert(draining.count() == 2);
  drainingPlayer.advance(draining, origin + 101);
  assert(drainingPlayer.progress().state == State::Running);
  assert(drainingPlayer.progress().next == 1 + EventsPerLoop);
  assert(draining.count() == 1);
  for (unsigned loop = 0; loop < 3; ++loop)
    drainingPlayer.advance(draining, origin + 102 + loop);
  assert(drainingPlayer.progress().state == State::Complete && drainingPlayer.progress().skipped == 31);
  assert(draining.cancel(unrelated.handle));

  // Numerical conformance stream. Python's independent rational oracle
  // checks signed division at all integer ticks and every easing endpoint.
  for (unsigned curve = 0; curve < 5; ++curve)
    for (int first = -2; first <= 2; ++first)
      for (int last = -2; last <= 2; ++last) {
        Table motion;
        motion.start(origin, {Region::LeftEye, Motion::Gaze, 0, static_cast<Easing>(curve),
                              31, int8_t(first), int8_t(last)});
        for (uint32_t time = 0; time <= 31; ++time)
          std::cout << curve << ' ' << first << ' ' << last << ' ' << time << ' '
                    << int(motion.resolveChannel(Region::LeftEye, Motion::Gaze, origin + time).value) << '\n';
      }
}
