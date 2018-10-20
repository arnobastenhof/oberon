/* Ported from ORG.Mod.txt, part of the Oberon07 reference implementation. */

#include "org.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "risc.h"

enum {
  /* Upper bounds on object file areas */
  kMaxCode = 256,                   /* Max no. of instructions */
  kMaxStrx = 512,                   /* Max size of string pool */

  /* Modifier bits used for instruction assembly*/
  kModV = 0x1000,                   /* Controls sign extension of constants */
  kModU = 0x2000,                   /* Miscellaneous, depending on the form of
                                     * the instruction and the opcode */
};

/* Tables used for generating assembly */

/* Opcode mnemonics */
static const char * const g_mnemo[] = {
  "MOV", "LSL", "ASR", "ROR", "AND", "ANN",   /* Register instructions */
  "IOR", "XOR", "ADD", "SUB", "MUL", "DIV",   /* Register instructions cont. */
  "LDW", "LDB", "STW", "STB",                 /* Memory instructions */
  "BR",  "BLR", "BC",  "BL"                   /* Branch instructions */
};

/* Conditions for use with branch instructions */
static const char * const g_cond[] = {
  "MI", "EQ", "CS", "VS", "LS", "LT", "LE", "T",
  "PL", "NE", "CC", "VC", "HI", "GE", "GT", "F"
};

/* General-purpose register names */
/* Register 12 (MT) was used by Wirth to point to a module table. The current
 * implementation does not yet support separate compilation, but we have kept
 * R12 reserved regardless.
 */
static const char * const g_regs[] = {
  "R0",  "R1", "R2",  "R3",  "R4",  "R5",  "R6",  "R7",
  "R8",  "R9", "R10", "R11", "MT",  "SB",  "SP",  "LNK"
};

/*
 * Indices are symbols (kEql .. kGeq, minus kEql), values are conditions 
 * (risc.h) to be used with Put3 (below).
 */
static const int g_relmap[] = {
  kCondEQ,                          /* equals */
  kCondNE,                          /* not equals */
  kCondLT,                          /* less than */
  kCondLE,                          /* less or equal */
  kCondGT,                          /* greater than */
  kCondGE                           /* greater or equal */
};

/* Prototypes */
static void       Put0(const int, const int, const int, const int);
static void       Put1(int, const int, const int, const int32_t);
static void       Put1a(int, const int, const int, const int32_t);
static void       Put2(const int, int, int, int);
static void       Put3(const int, int, int);
static void       IncR(void);
static void       SetCC(item_t * const, const int);
static void       Trap(const int, const int);
static inline int Negated(const int);
static void       Fix(const int, const int);
static void       FixLinkWith(int, const int);
static int        Merged(const int, int);
static void       NilCheck(void);
static void       Load(item_t * const);
static void       LoadAdr(item_t * const);
static void       LoadCond(item_t * const);
static void       LoadStringAdr(item_t * const);
static int        Log2(int, int *);
static void       Store(item_t * const, const int);
static void       SaveRegs(const int);
static void       RestoreRegs(const int);

/* Private state */
static int        g_pc;             /* Program counter */
static int        g_varsize;        /* Size of local variable declarations */
static int        g_rh;             /* Next free reg / stack top */
static int        g_frame;          /* Frame offset (Save- and RestoreRegs) */
static char       g_pool[kMaxStrx]; /* String pool */
static int        g_strx;           /* Pointer into g_str */

void
ORG_CheckRegs(void)
{
  if (g_rh != 0) {
    ORS_Mark("Reg Stack");
    g_rh = 0;
  }
  if (g_pc >= kMaxCode - 3) {
    ORS_Mark("program too long");
  }
  if (g_frame != 0) {
    ORS_Mark("frame error");
    g_frame = 0;
  }
}

/* Sets the offset of the instruction in format F3 at address 'at' to its
 * distance relative to the current value of PC.
 */
void
ORG_FixOne(const int at)
{
  Fix(at, g_pc-at-1);
}

/* Similar to FixOne, but operating on a list of branch instructions that are
 * linked together through their offset fields (terminating in a 0 value).
 */
void
ORG_FixLink(int l0)
{
  int     l1;

  while (l0 != 0) {
    /* Store link to next instruction */
    l1 = g_mem[l0] & 0x3FFFF;

    /* Fix offset (overwrites link) */
    Fix(l0, g_pc-l0-1);

    /* Move on to next instruction */
    l0 = l1;
  }
}

void
ORG_MakeConst(item_t * const x, type_t * const type, const int32_t val)
{
  assert(x);
  assert(type);

  x->mode = kModeImmediate;
  x->type = type;
  x->a = val;
  x->b = 0;
}

void
ORG_MakeString(item_t * const x, int len)
{
  int     i;

  assert(x);
  assert(len > 0);

  x->mode = kModeImmediate;
  x->type = &g_str_type;
  x->a = g_strx;
  x->b = len;

  if (g_strx + len + 4 >= kMaxStrx) {
    ORS_Mark("too many strings");
  } else {
    /* Copy characters into string pool */
    for (i = 0; len > 0; --len) {
      g_pool[g_strx++] = g_str[i++];
    }

    /* Pad with 0's */
    while (g_strx % 4) {
      g_pool[g_strx++] = '\0';
    }
  }
}

void
ORG_MakeItem(item_t * const x, object_t * const y, const int curlev)
{
  assert(x);
  assert(y);

  x->type = y->type;
  x->a = y->val;
  x->rdo = y->rdo;

  switch (y->tag) {
  case kObjConst:
    x->mode = kModeImmediate;
    x->r = y->level;
    if (y->type->tag == kTypeString) {
      /* Set length */
      x->b = y->level;
    }
    break;
  case kObjVar:
    x->mode = kModeDirect;

    /* A level of > 0 indicates a local variable, meaning SP is to be used as
     * a base address. If instead the level is <= 0, the variable is global to
     * the current module (0) or to an imported module (< 0) and the SB
     * register is to be used instead. See also Load, case kModeDirect.
     */
    x->r = y->level;
    break;
  case kObjType:
    x->mode = kModeType;
    x->a = y->type->u.len;
    break;
  case kObjParam:
    x->mode = kModeParam;
    x->b = 0;
    break;
  default:
    assert(0);
  }

  /* See Wirth's "On Programming Styles": with Revised Oberon access to
   * variables local to a parent procedure became disallowed. In other words
   * only strictly local- and strictly global variables are accessible.
   */
  if ((y->level > 0) && (y->level != curlev) && (y->tag != kObjConst)) {
    ORS_Mark("level error, not accessible");
  }
}

/* Selectors */

/* x := x.y */
void
ORG_Field(item_t * const x, object_t * const y)
{
  assert(x && x->type && x->type->tag == kTypeRecord);
  assert(y && y->tag == kObjField);

  switch(x->mode) {
  case kModeDirect:
    if (x->r >= 0) {
      /* Local variable */
      /* Add offset to variable address */
      x->a += y->val;
    } else {
      /* Global variable */
      /* Store base address in a register and the offset in x->a */
      LoadAdr(x);
      x->mode = kModeRegI;
      x->a = y->val;
    }
    break;
  case kModeRegI:
    x->a += y->val;
    break;
  case kModeParam:
    x->b += y->val;
    break;
  default:
    assert(0);
  }
}

/* x := x[y] */
void
ORG_Index(item_t * const x, item_t * const y)
{
  int           lim;          /* Array size (used for bounds checking) */
  int           scale;        /* Amount by which to scale the index */

  assert(x && x->type && x->type->tag == kTypeArray);
  assert(x->mode != kModeImmediate && x->mode != kModeReg);
  assert(y && y->type && y->type->tag == kTypeInt);

  lim = x->type->u.len;
  scale = x->type->base->size;

  if (y->mode == kModeImmediate && lim >= 0) {
    if (y->a < 0 || y->a >= lim) {
      ORS_Mark("bad index");
    }
    if (x->mode == kModeDirect || x->mode == kModeRegI) {
      x->a += scale * y->a;
    } else if (x->mode == kModeParam) {
      x->b += scale * y->a;
    }
  } else {
    Load(y);

    /* Check array bounds (at runtime) */
    Trap(kCondMI, kTrapIndexOutOfBounds);
    if (lim >= 0) {
      Put1a(kOpCmp, g_rh, y->r, lim);                   /* CMP R.y and lim */
    } else {
      if (x->mode == kModeDirect || x->mode == kModeParam) {
        Put2(kOpLdr, g_rh, kRegSP, x->a + 4 + g_frame); /* RH := Mem[SP+x+4] */
        Put0(kOpCmp, g_rh, y->r, g_rh);                 /* CMP R.y and RH */
      } else {
        ORS_Mark("error in Index");
      }
    }
    Trap(kCondGT, kTrapIndexOutOfBounds);

    /* Multiply index by scale factor */
    if (scale == 4) {
      Put1(kOpLsl, y->r, y->r, 2);
    } else if (scale > 1) {
      Put1a(kOpMul, y->r, y->r, scale);
    }

    switch (x->mode) {
    case kModeDirect:
      if (x->r > 0) {
        /* Local variable */
        /* At this point y->r contains a scaled index and x->a a local variable
         * offset. The element of interest is then to be found at the address
         * SP + R.y + x.offset + frame. Here SP + R.y makes up the base address
         * and x.offset + frame the new offset. 
         */
        Put0(kOpAdd, y->r, kRegSP, y->r);
        x->a += g_frame;
      } else {
        /* Global variable */
        /* Similar to the local variable case, except we don't have to take the
         * frame offset into account.
         */
        if (x->r == 0) {
          /* Declared in current module */
          Put0(kOpAdd, y->r, kRegSB, y->r);
        } else {
          /* Imported */
          Put1a(kOpAdd, g_rh, kRegSB, x->a);
          Put0(kOpAdd, y->r, g_rh, y->r);
          x->a = 0;
        }
      }
      x->r = y->r;
      x->mode = kModeRegI;
      break;
    case kModeParam:
      /* Add base address to scaled index in R.y */
      Put2(kOpLdr, g_rh, kRegSP, x->a + g_frame); /* RH := Mem[SP + x] */
      Put0(kOpAdd, y->r, g_rh, y->r);             /* R.y := RH + R.y */
      /* Use R.y as the base address and x.b as the offset for mode RegI */
      x->mode = kModeRegI;
      x->r = y->r;
      x->a = x->b;
      break;
    case kModeRegI:
      Put0(kOpAdd, x->r, x->r, y->r);
      --g_rh;
      break;
    default:
      assert(0);
    }
  }
}

