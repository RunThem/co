#include "co.h"

#include <libsock.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define inf(fmt, ...) fprintf(stderr, fmt "\n" __VA_OPT__(, ) __VA_ARGS__)

void echo(int fd) {
  char buf[1024] = {};
  ssize_t size   = {};

  while (true) {
    size = co_recv(fd, buf, sizeof(buf), 0);
    if (size == 0) {
      close(fd);
      break;
    }

    send(fd, buf, size, 0);
  }
}

void _main() {
  int cfd                 = {};
  struct sockaddr_in addr = {};
  socklen_t addr_len      = {};
  sock_conf_t conf        = {
             .type     = SOCK_TYPE_INET4_TCP,
             .host     = "0.0.0.0",
             .port     = 8080,
             .nonblock = true,
             .listen   = 5,
  };

  sock_open(&conf);
  inf("listen %s:%d, fd is %d", conf.host, conf.port, conf.fd);

  if (conf.fd == -1) {
    return;
  }

  while (true) {
    cfd = co_accept(conf.fd, (struct sockaddr*)&addr, &addr_len);
    inf("client is %d", cfd);

    if (conf.fd == -1) {
      return;
    }

    co_new(echo, cfd);
  }

  sock_close(&conf);
}

void fun1() {
  for (size_t i = 0; i < 100; i++) {
    printf("1\n");
    co_yield (3);
  }
}

void fun2() {
  for (size_t i = 0; i < 100; i++) {
    printf("2\n");
    co_yield (3);
  }
}

int main() {
  co_init();

  co_new(_main);

  // co_new(fun1);
  // co_new(fun2);

  co_loop();

  return 0;
}
