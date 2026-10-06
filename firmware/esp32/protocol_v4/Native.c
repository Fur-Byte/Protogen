#include "Native.h"
#include "vendor/mlkem-native/mlkem/mlkem_native.c"
static _Thread_local pqv4_rng current_rng;
static _Thread_local void *current_context;
int randombytes(uint8_t *out, size_t n) {
  return current_rng ? current_rng(current_context, out, n) : -1;
}
int pqv4_native(unsigned op, uint8_t *pk, uint8_t *sk, uint8_t *ct, uint8_t *ss,
                pqv4_rng rng, void *context) {
  if (current_rng || !rng || op > 2)
    return -1;
  current_rng = rng;
  current_context = context;
  int status = op == 0   ? pqv4_keypair(pk, sk)
               : op == 1 ? pqv4_enc(ct, ss, pk)
                         : pqv4_dec(ss, ct, sk);
  current_rng = NULL;
  current_context = NULL;
  return status;
}
void pqv4_sha3(uint8_t out[32], const uint8_t *in, size_t n) {
  pqv4_sha3_256(out, in, n);
}
