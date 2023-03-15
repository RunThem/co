#include "co.h"

#include <stdio.h>
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

static void co_exit_();

void co_new(co_func_t func, void* arg) {
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
    return;
  }

  do {
    co_cur = &co_.contexts[rand_next() % co_.len];
  } while (co_.len > 3 && res == co_cur->id);

  if (!co_cur->is_init) {
    co_cur->is_init = true;
    void* now       = (void*)(alignment16(((uintptr_t)co_cur->stack + STACK_SIZE)));

    asm volatile("movq %0, %%rsp;"
                 "movq %2, %%rdi;"
                 "pushq %3;"
                 "jmp *%1;"
                 :
                 : "b"(now), "d"(co_cur->func), "a"(co_cur->arg), "c"(co_exit_)
                 : "memory");
  } else {
    longjmp(co_cur->buf, 1);
  }
}

static void co_exit_() {
  for (size_t i = 0; i < co_.len; i++) {
    if (co_.contexts[i].id == co_cur->id) {
      co_.contexts[i] = co_.contexts[--co_.len];
      break;
    }
  }

  longjmp(co_main, 0);
}
