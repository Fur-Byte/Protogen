#pragma once
#include <stddef.h>
#include <stdint.h>

static inline size_t protogenCobsEncode(const uint8_t *in,size_t n,uint8_t *out,size_t capacity) {
  // The minimum includes the initial code and delimiter. Additional code
  // bytes are checked when reserved; avoid overflowing n + overhead.
  if (!out || (!in && n) || capacity < 2 || n > capacity - 2) return 0;
  size_t codeAt=0, used=1; uint8_t code=1;
  for(size_t i=0;i<n;++i) {
    if(in[i]==0) {
      out[codeAt]=code;
      if(used>=capacity)return 0;
      codeAt=used++; code=1;
    } else {
      if(used>=capacity)return 0;
      out[used++]=in[i];
      if(++code==0xff) {
        out[codeAt]=code;
        if(used>=capacity)return 0;
        codeAt=used++;code=1;
      }
    }
  }
  out[codeAt]=code; if(used>=capacity)return 0; out[used++]=0; return used;
}
static inline size_t protogenCobsDecode(const uint8_t *in,size_t n,uint8_t *out,size_t capacity) {
  if ((!in && n) || !out) return 0;
  size_t used=0, i=0;
  while(i<n) {
    const uint8_t code=in[i++]; if(!code || (size_t)(code-1)>n-i || (size_t)(code-1)>capacity-used) return 0;
    for(unsigned j=1;j<code;++j) { if(!in[i]) return 0; out[used++]=in[i++]; }
    if(code!=0xff && i<n) {if(used==capacity)return 0;out[used++]=0;}
  }
  return used;
}
