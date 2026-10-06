#include "arena_config.h"
#include "mlkem_native.h"
#include <sys/random.h>
#include <string.h>
static struct PqArena arena;
static size_t fail_at;
int randombytes(uint8_t *p,size_t n) {
  while (n) { ssize_t r=getrandom(p,n,0); if (r<=0) return -1; p+=r; n-=(size_t)r; }
  return 0;
}
static void start(void) { memset(&arena,0,sizeof(arena)); arena.fail_at=fail_at; }
static int end(int result) {
  if (arena.dirty_free || arena.top || arena.depth) return -100;
  volatile uint8_t *p=arena.bytes;
  for (size_t i=0;i<sizeof(arena.bytes);++i) p[i]=0;
  return result;
}
int qual_keygen(uint8_t *pk,uint8_t *sk,const uint8_t *coins) {
  start(); return end(pqarena_keypair_derand(pk,sk,coins,&arena));
}
int qual_enc(uint8_t *ct,uint8_t *ss,const uint8_t *pk,const uint8_t *coins) {
  start(); return end(pqarena_enc_derand(ct,ss,pk,coins,&arena));
}
int qual_dec(uint8_t *ss,const uint8_t *ct,const uint8_t *sk) {
  start(); return end(pqarena_dec(ss,ct,sk,&arena));
}
size_t qual_peak(void) { return arena.peak; }
size_t qual_attempts(void) { return arena.attempts; }
void qual_fail_at(size_t n) { fail_at=n; }
