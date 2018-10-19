/* Ported from ORP.Mod.txt, part of the Oberon07 reference implementation. */

#include "orp.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "except.h"
#include "orb.h"
#include "ors.h"
#include "org.h"
#include "risc.h"
#include "pool.h"

/* Type declarations for pointers in Oberon may forward-reference their base
 * types, as in, e.g.,
 *
 * TYPE
 *   Node = POINTER TO NodeDesc;
 *   NodeDesc = RECORD OF link : NODE END;
 *
 * On parsing the declaration for Node, NodeDesc refers to an as of yet
 * unseen record type. At this point the reference is stored in a linked list
 * and will be resolved after all declarations have been parsed.
 */

typedef struct ptrBase_s ptrBase_t;

struct ptrBase_s {
  ptrBase_t * link;
  char        name[kIdLen]; /* Name of the unresolved base type (NodeDesc) */
  type_t *    type;         /* The pointer type referencing name (Node) */
};

/* Prototypes */
static inline void   Consume(void);
static inline void   Expect(const int, const char * const);
static inline void   CheckBool(item_t * const);
static inline void   CheckInt(item_t * const);
static inline void   CheckSet(item_t * const);
static void          CheckSetVal(item_t * const);
static inline void   CheckBasicType(item_t * const);
static void          CheckConst(item_t * const);
static void          CheckReadOnly(item_t * const);
static bool          CheckExport(void);
static object_t *    QualIdent(void);
static inline bool   MatchSelector(const int);
static void          Selector(item_t * const);
static bool          IsExtension(type_t * const, type_t * const);
static inline bool   MatchingArrayTypes(type_t * const, type_t * const);
static inline bool   MatchingProcTypes(type_t * const, type_t * const);
static inline bool   MatchingRecordTypes(type_t * const, type_t * const);
static inline bool   MatchingPointerTypes(type_t * const, type_t * const);
static bool          EqualSignatures(type_t * const, type_t * const);
static bool          CompatibleTypes(type_t * const, type_t * const, bool);
static inline bool   IsOpenArray(type_t * const);
static void          Parameter(object_t * const params);
static void          ParamList(item_t * const);
static void          StandFunc(item_t * const, int, type_t *);
static void          Element(item_t * const);
static void          Set(item_t * const);
static inline bool   MatchExpr(const int);
static void          Factor(item_t * const);
static inline bool   IsMulOperator(const int);
static void          Term(item_t * const);
static inline bool   IsAddOperator(const int);
static void          SimpleExpr(item_t * const);
static bool          IsCharArray(item_t * const);
static void          Expr(item_t * const);
static void          StandProc(int32_t);
static void          AssignmentStmt(item_t * const);
static void          IfStmt(void);
static void          WhileStmt(void);
static void          Stmt(void);
static inline bool   MatchStmt(const int);
static void          StmtSequence(void);
static object_t *    IdentList(const int);
static int           Align(const int);
static type_t *      ArrayType(void);
static type_t *      RecordType(void);
static type_t *      FormalType(const int);
static void          CheckRecLevel(const int);
static type_t *      PointerType(void);
static int           FPSection(int * const);
static type_t *      ProcedureType(int * const);
static inline bool   MatchType(const int);
static type_t *      Type(void);
static void          ConstDecl(void);
static void          VarDecl(int * const);
static void          TypeDecl(void);
static inline bool   MatchDecl(const int);
static void          Declarations(int * const);
static void          ProcedureDecl(void);
static void          Procedures(void);
static void          Module(void);

/* Global exception handler */
jmp_buf * g_handler;

/* Private data */
static int           g_sym;      /* Last read symbol */
static int           g_dc;       /* Data counter (for variable declarations) */
static int           g_level;    /* Incremented on entering procedures */
static ptrBase_t *   g_pbs_list; /* List of ptr base type forward-references */
static int           g_sb;       /* Start address for globals */
static int           g_entry;    /* Address of first instruction to execute */

/*
 * Dummy object used to continue parsing after failing to look up an
 * identifier in the current scope.
 */
static object_t      g_dummy = {
  NULL, NULL, &g_int_type, "dummy", kObjVar, 0, false, false, 0
};

void
ORP_Compile(const char * const fname, const int sflag)
{
  /* Initialize lexer */
  ORS_Init(fname);

  TRY
    /* Push arena for globals */
    Pool_Push();

    /* Initialize universe */
    ORB_Init();

    /* Parse */
    Module();

    /* Pop globals arena */
    Pool_Pop();
  CATCH
    /* Out of memory */
    ++g_errcnt;
  END

  /* Free lexer */
  ORS_Free();

  if (g_errcnt == 0) {
    if (sflag) {
      /* Print assembly */
      ORG_Decode();
    } else {
      /* Run interpreter */
      RISC_Interpret(g_sb, g_entry);
    }
  } else {
    fprintf(stderr, "compilation FAILED\n");
  }
}

/* Reads the next symbol */
static inline void
Consume(void)
{
  g_sym = ORS_Get();
}

/* Tests the current symbol */
static inline void
Expect(const int exp, const char * const msg)
{
  assert(msg);

  if (exp == g_sym) {
    Consume();
  } else {
    ORS_Mark(msg);
  }
}

/* Methods for type checking */

static inline void
CheckBool(item_t * const x)
{
  if (x->type->tag != kTypeBool) {
    ORS_Mark("not a boolean");
    x->type = &g_bool_type;
  }
}

static inline void
CheckInt(item_t * const x)
{
  if (x->type->tag != kTypeInt) {
    ORS_Mark("not an integer");
    x->type = &g_int_type;
  }
}

static inline void
CheckSet(item_t * const x)
{
  if (x->type->tag != kTypeSet) {
    ORS_Mark("not a set");
    x->type = &g_set_type;
  }
}

static void
CheckSetVal(item_t * const x)
{
  assert(x && x->type);

  /* Set elements can be any integers between 0 and 32 (incl.) */
  if (x->type->tag != kTypeInt) {
    ORS_Mark("not an integer");
    x->type = &g_set_type;
  } else if (x->mode == kModeImmediate && (x->a < 0 || x->a >= 32)) {
    ORS_Mark("invalid set");
  }
}

static inline void
CheckBasicType(item_t * const x)
{
  if (x->type->tag > kTypeSet) {
    ORS_Mark("not a basic type");
    x->type = &g_int_type;
  }
}

/* Methods for validating items */

static void
CheckConst(item_t * const x)
{
  if (x->mode != kModeImmediate) {
    ORS_Mark("not a constant");
    x->mode = kModeImmediate;
  }
}

static void
CheckReadOnly(item_t * const x)
{
  assert(x);

  if (x->rdo) {
    ORS_Mark("read-only");
  }
}

static bool
CheckExport(void)
{
  if (g_sym == kSymTimes) {
    Consume();
    if (g_level != 0) {
      ORS_Mark("remove asterisk");
    }
    return true;
  }
  return false;
}

/* Identifiers take the form ident or ident.ident, the latter being qualified
 * by the name of an imported module (e.g., SYSTEM).
 */

static object_t *
QualIdent(void)
{
  object_t *    obj;

  if (!(obj = ORB_This())) {
    ORS_Mark("undef");
    obj = &g_dummy;
  }
  Consume();
  if (g_sym == kSymPeriod && obj->tag == kObjMod) {
    Consume();
    if (g_sym != kSymIdent) {
      ORS_Mark("identifier expected");
      return &g_dummy;
    }
    if (!(obj = ORB_ThisImport(obj))) {
      ORS_Mark("undef");
      return &g_dummy;
    }
    Consume();
  }
  return obj;
}

/* Selectors are used for accessing array elements, record fields and for
 * dereferencing pointers. Oberon also specifies type guards, but these are
 * not supported in the current implementation.
 */

