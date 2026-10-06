#pragma once
#include "Interfaces.hpp"
namespace ProtogenV4 {
constexpr size_t PairRecordBytes = 112;
enum class PairStatus {
  Unpaired,
  PairedV4,
  V3_REPAIR_REQUIRED,
  RecoveryRequired
};
PairStatus decodeRecord(Crypto &, uint8_t role, const uint8_t *, size_t,
                        uint8_t secret[32]);
bool encodeRecord(Crypto &, uint8_t role, const uint8_t secret[32],
                  const uint8_t transcript[32], uint8_t out[PairRecordBytes]);
} // namespace ProtogenV4
