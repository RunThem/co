#include "co.h"

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static struct {
  co_t* contexts; /* contexts */
  size_t len;     /* index */
  size_t alloc;   /* contexts alloc size */
} _co;

static size_t co_cnt; /* a self-incrementing counter */

static co_t co_main;        /* scheduling stack */
static co_t* co_cur = NULL; /* runtime stack frame */

static void co_exit_();

void co_new(co_func_t func, void* arg) {
  co_t co = {.func = func, .arg = arg, .is_init = false, .count = co_cnt++};

  if (_co.contexts == NULL) {
    _co.alloc    = 16;
    _co.contexts = (co_t*)calloc(_co.alloc, sizeof(co_t));
  } else if (_co.len == _co.alloc) {
    _co.alloc *= 2;
    _co.contexts = (co_t*)realloc(_co.contexts, _co.alloc * sizeof(co_t));
  }

  _co.contexts[_co.len++] = co;
}

void co_yield () {
  if (!setjmp(co_cur->buf)) {
    longjmp(co_main.buf, co_cur->count);
  }
}

void co_loop() {
  srand(time(NULL));
  size_t res = setjmp(co_main.buf);

  if (_co.len == 0) {
    return;
  }

  do {
    co_cur = &_co.contexts[rand() % _co.len];
  } while (_co.len > 3 && res == co_cur->count);

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
  for (size_t i = 0; i < _co.len; i++) {
    if (_co.contexts[i].count == co_cur->count) {
      _co.contexts[i] = _co.contexts[--_co.len];
      break;
    }
  }

  longjmp(co_main.buf, 0);
}
