#ifndef POOL_H_
#define POOL_H_

#include <stddef.h>

/* The current interface is a variation of Fraser and Hanson's arenas,
 * faciliating constant time allocation of individual objects, as well as
 * fast deallocation of entire groups of objects based on their lifetimes.
 * In contrast to LCC, we allow for a variable number of arenas that are
 * organized into a stack, mirroring the nesting of procedures in the source
 * language.
 */

extern void *  Pool_Alloc(size_t);  /* Allocate from arena at top of stack */
extern void    Pool_Push(void);     /* Push an arena */
extern void    Pool_Pop(void);      /* Pop (deallocate) an arena */

#endif /* POOL_H_ */
