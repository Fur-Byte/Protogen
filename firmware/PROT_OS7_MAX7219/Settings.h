#pragma once
#include <stdint.h>

// User adjustments. Pin assignments, panel rotations, and artwork stay in the sketch.
// Optional hardware is still enabled by the three #define lines in the main .ino.
namespace Settings {
constexpr int Brightness = 1;              // MAX7219 intensity: 0 (dimmest) .. 15.
constexpr int StartupExpression = 0;       // 0 happy, 1 surprised, 2 angry, 3 vwv.
constexpr uint32_t ButtonDebounceMs = 25;
constexpr uint32_t ButtonHoldMs = 1200;    // Hold to sleep; tap or hold to wake.
constexpr uint32_t VisorDebounceMs = 20;
constexpr uint32_t IndicatorMs = 1000;
constexpr int BoopThreshold = 500;         // Trigger at/below this 10-bit ADC value.
constexpr int BoopReleaseThreshold = 550; // Must rise to this value to release/rearm.
constexpr uint32_t BoopPollMs = 5;
constexpr uint32_t BoopDurationMs = 6000;
constexpr int MicrophoneSensitivity = 65;
constexpr uint32_t SpeakingHoldMs = 100;
constexpr uint32_t BlinkIntervalMs = 9000;
constexpr uint32_t BlinkStepMs = 20;
constexpr uint32_t GlitchIntervalMs = 7000;
constexpr uint32_t GlitchStepMs = 100;
// MAX7219 SPI clock rate (max 10 MHz). Reduce to 4 MHz if wiring is long.
// TODO: Verify DIN/CLK signal integrity on the assembled 14-module chain at 8 MHz.
//       If display artifacts appear, set SpiFrequencyHz = 4000000UL and rebuild.
constexpr uint32_t SpiFrequencyHz = 8000000UL;

static_assert(Brightness >= 0 && Brightness <= 15, "Brightness must be 0..15.");
static_assert(StartupExpression >= 0 && StartupExpression < 4,
              "StartupExpression must be 0..3 (0=happy, 1=surprised, 2=angry, 3=vwv).");
static_assert(BoopThreshold >= 0 && BoopReleaseThreshold <= 1023 &&
              BoopThreshold < BoopReleaseThreshold, "Invalid boop hysteresis thresholds.");
static_assert(MicrophoneSensitivity >= 0 && MicrophoneSensitivity <= 1023,
              "MicrophoneSensitivity must be 0..1023.");
static_assert(SpiFrequencyHz > 0 && SpiFrequencyHz <= 10000000UL,
              "SpiFrequencyHz must be 1..10000000 Hz.");
constexpr bool validInterval(uint32_t interval) {
  return interval > 0 && interval < 0x80000000UL;
}
static_assert(ButtonHoldMs > ButtonDebounceMs, "Hold must be longer than debounce.");
static_assert(validInterval(ButtonDebounceMs) && validInterval(ButtonHoldMs) &&
              validInterval(VisorDebounceMs) && validInterval(IndicatorMs) &&
              validInterval(BoopPollMs) && validInterval(BoopDurationMs) &&
              validInterval(SpeakingHoldMs) && validInterval(BlinkIntervalMs) &&
              validInterval(BlinkStepMs) && validInterval(GlitchIntervalMs) &&
              validInterval(GlitchStepMs), "Timing intervals must be 1..2147483647 ms.");
}
