#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <array>
#include <vector>
using byte = uint8_t;
using boolean = bool;
#define PROGMEM
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define FALLING 3
#define A0 14
#define A7 21
#define sq(x) ((x)*(x))
#define bitWrite(value, bit, set) ((value) = ((value) & ~(1u << (bit))) | ((set) << (bit)))
inline uint64_t fakeUs = 0;
inline int buttonInput = HIGH, visorInput = LOW, boopInput = 1023, micInput = 510;
inline unsigned adcReads = 0, pwmWrites = 0;
inline std::array<unsigned, 32> adcReadsByPin{}, digitalReadsByPin{};
inline std::array<int, 32> pwmValues{};
inline std::array<int, 32> pinModes{}, digitalValues{};
inline unsigned adcReadDurationUs = 0, spiByteDurationUs = 0;
inline bool syntheticMicrophone = false, recordMicrophoneTimes = false;
inline std::vector<uint64_t> microphoneTimes;
inline uint32_t micros() { return static_cast<uint32_t>(fakeUs); }
inline uint32_t millis() { return static_cast<uint32_t>(fakeUs / 1000); }
inline int analogRead(int pin) {
  ++adcReads; ++adcReadsByPin.at(pin);
  if (pin == A0 && recordMicrophoneTimes) microphoneTimes.push_back(fakeUs);
  int result = pin == A7 ? boopInput : micInput;
  if (pin == A0 && syntheticMicrophone)
    result = static_cast<int>(510 + 200 * std::sin(6.28318530718 * 1000 * fakeUs / 1000000.0));
  fakeUs += adcReadDurationUs;
  return result;
}
inline int digitalRead(int pin) { ++digitalReadsByPin.at(pin); return pin == 2 ? buttonInput : visorInput; }
inline void analogWrite(int pin, int value) { ++pwmWrites; pwmValues.at(pin) = value; }
inline void pinMode(int pin, int mode) { pinModes.at(pin) = mode; }
inline void digitalWrite(int pin, int value) {
  // Teensy 4.x writes the output latch only while configured as an output.
  if (pinModes.at(pin) == OUTPUT) digitalValues.at(pin) = value;
}
inline void analogReadResolution(int) {}
inline void noInterrupts() {}
inline void interrupts() {}
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
inline long map(long x, long a, long b, long c, long d) { return (x-a)*(d-c)/(b-a)+c; }
