#include "co.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// int count = 0;

void entry(co_arg_t _arg) {
  char* arg = (char*)_arg;

#if 0
  for (int i = 0; i < 2; i++) {
    // count++;
    // printf("%s\n", arg);
    // co_yield ();
  }
#endif

  // sleep(1);

  // printf("%s\n", arg);
  // co_yield ();

  // printf("[%s]\n", arg);
}

int main() {
  char buf[10] = {0};

  for (int i = 0; i < 100; i++) {
    sprintf(buf, "%d", i);

    co_new(entry, (char*)strdup(buf));
  }

  co_loop();

  // printf("count %d\n", count);

  return 0;
}
