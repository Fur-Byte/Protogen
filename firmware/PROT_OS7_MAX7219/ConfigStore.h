#pragma once
#include "Configuration.h"
#include <EEPROM.h>

// Two alternating records with an explicit byte layout (no struct padding).
// Write the commit marker last, so an interrupted save retains the previous slot.
namespace ConfigStore {
constexpr int SlotBytes = 64, HeaderBytes = 12;
constexpr int PayloadBytes = configFieldCount * 2;
constexpr int CrcOffset = HeaderBytes + PayloadBytes, MarkerOffset = CrcOffset + 4;
constexpr int RecordBytes = MarkerOffset + 1;
static_assert(RecordBytes <= SlotBytes, "Configuration record exceeds storage slot.");
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
  if (EEPROM.length() < SlotBytes * 2) return record;
  for (int i = 0; i < RecordBytes; ++i) record.bytes[i] = EEPROM.read(slot * SlotBytes + i);
  const auto *data = record.bytes;
  const uint32_t schema = readNumber(data + 4, 2), payload = readNumber(data + 6, 2);
  const bool legacy = schema == 1 && payload == 30;
  if (memcmp(data, "P8CF", 4) || (!legacy && (schema != configSchemaVersion || payload != PayloadBytes))) return record;
  const int crcAt = HeaderBytes + static_cast<int>(payload), markerAt = crcAt + 4;
  if (data[markerAt] != 0xa5 || readNumber(data + crcAt, 4) != crc(data, crcAt)) return record;
  // The appended boop fields default to Feronium's held reaction when loading v1.
  for (size_t i = 0; i < payload / 2; ++i)
    record.config.*(configFields[i].member) = static_cast<uint16_t>(readNumber(data + HeaderBytes + i * 2, 2));
  record.sequence = readNumber(data + 8, 4);
  record.valid = validConfig(record.config);
  return record;
}
inline int newest(const Record &a, const Record &b) {
  if (!a.valid) return b.valid ? 1 : -1;
  if (!b.valid) return 0;
  return static_cast<int32_t>(b.sequence - a.sequence) > 0 ? 1 : 0;
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
  const auto a = readSlot(0), b = readSlot(1);
  const int active = newest(a, b);
  const auto &previous = active == 0 ? a : b;
  if (active >= 0 && sameConfig(value, previous.config)) return true; // No flash wear for identical saves.
  Record record;
  memcpy(record.bytes, "P8CF", 4);
  writeNumber(record.bytes + 4, configSchemaVersion, 2);
  writeNumber(record.bytes + 6, PayloadBytes, 2);
  const uint32_t sequence = active >= 0 ? previous.sequence + 1 : 1;
  writeNumber(record.bytes + 8, sequence, 4);
  for (size_t i = 0; i < configFieldCount; ++i)
    writeNumber(record.bytes + HeaderBytes + i * 2, value.*(configFields[i].member), 2);
  writeNumber(record.bytes + CrcOffset, crc(record.bytes, CrcOffset), 4);
  const int slot = active == 0 ? 1 : 0, base = slot * SlotBytes;
  EEPROM.update(base + MarkerOffset, 0);
  for (int i = 0; i < MarkerOffset; ++i) EEPROM.update(base + i, record.bytes[i]);
  EEPROM.update(base + MarkerOffset, 0xa5);
  const auto result = readSlot(slot);
  return result.valid && result.sequence == sequence && sameConfig(result.config, value);
}
