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

extern void co_new(void* func, ...);
extern void co_yield ();
extern void co_loop();

#ifdef __cplusplus
} /* extern "C" */
#endif
