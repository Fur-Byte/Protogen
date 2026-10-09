#pragma once
#include <stdint.h>

// User adjustments. Pin assignments, panel rotations, and artwork stay in the sketch.
// Optional hardware is still enabled by the three #define lines in the main .ino.
namespace Settings {
constexpr int Brightness = 1;              // MAX7219 intensity: 0 (dimmest) .. 15.
constexpr int StartupExpression = 0;       // 0 happy, 1 surprised, 2 angry, 3 vwv, 4 sleepy, 5 wink.
constexpr uint32_t ButtonDebounceMs = 25;
constexpr uint32_t ButtonHoldMs = 1200;    // Hold to sleep; tap or hold to wake.
constexpr uint32_t VisorDebounceMs = 150; // DRV5032FC sampling can take up to 75 ms.
constexpr uint32_t IndicatorMs = 1000;
constexpr int BoopThreshold = 500;         // Trigger at/below this 10-bit ADC value.
constexpr int BoopReleaseThreshold = 550; // Must rise to this value to release/rearm.
constexpr uint32_t BoopPollMs = 5;
constexpr uint32_t BoopDurationMs = 6000;
constexpr int BoopMode = 0; // 0 original held/proximity; 1 timed prototype reaction.
constexpr int MicrophoneSensitivity = 65;
constexpr uint32_t SpeakingHoldMs = 100;
constexpr uint32_t BlinkIntervalMs = 4000; // Center of ordinary inter-blink pauses.
constexpr uint32_t GlitchIntervalMs = 7000;
constexpr uint32_t GlitchStepMs = 100;
constexpr uint32_t TransitionMs = 75;
constexpr uint32_t BlinkJitterMs = 2000;
constexpr uint32_t AudioAttackMs = 20;
constexpr uint32_t AudioReleaseMs = 120;
constexpr int NoiseMargin = 12;
constexpr int NoiseFloor = 0; // Updated by quiet-room calibration; explicitly save to persist.
constexpr bool NaturalBlinks = true;
constexpr bool IdleMotion = false;
constexpr int MouthMode = 0; // 0 spectrum (original style), 1 amplitude.
// Conservative default for the 14-module chain; faster clocks need helmet validation.
constexpr uint32_t SpiFrequencyHz = 4000000UL;

static_assert(Brightness >= 0 && Brightness <= 15, "Brightness must be 0..15.");
static_assert(StartupExpression >= 0 && StartupExpression < 6,
              "StartupExpression must be 0..5.");
static_assert(BoopThreshold >= 0 && BoopReleaseThreshold <= 1023 &&
              BoopThreshold < BoopReleaseThreshold, "Invalid boop hysteresis thresholds.");
static_assert(MicrophoneSensitivity >= 0 && MicrophoneSensitivity <= 1023,
              "MicrophoneSensitivity must be 0..1023.");
static_assert(NoiseMargin >= 1 && NoiseMargin <= 1023, "NoiseMargin must be 1..1023.");
static_assert(NoiseFloor >= 0 && NoiseFloor <= 1023, "NoiseFloor must be 0..1023.");
static_assert(MouthMode >= 0 && MouthMode <= 1, "MouthMode must be 0 or 1.");
static_assert(TransitionMs <= 1000 && BlinkIntervalMs >= 1000 && BlinkIntervalMs <= 60000 &&
              BlinkJitterMs < BlinkIntervalMs && BlinkJitterMs <= 5000 &&
              AudioAttackMs <= 2000 && AudioReleaseMs <= 2000,
              "Invalid animation/audio configuration defaults.");
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
              validInterval(GlitchIntervalMs) &&
              validInterval(GlitchStepMs) && (TransitionMs == 0 || validInterval(TransitionMs)) &&
              (BlinkJitterMs == 0 || validInterval(BlinkJitterMs)) && validInterval(AudioAttackMs) &&
              validInterval(AudioReleaseMs), "Timing intervals must be 1..2147483647 ms.");
}
