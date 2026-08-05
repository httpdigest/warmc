#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

#define ALIGN_BYTES 64u

#define REG_VMCTX   (s->vmctx_reg)
#define REG_MEMBASE (s->membase_reg)
#define REG_MEMSIZE R21

#define CALL_TID_SHARE 0.02

#define S0 R12
#define S1 R13
#define S2 R14
#define S3 R15
#define TMP0 R16
#define TMP1 R17
#define F0 26
#define F1 27
#define F2 28
#define FTMP 29

#define F3 30
#define F4 31

static const int GPR_ALLOC[] = { R0, R1, R2, R3, R4, R5, R6, R7, R9, R10, R11,
                                 R22, R23, R24, R25, R26, R27, R28 };
static const int FPR_ALLOC[] = { 0, 1, 2, 3, 4, 5, 6, 7,
                                 8, 9, 10, 11, 12, 13, 14, 15,
                                 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };
#define NGPR_ALLOC ((int)(sizeof GPR_ALLOC / sizeof GPR_ALLOC[0]))
#define NFPR_ALLOC ((int)(sizeof FPR_ALLOC / sizeof FPR_ALLOC[0]))

static bool gpr_is_callee_saved(int r) { return r >= R21 && r <= R28; }
static bool fpr_is_callee_saved(int r) { return r >= 8 && r <= 15; }

typedef struct { int src, dst; uint8_t fp, wide, vec; } Move;

#define LOC_NONE  0x7fffffff
#define SLOT(n)   (-((int)(n) + 1))
#define IS_SLOT(l) ((l) < 0)
#define SLOT_OF(l) ((uint32_t)(-(l) - 1))

struct Bfx { bool ok, sign; int immr, imms; Ref src; };

typedef struct BrFix {
  size_t site;
  int is19;
  uint32_t target;
  struct BrFix *next;
} BrFix;

typedef struct TabFix {
  size_t word;
  size_t base;
  size_t stub;
  struct TabFix *next;
} TabFix;

/* The live-range split plan: written by the first allocation, consumed by
   the retry.  veto flags params whose ENTRY-edge coalescing the retry
   skips, cutting a spilled web at its loop boundary so the in-loop piece
   competes for a register on its own; addv/addhdr name values to thread
   through a NEW header param first (the loop-invariant shape).  The plan
   only ever asks -- the retry's allocation stays the sole authority. */
typedef struct SplitCtl {
  uint8_t *veto;
  uint32_t veto_n;
  Ref *addv;
  uint32_t *addhdr;
  Ref *addp;
  uint32_t nadd, cap_add;
  uint32_t nflagged;

  /* The retry-aware half: cutp lists the vetoed params so the retry can
     see a cut fail (its web still in a slot) the same way it sees an inv
     param fail.  The retry marks failures instead of emitting; the driver
     rolls the IR back, drops the failed grants and re-runs, so a failed
     grant never competes -- the second-order shuffle that kept the whole
     feature opt-in.  accept stops the loop: failures are then absorbed by
     the singleton slot-alias fallback. */
  Ref *cutp;
  uint32_t ncut, cap_cut;
  uint8_t *inv_fail;
  uint8_t *cut_fail;
  uint32_t nfailed_new;
  bool accept;
} SplitCtl;

typedef struct {
  IrFunc *f;
  ObjBuilder *o;
  const OptOpts *opt;

  Buf body;
  Reloc *relocs; uint32_t nrelocs, cap_relocs;

  int *loc;
  uint8_t *cls;
  uint32_t nslots;

  uint32_t out_bytes;
  uint32_t slots_off;
  uint32_t retarea_off;
  uint32_t retptr_slot;
  uint32_t fpsave_off;

  uint32_t exn_slot;
  uint32_t throw_area_off, throw_area_bytes;
  uint32_t frame;

  uint32_t *block_off;
  BrFix *fixups;

  uint32_t used_gpr_cs;
  uint32_t used_fpr_cs;

  bool mem_valid;
  bool naive;

  SplitCtl *split_sc;
  bool split_collect;

  bool tail_done;

  bool has_call;
  bool needs_mem;
  bool needs_memsize;
  bool needs_vmctx;
  bool needs_retarea;
  bool has_pads;
  uint32_t saved_bytes;
  uint8_t *mem_valid_in;

  UwSite *sites; uint32_t nsites, cap_sites;
  UwClause *clauses; uint32_t nclauses, cap_clauses;

  uint32_t *sp_first, *sp_n;
  Ref *sp_vals; uint32_t nsp_vals, cap_sp_vals;

  int32_t *csa_slot;
  uint8_t *csa_on;
  uint32_t *csa_first, *csa_n;
  Ref *csa_vals; uint32_t ncsa_vals, cap_csa_vals;
  uint32_t csa_consumed;
  GcSite *gcsites; uint32_t ngcsites, cap_gcsites;
  uint32_t *gcroots; uint32_t ngcroots, cap_gcroots;
  uint32_t cur_block;

  Ref cur_inst;

  bool at_eh_site;
  uint8_t *pinned;

  uint64_t *lv_in, *lv_out;

  bool no_imm;
  uint8_t *imm_fold;
  uint8_t *fused;
  uint8_t *addr_fuse;
  uint8_t *ptr_fuse;
  uint8_t *zx_fuse;
  uint8_t *no_remat;
  uint8_t *paired;
  bool no_ldpair;
  /* one-entry cache: TMP1 holds mem_base(t1_mem) + zx(t1_aa), set by the
     fused-address emissions, cleared at every other instruction */
  Ref t1_aa;
  uint32_t t1_mem;
  bool t1_ok, t1_was;
  uint8_t *folded;
  struct Bfx *bfx;
  uint32_t *uses;

  uint32_t prof_skip;
  size_t *prof_site;
  size_t *prof_start;

  struct TrapFix { size_t site; bool is19; int code; struct TrapFix *next; } *traps;

  struct { uint64_t word; size_t at; } *pool;
  uint32_t npool, cap_pool;
  struct PoolFix { size_t site; uint32_t idx; struct PoolFix *next; } *poolfix;
  bool pool_ok;
  bool remat_fp, remat_v128, remat_glob;
  int remat_cost;
  double remat2_w, remat2_seg, remat2_wcap;
  bool far_br, far_br_needed;

  struct EdgeStub {
    size_t site;
    Move *mv; int n;
    uint32_t target;
    struct EdgeStub *next;
  } *edge_stubs;
  uint8_t trap_used[TRAP_CODE_COUNT];

  int lab_reg;

  int vmctx_reg, membase_reg;

  int fl_kind;
  Ref fl_a, fl_b;
  uint64_t fl_imm;
  int fl_sf;
  uint32_t fl_epoch;

  Ref cmp_ref;
  int cmp_cc;
  size_t cmp_len;

  int rc_reg[2];
  uint32_t rc_slot[2];
  uint8_t rc_used[2];
  uint8_t rc_next;

  int tid_reg;
  uint64_t tid_val;

  struct AllocFix {
    size_t site;
    bool cached;
    size_t back;
    const Ref *args; uint32_t nargs;
    const char *helper; uint32_t type_idx;
    Ref inst;
    struct AllocFix *next;
  } *allocs;

  struct BarFix {
    size_t site;
    size_t back;
    int dst;
    int base;
    int off_reg;
    uint32_t off;
    struct BarFix *next;
  } *bars;
} Isel;

#define B (&s->body)

static void isel_reloc(Isel *s, RelKind kind, uint64_t off, uint32_t sym, int64_t addend) {
  if (s->nrelocs == s->cap_relocs) {
    s->cap_relocs = s->cap_relocs ? s->cap_relocs * 2 : 16;
    s->relocs = xrealloc(s->relocs, s->cap_relocs * sizeof(Reloc));
  }
  Reloc *r = &s->relocs[s->nrelocs++];
  r->kind = kind; r->section = 0; r->offset = off; r->sym = sym; r->addend = addend;
}

static void add_fix(Isel *s, size_t site, int is19, uint32_t target) {
  BrFix *fx = xmalloc(sizeof *fx);
  fx->site = site; fx->is19 = is19; fx->target = target; fx->next = s->fixups;
  s->fixups = fx;
}

static uint32_t slot_off(Isel *s, uint32_t slot) { return s->slots_off + 8u * slot; }

static void slot_load(Buf *bf, int fp, int vec, int reg, uint32_t off) {
  if (vec) a64_ldr_q(bf, reg, RSP, off);
  else if (fp) a64_fldr_imm(bf, 3, reg, RSP, off);
  else a64_ldr_imm(bf, 3, reg, RSP, off);
}

static void slot_store(Buf *bf, int fp, int vec, int reg, uint32_t off) {
  if (vec) a64_str_q(bf, reg, RSP, off);
  else if (fp) a64_fstr_imm(bf, 3, reg, RSP, off);
  else a64_str_imm(bf, 3, reg, RSP, off);
}

static bool is_vec_value(IrFunc *f, Ref v) { return (ValType)f->insts[v].type == VT_V128; }
static bool is_fp_value(IrFunc *f, Ref v) {
  ValType t = (ValType)f->insts[v].type;
  return vt_is_float(t) || t == VT_V128;
}
static bool is64(IrFunc *f, Ref v) { return vt_is_64((ValType)f->insts[v].type); }

#define A64_IDX_UXTW 2
#define A64_IDX_LSL  3
static int idx_option(IrFunc *f, Ref addr) {
  return is64(f, addr) ? A64_IDX_LSL : A64_IDX_UXTW;
}

static Ref addr_index(Isel *s, IrFunc *f, Ref addr) {
  return (s->zx_fuse && s->zx_fuse[addr]) ? ir_args(f, addr)[0] : addr;
}

static bool fits_gpr(IrFunc *f, Ref v) {
  ValType t = (ValType)f->insts[v].type;
  return !vt_is_float(t) && t != VT_V128;
}

static bool is_imm(Isel *s, Ref v) { return s->imm_fold && s->imm_fold[v]; }
static uint64_t imm_of(Isel *s, Ref v) { return s->f->insts[v].imm; }

static bool is_imm_at(Isel *s, Ref user, uint32_t idx, Ref v) {
  if (s->imm_fold && s->imm_fold[v]) return true;
  if (s->no_imm) return false;
  Inst *in = &s->f->insts[v];
  if (in->op != I_CONST || !fits_gpr(s->f, v)) return false;
  return ir_imm_ok(s->f, user, idx, in->imm, (ValType)in->type);
}

static void load_vconst(Isel *s, int fr, uint64_t lo, uint64_t hi);

static int mem_base_reg_fwd(Isel *s, uint64_t k, int scratch);
static void pool_load(Isel *s, int ft, int fr, uint64_t bits);
static int vmctx_at(Isel *s, uint32_t off, int size, uint32_t *disp,
                    int scratch);

static int rd(Isel *s, Ref v, int scratch) {
  int l = s->loc[v];
  if (l == LOC_NONE) {
    Inst *in = &s->f->insts[v];

    if (in->op == I_MEM_BASE) {
      int mb = mem_base_reg_fwd(s, in->imm2, TMP0);
      a64_add_imm(B, SF64, scratch, mb, (uint32_t)in->imm, 0);
      return scratch;
    }
    if (in->op == I_GLOBAL_GET) {
      int size = (in->type == VT_I32 || in->type == VT_F32) ? 2 : 3;
      int asize = (in->type == VT_V128) ? 4 : size;
      uint32_t off;
      int base = vmctx_at(s, VMCTX_GLOBALS + GLOBAL_SLOT_SIZE * (uint32_t)in->imm,
                          asize, &off, TMP0);
      if ((ValType)in->type == VT_V128) a64_ldr_q(B, scratch, base, off);
      else if (vt_is_float((ValType)in->type)) a64_fldr_imm(B, size, scratch, base, off);
      else a64_ldr_imm(B, size, scratch, base, off);
      return scratch;
    }
    if (in->op != I_CONST)
      warmc_fatal("internal: value v%u has no location", v);
    if ((ValType)in->type == VT_V128) {
      load_vconst(s, scratch, in->imm, in->imm2);
    } else if (vt_is_float((ValType)in->type)) {
      int ft = is64(s->f, v) ? FT_D : FT_S;
      uint32_t imm8;
      if (a64_fmov_imm(in->imm, ft, &imm8)) a64_fmov_imm_emit(B, ft, scratch, imm8);
      else if (s->pool_ok) pool_load(s, ft, scratch, in->imm);
      else {
        a64_load_imm(B, is64(s->f, v) ? SF64 : SF32, TMP0, in->imm);
        a64_fmov_from_gpr(B, is64(s->f, v) ? SF64 : SF32, ft, scratch, TMP0);
      }
    } else {
      a64_load_imm(B, is64(s->f, v) ? SF64 : SF32, scratch, in->imm);
    }
    return scratch;
  }
  if (!IS_SLOT(l)) return l;
  uint32_t sl = SLOT_OF(l);
  if (s->rc_reg[0] && !is_fp_value(s->f, v) && !is_vec_value(s->f, v)) {
    for (int i = 0; i < 2; i++)
      if (s->rc_reg[i] && s->rc_slot[i] == sl + 1) {
        s->rc_used[i] = 1;
        return s->rc_reg[i];
      }

    int pick = -1;
    for (int i = 0; i < 2 && pick < 0; i++)
      if (s->rc_reg[i] && !s->rc_used[i] && !s->rc_slot[i]) pick = i;
    for (int k = 0; k < 2 && pick < 0; k++) {
      int i = (s->rc_next + k) & 1;
      if (s->rc_reg[i] && !s->rc_used[i]) pick = i;
    }
    if (pick >= 0) {
      slot_load(B, 0, 0, s->rc_reg[pick], slot_off(s, sl));
      g_stats.reloads++;
      s->rc_slot[pick] = sl + 1;
      s->rc_used[pick] = 1;
      s->rc_next = (uint8_t)((pick + 1) & 1);
      return s->rc_reg[pick];
    }
  }
  slot_load(B, is_fp_value(s->f, v), is_vec_value(s->f, v), scratch,
            slot_off(s, SLOT_OF(l)));
  g_stats.reloads++;
  return scratch;
}

static void rc_clear(Isel *s) { s->rc_slot[0] = 0; s->rc_slot[1] = 0; }

static void rc_kill_reg(Isel *s, int reg) {
  for (int i = 0; i < 2; i++) if (s->rc_reg[i] == reg) s->rc_slot[i] = 0;
}

static void rc_kill_slot(Isel *s, uint32_t slot) {
  for (int i = 0; i < 2; i++) if (s->rc_slot[i] == slot + 1) s->rc_slot[i] = 0;
}

static void rc_clear(Isel *s);
static void rc_kill_slot(Isel *s, uint32_t slot);

static bool op_clobbers_caller_saved(uint16_t op);

static int wr(Isel *s, Ref v, int scratch) {
  int l = s->loc[v];
  return IS_SLOT(l) ? scratch : l;
}

