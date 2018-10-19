/* Ported from ORS.Mod.txt, part of the Oberon07 reference implementation. */

#include "ors.h"

#include <fcntl.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  /* Bitmasks used for indicating properties of ASCII characters */
  kBlank  = 01,       /* Blanks */
  kBreak  = 02,       /* Line breaks */
  kLetter = 04,       /* Lower- and uppercase letters */
  kDigit  = 010,      /* Decimal digits */
  kHex    = 020,      /* Hexadecimal digits */

  /* Misc */
  kMaxLineLen = 256   /* Max line length in a source file */
};

/* Character traits (see Fraser & Hanson, A Retargetable C Compiler) */
static const int g_traits[] = {
  0,                   0,                   0,              0,
  0,                   0,                   0,              0,
  0,                   kBlank,              kBreak,         kBlank,
  kBlank,              kBreak,              0,              0,
  0,                   0,                   0,              0,
  0,                   0,                   0,              0,
  0,                   0,                   0,              0,
  0,                   0,                   0,              0,
  kBlank,              0,                   0,              0,
  0,                   0,                   0,              0,
  0,                   0,                   0,              0,
  0,                   0,                   0,              0,
  kDigit|kHex,         kDigit|kHex,         kDigit|kHex,    kDigit|kHex,
  kDigit|kHex,         kDigit|kHex,         kDigit|kHex,    kDigit|kHex,
  kDigit|kHex,         kDigit|kHex,         0,              0,  
  0,                   0,                   0,              0,
  0,                   kHex|kLetter,        kHex|kLetter,   kHex|kLetter,
  kHex|kLetter,        kHex|kLetter,        kHex|kLetter,   kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        0,
  0,                   0,                   0,              0,
  0,                   kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        kLetter,
  kLetter,             kLetter,             kLetter,        0,
  0,                   0,                   0,              0
};

/* Reserved words */
const char * const g_keytab_id[] = {
  "ARRAY",     "BEGIN",     "BY",        "CASE",      "CONST",     "DIV",
  "DO",        "ELSE",      "ELSIF",     "END",       "FALSE",     "FOR", 
  "IF",        "IMPORT",    "IN",        "IS",        "MOD",       "MODULE",
  "NIL",       "OF",        "OR",        "POINTER",   "PROCEDURE", "RECORD",
  "REPEAT",    "RETURN",    "THEN",      "TO",        "TRUE",      "TYPE",
  "UNTIL",     "VAR",       "WHILE"
};

/* Symbols for reserved words */
const int g_keytab_sym[] = {
  kSymArray,  kSymBegin,  kSymBy,     kSymCase,    kSymConst,     kSymDiv,
  kSymDo,     kSymElse,   kSymElsif,  kSymEnd,     kSymFalse,     kSymFor,
  kSymIf,     kSymImport, kSymIn,     kSymIs,      kSymMod,       kSymModule,
  kSymNil,    kSymOf,     kSymOr,     kSymPointer, kSymProcedure, kSymRecord,
  kSymRepeat, kSymReturn, kSymThen,   kSymTo,      kSymTrue,      kSymType,
  kSymUntil,  kSymVar,    kSymWhile
};

/* Prototypes */
static inline bool  IsBlank(const int);
static inline bool  IsLetter(const int);
static inline bool  IsDigit(const int);
static inline bool  IsHexDigit(const int);
static void         Consume(void);
static void         Comment(void);
static int          StrCmp(const void *, const void *);
static int          Identifier(void);
static int          String(void);
static int          HexString(void);
static int          Number(void);
static int          Switch(const int, const char, const int);
static inline int   Select(const int);

/* Public state (see lex.h) */

long                g_ival;                 /* Numbers */
char                g_id[kIdLen];           /* Identifiers and keywords */
char                g_str[kStrBufSz];       /* String literals */
int                 g_slen;                 /* String literal length */
int                 g_errcnt;               /* Error count */

/* Private state */
static const char * g_fname;                /* Filename */
static FILE *       g_fp;                   /* Source file handler */
static int          g_ch;                   /* Last character read */
static char         g_line[kMaxLineLen+1];  /* Line buffer (+1 for 0 byte) */
static int          g_cc;                   /* Character count */
static int          g_lineno;               /* Line number */
static int          g_err_ch;               /* Char position of last error */
static int          g_err_line;             /* Line number of last error */

int
ORS_Init(const char *fname)
{
  assert(fname);

  if ((g_fp = fopen(fname, "r")) == NULL) {
    perror(fname);
    return 1;
  }
  g_lineno = 0;
  g_cc = 0;
  g_err_ch = 0;
  g_err_line = 0;
  g_ch = '\n';
  Consume();
  g_id[0] = '\0';
  g_fname = fname;
  g_errcnt = 0;
  return 0;
}

void 
ORS_Free(void)
{
  /* Close source file */
  if (fclose(g_fp) == EOF) {
    perror(g_fname);
  }
}

