/* The target-neutral half of code generation: the split/far-branch retry
   driver around produce_body, and the merged-body cache that commits
   finished functions into the object.  Everything arch-specific arrives
   through g_target. */

#include "ir.h"
#include "target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN_BYTES 64u
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
  bool dbg = warmc_getenv("WARMC_SPLIT_DEBUG") != NULL;
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

  /* An exit merge goes with its cut.  One param can serve two cuts --
     the exit of one loop the header of the next -- so the survivors set
     their veto bits again after the dropped ones cleared theirs. */
  w = 0;
  for (uint32_t k = 0; k < sc->nxcut; k++) {
    bool keep = false;
    for (uint32_t c = 0; c < sc->ncut && !keep; c++)
      keep = sc->cutp[c] == sc->xcut_hdr[k];
    if (!keep) {
      if (sc->xcutp[k] < sc->veto_n) sc->veto[sc->xcutp[k]] = 0;
      continue;
    }
    sc->xcutp[w] = sc->xcutp[k];
    sc->xcut_hdr[w++] = sc->xcut_hdr[k];
  }
  sc->nxcut = w;
  for (uint32_t c = 0; c < sc->ncut; c++)
    if (sc->cutp[c] < sc->veto_n) sc->veto[sc->cutp[c]] = 1;
  for (uint32_t k = 0; k < sc->nxcut; k++)
    if (sc->xcutp[k] < sc->veto_n) sc->veto[sc->xcutp[k]] = 1;
  if (sc->veto && sc->veto_n > f->ninsts)
    memset(sc->veto + f->ninsts, 0, sc->veto_n - f->ninsts);
  if (sc->inv_fail) memset(sc->inv_fail, 0, sc->cap_add);
  if (sc->cut_fail) memset(sc->cut_fail, 0, sc->cap_cut);
  sc->nfailed_new = 0;
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
bool codegen_rtest_slots(const Module *m, uint64_t imm, uint64_t id,
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

/* In a module no other module's object can reach (Module.gc_sealed) a slot
   answers every ref.test of the module's own concrete types: each whose
   passing slots are one run -- most are one type, a class without
   subclasses -- becomes an I_REF_SLOT, which instruction selection decides
   inline, and which, making no call, leaves a function that only tests and
   casts a leaf.  A test whose passing slots have gaps, as structurally
   equal types numbered apart give, keeps the runtime call for the slots
   outside its first run. */
static Ref unconverted(const IrFunc *f, Ref v) {
  while (f->insts[v].op == I_REF_CONVERT) v = ir_args((IrFunc *)f, v)[0];
  return v;
}

/* Whether u checks the object x, under whatever casts of it, for null: a
   GC access of it, or a ref.as_non_null.  An access checks wherever the
   object can be null; where it does not, x is not null there, nor so at a
   test of it before with nothing between, which then never traps. */
static bool null_checks(const IrFunc *f, Ref u, Ref x) {
  switch (f->insts[u].op) {
    case I_FIELD_GET: case I_FIELD_SET: case I_ARRAY_GET: case I_ARRAY_SET:
    case I_ARRAY_LEN: case I_TRAP_IF:
      return unconverted(f, ir_args((IrFunc *)f, u)[0]) == x;
    default:
      return false;
  }
}

static void seal_tests(IrFunc *f) {
  if (!f->m->gc_sealed) return;
  for (Ref v = 0; v < f->ninsts; v++) {
    Inst *in = &f->insts[v];
    if (in->op != I_REF_TEST) continue;
    if ((RTEST_KIND(in->imm) & ~RTK_NULLABLE) != RTK_CONCRETE_ANY) continue;
    uint32_t lo, hi, nslots;
    bool exact;
    if (codegen_rtest_slots(f->m, in->imm, in->imm2, &lo, &hi, &exact, &nslots) && exact) {
      in->op = I_REF_SLOT;
      g_stats.slot_tests++;
    }
  }

  /* A test that null passes, of an object that an access then checks for
     null -- a nullable cast of an element of a Java array and a read of a
     field of it -- traps on null as the access would have, and the access
     checks nothing.  Between the two, in one block, runs nothing that has an
     effect or can trap but a trap on the test's answer, which null passes;
     so nothing sees the trap come earlier. */
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op != I_REF_SLOT || !(RTEST_KIND(in->imm) & RTK_NULLABLE)) continue;
      Ref x = unconverted(f, ir_args(f, v)[0]);
      for (uint32_t k = i + 1; k < b->ninsts; k++) {
        Ref u = b->insts[k];
        Inst *ui = &f->insts[u];
        if (null_checks(f, u, x)) {
          in->imm = (in->imm & ~(uint64_t)RTK_NULLABLE) | RTK_NULLTRAP;
          g_stats.slot_nullchks++;
          if (ui->op == I_TRAP_IF) {
            ui->op = I_NOP;
            ui->nargs = 0;
          } else {
            ui->imm2 &= ~(uint64_t)GCF_NULLCHK;
          }
          break;
        }
        bool on_test = ui->op == I_TRAP_IF && ir_args(f, u)[0] == v;
        if (!on_test && !ir_has_no_effect(ui)) break;
      }
    }
  }
}

