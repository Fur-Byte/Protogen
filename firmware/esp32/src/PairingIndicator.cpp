#include "PairingIndicator.h"
namespace Wireless {
uint32_t Indicator::cycleMs(uint32_t code) {
  uint32_t total = IndicatorMarkerMs + IndicatorMarkerGapMs;
  uint32_t divisor = 100000;
  for (unsigned i = 0; i < 6; ++i) {
    unsigned digit = (code / divisor) % 10;
    if (!digit) digit = 10;
    total += digit * IndicatorPulsePeriodMs + IndicatorDigitGapMs;
    divisor /= 10;
  }
  return total;
}
bool Indicator::complete(uint32_t now, uint32_t code) const {
  return showing && lastCode == code && now - began >= cycleMs(code);
}
bool Indicator::level(uint32_t now, bool active, bool hasCode, uint32_t code) {
  if (!active) {
    showing = false;
    return false;
  }
  if (!hasCode) {
    showing = false;
    return (now / 250) % 2 == 0;
  }
  if (!showing || code != lastCode) {
    began = now;
    lastCode = code;
    showing = true;
  }
  unsigned digits[6];
  uint32_t divisor = 100000;
  const uint32_t total = cycleMs(code);
  for (unsigned i = 0; i < 6; ++i) {
    digits[i] = (code / divisor) % 10;
    if (!digits[i])
      digits[i] = 10;
    divisor /= 10;
  }
  uint32_t t = (now - began) % total;
  if (t < IndicatorMarkerMs + IndicatorMarkerGapMs)
    return t < IndicatorMarkerMs;
  t -= IndicatorMarkerMs + IndicatorMarkerGapMs;
  for (unsigned digit : digits) {
    const uint32_t span = digit * IndicatorPulsePeriodMs;
    if (t < span)
      return t % IndicatorPulsePeriodMs < IndicatorPulseMs;
    t -= span;
    if (t < IndicatorDigitGapMs)
      return false;
    t -= IndicatorDigitGapMs;
  }
  return false;
}
} // namespace Wireless
