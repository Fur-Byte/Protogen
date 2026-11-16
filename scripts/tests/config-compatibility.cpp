#include "ConfigStore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

using Slot = std::array<uint8_t, 64>;
static_assert(configSchemaVersion == 2 && ConfigStore::RecordVersion == 3);
static_assert(ConfigStore::SlotBytes == 64 && ConfigStore::RecordBytes == 53);
static_assert(configFieldCount == 17);

Slot readFixture(std::istream &input) {
  std::string hex;
  assert(input >> hex && hex.size() == 128);
  Slot slot{};
  for (size_t i = 0; i < slot.size(); ++i)
    slot[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
  return slot;
}

RuntimeConfig highRequest() {
  RuntimeConfig result;
  // Deliberately distinguish adjacent fields so swaps cannot hide behind defaults.
  const uint16_t values[] = {15, 4, 1, 740, 81, 310, 455, 690, 12200, 4200, 235, 40, 340, 0, 1, 1, 3400};
  for (size_t i = 0; i < configFieldCount; ++i)
    assert(setConfigField(result, configFields[i].name, values[i]));
  return result;
}

void putSlot(const Slot &slot, size_t index) {
  std::copy(slot.begin(), slot.end(), EEPROM.bytes.begin() + index * slot.size());
}

void checkSlot(const Slot &slot, size_t index) {
  assert(std::equal(slot.begin(), slot.end(), EEPROM.bytes.begin() + index * slot.size()));
}

void requireLoad(const RuntimeConfig &expected) {
  RuntimeConfig actual;
  assert(loadStoredConfig(actual));
  assert(sameConfig(actual, expected));
}

void interruptedWrites(const FakeEEPROM &before, const RuntimeConfig &oldConfig,
                       const RuntimeConfig &request, const RuntimeConfig &savedConfig,
                       const Slot &expectedSlot, size_t slotIndex) {
  EEPROM = before;
  assert(saveStoredConfig(request));
  checkSlot(expectedSlot, slotIndex);
  requireLoad(savedConfig);
  assert(std::equal(EEPROM.bytes.begin() + 128, EEPROM.bytes.end(), before.bytes.begin() + 128));
  const unsigned finishedWrites = EEPROM.writes - before.writes;
  assert(finishedWrites > 1);
  for (unsigned cut = 0; cut < finishedWrites; ++cut) {
    EEPROM = before;
    EEPROM.writeBudget = cut;
    bool interrupted = false;
    try {
      saveStoredConfig(request);
    } catch (const std::runtime_error &) {
      interrupted = true;
    }
    assert(interrupted);
    EEPROM.writeBudget = -1; // reboot, with persistent bytes intact
    requireLoad(oldConfig);
    assert(saveStoredConfig(request)); // recovery and retry produce identical bytes
    checkSlot(expectedSlot, slotIndex);
    requireLoad(savedConfig);
  }
}

int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream fixtures(argv[1]);
  const auto v1 = readFixture(fixtures), v2 = readFixture(fixtures), v3 = readFixture(fixtures);
  const auto v3Second = readFixture(fixtures), v3Third = readFixture(fixtures);
  std::string trailing;
  assert(!(fixtures >> trailing));

  const auto high = highRequest();
  assert(high.brightness == 15 && high.brightnessApproval == 1 && high.effectiveBrightness() == 1);
  auto unapproved = high;
  unapproved.brightnessApproval = 0;
  assert(unapproved.effectiveBrightness() == 0);
  assert(setConfigField(unapproved, "transition_ms", 321));
  assert(unapproved.brightnessApproval == 0);
  auto oversized = high;
  oversized.brightnessApproval = 15;
  assert(oversized.effectiveBrightness() == 1);

  auto invalid = high;
  assert(setConfigField(invalid, "boop_trigger", high.boopRelease) && !validConfig(invalid));
  invalid = high;
  assert(setConfigField(invalid, "blink_interval_ms", high.blinkJitter) && !validConfig(invalid));
  assert(!setConfigField(invalid, "brightness", 65536));

  auto first = high;
  first.boopMode = 0;
  first.boopDuration = 6000; // legacy v1 omitted both fields
  for (const auto *slot : {&v1, &v2, &v3}) {
    EEPROM = FakeEEPROM{};
    putSlot(*slot, 0);
    const auto &legacy = slot == &v1 ? first : high;
    requireLoad(legacy);
    assert(EEPROM.writes == 0);
    auto next = high;
    assert(setConfigField(next, "transition_ms", 321));
    assert(setConfigField(next, "brightness", 0));
    const auto initial = EEPROM;
    interruptedWrites(initial, legacy, next, next, v3Second, 1);
  }

  EEPROM = FakeEEPROM{};
  assert(saveStoredConfig(high));
  checkSlot(v3, 0);
  putSlot(v3Second, 1);
  RuntimeConfig previous = high;
  assert(setConfigField(previous, "transition_ms", 321));
  assert(setConfigField(previous, "brightness", 0));
  requireLoad(previous);
  const auto bothSlots = EEPROM;
  RuntimeConfig next = high;
  assert(setConfigField(next, "transition_ms", 640));
  assert(setConfigField(next, "brightness", 14));
  interruptedWrites(bothSlots, previous, next, next, v3Third, 0);

  for (size_t offset : {48, 52}) { // isolated CRC and marker corruption in newest slot
    EEPROM = bothSlots;
    EEPROM.bytes[64 + offset] ^= 1;
    requireLoad(high);
  }
  std::cout << "PASS: byte-exact EEPROM v1/v2/v3, approval, cross-field bounds, and all interrupted writes\n";
}
