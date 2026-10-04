#pragma once
#include "Arduino.h"
#include <stdexcept>
struct FakeEEPROM {
  std::array<uint8_t, 1080> bytes;
  unsigned writes = 0;
  int writeBudget = -1;
  FakeEEPROM() { bytes.fill(0xff); }
  uint16_t length() const { return bytes.size(); }
  uint8_t read(int index) const { return bytes.at(index); }
  void update(int index, uint8_t value) {
    if (bytes.at(index) == value) return;
    if (writeBudget == 0) throw std::runtime_error("simulated power loss");
    if (writeBudget > 0) --writeBudget;
    bytes.at(index) = value; ++writes;
  }
};
inline FakeEEPROM EEPROM;
