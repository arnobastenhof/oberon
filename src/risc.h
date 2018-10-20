/* Ported from RISC.Mod.txt, part of the Oberon07 reference implementation. */

#ifndef RISC_H_
#define RISC_H_

#include <stdint.h>

enum {
  /* Reserved registers */
  kRegMT  = 12,                 /* Module Table (unused, but kept reserved)  */
  kRegSB  = 13,                 /* Static Base (offset for global variables) */
  kRegSP  = 14,                 /* Stack Pointer (offset for local vars)     */
  kRegLNK = 15,                 /* Link register (return address)            */

  /* Memory size (in bytes) */
  kMemSz = 4096,

  /* Instruction decoding */
  kInsnMsb = ~0x7FFFFFFF,       /* Most significant bit                      */
  kInsnQ   =  0x40000000,       /* Second most significant bit               */
  kInsnU   =  0x20000000,       /* Third most significant bit                */
  kInsnV   =  0x10000000,       /* Fourth most significant bit               */

  /* Conditions for use with branch instructions */
  kCondMI = 0,                  /* negative (minus)   N                      */
  kCondEQ = 1,                  /* equal (zero)       Z                      */
  kCondCS = 2,                  /* carry set          C                      */
  kCondVS = 3,                  /* overflow set       V                      */
  kCondLS = 4,                  /* less or same       ~C | Z                 */
  kCondLT = 5,                  /* less than          N != V                 */
  kCondLE = 6,                  /* less or equal      (N != V) | Z           */
  kCondTrue = 7,                /* always             1                      */
  kCondPL = 8,                  /* positive (plus)    ~N                     */
  kCondNE = 9,                  /* not equal (zero)   ~Z                     */
  kCondCC = 10,                 /* carry clear        ~C                     */
  kCondVC = 11,                 /* overflow clear     ~V                     */
  kCondHI = 12,                 /* high               ~(~C | Z)              */
  kCondGE = 13,                 /* greater or equal   ~(N != V)              */
  kCondGT = 14,                 /* greater than       ~((N != V) | Z)        */
  kCondFalse = 15,              /* never              0                      */

  /* Special code addresses used for runtime checks */
  kTrapNilPtr           = -1,   /* Dereferencing a null pointer              */
  kTrapIndexOutOfBounds = -2,   /* Index out of bounds                       */
  kTrapDivByZero        = -3,   /* Division by zero                          */
  kTrapAssert           = -4,   /* Assertion failure                         */
  kTrapIO               = -5,   /* I/O exception                             */

  /* Opcodes for register instructions; see Put0 (n is c) and Put1 (n is im) */
  kOpMov = 0,   /* MOV a, n       R.a := n                                   */
  kOpLsl = 1,   /* LSL a, b, n    R.a := R.b << n                            */
  kOpAsr = 2,   /* ASR a, b, n    R.a := R.b >> n  (w. sign extension)       */
  kOpRor = 3,   /* ROR a, b, n    R.a := R.b rot n (right rotate)            */
  kOpAnd = 4,   /* AND a, b, n    R.a := R.b & n                             */
  kOpAnn = 5,   /* ANN a, b, n    R.a := R.b & ~n                            */
  kOpIor = 6,   /* IOR a, b, n    R.a := R.b | n                             */
  kOpXor = 7,   /* XOR a, b, n    R.a := R.b ^ n                             */
  kOpAdd = 8,   /* ADD a, b, n    R.a := R.b + n                             */
  kOpSub = 9,   /* SUB a, b, n    R.a := R.b - n                             */
  kOpMul = 10,  /* MUL a, b, n    R.a := R.b * n                             */
  kOpDiv = 11,  /* DIV a, b, n    R.a := R.b / n   (integer division)        */
  kOpCmp = 9,   /* Synonym of SUB when used for comparison purposes only     */

  /* Opcodes for memory instructions, used with Put2. Note both have two
   * variants: kOpLdr and kOpStr operate on words, whereas kOpLdr + 1 and
   * kOpStr + 1 operate on bytes (see also Put2).
   */
  kOpLdr = 8,   /* LDR a, b, off  R.a := Mem[R.b + off]                      */
  kOpStr = 10,  /* STR a, b, off  Mem[R.b + off] := R.a                      */

  /* Opcodes for branch instructions, used with Put3. */
  kOpBr  = 0,   /* BR  cond, c    if (cond) { PC := R.c }                    */
  kOpBlr = 1,   /* BLR cond, c    if (cond) { R15 := PC+1, PC := R.c }       */
  kOpBc  = 2,   /* BC  cond, off  if (cond) { PC := PC+1+off }               */
  kOpBl  = 3    /* BL  cond, off  if (cond) { R15 := PC+1, PC := PC+1+off }  */
};

/* Exported functions */
extern void       RISC_Interpret(const int, const int);

/* Exported data */
extern int32_t    g_mem[kMemSz / 4];  /* Memory */

#endif
