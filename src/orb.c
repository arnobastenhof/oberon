/* Ported from ORB.Mod.txt, part of the Oberon07 reference implementation. */

#include "orb.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "pool.h"

#define TYPE(tag, size) { NULL, NULL, NULL, (size), (tag), { 0 } }

/* Prototypes */
static void         Enter(object_t ** const, const char * const, const class_t,
                          type_t * const, const int);

/* Predeclared types */
type_t              g_byte_type = TYPE(kTypeInt,    1);
type_t              g_bool_type = TYPE(kTypeBool,   1);
type_t              g_char_type = TYPE(kTypeChar,   1);
type_t              g_int_type  = TYPE(kTypeInt,    4);
type_t              g_set_type  = TYPE(kTypeSet,    4);
type_t              g_nil_type  = TYPE(kTypeNil,    4);
type_t              g_no_type   = TYPE(kTypeNone,   4);
type_t              g_str_type  = TYPE(kTypeString, 8);

/* Scopes */
object_t *          g_top_scope;  /* Current scope */
static object_t *   g_universe;   /* Root scope, incl. predecl. identifiers */
static object_t *   g_system;     /* SYSTEM module scope */

void
ORB_Init(void)
{
  object_t *        list;         /* Objects declared in the universal scope */
  object_t *        mod;          /* SYSTEM module */

  list = NULL;

  /* Functions in global scope */
  /* See StandFunc in orp.c for significance of last argument */
  Enter(&list,  "ROR",       kObjSFunc, &g_int_type,  72 );
  Enter(&list,  "ASR",       kObjSFunc, &g_int_type,  62 );
  Enter(&list,  "LSL",       kObjSFunc, &g_int_type,  52 );
  Enter(&list,  "LEN",       kObjSFunc, &g_int_type,  41 );
  Enter(&list,  "CHR",       kObjSFunc, &g_char_type, 31 );
  Enter(&list,  "ORD",       kObjSFunc, &g_int_type,  21 );
  Enter(&list,  "ODD",       kObjSFunc, &g_bool_type, 11 );
  Enter(&list,  "ABS",       kObjSFunc, &g_int_type,  1  );

  /* Procedures (see StandProc in orp.c) */
  Enter(&list,  "WriteLn",   kObjSProc, &g_no_type,   71 );
  Enter(&list,  "Write",     kObjSProc, &g_no_type,   61 );
  Enter(&list,  "Read",      kObjSProc, &g_no_type,   51 );
  Enter(&list,  "ASSERT",    kObjSProc, &g_no_type,   41 );
  Enter(&list,  "EXCL",      kObjSProc, &g_no_type,   32 );
  Enter(&list,  "INCL",      kObjSProc, &g_no_type,   22 );
  Enter(&list,  "DEC",       kObjSProc, &g_no_type,   11 );
  Enter(&list,  "INC",       kObjSProc, &g_no_type,   1  );

  /* Built-in basic types */
  Enter(&list,  "SET",       kObjType,  &g_set_type,  0  );
  Enter(&list,  "BOOLEAN",   kObjType,  &g_bool_type, 0  );
  Enter(&list,  "BYTE",      kObjType,  &g_byte_type, 0  );
  Enter(&list,  "CHAR",      kObjType,  &g_char_type, 0  );
  Enter(&list,  "INTEGER",   kObjType,  &g_int_type,  0  );

  /* Initialize universe */
  g_top_scope = NULL;
  ORB_OpenScope();
  g_top_scope->rlink = list;
  g_universe = g_top_scope;

  /* Initialize SYSTEM */
  g_system = NULL;
  Enter(&g_system, "COND",  kObjSFunc, &g_bool_type, 131);
  Enter(&g_system, "SIZE",  kObjSFunc, &g_int_type,  121);
  Enter(&g_system, "ADR",   kObjSFunc, &g_int_type,  111);
  Enter(&g_system, "VAL",   kObjSFunc, &g_int_type,  102);
  Enter(&g_system, "REG",   kObjSFunc, &g_int_type,  91);
  Enter(&g_system, "BIT",   kObjSFunc, &g_bool_type, 82);
  Enter(&g_system, "COPY",  kObjSProc, &g_no_type,   103);
  Enter(&g_system, "PUT",   kObjSProc, &g_no_type,   92);
  Enter(&g_system, "GET",   kObjSProc, &g_no_type,   82);

  /* Create module object for SYSTEM */
  mod = ORB_NewModule("SYSTEM");
  mod->rdo = true;
  mod->dlink = g_system;
  mod->rlink = g_top_scope->rlink;
  g_top_scope->rlink = mod;
}

