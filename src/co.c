#include "co.h"
#include "lfq.h"

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

#  define inf(fmt, ...)                                                                            \
    fprintf(stderr, "[%s:%d]: " fmt "\n", __FUNCTION__, __LINE__ __VA_OPT__(, ) __VA_ARGS__);
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
  size_t count;            /* 协程个数 */
  reg_t regs[CO_ARGS_NUM]; /* 参数缓存 */

  co_t* run;   /* 当前运行的协程 */
  lfq_t ready; /* 就绪队列 */
  lfq_t dead;  /* 死亡队列 */

  thrd_t fd_thrd;    /* 描述符调度器线程 */
  co_list_t fd_wait; /* 等待队列 */
  lfq_t rfds;        /* read 等待队列 */
  lfq_t wfds;        /* write 等待队列 */
} co_loop_t;

static int co_return  = {};
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
        "     mov $0x1, %esi                                 \n"
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
 * Core
 * */
[[gnu::constructor]]
void co_init() {
  loop.id = 1;

  lfq_init(&loop.ready, 1000);
  lfq_init(&loop.dead, 1000);

  lfq_init(&loop.rfds, 1000);
  lfq_init(&loop.wfds, 1000);

  TAILQ_INIT(&loop.fd_wait);

  extern int co_fd_scheduler(void*);
  thrd_create(&loop.fd_thrd, co_fd_scheduler, nullptr);
}

void co_exit(int code) {
  co_return  = code;
  loop.count = 0;

  co_yield (1);
}

void co_new(void* func, ...) {
  co_t* co = nullptr;

  __asm__ volatile("movq %%rsi, %0\n\t"
                   "movq %%rdx, %1\n\t"
                   "movq %%rcx, %2\n\t"
                   : "=m"(loop.regs[0]), "=m"(loop.regs[1]), "=m"(loop.regs[2])
                   :
                   : "rsi", "rdx", "rcx");

  co = lfq_pop(&loop.dead);
  if (co == nullptr) {
    co = co_alloc(CO_STACK_SIZE);
  }

  co->id      = loop.id++;
  co->func    = func;
  co->stack   = nullptr;
  co->args[0] = loop.regs[0];
  co->args[1] = loop.regs[1];
  co->args[2] = loop.regs[2];

  loop.count++;

  inf("new {%zu - %zu}", co->id, loop.count);

  lfq_put(&loop.ready, co);
}

void co_yield (int flag) {
  if (!setjmp(loop.run->ctx)) {
    longjmp(ctx, flag);
  }
}

int co_loop(void (*start)(int, const char*[]), int argc, const char* argv[]) {
  co_t* co = nullptr;
  int flag = setjmp(ctx);

  /* flag { 0(init), 1(dead), 2(continue), 3(rfd_wait), 4(wfd_wait) } */
  if (flag == 0) {
    co_new(start, argc, argv);
  } else if (flag == 1) {
    loop.count--;
    inf("del {%zu - %zu}", loop.run->id, loop.count);
    lfq_put(&loop.dead, loop.run);
  } else if (flag == 2) {
    lfq_put(&loop.ready, loop.run);
  } else if (flag == 3) {
    lfq_put(&loop.rfds, loop.run);
  } else if (flag == 4) {
    lfq_put(&loop.wfds, loop.run);
  }

  if (loop.count == 0) {
    inf("end");
    goto end;
  }

  do {
    loop.run = lfq_pop(&loop.ready);
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

end:

  while ((co = lfq_pop(&loop.ready))) {
    co_free(co);
  }

  while ((co = lfq_pop(&loop.dead))) {
    co_free(co);
  }

  while ((co = lfq_pop(&loop.rfds))) {
    co_free(co);
  }

  while ((co = lfq_pop(&loop.wfds))) {
    co_free(co);
  }

  while ((co = TAILQ_FIRST(&loop.fd_wait))) {
    TAILQ_REMOVE(&loop.fd_wait, co, next);
    co_free(co);
  }

  lfq_cleanup(&loop.ready);
  lfq_cleanup(&loop.dead);

  lfq_cleanup(&loop.rfds);
  lfq_cleanup(&loop.wfds);

  return co_return;
}

/*
 * Fd scheduler
 * */
int co_fd_scheduler(void* args) {
  co_t* co               = {};
  int maxfd              = {};
  fd_set fds[2]          = {};
  fd_set _fds[2]         = {};
  struct timeval timeout = {.tv_usec = 50000};

  FD_ZERO(&fds[0]);
  FD_ZERO(&fds[0]);

  FD_ZERO(&_fds[0]);
  FD_ZERO(&_fds[0]);

  while (true) {
    /* read */
    while ((co = lfq_pop(&loop.rfds))) {
      FD_SET(co->fd, &fds[0]);
      if (co->fd > maxfd) {
        maxfd = co->fd;
      }

      inf("wait {%zu} R %d", co->id, co->fd);
      TAILQ_INSERT_TAIL(&loop.fd_wait, co, next);
    }

    /* write */
    while ((co = lfq_pop(&loop.wfds))) {
      FD_SET(co->fd, &fds[1]);
      if (co->fd > maxfd) {
        maxfd = co->fd;
      }

      inf("wait {%zu} W %d", co->id, co->fd);
      TAILQ_INSERT_TAIL(&loop.fd_wait, co, next);
    }

    _fds[0] = fds[0];
    _fds[1] = fds[1];

    if (select(maxfd + 1, &_fds[0], &_fds[1], nullptr, &timeout) == 0) {
      continue;
    }

    TAILQ_FOREACH(co, &loop.fd_wait, next) {
      if (FD_ISSET(co->fd, &_fds[0])) {
        inf("select {%zu} R %d", co->id, co->fd);
        TAILQ_REMOVE(&loop.fd_wait, co, next);
        FD_CLR(co->fd, &fds[0]);

        lfq_put(&loop.ready, co);
      }

      if (FD_ISSET(co->fd, &_fds[1])) {
        inf("select {%zu} W %d", co->id, co->fd);
        TAILQ_REMOVE(&loop.fd_wait, co, next);
        FD_CLR(co->fd, &fds[1]);

        lfq_put(&loop.ready, co);
      }
    }
  }

  return 0;
}

int co_accept(int fd, __SOCKADDR_ARG addr, socklen_t* restrict addr_len) {
  loop.run->fd = fd;
  co_yield (3);
  return accept(fd, addr, addr_len);
}

ssize_t co_recv(int fd, void* buf, size_t n, int flags) {
  loop.run->fd = fd;
  co_yield (3);
  return recv(fd, buf, n, flags);
}

ssize_t co_send(int fd, const void* buf, size_t n, int flags) {
  loop.run->fd = fd;
  co_yield (4);
  return send(fd, buf, n, flags);
}