static inline bool
MatchSelector(const int sym)
{
  return sym == kSymPeriod || sym == kSymLBrak || sym == kSymArrow;
}

static void
Selector(item_t * const x)
{
  item_t        y;
  object_t *    obj;

  while (MatchSelector(g_sym)) {
    switch (g_sym) {
    case kSymLBrak:
      /* Array indices */
      do {
        Consume();
        Expr(&y);
        if (x->type->tag != kTypeArray) {
          ORS_Mark("not an array");
        } else {
          CheckInt(&y);
          ORG_Index(x, &y);
          x->type = x->type->base;
        }
      } while (g_sym == kSymComma);
      Expect(kSymRBrak, "no ]");
      break;
    case kSymPeriod:
      /* Field selector */
      Consume();
      if (g_sym != kSymIdent) {
        ORS_Mark("ident?");
        continue;
      }
      if (x->type->tag == kTypePointer) {
        ORG_Deref(x);
        x->type = x->type->base;
      }
      if (x->type->tag != kTypeRecord) {
        ORS_Mark("not a record");
        Consume();
        continue;
      }
      obj = ORB_ThisField(x->type);
      Consume();
      if (!obj) {
        ORS_Mark("undef");
      } else {
        ORG_Field(x, obj);
        x->type = obj->type;
      }
      break;
    case kSymArrow:
      /* Pointer dereference */
      Consume();
      if (x->type->tag != kTypePointer) {
        ORS_Mark("not a pointer");
        continue;
      }
      ORG_Deref(x);
      x->type = x->type->base;
      break;
    }
  }
}

/* Methods for testing assignability of types, used in parsing assignments
 * and procedure calls.
 */

static bool
IsExtension(type_t * const t0, type_t * const t1)
{
  return (t0 == t1) || (t1 && IsExtension(t0, t1->base));
}


static inline bool
MatchingArrayTypes(type_t * const t0, type_t * const t1)
{
  assert(t0);
  assert(t1);

  return (t0->tag == kTypeArray) && (t1->tag == kTypeArray)
      && (t0->u.len == t1->u.len)    /* array lengths match */
      && (t0->base == t1->base);     /* element types match */
}

static inline bool
MatchingProcTypes(type_t * const t0, type_t * const t1)
{
  assert(t0);
  assert(t1);

  return (t0->tag == kTypeProc) && (t1->tag == kTypeProc)
      && EqualSignatures(t0, t1);
}

static inline bool
MatchingRecordTypes(type_t * const t0, type_t * const t1)
{
  assert(t0);
  assert(t1);

  return (t0->tag == kTypeRecord) && (t1->tag == kTypeRecord)
      && IsExtension(t0, t1);
}

static inline bool
MatchingPointerTypes(type_t * const t0, type_t * const t1)
{
  assert(t0);
  assert(t1);

  return (t0->tag == kTypePointer) && (t1->tag == kTypePointer)
      && IsExtension(t0->base, t1->base);
}

static bool
EqualSignatures(type_t * const t0, type_t * const t1)
{
  object_t *    p0;           /* Parameter list of t0 */
  object_t *    p1;           /* Parameter list of t1 */

  assert(t0 && t0->tag == kTypeProc);
  assert(t1 && t1->tag == kTypeProc);

  /* Return types and numbers of parameters must be the same */
  if (t0->base != t1->base || t0->u.nofpar != t1->u.nofpar) {
    return false;
  }

  /* Compare parameters */
  for (p0 = t0->dlink, p1 = t1->dlink; p0; p0 = p0->rlink, p1 = p1->rlink) {
    if ((p0->tag != p1->tag || p0->rdo != p1->rdo || p0->type != p1->type) &&
        !MatchingArrayTypes(p0->type, p1->type) &&
        !MatchingProcTypes(p0->type, p1->type)) {
      return false;
    }
  }
  assert(!p0);
  return true;
}

/* Checks for assignment compatibility */
static bool
CompatibleTypes(type_t * const t0, type_t * const t1, bool varpar)
{
  assert(t0);
  assert(t1);

  if (t0 == t1 || MatchingArrayTypes(t0, t1) || MatchingRecordTypes(t0, t1)) {
    return true;
  }
  if (varpar) {
    return false;
  }
  if (MatchingPointerTypes(t0, t1) || MatchingProcTypes(t0, t1)) {
    return true;
  }
  return (t0->tag == kTypePointer || t0->tag == kTypeProc)
      && (t1->tag == kTypeNil);
}

/* Code for parsing the arguments of a procedure call */

static inline bool
IsOpenArray(type_t * const type)
{
  assert(type);

  return type->tag == kTypeArray && type->u.len < 0;
}

static void
Parameter(object_t * const param)
{
  item_t        x;
  bool          varpar;       /* Whether a variable or value parameter */

  /* Parse actual argument */
  Expr(&x);

  /* Param is a formal parameter of the invoked procedure */
  /* NULL if the name of the invoked PROCEDURE was not recognized. In this
   * case, we do want to be able to keep on parsing the actual arguments to
   * see if more errors can be found.
   */
  if (param == NULL) {
    return;
  }

  /* Whether a VAR parameter (true) or a value parameter (false) */
  varpar = param->tag == kObjParam;

  /* Compare the actual- and formal parameter */
  if (CompatibleTypes(param->type, x.type, varpar)) {
    if (!varpar) {
      ORG_ValueParam(&x);
    } else {
      assert(param->tag == kObjParam);
      if (!param->rdo) {
        CheckReadOnly(&x);
      }
      ORG_VarParam(&x, param->type);
    }
  } else if (IsOpenArray(param->type) && x.type->tag == kTypeArray &&
             x.type->base == param->type->base) {
    /* Passing an array to an open array parameter */
    if (!param->rdo) {
      CheckReadOnly(&x);
    }
    ORG_OpenArrayParam(&x, false);
  } else if (x.type->tag == kTypeString && varpar && param->rdo
         &&  IsOpenArray(param->type)
         &&  param->type->base->tag == kTypeChar) {
    /* Passing a string to an open array parameter w. element type CHAR */
    ORG_StringParam(&x);
  } else if (!varpar && param->type->tag == kTypeInt
         &&  x.type->tag == kTypeInt) {
    /* Byte */
    ORG_ValueParam(&x);
  } else if (x.type->tag == kTypeString && x.b == 2
         &&  param->tag == kObjVar && param->type->tag == kTypeChar) {
    /* Passing a string of size 1 (e.g., "x") to a parameter of type CHAR */
    ORG_StrToChar(&x);
    ORG_ValueParam(&x);
  } else if (param->type->tag == kTypeArray
         &&  param->type->base == &g_byte_type
         &&  param->type->u.len >= 0
         &&  param->type->size == x.type->size) {
    ORG_VarParam(&x, param->type);
  } else {
    ORS_Mark("incompatible parameters");
  }
}

static void
ParamList(item_t * const x)
{
  object_t *    param;        /* Iterator over formal parameters */
  int           n;            /* Number of actual parameters */

  /* x represents the invoked procedure */
  assert(x);

  /* Extract formal parameter list from the procedure type */
  param = x->type->dlink;

  /* Parse actual parameters */
  if (g_sym == kSymRParen) {
    /* Empty parameter list */
    n = 0;
    Consume();
  } else {
    Parameter(param);
    n = 1;
    while (g_sym == kSymComma) {
      Consume();
      if (param) {
        param = param->rlink;
      }
      Parameter(param);
      ++n;
    }
    Expect(kSymRParen, ") missing");
  }

  /* Ensure the numbers of actual- and formal parameters match */
  if (n < x->type->u.nofpar) {
    ORS_Mark("too few params");
  } else if (n > x->type->u.nofpar) {
    ORS_Mark("too many params");
  }
}

