#ifndef __LFQ__
#define __LFQ__

#include <assert.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
  size_t cap;
  _Atomic(size_t) count;
  _Atomic(size_t) head;
  size_t tail;
  _Atomic(void*)* bufs;
} lfq_t;

static inline void lfq_init(lfq_t* lfq, size_t cap) {
  atomic_init(&lfq->count, 0);
  atomic_init(&lfq->head, 0);

  lfq->tail = 0;
  lfq->bufs = calloc(cap, sizeof(void*));
  lfq->cap  = cap;

  atomic_thread_fence(memory_order_release);
}

static inline void lfq_cleanup(lfq_t* lfq) {
  free(lfq->bufs);
}

static inline bool lfq_put(lfq_t* lfq, void* obj) {
  size_t count = atomic_fetch_add_explicit(&lfq->count, 1, memory_order_acquire);
  if (count >= lfq->cap) {
    atomic_fetch_sub_explicit(&lfq->count, 1, memory_order_release);
    return false;
  }

  size_t head = atomic_fetch_add_explicit(&lfq->head, 1, memory_order_acquire);
  assert(lfq->bufs[head % lfq->cap] == nullptr);
  void* rv = atomic_exchange_explicit(&lfq->bufs[head % lfq->cap], obj, memory_order_release);
  assert(rv == nullptr);

  return true;
}

static inline void* lfq_pop(lfq_t* lfq) {
  void* obj = atomic_exchange_explicit(&lfq->bufs[lfq->tail], NULL, memory_order_acquire);
  if (obj == nullptr) {
    return nullptr;
  }

  if (++lfq->tail >= lfq->cap) {
    lfq->tail = 0;
  }

  size_t rv = atomic_fetch_sub_explicit(&lfq->count, 1, memory_order_release);
  assert(rv > 0);

  return obj;
}

static inline void* lfq_peek(lfq_t* lfq) {
  return atomic_load_explicit(&lfq->bufs[lfq->tail], memory_order_relaxed);
}

static inline size_t lfq_count(lfq_t* lfq) {
  return atomic_load_explicit(&lfq->count, memory_order_relaxed);
}

static inline bool lfq_empty(lfq_t* lfq) {
  return atomic_load_explicit(&lfq->count, memory_order_relaxed) == 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !__LFQ__ */
