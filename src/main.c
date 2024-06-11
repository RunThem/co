#include "co.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

struct st {
  int a;
  int b;
};

void fun(va_list args) {
  printf("hello\n");

#define co_args(ap, type, var) type var = va_arg(ap, type)

  // int a       = va_arg(args, int);
  // struct st b = va_arg(args, struct st);
  // double c    = va_arg(args, double);

  co_args(args, int, a);
  co_args(args, struct st, b);
  co_args(args, double, c);

  struct {
    int _1;
  } ap;

  printf("%d\n", va_arg(args, int));
  printf("%d\n", va_arg(args, struct st).a);
  printf("%f\n", va_arg(args, double));
}

void fun1(...) {
  uint64_t regs[4] = {};

  asm volatile("movq %%rdi, %0\n\t"
               "movq %%rsi, %1\n\t"
               "movq %%rdx, %2\n\t"
               "movq %%rcx, %3\n\t"
               : "=m"(regs[0]), "=m"(regs[1]), "=m"(regs[2]), "=m"(regs[3])
               :
               : "rdi", "rsi", "rdx", "rcx");

  printf("%lu, %lu, %lu, %lu\n", regs[0], regs[1], regs[2], regs[3]);

  // va_list ap, ap2;
  // va_start(ap);
  //
  // va_copy(ap2, ap);
  //
  // fun(ap2);
  //
  // va_end(ap);
}

void co_create(int a, void* b, char* c) {
  printf("%d\n", a);
  printf("%p\n", b);
  printf("%s\n", c);

  // for (int i = 0; i < 100; i++)
  //   co_yield ();
}

int main() {
  // fun1(1, 2, 3);

  co_new(co_create, 12, co_create, "sdifj");

  co_loop();
  return 0;
}
