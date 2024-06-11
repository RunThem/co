#include "co.h"

#include <mimalloc.h>
#include <setjmp.h>
#include <string.h>
#include <sys/queue.h>

/* libs */

#ifdef NDEBUG
#  define infln(fmt, ...)
#else
#  include <stdio.h>

#  define infln(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__);
#endif

typedef uint64_t reg_t;
typedef struct co_t {
  size_t id;

  void* func;
  reg_t args[4];

  jmp_buf ctx;
  uint8_t* stack;

  STAILQ_ENTRY(co_t) next;
} co_t;

typedef STAILQ_HEAD(co_list_t, co_t) co_list_t, *co_list_ref_t;

typedef struct {
  size_t id;

  jmp_buf ctx;
  co_t* run;

  reg_t regs[3];

  co_list_t ready;
  co_list_t dead;
} co_loop_t;

static co_loop_t loop = {
    .id    = 1,
    .ready = {.stqh_first = NULL, .stqh_last = &loop.ready.stqh_first},
    .dead  = {.stqh_first = NULL, .stqh_last = &loop.dead.stqh_first },
};

void co_exit() {
  longjmp(loop.ctx, -1);
}

/*
 * @param stack         the stack               (rdi)
 * @param func          the func                (rsi)
 * @param rdi           the func args           (rdx)
 * @param rsi           the func args           (rcx)
 * @param rdx           the func args           (r8)
 * */
int __switch(void* stack, void* func, reg_t rdi, reg_t rsi, reg_t rdx);

asm(".text                                               \n"
    ".align 8                                            \n"
    ".globl  __switch                                    \n"
    ".type   __switch %function                          \n"
    ".hidden __switch                                    \n"
    "__switch:                                           \n"
    "     # 16-align for the stack top address           \n"
    "     movabs $-16, %rax                              \n"
    "     andq %rax, %rdi                                \n"
    "                                                    \n"
    "     # switch to the new stack                      \n"
    "     movq %rdi, %rsp                                \n"
    "                                                    \n"
    "     # save exit function                           \n"
    "     leaq co_exit(%rip), %rax                       \n"
    "     pushq %rax                                     \n"
    "                                                    \n"
    "     # save entry function args                     \n"
    "     movq %rsi, %rax                                \n"
    "     movq %rdx, %rdi                                \n"
    "     movq %rcx, %rsi                                \n"
    "     movq %r8,  %rdx                                \n"
    "                                                    \n"
    "     # jum entry function                           \n"
    "     jmp *%rax                                      \n");

void co_new(void* func, ...) {
  co_t* co = NULL;

  asm volatile("movq %%rsi, %0\n\t"
               "movq %%rdx, %1\n\t"
               "movq %%rcx, %2\n\t"
               : "=m"(loop.regs[0]), "=m"(loop.regs[1]), "=m"(loop.regs[2])
               :
               : "rsi", "rdx", "rcx");

  if (!STAILQ_EMPTY(&loop.dead)) {
    co = STAILQ_FIRST(&loop.dead);
    STAILQ_REMOVE_HEAD(&loop.dead, next);
  } else {
    co = mi_calloc(CO_STACK_SIZE, 1);
  }

  co->id      = loop.id++;
  co->func    = func;
  co->stack   = NULL;
  co->args[0] = loop.regs[0];
  co->args[1] = loop.regs[1];
  co->args[2] = loop.regs[2];

  STAILQ_INSERT_TAIL(&loop.ready, co, next);
}

void co_yield () {
  if (!setjmp(loop.run->ctx)) {
    longjmp(loop.ctx, (int)loop.run->id);
  }
}

void co_loop() {
  co_t* co = NULL;
  int flag = setjmp(loop.ctx);

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
    __switch(loop.run->stack,
             loop.run->func,
             loop.run->args[0],
             loop.run->args[1],
             loop.run->args[2]);
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
