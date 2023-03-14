#include "co.h"

#include <stdio.h>
#include <time.h>

int count = 0;
void entry(void* _arg) {
  (void)_arg;

  for (int i = 0; i < 1000; i++) {
    count++;
    co_yield ();
  }
}

int main() {

  for (int i = 0; i < 1000; i++) {
    co_new(entry, NULL);
  }

  co_loop();

  printf("count %d\n", count);

  return 0;
}
