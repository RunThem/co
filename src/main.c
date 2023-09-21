#include "co.h"

#include <stdint.h>
#include <stdio.h>
#include <u/u.h>

uint64_t cnt = 0;

void co_switch(co_arg_t arg) {
  for (ssize_t i = 0; i < 100; i++) {
    co_yield ();
    cnt++;
  }
}

void co_create(co_arg_t arg) {
}

void co_recreate(co_arg_t arg) {
  for (ssize_t i = 0; i < 100; i++) {
    co_new(co_create, NULL);
  }
}

void co_clean_create(co_arg_t arg) {
  benchmark("clean create", 10000, {
    for (ssize_t i = 0; i < N; i++) {
      co_new(co_create, NULL);
    }
  });
}

int main() {

  benchmark("create co", 10000, {
    if (1) {
      for (ssize_t i = 0; i < N; i++) {
        co_new(co_switch, ((void*)0));
      }
    }
  });

  if (0)
    for (ssize_t i = 0; i < 10000; i++) {
      co_new(co_create, NULL);
    }

  if (0)
    for (ssize_t i = 0; i < 100; i++) {
      co_new(co_recreate, NULL);
    }

  co_new(co_clean_create, NULL);

  benchmark("switch co", cnt, { co_loop(); });

  printf("cnt is %zu\n", cnt);

  return 0;
}
