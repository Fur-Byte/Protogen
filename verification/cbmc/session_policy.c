#include "abi.h"
#include "SessionPolicy.h"
#include "NonceCore.h"
#include "ReplayCore.h"
#include <assert.h>
#include <string.h>
extern uint64_t nondet_u64(void);
extern bool nondet_bool(void);
extern uint8_t nondet_byte(void);
int main(void) {
  uint64_t sequence = nondet_u64(), records = nondet_u64();
  bool active = nondet_bool();
  uint64_t before = sequence, originalRecords = records;
  bool seal = protogenMaySeal(active, records, sequence);
  assert(seal == (active && records < UINT64_C(1000000) && sequence < UINT64_C(0x7fffffffffffffff)));
  assert(protogenMayOpen(active, records) == (active && records < UINT64_C(1000000)));
  assert(protogenNeedsRekey(records, sequence) ==
    (records >= UINT64_C(1000000) || sequence >= UINT64_C(0x7ffffffffffffbff)));
  if (seal) {
    uint64_t first = protogenReserveSequence(&sequence);
    assert(first == sequence && sequence > before && sequence <= PROTOGEN_SEQUENCE_LIMIT);
    bool encrypted = nondet_bool();
    protogenAccountRecord(&records, encrypted);
    assert(records == originalRecords + (uint64_t)encrypted && records <= PROTOGEN_RECORD_LIMIT);
    if (protogenMaySeal(active, records, sequence)) {
      uint64_t second = protogenReserveSequence(&sequence);
      uint8_t a[12], b[12], role = nondet_byte();
      protogenNonce(a, 3, role, first);
      protogenNonce(b, 3, role, second);
      assert(second > first && memcmp(a,b,12) != 0);
    }
  }
  bool authenticated = nondet_bool(), parsed = nondet_bool(), valid = nondet_bool();
  bool commit = protogenMayCommitReplay(authenticated, parsed, valid);
  assert(commit == (authenticated && parsed && valid));
  uint64_t top = nondet_u64(), bits = nondet_u64(), incoming = nondet_u64();
  __CPROVER_assume(top <= PROTOGEN_MAX_SEQUENCE);
  if (protogenReplayAllows(top,bits,incoming)) {
    uint64_t oldTop = top, oldBits = bits;
    if (commit) protogenReplayAccept(&top,&bits,incoming);
    if (commit) assert(!protogenReplayAllows(top,bits,incoming));
    else assert(top == oldTop && bits == oldBits && protogenReplayAllows(top,bits,incoming));
  }
}
