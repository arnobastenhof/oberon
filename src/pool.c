#include "pool.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "except.h"

/* As an implementation note, much of the current file is based on Fraser and
 * Hanson's use of arenas in LCC, as described in their book "A retargetable
 * C compiler."
 */

/* Arenas are based on object lifetimes, not -sizes. In particular, the same
 * memory block may serve allocations for objects of many different types. To
 * ensure their proper alignment, we simply always adhere to the requirements
 * for the most restrictive type. The latter may differ, however, between
 * platforms, and there are two portable methods at our disposal for
 * determining its identity at compile-time.
 */

#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 201112L

/* The C11 standard defines max_align_t in limits.h as a synonym for the scalar
 * type with the strictest alignment requirements.
 */
#include <limits.h>

#else

/* Else, we can provide our own definition using a union. (See, e.g., K&R and
 * chapter 2 of Fraser and Hanson's "A retargetable C compiler".) */

typedef union {
  long            l;
  double          d;
  long long       ll;
  long double     ld;
  void *          p;
} max_align_t;

#endif /* __STDC_VERSION__ >= 201112L */
#endif /* __STDC_VERSION__ */

/* An arena is a linked list of memory blocks, where each block is headed by
 * a link (rlink), a pointer delimiting the block size (limit) and another
 * pointer identifying the next allocable byte within the block (avail).
 * Objects allocated within the same arena share the their lifetime and can be
 * deallocated all at once, thus foregoing the need for the often error-prone
 * practice of freeing them individually. In a compiler, lifetimes are
 * typically bound to the parsing of certain language constructs, and so in
 * Fraser and Hanson's LCC, for instance, a dedicated arena was used that was
 * deallocated after every method definition, beside another one for storing
 * globals. Unlike in C, however, procedures can nest in Oberon, and so we
 * instead allow for a variable number of arenas that can be stacked, mirroring
 * the language situation. To this end, we added another link (dlink) to the
 * header, pointing from child to parent. In practice we set it only for the
 * first block in every arena, keeping it NULL otherwise. This wastefulness
 * could be remedied at the cost of a slight increase in complexity, which we
 * didn't bother with.
 *
 *      dlink
 *  +----------> (...)
 *  |
 *  |
 *  |  +--------+  rlink   +------+  rlink            rlink
 *  |  |   -----+--------->|   ---+---------> (...) ---------> NULL
 *  |  +--------+          +------+
 *  +--+----    |          | NULL |
 *     +--------+          +------+
 *     | limit  |          |  .   |
 *     +--------+          |  .   |
 *     | avail  |          |  .   |
 *     +--------+          |  .   |
 *     |        |          |  .   |
 *     | *avail |          |      |
 *     |        |          |      |
 *     |        |          |      |
 *     +--------+          +------+
 *       *limit
 */

typedef struct node_s node_t;

struct node_s {
  node_t *      rlink;        /* Next block within this arena */
  node_t *      dlink;        /* Chains the 1st block of every arena together,
                                 pointing from child to parent (else NULL) */
  uint8_t *     limit;        /* One byte past the last allocable one */
  uint8_t *     avail;        /* Next allocable byte */
};

/* The following type definition is used for setting avail upon initialization
 * of a memory block, ensuring the first allocated byte is properly aligned.
 */
typedef union {
  node_t        n;
  max_align_t   a;
} header_t;

/* In contrast to LCC, blocks have constant size. They are allocated from a
 * dedicated memory pool and deallocated in constant time (along with the
 * objects they contain) by placing them on a free list.
 */

#define ALIGN_SZ        (sizeof(max_align_t))
#define ROUND(n)        ((((n) + ALIGN_SZ - 1) / ALIGN_SZ) * ALIGN_SZ)
#define BLOCK_SZ        ROUND(sizeof(header_t) + 512)
#define POOL_SZ         (16 * BLOCK_SZ)

static uint8_t          g_pool[POOL_SZ];            /* Block pool */
static uint8_t * const  g_limit = g_pool + POOL_SZ; /* End of block pool */
static uint8_t *        g_avail = g_pool;           /* Next available block */
static node_t *         g_free = NULL;              /* List of free blocks */

/* At any given time, we keep track of the first- and last blocks of the
 * arena on top of the stack, the former designated by g_top, and the latter,
 * being used for allocation, by g_arena.
 *
 *   NULL
 *     ^
 *     | dlink
 *   (...)
 *     ^
 *     | dlink
 *   +---+
 *   |   |-------> (...) -------> NULL
 *   +---+ rlink          rlink
 *     ^
 *     | dlink
 *   +---+                        +---+
 *   |   |-------> (...) -------> |   |-------> NULL
 *   +---+ rlink          rlink   +---+ rlink
 *     ^                            ^
 *     |                            |
 *   g_top                       g_arena
 */
static node_t *         NewBlock(void);

static node_t *         g_top = NULL;          /* Stack top */
static node_t *         g_arena = NULL;        /* Current block */

/* Allocates an object of the requested size from the arena on top of the
 * stack. */
void *
Pool_Alloc(size_t sz)
{
  uint8_t *     ptr;
  node_t *      block;

  assert(g_arena);

  /* Round up size to a multiple of sizeof(max_align_t) */
  sz = ROUND(sz);

  /* If needed, first get a new block */
  if (g_arena->avail + sz >= g_arena->limit) {
    block = NewBlock();
    g_arena->rlink = block;
    g_arena = block;
  }

  /* Allocate object in current block */
  ptr = g_arena->avail;
  g_arena->avail += sz;
  return (void *)ptr;
}

/* Pushes a new arena on top of the stack. */
void
Pool_Push(void)
{
  node_t *      block;

  block = NewBlock();
  block->dlink = g_top;
  g_top = block;
  g_arena = block;
}

/* Pops an arena from the stack and places it on the free list, effectively
 * deallocating the objects it contains all at once.
 */
void
Pool_Pop(void)
{
  assert(g_top && g_arena);

  g_arena->rlink = g_free;
  g_free = g_top;
  g_top = g_top->dlink;

  /* Reset current block to the last in the chain. As arenas will typically
   * consist of relatively few blocks, this should be cheap.
   */
  g_arena = g_top;
  while (g_arena && g_arena->rlink) {
    g_arena = g_arena->rlink;
  }
}

static node_t *
NewBlock(void)
{
  node_t *      block;

  /* Find new block */
  if ((block = g_free)) {                      /* First try the free list */
    g_free = block->rlink;
  } else if (g_avail + BLOCK_SZ < g_limit) {   /* Else, allocate from pool */
    block = (node_t *)g_avail;
    g_avail += BLOCK_SZ;
  } else {                                     /* Else, we ran out of memory */
    fprintf(stderr, "Out of memory\n");
    THROW;
  }

  /* Initialization */
  block->limit = ((uint8_t *)block) + BLOCK_SZ;
  block->avail = ((uint8_t *)block) + sizeof(header_t);

  return block;
}
