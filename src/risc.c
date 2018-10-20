/* Ported from RISC.Mod.txt, part of the Oberon07 reference implementation. */

#include "risc.h"

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

enum {
  /* Condition registers (bitmasks) */
  kFlagN    =  0x8,                       /* Negative */
  kFlagZ    =  0x4,                       /* Zero */
  kFlagC    =  0x2,                       /* Carry / borrow */
  kFlagV    =  0x1,                       /* oVerflow */

  /* Misc. */
  kNumSign  = ~0x7FFFFFFF,                /* Mask for extracting sign bit */
  kMaxSteps = 100000                      /* Max no. of insns to execute */
};

/* Prototypes */
static void         Dump(void);           /* Dumps VM state to standard out */
static inline void  SetN(const int64_t);  /* Set N flag */
static inline void  SetZ(const int64_t);  /* Set Z flag */
static void         WriteStr(const int);  /* Write a string to stdout */
static bool         IsTrue(const int);    /* Tests a jump condition */

/* Memory */
int32_t             g_mem[kMemSz/4];

/* Registers */
static int          g_pc;                 /* Program counter */
static int32_t      g_ir;                 /* Instruction register */
static int32_t      g_reg[16];            /* General-purpose registers r0-15 */
static int32_t      g_h;                  /* For storing remainders */
static uint8_t      g_cond;               /* Condition flags [N, Z, C, V] */

/* Runtime error messages */
static const char * const g_trap[] = {
  "",
  "nil pointer",
  "index out of bounds",
  "division by zero",
  "assert failure",
  "I/O exception"
};

