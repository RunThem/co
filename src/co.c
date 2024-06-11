#include "co.h"

#include <setjmp.h>
#include <stdint.h>
#include <sys/queue.h>

#ifdef CO_USE_MIMALLOC
#  include <mimalloc.h>
#  define co_alloc(size) mi_malloc(size)
#  define co_free(ptr)   mi_free(ptr)
#else /* !CO_USE_MIMALLOC */
#  include <stdlib.h>
#  define co_alloc(size) malloc(size)
#  define co_free(ptr)   free(ptr)
#endif /* !CO_USE_MIMALLOC */

#ifdef NDEBUG
#  define infln(fmt, ...)
#else /* !NDEBUG */
#  include <stdio.h>

#  define infln(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__);
#endif /* !NDEBUG */

#define CO_ARGS_NUM 3 /* rdi, rsi, rdx */

typedef uint64_t reg_t;

typedef struct co_t {
  size_t id; /* 线程 id */

  void* func;              /* 线程执行入口 */
  reg_t args[CO_ARGS_NUM]; /* 线程参数 */

  jmp_buf ctx;    /* 上下文 */
  uint8_t* stack; /* 栈帧 */

  STAILQ_ENTRY(co_t) next;
} co_t;

typedef STAILQ_HEAD(co_list_t, co_t) co_list_t, *co_list_ref_t;

typedef struct {
  size_t id;               /* 线程 id 分配器 */
  reg_t regs[CO_ARGS_NUM]; /* 参数缓存 */

  co_t* run;       /* 当前运行的线程 */
  co_list_t ready; /* 就绪队列 */
  co_list_t dead;  /* 死亡队列 */
} co_loop_t;

static jmp_buf ctx;

static co_loop_t loop = {
    .id    = 1,
    .ready = {.stqh_first = NULL, .stqh_last = &loop.ready.stqh_first},
    .dead  = {.stqh_first = NULL, .stqh_last = &loop.dead.stqh_first },
};

/*
 * @param stack         the stack               (rdi)
 * @param func          the func                (rsi)
 * @param rdi           the func args           (rdx)
 * @param rsi           the func args           (rcx)
 * @param rdx           the func args           (r8)
 * */
int __co_switch(void* stack, void* func, reg_t rdi, reg_t rsi, reg_t rdx);

asm(".text                                               \n"
    ".align 8                                            \n"
    ".globl  __co_exit                                   \n"
    ".type   __co_exit %function                         \n"
    ".hidden __co_exit                                   \n"
    "__co_exit:                                          \n"
    "     lea ctx(%rip), %rdi                            \n"
    "     mov $0xffffffff, %esi                          \n"
    "                                                    \n"
    "     call longjmp@plt                               \n"
    "                                                    \n"
    "                                                    \n"
    "                                                    \n"
    ".text                                               \n"
    ".align 8                                            \n"
    ".globl  __co_switch                                 \n"
    ".type   __co_switch %function                       \n"
    ".hidden __co_switch                                 \n"
    "__co_switch:                                        \n"
    "     # 16-align for the stack top address           \n"
    "     movabs $-16, %rax                              \n"
    "     andq %rax, %rdi                                \n"
    "                                                    \n"
    "     # switch to the new stack                      \n"
    "     movq %rdi, %rsp                                \n"
    "                                                    \n"
    "     # save exit function                           \n"
    "     leaq __co_exit(%rip), %rax                     \n"
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
    co = co_alloc(CO_STACK_SIZE);
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
    longjmp(ctx, (int)loop.run->id);
  }
}

void co_loop() {
  co_t* co = NULL;
  int flag = setjmp(ctx);

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
    __co_switch(loop.run->stack,
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
    co_free(co);
  }

  while ((co = STAILQ_FIRST(&loop.dead))) {
    STAILQ_REMOVE_HEAD(&loop.dead, next);
    co_free(co);
  }
}
