/* isel_core.inc -- the target-neutral half of instruction selection,
   textually included by each backend (isel.c, isel_x64.c) after it has
   defined its register macros.  Everything here is shared state and
   analysis: the Isel struct, locations, safepoint / EH bookkeeping, the
   parallel-copy solver, and the pre-isel function analyses.  Machine
   specifics enter only through the hook declarations below. */

/* remat: src is no location -- rv is a constant (or another value warmc
   rematerialises) made straight into dst once every other move is done. */
typedef struct { int src, dst; uint8_t fp, wide, vec, remat; Ref rv; } Move;

#define LOC_NONE  0x7fffffff
#define SLOT(n)   (-((int)(n) + 1))
#define IS_SLOT(l) ((l) < 0)
#define SLOT_OF(l) ((uint32_t)(-(l) - 1))

struct Bfx { bool ok, sign; int immr, imms; Ref src; };

/* is19: 0 a 26-bit branch, 1 a 19-bit one, BRFIX_TABLE a br_table word that
   holds the target's offset from the table at base. */
#define BRFIX_TABLE 2
typedef struct BrFix {
  size_t site, base;
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
  /* arm64, a frame past a scaled offset's reach: its slots are laid out
     last, and far_save is the near slot a far access parks the register it
     borrows in (sp_access) */
  bool far_frame;
  uint32_t far_save;
  /* placing a tail call's stack arguments, in the incoming area */
  bool tail_args;

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
  /* has_call for something other than a wasm call */
  bool call_other;
  /* Every call is a quasi-leaf's cold one, made in a frame of its own
     (cold_calls_only in isel.c); cold_out is that frame's outgoing area. */
  bool cold_frame;
  uint32_t cold_out;
  /* With an early return tested ahead of the prologue (early_ret_shape):
     1 + the entry's edge the rest of the function goes by, else 0. */
  uint32_t early_main;
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
  uint32_t cur_pos;   /* the instruction's index in cur_block */

  Ref cur_inst;

  bool at_eh_site;
  uint8_t *pinned;

  /* Live-in and live-out sets per block, built on first use (regalloc.inc). */
  uint32_t *lv_in_off, *lv_out_off;
  Ref *lv_in, *lv_out;

  bool no_imm;
  uint8_t *imm_fold;
  uint8_t *fused;
  uint8_t *addr_fuse;
  uint8_t *ptr_fuse;
  uint8_t *zx_fuse;
  /* select(c, x + 1, y), select(c, 0 - x, y) or select(c, x ^ -1, y),
     either way round: 1 + the arm that is the add, negation or not, which
     is folded, plus CS_NEG or CS_INV for the second and third, and the
     select is a csinc, csneg or csinv of y and x */
  uint8_t *csinc;
#define CS_NEG 4u
#define CS_INV 8u
  /* an or or xor of x << k and y >> (width - k), both folded: one extr */
  uint8_t *extr;
  /* Use-cluster pieces: a value with no register of its own (a slot or a
     rematerialisation) held in a register that is free across a run of its
     uses in one block, filled once before the first of them.  pc is sorted
     by (block, first instruction); pc_bfirst/pc_bn index it per block, and
     pc_head/pc_next chain each value's pieces. */
  struct Piece { Ref v; uint32_t block, lo, hi; int reg; } *pc;
  uint32_t npc;
  uint32_t *pc_bfirst, *pc_bn;
  int32_t *pc_head, *pc_next;
  bool pc_live;
  /* a wrap all of whose readers read only the low half of a register */
  uint8_t *lowonly;
  uint8_t *no_remat;
  uint8_t *paired;
  /* (base, offset) of every 32-bit raw-pointer load, sorted; built on the
     first store pair that asks (see narrow_reloaded). */
  struct NarrowLoad { Ref base; int64_t off; } *nl;
  uint32_t nnl;
  bool nl_built;
  bool no_ldpair;
  /* The inline memory.copy/fill fast path's branches to past its call. */
  bool no_small_memop;
  bool no_rc_def;    /* WARMC_NO_RC_DEF: commit does not fill the reload cache */
  size_t smo_done[6];
  uint32_t smo_ndone;
  int smo_cond_zero;
  /* one-entry cache: the fused-address scratch holds mem_base(t1_mem) +
     zx(t1_aa), set by the fused-address emissions, cleared at every other
     instruction */
  Ref t1_aa;
  uint32_t t1_mem;
  bool t1_ok, t1_was;
  uint8_t *folded;
  struct Bfx *bfx;
  /* A vector or that is a rotate: its left shift amount, the shifts folded
     into it (arm64 only). */
  uint8_t *vrot;
  /* A vector add of a pairwise widening add: which operand that is, plus
     one, folded into the add as an add-accumulate (arm64 only). */
  uint8_t *vadal;
  uint32_t *uses;

  uint32_t prof_skip;
  size_t *prof_site;
  size_t *prof_start;

  struct TrapFix { size_t site; bool is19; int code; struct TrapFix *next; } *traps;

  struct { uint64_t word; size_t at; } *pool;
  uint32_t npool, cap_pool;
  /* 16-byte entries, emitted after the word pool: the SIMD lane masks the
     x64 backend loads RIP-relative.  Empty on backends whose vector
     constants all fit in registers. */
  struct { uint64_t lo, hi; size_t at; } *vpool;
  uint32_t nvpool, cap_vpool;
  struct PoolFix { size_t site; uint32_t idx; bool vec; struct PoolFix *next; } *poolfix;
  bool pool_ok;
  bool remat_fp, remat_v128, remat_glob;
  int remat_cost;
  double remat2_w, remat2_seg;
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
  uint32_t prev_bi;   /* the block emitted last, for its flags */

  /* The compare whose result the flags still hold, as cc, while the buffer's
     flags epoch is still cmp_epoch: a select on it reads the flags instead of
     testing the 0/1 it was also materialised to. */
  Ref cmp_ref;
  int cmp_cc;
  uint32_t cmp_epoch;
  Ref cmp_bare;   /* a compare left in the flags only, never made a 0/1 */

  int rc_reg[2];
  uint32_t rc_slot[2];   /* slot + 1, or RC_CONST | the constant */
  bool rc_noconst;       /* making an edge's moves: no constant cache */
  uint8_t rc_used[2];
  uint8_t rc_next;
  /* the same for scalar floats, in two FP registers nothing allocates */
  int frc_reg[2];
  uint32_t frc_slot[2];
  uint8_t frc_used[2];
  uint8_t frc_next;
  /* The value commit last stored to its slot, the register it stored from,
     and where the code ended after the store: while nothing has been
     emitted since, that register still holds the value (arm64's rd). */
  Ref st_v;
  int st_reg;
  size_t st_end;

  int tid_reg;
  uint64_t tid_val;
  /* tid_reg holds the address of code table tid_ict (Module.ict) instead
     of the type id, when the hot type has one */
  bool tid_tab;
  uint32_t tid_ict;

  /* A code-table call_indirect's index outside its window: the full lookup
     out of line, from the register the index was in, back to `back` with
     the code in S2. */
  struct IctFix {
    size_t site, back;
    int idx;
    Inst *in;
    struct IctFix *next;
  } *icts;

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

/* Hooks each backend defines after including this file: spill-slot access,
   register-register moves, the parallel-copy cycle breaker, the
   callee-saved-register -> unwind-index map, FP-constant remat, and the
   reserved registers this function leaves idle for the allocator.
   regalloc.inc additionally wants the GPR_ALLOC / FPR_ALLOC tables, the
   gpr/fpr_is_callee_saved predicates, REG_MEMSIZE, and the
   RA_HAVE_RETAREA_REUSE / RA_RETAREA_REG / RA_ALLOC_RES_REG macros. */
static void slot_load(Isel *s, Buf *bf, int fp, int vec, int reg, uint32_t off);
static void slot_store(Isel *s, Buf *bf, int fp, int vec, int reg, uint32_t off);
static void emit_reg_move(Buf *out, int dst, int src, int fp, int wide,
                          int vec);
static int isel_break_reg(int bank);
static int rd(Isel *s, Ref v, int scratch);
static int isel_cs_index(int reg);
static bool isel_fp_remat_ok(Isel *s, const Inst *in);
static int isel_spare_gprs(Isel *s, int *out);
static bool skip_location(Isel *s, Ref v);
static bool op_clobbers_caller_saved(uint16_t op);

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
  fx->site = site; fx->base = 0; fx->is19 = is19; fx->target = target;
  fx->next = s->fixups;
  s->fixups = fx;
}