static void commit(Isel *s, Ref v, int reg) {
  int l = s->loc[v];
  if (IS_SLOT(l)) rc_kill_slot(s, SLOT_OF(l));
  if (l == LOC_NONE)
    warmc_fatal("internal: committing v%u which has no location", v);
  if (!IS_SLOT(l)) {
    if (l != reg) {
      if (is_vec_value(s->f, v)) a64_vmov(B, l, reg);
      else if (is_fp_value(s->f, v)) a64_fmov_reg(B, is64(s->f, v) ? FT_D : FT_S, l, reg);
      else a64_mov_reg(B, SF64, l, reg);
    }
    return;
  }
  slot_store(B, is_fp_value(s->f, v), is_vec_value(s->f, v), reg,
             slot_off(s, SLOT_OF(l)));
}

static void reload_size(Isel *s) {
  if (s->mem_valid || !s->needs_memsize) return;
  a64_ldr_imm(B, 3, REG_MEMSIZE, REG_VMCTX, VMCTX_MEM_SIZE);
  s->mem_valid = true;
}

/* A value the allocator marked caller-saved-around-call keeps its register
   everywhere except across the clobber sites it is live over: its register is
   stored to a private slot before the site's call and reloaded after.  The
   save must precede argument marshalling (which writes the argument
   registers) and the restore must follow the result commit. */
static void csa_save(Isel *s, Ref site) {
  if (!s->csa_n || !s->csa_n[site]) return;
  for (uint32_t k = 0; k < s->csa_n[site]; k++) {
    Ref v = s->csa_vals[s->csa_first[site] + k];
    slot_store(B, is_fp_value(s->f, v), is_vec_value(s->f, v), s->loc[v],
               slot_off(s, (uint32_t)s->csa_slot[v]));
  }
  s->csa_consumed += s->csa_n[site];
}

static void csa_restore(Isel *s, Ref site) {
  if (!s->csa_n || !s->csa_n[site]) return;
  for (uint32_t k = 0; k < s->csa_n[site]; k++) {
    Ref v = s->csa_vals[s->csa_first[site] + k];
    slot_load(B, is_fp_value(s->f, v), is_vec_value(s->f, v), s->loc[v],
              slot_off(s, (uint32_t)s->csa_slot[v]));
    g_stats.csa_restores++;
  }
}

static void emit_trap(Isel *s, int code) {
  a64_load_imm(B, SF32, R0, (uint64_t)code);
  uint32_t sym = obj_intern_sym(s->o, "warmc_trap", SYM_UNDEF_FUNC, -1, 0);
  isel_reloc(s, REL_CALL26, B->len, sym, 0);
  a64_bl(B, 0);
  a64_brk(B, 0);
}

static size_t br_cc(Isel *s, int kind, int cc, int zsf, int zreg) {
  if (!s->far_br) {
    size_t at = B->len;
    if (kind == 0) a64_bcond(B, cc, 0);
    else if (kind == 1) a64_cbz(B, zsf, zreg, 0);
    else a64_cbnz(B, zsf, zreg, 0);
    return at;
  }

  if (kind == 0) a64_bcond(B, cc_invert(cc), 2);
  else if (kind == 1) a64_cbnz(B, zsf, zreg, 2);
  else a64_cbz(B, zsf, zreg, 2);
  size_t at = B->len;
  a64_b(B, 0);
  return at;
}

static void patch_b26_checked(Isel *s, size_t site, size_t target) {
  if (a64_patch_b26_ok(B, site, target)) return;
  warmc_fatal("function %u compiles to %zu bytes of code, past the range a "
              "branch can reach", s->f->func_index, B->len);
}

static void br_patch(Isel *s, size_t site, size_t target) {
  if (s->far_br) { patch_b26_checked(s, site, target); return; }
  if (!a64_patch_b19_ok(B, site, target)) s->far_br_needed = true;
}

static void trap_defer(Isel *s, size_t site, bool is19, int code) {
  struct TrapFix *t = xmalloc(sizeof *t);
  t->site = site; t->is19 = is19; t->code = code; t->next = s->traps;
  s->traps = t;
  s->trap_used[code] = 1;
}

static uint32_t pool_ref(Isel *s, uint64_t word) {
  for (uint32_t i = 0; i < s->npool; i++)
    if (s->pool[i].word == word) return i;
  if (s->npool == s->cap_pool) {
    s->cap_pool = s->cap_pool ? s->cap_pool * 2 : 8;
    s->pool = xrealloc(s->pool, s->cap_pool * sizeof *s->pool);
  }
  s->pool[s->npool].word = word;
  s->pool[s->npool].at = 0;
  return s->npool++;
}

static void pool_load(Isel *s, int ft, int fr, uint64_t bits) {
  uint32_t idx = pool_ref(s, ft == FT_S ? (uint64_t)(uint32_t)bits : bits);
  size_t site = B->len;
  a64_ldr_lit_fp(B, ft, fr);
  struct PoolFix *p = xmalloc(sizeof *p);
  p->site = site; p->idx = idx; p->next = s->poolfix;
  s->poolfix = p;
}

static bool pool_in_reach(const IrFunc *f) {
  uint64_t bound = (uint64_t)f->ninsts * 128 + (uint64_t)f->nblocks * 128;
  return bound + 65536 < (1u << 20);
}

static void trap_if_cc(Isel *s, int cond, int code) {
  size_t site = br_cc(s, 0, cond, 0, 0);
  trap_defer(s, site, !s->far_br, code);
}

static void trap_if_zero(Isel *s, int sf, int reg, int code) {
  size_t site = br_cc(s, 1, 0, sf, reg);
  trap_defer(s, site, !s->far_br, code);
}

static bool load_needs_barrier(const IrFunc *f, const Inst *in) {
  if (in->imm2 & GCF_NOBAR) return false;
  return warmc_load_barrier && vt_ref_collectable(f->m, (ValType)in->type);
}

static Ref load_barrier_mask(const IrFunc *f, Ref v) {
  const Inst *in = &f->insts[v];
  if (!(in->imm2 & GCF_MASKARG)) return REF_NONE;
  return ir_args((IrFunc *)f, v)[in->nargs - 1];
}

static int emit_gc_load_barrier_pre(Isel *s, int dst, int base) {
  if (dst != base) return base;
  a64_mov_reg(B, SF64, S3, base);
  return S3;
}

static void emit_gc_load_barrier(Isel *s, int dst, int base, int off_reg,
                                 uint32_t off, Ref mask) {
  int m;
  if (mask != REF_NONE) {
    m = rd(s, mask, TMP1);
  } else {
    a64_ldr_imm(B, 3, TMP1, REG_VMCTX, VMCTX_GC_BADMASK);
    m = TMP1;
  }
  a64_tst_reg(B, SF64, dst, m);
  struct BarFix *bf = xmalloc(sizeof *bf);
  bf->site = br_cc(s, 0, CC_NE, 0, 0);
  bf->back = B->len;
  bf->dst = dst; bf->base = base; bf->off_reg = off_reg; bf->off = off;
  bf->next = s->bars;
  s->bars = bf;
  rc_kill_reg(s, R30);
}

static void emit_gc_load_stubs(Isel *s) {
  for (struct BarFix *bf = s->bars; bf;) {
    struct BarFix *n = bf->next;
    br_patch(s, bf->site, B->len);
    if (bf->off_reg >= 0) {
      a64_add_reg(B, SF64, S3, bf->base, bf->off_reg);
    } else if (bf->off > 0xfff) {

      a64_load_imm(B, SF64, S3, bf->off);
      a64_add_reg(B, SF64, S3, bf->base, S3);
    } else if (bf->off) {
      a64_add_imm(B, SF64, S3, bf->base, bf->off, 0);
    } else if (bf->base != S3) {
      a64_mov_reg(B, SF64, S3, bf->base);
    }
    if (bf->dst != S2) a64_mov_reg(B, SF64, S2, bf->dst);
    uint32_t sym = obj_intern_sym(s->o, "warmc_gc_load_stub", SYM_UNDEF_FUNC, -1, 0);
    isel_reloc(s, REL_CALL26, B->len, sym, 0);
    a64_bl(B, 0);
    if (bf->dst != S2) a64_mov_reg(B, SF64, bf->dst, S2);
    size_t back = B->len;
    a64_b(B, 0);
    patch_b26_checked(s, back, bf->back);
    xfree(bf);
    bf = n;
  }
  s->bars = 0;
}

static void emit_move_to(Isel *s, Buf *out, int dst, int src, int fp, int wide,
                         int vec) {
  if (dst == src) return;
  if (src == LOC_NONE || dst == LOC_NONE)
    warmc_fatal("internal: move involving a value with no location");

  if (IS_SLOT(dst)) rc_kill_slot(s, SLOT_OF(dst));
  if (IS_SLOT(dst) && IS_SLOT(src)) {
    int t = (fp || vec) ? FTMP : TMP0;
    slot_load(out, fp, vec, t, slot_off(s, SLOT_OF(src)));
    slot_store(out, fp, vec, t, slot_off(s, SLOT_OF(dst)));
  } else if (IS_SLOT(dst)) {
    slot_store(out, fp, vec, src, slot_off(s, SLOT_OF(dst)));
  } else if (IS_SLOT(src)) {
    slot_load(out, fp, vec, dst, slot_off(s, SLOT_OF(src)));
  } else {
    if (vec) a64_vmov(out, dst, src);
    else if (fp) a64_fmov_reg(out, wide ? FT_D : FT_S, dst, src);
    else a64_mov_reg(out, SF64, dst, src);
  }
}

static bool move_bank(const Move *m) { return m->fp || m->vec; }

static bool same_place(int a, bool abank, int b, bool bbank) {
  if (a != b) return false;
  return IS_SLOT(a) || abank == bbank;
}

static void parallel_copy_to(Isel *s, Buf *out, Move *mv, int n) {
  uint8_t *done = xcalloc(n ? n : 1, 1);
  int remaining = 0;
  for (int i = 0; i < n; i++) {
    if (mv[i].src == mv[i].dst) done[i] = 1;
    else remaining++;
  }
  while (remaining > 0) {
    int progress = 0;
    for (int i = 0; i < n; i++) {
      if (done[i]) continue;
      int blocked = 0;
      for (int j = 0; j < n; j++)
        if (!done[j] && j != i &&
            same_place(mv[j].src, move_bank(&mv[j]),
                       mv[i].dst, move_bank(&mv[i]))) { blocked = 1; break; }
      if (blocked) continue;
      emit_move_to(s, out, mv[i].dst, mv[i].src, mv[i].fp, mv[i].wide, mv[i].vec);
      done[i] = 1;
      remaining--;
      progress = 1;
    }
    if (progress) continue;

    bool broke = false;
    for (int i = 0; i < n && !broke; i++) {
      if (done[i]) continue;
      int tmp = move_bank(&mv[i]) ? F3 : TMP1;
      if (mv[i].src == tmp) continue;
      emit_move_to(s, out, tmp, mv[i].src, mv[i].fp, mv[i].wide, mv[i].vec);

      for (int j = 0; j < n; j++)
        if (!done[j] && mv[j].src == mv[i].src &&
            move_bank(&mv[j]) == move_bank(&mv[i]))
          mv[j].src = tmp;
      broke = true;
    }
    if (!broke)
      warmc_fatal("internal: %d moves left with no cycle to break", remaining);
  }
  xfree(done);
}

static void parallel_copy(Isel *s, Move *mv, int n) {
  parallel_copy_to(s, B, mv, n);
}

static int edge_moves(Isel *s, Edge *e, Move *mv) {
  IrFunc *f = s->f;
  Block *t = &f->blocks[e->target];
  int n = 0;
  for (uint32_t i = 0; i < t->nparams; i++) {
    Ref dstv = t->params[i], srcv = e->args[i];
    if (s->loc[dstv] == LOC_NONE) continue;
    if (s->loc[dstv] == s->loc[srcv]) continue;
    mv[n].src = s->loc[srcv];
    mv[n].dst = s->loc[dstv];
    mv[n].fp = is_fp_value(f, dstv);
    mv[n].wide = is64(f, dstv);
    mv[n].vec = is_vec_value(f, dstv);
    n++;
  }
  return n;
}

static void emit_edge(Isel *s, Edge *e) {
  Block *t = &s->f->blocks[e->target];
  if (t->nparams == 0) return;
  Move *mv = xmalloc(t->nparams * sizeof(Move));
  int n = edge_moves(s, e, mv);
  parallel_copy(s, mv, n);
  xfree(mv);
}

__attribute__((unused))

static void emit_call_args_core(Isel *s, const Ref *args, uint32_t nargs,
                                int stack_base, int first_int,
                                bool want_vmctx) {
  IrFunc *f = s->f;
  int next_int = first_int, next_flt = 0, stack_slot = 0;
  Move mv[24];
  int nmv = 0;

  int ints = 0, flts = 0;
  for (uint32_t i = 0; i < nargs; i++) {
    Ref v = args[i];
    int fp = is_fp_value(f, v), wide = is64(f, v), vec = is_vec_value(f, v);
    int l = s->loc[v];
    bool on_stack = fp ? (flts >= 8) : (ints >= 8 - first_int);
    if (fp) flts++; else ints++;
    if (!on_stack) continue;
    uint32_t at = (uint32_t)stack_slot++ * ARG_STACK_SLOT;
    if (vec) {
      int r = IS_SLOT(l) ? (slot_load(B, 1, 1, FTMP, slot_off(s, SLOT_OF(l))), FTMP) : l;
      a64_str_q(B, r, stack_base, at);
    } else if (fp) {
      int r = IS_SLOT(l) ? (slot_load(B, 1, 0, FTMP, slot_off(s, SLOT_OF(l))), FTMP) : l;
      a64_fstr_imm(B, wide ? 3 : 2, r, stack_base, at);
    } else {
      int r = IS_SLOT(l) ? (slot_load(B, 0, 0, TMP0, slot_off(s, SLOT_OF(l))), TMP0) : l;
      a64_str_imm(B, wide ? 3 : 2, r, stack_base, at);
    }
  }

  for (uint32_t i = 0; i < nargs; i++) {
    Ref v = args[i];
    int fp = is_fp_value(f, v), wide = is64(f, v), vec = is_vec_value(f, v);
    int l = s->loc[v];
    if (fp) {
      if (next_flt >= 8) { next_flt++; continue; }
      mv[nmv++] = (Move){ l, next_flt++, 1, (uint8_t)wide, (uint8_t)vec };
    } else {
      if (next_int > 7) { next_int++; continue; }
      mv[nmv++] = (Move){ l, next_int++, 0, (uint8_t)wide, 0 };
    }
  }
  if (want_vmctx) mv[nmv++] = (Move){ REG_VMCTX, R0, 0, 1, 0 };
  parallel_copy(s, mv, nmv);
}

static void emit_call_args_to(Isel *s, const Ref *args, uint32_t nargs,
                              int stack_base) {
  emit_call_args_core(s, args, nargs, stack_base, 1, true);
}

static void emit_call_args(Isel *s, const Ref *args, uint32_t nargs) {
  emit_call_args_core(s, args, nargs, RSP, 1, true);
}

static void emit_call_args_novmctx(Isel *s, const Ref *args, uint32_t nargs) {
  emit_call_args_core(s, args, nargs, RSP, 1, false);
}

