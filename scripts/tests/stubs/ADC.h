#pragma once
#include "Arduino.h"
#include <cassert>
enum class ADC_CONVERSION_SPEED { MED_SPEED };
enum class ADC_SAMPLING_SPEED { MED_SPEED };
class ADC_Module {
public:
  int pin = A0, captured = 510;
  uint32_t frequency = 0;
  uint64_t next = 0;
  bool enabled = false;
  bool stuck = false, rejectStart = false, validPin = true;
  bool timerStalled = false;
  unsigned starts = 0, stops = 0, resets = 0;
  void (*callback)() = nullptr;
  void setResolution(int value) { assert(value == 10); }
  void setAveraging(int value) { assert(value == 1); }
  void setConversionSpeed(ADC_CONVERSION_SPEED) {}
  void setSamplingSpeed(ADC_SAMPLING_SPEED) {}
  bool startSingleRead(int value) {
    ++starts; pin = value;
    if (rejectStart) return false;
    captured = readADCValue(pin); return true;
  }
  bool checkPin(int value) { return validPin && (value == A0 || value == A7 || value == 15); }
  void stopContinuous() { ++stops; }
  void resetError() { ++resets; }
  bool isComplete() { return !stuck; }
  void enableInterrupts(void (*fn)(), unsigned) { callback = fn; enabled = true; callback(); }
  void disableInterrupts() { enabled = false; }
  void startTimer(uint32_t rate) { frequency = rate; next = fakeUs + 1000000 / rate; }
  void stopTimer() { frequency = 0; }
  uint32_t getTimerFrequency() const { return frequency; }
  int readSingle() { return frequency ? readADCValue(pin) : captured; }
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
      if (module.enabled && interruptsEnabled && !module.timerStalled) module.callback();
    }
  }
public:
  ADC_Module *adc0 = &modules[0], *adc1 = &modules[1];
  ADC() { instance = this; timeAdvanceHook = advance; }
};
