#ifndef WARMC_H
#define WARMC_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buf.h"

enum { EXIT_FATAL = 1, EXIT_MALFORMED = 2, EXIT_INVALID = 3, EXIT_UNLINKABLE = 4 };

void warmc_fatal(const char *fmt, ...)
  __attribute__((noreturn, format(printf, 1, 2)));
void warmc_malformed(const char *fmt, ...)
  __attribute__((noreturn, format(printf, 1, 2)));
void warmc_invalid(const char *fmt, ...)
  __attribute__((noreturn, format(printf, 1, 2)));
/* A thread doing one item of a parallel walk catches the diagnostic that
   would end the process: warmc_fatal and its kin longjmp to jb with the
   message, and the walk reports the one of the first item in order, as a
   walk in order would have (util.c). */
typedef struct { jmp_buf jb; const char *tag; int code; char *msg; } DiagCatch;
/* sets the thread's catch and returns the one it replaces */
DiagCatch *warmc_diag_catch(DiagCatch *c);
/* raises c's diagnostic again: to the thread's catch if it has one, else
   it ends the process */
void warmc_diag_rethrow(const DiagCatch *c) __attribute__((noreturn));
/* While a decoder has a DiagHold open, warmc_invalid_held keeps the first
   invalid verdict it is given rather than raise it, and the decoder goes on
   past it with a stand-in: the spec decodes a whole module before it
   validates any of it, so a malformed part after an invalid one makes the
   module malformed.  With none open it is warmc_invalid.  Holds nest, and
   a diagnostic raised while one is open drops them all. */
typedef struct DiagHold { struct DiagHold *outer; char *msg; } DiagHold;
void warmc_hold_open(DiagHold *h);
/* closes h, the innermost hold, and returns the verdict it kept (the
   caller frees it) or NULL */
