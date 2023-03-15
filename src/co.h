#ifndef _CO_H_
#define _CO_H_

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define alignment16(a) ((a) & (~(16 - 1)))
#define STACK_SIZE     4096

typedef void* co_arg_t;
typedef void (*co_func_t)(co_arg_t);

void co_new(co_func_t func, co_arg_t arg);
void co_yield ();
void co_loop();

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !_CO_H_ */
