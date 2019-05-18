#ifndef EXCEPT_H_
#define EXCEPT_H_

#include <setjmp.h>
#include <stdlib.h>

/* Tiny exception-handling library, providing support for only the most basic
 * use case involving no nested exceptions.
 */

#define TRY {                     \
  jmp_buf handler;                \
                                  \
  g_handler = &handler;           \
  if (!setjmp(handler)) {

#define CATCH } else {

#define END }                     \
    g_handler = NULL;             \
  }

#define THROW do {                \
  if (*g_handler) {               \
      longjmp(*g_handler, 1);     \
  } else {                        \
    exit(1);                      \
  }                               \
} while (0)

/* Exception handler; defined in orp.c */
extern jmp_buf *g_handler;

#endif /* EXCEPT_H_ */
