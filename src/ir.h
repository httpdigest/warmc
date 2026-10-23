#ifndef WARMC_IR_H
#define WARMC_IR_H

#include "warmc.h"

typedef uint32_t Ref;
#define REF_NONE 0xffffffffu

typedef enum {
  I_PARAM,
  I_CONST,

  I_ADD, I_SUB, I_MUL, I_DIVS, I_DIVU, I_REMS, I_REMU,

  I_MULHU, I_MULHS,
  I_AND, I_OR, I_XOR, I_SHL, I_SHRS, I_SHRU, I_ROTL, I_ROTR,
  I_CLZ, I_CTZ, I_POPCNT,

  I_BSWAP,

  I_EQ, I_NE, I_LTS, I_LTU, I_GTS, I_GTU, I_LES, I_LEU, I_GES, I_GEU, I_EQZ,

  I_FADD, I_FSUB, I_FMUL, I_FDIV, I_FMIN, I_FMAX, I_FCOPYSIGN,
  I_FABS, I_FNEG, I_FSQRT, I_FCEIL, I_FFLOOR, I_FTRUNC, I_FNEAREST,
  I_FEQ, I_FNE, I_FLT, I_FGT, I_FLE, I_FGE,

  I_WRAP, I_EXTENDS, I_EXTENDU,
  I_TRUNCS, I_TRUNCU, I_TRUNC_SATS, I_TRUNC_SATU,
  I_CONVERTS, I_CONVERTU, I_DEMOTE, I_PROMOTE, I_REINTERPRET,
  I_EXT8S, I_EXT16S, I_EXT32S,

  I_LOAD, I_STORE,
  I_MEM_SIZE, I_MEM_GROW, I_MEM_COPY, I_MEM_FILL, I_MEM_INIT, I_DATA_DROP,

  I_GLOBAL_GET, I_GLOBAL_SET,

  I_SELECT,
  I_CALL,
  I_CALL_INDIRECT,

  I_CALL_REF,

  I_RETURN_CALL,
  I_RETURN_CALL_INDIRECT,
  I_RETURN_CALL_REF,
  I_RESULT,

  I_TRAP,
  I_TRAP_IF,

  I_TABLE_GET, I_TABLE_SET, I_TABLE_SIZE, I_TABLE_GROW,
  I_TABLE_FILL, I_TABLE_COPY, I_TABLE_INIT, I_ELEM_DROP,
  I_REF_NULL, I_REF_IS_NULL, I_REF_FUNC,

  I_MEM_ADDR,

  I_BOUNDS_CHECK,

  I_MEM_PTR,

  I_MEM_BASE,

  I_VLOAD,
  I_VSTORE,
  I_VADD, I_VSUB, I_VMUL, I_VAND, I_VOR, I_VXOR,
  I_VSMAX, I_VSMIN, I_VUMAX, I_VUMIN,

  I_VUZP1, I_VUZP2, I_VZIP1, I_VZIP2,

  I_VCMP,
  I_VSEL,
  I_VSHL, I_VSHRU, I_VSHRS,
  I_VFADD, I_VFSUB, I_VFMUL, I_VFDIV, I_VFMIN, I_VFMAX,
  I_VFNEG, I_VFABS, I_VFSQRT,
  I_VSPLAT,
  I_VREDUCE,

  I_VNOT, I_VANDNOT,
  I_VNEG, I_VABS, I_VPOPCNT,
  /* the bytes of each lane (imm2) reversed: a lane's bswap, which only the
     vectoriser makes -- wasm has no instruction for it */
  I_VREV,
  I_VADDSATS, I_VADDSATU, I_VSUBSATS, I_VSUBSATU,
  I_VAVGRU,
  I_VQ15MULR,
  I_VDOT,

  I_VEXTADD, I_VEXTMUL, I_VEXTEND, I_VNARROW,

  I_VSHLV, I_VSHRUV, I_VSHRSV,
  I_VANYTRUE, I_VALLTRUE, I_VBITMASK,
  I_VEXTRACTS, I_VEXTRACTU,
  I_VREPLACE,
  I_VSHUFFLE,
  I_VSWIZZLE,
  I_VFPMIN, I_VFPMAX,
  I_VFCEIL, I_VFFLOOR, I_VFTRUNC, I_VFNEAREST,

  I_VTRUNCSAT, I_VCONVERT,
  I_VDEMOTE, I_VPROMOTE,

  I_VFMADD,

  I_VDOTI7, I_VDOTI7ADD,

  I_THROW, I_THROW_REF,

  I_EXN_OBJ, I_EXN_PAYLOAD,

  I_MEM_LIMIT,

  I_STRUCT_NEW,
  I_FIELD_GET,
  I_FIELD_SET,

  I_ARRAY_NEW,

  I_ARRAY_NEW_SEG,

  I_ARRAY_OP,

  I_ARRAY_GET,
  I_ARRAY_SET,
  I_ARRAY_LEN,

  I_GC_LAB_OPEN,
  I_GC_LAB_FLUSH,

  I_GC_BADMASK,

  I_REF_TEST,

  I_REF_CONVERT,
  I_REF_I31,
  I_I31_GET,

  I_NOP,
  I_OP_COUNT
} IrOp;

