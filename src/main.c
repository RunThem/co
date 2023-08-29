#include "co.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int count = 0;

void co_1(co_arg_t _arg) {
  count++;
  co_yield ();
}

void co_2(co_arg_t _arg) {
  for (int i = 0; i < 100; i++) {
    co_new(co_1, NULL);

    co_yield ();
  }

  co_yield ();
}

int main() {
  for (int i = 0; i < 100; i++) {
    co_new(co_2, NULL);
  }

  co_loop();

  printf("%d", count);

  return 0;
}
