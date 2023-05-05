#include "co.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
  size_t id;
  enum status {
    S_INIT,
    S_RUN,
    S_UP,
    S_DEL,
  } status;

  co_func_t func;
  co_arg_t arg;

  jmp_buf buf;
  uint8_t* stack;
} co_t;

static struct {
  co_t* contexts; /* contexts */
  size_t len;     /* index */
  size_t alloc;   /* contexts alloc size */
} co_pool;

static size_t co_count = 0; /* a self-incrementing counter */

static jmp_buf co_main;     /* scheduling stack */
static co_t* co_cur = NULL; /* runtime stack frame */

static void co_exit();

static void rand_init(void);
static uint64_t rand_next(void);

void co_new(co_func_t func, co_arg_t arg) {
  if (co_pool.contexts == NULL) {
    co_pool.alloc    = 16;
    co_pool.contexts = (co_t*)calloc(co_pool.alloc, sizeof(co_t));
  } else if (co_pool.len == co_pool.alloc) {
    co_pool.alloc *= 2;
    co_pool.contexts = (co_t*)realloc(co_pool.contexts, co_pool.alloc * sizeof(co_t));
    if (co_pool.contexts == NULL) {
      fprintf(stderr, "out of memory\n");
      return;
    }
  }

  co_t co = {.id     = ++co_count,
             .status = S_INIT,
             .func   = func,
             .arg    = arg,
             .stack  = (uint8_t*)calloc(sizeof(uint8_t), STACK_SIZE)};

  co_pool.contexts[co_pool.len++] = co;
}

void co_yield () {
  if (!setjmp(co_cur->buf)) {
    co_cur->status = S_UP;
    fprintf(stderr, "co yield, %ld\n", co_cur->id);
    longjmp(co_main, (int)co_cur->id);
  }
}

void co_loop() {
  srand(time(NULL));

  size_t id = setjmp(co_main);

  if (id == 0) {
    fprintf(stderr, "co end\n");
  } else {
    fprintf(stderr, "co loop, %ld\n", id);
  }

  fprintf(stderr, "pool len %ld\n", co_pool.len);
  if (co_pool.len == 0) {
    free(co_pool.contexts);
    return;
  }

  do {
    int idx = rand() % co_pool.len;
    if (idx == 0) {
      continue;
    }

    co_cur = &co_pool.contexts[idx];
  } while (co_pool.len > 1 && co_cur->id == id && co_cur->status != S_DEL);

  fprintf(stderr, "run is %ld\n", co_cur->id);

  if (co_cur->status == S_INIT) {
    co_cur->status = S_RUN;
    void* stack    = (void*)(alignment16(((uintptr_t)co_cur->stack + STACK_SIZE - 16)));

    asm volatile("movq %0, %%rsp;"
                 "movq %2, %%rdi;"
                 "pushq %3;"
                 "jmp *%1;"
                 :
                 : "b"(stack), "d"(co_cur->func), "a"(co_cur->arg), "c"(co_exit)
                 : "memory");
  } else {
    longjmp(co_cur->buf, 1);
  }

  fprintf(stderr, "stack error!\n");
}

void co_exit() {
  fprintf(stderr, "exit id %ld\n", co_cur->id);

  co_cur->status = S_DEL;
  co_pool.len--;

  longjmp(co_main, 0);
}

void co_show() {
  for (size_t i = 0; i < co_pool.len; i++) {
    co_t* co = &co_pool.contexts[i];
    fprintf(stderr, "id is %ld, stack is %p\n", co->id, co->stack);
  }
}