static void emit_call_args_at(Isel *s, const Ref *args, uint32_t nargs,
                              int first_int) {
  emit_call_args_core(s, args, nargs, RSP, first_int, true);
}

static void emit_call_ret(Isel *s, Ref call, CompType *sig) {
  s->mem_valid = false;
  if (sig->nresults == 1) {

    if (vt_is_vec_reg(sig->results[0])) commit(s, call, 0);
    else commit(s, call, R0);
  }
}

static int vmctx_at(Isel *s, uint32_t off, int size, uint32_t *disp,
                    int scratch) {
  if ((off & ((1u << size) - 1u)) == 0 && (off >> size) <= 0xfffu) {
    *disp = off;
    return REG_VMCTX;
  }
  a64_load_imm(B, SF64, scratch, off);
  a64_add_reg(B, SF64, scratch, REG_VMCTX, scratch);
  *disp = 0;
  return scratch;
}

static uint32_t mem_slot_off(IrFunc *f, uint64_t k) {
  return VMCTX_MEMS(f->m->nglobals, f->m->nimported_funcs, f->m->ntables) +
         MEM_SLOT_SIZE * (uint32_t)k;
}

static int mem_base_reg(Isel *s, uint64_t k, int scratch);

static int mem_base_reg_fwd(Isel *s, uint64_t k, int scratch) {
  return mem_base_reg(s, k, scratch);
}

static int mem_base_reg(Isel *s, uint64_t k, int scratch) {
  if (k == 0) return REG_MEMBASE;
  uint32_t disp;
  int base = vmctx_at(s, mem_slot_off(s->f, k) + MEM_SLOT_BASE, 3, &disp, scratch);
  a64_ldr_imm(B, 3, scratch, base, disp);
  return scratch;
}

static void emit_type_check(Isel *s, const CompType *want) {

  int want_reg = (s->tid_reg && want->id == s->tid_val) ? s->tid_reg : -1;
  if (want->final_) {
    a64_ldr_imm(B, 3, S3, S1, FUNCREF_TYPE);
    if (want_reg >= 0) {
      a64_cmp_reg(B, SF64, S3, want_reg);
    } else {
      a64_load_imm(B, SF64, TMP0, want->id);
      a64_cmp_reg(B, SF64, S3, TMP0);
    }
    trap_if_cc(s, CC_NE, TRAP_INDIRECT_TYPE);
    return;
  }
  a64_ldr_imm(B, 3, S3, S1, FUNCREF_RTT);
  a64_ldr_imm(B, 3, TMP1, S3, 0);
  if (want->depth + 1u <= 4095u) {
    a64_cmp_imm(B, SF64, TMP1, want->depth + 1u);
  } else {
    a64_load_imm(B, SF64, TMP0, want->depth + 1u);
    a64_cmp_reg(B, SF64, TMP1, TMP0);
  }
  trap_if_cc(s, CC_LO, TRAP_INDIRECT_TYPE);
  uint64_t off = 8ull + 8ull * want->depth;
  if (off <= 8ull * 4095ull) {
    a64_ldr_imm(B, 3, S3, S3, (uint32_t)off);
  } else {
    a64_load_imm(B, SF64, TMP0, off);
    a64_add_reg(B, SF64, S3, S3, TMP0);
    a64_ldr_imm(B, 3, S3, S3, 0);
  }
  if (want_reg >= 0) {
    a64_cmp_reg(B, SF64, S3, want_reg);
  } else {
    a64_load_imm(B, SF64, TMP0, want->id);
    a64_cmp_reg(B, SF64, S3, TMP0);
  }
  trap_if_cc(s, CC_NE, TRAP_INDIRECT_TYPE);
}

static bool table_fixed_len(const Module *m, uint32_t ti, uint64_t *out) {
  const Table *t = &m->tables[ti];
  if (t->imported || !t->has_max || t->max != t->min) return false;
  *out = t->min;
  return true;
}

static void emit_indirect_lookup(Isel *s, Inst *in, Ref callee) {
  IrFunc *f = s->f;
  int idx = rd(s, callee, S0);
  uint64_t flen;
  bool fixed = table_fixed_len(f->m, (uint32_t)in->imm2, &flen) && flen <= 4095;
  if (in->imm2 == 0) {
    a64_ldr_imm(B, 3, S1, REG_VMCTX, VMCTX_TABLE_BASE);
    if (!fixed) a64_ldr_imm(B, 3, S2, REG_VMCTX, VMCTX_TABLE_LEN);
  } else {
    uint32_t off = VMCTX_TABLES(f->m->nglobals, f->m->nimported_funcs) +
                   TABLE_SLOT_SIZE * (uint32_t)in->imm2;

    uint32_t disp;
    int base = vmctx_at(s, off, 3, &disp, TMP1);
    a64_ldr_imm(B, 3, S1, base, disp + TABLE_SLOT_ENTRIES);
    if (!fixed) a64_ldr_imm(B, 3, S2, base, disp + TABLE_SLOT_LEN);
  }

  bool i64 = f->m->tables[in->imm2].index64;
  if (fixed) {

    a64_cmp_imm(B, i64 ? SF64 : SF32, idx, (uint32_t)flen);
    trap_if_cc(s, CC_HS, TRAP_INDIRECT_OOB);
  } else if (i64) {
    a64_cmp_reg(B, SF64, S2, idx);
    trap_if_cc(s, CC_LS, TRAP_INDIRECT_OOB);
  } else {
    a64_cmp_uxtw(B, S2, idx, 0);
    trap_if_cc(s, CC_LS, TRAP_INDIRECT_OOB);
  }
  if (i64) a64_add_reg_lsl(B, SF64, S1, S1, idx, 3);
  else a64_add_uxtw(B, S1, S1, idx, 3);

  a64_ldr_imm(B, 3, S1, S1, 0);
  trap_if_zero(s, SF64, S1, TRAP_INDIRECT_NULL);
  a64_ldr_imm(B, 3, S2, S1, FUNCREF_CODE);
  emit_type_check(s, &f->m->types[in->imm]);
}

static void emit_ref_lookup(Isel *s, Ref callee) {
  int cell = rd(s, callee, S1);
  if (cell != S1) a64_mov_reg(B, SF64, S1, cell);
  trap_if_zero(s, SF64, S1, TRAP_NULL_REF);
  a64_ldr_imm(B, 3, S2, S1, FUNCREF_CODE);
}

static void emit_ret_area(Isel *s, CompType *sig) {
  if (sig->nresults <= 1) return;
  uint32_t off = s->retarea_off;
  if (off < 0x1000) a64_add_imm(B, SF64, R8, RSP, off, 0);
  else { a64_load_imm(B, SF64, TMP0, off); a64_add_extx(B, R8, RSP, TMP0, 0); }
}

static void note_safepoint(Isel *s, Ref v);

static void call_helper_imm(Isel *s, const char *name, const Ref *args, uint32_t nargs,
                            uint64_t imm, Ref result, bool has_result) {
  csa_save(s, s->cur_inst);
  emit_call_args(s, args, nargs);
  a64_load_imm(B, SF32, R1 + (int)nargs, imm);
  uint32_t sym = obj_intern_sym(s->o, name, SYM_UNDEF_FUNC, -1, 0);
  isel_reloc(s, REL_CALL26, B->len, sym, 0);
  a64_bl(B, 0);
  note_safepoint(s, s->cur_inst);
  s->mem_valid = false;
  if (has_result) commit(s, result, R0);
  csa_restore(s, s->cur_inst);
}

static void call_helper_imm_first(Isel *s, const char *name, const Ref *args,
                                  uint32_t nargs, uint64_t imm, Ref result,
                                  bool has_result) {
  csa_save(s, s->cur_inst);
  emit_call_args_at(s, args, nargs, 2);
  a64_load_imm(B, SF32, R1, imm);
  uint32_t sym = obj_intern_sym(s->o, name, SYM_UNDEF_FUNC, -1, 0);
  isel_reloc(s, REL_CALL26, B->len, sym, 0);
  a64_bl(B, 0);
  note_safepoint(s, s->cur_inst);
  s->mem_valid = false;
  if (has_result) commit(s, result, R0);
  csa_restore(s, s->cur_inst);
}

#define GC_FASTNEW_MAX 256u

/* The one definition of "this allocation lowers to an inline bump with the
   helper call out of line" — the emitter and the register allocator must
   agree on it, or a save emitted for the slow arm covers the wrong site. */
static bool gc_fastnew_shape(IrFunc *f, Ref v, uint64_t *total_out,
                             int64_t *alen_out) {
  Inst *in = &f->insts[v];
  if (in->op == I_STRUCT_NEW) {
    uint64_t total = in->imm2 & ~(uint64_t)GCA_CACHED;
    if (!total || total > GC_FASTNEW_MAX) return false;
    *total_out = total;
    *alen_out = -1;
    return true;
  }
  if (in->op != I_ARRAY_NEW || in->nargs != 1) return false;
  Ref *a = ir_args(f, v);
  if (f->insts[a[0]].op != I_CONST) return false;
  uint32_t stride = (uint32_t)(in->imm2 & 0xff);
  uint32_t hdrsz = (uint32_t)((in->imm2 >> 8) & 0xff);
  if (!hdrsz || !stride) return false;
  uint64_t alen = f->insts[a[0]].imm & 0xffffffffu;
  uint64_t total = hdrsz + alen * stride;
  total = (total + 15) & ~15ull;
  if (total < 16) total = 16;
  if (total > GC_FASTNEW_MAX) return false;
  *total_out = total;
  *alen_out = (int64_t)alen;
  return true;
}

static bool emit_gc_fastnew(Isel *s, Ref v, uint32_t type_idx, uint64_t total,
                            int64_t arr_len, const char *helper,
                            const Ref *args, uint32_t nargs) {
  if (!total || total > GC_FASTNEW_MAX) return false;

  bool cached = (s->f->insts[v].imm2 & GCA_CACHED) != 0;
  a64_ldr_imm(B, 3, S0, REG_VMCTX, VMCTX_GC_LAB);

  int cur = cached ? s->lab_reg : S1;
  if (!cached) a64_ldr_imm(B, 3, S1, S0, 0);
  a64_ldr_imm(B, 3, S2, S0, 8);
  a64_add_imm(B, SF64, S3, cur, (uint32_t)total, 0);
  a64_cmp_reg(B, SF64, S3, S2);
  size_t bslow = br_cc(s, 0, CC_HI, 0, 0);
  if (!cached) a64_str_imm(B, 3, S3, S0, 0);

  if (cached) {
    a64_mov_reg(B, SF64, R0, cur);
    a64_mov_reg(B, SF64, s->lab_reg, S3);
    cur = R0;
  }
  a64_ldr_imm(B, 2, S2, REG_VMCTX, VMCTX_GC_HDR);

  if (type_idx) {
    uint32_t off = (uint32_t)type_idx << GC_TYPE_SHIFT;
    if (off <= 0xfffu) {
      a64_add_imm(B, SF32, S2, S2, off, 0);
    } else {
      a64_load_imm(B, SF32, S3, off);
      a64_add_reg(B, SF32, S2, S2, S3);
    }
  }
  a64_str_imm(B, 2, S2, cur, 0);
  if (arr_len > 0) {
    a64_load_imm(B, SF32, S2, (uint64_t)arr_len);
    a64_str_imm(B, 2, S2, cur, 4);
  }
  if (!cached) a64_mov_reg(B, SF64, R0, cur);

  struct AllocFix *af = xmalloc(sizeof *af);
  af->site = bslow;

  af->back = B->len;
  af->args = args; af->nargs = nargs;
  af->helper = helper; af->type_idx = type_idx;
  af->inst = s->cur_inst;
  af->cached = cached;
  af->next = s->allocs;
  s->allocs = af;
  s->mem_valid = false;
  commit(s, v, R0);
  return true;
}

static void emit_alloc_slow_arms(Isel *s) {
  for (struct AllocFix *af = s->allocs; af;) {
    struct AllocFix *n = af->next;
    size_t back_to = af->back;
    br_patch(s, af->site, B->len);
    Ref saved = s->cur_inst;
    s->cur_inst = af->inst;
    csa_save(s, af->inst);

    if (af->cached) {
      a64_ldr_imm(B, 3, TMP0, REG_VMCTX, VMCTX_GC_LAB);
      a64_str_imm(B, 3, s->lab_reg, TMP0, 0);
    }
    if (af->nargs) emit_call_args_at(s, af->args, af->nargs, 2);
    else emit_call_args(s, af->args, 0);
    a64_load_imm(B, SF32, R1, af->type_idx);
    uint32_t sym = obj_intern_sym(s->o, af->helper, SYM_UNDEF_FUNC, -1, 0);
    isel_reloc(s, REL_CALL26, B->len, sym, 0);
    a64_bl(B, 0);
    note_safepoint(s, af->inst);
    s->cur_inst = saved;

    if (af->cached) {
      a64_ldr_imm(B, 3, TMP0, REG_VMCTX, VMCTX_GC_LAB);
      a64_ldr_imm(B, 3, s->lab_reg, TMP0, 0);
    }
    csa_restore(s, af->inst);
    size_t site = B->len;
    a64_b(B, 0);
    patch_b26_checked(s, site, back_to);
    xfree(af);
    af = n;
  }
  s->allocs = NULL;
}

static void call_helper_imm2_first(Isel *s, const char *name, const Ref *args,
                                   uint32_t nargs, uint64_t imm, uint64_t imm2,
                                   Ref result, bool has_result) {
  csa_save(s, s->cur_inst);
  emit_call_args_at(s, args, nargs, 3);
  a64_load_imm(B, SF32, R1, imm);
  a64_load_imm(B, SF32, R2, imm2);
  uint32_t sym = obj_intern_sym(s->o, name, SYM_UNDEF_FUNC, -1, 0);
  isel_reloc(s, REL_CALL26, B->len, sym, 0);
  a64_bl(B, 0);
  note_safepoint(s, s->cur_inst);
  s->mem_valid = false;
  if (has_result) commit(s, result, R0);
  csa_restore(s, s->cur_inst);
}

static void call_helper_imm2(Isel *s, const char *name, const Ref *args, uint32_t nargs,
                             uint64_t imm, uint64_t imm2) {
  csa_save(s, s->cur_inst);
  emit_call_args(s, args, nargs);
  a64_load_imm(B, SF32, R1 + (int)nargs, imm);
  a64_load_imm(B, SF32, R2 + (int)nargs, imm2);
  uint32_t sym = obj_intern_sym(s->o, name, SYM_UNDEF_FUNC, -1, 0);
  isel_reloc(s, REL_CALL26, B->len, sym, 0);
  a64_bl(B, 0);
  note_safepoint(s, s->cur_inst);
  s->mem_valid = false;
  csa_restore(s, s->cur_inst);
}

static void emit_teardown(Isel *s);
static void emit_epilogue(Isel *s);
static void emit_block(Isel *s, uint32_t bi, uint32_t next_bi);
static bool skip_location(Isel *s, Ref v);

