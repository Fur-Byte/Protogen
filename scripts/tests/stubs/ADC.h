#pragma once
#include "Arduino.h"
#include <cassert>
enum class ADC_CONVERSION_SPEED { MED_SPEED };
enum class ADC_SAMPLING_SPEED { MED_SPEED };
class ADC_Module {
public:
  int pin = A0;
  uint32_t frequency = 0;
  uint64_t next = 0;
  bool enabled = false;
  bool stuck = false;
  void (*callback)() = nullptr;
  void setResolution(int value) { assert(value == 10); }
  void setAveraging(int value) { assert(value == 1); }
  void setConversionSpeed(ADC_CONVERSION_SPEED) {}
  void setSamplingSpeed(ADC_SAMPLING_SPEED) {}
  bool startSingleRead(int value) { pin = value; return true; }
  bool isComplete() { return !stuck; }
  void enableInterrupts(void (*fn)(), unsigned) { callback = fn; enabled = true; callback(); }
  void disableInterrupts() { enabled = false; }
  void startTimer(uint32_t rate) { frequency = rate; next = fakeUs + 1000000 / rate; }
  void stopTimer() { frequency = 0; }
  uint32_t getTimerFrequency() const { return frequency; }
  int readSingle() { return readADCValue(pin); }
  int analogRead(int value) { assert(value == A7); return ::analogRead(value); }
};
class ADC {
  ADC_Module modules[2];
  static inline ADC *instance = nullptr;
  static void advance(uint64_t target) {
    auto &module = instance->modules[0];
    if (!module.frequency) return;
    const uint64_t period = 1000000 / module.frequency;
    if (module.next < fakeUs) module.next = fakeUs + period;
    while (module.frequency && module.next <= target) {
      fakeUs = module.next; module.next += period;
      if (module.enabled && interruptsEnabled) module.callback();
    }
  }
public:
  ADC_Module *adc0 = &modules[0], *adc1 = &modules[1];
  ADC() { instance = this; timeAdvanceHook = advance; }
};
