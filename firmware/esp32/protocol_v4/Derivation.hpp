#pragma once
#include "Interfaces.hpp"
namespace ProtogenV4 {
constexpr size_t TranscriptCapacity = 4096;
struct Exchange {
  Mode mode = Mode::Session;
  uint64_t wire = 0;
  Identity paw{}, helmet{};
  uint8_t objects[ObjectCount][PublicKeyBytes]{};
  uint8_t complete = 0;
};
// Caller-owned bounded workspace avoids large ordinary-task stack frames.
struct DerivationWorkspace {
  uint8_t transcript[TranscriptCapacity]{},
      combiner[TranscriptCapacity + 128]{};
};
size_t canonical(Crypto &, const Exchange &, uint8_t *, size_t);
bool hybridRoot(Crypto &, const uint8_t[32], const uint8_t[32], const uint8_t *,
                size_t, uint8_t[32], DerivationWorkspace &);
bool deriveMaterial(Crypto &, const uint8_t[32], const uint8_t[32], Material &);
bool nonceCommit(Crypto &, Mode, uint64_t, uint8_t, const Identity &,
                 const uint8_t[129], const uint8_t[16], uint8_t[32]);
void objectHeader(Frame &, Mode, uint64_t, const Identity &, Object);
} // namespace ProtogenV4