void
ORG_Deref(item_t * const x)
{
  assert(x && x->type && x->type->tag == kTypePointer);

  switch (x->mode) {
  case kModeDirect:
    if (x->r > 0) {
      /* Local variable */
      Put2(kOpLdr, g_rh, kRegSP, x->a + g_frame); /* RH := Mem[SP + x] */
    } else {
      /* Global variable */
      Put2(kOpLdr, g_rh, kRegSB, x->a);           /* RH := Mem[SB + x] */
    }
    NilCheck();
    x->r = g_rh;
    IncR();
    break;
  case kModeParam:
    Put2(kOpLdr, g_rh, kRegSP, x->a + g_frame);   /* RH := Mem[SP + x] */
    Put2(kOpLdr, g_rh, g_rh, x->b);               /* RH := Mem[RH + x.b] */
    NilCheck();
    x->r = g_rh;
    IncR();
    break;
  case kModeRegI:
    Put2(kOpLdr, x->r, x->r, x->a);               /* R.x := Mem[R.x + x.a] */
    NilCheck();
    break;
  case kModeReg:
    break;
  default:
    assert(0);
  }
  x->mode = kModeRegI;
  x->a = x->b = 0;
}

/* Boolean operators */

void
ORG_Not(item_t * const x)
{
  int32_t   aux;

  assert(x && x->type && x->type->tag == kTypeBool);

  if (x->mode != kModeCond) {
    LoadCond(x);
  }

  /* Set condition to its negation */
  x->r = Negated(x->r);

  /* Swap x->a and x->b */
  aux = x->a;
  x->a = x->b;
  x->b = aux;
}

/*
 * p & q is translated similarly to IF p THEN q ELSE FALSE, while p OR q is
 * treated as IF p THEN TRUE ELSE q. I.e.,
 *
 * (* p & q *)          (* p OR q *)
 *
 *    code(p)              code(p)
 *    BC EQ, L             BC NE, L
 *    code(q)              code(q)
 * L: ...               L: ...
 *
 * We can combine this into larger expressions (p & q) OR r and (p OR q) & r:
 *
 * (* (p & q) OR r *)   (* (p OR q) & r *)
 *
 *     code(p)              code(p)
 *     BC EQ, L0            BC NE, L0
 *     code(q)              code(q)
 *     BC NE, L1            BC EQ, L1
 * L0: code(r)          L0: code(r)
 * L1: ...              L1: ...
 *
 * In general these translation schemes involve forward jumps, whose
 * destinations must be resolved when emitting code(r) and '...' above. To
 * this end, two chains of branch instructions must be maintained: one for
 * jumps whenever the associated conditions are false (cf. & above), stored in
 * x->a for x an item_t *, and the other for jumps whenever their conditions
 * are true (cf. OR), stored in x->b.
 */

/* x := x & */
void
ORG_And1(item_t * const x)
{
  assert(x);

  if (x->mode != kModeCond) {
    LoadCond(x);
  }

  /* Jump to end (after second operand) if first operand is false */
  Put3(kOpBc, Negated(x->r), x->a);

  /* Store address of emitted instruction so we can fix its offset later */
  x->a = g_pc - 1;

  /* Resolve unfixed forward jumps in x for ORs */
  ORG_FixLink(x->b);
  x->b = 0;
}

/* x := x & y */
void
ORG_And2(item_t * const x, item_t * const y)
{
  assert(x && x->mode == kModeCond);
  assert(y);

  if (y->mode != kModeCond) {
    LoadCond(y);
  }

  /* If y is non-atomic, y->a points to a chain of branch instructions with
   * unresolved offsets. Merged 'merges' this chain to that denoted by x->a.
   */
  x->a = Merged(y->a, x->a);

  /* y may contain unresolved forward jumps for ORs */
  x->b = y->b;

  /* Note a CMP instruction was last emitted while parsing y */
  x->r = y->r;
}

/* x := x OR */
void
ORG_Or1(item_t * const x)
{
  assert(x);

  if (x->mode != kModeCond) {
    LoadCond(x);
  }

  /* Jump to end if first operand is true */
  Put3(kOpBc, x->r, x->b);

  /* Store instruction address so we can fix its offset later */
  x->b = g_pc - 1;

  /* Resolve unfixed forward jumps in x for &'s */
  ORG_FixLink(x->a);
  x->a = 0;
}

/* x := x OR y */
void
ORG_Or2(item_t * const x, item_t * const y)
{
  assert(x && x->mode == kModeCond);
  assert(y);

  if (y->mode != kModeCond) {
    LoadCond(y);
  }

  /* y may contain unresolved forward jumps for &'s */
  x->a = y->a;

  /* If y is non-atomic, y->b points to a chain of branch instructions with
   * unresolved offsets. Merged 'merges' this chain to that denoted by x->b.
   */
  x->b = Merged(y->b, x->b);

  /* Note a CMP instruction was last emitted while parsing y */
  x->r = y->r;
}

/* Arithmetic operators */

void
ORG_Neg(item_t * const x)
{
  assert(x && x->type);

  if (x->type->tag == kTypeInt) {
    if (x->mode == kModeImmediate) {
      x->a = -x->a;
    } else {
      /* -x == 0-x */
      Load(x);
      Put1(kOpMov, g_rh, 0, 0);               /* RH := 0 */
      Put0(kOpSub, x->r, g_rh, x->r);         /* R.x := RH - R.x */
    }
  } else {
    assert(x->type->tag == kTypeSet);
    if (x->mode == kModeImmediate) {
      x->a = ~x->a;
    } else {
      /* ~x = x ^ -1 */
      Load(x);
      Put1(kOpXor, x->r, x->r, -1);           /* R.x := R.x ^ -1 */
    }
  }
  assert(x->mode == kModeImmediate || x->mode == kModeReg);
}

void
ORG_AddOp(const int op, item_t * const x, item_t * const y)
{
  assert(x && x->type && x->type->tag == kTypeInt);
  assert(y && y->type && y->type->tag == kTypeInt);

  if (x->mode == kModeImmediate && y->mode == kModeImmediate) {
    if (op == kSymPlus) {
      if (x->a > 0 && y->a > INT32_MAX - x->a) {
        ORS_Mark("overflow");
      }
      x->a += y->a;
    } else {
      assert(op == kSymMinus);
      if (x->a < 0 && y->a > INT32_MIN - x->a) {
        ORS_Mark("underflow");
      }
      x->a -= y->a;
    }
  } else if (y->mode == kModeImmediate) {
    Load(x);
    if (y->a != 0) {
      Put1a(op == kSymPlus ? kOpAdd : kOpSub, x->r, x->r, y->a);
    }
  } else {
    Load(x);
    Load(y);
    Put0(op == kSymPlus ? kOpAdd : kOpSub, g_rh-2, x->r, y->r);
    --g_rh;
    x->r = g_rh-1;
  }
  assert(x->mode == kModeImmediate || x->mode == kModeReg);
}

void
ORG_MulOp(item_t * const x, item_t * const y)
{
  int     exp;

  assert(x && x->type && x->type->tag == kTypeInt);
  assert(y && y->type && y->type->tag == kTypeInt);

  if (x->mode == kModeImmediate && y->mode == kModeImmediate) {
    /* Note: did not check for over- or underflow */
    x->a *= y->a;
  } else if (y->mode == kModeImmediate) {
    Load(x);
    if (y->a >= 2 && Log2(y->a, &exp) == 1) {
      Put1(kOpLsl, x->r, x->r, exp);
    } else if (y->a != 1) {
      Put1(kOpMul, x->r, x->r, y->a);
    }
  } else if (x->mode == kModeImmediate) {
    Load(y);
    if (x->a >= 2 && Log2(x->a, &exp) == 1) {
      Put1(kOpLsl, y->r, y->r, exp);
    } else if (x->a != 1) {
      Put1(kOpMul, y->r, y->r, x->a);
    }
    x->mode = kModeReg;
    x->r = y->r;
  } else {
    Load(x);
    Load(y);
    Put0(kOpMul, g_rh-2, x->r, y->r);
    --g_rh;
    x->r = g_rh - 1;
  }

  assert(x->mode == kModeImmediate || x->mode == kModeReg);
}

