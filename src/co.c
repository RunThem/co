#include "co.h"

#include <setjmp.h>
#include <stdint.h>
#include <strings.h>
#include <sys/queue.h>
#include <threads.h>
#include <unistd.h>

#ifdef CO_USE_MIMALLOC
#  include <mimalloc.h>
#  define co_alloc(size) mi_malloc(size)
#  define co_free(ptr)   mi_free(ptr)
#else /* !CO_USE_MIMALLOC */
#  include <stdlib.h>
#  define co_alloc(size) malloc(size)
#  define co_free(ptr)   free(ptr)
#endif /* !CO_USE_MIMALLOC */

#undef NDEBUG
#ifdef NDEBUG
#  define inf(fmt, ...)
#else /* !NDEBUG */
#  include <stdio.h>

#  define inf(fmt, ...) fprintf(stderr, fmt "\n" __VA_OPT__(, ) __VA_ARGS__);
#endif /* !NDEBUG */

#define CO_ARGS_NUM 3 /* rdi, rsi, rdx */

#define CO_INVALID_FD -1

/*
 * Typedef
 * */
typedef uint64_t reg_t;

typedef struct co_t {
  size_t id; /* 协程 id */

  void* func;              /* 协程执行入口 */
  reg_t args[CO_ARGS_NUM]; /* 协程参数 */

  jmp_buf ctx;    /* 上下文 */
  uint8_t* stack; /* 栈帧 */

  int fd; /* 监听的描述符 */

  TAILQ_ENTRY(co_t) next;
} co_t;

typedef TAILQ_HEAD(co_list_t, co_t) co_list_t, *co_list_ref_t;

typedef struct {
  size_t id;               /* 协程 id 分配器 */
  reg_t regs[CO_ARGS_NUM]; /* 参数缓存 */

  co_t* run;       /* 当前运行的协程 */
  co_list_t ready; /* 就绪队列 */
  co_list_t wait;  /* 等待队列 */
  co_list_t dead;  /* 死亡队列 */

  mtx_t mtx; /* 全局大锁 */

  thrd_t fd_thrd; /* 描述符调度器线程 */
  int maxfd;      /* 最大的描述符 */
  fd_set rfds;
  fd_set wfds;
} co_loop_t;

static jmp_buf ctx    = {};
static co_loop_t loop = {};

/*
 * Context switch
 * */

/*
 * @param stack         the stack               (rdi)
 * @param func          the func                (rsi)
 * @param rdi           the func args           (rdx)
 * @param rsi           the func args           (rcx)
 * @param rdx           the func args           (r8)
 * */
int __co_switch(void* stack, void* func, reg_t rdi, reg_t rsi, reg_t rdx);

__asm__(".text                                               \n"
        ".align 8                                            \n"
        ".globl  __co_exit                                   \n"
        ".type   __co_exit %function                         \n"
        ".hidden __co_exit                                   \n"
        "__co_exit:                                          \n"
        "     lea ctx(%rip), %rdi                            \n"
        "     mov $0xffffffff, %esi                          \n"
        "                                                    \n"
        "     call longjmp                                   \n"
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

/*
 * Fd scheduler
 * */
static int co_fd_scheduler(void* args) {
  int cnt                = {};
  int i                  = {};
  co_t* co               = {};
  fd_set rfds            = {};
  fd_set wfds            = {};
  struct timeval timeout = {.tv_sec = 1};

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);

  while (true) {
    rfds = loop.rfds;
    wfds = loop.wfds;

    cnt = select(loop.maxfd + 1, &rfds, &wfds, nullptr, &timeout);
    if (cnt == 0) {
      continue;
    }

    for (i = 0; i < loop.maxfd; i++) {
      TAILQ_FOREACH(co, &loop.wait, next) {
        if (co->fd == CO_INVALID_FD) {
          continue;
        }

        if (FD_ISSET(co->fd, &rfds)) {
          inf("select rfds is %d", co->fd);
          TAILQ_REMOVE(&loop.wait, co, next);
          TAILQ_INSERT_TAIL(&loop.ready, co, next);
        }

        if (FD_ISSET(co->fd, &wfds)) {
          inf("select wfds is %d", co->fd);
          TAILQ_REMOVE(&loop.wait, co, next);
          TAILQ_INSERT_TAIL(&loop.ready, co, next);
        }
      }
    }
  }

  return 0;
}