/* Parse calls to standard (i.e., inlined) procedures. Their numeric
 * identifiers, passed as func, are set in ORB_Init during initialization
 * of the universal scope and of the SYSTEM module.
 */
static void
StandFunc(item_t * const x, int func, type_t * result_type)
{
  item_t        y;            /* Second parameter */
  int           nap;          /* Number of actual parameters */
  int           nfp;          /* Number of formal parameters */

  Expect(kSymLParen, "no (");
  nfp = func % 10;            /* E.g., LEN (funct = 41) has 1 parameters */
  func = func / 10;           /* E.g., LEN is identified by 4 */

  /* First actual parameter */
  Expr(x);
  nap = 1;

  /* Second actual parameter */
  /* If too many parameters are passed, use the last one and report an error */
  while (g_sym == kSymComma) {
    Consume();
    Expr(&y);
    ++nap;
  }
  Expect(kSymRParen, "no )");
  if (nap != nfp) {
    ORS_Mark("wrong nof params");
    return;
  }

  /* Switch on function identifier */
  switch (func) {
  case 0: /* ABS */
    if (x->type->tag == kTypeInt) {
      ORG_Abs(x);
    } else {
      ORS_Mark("bad type");
    }
    break;
  case 1: /* ODD */
    CheckInt(x);
    ORG_Odd(x);
    break;
  case 2: /* ORD */
    if (x->type->tag <= kTypeProc) {
      ORG_Ord(x);
    } else if (x->type->tag == kTypeString && x->b == 2) {
      ORG_StrToChar(x);
    } else {
      ORS_Mark("bad type");
    }
    break;
  case 3: /* CHR */
    CheckInt(x);
    ORG_Ord(x);
    break;
  case 4: /* LEN */
    if (x->type->tag == kTypeArray) {
      ORG_Len(x);
    } else {
      ORS_Mark("not an array");
    }
    break;
  case 5: /* LSL */
    /* fallthrough */
  case 6: /* ASR */
    /* fallthrough */
  case 7: /* ROR */
    CheckInt(&y);
    if (x->type->tag == kTypeInt || x->type->tag == kTypeSet) {
      ORG_Shift(func-5, x, &y);
      result_type = x->type;
    } else {
      ORS_Mark("bad type");
    }
    break;
  case 8: /* BIT */
    CheckInt(x);
    CheckInt(&y);
    ORG_Bit(x, &y);
    break;
  case 9: /* REG */
    CheckConst(x);
    CheckInt(x);
    ORG_Register(x);
    break;
  case 10: /* VAL */
    if (x->mode == kModeType && x->type->size <= y.type->size) {
      result_type = x->type;
      x->mode = y.mode;
      x->a = y.a;
      x->b = y.b;
      x->rdo = y.rdo;
    } else {
      ORS_Mark("casting not allowed");
    }
    break;
  case 11: /* ADR */
    ORG_Adr(x);
    break;
  case 12: /* SIZE */
    if (x->mode == kModeType) {
      ORG_MakeConst(x, &g_int_type, x->type->size);
    } else {
      ORS_Mark("must be a type");
    }
    break;
  case 13: /* COND */
    CheckConst(x);
    CheckInt(x);
    ORG_Condition(x);
    break;
  default:
    assert(0);
  }
  x->type = result_type;
}

/* Methods for parsing set literals. */

/* Parses single elements x and ranges x..y */
static void
Element(item_t * const x)
{
  item_t    y;

  assert(x);

  Expr(x);
  CheckSetVal(x);
  if (g_sym == kSymUpTo) {
    Consume();
    Expr(&y);
    CheckSetVal(&y);
    ORG_Set(x, &y);
  } else {
    ORG_Singleton(x);
  }
  x->type = &g_set_type;
}

static void
Set(item_t * const x)
{
  item_t    y;

  assert(x);
  assert(g_sym == kSymLBrace);

  /* Init */
  Consume();

  /* >= kIf includes } and start symbols of statements */
  if (g_sym >= kSymIf) {
    if (g_sym != kSymRBrace) {
      ORS_Mark(" } missing");
    }
    /* Empty set */
    ORG_MakeConst(x, &g_set_type, 0);
    return;
  }

  /* Parse first element */
  Element(x);

  /* In parsing the remaining (>= 0) elements we stop at any righthand
   * grouping symbol instead of just }, validating the latter's occurence only
   * afterwards. This way we can correct for the use of the wrong grouping
   * symbol in the source text and keep parsing to look for further errors.
   */
  while (g_sym < kSymRParen || g_sym > kSymRBrace) {
    if (g_sym == kSymComma) {
      Consume();
    } else if (g_sym != kSymRBrace) {
      ORS_Mark("missing comma");
    }
    Element(&y);
    ORG_SetOp(kSymPlus, x, &y);
  }
}

/* Methods for parsing factors, terms, simple expressions and expressions. */

static inline bool
MatchExpr(const int sym)
{
  return g_sym >= kSymChar && g_sym <= kSymIdent;
}

static void
Factor(item_t * const x)
{
  object_t *    obj;
  int           rx;

  assert(x);

  if (!MatchExpr(g_sym)) {
    ORS_Mark("expression expected");
    /* Skip ahead until next start symbol of an expr, or to any follow symbol
     * that cannot occur as part of an expression itself.
     */
    do {
      Consume();
    } while (!MatchExpr(g_sym) && g_sym < kSymThen);
  }

  switch (g_sym) {
  case kSymIdent:
    obj = QualIdent();
    if (obj->tag == kObjSFunc) {
      /* Built-in (standard) function */
      StandFunc(x, obj->val, obj->type);
      return;
    }
    ORG_MakeItem(x, obj, g_level);
    Selector(x);
    if (g_sym == kSymLParen) {
      Consume();
      if (x->type->tag != kTypeProc || x->type->base->tag == kTypeNone) {
        ORS_Mark("not a function");
        ParamList(x);
      } else {
        rx = ORG_PrepCall(x);
        ParamList(x);
        ORG_Call(x, rx);

        /* Set x's type to that of its return value */
        x->type = x->type->base;
      }
    }
    break;
  case kSymInt:
    ORG_MakeConst(x, &g_int_type, g_ival);
    Consume();
    break;
  case kSymChar:
    ORG_MakeConst(x, &g_char_type, g_ival);
    Consume();
    break;
  case kSymNil:
    Consume();
    ORG_MakeConst(x, &g_nil_type, 0);
    break;
  case kSymString:
    ORG_MakeString(x, g_slen);
    Consume();
    return;
  case kSymLParen:
    Consume();
    Expr(x);
    Expect(kSymRParen, "no )");
    break;
  case kSymLBrace:
    Set(x);
    Expect(kSymRBrace, "no }");
    break;
  case kSymNot:
    Consume();
    Factor(x);
    CheckBool(x);
    ORG_Not(x);
    break;
  case kSymFalse:
    Consume();
    ORG_MakeConst(x, &g_bool_type, 0);
    break;
  case kSymTrue:
    Consume();
    ORG_MakeConst(x, &g_bool_type, 1);
    break;
  default:
    ORS_Mark("not a factor");
    ORG_MakeConst(x, &g_int_type, 0);
  }
}

static inline bool
IsMulOperator(const int sym)
{
  return sym >= kSymTimes && sym <= kSymAnd;
}

