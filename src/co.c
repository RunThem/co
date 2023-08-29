#define CC_NO_SHORT_NAMES

#include "co.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if 1
#  define die(fmt, ...)
#else
#  define die(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

typedef struct {
  size_t id;
  enum status {
    S_INIT,
    S_RUN,
    S_END,
  } status;

  co_func_t func;
  co_arg_t arg;

  jmp_buf buf;
  uint8_t stack[STACK_SIZE];
} co_t;

static co_t* co      = NULL;
static size_t co_len = 0;
static size_t co_cap = 100000;

static jmp_buf co_main = {0};  /* scheduling stack */
static co_t* co_cur    = NULL; /* runtime stack frame */

static void co_exit();

void co_new(co_func_t func, co_arg_t arg) {
  if (co == NULL) {
    co = calloc(co_cap, sizeof(co_t));
  }

  co[co_len].id     = co_len;
  co[co_len].status = S_INIT;
  co[co_len].func   = func;
  co[co_len].arg    = arg;

  co_len++;
}

void co_yield () {
  if (!setjmp(co_cur->buf)) {
    longjmp(co_main, (int)co_cur->id);
  }
}

void co_loop() {
  int flag = setjmp(co_main);

  if (flag == -1) {
    die("%zu finished", co_cur->id);
  }

  co_cur = NULL;
  for (size_t i = 0; i < co_len; i++) {
    if (co[i].status != S_END) {
      die("idx is %zu\n", i);
      co_cur = &co[i];

      break;
    }
  }

  if (co_cur == NULL) {
    return;
  }

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
  co_cur->status = S_END;
  longjmp(co_main, -1);
}