static void add_table_fix(Isel *s, size_t site, size_t base, uint32_t target) {
  add_fix(s, site, BRFIX_TABLE, target);
  s->fixups->base = base;
}

static uint32_t slot_off(Isel *s, uint32_t slot) { return s->slots_off + 8u * slot; }

static bool is_vec_value(IrFunc *f, Ref v) { return (ValType)f->insts[v].type == VT_V128; }
static bool is_fp_value(IrFunc *f, Ref v) {
  ValType t = (ValType)f->insts[v].type;
  return vt_is_float(t) || t == VT_V128;
}
static bool is64(IrFunc *f, Ref v) { return vt_is_64((ValType)f->insts[v].type); }

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

#define RC_CONST 0x80000000u

static void rc_clear(Isel *s) {
  s->rc_slot[0] = 0; s->rc_slot[1] = 0;
  s->frc_slot[0] = 0; s->frc_slot[1] = 0;
}

__attribute__((unused))
static void rc_kill_reg(Isel *s, int reg) {
  for (int i = 0; i < 2; i++) if (s->rc_reg[i] == reg) s->rc_slot[i] = 0;
}

static void frc_kill_reg(Isel *s, int reg) {
  for (int i = 0; i < 2; i++) if (s->frc_reg[i] == reg) s->frc_slot[i] = 0;
}

