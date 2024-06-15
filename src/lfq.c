#ifndef __LFQ__
#define __LFQ__

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

typedef struct lfq_node_t {
  void* item;
  _Atomic(struct lfq_node_t*) next;
} lfq_node_t;

typedef struct lfq_t {
  _Atomic(lfq_node_t*) head;
  _Atomic(lfq_node_t*) tail;
  _Atomic(lfq_node_t*) bufs;

  _Atomic(size_t) size;
} lfq_t;

static inline void lfq_init(lfq_t* lfq) {
  atomic_init(&lfq->size, 0);
  atomic_init(&lfq->head, nullptr);
  atomic_init(&lfq->tail, nullptr);
  atomic_init(&lfq->bufs, nullptr);
}

static inline void lfq_cleanup(lfq_t* lfq) {
  free(lfq->bufs);
}

static inline bool lfq_put(lfq_t* lfq, void* obj) {
  lfq_node_t* tail = nullptr;
  lfq_node_t* node = nullptr;
  lfq_node_t* cmp  = nullptr;

  node = malloc(sizeof(lfq_node_t));
  if (node == nullptr) {
    return false;
  }

  node->item = obj;
  node->next = nullptr;

  do {
    tail = lfq->tail;
  } while (atomic_compare_exchange_weak(&tail->next, &cmp, node));
  atomic_store_explicit(&lfq->tail, node, memory_order_release);
  atomic_fetch_add(&lfq->size, 1);

  return true;
}

static inline void* lfq_pop(lfq_t* lfq) {

  return nullptr;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !__LFQ__ */