#define GCF_SIZE(x)  ((x) & 7u)
#define GCF_SIGNED   8u
#define GCF_NULLCHK  16u

#define GCF_INIT     32u

#define GCF_NOBOUND  512u

#define GCF_NOBAR    64u

#define GCF_MASKARG  128u

#define GCA_CACHED   (1u << 31)

#define ARRO_FILL      0u
#define ARRO_COPY      1u
#define ARRO_INIT_DATA 2u
#define ARRO_INIT_ELEM 3u
#define ARRO_KIND(x)   ((uint32_t)((x) & 3u))
#define ARRO_SEG(x)    ((uint32_t)((x) >> 2))
#define ARRO_MK(k, seg) ((uint64_t)(k) | ((uint64_t)(seg) << 2))

/* A ref.test's imm: the runtime's kind and the target's subtyping depth,
   which are what the runtime is asked, and the heap type tested for, which is
   what the optimiser compares one test's proof with another's. */
#define RTEST_KIND(x)  ((uint32_t)((x) & 0xffu))
#define RTEST_DEPTH(x) ((uint32_t)(((x) >> 8) & 0xffffffu))
#define RTEST_HEAP(x)  ((HeapType)(int32_t)(uint32_t)((x) >> 32))
#define RTEST_MK(kind, depth, ht)                                    \
  ((uint64_t)(kind) | ((uint64_t)(depth) << 8) |                     \
   ((uint64_t)(uint32_t)(ht) << 32))

#define VLANE_I8   0u
#define VLANE_I16  1u
#define VLANE_I32  2u
#define VLANE_I64  3u
#define VLANE_F32  4u
#define VLANE_F64  5u
#define VLANE_IS_FLOAT(x) ((x) >= VLANE_F32)
#define VLANE_SIZE(x)     ((int)(VLANE_IS_FLOAT(x) ? (x) - VLANE_F32 + 2u : (x)))

#define MEMF_SIZE(x)   ((x) & 7u)
#define MEMF_SIGNED    8u
#define MEMF_NOCHECK   16u
#define MEMF_MEM_SHIFT 32
#define MEMF_MEM(x)    ((uint32_t)((uint64_t)(x) >> MEMF_MEM_SHIFT))
#define MEMF_MK(sz, sg) ((uint64_t)((sz) | ((sg) ? MEMF_SIGNED : 0)))
#define MEMF_MK_AT(sz, sg, mem) \
  (MEMF_MK(sz, sg) | ((uint64_t)(uint32_t)(mem) << MEMF_MEM_SHIFT))

