#include "co.h"

#include <setjmp.h>
#include <threads.h>

/* libs */
#include <u/core/queue.h>
#include <u/core/stack.h>
#include <u/u.h>

typedef enum {
  CO_STATUS_SUSPEND,
  CO_STATUS_RUNNING,
  CO_STATUS_DEAD,
} co_status_e;

typedef struct {
  size_t id;
  co_status_e statue;

  co_func_t func;
  co_arg_t arg;

  jmp_buf ctx;
  uint8_t stack[CO_STACK_SIZE];
} co_t;

typedef struct {
  size_t id;
  jmp_buf main_ctx;
  co_t* curr_co;

  u_queue_t(co_t*) ready;
  u_stack_t(co_t*) free;
} co_loop_t;

static co_loop_t loop  = {0};
once_flag co_init_flag = ONCE_FLAG_INIT;

static void __co_init() {
  loop.ready = u_queue_new(co_t*, CO_NUMS);
  loop.free  = u_stack_new(co_t*, CO_NUMS);
}

void co_new(co_func_t func, co_arg_t arg) {
  co_t* co = NULL;

  call_once(&co_init_flag, __co_init);

  if (!u_stack_empty(loop.free)) {
    co = u_stack_peek(loop.free);

    u_stack_pop(loop.free);
  } else {
    co = u_zalloc(sizeof(co_t));
  }

  co->id     = loop.id++;
  co->statue = CO_STATUS_SUSPEND;
  co->func   = func;
  co->arg    = arg;

  infln("id = %zu, func = %p, arg = %p", co->id, co->func, co->arg);

  u_queue_push(loop.ready, co);
}

void co_yield () {
  if (!setjmp(loop.curr_co->ctx)) {
    longjmp(loop.main_ctx, (int)loop.curr_co->id);
  }
}

void co_exit() {
  longjmp(loop.main_ctx, -1);
}

void co_loop() {
  int flag = setjmp(loop.main_ctx);

  if (flag == -1) { /* free co */
    infln("%zu finished", loop.curr_co->id);
    u_stack_push(loop.free, loop.curr_co);
    u_queue_pop(loop.ready);
  }

  u_err_if(u_queue_empty(loop.ready), end);

  loop.curr_co = u_queue_peek(loop.ready);

  if (loop.curr_co->statue == CO_STATUS_SUSPEND) {
    infln("first run %zu", loop.curr_co->id);

    loop.curr_co->statue = CO_STATUS_RUNNING;
    any_t stack = any(align_of(as(loop.curr_co->stack, uintptr_t) + CO_STACK_SIZE - 16, 16));

    asm volatile("movq %0, %%rsp;"
                 "movq %2, %%rdi;"
                 "pushq %3;"
                 "jmp *%1;"
                 :
                 : "b"(stack), "d"(loop.curr_co->func), "a"(loop.curr_co->arg), "c"(co_exit)
                 : "memory");
  } else {
    infln("continue run %zu", loop.curr_co->id);
    longjmp(loop.curr_co->ctx, 0);
  }

end:
  while (!u_stack_empty(loop.free)) {
    u_free(u_stack_peek(loop.free));
    u_stack_pop(loop.free);
  }

  u_stack_cleanup(loop.free);
  u_queue_cleanup(loop.ready);
}
