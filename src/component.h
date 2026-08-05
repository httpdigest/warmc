#ifndef WARMC_COMPONENT_H
#define WARMC_COMPONENT_H

#include "warmc.h"

#include <stdio.h>

enum {
  CMP_BOOL = 0x7f, CMP_S8 = 0x7e, CMP_U8 = 0x7d, CMP_S16 = 0x7c,
  CMP_U16 = 0x7b, CMP_S32 = 0x7a, CMP_U32 = 0x79, CMP_S64 = 0x78,
  CMP_U64 = 0x77, CMP_F32 = 0x76, CMP_F64 = 0x75, CMP_CHAR = 0x74,
  CMP_STRING = 0x73, CMP_ERROR_CONTEXT = 0x64
};

typedef struct {
  bool is_index;
  uint32_t index;
  uint8_t prim;
} CmValType;

typedef struct { bool present; CmValType t; } CmOptValType;

typedef struct { char *label; CmValType t; } CmField;
typedef struct { char *label; CmOptValType t; } CmCase;

typedef enum {
  CMD_PRIM, CMD_RECORD, CMD_VARIANT, CMD_LIST, CMD_FIXED_LIST, CMD_TUPLE,
  CMD_FLAGS, CMD_ENUM, CMD_OPTION, CMD_RESULT, CMD_OWN, CMD_BORROW,
  CMD_STREAM, CMD_FUTURE
} CmDefValKind;

typedef struct {
  CmDefValKind kind;
  uint8_t prim;
  CmField *fields; uint32_t nfields;
  CmCase *cases; uint32_t ncases;
  char **labels; uint32_t nlabels;
  CmValType *elems; uint32_t nelems;
  CmValType elem;
  uint32_t len;
  CmOptValType ok, err;
} CmDefVal;

typedef struct {
  CmField *params; uint32_t nparams;
  bool has_result;
  CmValType result;
  bool is_async;
} CmFuncType;

typedef enum {
  CME_CORE_MODULE = 0, CME_FUNC = 1, CME_VALUE = 2, CME_TYPE = 3,
  CME_COMPONENT = 4, CME_INSTANCE = 5
} CmExternKind;

typedef struct {
  CmExternKind kind;
  uint32_t index;
  uint8_t bound;
} CmExternType;

enum {
  CMS_CORE = 0x00, CMS_FUNC = 0x01, CMS_VALUE = 0x02, CMS_TYPE = 0x03,
  CMS_COMPONENT = 0x04, CMS_INSTANCE = 0x05
};
enum {
  CMCS_FUNC = 0x00, CMCS_TABLE = 0x01, CMCS_MEMORY = 0x02, CMCS_GLOBAL = 0x03,
  CMCS_TAG = 0x04, CMCS_TYPE = 0x10, CMCS_MODULE = 0x11, CMCS_INSTANCE = 0x12
};

typedef struct {
  uint8_t sort;
  uint8_t core_sort;
  uint32_t index;
} CmSortIdx;

typedef enum { CMTS_DECLARED, CMTS_ALIAS, CMTS_IMPORT, CMTS_EXPORT } CmTypeSlotKind;

typedef struct {
  CmTypeSlotKind kind;
  uint32_t index;
} CmTypeSlot;

typedef enum {
  CMFS_IMPORT, CMFS_ALIAS, CMFS_LIFT, CMFS_EXPORT, CMFS_DECL
} CmFuncSlotKind;

typedef struct {
  CmFuncSlotKind kind;
  uint32_t index;
} CmFuncSlot;

typedef enum {
  CMIS_IMPORT, CMIS_ALIAS, CMIS_INSTANTIATE, CMIS_EXPORT, CMIS_DECL
} CmInstSlotKind;

typedef struct {
  CmInstSlotKind kind;
  uint32_t index;
} CmInstSlot;

typedef enum { CMCF_ALIAS, CMCF_LOWER, CMCF_BUILTIN } CmCoreFuncSlotKind;

typedef struct {
  CmCoreFuncSlotKind kind;
  uint32_t index;
} CmCoreFuncSlot;

