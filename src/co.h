#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CO_STACK_SIZE
#  define CO_STACK_SIZE (8192)
#endif /* !CO_STACK_SIZE */

extern void co_new(void* func, ...);
extern void co_yield ();
extern void co_loop();

#ifdef __cplusplus
} /* extern "C" */
#endif