void
RISC_Interpret(const int sb, const int entry)
{
  int64_t           val;    /* Result value of register instructions */
  int32_t           b;      /* Operand R.b (F0, F1) or base address (F2) */
  int32_t           n;      /* Operand R.c (F0), im (F1) or abs address (F2) */
  int               a;      /* Result register R.a (F0-2) or condition (F3) */
  int               op;     /* Opcode */
  int               cnt;    /* Number of executed instructions */

  /* Initialization */
  g_pc = entry;             /* Code address to fetch 1st insn from */
  g_cond = 0;               /* Set all flags (N, Z, C, V) to 0 */
  cnt = 0;
  g_reg[kRegSB] = sb * 4;   /* Globals start after code */
  g_reg[kRegSP] = kMemSz;   /* The stack grows downward */
  g_reg[kRegLNK] = 0;       /* A jump to 0 terminates the interpreter */

  do {
    /* Fetch instruction */
    g_ir = g_mem[g_pc++];

    /* Register (F0,F1,F2) or condition (F3) */
    a = (g_ir >> 24) & 0xF;

    if (!(g_ir & kInsnMsb) || !(g_ir & kInsnQ)) {
      /* Register- and memory instructions */

      /* Operand (F0, F1) or base address (F2) */
      b = g_reg[(g_ir >> 20) & 0xF];

      if (!(g_ir & kInsnMsb)) {
        /* Register instruction (F0, F1) */

        /* Extract opcode */
        op = (g_ir >> 16) & 0xF;

        /* Second operand */
        if (!(g_ir & kInsnQ)) {
          /* Format F0 */
          n = g_reg[g_ir & 0xF];
        } else {
          /* Format F1 */
          n = g_ir & 0xFFFF;

          /* Sign-extend if the v modifier bit is set */
          if (g_ir & kInsnV) {
            n += 0xFFFF0000;
          }
        }

        /* Switch on opcode */
        switch (op) {
        case kOpMov:
          /* If u = 1, form = 0 and v = 0, R.a := H */
          /* If u = 1, form = 0 and v = 1, R.a := [N, Z, C, V] */
          /* If u = 1, form = 1, R.a := n << 16 */
          /* If u = 0, R.a := n */
          if (g_ir & kInsnU) {
            if (g_ir & kInsnQ) {
              val = n << 16;
            } else {
              val = (g_ir & kInsnV) ? g_cond : g_h;
            }
          } else {
            val = n;
          }
          break;

        case kOpRor:
          val = (((b >> n) & ~(~0 << (32 - n))) | (b << (32 - n)));
          break;

        case kOpLsl: val = b << n; break;
        case kOpAsr: val = b >> n; break;
        case kOpAnd: val = b & n;  break;
        case kOpAnn: val = b & ~n; break;
        case kOpIor: val = b | n;  break;
        case kOpXor: val = b ^ n;  break;

        case kOpAdd:
          val = b + n;

          /* Set oVerflow flag */
          if ((b & kNumSign) && (n & kNumSign) && !(val & kNumSign)) {
            /* (+A)+(+B) = -C */
            g_cond |= kFlagV;
          } else if (!(b & kNumSign) && !(n & kNumSign) && (val & kNumSign)) {
            /* (-A)+(-B) = +C */
            g_cond |= kFlagV;
          } else {
            g_cond &= ~kFlagV;
          }
          break;

        case kOpSub:
          val = b - n;

          /* Set oVerflow flag */
          if ((b & kNumSign) && !(n & kNumSign) && !(val & kNumSign)) {
            /* (+A)-(-B) = -C */
            g_cond |= kFlagV;
          } else if (!(b & kNumSign) && (n & kNumSign) && (val & kNumSign)) {
            /* (-A)-(+B) = +C */
            g_cond |= kFlagV;
          } else {
            g_cond &= ~kFlagV;
          }
          break;

        case kOpMul:
          val = b * n;
          g_h = (val >> 32) & 0xFFFFFFFF;
          break;

        case kOpDiv:
          /* Code generator already forces runtime checks */
          assert(n != 0);

          val = b / n;
          g_h = b % n;
          break;

        default:
          assert(0);
        }
        /* Store value in result register */
        g_reg[a] = val & 0xFFFFFFFF;

        /* Set condition flags */
        SetN(val);
        SetZ(val);
      } else {
        /* Memory instruction (format F2) */
        n = b + (g_ir & 0xFFFFF);
        if (g_ir & kInsnU) {
          if (n >= 0) {
            /* Store */
            if (g_ir & kInsnV) {
              /* Store a single byte */
              g_mem[n / 4] |= (g_reg[a] & 0xFF) << ((n % 4) * 8);
            } else {
              /* Store a word */
              g_mem[n / 4] = g_reg[a];
            }
          } else {
            /* Output */
            if (n == -1) {
              /* Write an integer */
              if (printf("%d", g_reg[a]) < 0) {
                g_pc = kTrapIO;
              }
            } else if (n == -2) {
              /* Write a character */
              if (putchar(g_reg[a]) == EOF) {
                g_pc = kTrapIO;
              }
            } else if (n == -3) {
              /* Write a string */
              WriteStr(a);
            } else if (n == -4) {
              /* Write a newline character */
              if (putchar('\n') == EOF) {
                g_pc = kTrapIO;
              }
            } else {
              assert(0);
            }
          }
        } else {
          if (n >= 0) {
            /* Load */
            if (g_ir & kInsnV) {
              /* Load a byte */
              val = (g_mem[n / 4] >> ((n % 4) * 8)) & 0xFF;
            } else {
              /* Load a word */
              val = g_mem[n / 4];
            }
            g_reg[a] = val & 0xFFFFFFFF;

            /* Set condition flags */
            SetN(val);
            SetZ(val);
          } else {
            /* Input */
            if (n == -1) {
              /* Read an integer */
              if (scanf("%d", g_reg + a) == EOF) {
                g_pc = kTrapIO;
              }
            } else if (n == -2) {
              /* Read a character */
              if ((g_reg[a] = getchar()) == EOF) {
                g_pc = kTrapIO;
              }
            } else {
              assert(0);
            }
          }
        }
      }
    } else {
      /* Branch instruction */
      if (IsTrue(a)) {
        if (g_ir & kInsnV) {
          /* Store return address in LNK register */
          g_reg[kRegLNK] = g_pc * 4;
        }
        if (g_ir & kInsnU) {
          /* If u = 1, read offset from instruction (could be < 0) */
          g_pc += ((g_ir & 0xFFFFFF) << 16) >> 16;

        } else {
          /* If u = 0, take the destination address from a register */
          g_pc = g_reg[g_ir & 0xF] / 4;
        }
      }
    }
  } while (g_pc > 0 && g_pc < sb && ++cnt != kMaxSteps);

  /* Check for a runtime error */
  if (g_pc != 0) {
    if (cnt == kMaxSteps) {
      fprintf(stderr, "Execution aborted\n");
    } else if (g_pc < 0 && g_pc >= kTrapIO) {
      fprintf(stderr, "Trap: %s\n", g_trap[abs(g_pc)]);
    } else {
      fprintf(stderr, "Illegal code address: %06x\n", g_pc);
    }
    Dump();
  }
}