void
ORG_DivOp(const int op, item_t * const x, item_t * const y)
{
  int     exp;

  assert(x && x->type && x->type->tag == kTypeInt);
  assert(y && y->type && y->type->tag == kTypeInt);

  if (op == kSymDiv) {
    if (y->mode == kModeImmediate) {
      if (y->a <= 0) {
        ORS_Mark("bad divisor");
      } else if (x->mode == kModeImmediate) {
        x->a /= y->a;
      } else {
        Load(x);
        if (y->a >= 2 && Log2(y->a, &exp) == 1) {
          Put1(kOpAsr, x->r, x->r, exp);
        } else {
          Put1a(kOpDiv, x->r, x->r, y->a);
        }
      }
    } else {
      Load(y);
      Trap(kCondLE, kTrapDivByZero);
      Load(x);
      Put0(kOpDiv, g_rh-2, x->r, y->r);
      --g_rh;
      x->r = g_rh-1;
    }
  } else {
    assert(op == kSymMod);
    if (y->mode == kModeImmediate) {
      if (y->a <= 0) {
        ORS_Mark("bad modulus");
      } else if (x->mode == kModeImmediate) {
        x->a %= y->a;
      } else {
        Load(x);
        if (y->a >= 2 && Log2(y->a, &exp) == 1) {
          if (exp <= 16) {
            Put1(kOpAnd, x->r, x->r, y->a-1);
            /* E.g., x % 0x0100 == x & 0x0011. In other words, the 32-exp
             * most significant bits of x->r get set to 0.
             */
          } else {
            /* If exp > 16, y->a-1 will not fit in im. Instead, we can set the
             * most significant 32-exp bits of x to 0 with a left-shift
             * followed by another right-shift. The latter, in particular,
             * should be a *logical* shift (sign extension would defeat the
             * purpose of setting the significant bits to 0), so instead of ASR
             * we use ROR.
             */
            Put1(kOpLsl, x->r, x->r, 32-exp);
            Put1(kOpRor, x->r, x->r, 32-exp);
          }
        } else {
          Load(x);
          /* Divide x by y, and set H to the remainder. */
          Put1a(kOpDiv, x->r, x->r, y->a);
          /* Set x to H. */
          Put0(kOpMov + kModU, x->r, 0, 0);
        }
      }
    } else {
      Load(y);
      Trap(kCondLE, kTrapDivByZero);
      Load(x);
      Put0(kOpDiv, g_rh-2, x->r, y->r);
      Put0(kOpMov + kModU, g_rh-2, 0, 0);
      --g_rh;
      x->r = g_rh-1;
    }
  }
  assert(x->mode == kModeImmediate || x->mode == kModeReg);
}

/* Sets */

/* x := {x} */
void
ORG_Singleton(item_t * const x)
{
  assert(x && x->type && x->type->tag == kTypeInt);

  if (x->mode == kModeImmediate) {
    x->a = 1 << x->a;
  } else {
    Load(x);
    Put1(kOpMov, g_rh, 0, 1);                 /* RH := 1 */
    Put0(kOpLsl, x->r, g_rh, x->r);           /* R.x := RH << R.x */
  }
  assert(x->mode == kModeImmediate || x->mode == kModeReg);
}

/* x := {x .. y} */
void
ORG_Set(item_t * const x, item_t * const y)
{
  assert(x);
  assert(y);

  if (x->mode == kModeImmediate && y->mode == kModeImmediate) {
    /* E.g., if x = 2 and y = 4, 1 << x = 4 = 0B0000 0100 while 2 << y = 32
     * = 0B0010 0000. Hence, (1 << x) - (2 << y) = 60 = 0B0001 1100.
     */
    x->a = (x->a > y->a) ? 0 : (2 << y->a) - (1 << x->a);
  } else {
    /* x = -1 << x. E.g., if x = 2, -1 << x = 0B1111 1100. */
    if (x->mode == kModeImmediate && x->a <= 16) {
      x->a = ~0u << x->a;
    } else {
      Load(x);
      Put1(kOpMov, g_rh, 0, -1);              /* RH := -1 */
      Put0(kOpLsl, x->r, g_rh, x->r);         /* R.x := RH << R.x */
    }

    /* y = -2 << y. E.g, if y = 4, -2 << y = 0B1110 0000. */
    if (y->mode == kModeImmediate && y->a < 16) {
      Put1(kOpMov, g_rh, 0, ~1u << y->a);      /* RH := -2 << y */
      y->mode = kModeReg;
      y->r = g_rh;
      IncR();
    } else {
      Load(y);
      Put1(kOpMov, g_rh, 0, -2);              /* RH := -2 */
      Put0(kOpLsl, y->r, g_rh, y->r);         /* R.y := RH << R.y */
    }

    assert(y->mode != kModeImmediate);
    if (x->mode == kModeImmediate) {
      if (x->a != 0) {
        Put1(kOpXor, y->r, y->r, -1);         /* R.y := R.y ^ -1 */
        Put1a(kOpAnd, g_rh-1, y->r, x->a);    /* RH-1 := R.y & x */
        /* E.g., if y = 0B1110 0000, y^-1 = y^(0B1111 1111)=0B0001 1111,
         * and if x = 0B1111 1100, (y^-1) & x = 0B0001 1100
         */
      }
      x->mode = kModeReg;
      x->r = g_rh-1;
    } else {
      --g_rh;
      Put0(kOpAnn, g_rh-1, x->r, y->r);       /* RH-1 := R.x & ~R.y */
      /* E.g., if x = 0B1111 1100 and y = 0B1110 0000, x & ~y = 0B0001 1100. */
    }
  }
  assert(x->mode == kModeImmediate || x->mode == kModeReg);
}

void
ORG_In(item_t * const x, item_t * const y)
{
  assert(x);
  assert(y);

  Load(y);

  /* Rotate the x'th element of the set y into the MSB position */
  if (x->mode == kModeImmediate) {
    Put1(kOpRor, y->r, y->r, (x->a + 1) & 0x1F);
    --g_rh;
  } else {
    Load(x);
    Put1(kOpAdd, x->r, x->r, 1);              /* R.x := R.x + 1 */
    Put0(kOpRor, y->r, y->r, x->r);           /* R.y := R.y ROR R.x */
    g_rh -= 2;
  }
  SetCC(x, kCondMI);
}

void
ORG_SetOp(const int op, item_t * const x, item_t * const y)
{
  assert(x && x->type && x->type->tag == kTypeSet);
  assert(y && y->type && y->type->tag == kTypeSet);

  if (x->mode == kModeImmediate && y->mode == kModeImmediate) {
    switch(op) {
    case kSymPlus:  x->a |= y->a;  break;
    case kSymMinus: x->a &= ~y->a; break;
    case kSymTimes: x->a &= y->a;  break;
    case kSymRDiv:  x->a ^= y->a;  break;
    }
  } else if (y->mode == kModeImmediate) {
    Load(x);
    switch (op) {
    case kSymPlus:  Put1a(kOpIor, x->r, x->r, y->a); break;
    case kSymMinus: Put1a(kOpAnn, x->r, x->r, y->a); break;
    case kSymTimes: Put1a(kOpAnd, x->r, x->r, y->a); break;
    case kSymRDiv:  Put1a(kOpXor, x->r, x->r, y->a); break;
    }
  } else {
    Load(x);
    Load(y);
    switch (op) {
    case kSymPlus:  Put0(kOpIor, g_rh-2, x->r, y->r); break;
    case kSymMinus: Put0(kOpAnn, g_rh-2, x->r, y->r); break;
    case kSymTimes: Put0(kOpAnd, g_rh-2, x->r, y->r); break;
    case kSymRDiv:  Put0(kOpXor, g_rh-2, x->r, y->r); break;
    }
    --g_rh;
    x->r = g_rh-1;
  }

  assert(x->mode == kModeImmediate || x->mode == kModeReg);
}

/* Relations */

/* x := x rel y */
void
ORG_IntRel(const int rel, item_t * const x, item_t * const y)
{
  assert(x && x->type && x->type->tag == kTypeInt);
  assert(y && y->type && y->type->tag == kTypeInt);

  /* A CMP instruction (as a synonym of SUB) is emitted to set the N, Z, C and
   * V condition registers, while its result value is discarded. The relation
   * type, in turn, is stored for later use in the r field of an item with mode
   * Cond.
   */
  if (y->mode == kModeImmediate && y->type->tag != kTypeProc) {
    Load(x);
    /* Relations x == 0 and x != 0 already represented by the existing values
     * of the condition registers
     */
    if (y->a != 0 || (rel != kSymEql && rel != kSymNeq)) {
      Put1a(kOpCmp, x->r, x->r, y->a);
    }
    --g_rh;
  } else {
    if (x->mode == kModeCond || y->mode == kModeCond) {
      ORS_Mark("not implemented");
    }
    Load(x);
    Load(y);
    Put0(kOpCmp, x->r, x->r, y->r);
    g_rh -= 2;
  }
  SetCC(x, g_relmap[rel - kSymEql]);
}

void
ORG_StringRel(const int rel, item_t * const x, item_t * const y)
{
  assert(x);
  assert(y);

  if (x->type->tag == kTypeString) {
    LoadStringAdr(x);
  } else {
    /* Char array */
    LoadAdr(x);
  }

  if (y->type->tag == kTypeString) {
    LoadStringAdr(y);
  } else {
    /* Char array */
    LoadAdr(y);
  }

  Put2(kOpLdr + 1, g_rh, x->r, 0);            /* RH := Mem[R.x] (load byte) */
  Put1(kOpAdd, x->r, x->r, 1);                /* R.x := R.x + 1 */
  Put2(kOpLdr + 1, g_rh + 1, y->r, 0);        /* RH+1 := Mem[R.y] (byte) */
  Put1(kOpAdd, y->r, y->r, 1);                /* R.y := R.y + 1 */
  Put0(kOpCmp, g_rh + 2, g_rh, g_rh + 1);     /* CMP RH, R+1 */
  Put3(kOpBc, kCondNE, 2);                    /* Jump to end if not equal */
  Put1(kOpCmp, g_rh + 2, g_rh, 0);            /* CMP RH, 0 */
  Put3(kOpBc, kCondNE, -8);                   /* Move to next byte if RH # 0 */
  g_rh -= 2;
  SetCC(x, g_relmap[rel - kSymEql]);
}