static void note_site(Isel *s) {
  if (!s->has_pads || !s->at_eh_site) return;
  IrFunc *f = s->f;
  Block *b = &f->blocks[s->cur_block];
  uint32_t first = s->nclauses, n = 0;
  for (uint32_t i = 0; i < b->nsuccs; i++) {
    if (!b->succs[i].eh) continue;
    Block *pad = &f->blocks[b->succs[i].target];
    if (s->nclauses == s->cap_clauses) {
      s->cap_clauses = s->cap_clauses ? s->cap_clauses * 2 : 8;
      s->clauses = xrealloc(s->clauses, s->cap_clauses * sizeof(UwClause));
    }
    UwClause *c = &s->clauses[s->nclauses++];
    c->kind = pad->pad_kind;
    c->tag = pad->pad_tag;
    c->pad = b->succs[i].target;
    c->pad_ = 0;
    n++;
  }
  if (!n) return;
  if (s->nsites == s->cap_sites) {
    s->cap_sites = s->cap_sites ? s->cap_sites * 2 : 8;
    s->sites = xrealloc(s->sites, s->cap_sites * sizeof(UwSite));
  }
  UwSite *site = &s->sites[s->nsites++];
  site->pc = (uint32_t)s->body.len;
  site->first = first;
  site->n = n;
}

static void note_safepoint(Isel *s, Ref v) {
  if (!s->sp_n || !s->sp_n[v]) return;
  IrFunc *f = s->f;
  uint32_t first = s->ngcroots;
  for (uint32_t k = 0; k < s->sp_n[v]; k++) {
    Ref x = s->sp_vals[s->sp_first[v] + k];
    int l = s->loc[x];
    uint32_t entry;
    if (l == LOC_NONE || skip_location(s, x)) {

      /* A null constant needs no root; an immutable global's referent is
         rooted by the global itself for as long as the module lives, and
         the collector never moves it, so a rematerialised copy the tables
         cannot see stays valid. */
      Inst *xi = &f->insts[x];
      if (xi->op != I_CONST &&
          !(xi->op == I_GLOBAL_GET && !f->m->globals[xi->imm].mutable_))
        warmc_fatal("internal: %s live across a safepoint in %s has no location",
                    ir_op_name(xi->op),
                    f->m->funcs[f->func_index].sym);
      continue;
    }
    if (IS_SLOT(l)) {
      entry = GCROOT_SLOT(slot_off(s, SLOT_OF(l)));
    } else {
      if (l < R19 || l > R28 || !gpr_is_callee_saved(l))
        warmc_fatal("internal: a reference live across a safepoint in %s is in "
                    "x%d, which a callee does not preserve",
                    f->m->funcs[f->func_index].sym, l);
      entry = GCROOT_REG((uint32_t)(l - R19));
    }

    bool seen = false;
    for (uint32_t j = first; j < s->ngcroots && !seen; j++)
      if (s->gcroots[j] == entry) seen = true;
    if (seen) continue;
    if (s->ngcroots == s->cap_gcroots) {
      s->cap_gcroots = s->cap_gcroots ? s->cap_gcroots * 2 : 16;
      s->gcroots = xrealloc(s->gcroots, s->cap_gcroots * sizeof(uint32_t));
    }
    s->gcroots[s->ngcroots++] = entry;
  }

  if (s->ngcroots == first) return;
  if (s->ngcsites == s->cap_gcsites) {
    s->cap_gcsites = s->cap_gcsites ? s->cap_gcsites * 2 : 8;
    s->gcsites = xrealloc(s->gcsites, s->cap_gcsites * sizeof(GcSite));
  }
  GcSite *site = &s->gcsites[s->ngcsites++];
  site->pc = (uint32_t)s->body.len;
  site->first = first;
  site->n = s->ngcroots - first;
}

static void emit_eh_stores(Isel *s, uint32_t bi) {
  IrFunc *f = s->f;
  Block *b = &f->blocks[bi];
  uint32_t cap = 0;
  for (uint32_t i = 0; i < b->nsuccs; i++)
    if (b->succs[i].eh) cap += f->blocks[b->succs[i].target].nparams;
  if (!cap) return;
  Move *mv = xmalloc(cap * sizeof(Move));
  int n = 0;
  for (uint32_t i = 0; i < b->nsuccs; i++)
    if (b->succs[i].eh) n += edge_moves(s, &b->succs[i], mv + n);
  parallel_copy(s, mv, n);
  xfree(mv);
}

static bool is_not(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  if (in->op != I_XOR || in->nargs != 2) return false;
  Ref c = ir_args(f, v)[1];
  if (f->insts[c].op != I_CONST) return false;
  uint64_t m = vt_is_64((ValType)in->type) ? ~0ull : 0xffffffffu;
  return (f->insts[c].imm & m) == m;
}

static bool is_zero_test(Isel *s, Ref v) {
  Inst *in = &s->f->insts[v];
  if (in->op == I_EQZ) return true;
  if (in->op != I_EQ && in->op != I_NE) return false;
  Ref c = ir_args(s->f, v)[1];
  Inst *ci = &s->f->insts[c];
  if (ci->op != I_CONST) return false;
  uint64_t m = vt_is_64((ValType)ci->type) ? ci->imm : (ci->imm & 0xffffffffu);
  return m == 0;
}

static int shift_fuse_kind(IrFunc *f, Ref v, ValType t, bool no_imm) {
  Inst *in = &f->insts[v];
  int width = vt_is_64(t) ? 64 : 32;
  if (no_imm) return -1;
  if (in->op != I_SHL && in->op != I_SHRU && in->op != I_SHRS &&
      in->op != I_ROTR && in->op != I_ROTL) return -1;
  if (!vt_is_64((ValType)in->type) != !vt_is_64(t)) return -1;
  Ref *a = ir_args(f, v);
  if (f->insts[a[1]].op != I_CONST) return -1;
  if (in->op == I_ROTL && (f->insts[a[1]].imm % (uint64_t)width) == 0) return -1;
  switch (in->op) {
    case I_SHL:  return SH_LSL;
    case I_SHRU: return SH_LSR;
    case I_SHRS: return SH_ASR;
    default:     return SH_ROR;
  }
}

static int low_run(uint64_t m, int width) {
  if (m == 0) return 0;
  int w = 0;
  while (w < 64 && (m >> w) != 0) w++;
  uint64_t full = (w >= 64) ? ~0ull : ((1ull << w) - 1);
  if (m != full || w >= width) return 0;
  return w;
}

static bool const_arg(IrFunc *f, Ref v, uint64_t *out) {
  if (f->insts[v].op != I_CONST) return false;
  *out = f->insts[v].imm;
  return true;
}

static struct Bfx bfx_of(Isel *s, Ref v) {
  IrFunc *f = s->f;
  struct Bfx r = { false, false, 0, 0, 0 };
  Inst *in = &f->insts[v];
  if (s->no_imm || in->nargs != 2) return r;
  int width = vt_is_64((ValType)in->type) ? 64 : 32;
  uint64_t wmask = (width == 64) ? ~0ull : 0xffffffffu;
  Ref *a = ir_args(f, v);
  Ref inner = a[0];
  Inst *ci = &f->insts[inner];
  if (s->uses[inner] != 1 || s->folded[inner] || ci->block != in->block) return r;
  if (s->bfx[inner].ok) return r;
  if (vt_is_64((ValType)ci->type) != (width == 64)) return r;
  if (ci->nargs != 2) return r;

  uint64_t icst;
  if (!const_arg(f, ir_args(f, inner)[1], &icst)) return r;
  uint64_t k = icst % (uint64_t)width;
  uint64_t imask = icst & wmask;
  Ref x = ir_args(f, inner)[0];

  uint64_t c;
  if (in->op == I_AND && const_arg(f, a[1], &c)) {
    c &= wmask;
    if (ci->op == I_SHRU) {

      int w = low_run(c, width);
      if (w > 0 && (uint64_t)w + k <= (uint64_t)width) {
        r.ok = true; r.immr = (int)k; r.imms = (int)k + w - 1; r.src = x;
      }
    } else if (ci->op == I_SHL && k > 0 && (c & ((1ull << k) - 1)) == 0) {

      int w = low_run(c >> k, width);
      if (w > 0 && (uint64_t)w + k <= (uint64_t)width) {
        r.ok = true; r.immr = (int)((uint64_t)width - k); r.imms = w - 1; r.src = x;
      }
    }
    return r;
  }

  if ((in->op == I_SHRU || in->op == I_SHRS) && ci->op == I_SHL) {

    uint64_t t;
    if (!const_arg(f, a[1], &t)) return r;
    t %= (uint64_t)width;
    if (t < k) return r;
    r.ok = true;
    r.sign = (in->op == I_SHRS);
    r.immr = (int)(t - k);
    r.imms = width - 1 - (int)k;
    r.src = x;
    return r;
  }

  if (in->op == I_SHRU && ci->op == I_AND) {

    uint64_t t;
    if (!const_arg(f, a[1], &t)) return r;
    t %= (uint64_t)width;
    if (t == 0 || (imask & ((1ull << t) - 1)) != 0) return r;
    int w = low_run(imask >> t, width);
    if (w > 0 && (uint64_t)w + t <= (uint64_t)width) {
      r.ok = true; r.immr = (int)t; r.imms = (int)t + w - 1; r.src = x;
    }
    return r;
  }

  if (in->op == I_SHL && ci->op == I_AND) {

    uint64_t t;
    if (!const_arg(f, a[1], &t)) return r;
    t %= (uint64_t)width;
    int w = low_run(imask, width);
    if (t > 0 && w > 0 && (uint64_t)w + t <= (uint64_t)width) {
      r.ok = true; r.immr = (int)((uint64_t)width - t); r.imms = w - 1; r.src = x;
    }
    return r;
  }
  return r;
}

static int shift_fuse_amount(IrFunc *f, Ref v, ValType t) {
  Inst *in = &f->insts[v];
  int width = vt_is_64(t) ? 64 : 32;
  uint64_t sh = f->insts[ir_args(f, v)[1]].imm % (uint64_t)width;
  if (in->op == I_ROTL) sh = (uint64_t)width - sh;
  return (int)(sh % (uint64_t)width);
}