static void
Term(item_t * const x)
{
  item_t        y;            /* Factors other than the first */
  int           op;           /* Operator (* or +) */
  form_t        f;            /* Type (tag) of the term */

  assert(x);

  /* First factor */
  Factor(x);
  f = x->type->tag;

  /* Remaining factors */
  while (IsMulOperator(op = g_sym)) {
    Consume();
    if (op == kSymTimes) {
      if (f == kTypeInt) {
        Factor(&y);
        CheckInt(&y);
        ORG_MulOp(x, &y);
      } else if (f == kTypeSet) {
        Factor(&y);
        CheckSet(&y);
        ORG_SetOp(op, x, &y);
      } else {
        /* Note the righthand operand is not parsed in this case */
        ORS_Mark("bad type");
      }
    } else if ((op == kSymDiv) || (op == kSymMod)) {
      CheckInt(x);
      Factor(&y);
      CheckInt(&y);
      ORG_DivOp(op, x, &y);
    } else if (op == kSymRDiv) {
      if (f == kTypeSet) {
        Factor(&y);
        CheckSet(&y);
        ORG_SetOp(op, x, &y);
      } else {
        /* Note the righthand operand is not parsed in this case */
        ORS_Mark("bad type");
      }
    } else {
      assert(op == kSymAnd);
      CheckBool(x);
      ORG_And1(x);
      Factor(&y);
      CheckBool(&y);
      ORG_And2(x, &y);
    }
  }
}

static inline bool
IsAddOperator(const int sym)
{
  return sym >= kSymPlus && sym <= kSymOr;
}

static void
SimpleExpr(item_t * const x)
{
  item_t        y;            /* Terms other than the first */
  int           op;           /* Operator (+ or -) */

  assert(x);

  /* Parse first term */
  if (g_sym == kSymMinus) {
    Consume();
    Term(x);
    if (x->type->tag == kTypeInt || x->type->tag == kTypeSet) {
      ORG_Neg(x);
    } else {
      CheckInt(x);
    }
  } else if (g_sym == kSymPlus) {
    Consume();
    Term(x);
  } else {
    Term(x);
  }

  /* Remaining terms */
  while (IsAddOperator(op = g_sym)) {
    Consume();
    if (op == kSymOr) {
      ORG_Or1(x);
      CheckBool(x);
      Term(&y);
      CheckBool(&y);
      ORG_Or2(x, &y);
    } else if (x->type->tag == kTypeInt) {
      Term(&y);
      CheckInt(&y);
      ORG_AddOp(op, x, &y);
    } else {
      CheckSet(x);
      Term(&y);
      CheckSet(&y);
      ORG_SetOp(op, x, &y);
    }
  }
}

static bool
IsCharArray(item_t * const x)
{
  return x->type->tag == kTypeArray && x->type->base->tag == kTypeChar;
}

static void
Expr(item_t * const x)
{
  item_t        y;            /* Second operand */
  int           rel;          /* Relation */
  form_t        xf, yf;       /* Operand type( tag)s */

  /* First operand */
  SimpleExpr(x);

  /* Parse relation */
  if (g_sym < kSymEql || g_sym > kSymIn) {
    return;
  }
  rel = g_sym;
  Consume();

  /* x IN y */
  if (rel == kSymIn) {
    CheckInt(x);
    SimpleExpr(&y);
    CheckSet(&y);
    ORG_In(x, &y);
    x->type = &g_bool_type;
    return;
  }

  /* Other relations beside IN */

  /* Parse second operand */
  SimpleExpr(&y);
  xf = x->type->tag;
  yf = y.type->tag;

  /* Switch on operand types */
  if (x->type == y.type) {
    if (xf == kTypeChar || xf == kTypeInt) {
      ORG_IntRel(rel, x, &y);
    } else if (xf == kTypeSet || xf == kTypePointer || xf == kTypeProc ||
               xf == kTypeNil || xf == kTypeBool) {
      if (rel > kSymNeq) {
        ORS_Mark("only = or #");
      } else {
        ORG_IntRel(rel, x, &y);
      }
    } else if (IsCharArray(x) || xf == kTypeString) {
      ORG_StringRel(rel, x, &y);
    } else {
      ORS_Mark("illegal comparison");
    }
  } else if (((xf == kTypePointer || xf == kTypeProc) && yf == kTypeNil)
         ||  ((yf == kTypePointer || yf == kTypeProc) && xf == kTypeNil)) {
    if (rel > kSymNeq) {
      ORS_Mark("only = or #");
    } else {
      ORG_IntRel(rel, x, &y);
    }
  } else if (( xf == kTypePointer && yf == kTypePointer
         &&   (IsExtension(x->type->base, y.type->base)
         ||    IsExtension(y.type->base, x->type->base)))
         ||  ( xf == kTypeProc && yf == kTypeProc
         &&    EqualSignatures(x->type, y.type))) {
    if (rel > kSymNeq) {
      ORS_Mark("only = or #");
    } else {
      ORG_IntRel(rel, x, &y);
    }
  } else if ((IsCharArray(x) && (yf == kTypeString || IsCharArray(&y)))
         ||  (IsCharArray(&y) && xf == kTypeString)) {
    ORG_StringRel(rel, x, &y);
  } else if (xf == kTypeChar && yf == kTypeString && y.b == 2) {
    ORG_StrToChar(&y);
    ORG_IntRel(rel, x, &y);
  } else if (yf == kTypeChar && xf == kTypeString && x->b == 2) {
    ORG_StrToChar(x);
    ORG_IntRel(rel, x, &y);
  } else if (xf == kTypeInt && yf == kTypeInt) {
    /* Byte */
    ORG_IntRel(rel, x, &y);
  } else {
    ORS_Mark("illegal comparison");
  }
  x->type = &g_bool_type;
}

/* Statements */

/* Parse calls to standard / inlined procedures. */
static void
StandProc(int32_t n)
{
  int           nap;          /* Number of actual parameters */
  int           nfp;          /* Number of formal parameters */
  item_t        x;            /* First actual parameter */
  item_t        y;            /* Second actual parameter */
  item_t        z;            /* Third actual parameter */

  nfp = n % 10;               /* E.g., ASSERT (n = 41) has 1 formal param */
  n = n / 10;                 /* E.g., ASSERT is identified by 4 */
  nap = 0;

  if (g_sym == kSymLParen) {
    Consume();

    /* First actual parameter */
    Expr(&x);
    nap = 1;

    if (g_sym == kSymComma) {
      /* Second actual parameter */
      Consume();
      Expr(&y);
      nap = 2;

      /* Third actual parameter. In case of >3 params, skip ahead until the last
      * one and report the error afterwards (see below).
      */
      z.type = &g_no_type;
      while (g_sym == kSymComma) {
        Consume();
        Expr(&z);
        ++nap;
      }
    } else {
      y.type = &g_no_type;
    }

    Expect(kSymRParen, "no )");
  } else {
    x.type = &g_no_type;
  }

  if (n != 0 && n != 1 && n != 8 && nap != nfp) {
    /* Note n == 0 (INC), n == 1 (DEC) and n == 8 (WriteLn) are treated
     * specially because one parameter is optional.
     */
    printf("n = %d, nap = %d\n", n, nap);
    ORS_Mark("wrong nof parameters");
    return;
  }

  /* Switch on procedure identifier */
  switch (n) {
  case 0: /* INC(v), INC(v, n) */
    /* fallthrough */
  case 1: /* DEC(v), DEC(v, n) */
    CheckInt(&x);
    CheckReadOnly(&x);
    if (y.type->tag != kTypeNone) {
      CheckInt(&y);
    }
    ORG_Increment(n, &x, &y);
    break;
  case 2: /* INCL(v, x) (v := v + {x}) */
    /* fallthrough */
  case 3: /* EXCL(v, x) (v := v - {x}) */
    CheckSet(&x);
    CheckReadOnly(&x);
    CheckSetVal(&y);
    ORG_Include(n - 2, &x, &y);
    break;
  case 4: /* ASSERT(b) */
    CheckBool(&x);
    ORG_Assert(&x);
    break;
  case 5:  /* Read(x) */
    CheckReadOnly(&x);
    ORG_Read(&x);
    break;
  case 6: /* Write(x) */
    /* fallthrough */
  case 7: /* WriteLn(x) */
    if (x.type->tag == kTypeString && x.b == 2) {
      ORG_StrToChar(&x);
    }
    ORG_Write(n - 6, &x);
    break;
  case 8:  /* GET(a, y) */
    /* fallthrough */
  case 9: /* PUT(a, x) */
    CheckInt(&x);
    CheckBasicType(&y);
    CheckReadOnly(&y);
    ORG_Get(n - 8, &x, &y);
    break;
  case 10: /* COPY(src, dst, n) */
    CheckInt(&x);
    CheckInt(&y);
    CheckInt(&z);
    ORG_Copy(&x, &y, &z);
    break;
  default:
    assert(0);
  }
}

