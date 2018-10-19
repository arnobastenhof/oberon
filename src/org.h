/* Ported from ORG.Mod.txt, part of the Oberon07 reference implementation. */

#ifndef ORG_H_
#define ORG_H_

#include <stdbool.h>
#include <stdint.h>

#include "orb.h"

/*
 * Addressing modes, serving as tags of items. The columns below specify the
 * contents of the item fields for each mode.
 */
typedef enum {
/* Constant           | Name              | a        | b       | r         */
/* -------------------+-------------------+----------+---------+---------- */
   kModeImmediate, /* | Immediate         | value    | -       | -         */
   kModeDirect,    /* | Direct            | rel addr |         | level     */
   kModeParam, /* (1) | VAR params        | offset1  | offset2 | -         */
   kModeType, /*      | -                 | -        | -       | -         */
   kModeReg,       /* | Register          | -        | -       | register  */
   kModeRegI,      /* | Register-Indirect | offset   | -       | register  */
   kModeCond   /* (2) | Jump conditions   | F-chain  | T-chain | condition */
} mode_t;
/* Notes:
 * 1 Offset1 is relative to SP and points at a memory cell (corresponding to
 *   a local variable) containing an absolute address that acts as the base
 *   for offset2. The latter is by default set to 0, but may be modified when
 *   processing selectors (specifically for array indexing and record fields).
 * 2 F-chain and T-chain refer to sequences of branch instructions (format F3),
 *   chained together through their offsets. Specifically, the instructions
 *   in an F-chain initiate a jump if their condition is False (being generated
 *   for the lefthand operands of &'s and for conditional- and loop statements)
 *   while those in a T-chain jump if their condition is True (generated for
 *   the lefthand operands of ORs).
 */

typedef struct {
  mode_t        mode;
  type_t *      type;
  int32_t       a;
  int           b;
  int           r;
  bool          rdo;   /* Whether read-only */
} item_t;

/* Item creation */
extern void     ORG_MakeConst(item_t * const, type_t * const, const int);
extern void     ORG_MakeString(item_t * const, int);
extern void     ORG_MakeItem(item_t * const, object_t * const, const int);

/* Code generation */
extern void     ORG_CheckRegs(void);
extern void     ORG_FixOne(const int);
extern void     ORG_FixLink(int);
extern void     ORG_Field(item_t * const, object_t * const);
extern void     ORG_Index(item_t * const, item_t * const);
extern void     ORG_Deref(item_t * const);
extern void     ORG_Not(item_t * const);
extern void     ORG_And1(item_t * const);
extern void     ORG_And2(item_t * const, item_t * const);
extern void     ORG_Or1(item_t * const);
extern void     ORG_Or2(item_t * const, item_t * const);
extern void     ORG_Neg(item_t * const);
extern void     ORG_AddOp(const int, item_t * const, item_t * const);
extern void     ORG_MulOp(item_t * const, item_t * const);
extern void     ORG_DivOp(const int, item_t * const, item_t * const);
extern void     ORG_Singleton(item_t * const);
extern void     ORG_Set(item_t * const, item_t * const);
extern void     ORG_In(item_t * const, item_t * const);
extern void     ORG_SetOp(const int, item_t * const, item_t * const);
extern void     ORG_IntRel(const int, item_t * const, item_t * const);
extern void     ORG_StringRel(const int, item_t * const, item_t * const);
extern void     ORG_StrToChar(item_t * const);
extern void     ORG_Store(item_t * const, item_t * const);
extern void     ORG_StoreStruct(item_t * const, item_t * const);
extern void     ORG_CopyString(item_t * const, item_t * const);
extern void     ORG_OpenArrayParam(item_t * const, const bool);
extern void     ORG_VarParam(item_t * const, type_t * const);
extern void     ORG_ValueParam(item_t * const);
extern void     ORG_StringParam(item_t * const);
extern void     ORG_For0(item_t * const);
extern int      ORG_For1(item_t * const, item_t * const, item_t * const,
                  item_t * const);
extern void     ORG_For2(item_t * const, item_t * const);
extern int      ORG_Here(void);
extern void     ORG_FJump(int * const);
extern void     ORG_CFJump(item_t * const);
extern void     ORG_BJump(const int);
extern void     ORG_CBJump(item_t * const, const int);
extern void     ORG_Fixup(item_t * const);
extern int      ORG_PrepCall(item_t * const);
extern void     ORG_Call(item_t * const, int);
extern void     ORG_Enter(const int, const int);
extern void     ORG_Return(const form_t, item_t * const, const int);
extern void     ORG_Increment(const bool, item_t * const, item_t * const);
extern void     ORG_Include(const bool, item_t * const, item_t * const);
extern void     ORG_Assert(item_t * const);
extern void     ORG_Read(item_t * const);
extern void     ORG_Write(const bool, item_t * const);
extern void     ORG_Get(const bool, item_t * const, item_t * const);
extern void     ORG_Copy(item_t * const, item_t * const, item_t * const);
extern void     ORG_Abs(item_t * const);
extern void     ORG_Odd(item_t * const);
extern void     ORG_Ord(item_t * const);
extern void     ORG_Len(item_t * const);
extern void     ORG_Shift(const int, item_t * const, item_t * const);
extern void     ORG_Bit(item_t * const, item_t * const);
extern void     ORG_Register(item_t * const);
extern void     ORG_Adr(item_t * const);
extern void     ORG_Condition(item_t * const);
extern void     ORG_Open(void);
extern void     ORG_SetDataSize(const int);
extern void     ORG_Header(void);
extern void     ORG_Close(void);

/* Assembly */
extern void     ORG_Decode(void);

#endif
