#pragma once
#include "src/HelmetProtocol/FlightRecorder.h"

#if PROTOGEN_FLIGHT_RECORDER
#include <atomic>
#include "Arduino.h"

namespace P3Teensy {
constexpr unsigned Capacity = 480;
uint32_t clockUs();
void emit(uint32_t at, Flight::Event event, uint16_t detail, uint32_t value);
void beginLoop();
void finishLoop();
extern Flight::Timing timing[Flight::StageCount];

// Sole producer is the ADC ISR. Relaxed atomic increments transfer a modulo
// count only. No other memory is published, and the owner never resets it.
static_assert(std::atomic<uint32_t>::is_always_lock_free, "ISR counter must be lock-free");
inline std::atomic<uint32_t> audioDrops{0};
inline void notifyAudioDrop() {
  // Exactly one ISR writes this counter. The loop only loads it, so no
  // read-modify-write retry loop, interrupt mask, or release fence is needed.
  audioDrops.store(audioDrops.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}
}
#define P3_TEENSY_SCOPE(variable, stage) \
  Flight::Scope variable(P3Teensy::timing[Flight::stage], P3Teensy::clockUs)
#define P3_TEENSY_CALL(stage, expression) \
  do { P3_TEENSY_SCOPE(p3Call, stage); expression; } while (false)
#else
#define P3_TEENSY_SCOPE(variable, stage)
#define P3_TEENSY_CALL(stage, expression) do { expression; } while (false)
#endif