char *warmc_hold_close(DiagHold *h);
void warmc_invalid_held(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* The intermediate object of a link, removed if an error ends warmc before
   the link does; NULL once it is removed or kept. */
void warmc_temp_output(const char *path);
/* fn(ctx, i, w) for every i below n, on nthreads threads (w the thread's
   number below nthreads), in no order; returns when all are done. */
void warmc_par_for(uint32_t n, uint32_t nthreads,
                   void (*fn)(void *ctx, uint32_t i, uint32_t w), void *ctx);
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
void xfree(void *p);
void xcache_release(void);
/* The most an xmalloc of n bytes can hold without costing more memory: n
   rounded up to fill its size class. */
size_t xmalloc_fit(size_t n);
/* The process's peak resident memory so far, in MB; 0 where unknown. */
double peak_rss_mb(void);
/* getenv for the WARMC_ switches, from a copy taken on first use (util.c). */
const char *warmc_getenv(const char *name);
char *xstrndup(const char *s, size_t n);

typedef int32_t HeapType;
#define HT_NOFUNC   (-0x0d)
#define HT_NOEXTERN (-0x0e)
#define HT_NONE     (-0x0f)
#define HT_FUNC     (-0x10)
#define HT_EXTERN   (-0x11)
#define HT_ANY      (-0x12)
#define HT_EQ       (-0x13)
#define HT_I31      (-0x14)
#define HT_STRUCT   (-0x15)
#define HT_ARRAY    (-0x16)
#define HT_EXN      (-0x17)

#define HT_NOEXN    (-0x0c)

#define HT_BYTE_LO 0x69
#define HT_BYTE_HI 0x74
#define HT_OF_BYTE(b) ((HeapType)(b) - 0x80)

typedef uint32_t ValType;

#define VT_I32   0x7fu
#define VT_I64   0x7eu
#define VT_F32   0x7du
#define VT_F64   0x7cu
#define VT_V128  0x7bu

#define VT_REF   0x63u

#define VT_BOT   0x00u

#define VT_KIND_MASK  0xffu
#define VT_HEAP_SHIFT 8
#define VT_HEAP_BITS  22
#define VT_HEAP_MAX   ((1u << VT_HEAP_BITS) - 1u)
#define VT_CONCRETE   (1u << 30)
#define VT_NULLABLE   (1u << 31)

#define VT_REF_ABS(nul, ht)                                                    \
  ((ValType)(VT_REF | ((uint32_t)(-(ht)) << VT_HEAP_SHIFT) |                   \
             ((nul) ? VT_NULLABLE : 0u)))
#define VT_REF_IDX(nul, idx)                                                   \
  ((ValType)(VT_REF | ((uint32_t)(idx) << VT_HEAP_SHIFT) | VT_CONCRETE |       \
             ((nul) ? VT_NULLABLE : 0u)))

#define VT_FUNCREF   VT_REF_ABS(true, HT_FUNC)
#define VT_EXTERNREF VT_REF_ABS(true, HT_EXTERN)

static inline bool vt_is_ref(ValType t) { return (t & VT_KIND_MASK) == VT_REF; }
static inline bool vt_nullable(ValType t) { return (t & VT_NULLABLE) != 0; }
static inline bool vt_concrete(ValType t) { return (t & VT_CONCRETE) != 0; }
static inline uint32_t vt_type_index(ValType t) {
  return (t >> VT_HEAP_SHIFT) & VT_HEAP_MAX;
}
static inline HeapType vt_heap(ValType t) {
  uint32_t raw = vt_type_index(t);
  return vt_concrete(t) ? (HeapType)raw : -(HeapType)raw;
}
static inline ValType vt_ref(bool nullable, HeapType ht) {
  return ht >= 0 ? VT_REF_IDX(nullable, (uint32_t)ht) : VT_REF_ABS(nullable, ht);
}

static inline ValType vt_as_non_null(ValType t) { return t & ~VT_NULLABLE; }
static inline ValType vt_as_nullable(ValType t) { return t | VT_NULLABLE; }

#define VT_I8   0x78u
#define VT_I16  0x77u
static inline bool vt_is_packed(ValType t) { return t == VT_I8 || t == VT_I16; }

static inline bool vt_is_float(ValType t) { return t == VT_F32 || t == VT_F64; }
static inline bool vt_is_64(ValType t) { return t == VT_I64 || t == VT_F64 || vt_is_ref(t); }

static inline bool vt_is_vec_reg(ValType t) { return vt_is_float(t) || t == VT_V128; }

static inline uint32_t vt_width(ValType t) {
  if (t == VT_V128) return 16u;
  if (t == VT_I8) return 1u;
  if (t == VT_I16) return 2u;
  return vt_is_64(t) ? 8u : 4u;
}

static inline ValType vt_unpacked(ValType t) {
  return vt_is_packed(t) ? VT_I32 : t;
}

static inline bool vt_defaultable(ValType t) {
  return !vt_is_ref(t) || vt_nullable(t);
}

static inline uint8_t vt_abi_code(ValType t) {
  return vt_is_ref(t) ? 0x70u : (uint8_t)(t & VT_KIND_MASK);
}
const char *vt_name(ValType t);

typedef struct {
  ValType type;
  bool mutable_;

  uint32_t offset;
} FieldType;

typedef enum { CT_FUNC = 0, CT_STRUCT = 1, CT_ARRAY = 2 } CompKind;

enum {
  RTK_ANY = 0, RTK_EQ, RTK_I31, RTK_STRUCT, RTK_ARRAY,
  RTK_NONE, RTK_FUNC, RTK_NOFUNC, RTK_EXTERN, RTK_NOEXTERN,
  RTK_EXN, RTK_NOEXN,
  RTK_CONCRETE_ANY, RTK_CONCRETE_FUNC
};
#define RTK_NULLABLE 0x80u

#define GC_HDR_BYTES 8u
#define GC_HDR_WIDE  16u

#define GC_HDR_FOR(align) ((align) >= 16u ? GC_HDR_WIDE : GC_HDR_BYTES)

#define GC_HDR_WORD_OFF 0u

#define GC_TYPE_SHIFT   0

#define EXN_VALS_OFF 32u

#define STRUCT_MAX_BYTES (1u << 28)

typedef struct {
  uint8_t kind;

  ValType *params;
  uint32_t nparams;
  ValType *results;
  uint32_t nresults;

  FieldType *fields;
  uint32_t nfields;

  uint32_t body_size;

  uint32_t hdr_size;
  bool final_;
  bool has_super;
  uint32_t super;
  uint32_t rec_first, rec_n;
  uint32_t depth;
  uint64_t id;

  uint64_t *display;
} CompType;

typedef enum {
  EK_FUNC = 0, EK_TABLE = 1, EK_MEM = 2, EK_GLOBAL = 3, EK_TAG = 4
} ExternKind;

typedef struct {
  char *module;
  char *name;
  uint32_t module_len, name_len;
  ExternKind kind;
  uint32_t type_index;
  bool mutable_;
  uint64_t min, max;
  bool has_max;
} Import;

typedef struct {
  char *name;
  uint32_t name_len;
  ExternKind kind;
  uint32_t index;
} Export;

typedef struct {
  uint32_t type_index;

  const uint8_t *body;
  const uint8_t *body_end;
  uint32_t nlocals;
  ValType *local_types;
  uint32_t ntotal_locals;
  bool imported;
  uint32_t import_index;
  char *sym;
  char *name;   /* the name section's name for it, or NULL */
} Func;

typedef enum {
#define CONST_OP(name, extra) IOP_##name,
#include "../rt/const_ops.inc"
  IOP_COUNT
} InitOp;

typedef struct {
  uint8_t op;
  uint64_t lo, hi;
} InitInsn;

typedef struct {
  InitInsn *ins;
  uint32_t n;
} InitExpr;

typedef struct {
  ValType elem_type;
  uint64_t min, max;
  bool has_max;
  bool imported;
  bool index64;

  bool has_init;
  InitExpr init;
} Table;

typedef struct {
  uint64_t min, max;
  bool has_max;
  bool imported;
  bool index64;
} Memory;

typedef struct {
  ValType type;
  bool mutable_;
  InitExpr init;
  bool imported;
} Global;

typedef struct {
  uint32_t type_index;
  bool imported;
} Tag;

typedef enum { SEG_ACTIVE, SEG_PASSIVE, SEG_DECLARATIVE } SegMode;

typedef struct {
  SegMode mode;
  uint32_t table_index;
  InitExpr offset;
  ValType elem_type;
  uint32_t n;
  InitExpr *items;
} ElemSeg;

typedef struct {
  SegMode mode;
  uint32_t mem_index;
  InitExpr offset;
  const uint8_t *data;
  uint32_t len;
} DataSeg;

/* Table 0 as instantiation leaves it, when nothing can change it after
   (opt.c's module_table0_facts): devirtualisation and the call_indirect code
   tables both read it.  entry[k] is the function slot k holds, or
   TABLE0_NULL, for n == the table's length slots, and is NULL when the
   elem segments do not say statically what each slot holds.  sites counts,
   per type index, the call_indirects through table 0 that name it. */
#define TABLE0_NULL 0xffffffffu
typedef struct {
  bool frozen;
  uint32_t init_fi;
  uint32_t n;
  uint32_t *entry;
  uint32_t *sites;
} Table0Facts;

/* One call type's code table (module_data.c): slot k holds the code of
   table 0's entry lo + k when that entry passes the call's type check, and
   a stub that traps as the check would have otherwise, so a call through it
   needs neither the check nor the funcref cell.  Indices outside
   [lo, lo + n) take the full lookup out of line, unless `whole` says the
   window covers every index below the table's length, so that an index past
   it is out of bounds and nothing else. */
typedef struct {
  uint64_t type_id;
  uint32_t ti;
  uint32_t lo, n;
  bool whole;
  uint32_t sym;
} IcTable;

/* A custom section: its name and payload, in the binary the module was
   read from */
typedef struct {
  const uint8_t *name, *data;
  uint32_t name_len, len;
} CustomSec;

typedef struct {
  const uint8_t *bin;
  size_t bin_len;

  CompType *types; uint32_t ntypes;
  uint64_t total_locals;
  uint32_t max_const_depth;
  Import *imports;  uint32_t nimports;
  Func *funcs;      uint32_t nfuncs;
  uint32_t nimported_funcs;
  Table *tables;    uint32_t ntables;
  Memory *memories; uint32_t nmemories;
  Global *globals;  uint32_t nglobals;
  uint32_t nimported_globals;
  Tag *tags;        uint32_t ntags;
  uint32_t nimported_tags;
  Export *exports;  uint32_t nexports;
  ElemSeg *elems;   uint32_t nelems;
  DataSeg *datas;   uint32_t ndatas;
  bool has_start; uint32_t start_func;
  bool has_datacount; uint32_t datacount;
  /* a go:buildid section: Go's compiler made it, whose stack is not C's
     (arguments above the callee's stack pointer, frames re-entered after
     the wasm stack unwinds) */
  bool go_stack;
  /* every custom section, in the binary's order */
  CustomSec *customs; uint32_t ncustoms;
  /* bodies a module transform wrote (tramp.c), which funcs[] point into in
     place of the binary */
  uint8_t **owned_bodies;
  uint32_t nowned_bodies;

  bool *reached;

  bool *type_live;

  bool *type_entry;

  bool *glob_read;

  bool *data_read;

  uint32_t *func_map;

  uint32_t *type_slot;

  Table0Facts *t0;

  /* the call_indirect code tables, and per type index the one its calls
     use, or UINT32_MAX (NULL when there are none) */
  IcTable *ict; uint32_t nict;
  uint32_t *ict_of;
} Module;

/* The threads compile_module uses for a module of nfuncs functions. */
uint32_t compile_want_threads(Module *m, uint32_t nfuncs);

const Table0Facts *module_table0_facts(Module *m);

static inline uint32_t gctype_slot(const Module *m, uint32_t ti) {
  return m->type_slot ? m->type_slot[ti] : ti;
}

static inline bool vt_ref_collectable(const Module *m, ValType t) {
  if (!vt_is_ref(t)) return false;
  if (vt_concrete(t)) {
    uint32_t i = vt_type_index(t);
    return !(i < m->ntypes && m->types[i].kind == CT_FUNC);
  }
  switch (vt_heap(t)) {
    case HT_FUNC: case HT_NOFUNC: case HT_NOEXTERN: case HT_NONE:
    case HT_NOEXN: case HT_I31:
      return false;
    default:
      return true;
  }
}

Module *wasm_parse(const uint8_t *bin, size_t len);
/* Decodes every body of m, validating none, and raises the first that does
   not decode as malformed: the spec decodes a whole module before it
   validates any of it, so this runs before a verdict of invalid is given. */
void wasm_decode_bodies(const Module *m);
void wasm_check_utf8(const uint8_t *p, uint32_t n);
/* the length of the longest prefix of p[0..n) that is well-formed UTF-8 */
size_t wasm_utf8_prefix(const uint8_t *p, size_t n);
void wasm_validate(Module *m);
void module_free(Module *m);
uint32_t module_add_distinct_func_type(Module *m, const ValType *params, uint32_t nparams,
                                       const ValType *results, uint32_t nresults);
uint32_t *module_type_canon(const Module *m);
void wasm_validate_funcs(Module *m, const uint32_t *fns, uint32_t n);
uint32_t module_untrampoline(Module *m);
uint32_t module_go_sp_args(Module *m);

bool vt_subtype(const Module *m, ValType a, ValType b);

bool heap_subtype(const Module *m, HeapType a, HeapType b);

ValType vt_join(const Module *m, ValType a, ValType b);

#define VMCTX_MEM_BASE      0
#define VMCTX_MEM_SIZE      8
#define VMCTX_MEM_PAGES     16
#define VMCTX_MEM_MAX_PAGES 24
#define VMCTX_TABLE_BASE    32
#define VMCTX_TABLE_LEN     40

#define VMCTX_GC_BADMASK    64
#define VMCTX_GC_LAB        72
#define VMCTX_GC_HDR        80
#define VMCTX_GLOBALS       96

#define GLOBAL_SLOT_SIZE    16

#define ARG_STACK_SLOT 16u

#define RESULT_SLOT_SIZE 16u

#define INVOKE_OUTGOING_BYTES 512u

#define INVOKE_MAX_RESULTS 16u

#define MAX_ARRAY_NEW_FIXED 10000u

#define WASM_PAGE_SIZE 65536

#define WASM_GUARD_OFFSET_MAX (32ull << 20)

#define TABLE_ENTRY_SIZE 8
#define FUNCREF_SIZE     32
#define FUNCREF_CODE     0
#define FUNCREF_INSTANCE 8
#define FUNCREF_TYPE     16
#define FUNCREF_RTT      24

#define VMCTX_IMPORTS(nglobals) (VMCTX_GLOBALS + GLOBAL_SLOT_SIZE * (nglobals))

#define VMCTX_TABLES(nglobals, nimpfuncs) \
  (VMCTX_IMPORTS(nglobals) + 16u * (nimpfuncs))
#define TABLE_SLOT_SIZE 16
#define TABLE_SLOT_ENTRIES 0
#define TABLE_SLOT_LEN 8

#define VMCTX_MEMS(nglobals, nimpfuncs, ntables) \
  (VMCTX_TABLES(nglobals, nimpfuncs) + TABLE_SLOT_SIZE * (ntables))
#define MEM_SLOT_SIZE 16
#define MEM_SLOT_BASE 0
#define MEM_SLOT_LEN 8

#define VMCTX_FUNCREFS(nglobals, nimpfuncs, ntables, nmemories) \
  (VMCTX_MEMS(nglobals, nimpfuncs, ntables) + MEM_SLOT_SIZE * (nmemories))

#define VTF_REF        1u
#define VTF_NULL       2u
#define VTF_BOTTOM     4u
#define VTF_TOP        8u
#define VTF_HIER_SHIFT 4

enum {
  TRAP_UNREACHABLE = 1,
  TRAP_MEM_OOB = 2,
  TRAP_DIV_ZERO = 3,
  TRAP_DIV_OVERFLOW = 4,
  TRAP_INDIRECT_OOB = 5,
  TRAP_INDIRECT_NULL = 6,
  TRAP_INDIRECT_TYPE = 7,
  TRAP_BAD_CONVERSION = 8,
  /* 9 is no trap: a null in a table is TRAP_INDIRECT_NULL's */
  TRAP_TABLE_OOB = 10,
  TRAP_STACK_OVERFLOW = 11,
  TRAP_NULL_REF = 12,

  TRAP_UNCAUGHT_EXN = 13,
  TRAP_HEAP_OOM = 14,
  TRAP_ARRAY_OOB = 15,
  TRAP_CAST = 16,
  TRAP_LINK = 100
};

#define TRAP_CODE_COUNT 17

typedef struct {
  uint32_t pc;
  uint32_t first;
  uint32_t n;
} UwSite;

typedef struct {
  uint32_t kind;
  uint32_t tag;
  uint32_t pad;
  uint32_t pad_;
} UwClause;

typedef struct { uint32_t pc, first, n; } GcSite;

#define GCROOT_REG(i)     ((uint32_t)(i) << 1)
#define GCROOT_SLOT(off)  ((((uint32_t)(off)) << 1) | 1u)
#define GCROOT_IS_SLOT(v) (((v) & 1u) != 0)
#define GCROOT_VAL(v)     ((v) >> 1)

#define UW_HAS_LR      1u
#define UW_GPR_SHIFT   8
#define UW_FPR_SHIFT  20

typedef struct {
  uint32_t func_index;
  uint32_t code_len;
  uint32_t frame, saved_bytes, exn_slot, fpsave_off;
  uint32_t saved;
  UwSite *sites; uint32_t nsites;
  UwClause *clauses; uint32_t nclauses;
  GcSite *gcsites; uint32_t ngcsites;
  uint32_t *gcroots; uint32_t ngcroots;
} FuncUnwind;

void codegen_note_unwind(const FuncUnwind *u);

void codegen_reset_unwind(void);

typedef enum {
  REL_CALL26,
  REL_ADR_PAGE21,
  REL_ADD_LO12,
  REL_LDST_LO12,
  REL_ABS64,

  // x64: S + A - P over a 4-byte field (call/jmp rel32 and RIP-relative
  // operands).  isel records addend -4 and pre-bakes it into the
  // instruction bytes, so COFF's in-situ-addend convention needs no
  // special casing.
  REL_PC32
} RelKind;

typedef struct {
  RelKind kind;
  int section;
  uint64_t offset;
  uint32_t sym;
  int64_t addend;
} Reloc;

typedef enum { SYM_LOCAL_FUNC, SYM_GLOBAL_FUNC, SYM_UNDEF_FUNC, SYM_LOCAL_DATA, SYM_GLOBAL_DATA, SYM_UNDEF_DATA } SymKind;

typedef struct {
  char *name;
  /* the name the object's symbol table gives it, when that is not `name`:
     a function's from the name section.  Only a local has one, which no
     reference resolves by name, so it may be any name at all. */
  char *display;
  SymKind kind;
  int section;
  uint64_t value;
  uint64_t size;
} Sym;

typedef struct {
  Sym *syms; uint32_t nsyms, cap_syms;

  uint32_t *sym_ix; uint32_t sym_ix_cap;

  bool frozen;
  Buf text;
  Buf rodata;
  Buf data;
  /* section 3: the module's custom sections (module_data.c), read-only and
     in a section of their own, so that a tool can find them in what is
     linked; written only when there are any */
  Buf custom;
  Reloc *relocs; uint32_t nrelocs, cap_relocs;
} ObjBuilder;

uint32_t obj_add_sym(ObjBuilder *o, const char *name, SymKind kind, int section, uint64_t value);
uint32_t obj_find_sym(ObjBuilder *o, const char *name);

typedef enum { TGT_ELF, TGT_MACHO, TGT_PE } TargetFmt;

void write_coff64(ObjBuilder *o, const char *path);

void warmc_link_exec(TargetFmt fmt, const char *out, const char *objpath,
                     const char *wasi, const char *rt, const char *entry,
                     bool strip);
/* A shared library whose dynamic symbols are exactly exports[] (C names). */
void warmc_link_shared(TargetFmt fmt, const char *out, const char *objpath,
                       const char *rt, char *const *exports, uint32_t nexports,
                       bool strip);

/* shlib.c: warmc --shared */
typedef struct Shlib Shlib;
Shlib *shlib_plan(Module *m, const char *prefix, const char *input);
void shlib_emit(Shlib *p, ObjBuilder *o);
char **shlib_symbols(Shlib *p, uint32_t *n);
void shlib_write_header(Shlib *p, const char *path, const char *libname);
void shlib_write_def(Shlib *p, const char *path, const char *dllname);
void shlib_free(Shlib *p);
uint32_t obj_intern_sym(ObjBuilder *o, const char *name, SymKind kind, int section, uint64_t value);
void obj_add_reloc(ObjBuilder *o, RelKind kind, int section, uint64_t off, uint32_t sym, int64_t addend);

void codegen_prepare_module(Module *m, ObjBuilder *o);

void codegen_set_symbol_prefix(const char *prefix);
/* which custom sections a module's output carries (module_data.c) */
typedef enum { CUSTOM_DEFAULT, CUSTOM_ALL, CUSTOM_NONE } CustomMode;
void codegen_set_custom_sections(CustomMode mode);
/* whether a function's symbol takes its name from the name section */
void codegen_set_function_names(bool on);

void codegen_set_load_barrier(bool on);
extern bool warmc_load_barrier;

void codegen_rename_funcs(Module *m, const char *prefix);

struct LinkPlan;
void codegen_emit_component(const struct LinkPlan *p, ObjBuilder *o);

uint64_t wasm_type_hash(const CompType *t);
void codegen_emit_module_data(Module *m, ObjBuilder *o);
uint32_t codegen_tail_out_bytes(Module *m);
extern bool g_guard_pages;

extern bool g_det_profile;

extern bool g_no_export_names;
/* Touch each page of a frame over one before taking it: a Windows DLL runs
   on its host's threads, whose stacks are committed a guard page at a time
   (warmc --shared). */
extern bool g_stack_probes;

extern bool g_profile;

void write_elf64(ObjBuilder *o, const char *path);
void write_macho64(ObjBuilder *o, const char *path);

#endif
