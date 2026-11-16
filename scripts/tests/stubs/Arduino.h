#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <array>
#include <vector>
#include <string>
#include <cstdio>
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
inline int buttonInput = HIGH, visorInput = LOW, pilotInput = LOW, voltageInput = 640, boopInput = 1023, micInput = 510;
inline unsigned adcReads = 0, pwmWrites = 0;
inline std::array<unsigned, 32> adcReadsByPin{}, digitalReadsByPin{};
inline std::array<int, 32> pwmValues{};
inline std::array<int, 32> pinModes{}, digitalValues{};
inline unsigned adcReadDurationUs = 0, spiByteDurationUs = 0;
inline int syntheticAmplitude = 200;
inline bool syntheticMicrophone = false, recordMicrophoneTimes = false;
inline int (*microphoneSampleHook)() = nullptr;
inline std::vector<uint64_t> microphoneTimes;
inline bool interruptsEnabled = true;
inline void (*timeAdvanceHook)(uint64_t) = nullptr;
inline void advanceTime(uint64_t duration) {
  const uint64_t target = fakeUs + duration;
  if (timeAdvanceHook) timeAdvanceHook(target);
  fakeUs = target;
}
inline uint32_t micros() { return static_cast<uint32_t>(fakeUs); }
inline uint32_t millis() { return static_cast<uint32_t>(fakeUs / 1000); }
inline int readADCValue(int pin) {
  ++adcReads; ++adcReadsByPin.at(pin);
  if (pin == A0 && recordMicrophoneTimes) microphoneTimes.push_back(fakeUs);
  int result = pin == A7 ? boopInput : pin == 15 ? voltageInput : micInput;
  if (pin == A0 && microphoneSampleHook) return microphoneSampleHook();
  if (pin == A0 && syntheticMicrophone)
    result = static_cast<int>(510 + syntheticAmplitude * std::sin(6.28318530718 * 1000 * fakeUs / 1000000.0));
  return result;
}
inline int analogRead(int pin) { const int value = readADCValue(pin); advanceTime(adcReadDurationUs); return value; }
inline int digitalRead(int pin) { ++digitalReadsByPin.at(pin); return pin == 2 ? buttonInput : pin == 12 ? pilotInput : visorInput; }
inline void analogWrite(int pin, int value) { ++pwmWrites; pwmValues.at(pin) = value; }
inline void pinMode(int pin, int mode) { pinModes.at(pin) = mode; }
inline void digitalWrite(int pin, int value) {
  // Teensy 4.x writes the output latch only while configured as an output.
  if (pinModes.at(pin) == OUTPUT) digitalValues.at(pin) = value;
}
inline void analogReadResolution(int) {}
inline void noInterrupts() { interruptsEnabled = false; }
inline void interrupts() { interruptsEnabled = true; }
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
inline long map(long x, long a, long b, long c, long d) { return (x-a)*(d-c)/(b-a)+c; }

struct FakeSerial {
  bool connected = true;
  int capacity = 4096;
  std::string input, output;
  int (*spaceHook)() = nullptr;
  size_t (*writeHook)(const uint8_t *, size_t) = nullptr;
  void begin(unsigned) {}
  void addMemoryForRead(void*, size_t) {}
  explicit operator bool() const { return connected; }
  int available() const { return static_cast<int>(input.size()); }
  int availableForWrite() const { return spaceHook ? spaceHook() : capacity; }
  int read() { if (input.empty()) return -1; const unsigned char c = input[0]; input.erase(0,1); return c; }
  size_t write(const uint8_t *bytes, size_t count) {
    if (writeHook) return writeHook(bytes, count);
    output.append(reinterpret_cast<const char*>(bytes), count); return count;
  }
};
inline FakeSerial Serial;
inline FakeSerial Serial1;