static void rc_kill_slot(Isel *s, uint32_t slot) {
  for (int i = 0; i < 2; i++) if (s->rc_slot[i] == slot + 1) s->rc_slot[i] = 0;
  for (int i = 0; i < 2; i++) if (s->frc_slot[i] == slot + 1) s->frc_slot[i] = 0;
}

/* Is v read again in this block within a few instructions, with no call
   between to clear the reload cache?  Most reads of a value that lives in a
   slot come right after its definition -- a quarter of all stack reloads did,
   in every corpus -- so commit keeps such a value in a cache register too. */
static bool used_soon(Isel *s, Ref v) {
  IrFunc *f = s->f;
  Block *b = &f->blocks[s->cur_block];
  uint32_t end = s->cur_pos + 13;
  if (end > b->ninsts) end = b->ninsts;
  for (uint32_t i = s->cur_pos + 1; i < end; i++) {
    Ref u = b->insts[i];
    Inst *in = &f->insts[u];
    Ref *a = ir_args(f, u);
    for (uint32_t j = 0; j < in->nargs; j++) if (a[j] == v) return true;
    if (op_clobbers_caller_saved(in->op)) return false;
  }
  return end == b->ninsts && b->term_arg == v &&
         (b->term == T_BRIF || b->term == T_BRTABLE);
}

static void rc_put(Isel *s, uint32_t slot, int reg) {
  int pick = -1;
  for (int i = 0; i < 2 && pick < 0; i++)
    if (!s->rc_slot[i] && !s->rc_used[i]) pick = i;
  for (int k = 0; k < 2 && pick < 0; k++) {
    int i = (s->rc_next + k) & 1;
    if (!s->rc_used[i]) pick = i;
  }
  if (pick < 0) return;
  emit_reg_move(B, s->rc_reg[pick], reg, 0, 1, 0);
  s->rc_slot[pick] = slot + 1;
  s->rc_next = (uint8_t)((pick + 1) & 1);
}

static void frc_put(Isel *s, uint32_t slot, int reg, bool wide) {
  int pick = -1;
  for (int i = 0; i < 2 && pick < 0; i++)
    if (!s->frc_slot[i] && !s->frc_used[i]) pick = i;
  for (int k = 0; k < 2 && pick < 0; k++) {
    int i = (s->frc_next + k) & 1;
    if (!s->frc_used[i]) pick = i;
  }
  if (pick < 0) return;
  emit_reg_move(B, s->frc_reg[pick], reg, 1, wide, 0);
  s->frc_slot[pick] = slot + 1;
  s->frc_next = (uint8_t)((pick + 1) & 1);
}

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
    if (l != reg)
      emit_reg_move(B, l, reg, is_fp_value(s->f, v), is64(s->f, v),
                    is_vec_value(s->f, v));
    return;
  }
  slot_store(s, B, is_fp_value(s->f, v), is_vec_value(s->f, v), reg,
             slot_off(s, SLOT_OF(l)));
  s->st_v = v;
  s->st_reg = reg;
  s->st_end = B->len;
  if (s->rc_reg[0] && !s->no_rc_def && !is_fp_value(s->f, v) &&
      !is_vec_value(s->f, v) && used_soon(s, v))
    rc_put(s, SLOT_OF(l), reg);
  if (s->frc_reg[0] && !s->no_rc_def && is_fp_value(s->f, v) &&
      !is_vec_value(s->f, v) && used_soon(s, v))
    frc_put(s, SLOT_OF(l), reg, is64(s->f, v));
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
    slot_store(s, B, is_fp_value(s->f, v), is_vec_value(s->f, v), s->loc[v],
               slot_off(s, (uint32_t)s->csa_slot[v]));
  }
  s->csa_consumed += s->csa_n[site];
}