static void
AssignmentStmt(item_t * const x)
{
  item_t        y;

  assert(x);
  assert(g_sym == kSymBecomes);

  Consume();
  CheckReadOnly(x);
  Expr(&y);

  if (CompatibleTypes(x->type, y.type, false)) {
    if (x->type->tag <= kTypePointer || x->type->tag == kTypeProc) {
      ORG_Store(x, &y);
    } else {
      ORG_StoreStruct(x, &y);
    }
  } else if (IsOpenArray(y.type) && x->type->tag == kTypeArray &&
             x->type->base == y.type->base) {
    ORG_StoreStruct(x, &y);
  } else if (IsCharArray(x) && y.type->tag == kTypeString) {
    ORG_CopyString(x, &y);
  } else if (x->type->tag == kTypeInt && y.type->tag == kTypeInt) {
    /* Byte */
    ORG_Store(x, &y);
  } else if (x->type->tag == kTypeChar && y.type->tag == kTypeString
         &&  y.b == 2) {
    ORG_StrToChar(&y);
    ORG_Store(x, &y);
  } else {
    ORS_Mark("illegal assignment");
  }
}

static void
IfStmt(void)
{
  item_t        x;
  int           l;            /* Label */

  assert(g_sym == kSymIf);

  /* Parse condition */
  Consume();
  Expr(&x);
  CheckBool(&x);
  ORG_CFJump(&x);             /* Cond forward jump to L0 (after THEN clause) */

  /* Parse THEN clause */
  Expect(kSymThen, "no THEN");
  StmtSequence();

  /* Parse ELSIF clauses */
  l = 0;
  while (g_sym == kSymElsif) {
    Consume();
    ORG_FJump(&l);            /* Uncond forward jump to L (end) */
    ORG_Fixup(&x);            /* L0 */
    Expr(&x);
    CheckBool(&x);
    ORG_CFJump(&x);           /* Cond forward jump to L0, after ELSIF clause */
    Expect(kSymThen, "no THEN");
    StmtSequence();
  }

  /* Parse ELSE clause */
  if (g_sym == kSymElse) {
    Consume();
    ORG_FJump(&l);            /* Uncond forward jump to L (end) */
    ORG_Fixup(&x);            /* L0 */
    StmtSequence();
  } else {
    ORG_Fixup(&x);            /* L0 */
  }

  ORG_FixLink(l);             /* L */
  Expect(kSymEnd, "no END");
}

static void
WhileStmt(void)
{
  item_t        x;
  int           l;            /* Label */

  assert(g_sym == kSymWhile);

  Consume();

  /* Parse condition */
  l = ORG_Here();             /* L */
  Expr(&x);
  CheckBool(&x);
  ORG_CFJump(&x);             /* Cond forward jump to L0 (after DO clause) */

  /* Parse DO clause */
  Expect(kSymDo, "no DO");
  StmtSequence();
  ORG_BJump(l);               /* Uncond backward jump to L (WHILE) */

  /* Parse ELSIF clauses */
  while (g_sym == kSymElsif) {
    Consume();
    ORG_Fixup(&x);            /* L0 */
    Expr(&x);
    CheckBool(&x);
    ORG_CFJump(&x);           /* Cond forward jump to L0, after ELSIF clause */
    Expect(kSymDo, "no DO");
    StmtSequence();
    ORG_BJump(l);             /* Uncond backward jump to L (WHILE) */
  }
  ORG_Fixup(&x);              /* L0 */
  Expect(kSymEnd, "no END");
}

static void
RepeatStmt(void)
{
  item_t    x;
  int       l;

  assert(g_sym == kSymRepeat);

  Consume();
  l = ORG_Here();             /* L */
  StmtSequence();

  if (g_sym != kSymUntil) {
    ORS_Mark("missing UNTIL");
  } else {
    Consume();
    Expr(&x);
    CheckBool(&x);
    ORG_CBJump(&x, l);        /* Cond backward jump to L (REPEAT) */
  }
}

static void
ForStmt(void)
{
  item_t        x;            /* Loop variable */
  item_t        y;            /* Initializer for x */
  item_t        z;            /* TO */
  item_t        w;            /* BY */
  object_t *    obj;          /* Identifier for type of x */
  int           l0;           /* Label L0 */
  int           l1;           /* Label L1 */

  assert(g_sym == kSymFor);

  Consume();
  if (g_sym != kSymIdent) {
    ORS_Mark("identifier expected");
    return;
  }

  obj = QualIdent();
  ORG_MakeItem(&x, obj, g_level);
  CheckInt(&x);
  CheckReadOnly(&x);

  /* Parse initializer */
  if (g_sym != kSymBecomes) {
    ORS_Mark(":= expected");
    return;
  }
  Consume();
  Expr(&y);
  CheckInt(&y);
  ORG_For0(&y);
  l0 = ORG_Here();                      /* L0 */

  /* Parse TO */
  Expect(kSymTo, "no TO");
  Expr(&z);
  CheckInt(&z);

  /* Parse BY */
  if (g_sym == kSymBy) {
    Consume();
    Expr(&w);
    CheckConst(&w);
    CheckInt(&w);
  } else {
    ORG_MakeConst(&w, &g_int_type, 1);
  }

  /* Ensure loop variable cannot be modified inside body */
  obj->rdo = true;

  /* Parse DO */
  Expect(kSymDo, "no DO");
  l1 = ORG_For1(&x, &y, &z, &w);        /* Conditional jump to L1 */
  StmtSequence();
  Expect(kSymEnd, "no END");
  ORG_For2(&x, &w);
  ORG_BJump(l0);                        /* Unconditional backjump to L0 */
  ORG_FixLink(l1);                      /* L1 */

  /* Allow for reuse of loop variable */
  obj->rdo = false;
}

static void
Stmt(void)
{
  item_t        x;
  item_t        y;
  object_t *    obj;
  int           rx;

  /* Note >= kSemicolon include the follow symbols of statements */
  if (!MatchStmt(g_sym) && g_sym < kSymSemicolon) {
    ORS_Mark("statement expected");
    /* Skip ahead to next start (or follow) symbol of a statement */
    do {
      Consume();
    } while (g_sym < kSymIdent);
  }

  switch (g_sym) {
  case kSymIdent:
    obj = QualIdent();
    if (obj->tag == kObjSProc) {
      StandProc(obj->val);
    } else {
      ORG_MakeItem(&x, obj, g_level);
      Selector(&x);
      if (g_sym == kSymBecomes) {
        AssignmentStmt(&x);
      } else if (g_sym == kSymEql) {
        ORS_Mark("should be :=");
        Consume();
        Expr(&y);
      } else if (g_sym == kSymLParen) {
        /* Procedure call; e.g., Foo() */
        Consume();
        if (x.type->tag != kTypeProc || x.type->base->tag != kTypeNone) {
          ORS_Mark("not a procedure");
          ParamList(&x);
        } else {
          rx = ORG_PrepCall(&x);
          ParamList(&x);
          ORG_Call(&x, rx);
        }
      } else if (x.type->tag == kTypeProc) {
        /* Procedure call with no arguments; e.g., Foo */
        if (x.type->u.nofpar > 0) {
          ORS_Mark("missing parameters");
        }
        if (x.type->base->tag != kTypeNone) {
          ORS_Mark("not a procedure");
        } else {
          rx = ORG_PrepCall(&x);
          ORG_Call(&x, rx);
        }
      } else if (x.mode == kModeType) {
        ORS_Mark("illegal assignment");
      } else {
        ORS_Mark("not a procedure");
      }
    }
    break;
  case kSymIf:
    IfStmt();
    break;
  case kSymWhile:
    WhileStmt();
    break;
  case kSymRepeat:
    RepeatStmt();
    break;
  case kSymFor:
    ForStmt();
    break;
  } /* default: empty statement */
}

