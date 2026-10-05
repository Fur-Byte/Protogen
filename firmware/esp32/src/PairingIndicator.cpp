#include "PairingIndicator.h"
namespace Wireless {
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
  uint32_t divisor = 100000, total = IndicatorMarkerMs + IndicatorMarkerGapMs;
  for (unsigned i = 0; i < 6; ++i) {
    digits[i] = (code / divisor) % 10;
    if (!digits[i])
      digits[i] = 10;
    total += digits[i] * IndicatorPulsePeriodMs + IndicatorDigitGapMs;
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
