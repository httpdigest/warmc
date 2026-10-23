#ifndef WARM_RT_H
#define WARM_RT_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char i8;
typedef short i16;
typedef int i32;
typedef long long i64;

/* Pointer-sized integers.  A plain long is NOT one on LLP64 Windows,
   so pointer casts and size parameters go through these instead. */
typedef __UINTPTR_TYPE__ uptr;
typedef __SIZE_TYPE__ usz;

_Static_assert(sizeof(void *) == 8, "warmc runtime targets 64-bit only");
_Static_assert(sizeof(uptr) == 8 && sizeof(usz) == 8, "64-bit only");

#define WASM_PAGE_SIZE 65536

typedef struct {
  u8 *mem_base;
  u64 mem_size;
  u64 mem_pages;
  u64 mem_max_pages;
  void *table_base;
  u64 table_len;
  u64 stack_limit;
  u64 icount;

  u64 gc_badmask;

  u64 gc_lab;
  u64 gc_hdr_base;

  u64 gc_reserved;
  u64 globals[];
} WarmInstance;

_Static_assert(sizeof(WarmInstance) == 96, "instance header must be 96 bytes");

typedef struct {
  void *code;
  void *instance;
  u64 type_id;

  const u64 *rtt;
} FuncRef;

typedef struct {
  void *code;
  u64 type_id;
  const u64 *rtt;
} FuncRefTemplate;

typedef void *TableEntry;

typedef enum {
#define CONST_OP(name, extra) IOP_##name,
#include "const_ops.inc"
  IOP_COUNT
} ConstOpKind;

typedef struct {
  u32 op;
  u32 a;
} ConstOp;

static inline u32 const_op_units(u32 op) {
  switch (op) {
#define CONST_OP(name, extra) case IOP_##name: return 1u + (u32)(extra);
#include "const_ops.inc"
    default: return 1u;
  }
}

static inline u64 const_op_wide(const ConstOp *p, u32 which) {
  const u32 *w = (const u32 *)(const void *)(p + 1 + which);
  return (u64)w[0] | ((u64)w[1] << 32);
}

#define GLOBAL_IMPORTED    1u

#define GLOBAL_NOINIT      2u
typedef struct {
  u8 type;
  u8 flags;
  u16 pad;
  u32 init;
} GlobalInitDesc;

#define DSEG_PASSIVE      1u

#define DSEG_OFF_CONST    2u
#define DSEG_MEM_INDEX(i) ((i) >> 8)

typedef struct {
  u32 blob_off;
  u32 len;
  u32 offset_init;
  u32 info;
} DataSegDesc;
_Static_assert(sizeof(DataSegDesc) == 16,
               "src/module_data.c writes sixteen bytes for each data segment");

typedef struct {
  u32 mode;
  u32 table_index;
  u32 offset_init;
  u32 pad;
  u64 first;
  u64 n;
} ElemSegDesc;

#define VTF_REF        1u
#define VTF_NULL       2u
#define VTF_BOTTOM     4u
#define VTF_TOP        8u
#define VTF_HIER_SHIFT 4

typedef struct {
  const char *name;
  u32 name_len;
  u32 kind;
  u32 index;
  u32 nparams;
  u32 nresults;
  u32 pad0;
  const u8 *ptypes;
  const u8 *rtypes;

  u64 type_id;
  u32 mutable_;
  u32 vt_flags;
  u64 heap_id;
  const u64 *supers;
} ExportDesc;

typedef struct {
  const char *module;
  const char *name;
  u64 type_id;
  u64 min;
  u64 max;
  u32 kind;
  u32 mutable_;
  u32 has_max;
  u32 index64;
  u32 vt_flags;
  u32 name_len;
  u64 heap_id;
  u32 module_len, pad3;
} ImportDesc;

typedef struct {
  u64 min, max;
  u32 has_max;
  u32 elem_type;
  u32 imported;
  u32 index64;

  u32 has_init;
  u32 init;
} TableDesc;

typedef struct {
  u64 min, max;
  u32 has_max;
  u32 imported;
  u32 index64;
  u32 pad;
} MemoryDesc;

typedef struct {
  u32 imported;
  u32 pad;
  u64 type_id;
} TagDesc;

typedef struct { u32 offset, width; } GcField;

typedef struct {
  i32 rtt;

  i32 refoffs;

  i32 fields;
  u32 body_size;
  u32 nrefs;
  u32 nfields;
  u8 kind;
  u8 elem_is_ref;

  u8 hdr_size;
  u8 pad;
} GcTypeInfo;

