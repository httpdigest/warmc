#ifndef WARMC_JIT_H
#define WARMC_JIT_H

/* The JIT's compiler side (src/jit.c), as the C API (src/capi.c) sees it.

   A module compiles as `warmc -c` would compile it, into an image: its
   sections and the relocations they still need.  Loading the image places
   it in this process -- its text within branch range of the runtime the
   library carries, so that its calls of the runtime are the instructions a
   linked program has -- and gives back the module's descriptor, which the
   runtime instantiates as it does any other.

   Nothing here includes the compiler's headers or the runtime's: the two
   name some things alike, so each side includes its own and meets the
   other through this one. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Value kinds, numbered as wasm.h numbers them, and include/warm.h's for
   what it has no name for. */
enum {
  JIT_I32 = 0, JIT_I64 = 1, JIT_F32 = 2, JIT_F64 = 3, JIT_V128 = 4,
  JIT_EXTERNREF = 128, JIT_FUNCREF = 129, JIT_ANYREF = 130, JIT_EXNREF = 131
};

/* Extern kinds, numbered as the runtime's ImportDesc and ExportDesc number
   them (which is not wasm.h's order) */
enum { JIT_EXT_FUNC = 0, JIT_EXT_TABLE = 1, JIT_EXT_MEMORY = 2,
       JIT_EXT_GLOBAL = 3, JIT_EXT_TAG = 4 };

typedef struct {
  uint32_t nparams, nresults;
  uint8_t *types;               /* the parameters' kinds, then the results' */
} JitFuncType;

/* A C function a native import calls: the import's name, its address */
typedef struct {
  const char *name;
  void *fn;
} JitNative;

/* An import or export and its type as the module declares it */
typedef struct {
  char *module;                 /* an import's module name, else NULL */
  char *name;
  uint32_t module_len, name_len;
  uint8_t kind;                 /* JIT_EXT_* */
  uint8_t valkind;              /* a global's type, a table's elements */
  bool mutable_;                /* a global's */
  bool index64;                 /* a memory's or a table's */
  bool has_max;
  uint64_t min, max;            /* a memory's (in pages) or a table's limits */
  JitFuncType ft;               /* a function's or a tag's */
  /* a function import of a native module (JitOpts.native_modules): a call
     of a C function, bound when the module is compiled, no import of the
     C API's */
  bool native;
} JitExtern;

/* A function type and the id the compiler gives it, which a function
   reference's FuncRef.type_id is: structural, the same in every module */
typedef struct {
  uint64_t id;
  JitFuncType ft;
} JitTypeId;

/* One module's code and data in this process, and its descriptor (an
   rt/warm_rt.h ModuleDesc) */
typedef struct JitCode JitCode;

/* A compiled module, placed where it runs: its code, what it needs to be
   serialised (the image), what it imports and exports, and every function
   type it has. */
typedef struct JitImage JitImage;
typedef struct JitTier JitTier;
typedef struct {
  JitCode *code;
  JitImage *image;
  /* a tiered module's optimised code still to come or in place (jit.c),
     else NULL */
  JitTier *tier;
  JitExtern *imports; uint32_t nimports;
  JitExtern *exports; uint32_t nexports;
  JitTypeId *ftypes; uint32_t nftypes;
  /* a tag of what compiled it (target, warmc's build): an image only loads
     where the tag is the same */
  uint64_t build_tag;
} JitModule;

typedef struct {
  int opt_level;                /* 0 to 3, as -O */
  const char *const *flags;     /* more options, as warmc's -O and -f */
  uint32_t nflags;
  uint32_t threads;             /* how many compile at once; 0: the default */
  /* compile at -O0 first, which is quick, and at the level asked for
     behind it, on a thread of its own, sending each function's calls to
     its optimised code once that is in place */
  bool tiered;
  /* modules whose function imports are calls of C functions, as warmc's
     --native-module makes them, and the C functions they call by name */
  const char *const *native_modules;
  uint32_t nnative_modules;
  const JitNative *natives;
  uint32_t nnatives;
} JitOpts;

/* Compiles a binary module and places it; NULL with *err set (to a message
   the caller frees with jit_free) when it is malformed, invalid, a
   component, the options are wrong, or there is no room for it within
   reach of the runtime. */
JitModule *jit_compile(const uint8_t *bin, size_t len, const JitOpts *o,
                       char **err);
/* Decodes and validates only. */
bool jit_validate(const uint8_t *bin, size_t len, char **err);
/* The binary module a text one assembles to (jit_free frees it). */
uint8_t *jit_wat2wasm(const char *text, size_t len, size_t *out_len, char **err);

/* Reserves the address space beside the runtime that modules' code and
   data go in, before anything else can take it; a module's placing does
   so when no one has. */
void jit_reserve(void);

/* The module's descriptor, where its code runs */
const void *jit_module_desc(const JitModule *m);

/* The module as bytes, and back, placed again: what wasm_module_serialize
   and wasm_module_deserialize trade in. */
uint8_t *jit_serialize(const JitModule *m, size_t *len);
/* Whether the module calls C functions as native imports: a serialised
   module cannot say where they are */
bool jit_has_natives(const JitModule *m);
/* Whether the runtime has a symbol of this name */
bool jit_rt_has(const char *name);
JitModule *jit_deserialize(const uint8_t *p, size_t len, char **err);

/* Whether a tiered module's optimised code is in place (any other
   module's is from the start), waiting for it to be with wait: false
   when it never will be, its compile having failed. */
bool jit_module_optimized(const JitModule *m, bool wait);

/* Waits until every tiered module's optimised code is in place, or has
   failed. */
void jit_tiers_wait(void);

/* No optimised code goes in place for the module after this: what the
   module's release does first.  The descriptor of the optimised code when
   it is in place, which the runtime is to forget with the module's own,
   else NULL. */
const void *jit_module_detach_tier(JitModule *m);

/* Frees the module, and its code with unload -- when no instance uses it
   any more, nor the runtime's collector (its heap types) */
void jit_module_free(JitModule *m, bool unload);
void jit_free(void *p);

/* The runtime's symbols, which a module's code refers to by name
   (build/jit/rtsyms.c, generated from the runtime's objects). */
typedef struct { const char *name; void *addr; } JitRtSym;
extern const JitRtSym warmc_jit_rtsyms[];
extern const unsigned warmc_jit_nrtsyms;

#endif
