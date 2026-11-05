/* count_alloc.h -- force-included (-include) into the relay host's sources by
 * the online tests: every malloc/calloc/realloc/free is counted, so a
 * start/stop cycle that leaks one allocation fails the test. */
#ifndef COUNT_ALLOC_H
#define COUNT_ALLOC_H
#include <stdlib.h>
#include <string.h>
extern volatile long g_ca_live;
static inline void *ca_malloc(size_t n) { void *p = malloc(n); if (p) __atomic_add_fetch(&g_ca_live, 1, __ATOMIC_SEQ_CST); return p; }
static inline void *ca_calloc(size_t n, size_t s) { void *p = calloc(n, s); if (p) __atomic_add_fetch(&g_ca_live, 1, __ATOMIC_SEQ_CST); return p; }
static inline void *ca_realloc(void *o, size_t n) { void *p = realloc(o, n); if (p && !o) __atomic_add_fetch(&g_ca_live, 1, __ATOMIC_SEQ_CST); return p; }
static inline void ca_free(void *p) { if (p) __atomic_sub_fetch(&g_ca_live, 1, __ATOMIC_SEQ_CST); free(p); }
#define malloc(n) ca_malloc(n)
#define calloc(n, s) ca_calloc(n, s)
#define realloc(o, n) ca_realloc(o, n)
#define free(p) ca_free(p)
#endif