int
ORS_Get(void)
{
  for (;;) {

    /* Skip blanks */
    while (g_ch != EOF && IsBlank(g_ch)) {
      Consume();
    }

    switch (g_ch) {
    case '(':
      Consume();
      if (g_ch == '*') {
        Comment();
        /* Skip */
        continue;
      }
      return kSymLParen;

    case '\n':
      Consume();
      continue;

    case EOF:  return kSymEot;
    case '"':  return String();
    case '$':  return HexString();
    case '.':  return Switch(kSymPeriod, '.', kSymUpTo);
    case ':':  return Switch(kSymColon, '=', kSymBecomes);
    case '<':  return Switch(kSymLss, '=', kSymLeq);
    case '>':  return Switch(kSymGtr, '=', kSymGeq);
    case '#':  return Select(kSymNeq);
    case '&':  return Select(kSymAnd);
    case ')':  return Select(kSymRParen);
    case '*':  return Select(kSymTimes);
    case '+':  return Select(kSymPlus);
    case ',':  return Select(kSymComma);
    case '-':  return Select(kSymMinus);
    case '/':  return Select(kSymRDiv);
    case ';':  return Select(kSymSemicolon);
    case '=':  return Select(kSymEql);
    case '[':  return Select(kSymLBrak);
    case ']':  return Select(kSymRBrak);
    case '^':  return Select(kSymArrow);
    case '{':  return Select(kSymLBrace);
    case '}':  return Select(kSymRBrace);
    case '|':  return Select(kSymBar);
    case '~':  return Select(kSymNot);
    case 0x7F: return Select(kSymUpTo);

    case '0': case '1': case '2': case '3': case '4': case '5': case '6':
    case '7': case '8': case '9':
      return Number();

    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
      return Identifier();
    }
    /* Silently ignore unrecognized characters */
    Consume();
  }
}

void
ORS_Mark(const char * const msg)
{
  int     i;

  if ((g_lineno > g_err_line || g_cc > g_err_ch) && g_errcnt < 25) {
    fprintf(stderr, "%5d: %s", g_lineno, g_line);
    for (i = 0; i != g_cc + 5; ++i) {
      fputc(' ', stderr);
    }
    fprintf(stderr, "^ %s\n", msg);
  }
  ++g_errcnt;
  g_err_line = g_lineno;
  g_err_ch = g_cc;
}

static inline bool
IsBlank(const int c)
{
  return g_traits[c] & kBlank;
}

static inline bool
IsLetter(const int c)
{
  return g_traits[c] & kLetter;
}

static inline bool
IsDigit(const int c)
{
  return g_traits[c] & kDigit;
}

static inline bool
IsHexDigit(const int c)
{
  return g_traits[c] & kHex;
}

static void
Consume(void)
{
  int     ch;
  int     i;

  if (g_ch == EOF) {
    fprintf(stderr, "program incomplete");
    ORS_Free();
    exit(1);
  } else if (g_ch == '\n') {
    ++g_lineno;
    g_cc = 0;
    i = 0;
    do {
      ch = fgetc(g_fp);
      g_line[i++] = ch;
    } while (i != kMaxLineLen && ch != '\n' && ch != EOF);
    if (i == kMaxLineLen && ch != '\n' && ch != EOF) {
      ORS_Mark("line too long");
    }
    g_line[i] = '\0';
  }
  g_ch = g_line[g_cc++];
}

static void
Comment(void)
{
  assert(g_ch == '*');

  /* Outer loop runs until ) is found. */
  do {
    /* Inner loop runs until * is found. */
    while (g_ch != EOF && g_ch != '*') {
      /* Test for nested comment */
      if (g_ch == '(') {
        Consume();
        if (g_ch == '*') {
          Comment();
        }
      } else {
        Consume();
      }
    }
    /* Read until one char beyond a sequence of >= 1 *'s */
    while (g_ch == '*') {
      Consume();
    }
  } while (g_ch != ')' && g_ch != EOF);

  /* Validate we didn't run out of source text before the end of the comment */
  if (g_ch != EOF) {
    assert(g_ch == ')');
    Consume();
  } else {
    ORS_Mark("unterminated comment");
  }
}

static int
StrCmp(const void *s1, const void *s2)
{
  return strcmp((const char *)s1, *(const char **)s2);
}

static int
Identifier(void)
{
  int                   i;    /* Loop index */
  const char * const *  cp;   /* Pointer into keyword table */

  assert(IsLetter(g_ch));

  /* Consume characters */
  i = 0;
  do {
    /* Silently drop characters if max. identifier length is exceeded */
    if (i < kIdLen - 1) {
      g_id[i++] = g_ch;
    }
    Consume();
  } while (IsLetter(g_ch) || IsDigit(g_ch));

  /* Terminate string */
  g_id[i] = '\0';

  /* Table lookup */
  if ((cp = bsearch(&g_id, g_keytab_id, 33, sizeof(char *), StrCmp))) {
    /* If found, return the corresponding symbol */
    return g_keytab_sym[cp - g_keytab_id];
  }
  /* Else, treat as regular identifier */
  return kSymIdent;
}

