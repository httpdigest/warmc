#ifndef WARMC_CLINK_H
#define WARMC_CLINK_H

#include "component.h"
#include "canon.h"
#include "ir.h"

typedef enum {

  LD_EXPORT,

  LD_CANON,

  LD_HOST,

  LD_UNRESOLVED
} LinkDefKind;

typedef struct {
  LinkDefKind kind;
  ExternKind sort;
  uint32_t inst, exp;
  uint32_t canon;
  const char *host_name;
  const char *why;
} LinkDef;

typedef struct {
  Module *m;
  char prefix[24];

  uint8_t *bytes;
} LinkModule;

typedef struct {
  uint32_t module;
  LinkDef *binds; uint32_t nbinds;
} LinkInst;

typedef struct {
  bool lift;
  LinkDef core;
  uint32_t func;
  const Component *c;
  uint32_t cinst;
  const CmFuncType *ft;
  CanonOpts opts;
  LinkDef memory, realloc, post_return;

  CanonSig sig;
  bool needs_memory;

  bool builtin;
  const char *builtin_name;

  bool host;
  const char *host_iface;
  const char *host_fn;
  char *host_name;

  bool resource;
  uint32_t res_rt;
  uint8_t res_op;
  bool has_dtor;
  LinkDef dtor;

  char *adapter;
  CanonAdapterImports aimports;
  uint32_t adapter_inst;
} LinkCanon;

typedef struct {
  bool import;
  char *impname;
  uint32_t canon;

  bool host;
  const char *host_iface;
} LinkFunc;

typedef struct {
  char *name;
  uint32_t func;
} LinkExport;

typedef struct LinkPlan {
  LinkModule *modules; uint32_t nmodules;
  LinkInst *insts;     uint32_t ninsts;
  LinkCanon *canons;   uint32_t ncanons;
  LinkFunc *funcs;     uint32_t nfuncs;
  LinkExport *exports; uint32_t nexports;

  char **blockers; uint32_t nblockers;
} LinkPlan;

#define CLINK_COMMAND_EXPORT "wasi:cli/run@0.2.x#run"

bool clink_host_iface(const char *name);
bool clink_is_command(const char *export_name);

LinkPlan *clink_plan(const Component *c);
void clink_free(LinkPlan *p);

void clink_dump(const LinkPlan *p, FILE *out);

const char *clink_why_not(const LinkPlan *p);

void clink_compile(LinkPlan *p, ObjBuilder *o, const OptOpts *opts);

#endif
