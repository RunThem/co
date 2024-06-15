#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>

#ifndef CO_STACK_SIZE
#  define CO_STACK_SIZE (8192)
#endif /* !CO_STACK_SIZE */

extern void co_init();
extern void co_new(void* func, ...);
extern void co_yield (int flag);
extern void co_loop();

extern int co_accept(int fd, __SOCKADDR_ARG addr, socklen_t* restrict addr_len);
extern ssize_t co_recv(int fd, void* buf, size_t n, int flags);
extern ssize_t co_send(int fd, const void* buf, size_t n, int flags);

#ifdef __cplusplus
} /* extern "C" */
#endif
