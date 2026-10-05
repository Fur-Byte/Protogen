#include "sketch.h"

int main() {
  setup(); loop();
  assert(!sleeping && !SPI.shutdown && state == Settings::StartupExpression);
  auto normal = SPI.display;
  for (int i = 0; i < 100; ++i) { advanceTime(250); loop(); }
#ifdef Microphone
  assert(adcReadsByPin[A0] > 0);
#else
  assert(adcReadsByPin[A0] == 0);
#endif
#ifdef BoopSensor
  assert(adcReadsByPin[A7] > 0);
  boopInput = Settings::BoopThreshold;
  for (int i = 0; i < 40; ++i) { advanceTime(1000); loop(); }
  assert(boopActive);
  boopInput = Settings::BoopReleaseThreshold;
  for (int i = 0; i < 40; ++i) { advanceTime(1000); loop(); }
  assert(!boopActive && SPI.display == normal);
#else
  assert(adcReadsByPin[A7] == 0 && !boopActive);
#endif
#ifdef VisorSensor
  assert(digitalReadsByPin[VisorPin] > 0);
  visorInput = HIGH;
  for (unsigned i = 0; i < Settings::VisorDebounceMs+2; ++i) { advanceTime(1000); loop(); }
  assert(SPI.shutdown && displayBlanked);
  const auto reads = adcReads;
  for (int i = 0; i < 40; ++i) { advanceTime(1000); loop(); }
  assert(adcReads == reads);
  visorInput = LOW;
  for (unsigned i = 0; i < Settings::VisorDebounceMs+2; ++i) { advanceTime(1000); loop(); }
  assert(!SPI.shutdown && SPI.display == normal);
#else
  assert(digitalReadsByPin[VisorPin] == 0);
#endif
  std::cout << "PASS: enabled sensors respond; disabled sensors have zero reads\n";
}