static inline const u64 *gcti_rtt(const GcTypeInfo *t) {
  return (const u64 *)(const void *)((const u8 *)t + t->rtt);
}
static inline const u32 *gcti_refoffs(const GcTypeInfo *t) {
  return (const u32 *)(const void *)((const u8 *)t + t->refoffs);
}
static inline const GcField *gcti_fields(const GcTypeInfo *t) {
  return (const GcField *)(const void *)((const u8 *)t + t->fields);
}

enum { GCK_STRUCT = 0, GCK_ARRAY = 1, GCK_EXN = 2 };

typedef struct {
  u32 w;
  u32 len;
} GcHeader;

#define GC_HDR_BYTES   8u
#define GC_HDR_WIDE    16u

#define GC_UNIT        16u

#define GC_TYPE_SHIFT  0u
#define GCW_TYPE(w)    ((w) >> GC_TYPE_SHIFT)

#define GC_I31_TAG 1u
#define GC_HOST_TAG 2u
#define GC_IS_I31(p) (((u64)(p) & 1u) != 0)
#define GC_IS_HOST(p) (((u64)(p) & 3u) == GC_HOST_TAG)

#define GC_IS_OBJ(p) ((p) != 0 && ((u64)(p) & 3u) == 0)

#define GC_COLOUR_SHIFT 56
#define GC_C_M0        (1ull << (GC_COLOUR_SHIFT + 0))
#define GC_C_M1        (1ull << (GC_COLOUR_SHIFT + 1))
#define GC_C_R         (1ull << (GC_COLOUR_SHIFT + 2))
#define GC_COLOUR_MASK (GC_C_M0 | GC_C_M1 | GC_C_R)
#define GC_ADDR(p)     ((u64)(p) & ~GC_COLOUR_MASK)

/* A reference as a dereferenceable address.  AArch64's top-byte-ignore makes
   a colour-tagged reference dereferenceable as it is; x64 has no TBI, so the
   colour must be masked off at every deref (compiled code does the same).
   The value that flows onward -- into slots, tables, other frames -- keeps
   its colour; only the address a deref uses is canonicalized. */
#if defined(__x86_64__)
#define GC_CANON(p) GC_ADDR(p)
#else
#define GC_CANON(p) ((u64)(p))
#endif

enum {
  RTK_ANY = 0, RTK_EQ, RTK_I31, RTK_STRUCT, RTK_ARRAY,
  RTK_NONE, RTK_FUNC, RTK_NOFUNC, RTK_EXTERN, RTK_NOEXTERN,
  RTK_EXN, RTK_NOEXN,

  RTK_CONCRETE_ANY, RTK_CONCRETE_FUNC
};

#define RTK_NULLABLE 0x80u

u32 warmc_ref_test(WarmInstance *vm, u64 v, u32 kind, u64 id, u32 depth);

void *warmc_struct_new(WarmInstance *vm, u32 type_index);
void *warmc_array_new(WarmInstance *vm, u32 type_index, u64 len);
void *warmc_struct_new_w(WarmInstance *vm, u32 type_index);
void *warmc_array_new_w(WarmInstance *vm, u32 type_index, u64 len);

void *warmc_array_new_fill(WarmInstance *vm, u32 type_index, u64 len,
                           u64 lo, u64 hi);
void *warmc_array_new_fill_w(WarmInstance *vm, u32 type_index, u64 len,
                             u64 lo, u64 hi);
void warmc_array_fill(WarmInstance *vm, u32 type_index, void *a, u32 idx,
                      u32 n, u64 lo, u64 hi);
void warmc_array_copy(WarmInstance *vm, u32 type_index, void *d, u32 di,
                      void *s, u32 si, u32 n);
void *warmc_array_new_data(WarmInstance *vm, u32 type_index, u32 seg,
                           u32 so, u32 n);
void *warmc_array_new_elem(WarmInstance *vm, u32 type_index, u32 seg,
                           u32 so, u32 n);
void *warmc_array_new_data_w(WarmInstance *vm, u32 type_index, u32 seg,
                             u32 so, u32 n);
void *warmc_array_new_elem_w(WarmInstance *vm, u32 type_index, u32 seg,
                             u32 so, u32 n);
void warmc_array_init_data(WarmInstance *vm, u32 type_index, u32 seg, void *a,
                           u32 di, u32 so, u32 n);