static void analyse_immediates(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  s->imm_fold = xcalloc(n ? n : 1, 1);
  s->fused = xcalloc(n ? n : 1, 1);
  s->pool_ok = s->opt->cpool && !s->far_br && pool_in_reach(f);
  s->remat_fp = !getenv("WARMC_NO_REMAT_FP");
  s->remat_v128 = !getenv("WARMC_NO_REMAT_V128");
  {
    /* Longest movz+movk chain a constant may cost and still rematerialise
       instead of taking a frame slot.  1 restores the historic single-move
       policy; the census (2026-08-18) put cost-2 constants at 92% of all
       spilled-constant events, so 2 is the default.  SEG spills a constant
       whose per-use move sequences outweigh its per-segment reloads, and
       WCAP spills any constant above that use weight: past it the static
       model cannot tell an L1 reload from the move chain, and the loop is
       hot enough for the guess to matter (regalloc.inc, remat_ok's sweep).
       Measured 2026-08-18, min-of-15 interleaved cycles: rsbench/btreemap
       -7.3%, rsbench/strparse -6.0%, gobench/strbuild -4.1%, vecgrow -1.4%;
       accepted cost gobench/iface +4.2% (small call-crossing constants in
       the dispatch-lookup loops; denying all call-crossing rematerialisation
       recovers half of it but forfeits the strbuild and vecgrow wins). */
    const char *rc = getenv("WARMC_REMAT_COST");
    s->remat_cost = rc ? atoi(rc) : 2;
    if (s->remat_cost < 1) s->remat_cost = 1;
    if (s->remat_cost > 4) s->remat_cost = 4;
    const char *rw = getenv("WARMC_REMAT2_W");
    s->remat2_w = rw ? atof(rw) : 0.5;
    const char *rs = getenv("WARMC_REMAT2_SEG");
    s->remat2_seg = rs ? atof(rs) : 2.0;
    const char *rc2 = getenv("WARMC_REMAT2_WCAP");
    s->remat2_wcap = rc2 ? atof(rc2) : 10000.0;
    s->remat_glob = !getenv("WARMC_NO_REMAT_GLOBAL");
  }
  s->no_remat = xcalloc(n ? n : 1, 1);
  s->paired = xcalloc(n ? n : 1, 1);
  s->no_ldpair = getenv("WARMC_NO_LDPAIR") != NULL;
  s->folded = xcalloc(n ? n : 1, 1);
  uint32_t *uses = xcalloc(n ? n : 1, sizeof(uint32_t));
  s->uses = uses;
  uint8_t *blocked = xcalloc(n ? n : 1, 1);

  s->no_imm = getenv("WARMC_NO_IMM") != NULL;
  if (!s->no_imm)
  for (uint32_t i = 0; i < n; i++)
    if (f->insts[i].op == I_CONST && fits_gpr(f, i))
      s->imm_fold[i] = 1;

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      Ref *a = ir_args(f, u);

      bool via_moves = (in->op == I_CALL || in->op == I_CALL_INDIRECT ||
                        in->op == I_CALL_REF || in->op == I_RETURN_CALL_REF ||
                        in->op == I_RETURN_CALL || in->op == I_RETURN_CALL_INDIRECT ||
                        in->op == I_MEM_GROW || in->op == I_MEM_COPY ||
                        in->op == I_MEM_FILL || in->op == I_MEM_INIT ||
                        in->op == I_TABLE_GET || in->op == I_TABLE_SET ||
                        in->op == I_TABLE_GROW || in->op == I_TABLE_FILL ||
                        in->op == I_TABLE_COPY || in->op == I_TABLE_INIT ||
                        in->op == I_STRUCT_NEW || in->op == I_ARRAY_NEW ||
                        in->op == I_ARRAY_NEW_SEG || in->op == I_ARRAY_OP ||
                        in->op == I_REF_TEST);
      for (uint32_t j = 0; j < in->nargs; j++) {
        uses[a[j]]++;
        if (via_moves) s->no_remat[a[j]] = 1;
        if (!s->imm_fold[a[j]]) continue;
        if (!ir_imm_ok(f, u, j, f->insts[a[j]].imm, (ValType)f->insts[a[j]].type))
          s->imm_fold[a[j]] = 0;
      }
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) { uses[b->term_arg]++; blocked[b->term_arg] = 1; }
    for (uint32_t i = 0; i < b->nret; i++) { uses[b->ret_args[i]]++; blocked[b->ret_args[i]] = 1; }
    for (uint32_t sidx = 0; sidx < b->nsuccs; sidx++)
      for (uint32_t j = 0; j < b->succs[sidx].nargs; j++) {
        uses[b->succs[sidx].args[j]]++;
        blocked[b->succs[sidx].args[j]] = 1;
        s->no_remat[b->succs[sidx].args[j]] = 1;
      }
  }
  for (uint32_t i = 0; i < n; i++) if (blocked[i]) s->imm_fold[i] = 0;

  s->addr_fuse = xcalloc(n ? n : 1, 1);

  uint8_t *addr_only = xmalloc(n ? n : 1);
  memset(addr_only, 1, n ? n : 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      Ref *a = ir_args(f, u);
      for (uint32_t j = 0; j < in->nargs; j++)
        if (j != 0 || (in->op != I_LOAD && in->op != I_STORE)) addr_only[a[j]] = 0;
    }
  }
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if (in->op != I_LOAD && in->op != I_STORE) continue;
      Ref ea = ir_args(f, u)[0];
      if (f->insts[ea].op != I_MEM_ADDR || f->insts[ea].nargs != 1) continue;
      if (blocked[ea]) continue;
      if (uses[ea] != 1 &&
          !(f->insts[ea].imm == 0 && addr_only[ea])) continue;
      uint64_t off = f->insts[ea].imm;
      uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);

      bool ok = (off == 0) ||
                ((off & ((1u << size) - 1)) == 0 && (off >> size) <= 0xfff);
      if (!ok) continue;
      if (!getenv("WARMC_NO_ADDRFUSE")) { s->addr_fuse[ea] = 1; s->folded[ea] = 1; }
    }
  }
  xfree(addr_only);

  s->ptr_fuse = xcalloc(n ? n : 1, 1);
  for (uint32_t i = 0; i < n; i++) {
    Inst *ai = &f->insts[i];
    if (ai->op != I_MEM_ADDR || ai->nargs != 2) continue;
    if (f->insts[ir_args(f, i)[1]].op != I_MEM_BASE) continue;
    if (blocked[i]) warmc_fatal("internal: mem_base pointer escapes an access");
    s->ptr_fuse[i] = 1;
    s->folded[i] = 1;
  }

  s->zx_fuse = xcalloc(n ? n : 1, 1);
  {
    uint8_t *zx_only = xmalloc(n ? n : 1);
    memset(zx_only, 1, n ? n : 1);
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      Block *b = &f->blocks[f->order[oi]];
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref u = b->insts[i];
        Inst *in = &f->insts[u];
        Ref *a = ir_args(f, u);
        bool addr = in->op == I_MEM_ADDR || in->op == I_MEM_PTR;
        for (uint32_t j = 0; j < in->nargs; j++)
          if (j != 0 || !addr) zx_only[a[j]] = 0;
      }
    }
    for (uint32_t i = 0; i < n; i++) {
      Inst *in = &f->insts[i];
      if (in->op != I_EXTENDU || in->nargs != 1) continue;
      if (blocked[i] || !zx_only[i] || uses[i] == 0) continue;
      if (s->folded[i]) continue;
      s->zx_fuse[i] = 1;
      s->folded[i] = 1;
    }
    xfree(zx_only);
  }

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      Ref *a = ir_args(f, u);
      for (uint32_t j = 0; j < in->nargs; j++) {
        if (!s->ptr_fuse[a[j]]) continue;
        if (j != 0 || (in->op != I_LOAD && in->op != I_STORE) || in->imm != 0)
          warmc_fatal("internal: mem_base pointer used outside an access");

        if (f->insts[ir_args(f, a[j])[1]].imm2 != MEMF_MEM(in->imm2))
          warmc_fatal("internal: mem_base pointer used on the wrong memory");
      }
    }
  }

  if (!getenv("WARMC_NO_NOTFUSE"))
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if (in->op != I_AND && in->op != I_OR && in->op != I_XOR) continue;
      if (s->folded[u]) continue;
      Ref *a = ir_args(f, u);
      for (int slot = 1; slot >= 0; slot--) {
        if (slot == 0 && f->insts[a[1]].op == I_CONST && s->imm_fold[a[1]]) continue;
        Ref cand = a[slot];
        if (uses[cand] != 1 || blocked[cand] || s->folded[cand]) continue;
        if (f->insts[cand].block != bi) continue;
        if (!is_not(f, cand)) continue;
        s->folded[cand] = 1;
        if (slot == 0) { Ref t = a[0]; a[0] = a[1]; a[1] = t; }
        break;
      }
    }
  }

  s->bfx = xcalloc(n ? n : 1, sizeof *s->bfx);
  if (!getenv("WARMC_NO_BFX"))
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      uint16_t op = f->insts[u].op;
      if (op != I_AND && op != I_SHL && op != I_SHRU && op != I_SHRS) continue;
      if (blocked[ir_args(f, u)[0]] || s->folded[u]) continue;
      struct Bfx r = bfx_of(s, u);
      if (!r.ok) continue;
      s->bfx[u] = r;
      s->folded[ir_args(f, u)[0]] = 1;
    }
  }

  if (!getenv("WARMC_NO_OPFUSE"))
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if (s->bfx[u].ok) continue;

      if (s->folded[u]) continue;
      bool arith = (in->op == I_ADD || in->op == I_SUB);
      bool logic = (in->op == I_AND || in->op == I_OR || in->op == I_XOR);
      if (!arith && !logic) continue;
      Ref *a = ir_args(f, u);
      for (int slot = 1; slot >= 0; slot--) {
        if (slot == 0 && in->op == I_SUB) continue;

        if (slot == 0 && f->insts[a[1]].op == I_CONST && s->imm_fold[a[1]]) continue;

        if (s->folded[a[0]] || s->folded[a[1]]) break;
        Ref cand = a[slot];
        Inst *ci = &f->insts[cand];
        if (uses[cand] != 1 || blocked[cand] || s->folded[cand]) continue;

        if (s->bfx[cand].ok) continue;
        if (f->insts[cand].block != bi) continue;
        if (shift_fuse_kind(f, cand, (ValType)in->type, s->no_imm) >= 0) {

          if (arith) {
            if (ci->op != I_SHL) continue;
            Ref *ca = ir_args(f, cand);
            uint64_t sh = f->insts[ca[1]].imm % (vt_is_64((ValType)in->type) ? 64 : 32);
            if (sh > 4) continue;
          }
          s->folded[cand] = 1;
          if (slot == 0) { Ref t = a[0]; a[0] = a[1]; a[1] = t; }
          break;
        }
        if (arith && ci->op == I_MUL) {
          s->folded[cand] = 1;
          if (slot == 0) { Ref t = a[0]; a[0] = a[1]; a[1] = t; }
          break;
        }
      }
    }
  }

  if (!getenv("WARMC_NO_FUSE"))
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      if (f->insts[u].op != I_SELECT) continue;
      Ref c = ir_args(f, u)[2];
      if (uses[c] != 1 || blocked[c] || s->folded[c]) continue;
      if (f->insts[c].block != bi) continue;
      uint16_t op = f->insts[c].op;
      if (op == I_EQZ || (op >= I_EQ && op <= I_GEU) || (op >= I_FEQ && op <= I_FGE))
        s->folded[c] = 1;
    }
  }

  if (!getenv("WARMC_NO_MASKSEL"))
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if (in->op != I_AND && in->op != I_OR && in->op != I_XOR) continue;
      if (in->nargs != 2 || s->bfx[u].ok || s->folded[u]) continue;
      Ref *a = ir_args(f, u);
      if (s->folded[a[0]] || s->folded[a[1]]) continue;
      uint64_t m = vt_is_64((ValType)in->type) ? ~0ull : 0xffffffffu;
      for (int slot = 1; slot >= 0; slot--) {
        Ref sel = a[slot];
        Inst *si = &f->insts[sel];
        if (si->op != I_SELECT || si->nargs != 3) continue;
        if (uses[sel] != 1 || blocked[sel] || si->block != bi) continue;
        if (vt_is_64((ValType)si->type) != vt_is_64((ValType)in->type)) continue;
        Ref *sa = ir_args(f, sel);
        if (f->insts[sa[0]].op != I_CONST || f->insts[sa[1]].op != I_CONST) continue;
        uint64_t tv = f->insts[sa[0]].imm & m, fv = f->insts[sa[1]].imm & m;
        if (!((tv == m && fv == 0) || (tv == 0 && fv == m))) continue;

        if (!(s->folded[sa[2]] && f->insts[sa[2]].block == bi)) continue;
        s->folded[sel] = 1;
        if (slot == 0) { Ref t = a[0]; a[0] = a[1]; a[1] = t; }
        break;
      }
    }
  }

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (b->term != T_BRIF) continue;
    Ref c = b->term_arg;
    if (uses[c] != 1) continue;
    if (f->insts[c].block != bi) continue;
    uint16_t op = f->insts[c].op;
    bool cmpish = (op >= I_EQ && op <= I_EQZ) || (op >= I_FEQ && op <= I_FGE);
    if (cmpish && !getenv("WARMC_NO_FUSE")) s->fused[c] = 1;
  }

  if (!getenv("WARMC_NO_TSTFUSE"))
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      if (!is_zero_test(s, u)) continue;

      if (s->fused[u]) continue;
      Ref cand = ir_args(f, u)[0];
      if (f->insts[cand].op != I_AND || f->insts[cand].nargs != 2) continue;
      if (uses[cand] != 1 || blocked[cand] || s->folded[cand]) continue;
      if (s->bfx[cand].ok || f->insts[cand].block != bi) continue;
      Ref *ca = ir_args(f, cand);

      if (s->folded[ca[0]] || s->folded[ca[1]]) continue;

      if (vt_is_64((ValType)f->insts[cand].type) !=
          vt_is_64((ValType)f->insts[ir_args(f, u)[0]].type)) continue;
      int sf = vt_is_64((ValType)f->insts[cand].type) ? SF64 : SF32;
      uint32_t enc;
      bool second_ok = f->insts[ca[1]].op != I_CONST ||
                       a64_logical_imm(sf ? f->insts[ca[1]].imm
                                          : (f->insts[ca[1]].imm & 0xffffffffu),
                                       sf, &enc);
      if (!second_ok) continue;
      s->folded[cand] = 1;
    }
  }

  xfree(blocked);
}

#include "isel_ops.inc"

static const int CS_GPR[] = { R19, R20, R21, R22, R23, R24, R25, R26, R27, R28 };
#define NCS_GPR ((int)(sizeof CS_GPR / sizeof CS_GPR[0]))

static int cs_gpr_list(Isel *s, int *list) {
  int n = 0;
  for (int i = 0; i < NCS_GPR; i++) {
    int r = CS_GPR[i];
    bool needed;

    if (r == R19) needed = s->needs_vmctx && s->vmctx_reg == R19;
    else if (r == R20) needed = s->needs_mem && s->membase_reg == R20;
    else if (r == R21) needed = s->needs_memsize ||
                                (s->used_gpr_cs & (1u << R21)) != 0;
    else needed = (s->used_gpr_cs & (1u << r)) != 0;
    if (needed) list[n++] = r;
  }
  if (n & 1) list[n++] = RZR;
  return n;
}

static bool invalidates_mem(uint16_t op) {
  switch (op) {
    case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
    case I_MEM_GROW: case I_MEM_COPY:
    case I_MEM_FILL: case I_MEM_INIT: case I_DATA_DROP: case I_TABLE_GET:
    case I_TABLE_SET: case I_TABLE_GROW: case I_TABLE_FILL: case I_TABLE_COPY:
    case I_TABLE_INIT: case I_ELEM_DROP:
    case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
    case I_ARRAY_OP: case I_REF_TEST:
      return true;
    default:
      return false;
  }
}

static bool uses_mem(const Inst *in) {
  return in->op == I_BOUNDS_CHECK && in->imm == 0;
}

static void compute_mem_validity(Isel *s) {
  IrFunc *f = s->f;
  s->mem_valid_in = xcalloc(f->nblocks, 1);
  uint8_t *out = xcalloc(f->nblocks, 1);
  for (uint32_t i = 0; i < f->nblocks; i++) { s->mem_valid_in[i] = 1; out[i] = 1; }
  s->mem_valid_in[f->entry] = 1;

  bool changed = true;
  while (changed) {
    changed = false;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      uint8_t in = 1;
      if (bi != f->entry) {
        in = 1;
        for (uint32_t p = 0; p < b->npreds; p++) if (!out[b->preds[p]]) { in = 0; break; }
        if (b->npreds == 0) in = 1;
      }
      uint8_t cur = in;
      for (uint32_t i = 0; i < b->ninsts; i++) {
        const Inst *in = &f->insts[b->insts[i]];
        if (invalidates_mem(in->op)) cur = 0;
        else if (uses_mem(in)) cur = 1;
      }
      if (s->mem_valid_in[bi] != in) { s->mem_valid_in[bi] = in; changed = true; }
      if (out[bi] != cur) { out[bi] = cur; changed = true; }
    }
  }
  xfree(out);
}

static void analyse_function(Isel *s) {
  IrFunc *f = s->f;
  s->has_call = false;
  s->needs_mem = false;
  s->needs_memsize = false;
  s->needs_retarea = false;
  bool touches_instance = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      switch (in->op) {

        case I_LOAD: case I_STORE:
          if (MEMF_MEM(in->imm2) == 0) s->needs_mem = true;
          touches_instance = true;
          break;

        case I_MEM_PTR: case I_MEM_BASE:
          if (in->imm2 == 0) s->needs_mem = true;
          touches_instance = true;
          break;
        case I_VLOAD: case I_VSTORE:
          s->needs_mem = true;
          touches_instance = true;
          break;
        case I_BOUNDS_CHECK:

          if (in->imm == 0 && in->nargs != 2) {
            s->needs_mem = true;
            s->needs_memsize = true;
          }
          touches_instance = true;
          break;
        case I_MEM_LIMIT:
          if (in->imm == 0) s->needs_memsize = true;
          touches_instance = true;
          break;

        case I_FIELD_GET: case I_ARRAY_GET: case I_EXN_PAYLOAD:
          if (load_needs_barrier(f, in)) {
            s->has_call = true;
            touches_instance = true;
          }
          break;

        case I_GC_BADMASK:
          touches_instance = true;
          break;
        case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF: {
          CompType *sig = (in->op == I_CALL)
              ? &f->m->types[f->m->funcs[in->imm].type_index]
              : &f->m->types[in->imm];
          if (sig->nresults > 1) s->needs_retarea = true;
          s->has_call = true;
          touches_instance = true;
          break;
        }

        case I_RETURN_CALL: case I_RETURN_CALL_INDIRECT: case I_RETURN_CALL_REF:
          touches_instance = true;
          break;
        case I_MEM_GROW: case I_MEM_COPY: case I_MEM_FILL: case I_MEM_INIT:
        case I_DATA_DROP: case I_TABLE_GET: case I_TABLE_SET:
        case I_TABLE_GROW: case I_TABLE_FILL: case I_TABLE_COPY:
        case I_TABLE_INIT: case I_ELEM_DROP:
        case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
        case I_ARRAY_OP: case I_REF_TEST:
          s->has_call = true;
          touches_instance = true;
          break;
        case I_TABLE_SIZE:

          if (in->imm != 0) s->has_call = true;
          touches_instance = true;
          break;
        case I_GLOBAL_GET: case I_GLOBAL_SET: case I_MEM_SIZE:
          touches_instance = true;
          break;

        case I_THROW: {
          const CompType *tt =
              &f->m->types[f->m->tags[in->imm].type_index];
          uint32_t want = ARG_STACK_SLOT * tt->nparams;
          if (want > s->throw_area_bytes) s->throw_area_bytes = want;
          touches_instance = true;
        }

        case I_THROW_REF:

          s->has_call = true;
          break;
        default: break;
      }
    }

    if (b->reachable && b->is_pad) { s->has_pads = true; touches_instance = true; }
  }
  if (f->sig->nresults > 1) touches_instance = true;
  if (s->opt->profile) touches_instance = true;
  s->needs_vmctx = touches_instance;
}

