/* Ported from ORB.Mod.txt, part of the Oberon07 reference implementation. */

#ifndef ORB_H_
#define ORB_H_

#include <stdbool.h>
#include <stdint.h>

#include "ors.h"

typedef struct object_s object_t;
typedef struct type_s type_t;

/* Object tags */
typedef enum {
/* tag           | description      | val                             */
/* --------------+------------------+-------------------------------- */
   kObjHead,  /* | Scope head       | n.a.                            */
   kObjConst, /* | Constant         | value                           */
   kObjVar,   /* | Variable         | relative address                */
   kObjParam, /* | VAR param        | relative address                */
   kObjField, /* | record field     | offset from start of record     */
   kObjType,  /* | Type             | n.a.                            */
   kObjSProc, /* | Inline procedure | see StandProc in parser.c       */
   kObjSFunc, /* | Inline function  | see StandFunc in parser.c       */
   kObjMod,   /* | Module           | n.a.                            */
} class_t;

/* Type tags */
typedef enum {
  kTypeByte,      kTypeBool,      kTypeChar,      kTypeInt,       kTypeSet,
  kTypePointer,   kTypeNil,       kTypeNone,      kTypeProc,      kTypeString,
  kTypeArray,     kTypeRecord
} form_t;

struct object_s {
  object_t *    rlink;
  object_t *    dlink;   /* Parent scope (kObjHead) or declarations (kObjMod)*/
  type_t *      type;
  char          name[kIdLen];
  class_t       tag;
  int           level;   /* Static level difference (nested procedures) */
  bool          expo;    /* Whether exported */
  bool          rdo;     /* Whether read-only */
  int32_t       val;
};

struct type_s {
  type_t *      base;    /* Base type (records), element type (arrays), 
                          * or return type (procedures) */
  object_t *    typobj;  /* Identifier for this type */
  object_t *    dlink;   /* Parameters (procedure) or fields (record) */
  int           size;    /* In machine words (not bytes) */
  form_t        tag;
  union {                /* Redundant, but used for better code readability */
    int         len;     /* Length of an ARRAY */
    int         nofpar;  /* Number of PROCEDURE parameters */
    int         ext;     /* Extension level of RECORD types */
  }             u;
};

/* Predeclared types */
extern type_t       g_byte_type;
extern type_t       g_bool_type;
extern type_t       g_char_type;
extern type_t       g_int_type;
extern type_t       g_set_type;
extern type_t       g_nil_type;
extern type_t       g_no_type;
extern type_t       g_str_type;

/* Scope management */
extern object_t *   g_top_scope;

extern void         ORB_Init(void);
extern object_t *   ORB_New(const char * const, const class_t);
extern object_t *   ORB_NewModule(const char * const);
extern object_t *   ORB_This(void);
extern object_t *   ORB_ThisImport(object_t * const);
extern object_t *   ORB_ThisField(type_t * const);
extern void         ORB_OpenScope(void);
extern void         ORB_CloseScope(void);
extern void         ORB_Import(const char * const, const char * const);

#endif /* ORB_H_ */