void warmc_array_init_elem(WarmInstance *vm, u32 type_index, u32 seg, void *a,
                           u32 di, u32 so, u32 n);

void warmc_gc_hold(u64 *base, const u32 *n, void *slot);
void warmc_gc_release(void *slot);
unsigned warmc_gc_hold_bytes(void);

void warmc_gc_collect(void);

static inline u32 uw_uleb(const u8 **p) {
  u32 v = 0, s = 0, b;
  do { b = *(*p)++; v |= (b & 0x7Fu) << s; s += 7; } while (b & 0x80u);
  return v;
}

typedef struct { u32 kind, tag, pad, pad_; } UwClause;

#define UW_HAS_LR      1u
#define UW_GPR_SHIFT   8
#define UW_FPR_SHIFT  20

#define GCROOT_IS_SLOT(v) (((v) & 1u) != 0)
#define GCROOT_VAL(v)     ((v) >> 1)

typedef struct {
  const void *code;
  u32 code_len;
  u32 frame;
  u32 saved_bytes;
  u32 exn_slot;
  u32 fpsave_off;
  u32 saved;
  u32 nsites, ngcsites;

  const u8 *sites;
  const UwClause *clauses;
  const u8 *gcsites;
} FuncUnwind;

typedef struct {
  void *(*array_new)(WarmInstance *vm, u32 type_index, u64 len);
  void *(*array_new_fill)(WarmInstance *vm, u32 type_index, u64 len,
                          u64 lo, u64 hi);
  void *(*struct_new)(WarmInstance *vm, u32 type_index);
} RtAlloc;

typedef struct {
  u32 nmemories, mem_pad[3];
  u32 ntables, table_pad[3];
  u32 nglobals, ndata, nelem, nexport;
  const GlobalInitDesc *globals;
  const DataSegDesc *data;
  const u8 *data_blob;
  const ElemSegDesc *elem;

  const u32 *elem_entries;
  const ExportDesc *exports;

  u32 has_start, start_func;
  u32 needs_guard;

  u32 nfuncrefs;
  const FuncRefTemplate *funcrefs;

  u32 nimports;
  u32 nimported_funcs;
  const ImportDesc *imports;

  u32 det_profile, nconsts;
  const TableDesc *tables;

  const ConstOp *consts;
  const MemoryDesc *memories;

  u32 ntags, nunwind;
  const TagDesc *tags;
  const FuncUnwind *unwind;

  u32 ngctypes, const_depth;
  const GcTypeInfo *gctypes;

  const GcTypeInfo *exntypes;

  const RtAlloc *alloc;

  void (*report)(WarmInstance *inst);

  const u8 *gcroots;
} ModuleDesc;

#define WARMC_ASSERT_OFF(type, field, n) \
  _Static_assert(__builtin_offsetof(type, field) == (n), \
                 #type "." #field " moved; update test/check_macho.py "\
                 "and test/check_elf.py")
WARMC_ASSERT_OFF(ModuleDesc, nexport, 44);
WARMC_ASSERT_OFF(ModuleDesc, exports, 88);
WARMC_ASSERT_OFF(ModuleDesc, nfuncrefs, 108);
WARMC_ASSERT_OFF(ModuleDesc, funcrefs, 112);
WARMC_ASSERT_OFF(ModuleDesc, nimported_funcs, 124);
WARMC_ASSERT_OFF(ExportDesc, name, 0);
WARMC_ASSERT_OFF(ExportDesc, name_len, 8);
WARMC_ASSERT_OFF(ExportDesc, kind, 12);
WARMC_ASSERT_OFF(ExportDesc, index, 16);
WARMC_ASSERT_OFF(ExportDesc, nparams, 20);
WARMC_ASSERT_OFF(ExportDesc, nresults, 24);
WARMC_ASSERT_OFF(ExportDesc, ptypes, 32);
WARMC_ASSERT_OFF(ExportDesc, rtypes, 40);
_Static_assert(sizeof(ExportDesc) == 80,
               "ExportDesc size changed; update test/check_macho.py "
               "and test/check_elf.py");
_Static_assert(sizeof(FuncRefTemplate) == 24, "FuncRefTemplate size changed; update test/check_macho.py");

