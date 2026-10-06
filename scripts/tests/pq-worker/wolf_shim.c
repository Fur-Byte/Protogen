#include <wolfssl/wolfcrypt/wc_mlkem.h>
#include <string.h>
static MlKemKey key; // Single caller only in this qualification harness.
static void clear(void) {
  wc_MlKemKey_Free(&key);
  volatile unsigned char *p=(volatile unsigned char *)&key;
  for (size_t i=0;i<sizeof(key);++i) p[i]=0;
}
int qual_keygen(unsigned char *pk,unsigned char *sk,const unsigned char *coins) {
  int r=wc_MlKemKey_Init(&key,WC_ML_KEM_1024,NULL,INVALID_DEVID);
  if (!r) r=wc_MlKemKey_MakeKeyWithRandom(&key,coins,64);
  if (!r) r=wc_MlKemKey_EncodePublicKey(&key,pk,1568);
  if (!r) r=wc_MlKemKey_EncodePrivateKey(&key,sk,3168);
  clear(); return r;
}
int qual_enc(unsigned char *ct,unsigned char *ss,const unsigned char *pk,const unsigned char *coins) {
  int r=wc_MlKemKey_Init(&key,WC_ML_KEM_1024,NULL,INVALID_DEVID);
  if (!r) r=wc_MlKemKey_DecodePublicKey(&key,pk,1568);
  if (!r) r=wc_MlKemKey_EncapsulateWithRandom(&key,ct,ss,coins,32);
  clear(); return r;
}
int qual_dec(unsigned char *ss,const unsigned char *ct,const unsigned char *sk) {
  int r=wc_MlKemKey_Init(&key,WC_ML_KEM_1024,NULL,INVALID_DEVID);
  if (!r) r=wc_MlKemKey_DecodePrivateKey(&key,sk,3168);
  if (!r) r=wc_MlKemKey_Decapsulate(&key,ss,ct,1568);
  clear(); return r;
}
size_t qual_context_size(void) { return sizeof(key); }