static int
String(void)
{
  int     i;                  /* Loop index */

  assert(g_ch == '"');

  Consume();

  i = 0;
  while (g_ch != EOF && g_ch != '"') {
    /* Silently ignore non-characters */
    if (g_ch >= ' ' && g_ch < 127) {
      /* Verify whether the string buffer is not already full */
      if (i < kStrBufSz - 1) {
        g_str[i++] = g_ch;
      } else {
        ORS_Mark("string too long");
      }
    }
    Consume();
  }

  g_str[i++] = '\0';
  Consume();
  g_slen = i;
  return kSymString;
}

static int
HexString(void)
{
  int     i;                  /* Loop index */
  char    m;

  assert(g_ch == '$');

  Consume();
  i = 0;
  while (g_ch != EOF && g_ch != '$') {
    /* Silently ignore non-characters */
    while (g_ch != EOF && ((g_ch <= ' ') || (g_ch == '\127'))) {
      Consume();
    }
    /* Parse first hexadecimal digit */
    if (IsHexDigit(g_ch)) {
      /* From C in 4 functions by Robert Swierczek */
      m = (g_ch & 0xF) + (g_ch >= 'A' ? 9 : 0);
    } else {
      m = 0;
      ORS_Mark("hexdig expected");
    }
    /* Parse second hexadecimal digit */
    Consume();
    if (IsHexDigit(g_ch)) {
      m = (16 * m) + (g_ch & 0xF) + (g_ch >= 'A' ? 9 : 0);
    } else {
      m = 0;
      ORS_Mark("hexdig expected");
    }
    /* Veryify whether the string buffer is not already full */
    if (i < kStrBufSz) {
      g_str[i++] = m;
    } else {
      ORS_Mark("string too long");
    }
    Consume();
  }
  Consume();
  g_slen = i;
  /* Don't append '\0' */
  return kSymString;
}

static int
Number(void)
{
  static int    buff[16];     /* Buffer */
  int           n, i;         /* Loop indices */
  int           h;            /* Hexadecimal digit */
  int           sum;          /* Integer number */

  assert(IsHexDigit(g_ch));

  /* Fill buffer. */
  n = 0;
  do {
    if (n < 16) {
      buff[n++] = g_ch - '0';
    } else {
      ORS_Mark("too many digits");
      n = 0;
    }
    Consume();
  } while (IsHexDigit(g_ch));

  /* Parse buffer contents */
  i = 0;
  sum = 0;
  /* Hexadecimal */
  if ((g_ch == 'H') || (g_ch == 'R') || (g_ch == 'X')) {
    /* Calculate the sum of the hexadecimal digits in the buffer */
    do {
      h = buff[i++];
      if (h >= 10) {
        h -= 7;
      }
      sum = (sum * 16) + h;
    } while (i != n);

    /* Suffix X indicates a char */
    if (g_ch == 'X') {
      /* Overflow check */
      if (sum < 256) {
        g_ival = sum;
      } else {
        ORS_Mark("illegal value");
        g_ival = 0;
      }
      Consume();
      return kSymChar;
    }

    /* Suffer H indicates an integer */
    assert(g_ch == 'H');
    g_ival = sum;
    Consume();
    return kSymInt;
  }

  /* Decimal integer */
  do {
    if (buff[i] < 10) {
      if (sum <= ((INT_MAX - buff[i]) / 10)) {
        sum = sum * 10 + buff[i];
      } else {
        ORS_Mark("too large");
        sum = 0;
      }
    } else {
      ORS_Mark("bad integer");
    }
  } while (++i != n);
  g_ival = sum;
  return kSymInt;
}

/* Based on Golang sources */
static int
Switch(const int sym1, const char c, const int sym2)
{
  Consume();
  if (g_ch == c) {
    Consume();
    return sym2;
  }
  return sym1;
}

static inline int
Select(const int sym)
{
  Consume();
  return sym;
}

#ifdef TEST

#include "minunit.h"

#include <math.h>

#define EPSILON 0.00001

#define ASSERT_SYM(typ) ASSERT_EQ((typ), ORS_Get())

#define ASSERT_SYM_ID(id) do {                      \
  ASSERT_SYM(kSymIdent);                            \
  ASSERT_EQ_CMP(strcmp, (id), g_id);                \
} while (0)

#define ASSERT_SYM_STR(str) do {                    \
  ASSERT_SYM(kSymString);                           \
  ASSERT_EQ_CMP(strcmp, (str), g_str);              \
} while (0)

#define ASSERT_SYM_INT(ival) do {                   \
  ASSERT_SYM(kSymInt);                              \
  ASSERT_EQ((ival), g_ival);                        \
} while (0)

char *
TestScanner(void)
{
  ORS_Init("test/ors.in");

  /* Regular identifiers */
  ASSERT_SYM_ID("x");
  ASSERT_SYM_ID("Oberon123");

  /* Numbers */
  ASSERT_SYM_INT(1987);
  ASSERT_SYM_INT(100);
  ASSERT_SYM(kSymUpTo);
  ASSERT_SYM_INT(256);

  /* Strings */
  ASSERT_SYM_STR("OBERON");
  ASSERT_SYM_STR("OBERON");

  /* Misc */
  ASSERT_SYM(kSymBecomes);
  ASSERT_SYM(kSymColon);

  ORS_Free();

  return NULL;
}

#endif /* TEST */
