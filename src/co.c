#include "co.h"

#include <mimalloc.h>
#include <setjmp.h>
#include <sys/queue.h>

/* libs */

#ifdef NDEBUG
#  define infln(fmt, ...)
#else
#  include <stdio.h>

#  define infln(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__);
#endif

typedef struct co_t {
  size_t id;

  co_func_t func;
  co_args_t arg;

  jmp_buf ctx;
  uint8_t* stack;

  STAILQ_ENTRY(co_t) next;
} co_t;

typedef STAILQ_HEAD(co_list_t, co_t) co_list_t, *co_list_ref_t;

typedef struct {
  size_t id;
  jmp_buf main_ctx;
  co_t* run;

  co_list_t ready;
  co_list_t dead;
} co_loop_t;

static co_loop_t loop = {
    .id    = 1,
    .ready = {.stqh_first = NULL, .stqh_last = &loop.ready.stqh_first},
    .dead  = {.stqh_first = NULL, .stqh_last = &loop.dead.stqh_first },
};

static void co_exit() {
  longjmp(loop.main_ctx, -1);
}

/*
 * @param stack         the stack               (rdi)
 * @param exit          the exit function       (rsi)
 * @param func          the entry function      (rdx)
 * @param arg           the entry function args (rcx)
 * */
int __switch(void* stack, void (*exit)(), co_func_t func, co_args_t arg);

asm(".text                                               \n"
    ".align 8                                            \n"
    ".globl  __switch                                    \n"
    ".type   __switch %function                          \n"
    ".hidden __switch                                    \n"
    "__switch:                                           \n"
    "     # 16-align for the stack top address           \n"
    "     movabs $-16, %r8                               \n"
    "     andq %r8, %rdi                                 \n"
    "                                                    \n"
    "     # switch to the new stack                      \n"
    "     movq %rdi, %rsp                                \n"
    "                                                    \n"
    "     # save exit function                           \n"
    "     pushq %rsi                                     \n"
    "                                                    \n"
    "     # save entry function args                     \n"
    "     movq %rcx, %rdi                                \n"
    "                                                    \n"
    "     # jum entry function                           \n"
    "     jmp *%rdx                                      \n");

void co_new(co_func_t func, co_args_t args) {
  co_t* co = NULL;

  if (!STAILQ_EMPTY(&loop.dead)) {
    co = STAILQ_FIRST(&loop.dead);
    STAILQ_REMOVE_HEAD(&loop.dead, next);
  } else {
    co = mi_calloc(CO_STACK_SIZE, 1);
  }

  co->id    = loop.id++;
  co->func  = func;
  co->arg   = args;
  co->stack = NULL;

  infln("id = %zu, func = %p, arg = %p", co->id, co->func, co->arg);

  STAILQ_INSERT_TAIL(&loop.ready, co, next);
}

void co_yield () {
  if (!setjmp(loop.run->ctx)) {
    longjmp(loop.main_ctx, (int)loop.run->id);
  }
}

void co_loop() {
  co_t* co = NULL;
  int flag = setjmp(loop.main_ctx);

  if (flag == -1) { /* free co */
    infln("%zu finished", loop.run->id);

    STAILQ_REMOVE_HEAD(&loop.ready, next);
    STAILQ_INSERT_HEAD(&loop.dead, loop.run, next);
  }

  if (STAILQ_EMPTY(&loop.ready)) {
    goto end;
  }

  loop.run = STAILQ_FIRST(&loop.ready);

  if (!loop.run->stack) {
    infln("first run %zu", loop.run->id);

    loop.run->stack = (void*)loop.run + CO_STACK_SIZE - 16;
    __switch(loop.run->stack, co_exit, loop.run->func, loop.run->arg);
  } else {
    infln("continue run %zu", loop.run->id);
    longjmp(loop.run->ctx, 0);
  }

  infln("end");

end:
  while ((co = STAILQ_FIRST(&loop.ready))) {
    STAILQ_REMOVE_HEAD(&loop.ready, next);
    mi_free(co);
  }

  while ((co = STAILQ_FIRST(&loop.dead))) {
    STAILQ_REMOVE_HEAD(&loop.dead, next);
    mi_free(co);
  }
}
