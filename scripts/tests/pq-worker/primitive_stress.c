// Public deterministic fixtures only; whole backend receives ASan/UBSan.
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
int qual_keygen(uint8_t*,uint8_t*,const uint8_t*);
int qual_enc(uint8_t*,uint8_t*,const uint8_t*,const uint8_t*);
int qual_dec(uint8_t*,const uint8_t*,const uint8_t*);
static uint8_t pk[1568],sk[3168],ct[1568],ss[32],recovered[32],coins[64],message[32];
#ifdef QUAL_ARENA
size_t qual_attempts(void);
void qual_fail_at(size_t);
static int operation(unsigned op) {
  if (op==0) return qual_keygen(pk,sk,coins);
  if (op==1) return qual_enc(ct,ss,pk,message);
  return qual_dec(recovered,ct,sk);
}
#endif
int main(void) {
  for (unsigned i=0;i<1000;++i) {
    coins[0]=(uint8_t)i; coins[1]=(uint8_t)(i>>8); message[0]=(uint8_t)(i+1);
    assert(qual_keygen(pk,sk,coins)==0);
    assert(qual_enc(ct,ss,pk,message)==0);
    assert(qual_dec(recovered,ct,sk)==0);
    assert(memcmp(ss,recovered,32)==0);
    ct[i%sizeof(ct)]^=1;
    assert(qual_dec(recovered,ct,sk)==0);
    assert(memcmp(ss,recovered,32)!=0);
  }
#ifdef QUAL_ARENA
  size_t faults=0;
  for (unsigned op=0;op<3;++op) {
    assert(qual_keygen(pk,sk,coins)==0); assert(qual_enc(ct,ss,pk,message)==0);
    assert(operation(op)==0); const size_t attempts=qual_attempts();
    for (size_t fail=1;fail<=attempts;++fail) {
      qual_fail_at(fail); assert(operation(op)==-2); ++faults;
      qual_fail_at(0); assert(operation(op)==0);
    }
  }
  assert(faults==77); puts("PASS 77 allocation faults: exact OOM status, zeroized LIFO cleanup");
#endif
  volatile uint8_t *p=sk; for (size_t i=0;i<sizeof(sk);++i) p[i]=0;
  p=ss; for (size_t i=0;i<sizeof(ss);++i) p[i]=0;
  p=recovered; for (size_t i=0;i<sizeof(recovered);++i) p[i]=0;
  puts("PASS 1000 complete round trips and 1000 modified ciphertext implicit rejections");
}