/* Assignments */

/* Converts a string of size 1 (e.g., "x") back into a CHAR */
void
ORG_StrToChar(item_t * const x)
{
  assert(x && x->type && x->type->tag == kTypeString && x->b == 2);

  /* Reclaim storage from string pool */
  x->type = &g_char_type;
  g_strx -= 4;
  x->a = g_pool[x->a];
}

/* x := y */
void
ORG_Store(item_t * const x, item_t * const y)
{
  assert(x);
  assert(y);

  Load(y);
  Store(x, y->r);
  --g_rh;
}

void
ORG_StoreStruct(item_t * const x, item_t * const y)
{
  int     s;    /* Element type size */
  int     pc0;

  assert(x);
  assert(y);

  assert(g_frame == 0);

  /* Stop if there are no words to be copied */
  if (y->type->size == 0) {
    goto end;
  }

  /* Load absolute addresses of x and y into registers */
  LoadAdr(x);
  LoadAdr(y);

  /* Store the no. of words to be copied in a register */
  if (x->type->tag == kTypeArray && x->type->u.len > 0) {
    if (y->type->u.len >= 0) {
      if (x->type->size != y->type->size) {
        ORS_Mark("different length/size, not implemented");
      } else {
        /* Set RH to y's size in words (rounded up) */
        Put1a(kOpMov, g_rh, 0, (y->type->size + 3) / 4);
      }
    } else {
      /* Open array */
      /* y->a still contains y's offset from SP, with the 2nd word containing
       * its size */
      Put2(kOpLdr, g_rh, kRegSP, y->a + 4); /* RH := Mem[SP + y + 4] */
      s = y->type->base->size;
      pc0 = g_pc;
      Put3(kOpBc, kCondEQ, 0);                /* Jump to end if size = 0 */
      if (s == 1) {
        Put1(kOpAdd, g_rh, g_rh, 3);          /* RH := RH + 3 */
        Put1(kOpAsr, g_rh, g_rh, 2);          /* RH := RH DIV 4 */
      } else if (s != 4) {
        assert(!(s % 4));
        Put1a(kOpMul, g_rh, g_rh, s / 4);     /* RH:= RH * (s DIV 4) */
      }

      /* Validate array bounds (at runtime) */
      Put1a(kOpMov, g_rh + 1, 0, (x->type->size + 3) / 4);
      Put0(kOpCmp, g_rh + 1, g_rh, g_rh + 1);
      Trap(kCondGT, kTrapIndexOutOfBounds);

      Fix(pc0, g_pc + 5 - pc0);               /* Fix fwd jump */
    }
  } else if (x->type->tag == kTypeRecord) {
    assert(!(x->type->size % 4));
    Put1a(kOpMov, g_rh, 0, x->type->size/4);  /* RH := x.type.size DIV 4 */
  } else {
    ORS_Mark("inadmissible assignment");
  }

  /* Copy word by word */
  Put2(kOpLdr, g_rh+1, y->r, 0);              /* RH+1 := Mem[R.y] */
  Put1(kOpAdd, y->r, y->r, 4);                /* R.y := R.y + 4   */
  Put2(kOpStr, g_rh+1, x->r, 0);              /* Mem[R.x] := RH+1 */
  Put1(kOpAdd, x->r, x->r, 4);                /* R.x := R.x + 4   */
  Put1(kOpSub, g_rh, g_rh, 1);                /* RH := RH - 1     */
  Put3(kOpBc, kCondNE, -6);                   /* Repeat if RH # 0 */

end:
  g_rh = 0;
}

void
ORG_CopyString(item_t * const x, item_t * const y)
{
  int     len;

  assert(x && x->type->tag == kTypeArray);
  assert(y && y->type->tag == kTypeString);

  LoadAdr(x);

  /* Validate array size */
  if ((len = x->type->u.len) >= 0) {
    if (len < y->b) {
      ORS_Mark("string too long");
    }
  } else {
    assert(g_frame == 0);
    Put2(kOpLdr, g_rh, kRegSP, x->a + 4);     /* RH := Mem[x] */
    Put1(kOpCmp, g_rh, g_rh, y->b);           /* CMP RH and y.length */
    Trap(kCondLT, kTrapIndexOutOfBounds);     /* Jump if < */
  }

  LoadStringAdr(y);
  Put2(kOpLdr, g_rh, y->r, 0);                /* RH := Mem[R.y] */
  Put1(kOpAdd, y->r, y->r, 4);                /* R.y := R.y + 4 */
  Put2(kOpStr, g_rh, x->r, 0);                /* Mem[R.x] := RH */
  Put1(kOpAdd, x->r, x->r, 4);                /* R.x := R.x + 4 */
  Put1(kOpAsr, g_rh, g_rh, 24);               /* RH := RH >> 24 */
  Put3(kOpBc, kCondNE, -6);                   /* Repeat if # 0  */
  g_rh = 0;
}

/* Parameters */

void
ORG_OpenArrayParam(item_t * const x, const bool loaded)
{
  assert(x);

  /* Load the actual parameter's absolute address */
  if (!loaded) {
    LoadAdr(x);
  }
  assert(x->mode == kModeReg);

  /* Additionally store the actual parameter's length */
  if (x->type->u.len >= 0) {
    Put1a(kOpMov, g_rh, 0, x->type->u.len);         /* RH := x.type.len */
  } else {
    /* If the actual parameter is also an open array, then it must itself be
     * a VAR parameter of the current (i.e., calling) procedure, and we can
     * fetch the length from (the second word representing) its own
     * corresponding actual parameter.
     */
    assert(x->b == 0);
    Put2(kOpLdr, g_rh, kRegSP, x->a + 4 + g_frame); /* RH := Mem[RH + x + 4] */
  }
  IncR();
}

void
ORG_VarParam(item_t * const x, type_t * const ftype)
{
  assert(x);        /* x is the actual parameter */
  assert(ftype);    /* ftype is the formal parameter type */

  /* VAR parameters are represented by their absolute address */
  LoadAdr(x);

  /* For open arrays an additional word is used. */

  if (ftype->tag == kTypeArray && ftype->u.len < 0) {
    ORG_OpenArrayParam(x, true);
  } 
}

void
ORG_ValueParam(item_t * const x)
{
  Load(x);
}

void
ORG_StringParam(item_t * const x)
{
  assert(x);

  /* Load absolute address */
  LoadStringAdr(x);

  /* Load length */
  Put1(kOpMov, g_rh, 0, x->b);
  IncR();
}

/* FOR statements */

void
ORG_For0(item_t * const y)
{
  /* y is the initializer for the loop variable */
  assert(y);

  Load(y);
}

int
ORG_For1(item_t * const x, item_t * const y, item_t * const z,
         item_t * const w)
{
  int     l;

  assert(x);                            /* FOR x */
  assert(y);                            /* := y  */
  assert(z);                            /* TO z  */
  assert(w);                            /* BY w  */
  assert(y->mode == kModeReg);
  assert(w->mode == kModeImmediate);

  /* Compare y with z (y - z) */
  if (z->mode == kModeImmediate) {
    Put1a(kOpCmp, g_rh, y->r, z->a);    /* RH := R.y - z.a */
  } else {
    Load(z);
    --g_rh;
    assert(z->r == g_rh);
    Put0(kOpCmp, g_rh, y->r, z->r);     /* RH := R.y - R.z */
  }

  /* Conditional forward jump (fix offset later) */
  l = g_pc;
  if (w->a > 0) {
    Put3(kOpBc, kCondGT, 0);            /* If w > 0, exit loop when y > z */
  } else if (w->a < 0) {
    Put3(kOpBc, kCondLT, 0);            /* If w < 0, exit loop when y < z */
  } else {
    ORS_Mark("zero increment");
    Put3(kOpBc, kCondMI, 0);
  }

  /* x := y */
  ORG_Store(x, y);

  /* Return address of branch instruction for fixup */
  return l;
}

void
ORG_For2(item_t * const x, item_t * const w)
{
  assert(x);  /* Loop variable */
  assert(w);  /* Increment */
  assert(w->mode == kModeImmediate);

  Load(x);
  --g_rh;
  Put1a(kOpAdd, x->r, x->r, w->a);      /* R.x := R.x + w */
}

/* Branches and procedure calls */

int
ORG_Here(void)
{
  return g_pc;
}

/* Unconditional forward jump */
void
ORG_FJump(int * const l)
{
  /* *l is the address of another branch instruction (link) or 0 */
  assert(l);

  /* Unconditional jump with relative offset and no return */
  Put3(kOpBc, kCondTrue, *l);

  /* Save the emitted instruction's address so we can fix its offset later */
  *l = g_pc - 1;
}

/* Conditional forward jump */
void
ORG_CFJump(item_t * const x)
{
  assert(x);

  if (x->mode != kModeCond) {
    LoadCond(x);
  }

  /* x->a is the address of another branch instruction (link) or 0 */
  /* x->r is a condition s.t. a jump is to be performed if it does not hold */

  /* Conditional jump w/h return */
  Put3(kOpBc, Negated(x->r), x->a);

  /* Fix T-Chain */
  ORG_FixLink(x->b);

  /* Store address of emitted instruction so we can fix its offset later */
  x->a = g_pc-1;
}

/* Unconditional backwards jump */
void
ORG_BJump(const int l)
{
  Put3(kOpBc, kCondTrue, l-g_pc-1);
}