static int cs_fpr_list(Isel *s, int *list) {
  int n = 0;
  for (int r = 8; r <= 15; r++) if (s->used_fpr_cs & (1u << r)) list[n++] = r;
  return n;
}

static void emit_prologue(Isel *s, Buf *b) {
  IrFunc *f = s->f;

  int list[NCS_GPR + 1];
  int n = cs_gpr_list(s, list);

  uint32_t nsave = (uint32_t)(s->has_call ? 16 : 0) + 16u * ((uint32_t)n / 2u);

  bool record_only = s->has_call && n == 0;
  if (record_only) {
    a64_stp(b, SF64, R29, R30, RSP, -16, LSP_PRE);
  } else if (nsave) {
    a64_sub_imm(b, SF64, RSP, RSP, nsave, 0);
    uint32_t at = nsave;
    if (s->has_call) { at -= 16; a64_stp(b, SF64, R29, R30, RSP, (int)at, LSP_OFF); }
    for (int i = 0; i < n; i += 2) {
      at -= 16;
      a64_stp(b, SF64, list[i], list[i + 1], RSP, (int)at, LSP_OFF);
    }
  }

  if (s->frame & 0xfff) a64_sub_imm(b, SF64, RSP, RSP, s->frame & 0xfff, 0);
  if (s->frame >> 12) a64_sub_imm(b, SF64, RSP, RSP, s->frame >> 12, 1);

  int fl[8];
  int fn_ = cs_fpr_list(s, fl);
  for (int i = 0; i < fn_; i++)
    a64_fstr_imm(b, 3, fl[i], RSP, s->fpsave_off + 8u * (uint32_t)i);

  if (s->needs_vmctx) a64_mov_reg(b, SF64, REG_VMCTX, R0);

  if (s->has_call) {
    a64_ldr_imm(b, 3, TMP0, REG_VMCTX, 48);
    a64_mov_sp(b, SF64, TMP1, RSP);
    a64_cmp_reg(b, SF64, TMP1, TMP0);
    size_t site = b->len;
    a64_bcond(b, CC_HI, 0);
    a64_load_imm(b, SF32, R0, TRAP_STACK_OVERFLOW);
    uint32_t sym = obj_intern_sym(s->o, "warmc_trap", SYM_UNDEF_FUNC, -1, 0);
    isel_reloc(s, REL_CALL26, b->len | 0x8000000000000000ull, sym, 0);
    a64_bl(b, 0);
    a64_brk(b, 0);
    a64_patch_b19(b, site, b->len);
  }

  if (f->sig->nresults > 1) a64_str_imm(b, 3, R8, RSP, s->retptr_slot);

  Block *e = &f->blocks[f->entry];
  bool clash = false;
  {
    int ni = 1, nf = 0;
    int dst[64], nd = 0, src_of[64];
    for (uint32_t i = 0; i < f->sig->nparams && i < 64; i++) {
      ValType t = f->sig->params[i];
      int fp = vt_is_vec_reg(t);
      int src = -1;
      if (fp) { if (nf < 8) src = nf++; }
      else    { if (ni <= 7) src = ni++; }
      if (src < 0) continue;
      int l = s->loc[e->params[i]];
      if (l == LOC_NONE || IS_SLOT(l)) continue;
      dst[nd] = l; src_of[nd] = src; nd++;
      (void)fp;
    }

    for (int x = 0; x < nd && !clash; x++)
      for (int y = x + 1; y < nd; y++)
        if (dst[x] == src_of[y]) { clash = true; break; }
  }

  int next_int = 1, next_flt = 0, stack_slot = 0;

  Move pmv[16];
  int npmv = 0;
  for (uint32_t i = 0; i < f->sig->nparams; i++) {
    Ref p = e->params[i];
    ValType t = f->sig->params[i];
    bool vec = (t == VT_V128);
    int wide = vt_is_64(t), fp = vt_is_vec_reg(t);
    int src;

    uint32_t at = s->frame + s->saved_bytes + ARG_STACK_SLOT * (uint32_t)stack_slot;
    bool from_stack = false;
    if (fp) {
      if (next_flt < 8) src = next_flt++;
      else {
        stack_slot++;
        if (vec) a64_ldr_q(b, FTMP, RSP, at);
        else a64_fldr_imm(b, wide ? 3 : 2, FTMP, RSP, at);
        src = FTMP;
        from_stack = true;
      }
    } else {
      if (next_int <= 7) src = next_int++;
      else { stack_slot++; a64_ldr_imm(b, wide ? 3 : 2, TMP0, RSP, at); src = TMP0;
             from_stack = true; }
    }
    int l = s->loc[p];
    if (l == LOC_NONE) continue;
    if (IS_SLOT(l)) {
      slot_store(b, fp, vec, src, slot_off(s, SLOT_OF(l)));
    } else if (l != src) {

      if (clash && !from_stack) {
        if (npmv == (int)(sizeof pmv / sizeof pmv[0]))
          warmc_fatal("internal: more parameters in registers than there are");
        pmv[npmv++] = (Move){ src, l, (uint8_t)fp, (uint8_t)(fp ? wide : 1),
                              (uint8_t)vec };
      } else if (vec) a64_vmov(b, l, src);
      else if (fp) a64_fmov_reg(b, wide ? FT_D : FT_S, l, src);
      else a64_mov_reg(b, SF64, l, src);
    }
  }
  if (npmv) parallel_copy_to(s, b, pmv, npmv);

  if (s->needs_mem) {
    a64_ldr_imm(b, 3, REG_MEMBASE, REG_VMCTX, VMCTX_MEM_BASE);
    if (s->needs_memsize) a64_ldr_imm(b, 3, REG_MEMSIZE, REG_VMCTX, VMCTX_MEM_SIZE);
  }
  if (s->tid_reg) a64_load_imm(b, SF64, s->tid_reg, s->tid_val);
}

static void emit_teardown(Isel *s) {
  int fl[8];
  int fn_ = cs_fpr_list(s, fl);
  for (int i = 0; i < fn_; i++)
    a64_fldr_imm(B, 3, fl[i], RSP, s->fpsave_off + 8u * (uint32_t)i);
  if (s->frame & 0xfff) a64_add_imm(B, SF64, RSP, RSP, s->frame & 0xfff, 0);
  if (s->frame >> 12) a64_add_imm(B, SF64, RSP, RSP, s->frame >> 12, 1);
  int list[NCS_GPR + 1];
  int n = cs_gpr_list(s, list);

  uint32_t nsave = (uint32_t)(s->has_call ? 16 : 0) + 16u * ((uint32_t)n / 2u);

  if (s->has_call && n == 0) {
    a64_ldp(B, SF64, R29, R30, RSP, 16, LSP_POST);
  } else if (nsave) {
    uint32_t top = nsave - (uint32_t)(s->has_call ? 16 : 0);
    for (int i = n - 2; i >= 0; i -= 2)
      a64_ldp(B, SF64, list[i], list[i + 1], RSP,
              (int)(top - 16u - 16u * (uint32_t)(i / 2)), LSP_OFF);
    if (s->has_call) a64_ldp(B, SF64, R29, R30, RSP, (int)(nsave - 16), LSP_OFF);
    a64_add_imm(B, SF64, RSP, RSP, nsave, 0);
  }
}

static void emit_epilogue(Isel *s) {
  emit_teardown(s);
  a64_ret(B);
}

static uint32_t module_max_results(Module *m) {
  uint32_t worst = 1;
  for (uint32_t i = 0; i < m->ntypes; i++)
    if (m->types[i].nresults > worst) worst = m->types[i].nresults;
  return worst;
}

static void regalloc_naive(Isel *s);
static void regalloc_linear(Isel *s);

#include "regalloc.inc"

static void collect_refs(IrFunc *f, FuncOut *fout) {
  uint32_t n = 0, cap = 0;
  uint32_t *v = NULL;
  for (uint32_t b = 0; b < f->nblocks; b++)
    for (uint32_t k = 0; k < f->blocks[b].ninsts; k++) {
      const Inst *in = &f->insts[f->blocks[b].insts[k]];
      if (in->op != I_CALL && in->op != I_RETURN_CALL && in->op != I_REF_FUNC)
        continue;
      if (in->imm < f->m->nimported_funcs || in->imm >= f->m->nfuncs) continue;
      if (n == cap) { cap = cap ? cap * 2 : 8; v = xrealloc(v, cap * sizeof *v); }
      v[n++] = (uint32_t)in->imm;
    }
  fout->refs = v;
  fout->nrefs = n;
}

/* 1 = emitted, 0 = a conditional branch needs the far layout, 2 = the
   allocation filed a split plan; the caller applies it and retries. */