static inline bool
MatchStmt(const int sym)
{
  return sym >= kSymIdent && sym <= kSymFor;
}

static void
StmtSequence(void)
{
  for (;;) {
    Stmt();
    ORG_CheckRegs();

    /* End of sequence? */
    if (g_sym == kSymSemicolon) {
      Consume();
    } else if (g_sym < kSymSemicolon) {
      ORS_Mark("missing semicolon?");
    } else if (g_sym > kSymSemicolon) {
      break;
    }
  }
}

/* Type declarations */

/* Parses identifier lists like i, j, k. Used in VAR declarations and in
 * formal parameter lists (FPSection).
 */
static object_t *
IdentList(const int tag)
{
  object_t *    fst;
  object_t *    obj;

  assert(g_sym == kSymIdent);

  fst = ORB_New(g_id, tag);
  Consume();
  fst->expo = CheckExport();
  while (g_sym == kSymComma) {
    Consume();
    if (g_sym != kSymIdent) {
      ORS_Mark("identifier expected");
    } else {
      obj = ORB_New(g_id, tag);
      Consume();
      obj->expo = CheckExport();
    }
  }
  Expect(kSymColon, ":?");
  return fst;
}

/* Round up to nearest multiple of word size (4 bytes) */
static int
Align(const int sz)
{
  return ((sz + 3) / 4) * 4;
}

static type_t *
ArrayType(void)
{
  item_t        x;            /* Constant expression for array length */
  type_t *      type;
  int           len;          /* Array length */

  type = Pool_Alloc(sizeof *type);
  type->tag = kTypeNone;

  /* Parse length */
  Expr(&x);
  if (x.mode != kModeImmediate || x.type->tag != kTypeInt || x.a < 0) {
    len = 1;
    ORS_Mark("not a valid length");
    /* Note in particular open arrays are not allowed in TYPE declarations. */
  }
  len = x.a;

  if (g_sym == kSymOf) {
    /* Element type */
    Consume();
    type->base = Type();
    if (IsOpenArray(type->base)) {
      ORS_Mark("dynamic array not allowed");
    }
  } else if (g_sym == kSymComma) {
    /* Additional dimension */
    Consume();
    type->base = ArrayType();
  } else {
    ORS_Mark("missing OF");
    type->base = &g_int_type;
  }
  type->tag = kTypeArray;
  type->u.len = len;
  type->size = Align(len * type->base->size);
  return type;
}

static type_t *
RecordType(void)
{
  type_t *      new;          /* New record type */
  type_t *      type;         /* Field list type */
  object_t *    base;         /* Base type */
  object_t *    it;           /* Iterator over field declarations */
  object_t *    start;        /* Start of iteration */
  object_t *    end;          /* End of iteration */
  int           offset;       /* Field offsets */
  int           off;          /* Used to set field offsets */
  int           n;            /* Number of fields */

  assert(g_sym == kSymRecord);

  Consume();

  /* Initialization */
  base = NULL;
  end = NULL;
  offset = 0;
  new = Pool_Alloc(sizeof *new);
  new->tag = kTypeNone;
  new->base = NULL;
  new->u.ext = 0;

  /* Parse base type (record extension) */
  if (g_sym == kSymLParen) {
    Consume();
    if (g_level != 0) {
      ORS_Mark("extension of local types not implemented");
    }
    if (g_sym != kSymIdent) {
      ORS_Mark("ident expected");
    } else {
      base = QualIdent();
      if (base->tag != kObjType) {
        ORS_Mark("type expected");
      } else {
        if (base->type->tag != kTypeRecord) {
          new->base = &g_int_type;
          ORS_Mark("invalid extension");
        } else {
          new->base = base->type;
        }
        new->u.ext = new->base->u.ext + 1;
        offset = new->base->size;
        end = new->base->dlink;
      }
    }
    Expect(kSymRParen, "no )");
  }

  /* Parse fields */
  while (g_sym == kSymIdent) {
    /* Parse field list */
    n = 0;
    start = end;
    while (g_sym == kSymIdent) {

      /* Search record (incl. base) for fields with the same name */
      for (it = start; it && strcmp(it->name, g_id); it = it->rlink)
        ;
      if (it) {
        ORS_Mark("mult def");
      }

      /* Create new field */
      it = Pool_Alloc(sizeof *it);
      strcpy(it->name, g_id);
      it->tag = kObjField;
      it->rlink = start;
      it->level = 0;
      start = it;
      ++n;
      Consume();
      it->expo = CheckExport();

      /* Separator */
      if (g_sym != kSymComma && g_sym != kSymColon) {
        ORS_Mark("comma expected");
      } else if (g_sym == kSymComma) {
        Consume();
      }
    }
    Expect(kSymColon, "colon expected");
    type = Type();
    if (IsOpenArray(type)) {
      ORS_Mark("dynamic array not allowed");
    }

    /* Set types and offsets of field list entries
     * Note that as fields were pushed onto a stack, offsets have to be
     * set in reverse order!
     */
    if (type->size > 1) {
      offset = Align(offset);
    }
    offset += n * type->size;
    off = offset;
    for (it = start; it != end; it = it->rlink) {
      it->type = type;
      off -= type->size;
      it->val = off;
    }
    end = start;

    if (g_sym == kSymSemicolon) {
      Consume();
    } else if (g_sym != kSymEnd) {
      ORS_Mark(" ; or END");
    }
  }
  new->tag = kTypeRecord,
  new->dlink = end;
  new->size = Align(offset);
  
  Expect(kSymEnd, "no END");

  return new;
}

/* Parses the type of a formal parameter. */
static type_t *
FormalType(const int dim)
{
  object_t *    obj;          /* Symbol table entry for defined formal types */
  type_t *      type;
  int           dmy = 0;      /* Dummy offset (not needed) */

  switch (g_sym) {
  case kSymIdent:
    obj = QualIdent();
    if (obj->tag != kObjType) {
      ORS_Mark("not a type");
      return &g_int_type;
    } else {
      return obj->type;
    }
    assert(0);
  case kSymArray:
    Consume();
    Expect(kSymOf, "OF ?");
    if (dim >= 1) {
      ORS_Mark("multi-dimensional open arrays not implemented");
    }
    type = Pool_Alloc(sizeof *type);
    type->tag = kTypeArray;
    type->u.len = -1;   /* Open array indicated as such by length -1 */
    type->size = 8;     /* Open arrays use an additional word for the length */
    type->base = FormalType(dim + 1);
    return type;
  case kSymProcedure:
    Consume();
    type = ProcedureType(&dmy);
    ORB_CloseScope();     /* Closes the scope for the procedure's parameters */
    return type;
  default:
    ORS_Mark("identifier expected");
    return &g_no_type;
  }
}

static void
CheckRecLevel(const int level)
{
  if (level != 0) {
    ORS_Mark("ptr base must be global");
  }
}

