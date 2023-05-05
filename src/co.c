#define CC_NO_SHORT_NAMES

#include "co.h"

#include <cc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
  size_t id;
  enum status {
    S_INIT,
    S_RUN,
  } status;

  co_func_t func;
  co_arg_t arg;

  jmp_buf buf;
  uint8_t stack[STACK_SIZE];
} co_t;

static struct {
  cc_vec(co_t) contests;
} co_pool;

static size_t co_count = 1; /* a self-incrementing counter */
static jmp_buf co_main;     /* scheduling stack */
static co_t* co_cur = NULL; /* runtime stack frame */

static void co_exit();

void co_new(co_func_t func, co_arg_t arg) {
  if (co_pool.contests == NULL) {
    cc_init(&co_pool.contests);
  }

  co_t co = {.id = co_count++, .status = S_INIT, .func = func, .arg = arg};

  cc_push(&co_pool.contests, co);
}

void co_yield () {
  if (!setjmp(co_cur->buf)) {
    // fprintf(stderr, "co yield, %ld\n", co_cur->id);
    longjmp(co_main, (int)co_cur->id);
  }
}

void co_loop() {
  srand(time(NULL));
  size_t id = setjmp(co_main);

  if (cc_size(&co_pool.contests) == 0) {
    fprintf(stderr, "ok\n");
    return;
  }

  size_t idx = rand() % cc_size(&co_pool.contests);
  fprintf(stderr, "idx is %ld\n", idx);
  co_cur = cc_get(&co_pool.contests, idx);

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
    longjmp(co_cur->buf, 0);
  }
}

void co_exit() {
  cc_erase(&co_pool.contests, co_cur->id);
  longjmp(co_main, -1);
}