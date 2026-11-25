#include "abi.h"
#include "TimeCore.h"
#include <assert.h>
extern uint32_t nondet_u32(void);
extern bool nondet_bool(void);
int main(void) {
  uint32_t now = nondet_u32(), at = nondet_u32(), interval = nondet_u32();
  bool pending = nondet_bool();
  uint32_t age = now - at;
  uint32_t remaining = protogenRemaining(now, at, interval);
  assert(remaining <= interval);
  assert((remaining == 0) == (age >= interval));
  if (age < interval) assert((uint64_t)remaining + age == interval);
  bool next = protogenRetryPending(pending, now, at, interval);
  assert(next == (pending && age < interval));
  assert(pending || !next);
}
