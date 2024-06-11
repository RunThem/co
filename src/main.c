#include "co.h"

#include <stdarg.h>
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
  va_list ap, ap2;
  va_start(ap);

  va_copy(ap2, ap);

  fun(ap2);

  va_end(ap);
}

void co_create(co_args_t args) {
}

int main() {

  fun1(1, (struct st){9, 8}, 3.3);

  // co_new(co_create, NULL);
  //
  // co_loop();
  return 0;
}
