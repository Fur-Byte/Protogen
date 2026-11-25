#pragma once
#include <stdint.h>

// The producer ISR owns write and the main loop owns read. Interrupts are
// disabled when both indices are reset. Count is modulo 2^16.
enum { ProtogenAudioRingSize = 512 };
static inline uint16_t protogenAudioCount(uint16_t write, uint16_t read) {
  return (uint16_t)(write - read);
}
static inline uint16_t protogenAudioIndex(uint16_t counter) {
  return counter & (ProtogenAudioRingSize - 1);
}
static inline uint16_t protogenAudioNext(uint16_t counter) {
  return (uint16_t)(counter + 1);
}
