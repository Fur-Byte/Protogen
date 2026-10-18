#pragma once
// OS entropy for host qualification only. Never selected by MCU firmware.
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#if defined(__linux__)
#include <sys/random.h>
#elif defined(__APPLE__)
#include <sys/random.h>
#else
#error Unsupported_host_entropy_provider
#endif
static inline int qualification_os_random(uint8_t *p,size_t n) {
  while(n) {
#if defined(__linux__)
    const ssize_t r=getrandom(p,n,0);
    if(r<0&&errno==EINTR)continue;
    if(r<=0)return -1;
    const size_t take=(size_t)r;
#else
    const size_t take=n>256?256:n;
    if(getentropy(p,take)!=0)return -1;
#endif
    p+=take;n-=take;
  }
  return 0;
}
