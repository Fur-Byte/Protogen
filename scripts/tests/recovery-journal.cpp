#include <stdint.h>
uint32_t micros() {
  return 0;
}
#include "../../firmware/PROT_OS7_MAX7219/RecoveryJournal.h"
#include "../../firmware/PROT_OS7_MAX7219/ControllerIdle.h"
#include <cassert>
#include <cstring>

int main() {
  using namespace Recovery;
  constexpr uint64_t build = 0x123456789abcdef0ULL;
  const auto a = make(build, UINT32_MAX, 1, Running, 123, 456, 78, 9, Watchdog);
  const auto b = make(build, 0, 2, Saving, 124, 457, 79, 10, Watchdog);
  assert(valid(a, build) && valid(b, build));
  assert(newest(a, b, build) == 1);
  assert(newest(a, b, build + 1) == -1);
  assert(newest(a, a, build) == -1);
  Slot torn{};
  // Every partial prefix of a replacement with the commit word invalid.
  for (unsigned n = 0; n < sizeof(Slot); ++n) {
    torn = {};
    memcpy(&torn, &b, n);
    torn.marker = 0;
    assert(newest(a, torn, build) == 0);
  }
  for (unsigned bit = 0; bit < sizeof(Slot) * 8; ++bit) {
    auto damaged = b;
    reinterpret_cast<unsigned char *>(&damaged)[bit / 8] ^= 1u << (bit % 8);
    assert(!valid(damaged, build));
  }
  for (unsigned attempts = 0; attempts <= RetryLimit; ++attempts) {
    assert(bootAttempts(PowerOn, false, attempts) == 0);
    assert(bootAttempts(Watchdog, false, attempts) == RetryLimit);
    assert(bootAttempts(Watchdog, true, attempts) == (attempts < RetryLimit ? attempts + 1 : RetryLimit));
    assert(bootAttempts(Software, true, attempts) == attempts);
    assert(bootAttempts(Watchdog | PowerOn, true, attempts) == RetryLimit);
    assert(bootAttempts(0, true, attempts) == RetryLimit);
  }
  // Explicitly document the current watchdog-only retry scope: a trusted
  // software reset does not consume a retry; an ambiguous reset locks out.
  uint32_t softwareAttempts = 0;
  for (unsigned i = 0; i < RetryLimit; ++i)
    softwareAttempts = bootAttempts(Software, true, softwareAttempts);
  assert(softwareAttempts == 0);
  assert(bootAttempts(Software | PowerOn, true, softwareAttempts) == RetryLimit);
  HealthyWindow window;
  const uint32_t start = UINT32_MAX - 10000;
  for (uint32_t elapsed = 0; elapsed < HealthyMs; elapsed += 100)
    assert(!window.observe(start + elapsed, true));
  assert(window.observe(start + HealthyMs, true));
  assert(!window.observe(start + HealthyMs + HealthyGapMs + 1, true));
  assert(!window.observe(start + HealthyMs + HealthyGapMs + 2, false));
  assert(!window.observe(start + 2 * HealthyMs, true));
  assert(ControllerIdle::canSelectRun(0, 0x79));
  assert(!ControllerIdle::canSelectRun(4, 0x79));
  assert(!ControllerIdle::canSelectRun(0, 0x7a));
  assert(!ControllerIdle::canSelectRun(0, 0x179));
}