/* Conditional backwards jump */
void
ORG_CBJump(item_t * const x, const int l)
{
  assert(x);

  if (x->mode != kModeCond) {
    LoadCond(x);
  }
  Put3(kOpBc, Negated(x->r), l-g_pc-1);
  ORG_FixLink(x->b);
  FixLinkWith(x->a, l);
}

void
ORG_Fixup(item_t * const x)
{
  assert(x);
  ORG_FixLink(x->a);
}

int
ORG_PrepCall(item_t * const x)
{
  int     r;

  assert(x && x->type && x->type->tag == kTypeProc);

  /* If not a constant or variable- or value parameter, load the absolute
   * address of the procedure into a register.
   */
  if (x->mode == kModeRegI) {
    Load(x);
  }

  /* Save registers R0,...,RH. This will be needed in case of a function call
   * that occurs as a subexpression (the operand stack then not being empty) or
   * if the absolute address of the procedure has just been loaded into memory
   * with the above call to Load.
   */
  r = g_rh;
  if (r > 0) {
    assert(x->type->base->tag != kTypeNone || r == 1);
    SaveRegs(r);
    g_rh = 0;
  }

  /* Remember the number of saved registers so we can restore them later */
  return r;
}

void
ORG_Call(item_t * const x, int r)
{
  assert(x && x->type && x->type->tag == kTypeProc);

  if (x->mode == kModeImmediate) {
    /* x->a contains the byte address of a procedure relative to PC. Divide
     * by 4 to obtain the word address and subtract PC.
     */
    Put3(kOpBl, kCondTrue, (x->a / 4)-g_pc-1);
  } else {
    if (x->mode <= kModeParam) {
      Load(x);
      --g_rh;
    } else {
      /* SaveRegs loaded the procedure's address in a register and moved it
       * to memory (along with the operand stack, if non-empty). Restore it.
       */
      assert(x->mode == kModeReg);
      Put2(kOpLdr, g_rh, kRegSP, 0);    /* RH := Mem[SP] */

      /* We don't want the procedure's address 'restored' again as part of the
       * the operand stack after the procedure completes. Hence, remove it.
       */
      Put1(kOpAdd, kRegSP, kRegSP, 4);  /* SP := SP + 4 */
      --r;
      g_frame -= 4;
      assert(x->type->base->tag != kTypeNone || r == 0);
    }
    Trap(kCondEQ, kTrapNilPtr);
    Put3(kOpBlr, kCondTrue, g_rh);
  }

  if (x->type->base->tag == kTypeNone) {
    /* Procedure */
    /* Procedure calls, being statements, involve empty operand stacks */
    g_rh = 0;
  } else {
    /* Function */
    /* Function calls, being expressions, may involve non-empty operand
     * stacks
     */
    if (r > 0) {
      /* Move return value (in R0) to top of operand stack (r) */
      Put0(kOpMov, r, 0, 0);

      /* Restore operand stack (R0,...,R(r-1)) */
      RestoreRegs(r);
    }
    /* Functions leave their return values in registers */
    x->mode = kModeReg;
    x->r = r;
    g_rh = r+1;
  }
}

void
ORG_Enter(const int parblksize, const int locblksize)
{
  int     a;
  int     r;

  /* See ProcedureDecl in parser.h for the calculation of locblksize */

  /* The local block size includes storage for the parameters */
  assert(locblksize >= parblksize);

  /* The local block size at least accomodates a return address */
  assert(locblksize >= 4);

  g_frame = 0;

  if (locblksize >= 256) {
    ORS_Mark("too many locals");
  }

  /* Procedure prolog */

  /* Allocate memory for arguments and local variable declarations */
  Put1(kOpSub, kRegSP, kRegSP, locblksize);   /* SP := SP - locblksiz */

  /* Store the contents of the LNK register (return address), set by the
   * branch-and-link instruction that initiated the procedure call.
   */
  Put2(kOpStr, kRegLNK, kRegSP, 0);           /* Mem[SP] := LNK */

  /* Move the procedure's arguments from the registers to memory */
  /* Note a is initialized to 4 since we already set Mem[SP] to LNK */
  for (a = 4, r = 0; a < parblksize; ++r, a += 4) {
    Put2(kOpStr, r, kRegSP, a);               /* Mem[SP + a] = r */
  }

  /* Note no static link needs to be stored since only strictly local- and
   * global variables are accessible to any given procedure. Similarly, since
   * the size of a frame is known at compile time we don't need to store a
   * dynamic link either. See p.74 of Wirth's Compiler Construction, May 2017.
   */
}

void
ORG_Return(const form_t tag, item_t * const x, const int size)
{
  assert(x);
  assert(g_rh <= 1);

  /* Load the return value, if any */
  if (tag != kTypeNone) {
    Load(x);
  }

  /* Epilog */

  /* Restore contents of the LNK register (return address) */
  Put2(kOpLdr, kRegLNK, kRegSP, 0);         /* LNK := Mem[SP] */

  /* Release memory */
  Put1(kOpAdd, kRegSP, kRegSP, size);       /* SP := SP + size */

  /* Unconditional jump to return address */
  Put3(kOpBr, kCondTrue, kRegLNK);          /* BR T, LNK */

  /* Return value moved from R0 by Call */
  g_rh = 0;
}

/* In-line procedures */

void
ORG_Increment(const bool dec, item_t * const x, item_t * const y)
{
  int           op;           /* SUB or ADD */
  int           v;            /* Modifier bit */
  int           zr;           /* Register R.z */

  /* INC(x,y) / DEC(x,y) */
  assert(x);
  assert(y);
  assert(g_frame == 0);

  /* Set opcode according to whether INC or DEC was called */
  op = dec? kOpSub : kOpAdd;

  /* Set v modifier bit for loads and stores */
  v = (x->type == &g_byte_type) ? 1 : 0;

  /* By default (i.e., if y not specified) increment by 1 */
  if (y->type->tag == kTypeNone) {
    y->mode = kModeImmediate;
    y->a = 1;
  }

  if (x->mode == kModeDirect && x->r > 0) {
    /* x is a local variable */
    /* Load x */
    zr = g_rh;
    Put2(kOpLdr + v, zr, kRegSP, x->a);     /* R.z := Mem[SP + x] */
    IncR();
    /* z := x + y */
    if (y->mode == kModeImmediate) {
      Put1a(op, zr, zr, y->a);              /* R.z := R.z op y.a */
    } else {
      Load(y);
      Put0(op, zr, zr, y->r);               /* R.z := R.z op R.y */
      --g_rh;
    }
    Put2(kOpStr + v, zr, kRegSP, x->a);     /* Mem[SP + x] := R.z */
    --g_rh;
  } else {
    /* Load x */
    LoadAdr(x);                             /* R.x := ADR(x) */
    zr = g_rh;
    Put2(kOpLdr + v, g_rh, x->r, 0);        /* R.z := Mem[R.x] */
    IncR();
    /* z := x + y */
    if (y->mode == kModeImmediate) {
      Put1a(op, zr, zr, y->a);              /* R.z := R.z op y */
    } else {
      Load(y);
      Put0(op, zr, zr, y->r);               /* R.z := R.z op R.y */
      --g_rh;
    }
    Put2(kOpStr + v, zr, x->r, 0);
    g_rh -= 2;
  }
}

void
ORG_Include(const bool excl, item_t * const x, item_t * const y)
{
  int     op;
  int     zr;

  assert(x);
  assert(y);

  /* Load x */
  LoadAdr(x);                               /* R.x := ADR[x] */
  zr = g_rh;
  Put2(kOpLdr, g_rh, x->r, 0);              /* R.z := Mem[R.x] */
  IncR();

  /* Set opcode according to whether INCL or EXCL was called */
  op = excl ? kOpAnn : kOpIor;

  if (y->mode == kModeImmediate) {
    Put1a(op, zr, zr, 1 << y->a);           /* R.z := R.z op (1 << y) */
  } else {
    Load(y);
    Put1(kOpMov, g_rh, 0, 1);               /* RH := 1 */
    Put0(kOpLsl, y->r, g_rh, y->r);         /* R.y := RH << R.y */
    Put0(op, zr, zr, y->r);                 /* R.z := R.z op R.y */
    --g_rh;
  }
  Put2(kOpStr, zr, x->r, 0);                /* Mem[R.x] := R.z */
  g_rh -= 2;
}

void
ORG_Assert(item_t * const x)
{
  int     cond;

  assert(x);

  if (x->mode != kModeCond) {
    LoadCond(x);
  }
  if (x->a == 0) {
    /* No F-chain */
    cond = Negated(x->r);
  } else {
    /* Conditional jump, pushed on T-chain */
    Put3(kOpBc, x->r, x->b);
    x->b = g_pc-1;
    /* Resolve jump target for F-chain */
    ORG_FixLink(x->a);
    /* Set trap condition */
    cond = kCondTrue;
  }
  Trap(cond, kTrapAssert);

  /* Jump target for T-chain */
  ORG_FixLink(x->b);
}

void
ORG_Read(item_t * const x)
{
  int     addr;

  if (x->type->tag == kTypeInt) {
    addr = -1;
  } else if (x->type->tag == kTypeChar) {
    addr = -2;
  } else {
    ORS_Mark("not an INTEGER or CHAR");
    return;
  }

  Put1(kOpMov, g_rh, 0, addr);
  IncR();
  Put2(kOpLdr, g_rh, g_rh-1, 0);
  Store(x, g_rh);
  --g_rh;
}