static type_t *
PointerType(void)
{
  type_t *      type;
  object_t *    obj;          /* Symbol table entry for defined base types */
  ptrBase_t *   ptbase;       /* Used for recording forward references */

  assert(g_sym == kSymPointer);

  Consume();
  Expect(kSymTo, "no TO");

  type = Pool_Alloc(sizeof *type);
  type->tag = kTypePointer;
  type->size = 4;             /* A pointer is represented by an address */
  type->base = &g_int_type;   /* Dummy base type to later verify if resolved */

  if (g_sym == kSymIdent) {
    if ((obj = ORB_This())) {
      if (obj->tag == kObjType
          && (obj->type->tag == kTypeRecord || obj->type->tag == kTypeNone)){
        CheckRecLevel(obj->level);
        type->base = obj->type;
      } else if (obj->tag == kObjMod) {
        ORS_Mark("external base type not implemented");
      } else {
        ORS_Mark("no valid base type");
      }
    } else {
      /* Forward reference (resolved in the method TypeDecl) */
      CheckRecLevel(g_level);
      ptbase = Pool_Alloc(sizeof *ptbase);
      strcpy(ptbase->name, g_id);
      ptbase->type = type;
      ptbase->link = g_pbs_list;
      g_pbs_list = ptbase;
    }
    Consume();
  } else {
    type->base = Type();
    if (type->base->tag != kTypeRecord || type->base->typobj == NULL) {
      ORS_Mark("must point to named record");
    }
    CheckRecLevel(g_level);
  }

  return type;
}

/* A formal parameter section corresponds to an identifier list, optionally
 * preceded by VAR, and followed by a type. (E.g., VAR i, j, k : INTEGER)
 */
static int
FPSection(int * const offset)
{
  object_t *    fst;
  object_t *    it;           /* Iterator over identifiers */
  type_t *      type;         /* Parameter type */
  int           tag;          /* Object tag (variable- or value parameter) */
  int           nofpar;       /* Number of parameters in this section */
  int           parsize;      /* Size of a single parameter */
  bool          rdo;          /* Read-only */

  if (g_sym == kSymVar) {
    /* Variable parameter (passed by reference) */
    Consume();
    tag = kObjParam;
  } else {
    /* Value parameters treated like local variables */
    tag = kObjVar;
  }

  /* Parse identifier list and type */
  fst = IdentList(tag);
  type = FormalType(0);

  if (tag == kObjVar && (type->tag == kTypeArray || type->tag == kTypeRecord)){
    /* Records and arrays passed by value treated as read-only references */
    tag = kObjParam;
    rdo = true;
  } else {
    rdo = false;
  }

  /* Calculate the size of a single parameter (in no. of words) */
  if (IsOpenArray(type)) {
    /* Open arrays need another word for the length */
    parsize = 8;
  } else {
    /* All other parameters take up only one word */
    parsize = 4;
  }

  /* Set properties of identifiers in list */
  nofpar = 0;
  for (it = fst; it; it = it->rlink) {
    ++nofpar;
    it->tag = tag;
    it->type = type;
    it->rdo = rdo;
    it->level = g_level;
    it->val = *offset;
    *offset += parsize;
  }

  if (*offset >= 52) {
    ORS_Mark("too many parameters");
  }

  return nofpar;
}

static type_t *
ProcedureType(int *offset)
{
  type_t *      type;         /* The type of a procedure (its signature) */
  object_t *    obj;          /* Return type identifier */
  int           nofpar;       /* Number of formal parameters */

  ORB_OpenScope();

  type = Pool_Alloc(sizeof *type);
  type->tag = kTypeProc;
  type->size = 1;             /* A procedure is represented by its address */
  type->u.nofpar = 0;         /* Set no. of parameters to 0 by default */
  type->dlink = NULL;         /* Set parameters to empty list by default */
  type->base = &g_no_type;    /* Set return type to none by default */

  /* Absent signature indicates a parameterless procedure */
  if (g_sym != kSymLParen) {
    return type;
  }
  Consume();

  /* Parse formal parameters */
  if (g_sym == kSymRParen) {
    /* 0 parameters */
    Consume();
  } else {
    /* >0 parameters */
    nofpar = FPSection(offset);
    while (g_sym == kSymSemicolon) {
      Consume();
      nofpar += FPSection(offset);
    }
    Expect(kSymRParen, "no )");

    /* Update type with parameter information */
    type->u.nofpar = nofpar;
    type->dlink = g_top_scope->rlink;
  }

  /* No return type */
  if (g_sym != kSymColon) {
    return type;
  }

  /* Return type (function)
   * Note that, compared to a procedure, a function w/h parameters *must* still
   * include parentheses () prior to the declaration of its return type. 
   */
  Consume();
  if (g_sym != kSymIdent) {
    ORS_Mark("type identifier expected");
  } else {
    obj = QualIdent();
    type->base = obj->type;

    /* Validate return type */
    if (obj->tag != kObjType || obj->type->tag == kTypeNil
                             || obj->type->tag == kTypeNone
                             || obj->type->tag > kTypeProc) {
      ORS_Mark("illegal function type");
    }
  }

  /* Note: scope left open for adding local declarations in case this is part
   * of a procedure declaration.
   */

  return type;
}

static inline bool
MatchType(const int sym)
{
  return sym == kSymIdent || sym >= kSymArray;
}

static type_t *
Type(void)
{
  object_t *    obj;          /* Symbol table entry for defined types */
  type_t *      type;
  int           dmy = 0;      /* Dummy offset (not needed) */

  if (!MatchType(g_sym)) {
    ORS_Mark("not a type");
    do {
      Consume();
    } while (!MatchType(g_sym));
  }

  switch (g_sym) {
  case kSymIdent:
    obj = QualIdent();
    if (obj->tag != kObjType) {
      ORS_Mark("not a type or undefined");
      return &g_int_type;
    }
    if (obj->type == NULL || obj->type->tag == kTypeNone) {
      ORS_Mark("not a type");
      return &g_int_type;
    }
    return obj->type;
  case kSymArray:
    Consume();
    return ArrayType();
  case kSymRecord:
    return RecordType();
  case kSymPointer:
    return PointerType();
  case kSymProcedure:
    Consume();
    type = ProcedureType(&dmy);
    ORB_CloseScope();
    return type;
  default:
    ORS_Mark("illegal type");
    return &g_int_type;
  }
}

/* Declarations */

static void
ConstDecl(void)
{
  item_t        x;            /* Definiens */
  object_t *    obj;          /* Definiendum (object) */
  char          id[kIdLen];   /* Definiendum (identifier) */
  bool          expo;         /* Whether exported */

  assert(g_sym == kSymConst);

  Consume();
  while (g_sym == kSymIdent) {
    /* Definiendum */
    strcpy(id, g_id);
    Consume();
    expo = CheckExport();
    Expect(kSymEql, "= ?");

    /* Definiens */
    Expr(&x);
    obj = ORB_New(id, kObjConst);
    obj->expo = expo;
    if (x.mode != kModeImmediate) {
      ORS_Mark("expression not constant");
      obj->type = &g_int_type;
    } else {
      /* Convert strings consisting of a single character to chars */
      if (x.type->tag == kTypeString && x.b == 2) {
        ORG_StrToChar(&x);
      }
      obj->type = x.type;
      obj->level = x.b;
      obj->val = x.a;

      /* Terminator */
      Expect(kSymSemicolon, "; missing");
    }
  }
}

static void
TypeDecl(void)
{
  type_t *      type;         /* The declared type */
  object_t *    obj;          /* Symbol table entry for the declared type */
  ptrBase_t *   it;           /* Iterator over forward references */
  char          id[kIdLen];   /* Name of the declared type */
  bool          expo;         /* Whether exported */

  assert(g_sym == kSymType);

  Consume();
  while (g_sym == kSymIdent) {
    /* Parse type declaration */
    strcpy(id, g_id);
    Consume();
    expo = CheckExport();
    Expect(kSymEql, "=?");
    type = Type();

    /* Create new type object */
    obj = ORB_New(id, kObjType);
    obj->type = type;
    obj->expo = expo;
    obj->level = g_level;

    /* Set type object */
    if (type->typobj == NULL) {
      type->typobj = obj;
    }

    if (type->tag == kTypeRecord) {
      /* Check if forward-referenced by a prior pointer type decl */
      for (it = g_pbs_list; it; it = it->link) {
        if (!strcmp(obj->name, it->name)) {
          it->type->base = obj->type;
          break;
        }
      }
    }
    Expect(kSymSemicolon, "; missing");
  }
}

