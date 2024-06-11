#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CO_STACK_SIZE
#  define CO_STACK_SIZE (8192) /* 8 K */
#endif

#ifndef CO_NUMS
#  define CO_NUMS 256
#endif

typedef void* co_args_t;
typedef void (*co_func_t)(co_args_t);

extern void co_new(co_func_t func, co_args_t arg);
extern void co_yield ();
extern void co_loop();

#ifdef __cplusplus
} /* extern "C" */
#endif
