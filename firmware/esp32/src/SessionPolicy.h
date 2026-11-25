#pragma once
#include <stdbool.h>
#include <stdint.h>

// Bounds are shared by the session implementation and verification harness.
#define PROTOGEN_RECORD_LIMIT UINT64_C(1000000)
#define PROTOGEN_SEQUENCE_LIMIT UINT64_C(0x7fffffffffffffff)
#define PROTOGEN_RENEW_MARGIN UINT64_C(1024)

static inline bool protogenMayOpen(bool active, uint64_t records) {
  return active && records < PROTOGEN_RECORD_LIMIT;
}
static inline bool protogenMaySeal(bool active, uint64_t records, uint64_t sequence) {
  return protogenMayOpen(active, records) && sequence < PROTOGEN_SEQUENCE_LIMIT;
}
static inline bool protogenNeedsRekey(uint64_t records, uint64_t sequence) {
  return records >= PROTOGEN_RECORD_LIMIT ||
         sequence >= PROTOGEN_SEQUENCE_LIMIT - PROTOGEN_RENEW_MARGIN;
}
// Reserve before calling AEAD, including on failure: never retry a nonce.
static inline uint64_t protogenReserveSequence(uint64_t *sequence) {
  return ++*sequence;
}
// Precondition: admission checked the record limit. Authenticated malformed
// incoming records also consume a credit, but cannot commit replay state.
static inline void protogenAccountRecord(uint64_t *records, bool authenticated) {
  if (authenticated) ++*records;
}
static inline bool protogenMayCommitReplay(bool authenticated, bool decoded,
                                           bool validApplication) {
  return authenticated && decoded && validApplication;
}
