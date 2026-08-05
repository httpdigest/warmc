#ifndef WARMC_CANON_H
#define WARMC_CANON_H

#include "component.h"

typedef enum { CF_I32, CF_I64, CF_F32, CF_F64 } CanonFlat;

typedef struct CanonScope {
  const struct CanonScope *outer;
  const Component *c;
  const CmDeclList *dl;
} CanonScope;

const CmType *canon_type_in(const CanonScope *sc, uint32_t i);

const CmFuncType *canon_func_type_in(const CanonScope *sc, uint32_t i,
                                     CanonScope *at);

typedef enum { CSE_UTF8, CSE_UTF16, CSE_LATIN1_UTF16 } CanonStrEnc;

typedef struct {
  const Component *c;
  const CanonScope *scope;
  bool ptr64;
  CanonStrEnc str_enc;
  CanonStrEnc to_enc;
  uint32_t mem_from;
  uint32_t mem_to;
} CanonCtx;

#define CANON_MAX_FLAT_PARAMS  16
#define CANON_MAX_FLAT_RESULTS 1

#define CANON_MAX_SIG 17

typedef struct {
  CanonFlat params[CANON_MAX_SIG]; uint32_t nparams;
  CanonFlat results[CANON_MAX_SIG]; uint32_t nresults;
} CanonSig;

typedef struct {
  bool has_memory;      uint32_t memory;
  bool has_realloc;     uint32_t realloc;
  bool has_post_return; uint32_t post_return;
  bool has_callback;    uint32_t callback;
  CanonStrEnc string_encoding;
  bool is_async;
} CanonOpts;

void canon_options(const CmCanon *cn, bool lift, CanonOpts *out);

bool canon_needs_memory(const CanonCtx *cx, const CmFuncType *ft, bool lift);

uint32_t canon_align(const CanonCtx *cx, CmValType t);
uint32_t canon_size(const CanonCtx *cx, CmValType t);

uint32_t canon_flatten(const CanonCtx *cx, CmValType t, CanonFlat *out, uint32_t cap);

void canon_flatten_functype(const CanonCtx *cx, const CmFuncType *ft, bool lift,
                            CanonSig *out);

bool canon_functype_same(const CanonCtx *ca, const CmFuncType *fa,
                         const CanonCtx *cb, const CmFuncType *fb);

const char *canon_flat_name(CanonFlat f);

void canon_dump(const Component *c, bool ptr64, FILE *out);

void canon_dump_adapters(const Component *c, bool ptr64, FILE *out);

typedef enum {
  CAI_CALLER_MEM,
  CAI_CALLEE_MEM,
  CAI_CALLER_REALLOC,
  CAI_CALLEE_REALLOC,
  CAI_CALLEE_FUNC,
  CAI_CALLEE_POST
} CanonAdapterImport;

#define CANON_MAX_ADAPTER_IMPORTS 6

typedef struct {
  uint8_t kind[CANON_MAX_ADAPTER_IMPORTS];
  uint32_t n;
} CanonAdapterImports;

#define CANON_ADAPTER_EXPORT "adapt"

const char *canon_adapter_why_not(const CanonCtx *dn, const CanonCtx *up,
                                  const CmFuncType *ft);

char *canon_adapter_text(const CanonCtx *dn, const CanonCtx *up,
                         const CmFuncType *ft, bool post,
                         CanonAdapterImports *imports);

#endif
