#ifndef __LFQ__
#define __LFQ__

#include <assert.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
  atomic_size_t count;
  atomic_size_t head;
  size_t tail;
  size_t max;
  void* _Atomic* bufs;
  int flags;
} lfq_t;

static inline void lfq_init(lfq_t* lfq, size_t cap) {
  atomic_init(&lfq->count, 0);
  atomic_init(&lfq->head, 0);

  lfq->tail = 0;
  lfq->bufs = calloc(cap, sizeof(void*));
  lfq->max  = cap;

  atomic_thread_fence(memory_order_release);
}

static inline void lfq_cleanup(lfq_t* lfq) {
  free(lfq->bufs);
}

static inline bool lfq_put(lfq_t* lfq, void* obj) {
  size_t count = atomic_fetch_add_explicit(&lfq->count, 1, memory_order_acquire);
  if (count >= lfq->max) {
    atomic_fetch_sub_explicit(&lfq->count, 1, memory_order_release);
    return false;
  }

  size_t head = atomic_fetch_add_explicit(&lfq->head, 1, memory_order_acquire);
  assert(lfq->bufs[head % lfq->max] == 0);
  void* rv = atomic_exchange_explicit(&lfq->bufs[head % lfq->max], obj, memory_order_release);
  assert(rv == nullptr);

  return true;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !__LFQ__ */
