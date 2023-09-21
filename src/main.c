#include "co.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void co_switch(co_arg_t arg) {
  for (ssize_t i = 0; i < 10000; i++) {
    co_yield ();
  }
}

void co_create(co_arg_t arg) {
}

int main() {

#if 0

  for (ssize_t i = 0; i < 100; i++) {
    co_new(co_create, NULL);
  }

#else

  for (ssize_t i = 0; i < 100; i++) {
    co_new(co_switch, NULL);
  }

#endif

  co_loop();

  return 0;
}
