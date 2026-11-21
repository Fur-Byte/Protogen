#pragma once
#include "HelmetProtocol.h"

namespace Wireless {
inline HelmetLink::Packet extendHelmetStatus(const HelmetLink::Packet &p, int remainingPercent,
                                             uint16_t faults) {
  auto extended = p;
  if (p.kind == HelmetLink::Status && p.length == HelmetLink::StatusBytes) {
    extended.length = HelmetLink::ExtendedStatusBytes;
    HelmetLink::put16(extended.payload + HelmetLink::StatusBytes,
                      remainingPercent < 0 ? 0xffff : remainingPercent);
    HelmetLink::put16(extended.payload + HelmetLink::StatusBytes + 2, faults);
  }
  return extended;
}

// Helmet ESP cache remains unavailable until a status framed AFTER the matched
// UART ACK. This uses existing ordering/epochs; it adds no wire-format field.
class UartStatusFence {
  uint32_t sequence = 0, epoch = 0, ticket = 0, at = 0;
  uint8_t payload[8]{};
  enum Phase : uint8_t { Idle, AwaitAck, AwaitStatus } phase = Idle;

public:
  void clear() {
    phase = Idle;
    sequence = epoch = 0;
  }
  bool retry(const HelmetLink::Packet &p) const {
    return waiting() && p.kind == HelmetLink::Command && p.length == 8 && p.sequence == sequence &&
           p.epoch == epoch && p.ticket == ticket && !memcmp(p.payload, payload, 8);
  }
  void queued(const HelmetLink::Packet &p, uint32_t now = 0) {
    if (!retry(p)) at = now;
    sequence = p.sequence;
    epoch = p.epoch;
    ticket = p.ticket;
    memcpy(payload, p.payload, 8);
    phase = AwaitAck;
  }
  bool poll(uint32_t now) {
    if (waiting() && now - at >= HelmetLink::FreshMs) {
      clear();
      return true;
    }
    return false;
  }
  bool waiting() const {
    return phase != Idle;
  }
  bool awaitingStatus() const {
    return phase == AwaitStatus;
  }
  bool ack(const HelmetLink::Packet &p) {
    if (phase != AwaitAck || p.sequence != sequence || p.epoch != epoch) return false;
    phase = AwaitStatus;
    return true;
  }
  bool status(const HelmetLink::Packet &p) {
    if (!waiting()) return true;
    if (p.epoch != epoch) {
      clear();
      return true;
    }
    if (phase == AwaitAck) return false;
    clear();
    return true;
  }
};
struct HelmetStatus {
  HelmetLink::Packet packet{};
  uint32_t bridge = 0, at = 0;
  bool available = false, expecting = false;
  uint8_t expectedAction = 0, expectedValue = 0;
  uint32_t expectedAt = 0;
  static constexpr uint32_t AckSettleMs = 150;
  void expectAfterAck(const HelmetLink::Packet &command, uint8_t result, uint32_t now) {
    available = false;
    expecting = result == HelmetLink::Accepted || result == HelmetLink::Duplicate;
    expectedAction = command.payload[4];
    expectedValue = command.payload[5];
    expectedAt = now;
  }
  bool expected(const HelmetLink::Packet &p) const {
    if (expectedAction == HelmetLink::Expression) return p.payload[4] == expectedValue;
    if (expectedAction == HelmetLink::Sleep)
      return bool(p.payload[5] & HelmetLink::Sleeping) == bool(expectedValue);
    if (expectedAction == HelmetLink::MicControl)
      return bool(p.payload[5] & HelmetLink::MicEnabled) == bool(expectedValue);
    if (expectedAction == HelmetLink::Boop) return p.payload[5] & HelmetLink::Booping;
    return true;
  }

  bool receive(const HelmetLink::Packet &p, uint32_t now, bool &changed) {
    changed = false;
    if (!HelmetLink::validHelmetStatus(p)) return false;
    const uint32_t next = HelmetLink::u32(p.payload);
    changed = (packet.epoch && p.epoch != packet.epoch) || (bridge && next != bridge);
    if (!changed) {
      if (expecting && now - expectedAt < AckSettleMs && !expected(p)) return false;
      // Operational status counters are independent of GCM replay sequencing.
      // A delayed accepted frame must not undo a newer displayed state.
      if (packet.epoch && (packet.sequence || p.sequence) && int32_t(p.sequence - packet.sequence) < 0)
        return false;
    }
    expecting = false;
    bridge = next;
    packet = p;
    at = now;
    available = true;
    return true;
  }
};
} // namespace Wireless