static int produce_body(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                        uint32_t out_bytes, FuncOut *fout, bool far,
                        SplitCtl *sc, bool split_collect) {
  int split_ret = 0;
  Buf pro;
  memset(&pro, 0, sizeof pro);
  Isel st;
  memset(&st, 0, sizeof st);
  Isel *s = &st;
  s->f = f; s->o = obj; s->opt = opt;
  s->split_sc = sc;
  s->split_collect = split_collect;
  s->out_bytes = out_bytes;
  s->far_br = far || getenv("WARMC_FAR_BRANCH") != NULL;
  s->naive = !opt->regalloc;

  s->loc = xmalloc(f->ninsts * sizeof(int));
  s->cls = xmalloc(f->ninsts);
  for (uint32_t i = 0; i < f->ninsts; i++) {
    s->loc[i] = LOC_NONE;
    s->cls[i] = (uint8_t)(is_fp_value(f, i) ? 1 : 0);
  }

  analyse_function(s);

  s->lab_reg = 0;
  for (uint32_t bi = 0; bi < f->nblocks && !s->lab_reg; bi++)
    for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++)
      if (f->insts[f->blocks[bi].insts[i]].op == I_GC_LAB_OPEN) {
        s->lab_reg = R28;
        s->used_gpr_cs |= 1u << R28;
        break;
      }

  s->vmctx_reg = R19;
  s->membase_reg = R20;

  s->rc_reg[0] = s->rc_reg[1] = 0;
  s->rc_slot[0] = s->rc_slot[1] = 0;
  s->rc_next = 0;
  {

    bool leaf = f->npads == 0, quasi = f->npads == 0;
    for (uint32_t bi = 0; bi < f->nblocks && quasi; bi++) {
      Block *bb = &f->blocks[bi];
      if (!bb->reachable) continue;
      if (bb->is_pad) { leaf = quasi = false; break; }
      for (uint32_t i = 0; i < bb->ninsts; i++)
        if (op_clobbers_caller_saved(f->insts[bb->insts[i]].op)) {
          leaf = false;
          if (!bb->diverges || i + 1 != bb->ninsts) { quasi = false; break; }
        }
    }

    if (leaf || quasi) {
      if (s->needs_vmctx) s->vmctx_reg = R9;
      if (s->needs_mem) s->membase_reg = R10;
    }
  }

  s->tid_reg = 0;
  s->tid_val = 0;
  {

    uint64_t ids[8];
    double wts[8];
    uint32_t nid = 0;
    double total_w = 0, best_w = 0;
    uint64_t best_id = 0;
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *bb = &f->blocks[bi];
      if (!bb->reachable) continue;
      double w = 1.0;
      for (uint32_t k = 0; k < bb->loop_depth && k < 8; k++) w *= 10.0;
      total_w += w * (double)bb->ninsts;
      if (!bb->loop_depth) continue;
      for (uint32_t i = 0; i < bb->ninsts; i++) {
        Inst *in = &f->insts[bb->insts[i]];
        if (in->op != I_CALL_INDIRECT && in->op != I_CALL_REF &&
            in->op != I_RETURN_CALL_INDIRECT && in->op != I_RETURN_CALL_REF)
          continue;
        if (in->imm >= f->m->ntypes) continue;
        uint64_t id = f->m->types[in->imm].id;
        uint32_t j = 0;
        while (j < nid && ids[j] != id) j++;
        if (j == nid) {
          if (nid == 8) continue;
          ids[nid] = id; wts[nid] = 0; nid++;
        }
        wts[j] += w;
        if (wts[j] > best_w) { best_w = wts[j]; best_id = id; }
      }
    }

    if (best_w > 0 && 3.0 * best_w >= CALL_TID_SHARE * total_w) {
      int r = (s->lab_reg == R28) ? R27 : R28;
      s->tid_reg = r;
      s->tid_val = best_id;
      s->used_gpr_cs |= 1u << r;
    }
  }

  analyse_immediates(s);
  compute_mem_validity(s);

  s->pinned = xcalloc(f->ninsts ? f->ninsts : 1, 1);
  if (s->has_pads)
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *b = &f->blocks[bi];
      if (!b->reachable || !b->is_pad) continue;
      for (uint32_t p = 0; p < b->nparams; p++) s->pinned[b->params[p]] = 1;
    }

  if (s->naive) regalloc_naive(s);
  else regalloc_linear(s);

  if (sc && s->split_collect && sc->nflagged) { split_ret = 2; goto give_up; }
  if (sc && !s->split_collect && !sc->accept && sc->nfailed_new) {
    split_ret = 3;
    goto give_up;
  }

  if (s->has_call && !s->naive) { s->rc_reg[0] = R29; s->rc_reg[1] = R30; }
  compute_safepoints(s);

  s->slots_off = out_bytes;

  s->retarea_off = (out_bytes + 8u * s->nslots + 15u) & ~15u;
  uint32_t maxres = s->needs_retarea ? module_max_results(f->m) : 0;
  s->retptr_slot = s->retarea_off + RESULT_SLOT_SIZE * maxres;
  s->fpsave_off = s->retptr_slot + (f->sig->nresults > 1 ? 8u : 0u);
  uint32_t nfp = 0;
  for (int r = 8; r <= 15; r++) if (s->used_fpr_cs & (1u << r)) nfp++;
  s->exn_slot = s->fpsave_off + 8u * nfp;

  s->throw_area_off = (s->exn_slot + (s->has_pads ? 8u : 0u) + 15u) & ~15u;
  s->frame = s->throw_area_off + s->throw_area_bytes;
  s->frame = (s->frame + 15) & ~15u;
  {
    int list[NCS_GPR + 1];
    int ncs = cs_gpr_list(s, list);
    s->saved_bytes = (uint32_t)(8 * ncs) + (s->has_call ? 16u : 0u);
  }
  if (s->frame > 32000)
    warmc_fatal("function %s needs a %u byte frame, beyond the current addressing limit",
                f->m->funcs[f->func_index].sym, s->frame);
  if (s->frame > g_stats.max_frame_bytes) g_stats.max_frame_bytes = s->frame;

  s->prof_site = xcalloc(f->nblocks, sizeof(size_t));
  s->prof_start = xcalloc(f->nblocks, sizeof(size_t));
  s->block_off = xmalloc(f->nblocks * sizeof(uint32_t));
  for (uint32_t i = 0; i < f->nblocks; i++) s->block_off[i] = 0xffffffffu;

  emit_prologue(s, &pro);

  /* WARMC_LOOP_ALIGN=0|16|32 pads each loop-header label to that boundary
     within the function (the entry is 64-aligned whenever the function has
     a loop, so intra-body congruence is real).  Default 16: an unaligned
     tiny hot loop measured a 2x cycle cliff on M4 when unrelated code
     shifted it across a fetch boundary (ktbench/sum, +97% at
     byte-identical icount).  The nops sit on the entry path and run once
     per loop entry; on a loop entered fifty million times that measured
     +7.5% (rsbench/dyntrait), the accepted side of the trade -- padding
     only never-executed dead space fixed dyntrait but left every
     fall-through-entered loop, sum included, in the cliff lottery. */
  const char *lal_s = getenv("WARMC_LOOP_ALIGN");
  uint32_t lalign = lal_s ? (uint32_t)strtoul(lal_s, NULL, 0) : 16;
  if (lalign != 16 && lalign != 32) lalign = 0;

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    if (lalign && opt->align && f->blocks[bi].loop_depth) {
      Block *lb = &f->blocks[bi];
      bool header = false;
      for (uint32_t p = 0; p < lb->npreds && !header; p++)
        if (f->blocks[lb->preds[p]].rpo >= lb->rpo) header = true;
      if (header)
        while ((pro.len + B->len) & (lalign - 1)) a64_nop(B);
    }
    s->block_off[bi] = (uint32_t)B->len;
    s->mem_valid = s->mem_valid_in[bi] != 0;
    if (opt->profile) {
      a64_ldr_imm(B, 3, TMP0, REG_VMCTX, 56);
      s->prof_site[bi] = B->len;
      a64_add_imm(B, SF64, TMP0, TMP0, 0, 0);
      a64_str_imm(B, 3, TMP0, REG_VMCTX, 56);
      s->prof_start[bi] = B->len;
      s->prof_skip = 0;
    }
    emit_block(s, bi, oi + 1 < f->norder ? f->order[oi + 1] : 0xffffffffu);
    if (opt->profile) {
      uint32_t w = (uint32_t)((B->len - s->prof_start[bi]) / 4);
      w = (w > s->prof_skip) ? w - s->prof_skip : 0;
      if (bi == f->entry) w += (uint32_t)(pro.len / 4);
      if (w > 4095) w = 4095;
      uint32_t word;
      memcpy(&word, B->data + s->prof_site[bi], 4);
      word = (word & ~(0xfffu << 10)) | ((w & 0xfff) << 10);
      memcpy(B->data + s->prof_site[bi], &word, 4);
    }
  }

  emit_alloc_slow_arms(s);
  emit_gc_load_stubs(s);

  for (struct EdgeStub *st = s->edge_stubs; st;) {
    struct EdgeStub *n = st->next;
    size_t at = B->len;
    parallel_copy(s, st->mv, st->n);
    add_fix(s, B->len, 0, st->target);
    a64_b(B, 0);
    br_patch(s, st->site, at);
    xfree(st->mv); xfree(st);
    st = n;
  }
  s->edge_stubs = NULL;

  size_t trap_at[TRAP_CODE_COUNT];
  for (uint32_t c = 0; c < TRAP_CODE_COUNT; c++) {
    if (!s->trap_used[c]) continue;
    trap_at[c] = B->len;
    emit_trap(s, (int)c);
  }
  for (struct TrapFix *t = s->traps; t;) {
    struct TrapFix *n = t->next;
    if (t->is19) br_patch(s, t->site, trap_at[t->code]);
    else patch_b26_checked(s, t->site, trap_at[t->code]);
    xfree(t);
    t = n;
  }
  s->traps = NULL;

  for (BrFix *fx = s->fixups; fx;) {
    BrFix *n = fx->next;
    if (fx->is19) br_patch(s, fx->site, s->block_off[fx->target]);
    else patch_b26_checked(s, fx->site, s->block_off[fx->target]);
    xfree(fx);
    fx = n;
  }

  if (s->npool) {
    while (B->len & 7) a64_word(B, 0);
    for (uint32_t i = 0; i < s->npool; i++) {
      s->pool[i].at = B->len;
      a64_word(B, (uint32_t)s->pool[i].word);
      a64_word(B, (uint32_t)(s->pool[i].word >> 32));
    }
  }
  for (struct PoolFix *p = s->poolfix; p;) {
    struct PoolFix *n = p->next;

    if (!a64_patch_ldr_lit(B, p->site, s->pool[p->idx].at))
      s->far_br_needed = true;
    xfree(p);
    p = n;
  }
  s->poolfix = NULL;

  bool has_loop = false;
  for (uint32_t i = 0; i < f->nblocks && !has_loop; i++)
    if (f->blocks[i].reachable && f->blocks[i].loop_depth > 0) has_loop = true;

  if (getenv("WARMC_VERIFY_RA") && s->csa_consumed != s->ncsa_vals)
    warmc_fatal("internal: %u caller-save entries built but %u emitted in %s",
                s->ncsa_vals, s->csa_consumed, f->m->funcs[f->func_index].sym);

  if (s->far_br_needed) goto give_up;

  memset(fout, 0, sizeof *fout);
  fout->want_align = opt->align && has_loop;
  fout->code_len = (uint32_t)(pro.len + s->body.len);
  fout->code = xmalloc(fout->code_len ? fout->code_len : 1);
  memcpy(fout->code, pro.data, pro.len);
  memcpy(fout->code + pro.len, s->body.data, s->body.len);
  fout->nrelocs = s->nrelocs;
  fout->relocs = xmalloc((s->nrelocs ? s->nrelocs : 1) * sizeof(Reloc));
  for (uint32_t i = 0; i < s->nrelocs; i++) {
    Reloc *r = &s->relocs[i];
    uint64_t off = r->offset;
    if (off & 0x8000000000000000ull) off &= ~0x8000000000000000ull;
    else off += pro.len;
    fout->relocs[i] = *r;
    fout->relocs[i].section = 0;
    fout->relocs[i].offset = off;
  }
  const char *sym = f->m->funcs[f->func_index].sym;
  fout->sym = sym;
  fout->insts_emitted = (pro.len + s->body.len) / 4;
  collect_refs(f, fout);

  {
    FuncUnwind u;
    memset(&u, 0, sizeof u);
    u.func_index = f->func_index;
    u.code_len = (uint32_t)(pro.len + s->body.len);
    u.frame = s->frame;
    u.saved_bytes = s->saved_bytes;
    u.exn_slot = s->exn_slot;
    u.fpsave_off = s->fpsave_off;
    u.saved = s->has_call ? UW_HAS_LR : 0u;
    int list[NCS_GPR + 1];
    int ncs = cs_gpr_list(s, list);
    for (int i = 0; i < ncs; i++)
      if (list[i] != RZR)
        u.saved |= 1u << (UW_GPR_SHIFT + list[i] - R19);
    int fl[8];
    int nfl = cs_fpr_list(s, fl);
    for (int i = 0; i < nfl; i++) u.saved |= 1u << (UW_FPR_SHIFT + fl[i] - 8);
    for (uint32_t i = 0; i < s->nsites; i++) {
      s->sites[i].pc += (uint32_t)pro.len;

      if (s->sites[i].pc >= u.code_len)
        warmc_fatal("internal: protected call site at %u is past the end of %s",
                    s->sites[i].pc, sym);
    }
    for (uint32_t i = 0; i < s->nclauses; i++) {
      uint32_t pad_block = s->clauses[i].pad;
      if (s->block_off[pad_block] == 0xffffffffu)
        warmc_fatal("internal: landing pad b%u in %s was never emitted",
                    pad_block, sym);
      s->clauses[i].pad = s->block_off[pad_block] + (uint32_t)pro.len;
    }
    for (uint32_t i = 0; i < s->ngcsites; i++) {
      s->gcsites[i].pc += (uint32_t)pro.len;
      if (s->gcsites[i].pc >= u.code_len)
        warmc_fatal("internal: safepoint at %u is past the end of %s",
                    s->gcsites[i].pc, sym);
    }
    u.sites = s->sites; u.nsites = s->nsites;
    u.clauses = s->clauses; u.nclauses = s->nclauses;
    u.gcsites = s->gcsites; u.ngcsites = s->ngcsites;
    u.gcroots = s->gcroots; u.ngcroots = s->ngcroots;

    fout->uw = u;
    if (u.nsites) {
      fout->uw.sites = xmalloc(u.nsites * sizeof(UwSite));
      memcpy(fout->uw.sites, u.sites, u.nsites * sizeof(UwSite));
    }
    if (u.nclauses) {
      fout->uw.clauses = xmalloc(u.nclauses * sizeof(UwClause));
      memcpy(fout->uw.clauses, u.clauses, u.nclauses * sizeof(UwClause));
    }
    if (u.ngcsites) {
      fout->uw.gcsites = xmalloc(u.ngcsites * sizeof(GcSite));
      memcpy(fout->uw.gcsites, u.gcsites, u.ngcsites * sizeof(GcSite));
    }
    if (u.ngcroots) {
      fout->uw.gcroots = xmalloc(u.ngcroots * sizeof(uint32_t));
      memcpy(fout->uw.gcroots, u.gcroots, u.ngcroots * sizeof(uint32_t));
    }
  }

give_up:
  buf_free(&pro);
  buf_free(&s->body);
  xfree(s->relocs); xfree(s->loc); xfree(s->cls); xfree(s->block_off);
  xfree(s->mem_valid_in); xfree(s->prof_site); xfree(s->prof_start);
  xfree(s->imm_fold); xfree(s->fused); xfree(s->addr_fuse); xfree(s->ptr_fuse);
  xfree(s->zx_fuse); xfree(s->no_remat); xfree(s->paired); xfree(s->pool);
  xfree(s->folded); xfree(s->uses); xfree(s->bfx); xfree(s->pinned);
  xfree(s->sites); xfree(s->clauses);
  xfree(s->gcsites); xfree(s->gcroots);
  xfree(s->sp_first); xfree(s->sp_n); xfree(s->sp_vals);
  xfree(s->csa_slot); xfree(s->csa_on);
  xfree(s->csa_first); xfree(s->csa_n); xfree(s->csa_vals);
  xfree(s->lv_in); xfree(s->lv_out);
  if (split_ret) return split_ret;
  return s->far_br_needed ? 0 : 1;
}

static void split_edge_push_arg(IrFunc *f, uint32_t from, uint32_t si, Ref v) {
  Edge *e = &f->blocks[from].succs[si];
  if (e->nargs == e->cap) {
    uint32_t oc = e->cap;
    e->cap = e->cap ? e->cap * 2 : 4;
    e->args = ir_grow(f, e->args, (size_t)oc * sizeof(Ref),
                      (size_t)e->cap * sizeof(Ref));
  }
  e->args[e->nargs++] = v;
}

/* Thread each planned value through a new param of its loop header: the
   value itself arrives on the entry edges, the param carries itself around
   the back edges, and every use inside the loop reads the param.  Exit
   edges keep the original value -- the param's range stays the loop, and a
   post-loop use goes on reading the slot it always read.  The new param is
   vetoed so the retry's coalescer does not stitch the web back together. */
static void split_apply_adds(IrFunc *f, SplitCtl *sc) {
  if (!sc->nadd) return;
  if (!sc->addp) sc->addp = xmalloc(sc->nadd * sizeof(Ref));
  uint8_t *member = xcalloc(f->nblocks ? f->nblocks : 1, 1);
  uint32_t *stack = xmalloc(f->nblocks * sizeof(uint32_t));
  uint32_t *mblk = xmalloc(f->nblocks * sizeof(uint32_t));
  {
    uint32_t need = f->ninsts + sc->nadd;
    if (!sc->veto) {
      sc->veto = xcalloc(need, 1);
      sc->veto_n = need;
    } else if (sc->veto_n < need) {
      sc->veto = xrealloc(sc->veto, need);
      memset(sc->veto + sc->veto_n, 0, need - sc->veto_n);
      sc->veto_n = need;
    }
  }
  for (uint32_t k = 0; k < sc->nadd; k++) {
    Ref v = sc->addv[k];
    uint32_t hb = sc->addhdr[k];
    Block *hd = &f->blocks[hb];
    uint32_t nm = 0, sp = 0;
    member[hb] = 1; mblk[nm++] = hb;
    for (uint32_t p = 0; p < hd->npreds; p++) {
      uint32_t pb = hd->preds[p];
      if (!f->blocks[pb].reachable || f->blocks[pb].rpo < hd->rpo) continue;
      if (!member[pb]) { member[pb] = 1; mblk[nm++] = pb; stack[sp++] = pb; }
    }
    while (sp) {
      Block *c = &f->blocks[stack[--sp]];
      for (uint32_t p = 0; p < c->npreds; p++) {
        uint32_t pb = c->preds[p];
        if (member[pb] || !f->blocks[pb].reachable) continue;
        member[pb] = 1; mblk[nm++] = pb; stack[sp++] = pb;
      }
    }

    Ref pnew = ir_add_param(f, hb, (ValType)f->insts[v].type);
    sc->addp[k] = pnew;
    sc->veto[pnew] = 1;
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *b2 = &f->blocks[bi];
      for (uint32_t si = 0; si < b2->nsuccs; si++) {
        if (b2->succs[si].target != hb) continue;
        split_edge_push_arg(f, bi, si, member[bi] ? pnew : v);
      }
    }
    for (uint32_t mi = 0; mi < nm; mi++) {
      Block *b2 = &f->blocks[mblk[mi]];
      for (uint32_t i = 0; i < b2->ninsts; i++) {
        Inst *in = &f->insts[b2->insts[i]];
        Ref *a = ir_args(f, b2->insts[i]);
        for (uint32_t j = 0; j < in->nargs; j++)
          if (a[j] == v) a[j] = pnew;
      }
      if ((b2->term == T_BRIF || b2->term == T_BRTABLE) && b2->term_arg == v)
        b2->term_arg = pnew;
      for (uint32_t i = 0; i < b2->nret; i++)
        if (b2->ret_args[i] == v) b2->ret_args[i] = pnew;
      for (uint32_t si = 0; si < b2->nsuccs; si++) {
        Edge *e = &b2->succs[si];
        if (e->target == hb || !member[e->target]) continue;
        for (uint32_t j = 0; j < e->nargs; j++)
          if (e->args[j] == v) e->args[j] = pnew;
      }
    }
    for (uint32_t mi = 0; mi < nm; mi++) member[mblk[mi]] = 0;
  }
  ir_verify(f, "post-split");
  xfree(member); xfree(stack); xfree(mblk);
}

/* Undo split_apply_adds exactly: each added param is the newest inst and
   the newest param of its header, every use of it was a use of the source
   value, and every edge into the header carries it (or the value) as the
   newest arg.  Unapplying in reverse add order pops each layer off the
   same stacks it was pushed on, leaving the IR the retry's allocation
   never has to know was edited. */