typedef struct {
  uint16_t op;
  uint8_t flags;   /* IRF_* */
  ValType type;
  uint32_t nargs;
  uint32_t args_off;
  uint64_t imm;
  uint64_t imm2;
  uint32_t block;
} Inst;

/* a direct call pass_devirt made, which a later inline round looks at */
#define IRF_DEVIRT 1u

typedef struct {
  uint32_t target;
  Ref *args;
  uint32_t nargs, cap;
  bool eh;
} Edge;

typedef enum { T_NONE, T_JUMP, T_BRIF, T_BRTABLE, T_RETURN, T_TRAP } TermKind;

typedef struct {
  Ref *params; uint32_t nparams, cap_params;
  uint32_t *insts; uint32_t ninsts, cap_insts;

  uint8_t term;
  Ref term_arg;
  uint64_t term_imm;
  Edge *succs; uint32_t nsuccs, cap_succs;
  Ref *ret_args; uint32_t nret, cap_ret;

  uint32_t *preds; uint32_t npreds, cap_preds;

  bool sealed, filled, reachable;

  bool diverges;

  /* Code that runs only on a path the compiler made rare: the copy node
     splitting gives a dispatch, which Go enters only when a goroutine
     resumes.  Its loop depth is the copied loop's, but its uses are not hot. */
  bool cold;

  bool is_pad;
  uint8_t pad_kind;
  uint32_t pad_tag;
  uint32_t rpo;
  uint32_t loop_depth;
  uint32_t idom;

  uint32_t dom_in, dom_out;
} Block;

typedef struct {
  Module *m;
  uint32_t func_index;
  CompType *sig;

  Inst *insts; uint32_t ninsts, cap_insts;
  Ref *argpool; uint32_t nargpool, cap_argpool;

  Block *blocks; uint32_t nblocks, cap_blocks;
  uint32_t entry;

  uint32_t *kb_epoch;
  uint64_t *kb_bits;
  size_t kb_cap;
  uint32_t kb_gen;

  uint32_t *order_sig, *order_sig_new;
  size_t order_sig_len, order_sig_cap;
  bool order_sig_valid;

  uint32_t nlocals;
  ValType *local_types;

  uint32_t npads;

  Ref *curdef;
  uint32_t curdef_stride;
  size_t cap_curdef;

  Ref *subst;

  uint32_t *order; uint32_t norder, cap_order;

  /* The bytes each small loop the arm64 back end aligned turned out to
     take, by header block, from one emission for the next (see fit_pad in
     isel.c); NULL when not measured. */
  uint16_t *loop_bytes;
  /* The register allocation a first emission made, kept for the second when
     only the loops' padding differs between them (isel.c); ra_keep_drop
     frees it if the second does not take it. */
  void *ra_keep;
  void (*ra_keep_drop)(void *);
  /* What ir_compact found before renumbering, for the few code-generation
     decisions that read the whole instruction array rather than the code:
     how many instructions there were, the most arguments any had, and
     whether a dropped one was a safepoint or of reference type.  0 and
     false when the function was not compacted. */
  uint32_t made_insts, made_maxargs;
  bool ghost_safepoint, ghost_ref;

  struct IrChunk *chunks;
  char *bump;
  size_t bump_left;
} IrFunc;

void *ir_alloc(IrFunc *f, size_t n);
void *ir_grow(IrFunc *f, void *p, size_t oldn, size_t newn);

bool ir_dom_range(const IrFunc *f, uint32_t a, uint32_t b);
bool ir_dom_walk(const IrFunc *f, uint32_t a, uint32_t b);
bool ir_dominates_checked(const IrFunc *f, uint32_t a, uint32_t b);

IrFunc *ir_new(Module *m, uint32_t func_index);
void ir_free(IrFunc *f);

