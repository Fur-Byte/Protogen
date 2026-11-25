#pragma once
#include "Configuration.h"
#include "ConfigStorageCore.h"
#include <EEPROM.h>

// Two alternating records with an explicit byte layout (no struct padding).
// Write the commit marker last, so an interrupted save retains the previous slot.
namespace ConfigStore {
inline uint32_t writeAttempts = 0, verifiedWrites = 0, writeFailures = 0;
constexpr int SlotBytes = 64, HeaderBytes = 12;
// Storage format is versioned independently of public USB config schema 2.
// Formats 1/2 had no approval: their high requests never authorize more than 1.
constexpr uint16_t RecordVersion = 3, LegacyBrightnessApproval = 1;
constexpr int ConfigPayloadBytes = configFieldCount * 2;
constexpr int ApprovalOffset = HeaderBytes + ConfigPayloadBytes;
constexpr int PayloadBytes = ConfigPayloadBytes + 2;
constexpr int CrcOffset = HeaderBytes + PayloadBytes, MarkerOffset = CrcOffset + 4;
constexpr int RecordBytes = MarkerOffset + 1;
static_assert(RecordBytes <= SlotBytes, "Configuration record exceeds storage slot.");
static_assert(RecordVersion == 3 && HeaderBytes == 12 && ConfigPayloadBytes == 34 &&
              PayloadBytes == 36 && RecordBytes == 53, "Storage core layout mismatch");
struct Record { uint8_t bytes[RecordBytes] = {}; RuntimeConfig config; uint32_t sequence = 0; bool valid = false; };
inline uint32_t readNumber(const uint8_t *data, int count) {
  uint32_t value = 0;
  for (int i = 0; i < count; ++i) value |= static_cast<uint32_t>(data[i]) << (i * 8);
  return value;
}
inline void writeNumber(uint8_t *data, uint32_t value, int count) {
  for (int i = 0; i < count; ++i) data[i] = static_cast<uint8_t>(value >> (i * 8));
}
inline uint32_t crc(const uint8_t *data, int count) {
  uint32_t value = 0xffffffffUL;
  for (int i = 0; i < count; ++i) {
    value ^= data[i];
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320UL : 0);
  }
  return value ^ 0xffffffffUL;
}
inline Record readSlot(int slot) {
  Record record;
  if (slot < 0 || slot > 1 || EEPROM.length() < SlotBytes * 2) return record;
  for (int i = 0; i < RecordBytes; ++i) record.bytes[i] = EEPROM.read(slot * SlotBytes + i);
  const auto *data = record.bytes;
  const uint32_t format = readNumber(data + 4, 2), payload = readNumber(data + 6, 2);
  const int crcAt = protogenConfigCrcOffset(format, payload);
  if (memcmp(data, "P8CF", 4) || crcAt < 0) return record;
  const bool legacy1 = format == 1, current = format == RecordVersion;
  const int markerAt = crcAt + 4;
  if (data[markerAt] != 0xa5 || readNumber(data + crcAt, 4) != crc(data, crcAt)) return record;
  // The appended boop fields default to Feronium's held reaction when loading v1.
  for (size_t i = 0; i < (legacy1 ? 15 : configFieldCount); ++i)
    record.config.*(configFields[i].member) = static_cast<uint16_t>(readNumber(data + HeaderBytes + i * 2, 2));
  record.config.brightnessApproval = current
      ? static_cast<uint16_t>(readNumber(data + ApprovalOffset, 2)) : LegacyBrightnessApproval;
  if (current && record.config.brightnessApproval > record.config.brightness) return record;
  record.sequence = readNumber(data + 8, 4);
  record.valid = validConfig(record.config);
  return record;
}
inline int newest(const Record &a, const Record &b) {
  return protogenNewestConfig(a.valid, a.sequence, b.valid, b.sequence);
}
}
inline bool loadStoredConfig(RuntimeConfig &value) {
  const auto a = ConfigStore::readSlot(0), b = ConfigStore::readSlot(1);
  const int active = ConfigStore::newest(a, b);
  if (active < 0) return false;
  value = active == 0 ? a.config : b.config;
  return true;
}
inline bool saveStoredConfig(const RuntimeConfig &value) {
  using namespace ConfigStore;
  if (!validConfig(value) || EEPROM.length() < SlotBytes * 2) return false;
  RuntimeConfig approved = value;
  approved.brightnessApproval = value.effectiveBrightness();
  const auto a = readSlot(0), b = readSlot(1);
  const int active = newest(a, b);
  const auto &previous = active == 0 ? a : b;
  if (active >= 0 && sameConfig(approved, previous.config) &&
      readNumber(previous.bytes + 4, 2) == RecordVersion) return true; // No flash wear for identical saves.
  Record record;
  memcpy(record.bytes, "P8CF", 4);
  writeNumber(record.bytes + 4, RecordVersion, 2);
  writeNumber(record.bytes + 6, PayloadBytes, 2);
  const uint32_t sequence = active >= 0 ? previous.sequence + 1 : 1;
  writeNumber(record.bytes + 8, sequence, 4);
  for (size_t i = 0; i < configFieldCount; ++i)
    writeNumber(record.bytes + HeaderBytes + i * 2, approved.*(configFields[i].member), 2);
  writeNumber(record.bytes + ApprovalOffset, approved.brightnessApproval, 2);
  writeNumber(record.bytes + CrcOffset, crc(record.bytes, CrcOffset), 4);
  const int slot = active == 0 ? 1 : 0, base = slot * SlotBytes;
  if (writeAttempts != UINT32_MAX) ++writeAttempts;
  EEPROM.update(base + MarkerOffset, 0);
  for (int i = 0; i < MarkerOffset; ++i) EEPROM.update(base + i, record.bytes[i]);
  EEPROM.update(base + MarkerOffset, 0xa5);
  const auto result = readSlot(slot);
  const bool okay = result.valid && result.sequence == sequence && sameConfig(result.config, approved);
  uint32_t &count = okay ? verifiedWrites : writeFailures;
  if (count != UINT32_MAX) ++count;
  return okay;
}