/* Parses variable declarations like
 *   VAR i, j : INTEGER; ch : CHAR;
 * Here, i, j are parsed by a single call to IdentList, as is ch.
 */
static void
VarDecl(int * const offset)
{
  object_t *    fst;
  object_t *    it;
  type_t *      type;

  assert(g_sym == kSymVar);

  Consume();
  while (g_sym == kSymIdent) {
    /* Parse identifier list */
    fst = IdentList(kObjVar);
    assert(fst);
    
    /* Parse type */
    type = Type();
  
    /* Set variable properties (type and offset in local block) */
    for (it = fst; it; it = it->rlink) {
      /* Set attributes */
      it->type = type;
      it->level = g_level;
      if (type->size > 1) {
        *offset = Align(*offset);
      }
      it->val = *offset;
      *offset += type->size;
    }

    /* Variable declarations terminated by ; */
    Expect(kSymSemicolon, "; missing");
  }
}

static inline bool
MatchDecl(const int sym)
{
  return sym >= kSymConst && sym <= kSymVar;
}

static void
Declarations(int * const offset)
{
  ptrBase_t *   ptbase;

  /* Empty list of forward-referenced pointer base types */
  g_pbs_list = NULL;

  if (g_sym < kSymConst && g_sym != kSymEnd && g_sym != kSymReturn) {
    ORS_Mark("declaration?");
    do {
      Consume();
    } while (g_sym < kSymConst && g_sym != kSymEnd && g_sym != kSymReturn);
  }

  /* CONST declarations */
  if (g_sym == kSymConst) {
    ConstDecl();
  }

  /* TYPE declarations */
  if (g_sym == kSymType) {
    TypeDecl();
  }

  /* VAR declarations */
  if (g_sym == kSymVar) {
    VarDecl(offset);
  }
  *offset = Align(*offset);

  /* Validate forward-references were resolved */
  for (ptbase = g_pbs_list; ptbase; ptbase = ptbase->link) {
    /* Pointer base types were initialized to INTEGER as a dummy. */
    if (ptbase->type->base->tag == kTypeInt) {
      ORS_Mark("undefined pointer base");
    }
  }

  if (MatchDecl(g_sym)) {
    ORS_Mark("declaration in bad order");
  }
}

static void
ProcedureDecl(void)
{
  object_t *    proc;
  item_t        x;        /* Return value */
  type_t *      type;     /* Procedure type (corresponding to signature) */
  int           parblksz; /* Size of parameters */
  int           locblksz; /* Size of parameters + locally declared variables */
  int           l;        /* Label */

  assert(g_sym == kSymProcedure);

  /* Procedure name */
  Consume();
  if (g_sym != kSymIdent) {
    return;
  }
  proc = ORB_New(g_id, kObjConst);
  proc->level = g_level;

  /* Exported? */
  Consume();
  proc->expo = CheckExport();

  /* Parse the signature, consisting of a formal parameter list and an
   * optional return type.
   */
  parblksz = 4;
  ++g_level;
  type = ProcedureType(&parblksz);
  proc->type = type;
  Expect(kSymSemicolon, "no ;");

  /* Push new arena for local declarations */
  Pool_Push();

  /* Local declarations (minus nested procedures) */
  locblksz = parblksz;
  Declarations(&locblksz);
  proc->val = ORG_Here() * 4;

  /* Nested procedure declarations */
  if (g_sym == kSymProcedure) {
    l = 0;
    ORG_FJump(&l);
    do {
      ProcedureDecl();
      Expect(kSymSemicolon, "no ;");
    } while (g_sym == kSymProcedure);
    ORG_FixOne(l);
    proc->val = ORG_Here() * 4;
  }

  /* Procedure body */
  ORG_Enter(parblksz, locblksz);
  if (g_sym == kSymBegin) {
    Consume();
    StmtSequence();
  }

  /* Return */
  if (g_sym == kSymReturn) {
    Consume();
    Expr(&x);
    if (type->base == &g_no_type) {
      ORS_Mark("this is not a function");
    } else if (!CompatibleTypes(type->base, x.type, false)) {
      ORS_Mark("wrong result type");
    }
  } else if (type->base->tag != kTypeNone) {
    ORS_Mark("function without a result");
    type->base = &g_no_type;
  }

  /* End of procedure */
  ORG_Return(type->base->tag, &x, locblksz);
  ORB_CloseScope();
  Pool_Pop();
  --g_level;
  Expect(kSymEnd, "no END");
  if (g_sym != kSymIdent) {
    ORS_Mark("no proc id");
    return;
  }
  if (strcmp(g_id, proc->name)) {
    ORS_Mark("no match");
  }
  Consume();
}

static void
Procedures(void)
{
  while (g_sym == kSymProcedure) {
    ProcedureDecl();
    Expect(kSymSemicolon, "no ;");
  }
}

static void
Module(void)
{
  char      modid[kIdLen];

  /* Parse module declaration */
  printf("\nCompiling ");
  Consume();
  if (g_sym != kSymModule) {
    ORS_Mark("must start with MODULE");
    return;
  }
  Consume();
  if (g_sym != kSymIdent) {
    ORS_Mark("identifier expected");
  } else {
    strcpy(modid, g_id);
    Consume();
    printf("%s\n", modid);
  }
  Expect(kSymSemicolon, "no ;");

  /* Initialization */
  g_level = 0;
  g_dc = 0;

  ORB_OpenScope();

  /* Parse declarations */
  ORG_Open();
  Declarations(&g_dc);
  ORG_SetDataSize(Align(g_dc));
  Procedures();

  /* Save the code address with which to initialize the RISC-0's PC register */
  g_entry = ORG_Here();

  /* Code executed upon loading the module */
  ORG_Header();
  if (g_sym == kSymBegin) {
    Consume();
    StmtSequence();
  }

  /* End of module */
  Expect(kSymEnd, "no END");
  if (g_sym != kSymIdent) {
    ORS_Mark("identifier missing");
  } else {
    if (strcmp(modid, g_id)) {
      ORS_Mark("no match");
    }
    Consume();
  }
  if (g_sym != kSymPeriod) {
    ORS_Mark("period missing");
  }
  ORB_CloseScope();
  ORG_Close();

  /* Save start address for globals */
  g_sb = ORG_Here();

  /* Reset list of forward declarations */
  g_pbs_list = NULL;
}

#ifdef TEST

#include "minunit.h"

#define TEST_FILE(fname) do {           \
  char *  msg;                          \
                                        \
  if ((msg = TestFile((fname)))) {      \
    return msg;                         \
  }                                     \
} while (0)

enum {
  kMaxArgs = 4
};

static char * TestFile(const char * const);

char *
TestParser(void)
{
  TEST_FILE("test/basic.mod");
  TEST_FILE("test/const.mod");
  TEST_FILE("test/bool.mod");
  TEST_FILE("test/control.mod");
  TEST_FILE("test/array.mod");
  TEST_FILE("test/strings.mod");
  TEST_FILE("test/record.mod");
  TEST_FILE("test/pointer.mod");
  TEST_FILE("test/proc.mod");
  TEST_FILE("test/io.mod");

  return NULL;
}

static char *
TestFile(const char * const fname)
{
  ORP_Compile(fname, 0);
  return NULL;
}

#endif /* TEST */
