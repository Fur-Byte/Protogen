#pragma once
#include "Arduino.h"
#include <array>
#include <vector>
#include <cassert>
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings {
  unsigned clock;
  SPISettings(unsigned speed, int order, int mode) : clock(speed) {
    assert(speed > 0 && speed <= 10000000 && order == MSBFIRST && mode == SPI_MODE0);
  }
};
struct FakeSPI {
  using Frame = std::vector<std::array<byte, 8>>;
  unsigned bytes = 0, transactions = 0;
  unsigned shutdownCommands = 0, wakeCommands = 0, hiddenDigitWrites = 0;
  bool begun = false, active = false;
  unsigned clock = 0;
  bool shutdown = true;
  std::vector<byte> packet;
  Frame display;
  std::array<byte, 16> registers{};
  std::vector<Frame> wakeFrames;
  explicit FakeSPI(unsigned tileCount = 14): display(tileCount) {}
  void begin() { assert(pinModes[8] == OUTPUT && digitalValues[8] == HIGH); begun = true; }
  void beginTransaction(SPISettings settings) { assert(begun && !active); clock = settings.clock; active = true; packet.clear(); }
  void transfer(byte value) {
    assert(active && digitalValues[8] == LOW);
    ++bytes; packet.push_back(value); advanceTime(spiByteDurationUs ? spiByteDurationUs : (8000000UL + clock - 1) / clock);
  }
  void endTransaction() {
    assert(active && digitalValues[8] == HIGH && packet.size() == display.size() * 2);
    const byte opcode = packet[0];
    for (unsigned i = 0; i < display.size(); ++i) {
      byte op = packet[2*i];
      assert(op == opcode);
      if (op >= 1 && op <= 8) display[display.size()-1-i][op-1] = packet[2*i+1];
      else { assert(packet[2*i+1] == packet[1]); registers.at(op) = packet[1]; }
    }
    if (opcode >= 1 && opcode <= 8 && shutdown) ++hiddenDigitWrites;
    if (opcode == 12) {
      assert(packet[1] <= 1);
      shutdown = packet[1] == 0;
      if (shutdown) ++shutdownCommands;
      else { ++wakeCommands; wakeFrames.push_back(display); }
    }
    active = false; ++transactions;
  }
};
inline FakeSPI SPI;