void
ORG_Write(const bool line, item_t * const x)
{
  int     addr;

  assert(x);

  if (x->type->tag != kTypeNone) {
    if (x->type->tag == kTypeInt) {
      Load(x);
      addr = -1;
    } else if (x->type->tag == kTypeChar) {
      Load(x);
      addr = -2;
    } else if (x->type->tag == kTypeString) {
      assert(x->mode == kModeImmediate);
      LoadStringAdr(x);
      addr = -3;
    } else if (x->type->tag == kTypeArray && x->type->base->tag == kTypeChar) {
      LoadAdr(x);
      addr = -3;
    } else {
      ORS_Mark("not an INTEGER, CHAR (ARRAY) or STRING");
      return;
    }
    Put1(kOpMov, g_rh, 0, addr);      /* RH := addr */
    Put2(kOpStr, x->r, g_rh, 0);      /* Mem[RH] := R.x */
    --g_rh;
  }

  if (line) {
    Put1(kOpMov, g_rh, 0, -4);        /* RH := -4 */
    Put2(kOpStr, x->r, g_rh, 0);      /* Mem[RH] := R.x */
  }
}

void
ORG_Get(const bool put, item_t * const x, item_t * const y)
{
  assert(x);
  assert(y);

  /* Load x and interpret it as an absolute (base) address (with offset 0) */
  Load(x);
  x->type = y->type;
  x->mode = kModeRegI;
  x->a = 0;

  if (put) {
    ORG_Store(x, y);                  /* Load(y); Mem[R.x] := R.y */
  } else {
    ORG_Store(y, x);                  /* Load(x); Mem[R.y + y.a] := R.x */
  }
}

void
ORG_Copy(item_t * const x, item_t * const y, item_t * const z)
{
  assert(x);
  assert(y);
  assert(z);

  Load(x);
  Load(y);
  if (z->mode == kModeImmediate) {
    if (z->a > 0) {
      Load(z);
    } else {
      ORS_Mark("bad count");
    }
  } else {
    Load(z);
    Trap(kCondLT, kTrapIndexOutOfBounds);
    /* Skip remaining instructions if 0 (i.e., no) bytes are to be copied */
    Put3(kOpBc, kCondEQ, 6);          /* Jump (to PC + 1 + 6) if z = 0 */
  }
  Put2(kOpLdr, g_rh, x->r, 0);        /* RH := Mem[R.x] */
  Put1(kOpAdd, x->r, x->r, 4);        /* R.x := R.x + 4 */
  Put2(kOpStr, g_rh, y->r, 0);        /* Mem[R.y] := RH */
  Put1(kOpAdd, y->r, y->r, 4);        /* R.y := R.y + 4 */
  Put1(kOpSub, z->r, z->r, 1);        /* R.z := R.z - 1 */
  Put3(kOpBc, kCondNE, -6);           /* Jump (to PC + 1 - 6) if z # 0 */
  g_rh -= 3;
}

/* In-line functions */

void
ORG_Abs(item_t * const x)
{
  assert(x);

  if (x->mode == kModeImmediate) {
    x->a = abs(x->a);
  } else {
    Put1(kOpCmp, x->r, x->r, 0);      /* Compare R.x with 0 */
    Put3(kOpBc, kCondGE, 2);          /* Jump (to PC + 1 + 2) if R.x >= 0 */
    Put1(kOpMov, g_rh, 0, 0);         /* RH := 0 */
    Put0(kOpSub, x->r, g_rh, x->r);   /* R.x := RH - R.x */
  }
}

void
ORG_Odd(item_t * const x)
{
  assert(x);

  Load(x);
  Put1(kOpAnd, x->r, x->r, 1);        /* R.x := R.x & 1 */
  SetCC(x, kCondNE);
  --g_rh;
}

void
ORG_Ord(item_t * const x)
{
  assert(x);

  if (x->mode!=kModeImmediate && x->mode!=kModeType && x->mode!=kModeReg) {
    Load(x);
  }
}

void
ORG_Len(item_t * const x)
{
  assert(x && x->mode != kModeReg);

  if (x->type->u.len >= 0) {
    if (x->mode == kModeRegI) {
      --g_rh;
    }
    x->mode = kModeImmediate;
    x->a = x->type->u.len;
  } else {
    /* Open array */
    Put2(kOpLdr, g_rh, kRegSP, x->a + 4 + g_frame); /* RH := Mem[SP + x] */
    x->mode = kModeReg;
    x->r = g_rh;
    IncR();
  }
}

void
ORG_Shift(const int fct, item_t * const x, item_t * const y)
{
  int     op;

  assert(x);
  assert(y);

  if (fct == 0) {
    op = kOpLsl;                          /* Logical shift left */
  } else if (fct == 1) {
    op = kOpAsr;                          /* Arithmetic shift right */
  } else {
    assert(fct == 2);
    op = kOpRor;                          /* Rotate right */
  }

  if (y->mode == kModeImmediate) {
    Put1(op, x->r, x->r, y->a & 0x1F);    /* R.x := R.x op (y & 0x1F) */
  } else {
    Load(y);
    Put0(op, g_rh-2, x->r, y->r);         /* RH-2 := R.x op  R.y */
    --g_rh;
    x->r = g_rh - 1;
  }
}

void
ORG_Bit(item_t * const x, item_t * const y)
{
  assert(x);
  assert(y);

  /* BIT(a,n) extracts bit n from Mem[a] by rotating it into the MSB position
   * and testing whether the result is negative.
   */

  Load(x);
  Put2(kOpLdr, x->r, x->r, 0);          /* R.x := Mem[R.x] */
  if (y->mode == kModeImmediate) {
    Put1(kOpRor, x->r, x->r, y->a + 1); /* R.x := R.x rot (y + 1) */
    --g_rh;
  } else {
    Load(y);
    Put1(kOpAdd, y->r, y->r, 1);        /* R.y := R.y + 1 */
    Put0(kOpRor, x->r, x->r, y->r);     /* R.x := R.x rot R.y */
    g_rh -= 2;
  }
  SetCC(x, kCondMI);
}

void
ORG_Register(item_t * const x)
{
  assert(x && x->mode == kModeImmediate);

  Put0(kOpMov, g_rh, 0, x->a & 0xF);    /* RH := x & 0xF */
  x->mode = kModeReg;
  x->r = g_rh;
  IncR();
}

void
ORG_Adr(item_t * const x)
{
  if (x->mode == kModeDirect || x->mode == kModeParam || x->mode == kModeRegI){
    LoadAdr(x);
  } else if (x->mode == kModeImmediate && x->type->tag == kTypeProc) {
    Load(x);
  } else if (x->mode == kModeImmediate && x->type->tag == kTypeString) {
    LoadStringAdr(x);
  } else {
    ORS_Mark("not addressable");
  }
}

void
ORG_Condition(item_t * const x)
{
  assert(x && x->mode == kModeImmediate);

  SetCC(x, x->a);
}

void
ORG_Open(void)
{
  g_pc = 1;
  g_rh = 0;
  g_strx = 0;
}

void
ORG_SetDataSize(const int dc)
{
  g_varsize = dc;
}

void
ORG_Header(void)
{
  Put1(kOpSub, kRegSP, kRegSP, 4);    /* SP := SP - 4 */
  Put2(kOpStr, kRegLNK, kRegSP, 0);   /* Mem[SP] := LNK */
}

void
ORG_Close(void)
{
  int32_t * base;
  int       i;

  Put2(kOpLdr, kRegLNK, kRegSP, 0);   /* LNK := Mem[SP] */
  Put1(kOpAdd, kRegSP, kRegSP, 4);    /* SP := SP + 4 */
  Put3(kOpBr, kCondTrue, kRegLNK);    /* Return */

  /* Clear data bits */
  memset(g_mem+g_pc, 0, kMemSz-g_pc);

  /* Copy string pool over to memory */
  assert(!(g_strx % 4));
  base = g_mem + g_pc + (g_varsize / 4);
  for (i = 0; i != g_strx; i += 4) {
    assert(!(i % 4));
    base[i / 4] |= g_pool[i];
    base[i / 4] |= g_pool[i + 1] << 8;
    base[i / 4] |= g_pool[i + 2] << 16;
    base[i / 4] |= g_pool[i + 3] << 24;
  }
}

void ORG_Decode(void)
{
  int           pc;           /* program counter */
  int32_t       ir;           /* instruction 'register' */
  int           a;            /* Result register R.a (F0-2) or condition F3) */
  int           b;            /* Operand R.b (F0, F1) or base address (F2) */
  int           n;            /* Operand R.c (F0), im (F1) or address (F2) */
  int           op;           /* opcode */

  for (pc = 1; pc != g_pc; ++pc) {
    /* Print code address */
    printf("%04X: ", 4*pc);

    /* Fetch next instruction */
    ir = g_mem[pc];

    /* Register (F0, F1, F2) or condition (F3) */
    a = (ir >> 24) & 0xF;

    if (!(ir & kInsnMsb) || !(ir & kInsnQ)) {
      /* Register and memory instructions */

      /* Operand (F0, F1) or base address (F2) */
      b = (ir >> 20) & 0xF;

      if (!(ir & kInsnMsb)) {
        /* Register instruction (F0, F1) */
        op = (ir >> 16) & 0xF;
        printf("%-3s ", g_mnemo[op]);

        if (!(ir & kInsnQ)) {
          /* Format F0 */
          n = ir & 0xF;
          if (op == kOpMov) {
            if (ir & kInsnU) {
              if (ir & kInsnV) {
                printf("%s, [N,Z,C,V]\n", g_regs[a]);
              } else {
                printf("%s, H\n", g_regs[a]);
              }
            } else {
              printf("%s, %s\n", g_regs[a], g_regs[n]);
            }
          } else {
            printf("%s, %s, %s\n", g_regs[a], g_regs[b], g_regs[n]);
          }
        } else {
          /* Format F1 */
          n = ir & 0xFFFF;
          if (op == kOpMov) {
            if (ir & kInsnU) {
              printf("%s, %X << 16\n", g_regs[a], n);
            } else {
              printf("%s, %X\n", g_regs[a], n);
            }
          } else {
            printf("%s, %s, %X\n", g_regs[a], g_regs[b], n);
          }
        }
      } else {
        assert(!(ir & kInsnQ));

        /* Memory instruction (F2) */
        op = (ir >> 28) & 0xF;
        n = ir & 0xFFFFF;
        printf("%-3s %s, %s, %X\n", g_mnemo[op+4], g_regs[a], g_regs[b], n);
      }
    } else {
      /* Branch instruction (F3) */
      op = (ir >> 28) & 0x3;
      printf("%-3s ", g_mnemo[op+16]);

      switch (op) {
      case kOpBr: case kOpBlr:
        assert(!(ir & kInsnU));
        n = ir & 0xF;
        printf("%s, %s\n", g_cond[a], g_regs[n]);
        break;
      case kOpBc: case kOpBl:
        assert(ir & kInsnU);
        printf("%s, %X\n", g_cond[a], 4 * (ir & 0xFFFFFF));
        break;
      default:
        assert(0);
      }
    }
  }
}

