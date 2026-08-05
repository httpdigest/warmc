#ifndef WARMC_WIT_H
#define WARMC_WIT_H

#include "warmc.h"

#include <stdio.h>

#define WIT_NO_TYPE 0xffffffffu

typedef enum {
  WTY_BOOL, WTY_U8, WTY_U16, WTY_U32, WTY_U64,
  WTY_S8, WTY_S16, WTY_S32, WTY_S64, WTY_F32, WTY_F64,
  WTY_CHAR, WTY_STRING,
  WTY_TUPLE, WTY_LIST, WTY_FIXED_LIST, WTY_OPTION, WTY_RESULT,
  WTY_BORROW, WTY_OWN, WTY_FUTURE, WTY_STREAM, WTY_MAP,

  WTY_NAME
} WitTyKind;

typedef struct {
  WitTyKind kind;
  uint32_t *elems; uint32_t nelems;
  uint32_t elem;

  uint32_t ok, err;
  uint32_t key;
  uint32_t len;
  char *name;
  uint32_t def;
} WitTy;

typedef struct { char *name; uint32_t ty; } WitNamed;

typedef enum { WF_FREE, WF_METHOD, WF_STATIC, WF_CONSTRUCTOR } WitFuncKind;

typedef struct {
  char *name;
  WitFuncKind kind;
  bool is_async;
  WitNamed *params; uint32_t nparams;
  uint32_t result;
  char *external_id;
} WitFunc;

typedef enum {
  WD_ALIAS, WD_RECORD, WD_VARIANT, WD_ENUM, WD_FLAGS, WD_RESOURCE, WD_USE
} WitDefKind;

typedef struct {
  WitDefKind kind;
  char *name;
  uint32_t alias;
  WitNamed *cases; uint32_t ncases;
  char **labels; uint32_t nlabels;
  WitFunc *methods; uint32_t nmethods;

  char *path, *orig;

  uint32_t from_pkg, from_iface, from_def;
  char *external_id;
} WitTypeDef;

typedef struct {
  char *name;
  WitTypeDef *defs; uint32_t ndefs;
  WitFunc *funcs; uint32_t nfuncs;

  uint32_t ty_first, ty_last;

  bool inline_in_world;

  bool nested;

  char *pkg;
} WitInterface;

typedef enum { WW_IMPORT, WW_EXPORT } WitDir;
typedef enum {

  WWI_PATH,

  WWI_FUNC,

  WWI_INLINE
} WitWorldItemKind;

typedef struct {
  WitDir dir;
  WitWorldItemKind kind;
  char *name;
  WitFunc func;
  uint32_t pkg;
  uint32_t iface;

  char *external_id;
} WitWorldItem;

typedef struct {
  char *name;
  WitTypeDef *defs; uint32_t ndefs;
  WitWorldItem *items; uint32_t nitems;
  char **includes; uint32_t nincludes;
  bool nested;
  char *pkg;
  uint32_t ty_first, ty_last;
} WitWorld;

typedef struct {
  char *ns, *name, *version;
  char *id;
  WitInterface *ifaces; uint32_t nifaces;
  WitWorld *worlds; uint32_t nworlds;

  WitTy *types; uint32_t ntypes, cap_types;

  char **top_uses; char **top_names; uint32_t ntop_uses;
} WitPackage;

typedef struct {
  WitPackage **pkgs; uint32_t npkgs;
  uint32_t root;
} WitRegistry;

WitRegistry *wit_load(const char *path);
void wit_registry_free(WitRegistry *r);

void wit_registry_dump(const WitRegistry *r, FILE *out);

#endif
