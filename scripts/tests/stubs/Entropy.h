#pragma once
#include <cstdint>
#include <cassert>
#include <deque>
#include <random>

// Only the host harness uses this deterministic source. Real firmware includes
// Teensy's bundled Entropy library and reads the chip's TRNG. The desktop preview
// replaces provider with std::random_device (the operating system's entropy).
struct FakeEntropy {
  bool ready = true, initialized = false;
  int budget = -1; // Optional finite availability for partial-packet tests.
  unsigned reads = 0;
  std::deque<uint32_t> words;
  std::mt19937 deterministic{0x63f285b1};
  uint32_t (*provider)() = nullptr;
  void Initialize() { initialized = true; }
  uint8_t available() const { return ready && budget != 0 ? 16 : 0; }
  uint32_t random() {
    assert(initialized && ready);
    if (budget > 0) --budget;
    ++reads;
    if (!words.empty()) { const uint32_t value = words.front(); words.pop_front(); return value; }
    return provider ? provider() : static_cast<uint32_t>(deterministic());
  }
};
inline FakeEntropy Entropy;