uint32_t ir_new_block(IrFunc *f);
Ref ir_add_param(IrFunc *f, uint32_t block, ValType t);
Ref ir_emit(IrFunc *f, uint32_t block, IrOp op, ValType type,
            const Ref *args, uint32_t nargs, uint64_t imm, uint64_t imm2);
Ref ir_const(IrFunc *f, uint32_t block, ValType t, uint64_t bits);

static inline Inst *ir_inst(IrFunc *f, Ref r) { return &f->insts[r]; }
static inline Ref *ir_args(IrFunc *f, Ref r) {
  return &f->argpool[f->insts[r].args_off];
}

Ref ir_resolve(IrFunc *f, Ref r);
void ir_replace(IrFunc *f, Ref from, Ref to);
void ir_apply_subst(IrFunc *f);

void ir_add_edge(IrFunc *f, uint32_t from, uint32_t to, const Ref *args, uint32_t nargs);

void ir_add_eh_edge(IrFunc *f, uint32_t from, uint32_t to);

bool ir_block_has_eh(const IrFunc *f, uint32_t bi);
void ir_recompute_preds(IrFunc *f);

typedef struct { uint32_t *off, *src, *si; } IrEdgeIdx;
IrEdgeIdx ir_edge_index(IrFunc *f);
void ir_edge_index_free(IrEdgeIdx *x);
void ir_order(IrFunc *f);
/* ir_order, saying whether the graph had changed since the last. */
bool ir_order_changed(IrFunc *f);
void ir_compact(IrFunc *f);
void ir_path_share(const IrFunc *f, float *share);

int ir_succ_score(const IrFunc *f, const Block *b, uint32_t si);
const char *ir_op_name(uint16_t op);
void ir_verify(IrFunc *f, const char *stage);
void ir_print(IrFunc *f, const char *stage);

bool ir_is_pure(const Inst *in);

static inline uint64_t mem_guaranteed_bytes(const Module *m, uint32_t k) {
  if (k >= m->nmemories) return 0;
  uint64_t pages = m->memories[k].min;
  if (pages > UINT64_MAX / WASM_PAGE_SIZE) return 0;
  return pages * WASM_PAGE_SIZE;
}
int ir_const_cost(uint64_t bits, bool wide);
int ir_vconst_cost(uint64_t lo, uint64_t hi);
uint32_t ir_vec_copies(void);
bool ir_vec_mul_one(uint32_t lane);

bool ir_imm_ok(IrFunc *f, Ref user, uint32_t idx, uint64_t cval, ValType ct);
bool ir_mem_disp_ok(uint64_t off, uint32_t size_log2);

uint64_t ir_value_max(IrFunc *f, Ref v, int depth);

uint64_t ir_known_bits(IrFunc *f, Ref v, int depth);
bool ir_has_result(const Inst *in);

typedef struct {
  int level;
  bool fold, dce, cse, licm, branch, memopt, bce, inl, strength;
  bool thread, nodesplit;
  bool gssa_all;
  bool small_loop_align;

  bool reassoc;
  bool rotate;
  bool addroff;
  bool memptr;
  bool align;
  bool guard_pages;
  bool regalloc;
  bool split;      /* cut a spilled loop web where the loop meets the rest */
  bool vectorize;
  bool edgetests;  /* tests a dominating branch decided, before vectorize */
  bool selftail;
  bool ldcomb;
  bool escape;
  bool pescape;    /* partial: make an object only where it escapes */
  bool gloop;
  bool loopclose;
  bool cpool;
  bool gssa;

  bool labcache;

  bool barelide;

  bool reach;

  bool prune;

  bool devirt;
  bool reinline;   /* inline again what devirtualization and escape expose */
  bool throwjump;  /* a throw caught in its own function becomes a jump */

  bool unroll;

  bool gcpure;

  bool closed_world;
  bool print_ir;
  bool stats;
  bool profile;

  bool det_profile;
} OptOpts;

bool ir_op_is_gc_safepoint(uint16_t op);

