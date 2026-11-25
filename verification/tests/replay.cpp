#include "ReplayWindow.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <set>

int main() {
  using namespace Wireless;
  Replay window;
  std::set<uint64_t> history;
  uint64_t top = 0;
  std::mt19937_64 random(0x50524f544fULL);
  auto check = [&](uint64_t sequence) {
    const bool expected = sequence && sequence <= MaxSequence &&
        (sequence > top || (top - sequence < ReplayWindow && !history.count(sequence)));
    assert(window.allows(sequence) == expected);
    if (!expected) return;
    window.accept(sequence);
    top = std::max(top, sequence);
    history.insert(sequence);
    assert(!window.allows(sequence));
  };
  for (unsigned session = 0; session < 32; ++session) {
    window.reset();
    history.clear();
    top = 0;
    check(0);
    check(MaxSequence + 1);
    check(1);
    for (unsigned i = 0; i < 1000; ++i) {
      switch (random() % 5) {
        case 0: check(top); break;
        case 1: check(top > 63 ? top - 63 : 0); break;
        case 2: check(top > 64 ? top - 64 : 0); break;
        case 3: check(top + 1 + random() % 128); break;
        default: check(top > 64 ? top - random() % 64 : top); break;
      }
    }
    check(MaxSequence);
    check(MaxSequence - 63);
    check(MaxSequence - 64);
    check(MaxSequence);
    check(UINT64_MAX);
  }
  std::cout << "PASS: production replay wrapper, history equivalence, reset, reordering, and sequence boundaries\n";
}
