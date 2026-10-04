#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "Settings.h"

enum Expression : uint8_t {
  ExpressionHappy, ExpressionSurprised, ExpressionAngry, ExpressionVwv,
  ExpressionSleepy, ExpressionWink, ExpressionCount
};

// The same field table drives USB configuration, validation, and storage.
// Values are wide until validation, so 256 cannot silently become brightness 0.
struct RuntimeConfig {
  uint16_t brightness = Settings::Brightness;
  uint16_t startupExpression = Settings::StartupExpression;
  uint16_t mouthMode = Settings::MouthMode;
  uint16_t sensitivity = Settings::MicrophoneSensitivity;
  uint16_t noiseMargin = Settings::NoiseMargin;
  uint16_t noiseFloor = Settings::NoiseFloor;
  uint16_t boopThreshold = Settings::BoopThreshold;
  uint16_t boopRelease = Settings::BoopReleaseThreshold;
  uint16_t blinkInterval = Settings::BlinkIntervalMs;
  uint16_t blinkJitter = Settings::BlinkJitterMs;
  uint16_t transitionMs = Settings::TransitionMs;
  uint16_t attackMs = Settings::AudioAttackMs;
  uint16_t releaseMs = Settings::AudioReleaseMs;
  uint16_t naturalBlinks = Settings::NaturalBlinks;
  uint16_t idleMotion = Settings::IdleMotion;
};

struct ConfigField {
  const char *name;
  uint16_t RuntimeConfig::*member;
  uint16_t minimum, maximum;
};
constexpr ConfigField configFields[] = {
  {"brightness", &RuntimeConfig::brightness, 0, 15},
  {"startup_expression", &RuntimeConfig::startupExpression, 0, ExpressionCount - 1},
  {"mouth_mode", &RuntimeConfig::mouthMode, 0, 1},
  {"sensitivity", &RuntimeConfig::sensitivity, 0, 1023},
  {"noise_margin", &RuntimeConfig::noiseMargin, 1, 1023},
  {"noise_floor", &RuntimeConfig::noiseFloor, 0, 1023},
  {"boop_trigger", &RuntimeConfig::boopThreshold, 0, 1022},
  {"boop_release", &RuntimeConfig::boopRelease, 1, 1023},
  {"blink_interval_ms", &RuntimeConfig::blinkInterval, 1000, 60000},
  {"blink_jitter_ms", &RuntimeConfig::blinkJitter, 0, 5000},
  {"transition_ms", &RuntimeConfig::transitionMs, 0, 1000},
  {"attack_ms", &RuntimeConfig::attackMs, 1, 2000},
  {"release_ms", &RuntimeConfig::releaseMs, 1, 2000},
  {"natural_blinks", &RuntimeConfig::naturalBlinks, 0, 1},
  {"idle_motion", &RuntimeConfig::idleMotion, 0, 1}
};
constexpr size_t configFieldCount = sizeof(configFields) / sizeof(configFields[0]);
constexpr uint16_t configSchemaVersion = 1;

inline bool validConfig(const RuntimeConfig &value) {
  for (const auto &field : configFields) {
    const uint16_t number = value.*(field.member);
    if (number < field.minimum || number > field.maximum) return false;
  }
  return value.boopThreshold < value.boopRelease && value.blinkJitter < value.blinkInterval;
}

inline bool setConfigField(RuntimeConfig &value, const char *key, unsigned long number) {
  for (const auto &field : configFields) {
    if (strcmp(key, field.name) == 0) {
      if (number < field.minimum || number > field.maximum) return false;
      value.*(field.member) = static_cast<uint16_t>(number);
      return true;
    }
  }
  return false;
}

inline bool sameConfig(const RuntimeConfig &left, const RuntimeConfig &right) {
  for (const auto &field : configFields)
    if (left.*(field.member) != right.*(field.member)) return false;
  return true;
}
