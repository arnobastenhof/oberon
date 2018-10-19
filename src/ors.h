/* Ported from ORS.Mod.txt, part of the Oberon07 reference implementation. */

#ifndef ORS_H_
#define ORS_H_

/* Buffer sizes */
enum {
  kIdLen = 32,                                /* Identifier buffer */
  kStrBufSz = 256                             /* String literal buffer */
};

/* Symbols for multi-character tokens */
/* Not all of the keywords defined in the Revised Oberon specification are
 * used in the current implementation (e.g., imports), but we have kept them
 * reserved regardless.
 */
enum {
  /* Multiplication operators */
  kSymTimes = 1, kSymRDiv,      kSymDiv,       kSymMod,       kSymAnd,

  /* Addition operators */
  kSymPlus,      kSymMinus,     kSymOr,

  /* Relations */
  kSymEql,       kSymNeq,       kSymLss,       kSymLeq,       kSymGtr,
  kSymGeq,       kSymIn,        kSymIs,

  /* Pointer dereferencing and field selection */
  kSymArrow,     kSymPeriod,

  /* Start symbols for expressions */
  kSymChar,      kSymInt,       kSymFalse,     kSymTrue,      kSymNil,
  kSymString,    kSymNot,

  /* Left members of grouping symbols (more start symbols for expressions) */
  kSymLParen,    kSymLBrak,     kSymLBrace,

  /* Identifiers (start symbol for both expressions and statements) */
  kSymIdent,

  /* Start symbols for statements */
  kSymIf,        kSymWhile,     kSymRepeat,    kSymCase,      kSymFor,

  /* Separators between expressions (outside of operators and relations) */
  kSymComma,     kSymColon,     kSymBecomes,   kSymUpTo,

  /* Right members of grouping symbols */
  kSymRParen,    kSymRBrak,     kSymRBrace,

  /* Keywords that can follow expressions */
  kSymThen,      kSymOf,        kSymDo,        kSymTo,        kSymBy,

  /* Follow symbols for statements */
  kSymSemicolon, kSymEnd,       kSymBar,       kSymElse,      kSymElsif,
  kSymUntil,     kSymReturn,

  /* Keywords in type declarations */
  kSymArray,     kSymRecord,    kSymPointer,

  /* Declarations (except procedures) */
  kSymConst,     kSymType,      kSymVar,

  /* Remaining keywords */
  kSymProcedure, kSymBegin,     kSymImport,    kSymModule,    kSymEot
};

extern int      ORS_Init(const char * const); /* Open source file */
extern void     ORS_Free(void);               /* Close source file */
extern int      ORS_Get(void);                /* Get next symbol */
extern void     ORS_Mark(const char * const); /* Signal a source file error */

extern long     g_ival;                       /* Numbers */
extern char     g_id[kIdLen];                 /* Identifiers and keywords */
extern char     g_str[kStrBufSz];             /* String literals */
extern int      g_slen;                       /* String literal length */
extern int      g_errcnt;                     /* Error count */

#endif /* ORS_H_ */
