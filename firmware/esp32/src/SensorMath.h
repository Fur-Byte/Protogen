#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "SensorMathCore.h"

namespace SensorMath {
inline uint8_t shtCrc(const uint8_t *bytes, size_t count) {
  uint8_t crc = 0xff;
  for (size_t i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = static_cast<uint8_t>((crc << 1) ^ ((crc & 0x80) ? 0x31 : 0));
    }
  }
  return crc;
}

inline bool sht40(const uint8_t bytes[6], float &temperature, float &humidity) {
  if (shtCrc(bytes, 2) != bytes[2] || shtCrc(bytes + 3, 2) != bytes[5]) return false;

  const uint16_t rawTemperature = (uint16_t(bytes[0]) << 8) | bytes[1];
  temperature = -45.0f + 175.0f * rawTemperature / 65535.0f;
  const uint16_t rawHumidity = (uint16_t(bytes[3]) << 8) | bytes[4];
  humidity = -6.0f + 125.0f * rawHumidity / 65535.0f;
  if (humidity < 0) humidity = 0;
  if (humidity > 100) humidity = 100;
  return isfinite(temperature) && temperature >= -10 && temperature <= 70;
}

constexpr double CurrentLsb = 5.0 / 524288.0;
constexpr double ShuntOhms = 0.015; // Adafruit INA228 #5832's fitted 15 mOhm shunt.
constexpr uint16_t ShuntCalibration = 1875;
constexpr double EnergyJoulesLsb = 16.0 * 3.2 * CurrentLsb;

inline uint64_t bigEndian(const uint8_t *bytes, size_t count) {
  return protogenBigEndian(bytes, count);
}

inline int32_t signedCurrent(const uint8_t bytes[3]) {
  return protogenSignedCurrent(bytes);
}

inline bool ntc(int millivolts, float &temperature) {
  // 15k to 3.3 V; 10k B3950 NTC to ground; calibrated millivolts.
  if (millivolts < 100 || millivolts > 2400) return false;
  const double resistance = 15000.0 * millivolts / (3300.0 - millivolts);
  temperature = static_cast<float>(
      1.0 / (1.0 / 298.15 + log(resistance / 10000.0) / 3950.0) - 273.15);
  return isfinite(temperature) && temperature >= 0 && temperature <= 65;
}

inline int percent(double used, uint32_t usable) {
  if (!usable || !isfinite(used) || used < 0) return -1;
  double value = 100.0 * (1.0 - used / usable);
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  return static_cast<int>(value + 0.5);
}
} // namespace SensorMath
