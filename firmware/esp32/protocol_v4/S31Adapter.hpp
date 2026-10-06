#pragma once
#include "Interfaces.hpp"
// Contract only. There is deliberately no ESP-IDF component registration.
#if defined(PROTOGEN_V4_ACTIVATE_S31)
#error NOT_IMPLEMENTED_UNTIL_SUPPORTED_S31_SDK
#endif
namespace ProtogenV4 {
enum class PlatformStatus { NOT_IMPLEMENTED_UNTIL_SUPPORTED_S31_SDK };
struct HardwareMetrics {
  // Zero is not a measurement: validMask must identify each measured field.
  uint64_t validMask = 0;
  uint64_t stackAllocated = 0, stackMinimumFree = 0, heapCurrent = 0,
           heapMinimum = 0, heapLargestBlock = 0, keygenUs = 0, encapsUs = 0,
           decapsUs = 0, queueLatencyUs = 0, handshakeUs = 0,
           handshakeFrames = 0, radioMaxLatencyUs = 0, uartMaxLatencyUs = 0;
  uint32_t resetReason = 0, watchdogEvents = 0;
};
struct FutureS31Adapter {
  virtual ~FutureS31Adapter() = default;
  // Capability gate must be checked before constructing/starting a Core.
  // Future release adapter must independently verify SDK hardening support,
  // production provisioning, secure boot, flash encryption and secure storage.
  virtual PlatformStatus qualificationStatus() const = 0;
  virtual bool metrics(HardwareMetrics &) const = 0;
};
constexpr PlatformStatus S31IntegrationStatus =
    PlatformStatus::NOT_IMPLEMENTED_UNTIL_SUPPORTED_S31_SDK;
} // namespace ProtogenV4
