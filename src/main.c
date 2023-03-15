#include "co.h"

#include <stdio.h>
#include <time.h>

// int count = 0;

void entry(co_arg_t _arg) {
  char* arg = (char*)_arg;

  for (int i = 0; i < 2; i++) {
    // count++;
    printf("%s\n", arg);
    co_yield ();
  }

  // printf("[%s]\n", arg);
}

int main() {

  for (int i = 0; i < 59; i++) {
    co_new(entry, (void*)"1");
  }

  co_loop();

  // printf("count %d\n", count);

  return 0;
}