WARMC_ASSERT_OFF(FuncUnwind, code_len, 8);
WARMC_ASSERT_OFF(FuncUnwind, saved, 28);
WARMC_ASSERT_OFF(FuncUnwind, nsites, 32);
WARMC_ASSERT_OFF(FuncUnwind, ngcsites, 36);
WARMC_ASSERT_OFF(FuncUnwind, sites, 40);
WARMC_ASSERT_OFF(FuncUnwind, clauses, 48);
WARMC_ASSERT_OFF(FuncUnwind, gcsites, 56);
_Static_assert(sizeof(FuncUnwind) == 64,
               "FuncUnwind size changed; update src/module_data.c");

WARMC_ASSERT_OFF(GcTypeInfo, rtt, 0);
WARMC_ASSERT_OFF(GcTypeInfo, refoffs, 4);
WARMC_ASSERT_OFF(GcTypeInfo, fields, 8);
WARMC_ASSERT_OFF(GcTypeInfo, body_size, 12);
WARMC_ASSERT_OFF(GcTypeInfo, nrefs, 16);
WARMC_ASSERT_OFF(GcTypeInfo, nfields, 20);
WARMC_ASSERT_OFF(GcTypeInfo, kind, 24);
WARMC_ASSERT_OFF(GcTypeInfo, elem_is_ref, 25);
WARMC_ASSERT_OFF(GcTypeInfo, hdr_size, 26);
_Static_assert(sizeof(GcTypeInfo) == 28, "GcTypeInfo size changed; update src/module_data.c");

extern const ModuleDesc warmc_module;

extern const ModuleDesc *const *warmc_modules;
extern const char *const *warmc_module_names;
extern const unsigned warmc_nmodules;

typedef struct {

  unsigned inst;
  const char *name;

  const char *host;
} CompBind;

typedef struct {
  const ModuleDesc *md;

  const CompBind *binds;
} CompInstDesc;

typedef struct {
  const char *name;
  unsigned inst;
  const char *export_name;
} CompExportDesc;

typedef struct {
  unsigned ninsts, nexports;
  const CompInstDesc *insts;
  const CompExportDesc *exports;
} ComponentDesc;

extern const ComponentDesc *const warmc_component;

typedef struct {
  WarmInstance *inst;
  const char *name;

  void *code;
} WarmBinding;
WarmInstance *warmc_instantiate_bound(const ModuleDesc *md, const WarmBinding *binds);

extern void *(*warmc_host_lookup)(const char *name);

void warmc_component_instantiate(void);
WarmInstance *warmc_component_export(const char *name, const ExportDesc **out);

u32 warmc_res_new(u32 rt, u64 rep);
u64 warmc_res_rep(u32 rt, u32 h);
u64 warmc_res_drop(u32 rt, u32 h, u32 *owned);
u32 warmc_res_lower_own(u32 rt, u64 rep);
u32 warmc_res_lower_borrow(u32 rt, u64 rep);
u64 warmc_res_lift_own(u32 rt, u32 h);
u64 warmc_res_lift_borrow(u32 rt, u32 h);
void warmc_res_release(u32 rt, u32 h);

#define INSTANCE_GLOBALS_OFF 96

#define INSTANCE_GC_BARRIER_OFF 64

#define INSTANCE_GLOBAL_SIZE 16
#define INSTANCE_IMPORT_OFF(nglobals) \
  (INSTANCE_GLOBALS_OFF + INSTANCE_GLOBAL_SIZE * (nglobals))

#define INSTANCE_TABLE_OFF(nglobals, nimpfuncs) \
  (INSTANCE_IMPORT_OFF(nglobals) + 16u * (nimpfuncs))
#define INSTANCE_MEM_OFF(nglobals, nimpfuncs, ntables) \
  (INSTANCE_TABLE_OFF(nglobals, nimpfuncs) + 16u * (ntables))

#define INSTANCE_FUNCREF_OFF(nglobals, nimpfuncs, ntables, nmemories) \
  (INSTANCE_MEM_OFF(nglobals, nimpfuncs, ntables) + 16u * (nmemories))

WarmInstance *warmc_instantiate(const ModuleDesc *md);
void warmc_register(const char *name, WarmInstance *inst);
const ExportDesc *warmc_find_export(WarmInstance *inst, const char *name);

void warmc_register_n(const char *name, u32 len, WarmInstance *inst);
const ExportDesc *warmc_find_export_n(WarmInstance *inst, const char *name,
                                      u32 len);

FuncRef warmc_export_func(WarmInstance *inst, const ExportDesc *e);
u64 *warmc_global_cell(WarmInstance *inst, u32 index);