/* Instruction assemblers */

/*
 * Writes a register instruction (MSB 0) in format F0 (value 0 for the second
 * most significant bit):
 *
 *      4     4   4   4    12   4
 *   +------+---+---+----+----+---+
 *   | 00uv | a | b | op |    | c |
 *   +------+---+---+----+----+---+
 *
 * The operation with opcode 'op' is applied to the operands found in registers
 * 'b' and 'c', with the result being stored in register 'a'. The modifier bits
 * 'u' and 'v' have the following significance:
 * - If 'u' is set with MOV when 'v' is 0, R.a := H.
 * - If 'u' is set with MOV when 'v' is 1, R.a := [N,Z,C,V] (concatenated)
 * - If 'u' is set with ADD or SUB, the carry C is used as input.
 * - If 'u' is set with MUL, the operation is unsigned.
 */
static void
Put0(const int op, const int a, const int b, const int c)
{
  assert(0 <= a && a <= 15);
  assert(0 <= b && b <= 15);
  assert(0 <= c && c <= 15);
  g_mem[g_pc++] = (a << 24) | (b << 20) | (op << 16) | c;
}

/*
 * Writes a register instruction (MSB 0) in format F1 (value 1 for the second
 * most significant bit):
 *
 *      4     4   4   4      16
 *   +------+---+---+----+--------+
 *   | 01uv | a | b | op |   im   |
 *   +------+---+---+----+--------+
 *
 * The operation with opcode 'op' is applied to the operand found in register
 * 'b' and the constant 'im', with the result being stored in register 'a'. The
 * modifier bit 'v' states whether im is to be sign-extended when expanded to
 * 32-bits. The 'u' bit, in turn, has the following significance:
 * - If used with MOV, 'im' is left-shifted by 16-bits (the 'v' bit
 *   consequently being ignored).
 * - If used with ADD, SUB or MUL, the effect is the same as for format F0.
 */
static void
Put1(int op, const int a, const int b, const int32_t im)
{
  assert(0 <= a && a <= 15);
  assert(0 <= b && b <= 15);
  if (im < 0) {
    /* Set the v modifier bit to indicate sign extension is to be used in
     * widening im to a 32-bit word. Recall kModV is defined 0x1000 whereas
     * 0 <= op <= 7, with kModV + op thus leaving the two middle hexadecimal
     * digits for a and b.
     */
    op += kModV;
  }
  /* Note 0x40 equals 0100 0000 in binary. */
  g_mem[g_pc++] = ((a + 0x40) << 24) | (b << 20) | (op << 16) | (im & 0xFFFF);
}

/* Same as Put1, but also applies a range test to im. */
static void
Put1a(int op, const int a, const int b, const int32_t im)
{
  /* As the formal parameter im is a signed 32-bit integer, values -0x10000 and
   * 0x0FFFF have the 16 most significant bits set to all 1's, resp. all 0's.
   * Note Put1 tests whether im is >0 or <0 and sets the V modifier bit
   * accordingly to ensure im gets properly expanded back to a sigend 32-bit
   * word.
   */
  if (im >= -0x10000 && im <= 0x0FFFF) {
    Put1(op, a, b, im);
  } else {
    /* First load the 16 most significant bits to the top of the stack by
     * shifting them to the right. By setting the u bit, MOV shifts them back
     * to their original position.
     */
    Put1(kOpMov + kModU, g_rh, 0, (im >> 16) & 0xFFFF);

    /* Add back the 16 least significant bits using a bitwise or, if needed
     * (i.e., if not 0).
     */
    if (im & 0xFFFF) {
      Put1(kOpIor, g_rh, g_rh, im & 0xFFFF);
    }

    /* Now with both operands in registers, use Put0. */
    Put0(op, a, b, g_rh);
  }
}

/*
 * Writes a memory instruction (first- and second most significant bits 1 and
 * 0) in format F2:
 *
 *      4     4   4       20
 *   +------+---+---+-------------+
 *   | 10uv | a | b |     off     |
 *   +------+---+---+-------------+
 *
 * The 'off' field identifies an offset, to be added to the base address found
 * in register 'b'. The contents of register 'a' is stored at R.b + off if
 * the modifier bit 'u' is set (STW), whereas otherwise the word found at
 * R.b + off is loaded into R.a (LDW). Finally, for load instructions
 * (only) the modifier bit 'v' may be set to load a single byte instead of an
 * entire word (LDB).
 */
static void
Put2(const int op, int a, int b, int off)
{
  /* Note kOpLdr was defined as 1000 in binary (u == 0), and kOpSt as 1010
   * (u == 1). To set v, pass +1.
   */
  assert(op == kOpLdr || op == kOpLdr + 1 || op == kOpStr || op == kOpStr + 1);
  assert(0 <= a && a <= 15);
  assert(0 <= b && b <= 15);

  g_mem[g_pc++] = (op << 28) | (a << 24) | (b << 20) | (off & 0xFFFF);
}

/*
 * Writes a branch instruction in format F3:
 *
 *      4       4        20        4
 *   +------+------+-------------+---+
 *   | 110v | cond |             | c |    (u = 0)
 *   +------+------+-------------+---+
 *
 *      4       4           24
 *   +------+------+-----------------+
 *   | 110v | cond | offset (signed) |    (u = 1)
 *   +------+------+-----------------+
 *
 * If u = 0, PC is set to the contents of register R.c. Else, the jump target
 * is determined using PC-relative addressing, with the length of the jump
 * forward or backward being contained as a signed constant in the instruction.
 * In addition, if v = 1, the current value of PC is stored in the Link
 * register (R15), serving as a return address for procedure calls. In total,
 * this scheme produces the following four opcodes:
 *
 *  BR  (u = 0, v = 0), BLR (u = 0, v = 1),
 *  BC  (u = 1, v = 0), BL  (u = 1, v = 1)
 * 
 * Instructions are word-addressed, and all jumps are conditional on cond
 * (see the definition of the kCond(...) constants for the possible values).
 */
static void
Put3(const int op, int cond, int off)
{
  assert(0 <= cond && cond <= 15);

  g_mem[g_pc++] = ((op + 12) << 28) | (cond << 24) | (off & 0xFFFFFF);
}

/* Increments the register stack index RH (one of R0 - R11). */
static void
IncR(void)
{
  /* Note we did not write g_rh < kRegMT. RH does not merely point to one
   * register beyond the last one written to, but is also sometimes itself
   * used as a scratchpad. Therefore, it may never be the case that RH = MT.
   */
  if (g_rh < kRegMT - 1) {
    ++g_rh;
  } else {
    ORS_Mark("register stack overflow");
  }
}

static void
SetCC(item_t * const x, const int cond)
{
  assert(x);

  x->mode = kModeCond;
  x->a = x->b = 0;
  x->r = cond;
}

/* Conditional jump to a designated address that will cause the interpreter
 * to print an appropriate error message at runtime (e.g., division by zero,
 * or index out of bounds).
 */
static void
Trap(const int cond, const int addr)
{
  assert(addr < 0);

  Put3(kOpBc, cond, addr - g_pc - 1);
}

static inline int Negated(const int cond)
{
  /* Note there are 16 condition codes, with their numeric identifiers 0-15
   * having been so assigned as to have cond1 and cond2 be duals iff the
   * distance therebetween is 8. E.g., kCondTrue == 7 while kCondFalse == 15.
   */
  return (cond < 8) ? cond + 8 : cond - 8;
}

/* Sets the offset field of an instruction in format F3 at address 'at' to
 * (the 24 lower-order bits of) 'with'. (See also Put3.)
 */
static void
Fix(const int at, const int with)
{
  /* Assert the instruction at g_mem[at] has format F3. */
  assert(((g_mem[at] >> 30) & 0x3) == 3);

  g_mem[at] = (g_mem[at] & ~0xFFFFFF) | (with & 0xFFFFFF);
}

static void
FixLinkWith(int l0, const int dst)
{
  int     l1;

  while (l0 != 0) {
    /* Store link to next instruction */
    l1 = g_mem[l0] & 0xFFFFFF;

    /* Fix offset */
    Fix(l0, dst-l0-1);

    /* Move on to next instruction */
    l0 = l1;
  }
}