static void csa_restore(Isel *s, Ref site) {
  if (!s->csa_n || !s->csa_n[site]) return;
  for (uint32_t k = 0; k < s->csa_n[site]; k++) {
    Ref v = s->csa_vals[s->csa_first[site] + k];
    slot_load(s, B, is_fp_value(s->f, v), is_vec_value(s->f, v), s->loc[v],
              slot_off(s, (uint32_t)s->csa_slot[v]));
    g_stats.csa_restores++;
  }
}

static void trap_defer(Isel *s, size_t site, bool is19, int code) {
  struct TrapFix *t = xmalloc(sizeof *t);
  t->site = site; t->is19 = is19; t->code = code; t->next = s->traps;
  s->traps = t;
  s->trap_used[code] = 1;
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

/* A ref.test of a concrete type in the any hierarchy, as an object this
   module allocated answers it.  Such an object's header holds the instance's
   type base plus its type's slot; the slots are the types module_data gives a
   descriptor, numbered in type order, and every module has its own range of
   ids (gc_register_types), so an object of another module's -- even of a
   structurally identical type -- never lands inside it.  For this module's
   objects the slot alone decides the test, as warmc_ref_test would decide it
   from the display the slot's type was emitted with.  *lo..*hi is the first
   run of slots that pass (lo > hi when none does); *exact says no slot
   outside the run passes, so a slot of this module outside it fails without
   asking the runtime, which is left only another module's objects, whose
   slot is not below *nslots.  False when the slots are not numbered as
   module_data numbers them, which leaves the runtime every answer. */
static bool rtest_slots(const Module *m, uint64_t imm, uint64_t id,
                        uint32_t *lo, uint32_t *hi, bool *exact,
                        uint32_t *nslots) {
  uint32_t depth = RTEST_DEPTH(imm);
  uint32_t n = 0;
  *lo = 1;
  *hi = 0;
  *exact = true;
  for (uint32_t i = 0; i < m->ntypes; i++) {
    if (m->type_entry && !m->type_entry[i]) continue;
    if (gctype_slot(m, i) != n) return false;
    const CompType *t = &m->types[i];
    /* a type reach found unused is emitted with an empty display */
    bool live = !m->type_live || m->type_live[i];
    if (live && t->display && t->depth >= depth && t->display[depth] == id) {
      if (*lo > *hi) *lo = *hi = n;
      else if (*hi + 1 == n) *hi = n;
      else *exact = false;
    }
    n++;
  }
  *nslots = n;
  return true;
}

static void emit_move_to(Isel *s, Buf *out, int dst, int src, int fp, int wide,
                         int vec) {
  if (dst == src) return;
  if (src == LOC_NONE || dst == LOC_NONE)
    warmc_fatal("internal: move involving a value with no location");

  if (IS_SLOT(dst)) rc_kill_slot(s, SLOT_OF(dst));
  if (IS_SLOT(dst) && IS_SLOT(src)) {
    int t = (fp || vec) ? FTMP : TMP0;
    slot_load(s, out, fp, vec, t, slot_off(s, SLOT_OF(src)));
    slot_store(s, out, fp, vec, t, slot_off(s, SLOT_OF(dst)));
  } else if (IS_SLOT(dst)) {
    slot_store(s, out, fp, vec, src, slot_off(s, SLOT_OF(dst)));
  } else if (IS_SLOT(src)) {
    slot_load(s, out, fp, vec, dst, slot_off(s, SLOT_OF(src)));
  } else {
    emit_reg_move(out, dst, src, fp, wide, vec);
  }
}

static bool move_bank(const Move *m) { return m->fp || m->vec; }

static bool same_place(int a, bool abank, int b, bool bbank) {
  if (a != b) return false;
  return IS_SLOT(a) || abank == bbank;
}

static void emit_remat_move(Isel *s, Buf *out, const Move *m) {
  if (out != B)
    warmc_fatal("internal: a rematerialised move outside the function body");
  if (IS_SLOT(m->dst)) {
    rc_kill_slot(s, SLOT_OF(m->dst));
    s->rc_noconst = true;
    int r = rd(s, m->rv, (m->fp || m->vec) ? FTMP : TMP0);
    s->rc_noconst = false;
    slot_store(s, out, m->fp, m->vec, r, slot_off(s, SLOT_OF(m->dst)));
  } else {
    /* An edge's moves are made after the branch that picks the edge, and the
       other edge's after them in the buffer: a constant the first left in the
       reload cache is not there on the second's path, so these neither take
       one from the cache nor leave one in it. */
    s->rc_noconst = true;
    int r = rd(s, m->rv, m->dst);
    s->rc_noconst = false;
    if (r != m->dst) emit_reg_move(out, m->dst, r, m->fp, m->wide, m->vec);
  }
}

/* Rematerialised sources read no location, so they never block a move and
   are never part of a cycle; they go last, after every register and slot
   has been read, which also keeps their scratch use clear of a value the
   cycle breaker has parked. */
static void parallel_copy_to(Isel *s, Buf *out, Move *mv, int n) {
  uint8_t *done = xcalloc(n ? n : 1, 1);
  int remaining = 0;
  for (int i = 0; i < n; i++) {
    if (mv[i].remat || mv[i].src == mv[i].dst) done[i] = 1;
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
      int tmp = isel_break_reg(move_bank(&mv[i]));
      if (mv[i].src == tmp) continue;
      if (move_bank(&mv[i])) frc_kill_reg(s, tmp);
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
  for (int i = 0; i < n; i++)
    if (mv[i].remat) emit_remat_move(s, out, &mv[i]);
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
    mv[n].remat = s->loc[srcv] == LOC_NONE;
    mv[n].rv = srcv;
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

static bool edge_has_moves(Isel *s, Edge *e) {
  Block *t = &s->f->blocks[e->target];
  if (t->nparams == 0) return false;
  Move *mv = xmalloc(t->nparams * sizeof(Move));
  int n = edge_moves(s, e, mv);
  xfree(mv);
  return n != 0;
}

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
      int ci = isel_cs_index(l);
      if (ci < 0)
        warmc_fatal("internal: a reference live across a safepoint in %s is in "
                    "register %d, which a callee does not preserve",
                    f->m->funcs[f->func_index].sym, l);
      entry = GCROOT_REG((uint32_t)ci);
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

__attribute__((unused))
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

__attribute__((unused))
static int low_run(uint64_t m, int width) {
  if (m == 0) return 0;
  int w = 0;
  while (w < 64 && (m >> w) != 0) w++;
  uint64_t full = (w >= 64) ? ~0ull : ((1ull << w) - 1);
  if (m != full || w >= width) return 0;
  return w;
}

__attribute__((unused))
static bool const_arg(IrFunc *f, Ref v, uint64_t *out) {
  if (f->insts[v].op != I_CONST) return false;
  *out = f->insts[v].imm;
  return true;
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

/* The inline memory.copy/fill path (emit_small_memop) handles memory 0 when
   it has 32-bit addresses, and it reads the memory base, so a function that
   uses it needs one set up even if it has no other memory access. */
static bool small_memop_ok(Isel *s, const Inst *in, bool fill) {
  IrFunc *f = s->f;
  if (s->no_small_memop) return false;
  if (in->imm != 0 || (!fill && in->imm2 != 0)) return false;
  return f->m->nmemories && !f->m->memories[0].index64;
}

static void analyse_function(Isel *s) {
  IrFunc *f = s->f;
  s->has_call = false;
  s->call_other = false;
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
            s->has_call = s->call_other = true;
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
        case I_MEM_COPY: case I_MEM_FILL:
          if (small_memop_ok(s, in, in->op == I_MEM_FILL)) s->needs_mem = true;
          s->has_call = s->call_other = true;
          touches_instance = true;
          break;
        case I_MEM_GROW: case I_MEM_INIT:
        case I_DATA_DROP: case I_TABLE_GET: case I_TABLE_SET:
        case I_TABLE_GROW: case I_TABLE_FILL: case I_TABLE_COPY:
        case I_TABLE_INIT: case I_ELEM_DROP:
        case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
        case I_ARRAY_OP: case I_REF_TEST:
          s->has_call = s->call_other = true;
          touches_instance = true;
          break;
        case I_TABLE_SIZE:

          if (in->imm != 0) s->has_call = s->call_other = true;
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

          s->has_call = s->call_other = true;
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

static uint32_t module_max_results(Module *m) {
  uint32_t worst = 1;
  for (uint32_t i = 0; i < m->ntypes; i++)
    if (m->types[i].nresults > worst) worst = m->types[i].nresults;
  return worst;
}

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
