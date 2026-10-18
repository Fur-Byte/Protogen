// Single-caller host measurement wrapper, PCT enabled. Never a production RNG.
#include "mlkem_native.h"
#include "host_rng.h"
int randombytes(uint8_t *p,size_t n) {
  return qualification_os_random(p,n);
}
int qual_keygen(uint8_t *pk,uint8_t *sk,const uint8_t *coins) {
  return pqqual_keypair_derand(pk,sk,coins);
}
int qual_enc(uint8_t *ct,uint8_t *ss,const uint8_t *pk,const uint8_t *coins) {
  return pqqual_enc_derand(ct,ss,pk,coins);
}
int qual_dec(uint8_t *ss,const uint8_t *ct,const uint8_t *sk) {
  return pqqual_dec(ss,ct,sk);
}