object_t *
ORB_New(const char * const id, const class_t tag)
{
  object_t *    it;
  object_t *    new;

  assert(g_top_scope);

  /* Ensure identifier not already declared in current scope */
  it = g_top_scope;
  while (it->rlink && strcmp(it->rlink->name, id)) {
    it = it->rlink;
  }
  if (it->rlink) {
    ORS_Mark("mult def");
    return it->rlink;
  }

  /* Create a new object */
  new = Pool_Alloc(sizeof *new);
  new->rlink = NULL;
  new->dlink = NULL;
  new->type = &g_no_type;
  strcpy(new->name, id);
  new->tag = tag;
  new->rdo = false;

  /* Add to current scope */
  it->rlink = new;

  return new;
}

object_t *
ORB_NewModule(const char * const name)
{
  object_t *    new;

  new = Pool_Alloc(sizeof *new);
  new->rlink = NULL;
  new->dlink = NULL;
  new->type = &g_no_type;
  strcpy(new->name, name);
  new->tag = kObjMod;
  new->rdo = false;
  return new;
}

object_t *
ORB_This(void)
{
  object_t *    sc;
  object_t *    it;

  assert(g_top_scope);

  sc = g_top_scope;
  do {
    for (it = sc->rlink; it && strcmp(it->name, g_id); it = it->rlink)
      ;
    sc = sc->dlink;
  } while (!it && sc);

  return it;
}

object_t *
ORB_ThisImport(object_t * const mod)
{
  object_t *    it;

  assert(mod && mod->tag == kObjMod);

  /* Modules are marked as read-only upon export. */
  if (!mod->rdo) {
    return NULL;
  }

  for (it = mod->dlink; it && strcmp(it->name, g_id); it = it->rlink)
    ;
  return it;
}

object_t *
ORB_ThisField(type_t * const type)
{
  object_t *    fld;

  assert(type && type->tag == kTypeRecord);

  for (fld = type->dlink; fld && strcmp(fld->name, g_id); fld = fld->rlink)
    ;
  return fld;
}

void
ORB_OpenScope(void)
{
  object_t *    sc;

  sc = Pool_Alloc(sizeof *sc);
  sc->name[0] = '\0';           /* Scopes are not referenced by identifiers */
  sc->tag = kObjHead;
  sc->rlink = NULL;             /* Pointer to objects declared in this scope */
  sc->dlink = g_top_scope;      /* Pointer to parent scope */
  g_top_scope = sc;             /* Set new top scope */
}

void
ORB_CloseScope(void)
{
  assert(g_top_scope);

  g_top_scope = g_top_scope->dlink;
}

static void
Enter(object_t ** const list, const char * const name, const class_t tag, 
      type_t * const type, const int n)
{
  object_t *    obj;

  assert(list);
  assert(name);
  assert(type);

  obj = Pool_Alloc(sizeof *obj);
  strcpy(obj->name, name);
  obj->tag = tag;
  obj->type = type;
  obj->val = n;
  obj->dlink = NULL;
  obj->rlink = *list;
  *list = obj;

  if (tag == kObjType) {
    type->typobj = obj;
  }
}

#ifdef TEST

#include "minunit.h"

#define NEW_OBJ(obj, id, tag, typ) do {     \
  (obj) = ORB_New(id, (tag));               \
  (obj)->type = &(typ);                     \
} while (0)

#define ASSERT_OBJ(obj, id, typ) do {       \
  strcpy(g_id, (id));                       \
  (obj) = ORB_This();                       \
  ASSERT_NOT_NULL((obj));                   \
  ASSERT_EQ(&(typ), (obj)->type);           \
} while (0)

#define ASSERT_UNDEF(id) do {               \
  strcpy(g_id, (id));                       \
  ASSERT_NULL(ORB_This());                  \
} while (0)

char *
TestScopes(void)
{
  object_t *    obj;

  /* Initialization */
  Pool_Push();
  g_universe = NULL;
  ORB_OpenScope(); /* Universal scope */
  ORB_OpenScope();
  NEW_OBJ(obj, "x", kSymVar, g_int_type);
  NEW_OBJ(obj, "y", kSymVar, g_int_type);
  ORB_OpenScope();

  /* Asserts */
  ASSERT_OBJ(obj, "y", g_int_type);
  ASSERT_UNDEF("z");

  ORB_CloseScope();
  ASSERT_OBJ(obj, "x", g_int_type);
  ASSERT_OBJ(obj, "y", g_int_type);

  ORB_CloseScope();
  ASSERT_UNDEF("x");
  ASSERT_UNDEF("y");

  Pool_Pop();
  return NULL;
}

#endif /* TEST */
