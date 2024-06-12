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

    co_send(fd, buf, size, 0);
  }
}

void _main() {
  int cfd                 = {};
  struct sockaddr_in addr = {};
  socklen_t addr_len      = {};
  sock_conf_t conf        = {
             .type     = SOCK_TYPE_INET4_TCP,
             .host     = "localhost",
             .port     = 8080,
             .nonblock = true,
             .listen   = 5,
  };

  sock_open(&conf);
  inf("listen %s:%d, fd is %d", conf.host, conf.port, conf.fd);

  while (true) {
    cfd = co_accept(conf.fd, (struct sockaddr*)&addr, &addr_len);
    inf("client is %d", cfd);

    co_new(echo, cfd);
  }

  close(conf.fd);
}

int main() {
  co_init();

  co_new(_main);

  co_loop();

  return 0;
}