/*
 * Consider a boolean expression expr1 & expr2 or expr1 OR expr2. As part of
 * parsing expr1 a non-fixed forward branch instruction was emitted. Let L1 be
 * the address thereof. Similarly, if expr2 is itself a non-atomic boolean
 * expression, then the code generated for its lefthand operand will also
 * conclude with a (non-fixed, forward) branch instruction. Let L0 be the
 * address thereof, or else 0 if expr2 is atomic (see IntRel and LoadCond,
 * which both set x->a and x->b to 0). If L0 is non-zero, the instruction
 * contained thereat will contain a link in its offset. Merged follows this
 * chain and sets the offset of its terminating instruction to L1, thus
 * 'merging' the two chains together. It will return the address of the
 * instruction at the start of the new chain; i.e., L0 if non-zero, or else L1.
 */
static int
Merged(const int l0, int l1)
{
  int     l2, l3;

  assert(l1 != 0);

  if (l0 != 0) {
    /* Find last link in chain and store in l2 */
    l3 = l0;
    do {
      l2 = l3;
      l3 = g_mem[l2] & 0x3FFFF;
    } while (l3 != 0);

    /* Set offset of instruction at l2 */
    g_mem[l2] += l1;

    l1 = l0;
  }
  return l1;
}

/* Loading of operands and addresses into registers */

static void
NilCheck(void)
{
  Trap(kCondEQ, -1);
}

/* Loads a value into RH; i.e., the register used for the top of the stack. */
static void
Load(item_t * const x)
{
  int     op;

  assert(x);

  /* Nothing to do if already loaded. */
  if (x->mode == kModeReg) {
    return;
  }

  /* Set the u modifier bit (see Put2) if the value to be loaded is 1 byte in
   * size (i.e., when a BYTE, BOOLEAN or CHAR).
   */
  op = (x->type->size == 1) ? kOpLdr + 1 : kOpLdr;

  if (x->mode == kModeRegI) {
    /* x->r identifies the register containing the base address, while x->a
     * contains an offset.
     */
    Put2(op, x->r, x->r, x->a);                   /* R.x := Mem[R.x + x.a] */
  } else {
    if (x->mode == kModeImmediate) {
      if (x->type->tag == kTypeProc) {
        if (x->r > 0) {
          ORS_Mark("not allowed");
        } else {
          assert(x->r == 0);
          Put3(kOpBl, kCondTrue, 0);              /* LNK := PC+1, PC := PC+1 */
          Put1a(kOpSub,g_rh,kRegLNK,g_pc*4-x->a); /* RH := LNK - (PC*4 - x) */
        }
      } else if (x->a <= 0x0FFFF && x->a >= -0x10000) {
        /* Noting x->a is a 32-bits signed integer, the 16 most significant
         * bits of 0x0FFFF are all zeroes, whereas those of -0x10000 are all
         * ones. Though Put1 only retains the 16 least significant bits of x->a
         * in its operand im, it also sets a modifier bit v if x->a < 0,
         * indicating sign extension is to be used when widening im back to a
         * 32 bits (signed) word.
         */
        Put1(kOpMov, g_rh, 0, x->a);
      } else {
        /* The im field is only 16-bits wide. For loading constants that fit
         * only in >16 bits, we emit two instructions for loading the 16 most-
         * and least significant bits separately.
         */

        /* First emit a MOV for only the 16 most significant bits, shifted to
         * the left. Note setting the u bit means they will be shifted back to
         * the right again when stored in RH.
         */
        Put1(kOpMov + kModU, g_rh, 0, (x->a >> 16) & 0xFFFF);

        /* Next add the 16 least significant bits using a bitwise or. (Needed
         * only if non-zero.) */
        if (x->a & 0xFFFF) {
          Put1(kOpIor, g_rh, g_rh, x->a & 0xFFFF);
        }
      }
    } else if (x->mode == kModeDirect) {
      if (x->r > 0) {
        /* Local variable */
        Put2(op, g_rh, kRegSP, x->a + g_frame);   /* RH := Mem[SP + x] */
      } else {
        /* Global variable */
        Put2(op, g_rh, kRegSB, x->a);             /* RH := Mem[SB + x] */
      }
    } else if (x->mode == kModeParam) {
      /* Read base address from local variable offset and store in RH */
      Put2(kOpLdr, g_rh, kRegSP, x->a + g_frame); /* RH := Mem[SP + x] */

      /* Apply second offset */
      Put2(op, g_rh, g_rh, x->b);                 /* RH := Mem[RH + x.b] */
    } else if (x->mode == kModeCond) {
      Put3(kOpBc, Negated(x->r), 2);              /* If !x, jump to False */
      ORG_FixLink(x->b);                          /* Label True: */
      Put1(kOpMov, g_rh, 0, 1);                   /* RH := 1 */
      Put3(kOpBc, kCondTrue, 1);                  /* Unconditional jump to L */
      ORG_FixLink(x->a);                          /* Label False: */
      Put1(kOpMov, g_rh, 0, 0);                   /* RH := 0 */
                                                  /* Label L: */
    } else {
      assert(0);
    }

    /* Remember the register where R is stored and increment RH */
    x->r = g_rh;
    IncR();
  }

  /* Adjust mode */
  x->mode = kModeReg;
}

/* Load the absolute address of x into RH */
static void
LoadAdr(item_t * const x)
{
  assert(x);

  switch(x->mode) {
  case kModeDirect:
    if (x->r > 0) {
      /* Local variable */
      Put1a(kOpAdd, g_rh, kRegSP, x->a + g_frame);  /* RH := SP + x */
    } else {
      /* Global variable */
      Put1a(kOpAdd, g_rh, kRegSB, x->a);
    }
    x->r = g_rh;
    IncR();
    break;
  case kModeParam:
    Put2(kOpLdr, g_rh, kRegSP, x->a + g_frame);     /* RH := Mem[SP + x] */
    if (x->b != 0) {
      Put1a(kOpAdd, g_rh, g_rh, x->b);              /* RH := RH + x.b */
    }
    x->r = g_rh;
    IncR();
    break;
  case kModeRegI:
    /* Add offset to the address that's already in a register */
    if (x->a != 0) {
      Put1a(kOpAdd, x->r, x->r, x->a);              /* R.x := R.x + x.a */
    }
    break;
  default:
    ORS_Mark("address error");
  }
  x->mode = kModeReg;
}

static void
LoadCond(item_t * const x)
{
  assert(x);

  if (x->type->tag != kTypeBool) {
    ORS_Mark("not a Boolean?");
    return;
  }

  if (x->mode == kModeImmediate) {
    assert(x->a == 0 || x->a == 1);
    x->r = 15 - x->a * 8;
    /* I.e., if x->a == 1, x->r == 7 == kCondTrue. Else, if x->a == 0,
     * x->r == 15 == kCondFalse.
     */
  } else {
    /* Emit a CMP (== SUB) to set the condition registers and discard the
     * result.
     */
    Load(x);
    Put1(kOpCmp, x->r, x->r, 0);
    x->r = kCondNE;
    --g_rh;
  }
  x->mode = kModeCond;
  x->a = x->b = 0;
}

static void
LoadStringAdr(item_t * const x)
{
  assert(x);

  /* varsize contains the size of the global variable declarations, and x->a
   * the offset therefrom containing the first string byte */
  Put1a(kOpAdd, g_rh, kRegSB, g_varsize + x->a); /* RH := SB + varsize + x.a */
  x->mode = kModeReg;
  x->r = g_rh;
  IncR();
}

static int
Log2(int m, int * exp)
{
  *exp = 0;
  while (!(m % 2)) {
    m /= 2;
    ++*exp;
  }
  return m;
}

static void
Store(item_t * const x, const int r)
{
  int     op;

  assert(x);

  /* If x is a CHAR, BYTE or BOOLEAN, store only one byte; else, a word. */
  op = (x->type->size == 1) ? kOpStr + 1 : kOpStr;

  switch (x->mode) {
  case kModeDirect:
    if (x->r > 0) {
      /* Local variable (Mem[SP + x.offset + frame] := R.y) */
      Put2(op, r, kRegSP, x->a + g_frame);
    } else {
      /* Global variable */
      Put2(op, r, kRegSB, x->a);
    }
    break;
  case kModeParam:
    Put2(kOpLdr, g_rh, kRegSP, x->a + g_frame); /* RH := Mem[SP + x.a] */
    Put2(op, r, g_rh, x->b);                    /* Mem[RH + x.b] := R.y */
    break;
  case kModeRegI:
    Put2(op, r, x->r, x->a);
    --g_rh;
    break;
  default:
    ORS_Mark("bad mode in Store");
  }
}

/* Saves the contents of registers R0,...,R(r-1) before a procedure call */
static void
SaveRegs(const int r)
{
  int     r0;

  assert(r > 0);

  /* Allocate memory */
  Put1(kOpSub, kRegSP, kRegSP, r*4);        /* SP := SP - (r*4) */

  /* Increment frame offset; i.e., the base address (combined with SP) for
   * referencing local variables */
  g_frame += 4 * r;

  /* Store register values */
  r0 = 0;
  do {
    Put2(kOpStr, r0, kRegSP, (r-1-r0)*4);   /* Mem[SP + (r-1-r0)*4] := r0 */
  } while (++r0 != r);
}

/* Restores the contents of registers R(r-1),...,R0 after a procedure call */
static void
RestoreRegs(const int r)
{
  int     r0;

  assert(r > 0);

  /* Restore register values */
  r0 = r;
  do {
    --r0;
    Put2(kOpLdr, r0, kRegSP, (r-1-r0)*4);   /* r0: = Mem[SP + (r-1-r0)*4] */
  } while (r0 != 0);

  /* Deallocate memory */
  Put1(kOpAdd, kRegSP, kRegSP, r*4);        /* SP := SP + (r*4) */

  /* Decrement frame offset */
  g_frame -= 4 * r;
}