typedef enum {
  CMO_MODULE, CMO_CORE_INSTANCE, CMO_CORE_TYPE, CMO_COMPONENT, CMO_INSTANCE,
  CMO_ALIAS, CMO_TYPE, CMO_CANON, CMO_IMPORT, CMO_EXPORT, CMO_START
} CmOrderKind;

typedef struct { CmOrderKind kind; uint32_t index; } CmOrderEntry;

typedef enum {
  CMDECL_CORE_TYPE, CMDECL_TYPE, CMDECL_ALIAS, CMDECL_EXPORT, CMDECL_IMPORT
} CmDeclKind;

typedef struct CmDecl CmDecl;

typedef enum { CMA_EXPORT, CMA_CORE_EXPORT, CMA_OUTER } CmAliasKind;

typedef struct {
  CmAliasKind kind;
  CmSortIdx target;
  uint32_t instance;
  char *name;
  uint32_t count, index;
} CmAlias;

typedef struct {
  CmDecl *decls; uint32_t ndecls;

  CmTypeSlot *type_space; uint32_t ntype_space;
  CmFuncSlot *func_space; uint32_t nfunc_space;
  CmInstSlot *inst_space; uint32_t ninst_space;
} CmDeclList;

typedef enum { CMT_DEFVAL, CMT_FUNC, CMT_COMPONENT, CMT_INSTANCE, CMT_RESOURCE } CmTypeKind;

typedef struct {
  CmTypeKind kind;
  CmDefVal defval;
  CmFuncType func;
  CmDeclList decls;
  CmValType rep;
  bool has_dtor; uint32_t dtor;
} CmType;

struct CmDecl {
  CmDeclKind kind;
  char *name;
  CmExternType et;
  CmType type;
  CmAlias alias;
};

typedef struct {
  bool instantiate;
  uint32_t index;
  char **arg_names; CmSortIdx *args; uint32_t nargs;
  char **export_names; CmSortIdx *exports; uint32_t nexports;
} CmInstance;

typedef enum {
  CMC_LIFT, CMC_LOWER, CMC_RESOURCE_NEW, CMC_RESOURCE_DROP, CMC_RESOURCE_REP,
  CMC_UNSUPPORTED
} CmCanonKind;

typedef struct {
  uint8_t code;
  uint32_t index;
} CmCanonOpt;

typedef struct {
  CmCanonKind kind;
  uint32_t opcode;
  uint32_t func;
  uint32_t type;
  CmCanonOpt *opts; uint32_t nopts;
} CmCanon;

typedef struct {
  char *name;
  CmExternType et;
} CmImport;

typedef struct {
  char *name;
  CmSortIdx si;
  bool has_type;
  CmExternType et;
} CmExport;

typedef struct Component Component;

struct Component {

  Module **modules; uint32_t nmodules;
  Component **components; uint32_t ncomponents;

  CmInstance *core_instances; uint32_t ncore_instances;
  CmInstance *instances; uint32_t ninstances;
  CmAlias *aliases; uint32_t naliases;
  CmType *types; uint32_t ntypes;
  CmTypeSlot *type_space; uint32_t ntype_space;
  CmFuncSlot *func_space; uint32_t nfunc_space;
  CmInstSlot *inst_space; uint32_t ninst_space;
  CmCoreFuncSlot *cfunc_space; uint32_t ncfunc_space;
  CmType *core_types; uint32_t ncore_types;
  CmCanon *canons; uint32_t ncanons;
  CmImport *imports; uint32_t nimports;
  CmExport *exports; uint32_t nexports;

  CmOrderEntry *order; uint32_t norder;

  bool has_start;
  uint32_t start_func, start_nargs, start_nresults;
  uint32_t *start_args;

  uint32_t n_core_func, n_core_table, n_core_memory, n_core_global;
  uint32_t n_core_tag, n_core_type, n_core_module, n_core_instance;
  uint32_t n_func, n_value, n_type, n_component, n_instance;
};

bool wasm_is_component(const uint8_t *bin, size_t len);

Component *comp_parse(const uint8_t *bin, size_t len);
void comp_free(Component *c);

void comp_dump(const Component *c, FILE *out);

void wit_dump_component(const Component *c, FILE *out);

#endif