/* One produce_body, run a second time when the first has measured the loops
   it aligns and wants to place them again knowing their sizes (4).  The
   sizes are for that code only, so they go once the second run has used
   them. */
static int produce_once(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                        uint32_t out_bytes, FuncOut *fout, bool far,
                        SplitCtl *sc, bool collect) {
  int r = g_target->produce_body(f, obj, opt, out_bytes, fout, far, sc, collect);
  if (r == 4)
    r = g_target->produce_body(f, obj, opt, out_bytes, fout, far, sc, collect);
  f->loop_bytes = NULL;
  if (f->ra_keep) {
    f->ra_keep_drop(f->ra_keep);
    f->ra_keep = NULL;
  }
  return r;
}

void ir_produce_function(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                         uint32_t out_bytes, FuncOut *fout) {
  SplitCtl sc;
  memset(&sc, 0, sizeof sc);

  /* Loop-boundary live-range splitting.  The whole plan is opt-in
     (WARMC_SPLIT): the prune loop below rolls every failed grant out of
     the IR, so enabling it never loses a race any more (the old chunks
     +4.9% and the ktbench drift are gone); what remains is one reproduced
     winner (asbench mapint -3%) for +8-9% compile time on granted modules
     -- 71% of grants fail the retry and cost their extra regalloc rounds
     anyway.  Not a trade to make by default.  The measured record lives
     with the warmc-split-series memory note.

     Its cut shape, with the exit merges that make a cut stick, runs by
     default (-fsplit) in loops that make no call: that is the spilled
     accumulator of a callee inlined ahead of a register-hungry one, 20%
     of rsbench/itadapt, for 2% more compile time on the largest modules.
     The one row it costs, cppbench/algorithms at 2%, runs fewer
     instructions: an insertion sort's previous index left a stack slot
     for a register and its loads now issue early. */
  if (opt->regalloc) ir_compact(f);
  seal_tests(f);
  sc.full = warmc_getenv("WARMC_SPLIT") != NULL;
  bool collect = opt->regalloc && (opt->split || sc.full);
  int r = produce_once(f, obj, opt, out_bytes, fout, false, &sc, collect);
  if (r == 2) {
    split_apply_adds(f, &sc);
    r = produce_once(f, obj, opt, out_bytes, fout, false, &sc, false);
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
    r = produce_once(f, obj, opt, out_bytes, fout, false, &sc, false);
  }
  if (r == 0) {
    sc.accept = true;
    r = produce_once(f, obj, opt, out_bytes, fout, true, &sc, false);
  }
  xfree(sc.veto); xfree(sc.addv); xfree(sc.addhdr); xfree(sc.addp);
  xfree(sc.cutp); xfree(sc.inv_fail); xfree(sc.cut_fail);
  xfree(sc.xcutp); xfree(sc.xcut_hdr);
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

static uint64_t g_dead_stub_len;

static uint64_t dead_stub(ObjBuilder *obj) {
  if (g_dead_stub != UINT64_MAX) return g_dead_stub;
  g_dead_stub = obj->text.len;
  uint32_t sym = obj_intern_sym(obj, "warmc_unreached", SYM_UNDEF_FUNC, -1, 0);
  g_target->emit_dead_call(obj, sym);
  g_dead_stub_len = obj->text.len - g_dead_stub;
  return g_dead_stub;
}

void codegen_drop_function(ObjBuilder *obj, FuncOut *fo) {
  uint64_t at = dead_stub(obj);
  obj_intern_sym(obj, fo->sym, SYM_LOCAL_FUNC, 0, at);
  uint32_t si = obj_find_sym(obj, fo->sym);
  obj->syms[si].value = at;
  obj->syms[si].size = g_dead_stub_len;
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
  /* WARMC_LAYOUT_SEED=s: up to 60 bytes of nops, as s and the function's
     name pick, ahead of each function that aligns no loop, which moves the
     code without changing it -- for timing a change over several layouts,
     a change of a few percent being less than what layout alone moves a
     kernel by.  Measurement only: off unless set. */
  {
    const char *ls = warmc_getenv("WARMC_LAYOUT_SEED");
    if (ls && !fo->want_align) {
      uint64_t h = 1469598103934665603ull ^ strtoull(ls, NULL, 0);
      for (const char *c = fo->sym; c && *c; c++) h = (h ^ (uint8_t)*c) * 1099511628211ull;
      uint32_t pad = (uint32_t)(h >> 59) * 4;
      for (uint64_t end = obj->text.len + pad; obj->text.len < end;)
        g_target->pad_nop(&obj->text);
    }
  }
  if (fo->want_align)
    while (obj->text.len & (ALIGN_BYTES - 1)) g_target->pad_nop(&obj->text);
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
