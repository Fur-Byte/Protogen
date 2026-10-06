#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*pqv4_rng)(void*, uint8_t*, size_t);
int pqv4_native(unsigned operation, uint8_t*, uint8_t*, uint8_t*, uint8_t*, pqv4_rng, void*);
void pqv4_sha3(uint8_t[32], const uint8_t*, size_t);
#ifdef __cplusplus
}
#endif
