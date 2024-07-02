#include "co.h"

#include <setjmp.h>
#include <stdint.h>
#include <strings.h>
#include <sys/queue.h>
#include <threads.h>
#include <unistd.h>

#define u_map_defs  u_defs(map, (int, co_t*))
#define u_list_defs u_defs(list, co_t)
#include <u/u.h>

#define fd_alloc(fd)                                                                               \
  ({                                                                                               \
    int* _fd = u_talloc(int);                                                                      \
    *_fd     = fd;                                                                                 \
    _fd;                                                                                           \
  })

#define CO_ARGS_NUM   3 /* rdi, rsi, rdx */
#define CO_INVALID_FD -1

#define inf(fmt, ...) u_inf("th(%ld) " fmt, thrd_current() __VA_OPT__(, ) __VA_ARGS__)

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

  co_t* run;            /* 当前运行的协程 */
  u_list_t(co_t) ready; /* 就绪队列 */
  u_list_t(co_t) dead;  /* 死亡队列 */

  thrd_t fd_thrd;    /* 描述符调度器线程 */
  co_list_t fd_wait; /* 等待队列 */
  u_lfq_t rfds[2];   /* read 等待队列 */
  u_lfq_t wfds[2];   /* write 等待队列 */
  u_map_t(int, co_t*) map;
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

  loop.ready   = u_list_new(co_t);
  loop.dead    = u_list_new(co_t);
  loop.rfds[0] = u_lfq_new();
  loop.wfds[0] = u_lfq_new();
  loop.rfds[1] = u_lfq_new();
  loop.wfds[1] = u_lfq_new();
  loop.map     = u_map_new(int, co_t*);

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

  co = u_list_pop(loop.dead);
  if (co == nullptr) {
    co = u_zalloc(CO_STACK_SIZE);
  }

  co->id      = loop.id++;
  co->func    = func;
  co->stack   = nullptr;
  co->args[0] = loop.regs[0];
  co->args[1] = loop.regs[1];
  co->args[2] = loop.regs[2];

  loop.count++;

  inf("new {%zu - %zu}", co->id, loop.count);

  u_list_put(loop.ready, co);
}

void co_yield (int flag) {
  if (!setjmp(loop.run->ctx)) {
    longjmp(ctx, flag);
  }
}

int co_loop(void (*start)(int, const char*[]), int argc, const char* argv[]) {
  co_t* co = nullptr;
  int* fd  = nullptr;
  int flag = setjmp(ctx);

  inf("flag is %d", flag);

  /* flag { 0(init), 1(dead), 2(continue), 3(rfd_wait), 4(wfd_wait) } */
  switch (flag) {
    case 0: co_new(start, argc, argv); break;

    case 1:
      loop.count--;
      u_list_put(loop.dead, loop.run);
      inf("del {%zu - %zu}", loop.run->id, loop.count);
      break;

    case 2: u_list_put(loop.ready, loop.run); break;

    case 3:
      u_map_put(loop.map, loop.run->fd, loop.run);
      u_lfq_put(loop.rfds[0], fd_alloc(loop.run->fd));
      break;

    case 4:
      u_map_put(loop.map, loop.run->fd, loop.run);
      u_lfq_put(loop.wfds[0], fd_alloc(loop.run->fd));
      break;

    default: u_err("flag is %d, error", flag); goto end;
  }

  if (loop.count == 0) {
    goto end;
  }

  inf("ready size is %zu", u_list_len(loop.ready));

  loop.run = nullptr;
  do {
    loop.run = u_list_pop(loop.ready);

    if (loop.run == nullptr) {
      while ((fd = u_lfq_pop(loop.rfds[1]))) {
        co = u_map_pop(loop.map, *fd);
        u_list_put(loop.ready, co);
        inf("poll R %d", *fd);

        u_free(fd);
      }

      while ((fd = u_lfq_pop(loop.wfds[1]))) {
        co = u_map_pop(loop.map, *fd);
        u_list_put(loop.ready, co);
        inf("poll W %d", *fd);

        u_free(fd);
      }
    }
  } while (loop.run == nullptr);

  inf("run %p, %zu", loop.run, loop.run->id);

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
  inf("end");

  return co_return;
}

/*
 * Fd scheduler
 * */
int co_fd_scheduler(void* args) {
  int* _fd               = {};
  int fd                 = {};
  int maxfd              = {};
  fd_set fds[2]          = {};
  fd_set _fds[2]         = {};
  struct timeval timeout = {.tv_usec = 5'0000};

  FD_ZERO(&fds[0]);
  FD_ZERO(&fds[0]);

  FD_ZERO(&_fds[0]);
  FD_ZERO(&_fds[0]);

  while (true) {
    /* read */
    while ((_fd = u_lfq_pop(loop.rfds[0]))) {
      fd = *_fd;
      u_free(_fd);

      FD_SET(fd, &fds[0]);
      maxfd = max(maxfd, fd);
      inf("wait R %d", fd);
    }

    /* write */
    while ((_fd = u_lfq_pop(loop.wfds[0]))) {
      fd = *_fd;
      u_free(_fd);

      FD_SET(fd, &fds[1]);
      maxfd = max(maxfd, fd);
      inf("wait W %d", fd);
    }

    _fds[0] = fds[0];
    _fds[1] = fds[1];

    if (select(maxfd + 1, &_fds[0], &_fds[1], nullptr, &timeout) == 0) {
      continue;
    }

    u_each (i, maxfd + 1) {
      if (FD_ISSET(i, &_fds[0])) {
        FD_CLR(i, &fds[0]);

        u_lfq_put(loop.rfds[1], fd_alloc(i));
      }

      if (FD_ISSET(i, &_fds[1])) {
        FD_CLR(i, &fds[1]);

        u_lfq_put(loop.wfds[1], fd_alloc(i));
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
