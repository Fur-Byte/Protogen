// POSIX guarded host stacks. This is NOT MCU stack high-water.
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "host_rng.h"
#include <unistd.h>
#include "mlkem_native.h"
static uint8_t pk[1568],sk[3168],ct[1568],ss[32],recovered[32];
int randombytes(uint8_t* p,size_t n) {
  return qualification_os_random(p,n);
}
static void* task(void* unused) {
  (void)unused;
  for (unsigned i=0;i<100;++i) {
    assert(pqqual_keypair(pk,sk)==0);
    assert(pqqual_enc(ct,ss,pk)==0);
    assert(pqqual_dec(recovered,ct,sk)==0);
    assert(memcmp(ss,recovered,32)==0);
  }
  return NULL;
}
int main(int argc,char** argv) {
  assert(argc==2); size_t n=(size_t)strtoul(argv[1],NULL,10),page=(size_t)sysconf(_SC_PAGESIZE);
  assert(n%page==0);
  uint8_t *map=mmap(NULL,n+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  assert(map!=MAP_FAILED); uint8_t* stack=map+page;
  assert(mprotect(stack,n,PROT_READ|PROT_WRITE)==0); memset(stack,0xa5,n);
  pthread_attr_t attr; assert(pthread_attr_init(&attr)==0);
  assert(pthread_attr_setstack(&attr,stack,n)==0);
  pthread_t thread; assert(pthread_create(&thread,&attr,task,NULL)==0);
  assert(pthread_join(thread,NULL)==0); assert(pthread_attr_destroy(&attr)==0);
  size_t intact=0; while (intact<n && stack[intact]==0xa5) ++intact;
  printf("{\"host_stack_bytes\":%zu,\"host_canary_untouched_bytes\":%zu,\"host_written_extent_bytes\":%zu,\"operations\":300}\n",n,intact,n-intact);
  volatile uint8_t* wipe=stack; for (size_t i=0;i<n;++i) wipe[i]=0;
  assert(munmap(map,n+2*page)==0);
}
