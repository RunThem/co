#include "co.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  size_t id;
  bool is_init;

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
  }

  co_t co = {.id      = ++co_count,
             .is_init = false,
             .func    = func,
             .arg     = arg,
             .stack   = (uint8_t*)calloc(sizeof(uint8_t), STACK_SIZE)};

  co_pool.contexts[co_pool.len++] = co;
}

void co_yield () {
  if (!setjmp(co_cur->buf)) {
    longjmp(co_main, co_cur->id);
  }
}

void co_loop() {
  rand_init();
  size_t id = setjmp(co_main);

  if (co_pool.len == 0) {
    free(co_pool.contexts);
    return;
  }

  do {
    co_cur = &co_pool.contexts[rand_next() % co_pool.len];
  } while (co_pool.len > 1 && co_cur->id == id);

  if (!co_cur->is_init) {
    co_cur->is_init = true;
    void* stack     = (void*)(alignment16(((uintptr_t)co_cur->stack + STACK_SIZE)));

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
}

void co_exit() {
  free(co_cur->stack);
  *co_cur = co_pool.contexts[--co_pool.len];

  longjmp(co_main, 0);
}

/**
 * xoshiro256+ https://prng.di.unimi.it/xoshiro256plus.c
 **/
static uint64_t rand_s[4];

static void rand_init(void) {
  srand(time(NULL));
  rand_s[0] = rand();
  rand_s[1] = rand();
  rand_s[2] = rand();
  rand_s[3] = rand();
}

static inline uint64_t rand_next(void) {
  const uint64_t result = rand_s[0] + rand_s[3];

  const uint64_t t = rand_s[1] << 17;

  rand_s[2] ^= rand_s[0];
  rand_s[3] ^= rand_s[1];
  rand_s[1] ^= rand_s[2];
  rand_s[0] ^= rand_s[3];

  rand_s[2] ^= t;

  rand_s[3] = (rand_s[3] << 45) | (rand_s[3] >> 9);

  return result;
}
