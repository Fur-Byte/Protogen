#include "abi.h"
#include "IngressCore.h"
#include <assert.h>
extern uint32_t nondet_u32(void);
extern bool nondet_bool(void);
int main(void) {
  uint32_t captured = nondet_u32(), current = nondet_u32();
  bool enabled = nondet_bool();
  assert(protogenCurrentIngress(enabled,captured,current) == (enabled && captured == current));
  // Callback snapshot taken before reconfiguration; the consumer may run
  // before or after enqueue. After any nonzero modular advance it rejects.
  uint32_t advance = nondet_u32();
  __CPROVER_assume(advance != 0);
  uint32_t next = captured + advance;
  assert(!protogenCurrentIngress(true,captured,next));
  assert(!protogenCurrentIngress(false,captured,captured));
}
