// SPDX-License-Identifier: GPL-3.0-only
// Loaded only in the task-owned native RT test; never in a user's application.
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
static _Thread_local bool observing;
static _Thread_local uint64_t allocations, releases, locks;
__attribute__((visibility("default"))) void stochas_audit_begin(void) { allocations = releases = locks = 0; observing = true; }
__attribute__((visibility("default"))) void stochas_audit_end(uint64_t *a, uint64_t *r, uint64_t *l) { observing = false; *a = allocations; *r = releases; *l = locks; }
static void *audit_malloc(size_t n) { if (observing) ++allocations; return malloc(n); }
static void *audit_calloc(size_t n, size_t s) { if (observing) ++allocations; return calloc(n, s); }
static void *audit_realloc(void *p, size_t n) { if (observing) ++allocations; return realloc(p, n); }
static void audit_free(void *p) { if (observing && p) ++releases; free(p); }
static int audit_posix_memalign(void **p, size_t a, size_t n) { if (observing) ++allocations; return posix_memalign(p, a, n); }
static int audit_mutex_lock(pthread_mutex_t *m) { if (observing) ++locks; return pthread_mutex_lock(m); }
static int audit_mutex_trylock(pthread_mutex_t *m) { if (observing) ++locks; return pthread_mutex_trylock(m); }
#define INTERPOSE(replacement, original) \
 __attribute__((used)) static struct { const void *replacement; const void *original; } \
 interpose_##original __attribute__((section("__DATA,__interpose"))) = {(const void *)&replacement, (const void *)&original};
INTERPOSE(audit_malloc, malloc)
INTERPOSE(audit_calloc, calloc)
INTERPOSE(audit_realloc, realloc)
INTERPOSE(audit_free, free)
INTERPOSE(audit_posix_memalign, posix_memalign)
INTERPOSE(audit_mutex_lock, pthread_mutex_lock)
INTERPOSE(audit_mutex_trylock, pthread_mutex_trylock)
