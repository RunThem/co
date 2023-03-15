#include "co.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  size_t id;
  bool is_init;

  co_func_t func;
  co_arg_t arg;

  jmp_buf buf;
  uint8_t stack[STACK_SIZE];
} co_t;

static struct {
  co_t* contexts; /* contexts */
  size_t len;     /* index */
  size_t alloc;   /* contexts alloc size */
} co_;

static size_t co_count; /* a self-incrementing counter */

static jmp_buf co_main;     /* scheduling stack */
static co_t* co_cur = NULL; /* runtime stack frame */

static void co_exit();

static void rand_init(void);
static uint64_t rand_next(void);

void co_new(co_func_t func, co_arg_t arg) {
  co_t co = {.func = func, .arg = arg, .is_init = false, .id = ++co_count};

  if (co_.contexts == NULL) {
    co_.alloc    = 16;
    co_.contexts = (co_t*)calloc(co_.alloc, sizeof(co_t));
  } else if (co_.len == co_.alloc) {
    co_.alloc *= 2;
    co_.contexts = (co_t*)realloc(co_.contexts, co_.alloc * sizeof(co_t));
  }

  co_.contexts[co_.len++] = co;
}

void co_yield () {
  if (!setjmp(co_cur->buf)) {
    longjmp(co_main, co_cur->id);
  }
}

void co_loop() {
  rand_init();
  size_t res = setjmp(co_main);

  if (co_.len == 0) {
    free(co_.contexts);
    return;
  }

  do {
    co_cur = &co_.contexts[rand_next() % co_.len];
  } while (co_.len > 1 && res == co_cur->id);

  if (!co_cur->is_init) {
    co_cur->is_init = true;
    void* now       = (void*)(alignment16(((uintptr_t)co_cur->stack + STACK_SIZE)));

    asm volatile("movq %0, %%rsp;"
                 "movq %2, %%rdi;"
                 "pushq %3;"
                 "jmp *%1;"
                 :
                 : "b"(now), "d"(co_cur->func), "a"(co_cur->arg), "c"(co_exit)
                 : "memory");
  } else {
    longjmp(co_cur->buf, 1);
  }
}

static void co_exit() {
  if (co_.len != 1) {
    *co_cur = co_.contexts[--co_.len];
  }

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
