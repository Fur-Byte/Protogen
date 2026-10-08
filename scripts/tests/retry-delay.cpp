#include "RetryDelay.h"
#include <cassert>
#include <iostream>

template <uint32_t DelayMs> static void exercise() {
  for (uint32_t start : {0u, 0x80000010u, 0xfffffff0u}) {
    HelmetLink::RetryDelay<DelayMs> retry;
    assert(retry.ready(start)); // A never-scheduled retry is ready at any uptime.
    retry.start(start);
    assert(!retry.ready(start));
    assert(!retry.ready(start + DelayMs - 1));
    assert(retry.ready(start + DelayMs));
    assert(retry.ready(start + DelayMs + 0x80000010u));
    assert(retry.ready(start - 1)); // Expiry remains disarmed across clock wrap.
    retry.start(start + DelayMs);
    assert(!retry.ready(start + 2 * DelayMs - 1));
    assert(retry.ready(start + 2 * DelayMs));
  }
  HelmetLink::RetryDelay<DelayMs> retry;
  retry.start(100);
  retry.start(100 + DelayMs - 1);
  assert(!retry.ready(100 + DelayMs)); // Another fault restarts the cooldown.
  assert(retry.ready(100 + 2 * DelayMs - 1));
}

int main() {
  exercise<500>();
  exercise<5000>();
  std::cout << "PASS: visor/I2C retry delays, initial readiness, exact cooldown, half-range uptime, clock wrap and fault rescheduling\n";
}