int co_accept(int fd, __SOCKADDR_ARG addr, socklen_t* restrict addr_len) {
  TAILQ_REMOVE(&loop.ready, loop.run, next);
  TAILQ_INSERT_TAIL(&loop.wait, loop.run, next);

  inf("accept fd %d", fd);

  mtx_lock(&loop.mtx);
  FD_SET(fd, &loop.rfds);
  loop.maxfd   = (fd > loop.maxfd) ? fd : loop.maxfd;
  loop.run->fd = fd;
  mtx_unlock(&loop.mtx);

  co_yield ();

  mtx_lock(&loop.mtx);
  FD_CLR(fd, &loop.rfds);
  loop.run->fd = CO_INVALID_FD;
  mtx_unlock(&loop.mtx);

  return accept(fd, addr, addr_len);
}

ssize_t co_recv(int fd, void* buf, size_t n, int flags) {
  TAILQ_REMOVE(&loop.ready, loop.run, next);
  TAILQ_INSERT_TAIL(&loop.wait, loop.run, next);

  inf("recv fd %d", fd);

  mtx_lock(&loop.mtx);
  FD_SET(fd, &loop.rfds);
  loop.maxfd   = (fd > loop.maxfd) ? fd : loop.maxfd;
  loop.run->fd = fd;
  mtx_unlock(&loop.mtx);

  co_yield ();

  mtx_lock(&loop.mtx);
  FD_CLR(fd, &loop.rfds);
  loop.run->fd = CO_INVALID_FD;
  mtx_unlock(&loop.mtx);

  return recv(fd, buf, n, flags);
}

ssize_t co_send(int fd, const void* buf, size_t n, int flags) {
  TAILQ_REMOVE(&loop.ready, loop.run, next);
  TAILQ_INSERT_TAIL(&loop.wait, loop.run, next);

  inf("send fd %d", fd);

  mtx_lock(&loop.mtx);
  FD_SET(fd, &loop.wfds);
  loop.maxfd   = (fd > loop.maxfd) ? fd : loop.maxfd;
  loop.run->fd = fd;
  mtx_unlock(&loop.mtx);

  co_yield ();

  mtx_lock(&loop.mtx);
  FD_CLR(fd, &loop.wfds);
  loop.run->fd = CO_INVALID_FD;
  mtx_unlock(&loop.mtx);

  return send(fd, buf, n, flags);
}

/*
 * Core
 * */
void co_init() {
  loop.id = 1;

  TAILQ_INIT(&loop.ready);
  TAILQ_INIT(&loop.wait);
  TAILQ_INIT(&loop.dead);

  thrd_create(&loop.fd_thrd, co_fd_scheduler, nullptr);

  mtx_init(&loop.mtx, mtx_plain);
}

void co_new(void* func, ...) {
  co_t* co = NULL;

  __asm__ volatile("movq %%rsi, %0\n\t"
                   "movq %%rdx, %1\n\t"
                   "movq %%rcx, %2\n\t"
                   : "=m"(loop.regs[0]), "=m"(loop.regs[1]), "=m"(loop.regs[2])
                   :
                   : "rsi", "rdx", "rcx");

  if (!TAILQ_EMPTY(&loop.dead)) {
    co = TAILQ_FIRST(&loop.dead);
    TAILQ_REMOVE(&loop.dead, co, next);
  } else {
    co = co_alloc(CO_STACK_SIZE);
  }

  co->id      = loop.id++;
  co->func    = func;
  co->stack   = NULL;
  co->args[0] = loop.regs[0];
  co->args[1] = loop.regs[1];
  co->args[2] = loop.regs[2];

  TAILQ_INSERT_TAIL(&loop.ready, co, next);
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
    TAILQ_REMOVE(&loop.ready, loop.run, next);
    TAILQ_INSERT_HEAD(&loop.dead, loop.run, next);
  }

  if (TAILQ_EMPTY(&loop.ready) && TAILQ_EMPTY(&loop.wait)) {
    goto end;
  }

  do {
    loop.run = TAILQ_FIRST(&loop.ready);

    // sleep(1);
  } while (loop.run == nullptr);

  if (!loop.run->stack) {
    loop.run->stack = (void*)loop.run + CO_STACK_SIZE - 16;
    __co_switch(loop.run->stack,
                loop.run->func,
                loop.run->args[0],
                loop.run->args[1],
                loop.run->args[2]);
  } else {
    longjmp(loop.run->ctx, 0);
  }

  inf("end");

end:
  while ((co = TAILQ_FIRST(&loop.dead))) {
    TAILQ_REMOVE(&loop.dead, co, next);
    co_free(co);
  }
}
