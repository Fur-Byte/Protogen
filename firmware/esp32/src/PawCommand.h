#pragma once
#include "ActionQueue.h"
#include "ApplicationStatus.h"

namespace Wireless {
enum class ActionSend { None, Blocked, Exhausted, Sent };

// Single-owner paw loop: only a successful transport admission commits
// the next sequence and removes the intent. No retry extends freshness.
template <typename Send>
ActionSend tryQueueAction(Actions &actions, const HelmetStatus &status,
                          uint32_t now, uint32_t &sequence,
                          HelmetLink::Packet &sent, uint32_t &age, Send send) {
  ActionSend outcome = ActionSend::None;
  actions.dispatch(now, [&](Intent intent) {
    if (sequence == UINT32_MAX) {
      outcome = ActionSend::Exhausted;
      return false;
    }
    if (intent.action == Actions::Next || intent.action == Actions::Previous) {
      intent.value = (status.packet.payload[4] +
                      (intent.action == Actions::Next ? 1 : 5)) % 6;
      intent.action = HelmetLink::Expression;
    } else if (intent.action == Actions::ToggleSleep) {
      intent.action = HelmetLink::Sleep;
      intent.value = !(status.packet.payload[5] & HelmetLink::Sleeping);
    }
    HelmetLink::Packet p;
    p.kind = HelmetLink::Command;
    p.length = 8;
    p.epoch = status.packet.epoch;
    p.ticket = status.packet.ticket;
    p.sequence = sequence + 1;
    HelmetLink::put32(p.payload, status.bridge);
    p.payload[4] = intent.action;
    p.payload[5] = intent.value;
    HelmetLink::put16(p.payload + 6, intent.duration);
    if (!send(p)) {
      outcome = ActionSend::Blocked;
      return false;
    }
    sequence = p.sequence;
    sent = p;
    age = now - intent.at;
    outcome = ActionSend::Sent;
    return true;
  });
  return outcome;
}
} // namespace Wireless
