#ifndef PQ_QUAL_ARENA_CONFIG_H
#define PQ_QUAL_ARENA_CONFIG_H
#include <stddef.h>
#include <stdint.h>
#define MLK_CONFIG_PARAMETER_SET 1024
#define MLK_CONFIG_NAMESPACE_PREFIX pqarena
#define MLK_CONFIG_KEYGEN_PCT
#define MLK_CONFIG_CUSTOM_ALLOC_FREE
#define MLK_CONFIG_CONTEXT_PARAMETER
#define MLK_CONFIG_CONTEXT_PARAMETER_TYPE struct PqArena *
struct PqArena {
  _Alignas(32) uint8_t bytes[24576];
  size_t top, peak, depth, attempts, fail_at;
  size_t offsets[64];
  int dirty_free;
};
static inline void *pq_arena_alloc(struct PqArena *a,size_t n) {
  const size_t padded=(n+31)&~(size_t)31;
  if (++a->attempts==a->fail_at || a->depth==64 || padded>sizeof(a->bytes)-a->top) return NULL;
  a->offsets[a->depth++]=a->top;
  void *p=a->bytes+a->top; a->top+=padded;
  if (a->top>a->peak) a->peak=a->top;
  return p;
}
static inline void pq_arena_free(struct PqArena *a,void *p,size_t n) {
  volatile uint8_t *v=(volatile uint8_t *)p;
  for (size_t i=0;i<n;++i) if (v[i]) a->dirty_free=1;
  // Upstream guarantees reverse-order free. Detect deviations in the harness.
  if (!a->depth || p!=a->bytes+a->offsets[a->depth-1]) { a->dirty_free=1; return; }
  a->top=a->offsets[--a->depth];
}
#define MLK_CUSTOM_ALLOC(v,T,N,context) T *v=(T *)pq_arena_alloc(context,sizeof(T)*(N))
#define MLK_CUSTOM_FREE(v,T,N,context) pq_arena_free(context,v,sizeof(T)*(N))
#endif