static void split_unapply_adds(IrFunc *f, SplitCtl *sc) {
  for (uint32_t k = sc->nadd; k-- > 0;) {
    Ref p = sc->addp[k], v = sc->addv[k];
    uint32_t hb = sc->addhdr[k];
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *b = &f->blocks[bi];
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Inst *in = &f->insts[b->insts[i]];
        Ref *a = ir_args(f, b->insts[i]);
        for (uint32_t j = 0; j < in->nargs; j++)
          if (a[j] == p) a[j] = v;
      }
      if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == p)
        b->term_arg = v;
      for (uint32_t i = 0; i < b->nret; i++)
        if (b->ret_args[i] == p) b->ret_args[i] = v;
      for (uint32_t si = 0; si < b->nsuccs; si++) {
        Edge *e = &b->succs[si];
        if (e->target == hb) {
          if (!e->nargs)
            warmc_fatal("split unapply: edge %u->%u has no arg to pop", bi, hb);
          e->nargs--;
        } else {
          for (uint32_t j = 0; j < e->nargs; j++)
            if (e->args[j] == p) e->args[j] = v;
        }
      }
    }
    Block *hd = &f->blocks[hb];
    if (!hd->nparams || hd->params[hd->nparams - 1] != p)
      warmc_fatal("split unapply: param v%u is not block %u's newest", p, hb);
    hd->nparams--;
    if (p != f->ninsts - 1)
      warmc_fatal("split unapply: param v%u is not the newest inst", p);
    f->ninsts--;
  }
  if (sc->nadd) ir_verify(f, "post-split-unapply");
}

/* Drop every grant the retry failed to keep in a register.  Cut grants
   are only a veto bit, so pruning one restores the entry-edge coalescing
   it blocked; inv grants were already rolled out of the IR by
   split_unapply_adds, so pruning is compacting them out of the plan.
   Grant counters follow the survivors -- --stats reports what the emitted
   code carries, not what was asked for. */
static void split_prune_plan(IrFunc *f, SplitCtl *sc) {
  bool dbg = getenv("WARMC_SPLIT_DEBUG") != NULL;
  uint32_t w = 0;
  for (uint32_t k = 0; k < sc->nadd; k++) {
    if (sc->inv_fail && sc->inv_fail[k]) {
      if (dbg)
        fprintf(stderr, "split[%u] PRUNE inv v%u hdr=b%u\n", f->func_index,
                sc->addv[k], sc->addhdr[k]);
      g_stats.split_invs--;
      continue;
    }
    sc->addv[w] = sc->addv[k];
    sc->addhdr[w] = sc->addhdr[k];
    w++;
  }
  sc->nadd = w;
  w = 0;
  for (uint32_t c = 0; c < sc->ncut; c++) {
    Ref p = sc->cutp[c];
    if (sc->cut_fail && sc->cut_fail[c]) {
      if (dbg) fprintf(stderr, "split[%u] PRUNE cut v%u\n", f->func_index, p);
      if (p < sc->veto_n) sc->veto[p] = 0;
      g_stats.split_cuts--;
      continue;
    }
    sc->cutp[w++] = p;
  }
  sc->ncut = w;
  if (sc->veto && sc->veto_n > f->ninsts)
    memset(sc->veto + f->ninsts, 0, sc->veto_n - f->ninsts);
  if (sc->inv_fail) memset(sc->inv_fail, 0, sc->cap_add);
  if (sc->cut_fail) memset(sc->cut_fail, 0, sc->cap_cut);
  sc->nfailed_new = 0;
}

void ir_produce_function(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                         uint32_t out_bytes, FuncOut *fout) {
  SplitCtl sc;
  memset(&sc, 0, sizeof sc);

  /* Loop-boundary live-range splitting, opt-in.  The prune loop below
     rolls every failed grant out of the IR, so enabling this never loses
     a race any more (the old chunks +4.9% and the ktbench drift are
     gone); what remains is one reproduced winner (asbench mapint -3%)
     for +8-9% compile time on granted modules -- 71% of grants fail the
     retry and cost their extra regalloc rounds anyway.  Not a trade to
     make by default.  The measured record lives with the
     warmc-split-series memory note. */
  bool collect = opt->regalloc && getenv("WARMC_SPLIT") != NULL;
  int r = produce_body(f, obj, opt, out_bytes, fout, false, &sc, collect);
  if (r == 2) {
    split_apply_adds(f, &sc);
    r = produce_body(f, obj, opt, out_bytes, fout, false, &sc, false);
  }
  /* Each round the retry reports the grants it could not keep in
     registers; they are rolled out of the IR and the survivors re-run, so
     a failed grant never competes.  Every round removes at least one
     grant, so this terminates on its own; the accept backstop only caps a
     pathological function's regalloc reruns. */
  for (int rounds = 0; r == 3; rounds++) {
    split_unapply_adds(f, &sc);
    split_prune_plan(f, &sc);
    split_apply_adds(f, &sc);
    if (rounds >= 8) sc.accept = true;
    r = produce_body(f, obj, opt, out_bytes, fout, false, &sc, false);
  }
  if (r == 0) {
    sc.accept = true;
    r = produce_body(f, obj, opt, out_bytes, fout, true, &sc, false);
  }
  xfree(sc.veto); xfree(sc.addv); xfree(sc.addhdr); xfree(sc.addp);
  xfree(sc.cutp); xfree(sc.inv_fail); xfree(sc.cut_fail);
  if (r == 1) return;
  warmc_fatal("function %u cannot be laid out: a conditional branch is still "
              "out of range after relaxation", f->func_index);
}

typedef struct {
  uint64_t hash;
  uint64_t base;
  uint32_t code_len;
  uint8_t *aux;
  uint32_t naux;
} MergedBody;

typedef struct { uint8_t *p; uint32_t len, cap; } AuxBuf;

static MergedBody *g_mb;
static uint32_t g_nmb, g_cap_mb;
static uint32_t *g_mb_ix, g_mb_ix_cap;
static uint32_t g_mb_hits;
static uint64_t g_mb_bytes;
static AuxBuf g_aux;
static bool g_merge_on = true;
static uint32_t g_dead_bodies;
static uint64_t g_dead_bytes;
static uint64_t g_dead_stub = UINT64_MAX;

void codegen_set_merge(bool on) { g_merge_on = on; }

void codegen_reset_merge(void) {
  for (uint32_t i = 0; i < g_nmb; i++) xfree(g_mb[i].aux);
  xfree(g_mb); xfree(g_mb_ix); xfree(g_aux.p);
  g_mb = NULL; g_mb_ix = NULL; g_aux.p = NULL;
  g_nmb = g_cap_mb = g_mb_ix_cap = 0;
  g_aux.len = g_aux.cap = 0;
  g_mb_hits = 0; g_mb_bytes = 0;
  g_dead_bodies = 0; g_dead_bytes = 0;
  g_dead_stub = UINT64_MAX;
}

void codegen_merge_stats(uint32_t *bodies, uint64_t *bytes) {
  *bodies = g_mb_hits;
  *bytes = g_mb_bytes;
}

static void aux_u32(AuxBuf *a, uint32_t v) {
  if (a->len + 4 > a->cap) {
    a->cap = a->cap ? a->cap * 2 : 256;
    a->p = xrealloc(a->p, a->cap);
  }
  memcpy(a->p + a->len, &v, 4);
  a->len += 4;
}

static void aux_u64(AuxBuf *a, uint64_t v) {
  aux_u32(a, (uint32_t)v);
  aux_u32(a, (uint32_t)(v >> 32));
}

static void aux_build(AuxBuf *a, const FuncOut *fo) {
  const FuncUnwind *u = &fo->uw;
  a->len = 0;
  aux_u32(a, fo->want_align ? 1u : 0u);
  aux_u32(a, fo->nrelocs);
  for (uint32_t i = 0; i < fo->nrelocs; i++) {
    aux_u32(a, (uint32_t)fo->relocs[i].kind);
    aux_u64(a, fo->relocs[i].offset);
    aux_u32(a, fo->relocs[i].sym);
    aux_u64(a, (uint64_t)fo->relocs[i].addend);
  }
  aux_u32(a, u->frame); aux_u32(a, u->saved_bytes);
  aux_u32(a, u->exn_slot); aux_u32(a, u->fpsave_off);
  aux_u32(a, u->saved);
  aux_u32(a, u->nsites);
  for (uint32_t i = 0; i < u->nsites; i++) {
    aux_u32(a, u->sites[i].pc); aux_u32(a, u->sites[i].first);
    aux_u32(a, u->sites[i].n);
  }
  aux_u32(a, u->nclauses);
  for (uint32_t i = 0; i < u->nclauses; i++) {
    aux_u32(a, u->clauses[i].kind); aux_u32(a, u->clauses[i].tag);
    aux_u32(a, u->clauses[i].pad);
  }
  aux_u32(a, u->ngcsites);
  for (uint32_t i = 0; i < u->ngcsites; i++) {
    aux_u32(a, u->gcsites[i].pc); aux_u32(a, u->gcsites[i].first);
    aux_u32(a, u->gcsites[i].n);
  }
  aux_u32(a, u->ngcroots);
  for (uint32_t i = 0; i < u->ngcroots; i++) aux_u32(a, u->gcroots[i]);
}

static uint64_t mb_hash(const void *p, size_t n) {
  const uint8_t *b = p;
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}

static void mb_ix_put(uint32_t i) {
  uint32_t mask = g_mb_ix_cap - 1;
  uint32_t h = (uint32_t)g_mb[i].hash & mask;
  while (g_mb_ix[h]) h = (h + 1) & mask;
  g_mb_ix[h] = i + 1;
}

static void mb_ix_grow(void) {
  uint32_t want = g_mb_ix_cap ? g_mb_ix_cap * 2 : 256;
  xfree(g_mb_ix);
  g_mb_ix = xcalloc(want, sizeof(uint32_t));
  g_mb_ix_cap = want;
  for (uint32_t i = 0; i < g_nmb; i++) mb_ix_put(i);
}

static uint64_t mb_find(const ObjBuilder *obj, const FuncOut *fo,
                        const AuxBuf *aux, uint64_t h) {
  if (!g_mb_ix_cap) return UINT64_MAX;
  uint32_t mask = g_mb_ix_cap - 1;
  uint32_t k = (uint32_t)h & mask;
  while (g_mb_ix[k]) {
    const MergedBody *b = &g_mb[g_mb_ix[k] - 1];
    if (b->hash == h && b->code_len == fo->code_len && b->naux == aux->len &&
        !memcmp(obj->text.data + b->base, fo->code, fo->code_len) &&
        !memcmp(b->aux, aux->p, aux->len))
      return b->base;
    k = (k + 1) & mask;
  }
  return UINT64_MAX;
}

static void mb_add(uint64_t base, const FuncOut *fo, const AuxBuf *aux,
                   uint64_t h) {
  if (g_nmb == g_cap_mb) {
    g_cap_mb = g_cap_mb ? g_cap_mb * 2 : 64;
    g_mb = xrealloc(g_mb, g_cap_mb * sizeof(MergedBody));
  }
  g_mb[g_nmb].hash = h;
  g_mb[g_nmb].base = base;
  g_mb[g_nmb].code_len = fo->code_len;
  g_mb[g_nmb].naux = aux->len;
  g_mb[g_nmb].aux = xmalloc(aux->len ? aux->len : 1);
  memcpy(g_mb[g_nmb].aux, aux->p, aux->len);
  uint32_t idx = g_nmb++;

  if ((uint64_t)g_nmb * 3 >= (uint64_t)g_mb_ix_cap * 2) mb_ix_grow();
  else mb_ix_put(idx);
}

static uint64_t dead_stub(ObjBuilder *obj) {
  if (g_dead_stub != UINT64_MAX) return g_dead_stub;
  g_dead_stub = obj->text.len;
  uint32_t sym = obj_intern_sym(obj, "warmc_unreached", SYM_UNDEF_FUNC, -1, 0);
  obj_add_reloc(obj, REL_CALL26, 0, obj->text.len, sym, 0);
  a64_b(&obj->text, 0);
  return g_dead_stub;
}

void codegen_drop_function(ObjBuilder *obj, FuncOut *fo) {
  uint64_t at = dead_stub(obj);
  obj_intern_sym(obj, fo->sym, SYM_LOCAL_FUNC, 0, at);
  uint32_t si = obj_find_sym(obj, fo->sym);
  obj->syms[si].value = at;
  obj->syms[si].size = 4;
  g_dead_bodies++;
  g_dead_bytes += fo->code_len;
  xfree(fo->uw.sites); xfree(fo->uw.clauses);
  xfree(fo->uw.gcsites); xfree(fo->uw.gcroots);
  xfree(fo->code); xfree(fo->relocs); xfree(fo->refs);
}

void codegen_dead_stats(uint32_t *bodies, uint64_t *bytes) {
  *bodies = g_dead_bodies;
  *bytes = g_dead_bytes;
}

void codegen_commit_function(ObjBuilder *obj, FuncOut *fo) {
  aux_build(&g_aux, fo);
  uint64_t h = mb_hash(fo->code, fo->code_len);
  h ^= mb_hash(g_aux.p, g_aux.len) * 1099511628211ull;
  uint64_t at = g_merge_on ? mb_find(obj, fo, &g_aux, h) : UINT64_MAX;
  if (at != UINT64_MAX) {

    obj_intern_sym(obj, fo->sym, SYM_LOCAL_FUNC, 0, at);
    uint32_t si = obj_find_sym(obj, fo->sym);
    obj->syms[si].value = at;
    obj->syms[si].size = fo->code_len;
    g_mb_hits++;
    g_mb_bytes += fo->code_len;
    g_stats.insts_emitted += fo->insts_emitted;
    xfree(fo->uw.sites); xfree(fo->uw.clauses);
    xfree(fo->uw.gcsites); xfree(fo->uw.gcroots);
    xfree(fo->code); xfree(fo->relocs);
    return;
  }
  if (fo->want_align)
    while (obj->text.len & (ALIGN_BYTES - 1)) a64_nop(&obj->text);
  uint64_t base = obj->text.len;
  buf_bytes(&obj->text, fo->code, fo->code_len);
  for (uint32_t i = 0; i < fo->nrelocs; i++)
    obj_add_reloc(obj, fo->relocs[i].kind, 0, base + fo->relocs[i].offset,
                  fo->relocs[i].sym, fo->relocs[i].addend);
  obj_intern_sym(obj, fo->sym, SYM_LOCAL_FUNC, 0, base);
  uint32_t si = obj_find_sym(obj, fo->sym);
  obj->syms[si].value = base;
  obj->syms[si].size = fo->code_len;
  g_stats.insts_emitted += fo->insts_emitted;
  codegen_note_unwind(&fo->uw);
  if (g_merge_on) mb_add(base, fo, &g_aux, h);
  xfree(fo->uw.sites); xfree(fo->uw.clauses);
  xfree(fo->uw.gcsites); xfree(fo->uw.gcroots);
  xfree(fo->code); xfree(fo->relocs); xfree(fo->refs);
}