static void
Dump(void)
{
  int m, n;

  /* Print special-purpose registers */
  printf("Registers:\n");
  printf("PC,      IR,      N,       Z,       C,       V\n");
  printf("%08x,%08x,",    g_pc,                 g_ir);
  printf("%08x,%08x,",    g_cond & kFlagN >> 3, g_cond & kFlagZ >> 2);
  printf("%08x,%08x\n\n", g_cond & kFlagC >> 1, g_cond & kFlagV);

  /* Print general-purpose registers */
  printf("R0,      R1,      R2,      R3,      R4,      R5,      R6,      R7");
  printf("\n%08x,%08x,%08x,%08x,", g_reg[0], g_reg[1], g_reg[2], g_reg[3]);
  printf("%08x,%08x,%08x,%08x\n\n",g_reg[4], g_reg[5], g_reg[6], g_reg[7]);
  printf("R8,      R9,      R10,     R11,     MT,      SB,      SP,      LNK");
  printf("\n%08x,%08x,%08x,%08x,", g_reg[8],  g_reg[9],  g_reg[10], g_reg[11]);
  printf("%08x,%08x,%08x,%08x\n\n",g_reg[12], g_reg[13], g_reg[14], g_reg[15]);

  /* Print memory contents */
  printf("Memory:\n");
  printf("       00000000,00000004,00000008,0000000C,"
         "00000010,00000014,00000018,0000001C\n");
  for (n = 0; n < kMemSz; n += 32) {
    printf("%06x ", n);
    for (m = 0; n + m != kMemSz && m != 32; m += 4) {
      printf("%08x", g_mem[(n + m) / 4]);
      if (m != 28) {
        putchar(',');
      }
    }
    putchar('\n');
  }
  putchar('\n');
}

static void
SetN(const int64_t val)
{
  if (val < 0) {
    g_cond |= kFlagN;
  } else {
    g_cond &= ~kFlagN;
  }
}

static void
SetZ(const int64_t val)
{
  /* Set Z flag */
  if (val == 0) {
    g_cond |= kFlagZ;
  } else {
    g_cond &= ~kFlagZ;
  }
}

static void
WriteStr(const int a)
{
  int           i;            /* Number of bits written */
  int           ch;
  int           base;         /* Word address */

  base = g_reg[a] / 4;
  i = 0;
  do {
    ch = (g_mem[base] >> i) & 0xFF;
    if (putchar(ch) == EOF) {
      g_pc = kTrapIO;
      return;
    }
    i += 8;
    if (i == 32) {
      i = 0;
      ++base;
    }
  } while (ch);
}

static bool
IsTrue(const int cond)
{
  bool flagN = g_cond & kFlagN;
  bool flagZ = g_cond & kFlagZ;
  bool flagV = g_cond & kFlagV;

  return (cond == kCondTrue)                                ||
         (cond == kCondMI && flagN)                         ||
         (cond == kCondEQ && flagZ)                         ||
         (cond == kCondVS && flagV)                         ||
         (cond == kCondLT && (flagN != flagV))              ||
         (cond == kCondLE && ((flagN != flagV) || flagZ))   ||
         (cond == kCondPL && !flagN)                        ||
         (cond == kCondNE && !flagZ)                        ||
         (cond == kCondVC && !flagV)                        ||
         (cond == kCondGE && (flagN == flagV))              ||
         (cond == kCondGT && ((flagN == flagV) && !flagZ));
}