IrFunc *ir_build(Module *m, uint32_t func_index, const OptOpts *o);
void ir_optimize(IrFunc *f, const OptOpts *o);

typedef struct {
  uint8_t *code; uint32_t code_len;
  Reloc *relocs; uint32_t nrelocs;
  bool want_align;
  const char *sym;
  FuncUnwind uw;
  uint64_t insts_emitted;

  uint32_t *refs; uint32_t nrefs;
} FuncOut;

void ir_produce_function(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                         uint32_t out_bytes, FuncOut *out);
void codegen_commit_function(ObjBuilder *obj, FuncOut *out);

void codegen_drop_function(ObjBuilder *obj, FuncOut *out);

void codegen_reset_merge(void);
void codegen_set_merge(bool on);
void codegen_merge_stats(uint32_t *bodies, uint64_t *bytes);
void codegen_dead_stats(uint32_t *bodies, uint64_t *bytes);

void codegen_prewarm_symbols(Module *m, ObjBuilder *o);
void compile_module(Module *m, ObjBuilder *o, const OptOpts *opts);
void compile_set_jobs(long n);
bool pass_inline(IrFunc *f, const OptOpts *o);
/* Frees the calling thread's inliner memo (inline.c). */
void inl_memos_release(void);
bool pass_inline_round2(IrFunc *f, const OptOpts *o);
bool pass_inline_more(IrFunc *f, const OptOpts *o);
bool opt_const_object(IrFunc *f, Ref r);
uint32_t opt_direct_callsite_count(const Module *m, uint32_t fi);
bool opt_call_frame_only(const Module *m, uint32_t fi);
uint32_t opt_table0_target(const Module *m, uint32_t site_ti, uint64_t idx);

typedef struct {
  uint64_t folded, dce_removed, cse_removed, licm_hoisted, branch_simplified;
  uint64_t loads_forwarded, stores_removed, checks_removed, inlined;
  uint64_t params_removed, strength_reduced, moves_coalesced, sunk, rotated;
  uint64_t insts_emitted, spills, reloads, vectorized, self_tail;
  uint64_t escape_removed, escape_forwarded, escape_made;
  uint64_t loads_combined;
  uint64_t globals_promoted, loops_closed, globals_ssa;
  uint64_t reassoc;
  uint64_t cmp_reused;

  uint64_t bar_masks, bar_elided;
  uint64_t caller_saves, csa_restores, spills_v128_xcall, range_budget_trips;
  uint64_t split_cuts, split_invs, pieces;
  uint64_t devirt_ref, devirt_indirect, devirt_null_kept;
  uint64_t const_reads, throw_jumps, inl_more;
  uint64_t inl_flat, inl_spliced, inl_once_forced, inl_retcall_sites;
  uint64_t inl_transitive, inl_escape;
  uint64_t inl_rej_size, inl_rej_budget, inl_rej_tailcall, inl_rej_retcalls;
  uint64_t inl_rej_pads, inl_rej_score, inl_rej_depth, inl_rej_recursion;
  uint64_t unrolled, unr_guarded, unr_rej_shape, unr_rej_body, unr_rej_exit;
  uint64_t unr_rej_trip, unr_rej_escape, unr_addr_folded;
  uint64_t gc_nullchk_removed;
  uint64_t ldpairs, stpairs;

  uint64_t max_frame_bytes;
} OptStats;
extern _Thread_local OptStats g_stats;
void opt_stats_add(OptStats *dst, const OptStats *src);
void opt_init_tunables(void);

void opt_passtime_report(void);

void opt_effects_begin(Module *m, const OptOpts *o);
void opt_effects_scan(Module *m, const OptOpts *o, uint32_t fi);
void opt_effects_end(Module *m, const OptOpts *o);
extern bool opt_effects_workers_running;

typedef struct { uint64_t m; int add; int sh; } Magic;
Magic opt_magic_unsigned(uint64_t d, bool wide);

#endif
