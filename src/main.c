#include "co.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

struct st {
  int a;
  int b;
};

void co_fun1(int a, struct st b, char* c) {
  printf("args[0] is %d\n", a);
  printf("args[1].a is %d\n", b.a);
  printf("args[1].b is %d\n", b.b);
  printf("args[3] is '%s'\n", c);
}

size_t cnt = 0;
void co_fun2() {
  for (int i = 0; i < 100; i++) {
    co_yield ();
    cnt++;
  }
}

int main() {
  struct st s = {.a = 1234567, .b = 7654321};

  co_new(co_fun1, 12, s, "sdifj");

  for (int i = 0; i < 100; i++) {
    co_new(co_fun2);
  }

  co_loop();

  printf("cnt is %lu\n", cnt);

  return 0;
}