WarmInstance *warmc_instantiate_catching(const ModuleDesc *md, u32 *err);
u32 warmc_invoke_catching(void *fn, WarmInstance *vm, const u64 *iargs,
                          const u64 *fargs, u64 *retbuf);
const char *warmc_trap_name(u32 code);
long warmc_rt_write(int fd, const void *buf, usz n);
long warmc_rt_read(int fd, void *buf, usz n);

u64 warmc_rt_nanos(int realtime);

int warmc_rt_random(void *buf, usz n);
char **warmc_rt_environ(void);
__attribute__((noreturn)) void warmc_rt_exit(int code);

void warmc_report_run(WarmInstance *inst);

extern void (*warmc_gcth_tests)(void);
extern void (*warmc_report_extra)(WarmInstance *inst);
void warmc_rt_report(WarmInstance *inst);

u32 warmc_bind_import(WarmInstance *v, const char *module, const char *name,
                      void *code, WarmInstance *self);

enum {
  TRAP_UNREACHABLE = 1,
  TRAP_MEM_OOB = 2,
  TRAP_DIV_ZERO = 3,
  TRAP_DIV_OVERFLOW = 4,
  TRAP_INDIRECT_OOB = 5,
  TRAP_INDIRECT_NULL = 6,
  TRAP_INDIRECT_TYPE = 7,
  TRAP_BAD_CONVERSION = 8,
  TRAP_UNINIT_ELEM = 9,
  TRAP_TABLE_OOB = 10,
  TRAP_STACK_OVERFLOW = 11,
  TRAP_NULL_REF = 12,

  TRAP_UNCAUGHT_EXN = 13,

  TRAP_HEAP_OOM = 14,

  TRAP_ARRAY_OOB = 15,

  TRAP_CAST = 16,

  TRAP_LINK = 100
};

typedef struct {

  u32 w;
  u32 hdr_len;
  u64 hdr_pad;
  const void *tag;
  u64 nvals;
  u64 vals[];
} WarmExn;

#define EXN_VALS_OFF 32u
_Static_assert(__builtin_offsetof(WarmExn, vals) == EXN_VALS_OFF,
               "the exception payload moved; update EXN_VALS_OFF in src/warmc.h");

typedef struct { u64 gpr[10]; u64 fpr[8]; } WarmRegs;

void warmc_throw(WarmInstance *vm, u32 tag, const u64 *payload, u32 nvals);
void warmc_throw_ref(WarmExn *exn);

__attribute__((noreturn))
void warmc_resume(u64 sp, u64 pad, WarmExn *exn, u64 slot,
                  const u64 *gpr, const u64 *fpr);

enum { VT_I32 = 0x7f, VT_I64 = 0x7e, VT_F32 = 0x7d, VT_F64 = 0x7c,
       VT_V128 = 0x7b, VT_REF = 0x70 };

#define WARMC_INVOKE_OUTGOING 512

#define WARMC_FARGS_WORDS  16

#define WARMC_RET_MAX_RESULTS 16

#define WARMC_RET_X0       0
#define WARMC_RET_V0       2
#define WARMC_RET_SCRATCH  4

#define WARMC_RET_MULTI(i) (WARMC_RET_SCRATCH + 2u * (i))
#define WARMC_RETBUF_WORDS (WARMC_RET_SCRATCH + 2u * WARMC_RET_MAX_RESULTS)

void warmc_invoke(void *fn, WarmInstance *vm, const u64 *iargs,
                  const u64 *fargs, u64 *retbuf);

#if defined(__x86_64__)
/* x64 carries only 3 ints and 4 floats in argument registers; a callee
   with more reads the rest from its incoming stack slots, which
   warmc_invoke cannot populate.  These variants also copy a caller-built
   image of those slots: 8 bytes per scalar parameter past its class's
   register ceiling and 16 per v128, in parameter order. */
void warmc_invoke_s(void *fn, WarmInstance *vm, const u64 *iargs,
                    const u64 *fargs, u64 *retbuf,
                    const u64 *stk, u64 stk_bytes);
u32 warmc_invoke_catching_s(void *fn, WarmInstance *vm, const u64 *iargs,
                            const u64 *fargs, u64 *retbuf,
                            const u64 *stk, u64 stk_bytes);
#endif

void warmc_trap(u32 code);
void warmc_unreached(void);

#endif
