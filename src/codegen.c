/* The target-neutral half of code generation: the split/far-branch retry
   driver around produce_body, and the merged-body cache that commits
   finished functions into the object.  Everything arch-specific arrives
   through g_target. */

#include "ir.h"
#include "target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

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
            ir_make_nop(ui);
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
/* The copies loop_consts makes, by header and constant: an open-addressed
   table, never more than half full. */
typedef struct { uint64_t *key; Ref *val; uint32_t cap, n; } LcMap;

static uint32_t lc_slot(const LcMap *m, uint64_t k) {
  uint32_t i = (uint32_t)(k * 0x9e3779b97f4a7c15ull >> 40) & (m->cap - 1);
  while (m->key[i] && m->key[i] != k) i = (i + 1) & (m->cap - 1);
  return i;
}

/* the copy of constant c over the loop headed by h, made the first time */
static Ref lc_copy(IrFunc *f, LcMap *m, uint32_t h, Ref c) {
  if (2 * (m->n + 1) > m->cap) {
    LcMap o = *m;
    m->cap = o.cap ? 2 * o.cap : 64;
    m->key = xcalloc(m->cap, sizeof(uint64_t));
    m->val = xmalloc(m->cap * sizeof(Ref));
    for (uint32_t t = 0; t < o.cap; t++)
      if (o.key[t]) {
        uint32_t i = lc_slot(m, o.key[t]);
        m->key[i] = o.key[t];
        m->val[i] = o.val[t];
      }
    xfree(o.key); xfree(o.val);
  }
  uint64_t k = ((uint64_t)h << 32 | c) + 1;
  uint32_t i = lc_slot(m, k);
  if (!m->key[i]) {
    Inst c0 = f->insts[c];
    m->key[i] = k;
    m->val[i] = ir_emit(f, f->blocks[h].idom, I_CONST, (ValType)c0.type, NULL, 0, c0.imm, c0.imm2);
    m->n++;
  }
  return m->val[i];
}

static bool lc_wants(IrFunc *f, Ref c) {
  const Inst *ci = &f->insts[c];
  return ci->op == I_CONST && (ci->type == VT_I32 || ci->type == VT_I64) &&
         ir_const_cost(ci->imm, ci->type == VT_I64) >= 2;
}

/* On arm64, a constant two or more instructions make that a loop reads is
   read there from a copy made in the block over the loop, its header's
   immediate dominator: made once a run of the loop, the copy lives only
   where the loop does and gets a register where the loop has one free.
   The constant itself, defined at the entry and read all over a function
   too big for one register to hold it from end to end, is made again at
   each read: zigbench/wordfreq's tokenizer built the text's base, mov and
   movk, for every byte it scanned.  A copy no register takes is made
   again at its reads as the constant was.  A read in a cold block keeps
   the constant (WARMC_LOOP_CONSTS_COLD copies it too). */
static void loop_consts(IrFunc *f) {
  if (g_target != &warmc_target_a64 || warmc_getenv("WARMC_NO_LOOP_CONSTS")) return;
  uint32_t nb = f->nblocks;
  uint32_t *inner = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t *stk = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t *seen = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  for (uint32_t i = 0; i < nb; i++) inner[i] = UINT32_MAX;
  bool any = false;
  /* each block's innermost loop, by its header: the deepest of the natural
     loops, found back from their latches, that hold it -- each dominated by
     its header, so by the header's immediate dominator too */
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t h = f->order[oi];
    Block *hb = &f->blocks[h];
    uint32_t sp = 0;
    bool loop = false, natural = true;
    seen[h] = h + 1;
    for (uint32_t p = 0; p < hb->npreds; p++) {
      uint32_t l = hb->preds[p];
      if (!f->blocks[l].reachable || f->blocks[l].rpo < hb->rpo) continue;
      loop = true;
      natural &= ir_dominates_checked(f, h, l);
      if (seen[l] != h + 1) { seen[l] = h + 1; stk[sp++] = l; }
    }
    /* an irreducible region's entry does not dominate the blocks round it,
       nor does what dominates it (cbench/lexer's comment states) */
    if (!loop || !natural) continue;
    any = true;
    for (stk[sp++] = h; sp;) {
      uint32_t x = stk[--sp];
      if (inner[x] == UINT32_MAX || f->blocks[inner[x]].loop_depth < hb->loop_depth) inner[x] = h;
      if (x == h) continue;
      Block *xb = &f->blocks[x];
      for (uint32_t p = 0; p < xb->npreds; p++) {
        uint32_t q = xb->preds[p];
        if (seen[q] != h + 1 && f->blocks[q].reachable) { seen[q] = h + 1; stk[sp++] = q; }
      }
    }
  }
  xfree(stk); xfree(seen);
  LcMap m = { NULL, NULL, 0, 0 };
  for (uint32_t oi = 0; any && oi < f->norder; oi++) {
    uint32_t x = f->order[oi], h = inner[x];
    if (h == UINT32_MAX) continue;
    /* a read on a rare way keeps its constant, made there: a copy over the
       loop for a slow path's call -- luawasm's key hashes and cache
       addresses -- lived through the loop, and was made again every time
       round where the slow path's calls left it no register */
    if (f->blocks[x].cold && !warmc_getenv("WARMC_LOOP_CONSTS_COLD")) continue;
    uint32_t d = f->blocks[h].idom;
    if (d == UINT32_MAX || d == h) continue;
    for (uint32_t k = 0; k < f->blocks[x].ninsts; k++) {
      Ref u = f->blocks[x].insts[k];
      for (uint32_t j = 0; j < f->insts[u].nargs; j++) {
        Ref c = ir_args(f, u)[j];
        if (lc_wants(f, c)) {
          Ref cp = lc_copy(f, &m, h, c);
          ir_args(f, u)[j] = cp;
        }
      }
    }
    for (uint32_t e = 0; e < f->blocks[x].nsuccs; e++)
      for (uint32_t j = 0; j < f->blocks[x].succs[e].nargs; j++) {
        Ref c = f->blocks[x].succs[e].args[j];
        if (lc_wants(f, c)) {
          Ref cp = lc_copy(f, &m, h, c);
          f->blocks[x].succs[e].args[j] = cp;
        }
      }
  }
  g_stats.loop_consts += m.n;
  xfree(m.key); xfree(m.val); xfree(inner);
}

static int produce_once(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                        uint32_t out_bytes, FuncOut *fout, bool far,
                        SplitCtl *sc, bool collect) {
  int r = g_target->produce_body(f, obj, opt, out_bytes, fout, far, sc, collect);
  /* again with the loops measured, and once more where a test-bit branch
     that reached before the loops moved no longer does */
  for (int k = 0; r == 4 && k < 2; k++)
    r = g_target->produce_body(f, obj, opt, out_bytes, fout, far, sc, collect);
  f->loop_bytes = NULL;
  f->loop_phase = 0;
  f->no_tbz = false;
  if (f->ra_keep) {
    f->ra_keep_drop(f->ra_keep);
    f->ra_keep = NULL;
  }
  return r;
}

static void produce_whole(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
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
  if (opt->regalloc) loop_consts(f);
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

/* ---- shrink-wrapping ------------------------------------------------------

   A function that calls saves the callee-saved registers it uses, makes its
   frame record and checks the stack on every call, also on a way that
   calls nothing: TeaVM's ArrayList.ensureCapacity returned, nearly every
   time, after three loads and a compare, and saved six registers and x29
   and x30 for it.  So such a function may be emitted twice: its start, the
   blocks on the ways from its entry to a return of at most 24 instructions
   with no call, loop or allocation, as a leaf of its own at the function's
   symbol; and the whole function, as it is, after it at "<sym>$full".  Not
   a function that already returns early ahead of its prologue (isel.c
   early_ret_shape), to which this adds only a jump, nor one whose start
   runs more again, by the longest way to a jump, than the way back saves:
   without a profile both ways count alike, so a recursion's base case does
   not pay for its other calls testing twice.  Each way out of the start
   into the rest is a jump to the whole function with the arguments as they
   came (RCALL_FULL), which runs the start again: so on its way to such a jump
   the start may read and trap -- a trap there is the one the whole function
   makes first -- but not write, and the blocks that only lead to a return
   may store.  The start keeps only where it has no frame, no saved register
   and no safepoint, and the whole function makes a frame; it is never
   below another frame on the stack, so the runtime needs no record of it,
   and the whole function's is found by its own symbol.  On arm64, in a
   module whose functions take no check on the way in from another instance
   (codegen_entry_off), and not for a later tier's patchable entry.
   WARMC_NO_SHRINK_WRAP=1 turns it off; WARMC_SHRINK_WRAP_MAX is the most
   instructions a start keeps (24); WARMC_SW_DEBUG=1 says what was taken and
   why the rest was not. */

static bool sw_bad_op(uint16_t op) {
  switch (op) {
    case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
    case I_RETURN_CALL: case I_RETURN_CALL_INDIRECT: case I_RETURN_CALL_REF:
    case I_CONT_OP: case I_THROW: case I_THROW_REF:
    case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG: case I_ARRAY_OP:
    case I_MEM_GROW: case I_MEM_COPY: case I_MEM_FILL: case I_MEM_INIT:
    case I_DATA_DROP: case I_TABLE_GET: case I_TABLE_SET: case I_TABLE_GROW:
    case I_TABLE_FILL: case I_TABLE_COPY: case I_TABLE_INIT: case I_ELEM_DROP:
    case I_TABLE_SIZE: case I_ANY_OF_EXTERN: case I_EXTERN_OF_ANY:
      return true;
    default:
      return false;
  }
}

/* whether running it again, with nothing written in between, does nothing
   the first run did not: it writes nothing */
static bool sw_rerun_ok(const Inst *in) {
  if (ir_has_no_effect(in)) return true;
  switch (in->op) {
    case I_FIELD_GET: case I_ARRAY_GET: case I_ARRAY_LEN: case I_I31_GET:
    case I_DIVS: case I_DIVU: case I_REMS: case I_REMU:
    case I_TRUNCS: case I_TRUNCU:
    case I_LOAD: case I_MEM_SIZE: case I_REF_SLOT:
    /* the collector's bad mask, which a load barrier tests a ref against */
    case I_GC_BADMASK:
    /* a check, which traps as the whole function's would first */
    case I_TRAP_IF:
      return true;
    default:
      return false;
  }
}

/* What may stand between a write sw_sink_to moves and where it goes: no
   effect, no read of memory -- that would see the write not yet made. */
static bool sw_sink_past(const Inst *in) {
  switch (in->op) {
    case I_FIELD_GET: case I_ARRAY_GET: case I_ARRAY_LEN: case I_LOAD: case I_GLOBAL_GET:
    case I_MEM_SIZE: case I_TABLE_GET: case I_TABLE_SIZE:
      return false;
    default:
      return ir_has_no_effect(in);
  }
}

/* A write the start makes on its way to a cut, which the whole would make
   again: where it can go instead -- past the rest of its block and the
   blocks after it that only it enters (in the start), all of no effect, to
   the side of the branch that ends them which goes on to no cut, that
   side's start then: permute's self.count = self.count + 1, then n ~= 0's
   return.  REF_NONE where there is no such place. */
static uint32_t sw_sink_to(IrFunc *f, const uint8_t *keep, const uint8_t *cut, const uint8_t *mcut,
                           uint32_t bi, uint32_t at) {
  const Inst *w = &f->insts[f->blocks[bi].insts[at]];
  if (w->op != I_FIELD_SET && w->op != I_GLOBAL_SET && w->op != I_STORE && w->op != I_ARRAY_SET) return REF_NONE;
  if (warmc_getenv("WARMC_NO_SW_SINK")) return REF_NONE;
  uint32_t b = bi, from = at + 1;
  for (int hop = 0; hop < 8; hop++) {
    const Block *B = &f->blocks[b];
    for (uint32_t i = from; i < B->ninsts; i++)
      if (!sw_sink_past(&f->insts[B->insts[i]])) return REF_NONE;
    uint32_t next = REF_NONE;
    if (B->term == T_JUMP && B->nsuccs == 1) {
      next = B->succs[0].target;
      if (!keep[next] || cut[next] == 0) {
        /* (a jump to the way out itself) */
        if (!keep[next]) return REF_NONE;
      }
    } else if (B->term == T_BRIF && B->nsuccs == 2) {
      /* one side surely goes on to the whole, which writes again; the other
         surely returns, the write moved there */
      uint32_t s0 = B->succs[0].target, s1 = B->succs[1].target;
      bool on0 = !keep[s0] || mcut[s0], on1 = !keep[s1] || mcut[s1];
      bool back0 = keep[s0] && !cut[s0], back1 = keep[s1] && !cut[s1];
      if (on0 && back1) next = s1;
      else if (on1 && back0) next = s0;
      else return REF_NONE;
    } else {
      return REF_NONE;
    }
    /* the place: entered only from here within the start */
    uint32_t kp = 0;
    for (uint32_t p = 0; p < f->blocks[next].npreds; p++)
      if (keep[f->blocks[next].preds[p]]) kp++;
    if (kp != 1) return REF_NONE;
    if (B->term == T_BRIF || !cut[next]) return next;
    b = next;
    from = 0;
  }
  return REF_NONE;
}

static uint32_t sw_max(void) {
  static _Atomic int v = -1;
  int x = atomic_load_explicit(&v, memory_order_relaxed);
  if (x < 0) {
    const char *e = warmc_getenv("WARMC_SHRINK_WRAP_MAX");
    x = e ? atoi(e) : 24;
    if (warmc_getenv("WARMC_NO_SHRINK_WRAP")) x = 0;
    atomic_store_explicit(&v, x, memory_order_relaxed);
  }
  return (uint32_t)x;
}

/* The start of f, as a function of its own whose ways into the rest jump to
   the whole; NULL where f has none worth one. */
static IrFunc *sw_start(IrFunc *f, const OptOpts *opt, uint32_t *dup_out) {
  Module *m = f->m;
  uint32_t lim = sw_max();
  if (!lim || g_target->is_x64 || !opt->regalloc || opt->patchable || opt->profile ||
      codegen_entry_off(m, f->func_index) || f->sig->nresults > 1)
    return NULL;
  uint32_t ints = 0, flts = 0;
  for (uint32_t i = 0; i < f->sig->nparams; i++) {
    ValType t = f->sig->params[i];
    if (vt_is_float(t) || t == VT_V128) flts++;
    else ints++;
  }
  if (ints > 6 || flts > 8) return NULL;
  ir_order(f);
  uint32_t nb = f->nblocks;
  bool calls = false;
  uint8_t *bad = xcalloc(nb ? nb : 1, 1), *in_g = xcalloc(nb ? nb : 1, 1);
  uint8_t *keep = xcalloc(nb ? nb : 1, 1), *cut = xcalloc(nb ? nb : 1, 1);
  /* a block every way from which goes on to the whole (sw_sink_to) */
  uint8_t *mcut = xcalloc(nb ? nb : 1, 1);
  uint32_t *sz = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  uint32_t *din = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  uint32_t *dout = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  /* per instruction: the block a write the start moves goes to, or REF_NONE */
  uint32_t *sink = xmalloc((f->ninsts ? f->ninsts : 1) * sizeof(uint32_t));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (b->loop_depth || b->is_pad) bad[bi] = 1;
    for (uint32_t s = 0; s < b->nsuccs; s++)
      if (b->succs[s].eh) bad[bi] = 1;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      uint16_t op = f->insts[b->insts[i]].op;
      if (sw_bad_op(op)) {
        bad[bi] = 1;
        if (op != I_THROW && op != I_THROW_REF) calls = true;
      }
    }
  }
  IrFunc *st = NULL;
  uint32_t n = 0, ncut = 0;
  bool any_ret = false;
  const char *why = "no call";
  if (!calls) goto done;
  why = "entry calls or loops";
  if (bad[f->entry]) goto done;
  /* the start, made smaller while a block on the way to a jump writes */
  for (uint32_t round = 0; round <= f->norder; round++) {
    if (bad[f->entry]) goto done;
    memset(in_g, 0, nb);
    in_g[f->entry] = 1;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      if (!in_g[bi]) continue;
      const Block *b = &f->blocks[bi];
      for (uint32_t s = 0; s < b->nsuccs; s++)
        if (!bad[b->succs[s].target]) in_g[b->succs[s].target] = 1;
    }
    /* the blocks on a way from the entry to a return or a trap of at most
       lim instructions: the short ways out, not every block that reaches a
       return without calling (a set closed under dominators: a block's
       dominator is on every way through it) */
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      uint32_t k = 0;
      for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++) {
        uint16_t op = f->insts[f->blocks[bi].insts[i]].op;
        if (op != I_CONST && op != I_PARAM && op != I_NOP) k++;
      }
      sz[bi] = k;
      din[bi] = dout[bi] = UINT32_MAX / 4;
    }
    for (uint32_t oi = f->norder; oi-- > 0;) {
      uint32_t bi = f->order[oi];
      if (!in_g[bi]) continue;
      const Block *b = &f->blocks[bi];
      uint32_t best = b->term == T_RETURN || b->term == T_TRAP ? 0 : UINT32_MAX / 4;
      for (uint32_t s = 0; s < b->nsuccs; s++)
        if (in_g[b->succs[s].target] && dout[b->succs[s].target] < best)
          best = dout[b->succs[s].target];
      dout[bi] = sz[bi] + best;
    }
    din[f->entry] = 0;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      if (!in_g[bi] || din[bi] >= UINT32_MAX / 4) continue;
      const Block *b = &f->blocks[bi];
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        uint32_t t = b->succs[s].target;
        if (in_g[t] && din[bi] + sz[bi] < din[t]) din[t] = din[bi] + sz[bi];
      }
    }
    memset(keep, 0, nb);
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      if (in_g[bi] && din[bi] + dout[bi] <= lim) keep[bi] = 1;
    }
    why = "no short way out";
    if (!keep[f->entry]) goto done;
    memset(cut, 0, nb);
    memset(mcut, 0, nb);
    for (uint32_t i = 0; i < f->ninsts; i++) sink[i] = REF_NONE;
    bool again = false;
    n = ncut = 0;
    any_ret = false;
    for (uint32_t oi = f->norder; oi-- > 0;) {
      uint32_t bi = f->order[oi];
      if (!keep[bi]) continue;
      const Block *b = &f->blocks[bi];
      if (b->term == T_RETURN) any_ret = true;
      bool all_on = b->nsuccs > 0 && b->term != T_RETURN && b->term != T_TRAP;
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        uint32_t t = b->succs[s].target;
        if (!keep[t]) { cut[bi] = 1; ncut++; }
        else if (cut[t]) cut[bi] = 1;
        if (keep[t] && !mcut[t]) all_on = false;
      }
      mcut[bi] = all_on;
      for (uint32_t i = 0; i < b->ninsts; i++) {
        const Inst *in = &f->insts[b->insts[i]];
        if (in->op != I_CONST && in->op != I_PARAM && in->op != I_NOP) n++;
        if (cut[bi] && !sw_rerun_ok(in)) {
          uint32_t to = sw_sink_to(f, keep, cut, mcut, bi, i);
          if (to != REF_NONE) {
            sink[b->insts[i]] = to;
            continue;
          }
          if (warmc_getenv("WARMC_SW_DEBUG"))
            fprintf(stderr, "sw %s: b%u writes (%s) on its way on\n",
                    m->funcs[f->func_index].name ? m->funcs[f->func_index].name : m->funcs[f->func_index].sym,
                    bi, ir_op_name(in->op));
          bad[bi] = 1;
          again = true;
        }
      }
    }
    if (!again) break;
  }
  why = !ncut ? "nothing cut" : !any_ret ? "no return" : "too big";
  if (!ncut || !any_ret || n > lim) goto done;
  why = NULL;
  /* what a way on runs twice: the most machine instructions before a cut,
     as many as each IR instruction takes about -- a GC access its null test
     and its barrier's, a type test or cast the type's range */
  uint32_t dup = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    uint32_t w = 0;
    for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++) {
      Ref u = f->blocks[bi].insts[i];
      switch (f->insts[u].op) {
        case I_CONST: case I_PARAM: case I_NOP: break;
        case I_FIELD_GET: {
          /* the load, the barrier's test of a ref, a nullable base's test */
          ValType bt = (ValType)f->insts[ir_resolve(f, ir_args(f, u)[0])].type;
          w += 1 + (vt_is_ref((ValType)f->insts[u].type) ? 2 : 0) + (!vt_is_ref(bt) || vt_nullable(bt) ? 1 : 0);
          break;
        }
        case I_ARRAY_GET: w += 4; break;
        case I_REF_TEST: case I_REF_CONVERT: w += 6; break;
        default: w += 1; break;
      }
    }
    sz[bi] = w;
    din[bi] = 0;
  }
  /* by the longest way there: the shortest may go a way the test before
     rules out (a null test's other side) -- but not a way a hint calls rare,
     which is no way the function usually goes on by (luawasm's typed slot
     store that finds the slot's sentinel) */
  bool nocold = !warmc_getenv("WARMC_SW_COLDDUP");
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    if (!keep[bi]) continue;
    const Block *b = &f->blocks[bi];
    if (nocold && b->cold && bi != f->entry) continue;
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      if (nocold && b->succs[s].cold) continue;
      uint32_t t = b->succs[s].target;
      if (!keep[t]) { if (din[bi] + sz[bi] > dup) dup = din[bi] + sz[bi]; }
      else if (din[bi] + sz[bi] > din[t]) din[t] = din[bi] + sz[bi];
    }
  }
  *dup_out = dup;

  {
    if (warmc_getenv("WARMC_SW_DEBUG"))
      fprintf(stderr, "sw %s: start of %u instructions, %u ways on\n",
              m->funcs[f->func_index].name ? m->funcs[f->func_index].name : m->funcs[f->func_index].sym,
              n, ncut);
    st = ir_new(m, f->func_index);
    uint32_t *bmap = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
    Ref *vmap = xmalloc((f->ninsts ? f->ninsts : 1) * sizeof(Ref));
    for (uint32_t i = 0; i < nb; i++) bmap[i] = UINT32_MAX;
    for (uint32_t i = 0; i < f->ninsts; i++) vmap[i] = REF_NONE;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      if (!keep[bi]) continue;
      uint32_t nbk = ir_new_block(st);
      bmap[bi] = nbk;
      Block *k = &st->blocks[nbk];
      k->sealed = k->filled = k->reachable = true;
      k->cold = f->blocks[bi].cold;
      for (uint32_t p = 0; p < f->blocks[bi].nparams; p++) {
        Ref src = f->blocks[bi].params[p];
        vmap[src] = ir_add_param(st, nbk, (ValType)f->insts[src].type);
      }
    }
    st->entry = bmap[f->entry];
    uint32_t sb = ir_new_block(st);
    st->blocks[sb].sealed = st->blocks[sb].filled = st->blocks[sb].reachable = true;
    Ref *tmp = NULL;
    uint32_t tcap = 0;
    bool ok = true;
    for (uint32_t oi = 0; oi < f->norder && ok; oi++) {
      uint32_t bi = f->order[oi];
      if (!keep[bi]) continue;
      const Block *b = &f->blocks[bi];
      /* the writes moved here first, then the block's own but those moved
         on (sw_sink_to) */
      uint32_t nsunk = 0;
      for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t cnt = pass == 0 ? f->ninsts : b->ninsts;
        for (uint32_t i = 0; i < cnt && ok; i++) {
        Ref src = pass == 0 ? i : b->insts[i];
        if (pass == 0 ? sink[src] != bi : sink[src] != REF_NONE) continue;
        if (pass == 0) nsunk++;
        const Inst *si = &f->insts[src];
        if (si->op == I_PARAM) continue;
        if (si->nargs > tcap) { tcap = si->nargs * 2 + 4; tmp = xrealloc(tmp, tcap * sizeof(Ref)); }
        const Ref *a = ir_args(f, src);
        for (uint32_t j = 0; j < si->nargs && ok; j++) {
          tmp[j] = vmap[ir_resolve(f, a[j])];
          if (tmp[j] == REF_NONE) ok = false;
        }
        if (!ok) break;
        Ref nv = ir_emit(st, bmap[bi], (IrOp)si->op, (ValType)si->type, tmp, si->nargs,
                         si->imm, si->imm2);
        st->insts[nv].flags = si->flags;
        vmap[src] = nv;
        }
      }
      if (nsunk && warmc_getenv("WARMC_SW_DEBUG"))
        fprintf(stderr, "sw %s: %u write(s) moved to b%u\n",
                m->funcs[f->func_index].name ? m->funcs[f->func_index].name : m->funcs[f->func_index].sym, nsunk, bi);
    }
    for (uint32_t oi = 0; oi < f->norder && ok; oi++) {
      uint32_t bi = f->order[oi];
      if (!keep[bi]) continue;
      const Block *b = &f->blocks[bi];
      uint32_t kb = bmap[bi];
      switch (b->term) {
        case T_RETURN: {
          Ref *rv = ir_alloc(st, (b->nret ? b->nret : 1) * sizeof(Ref));
          for (uint32_t j = 0; j < b->nret && ok; j++) {
            rv[j] = vmap[ir_resolve(f, b->ret_args[j])];
            if (rv[j] == REF_NONE) ok = false;
          }
          Block *k = &st->blocks[kb];
          k->term = T_RETURN;
          k->nret = k->cap_ret = b->nret;
          k->ret_args = rv;
          break;
        }
        case T_TRAP:
          st->blocks[kb].term = T_TRAP;
          st->blocks[kb].term_imm = b->term_imm;
          break;
        case T_JUMP: case T_BRIF: case T_BRTABLE:
          st->blocks[kb].term = b->term;
          st->blocks[kb].term_imm = b->term_imm;
          if (b->term != T_JUMP) {
            st->blocks[kb].term_arg = vmap[ir_resolve(f, b->term_arg)];
            if (st->blocks[kb].term_arg == REF_NONE) ok = false;
          }
          for (uint32_t s = 0; s < b->nsuccs && ok; s++) {
            const Edge *e = &b->succs[s];
            if (!keep[e->target]) {
              ir_add_edge(st, kb, sb, NULL, 0);
            } else {
              if (e->nargs > tcap) { tcap = e->nargs * 2 + 4; tmp = xrealloc(tmp, tcap * sizeof(Ref)); }
              for (uint32_t j = 0; j < e->nargs && ok; j++) {
                tmp[j] = vmap[ir_resolve(f, e->args[j])];
                if (tmp[j] == REF_NONE) ok = false;
              }
              ir_add_edge(st, kb, bmap[e->target], tmp, e->nargs);
            }
            st->blocks[kb].succs[st->blocks[kb].nsuccs - 1].cold = e->cold;
          }
          break;
        default:
          ok = false;
          break;
      }
    }
    if (ok) {
      const Block *eb = &st->blocks[st->entry];
      Ref *args = xmalloc((eb->nparams ? eb->nparams : 1) * sizeof(Ref));
      memcpy(args, eb->params, eb->nparams * sizeof(Ref));
      ir_emit(st, sb, I_RETURN_CALL, VT_BOT, args, eb->nparams, f->func_index, RCALL_FULL);
      xfree(args);
      st->blocks[sb].term = T_TRAP;
      st->blocks[sb].term_imm = TRAP_UNREACHABLE;
      ir_order(st);
    } else {
      if (warmc_getenv("WARMC_SW_DEBUG"))
        fprintf(stderr, "sw %s: start not made: a value from outside it\n",
                m->funcs[f->func_index].name ? m->funcs[f->func_index].name : m->funcs[f->func_index].sym);
      ir_free(st);
      st = NULL;
    }
    xfree(tmp); xfree(bmap); xfree(vmap);
  }
done:
  if (why && warmc_getenv("WARMC_SW_DEBUG") && calls)
    fprintf(stderr, "sw %s: none: %s (%u instructions)\n",
            m->funcs[f->func_index].name ? m->funcs[f->func_index].name : m->funcs[f->func_index].sym,
            why, n);
  xfree(bad); xfree(in_g); xfree(keep); xfree(cut); xfree(mcut);
  xfree(sz); xfree(din); xfree(dout); xfree(sink);
  return st;
}

static void fout_free(FuncOut *fo) {
  xfree(fo->uw.sites); xfree(fo->uw.clauses);
  xfree(fo->uw.gcsites); xfree(fo->uw.gcroots);
  xfree(fo->code); xfree(fo->relocs); xfree(fo->refs); xfree(fo->full_br);
  if (fo->sym_owned) xfree((char *)fo->sym);
  memset(fo, 0, sizeof *fo);
}

void ir_produce_function(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                         uint32_t out_bytes, FuncOut *fout) {
  uint32_t dup = 0;
  IrFunc *st = sw_start(f, opt, &dup);
  produce_whole(f, obj, opt, out_bytes, fout);
  if (!st) return;
  /* worth it where the whole function makes a frame, and not again for a
     function whose body itself jumps to a full body */
  if (!(fout->uw.saved & UW_HAS_LR) || fout->nfull_br || fout->early_ret) {
    if (warmc_getenv("WARMC_SW_DEBUG"))
      fprintf(stderr, "sw %s: whole has %s\n", f->m->funcs[f->func_index].name ? f->m->funcs[f->func_index].name : fout->sym,
              !(fout->uw.saved & UW_HAS_LR) ? "no frame record" : fout->nfull_br ? "a jump to a full body" : "an early return");
    ir_free(st);
    return;
  }
  /* What a way on runs again -- the instructions before its cut and the
     jump -- against what the way back saves: the frame record,
     the stack check, a register's save and restore, the frame's.  Without a
     profile the two ways are as likely: ktbench/sealed's eval, nine
     instructions of type tests before a cut and two registers saved, ran
     15% slower, half its calls on each way. */
  {
    uint32_t regs = 0;
    for (uint32_t k = UW_GPR_SHIFT; k < 32; k++) regs += (fout->uw.saved >> k) & 1;
    uint32_t save = 6 + regs + (fout->uw.frame ? 2 : 0);
    if (dup + 1 > save && !warmc_getenv("WARMC_SW_FORCE")) {
      if (warmc_getenv("WARMC_SW_DEBUG"))
        fprintf(stderr, "sw %s: not worth it: %u before a cut, %u saved\n",
                f->m->funcs[f->func_index].name ? f->m->funcs[f->func_index].name : fout->sym, dup, save);
      ir_free(st);
      return;
    }
  }
  FuncOut so;
  memset(&so, 0, sizeof so);
  produce_whole(st, obj, opt, out_bytes, &so);
  ir_free(st);
  if (so.uw.saved || so.uw.frame || so.uw.nsites || so.uw.ngcsites || so.entry_off ||
      !so.nfull_br || so.code_len >= fout->code_len) {
    if (warmc_getenv("WARMC_SW_DEBUG"))
      fprintf(stderr, "sw %s: start refused: saved %#x frame %u sites %u gcsites %u br %u len %u of %u\n",
              f->m->funcs[f->func_index].name ? f->m->funcs[f->func_index].name : fout->sym, so.uw.saved, so.uw.frame, so.uw.nsites, so.uw.ngcsites, so.nfull_br,
              so.code_len, fout->code_len);
    fout_free(&so);
    return;
  }
  if (warmc_getenv("WARMC_SW_DEBUG")) {
    uint32_t regs = 0;
    for (uint32_t k = UW_GPR_SHIFT; k < 32; k++) regs += (fout->uw.saved >> k) & 1;
    const Func *fn = &f->m->funcs[f->func_index];
    fprintf(stderr, "sw %s: taken: start %u bytes, %u before a cut, full saves %u registers, frame %u, full %u bytes\n",
            fn->name ? fn->name : fn->sym, so.code_len, dup, regs, fout->uw.frame, fout->code_len);
  }
  FuncOut *full = xmalloc(sizeof *full);
  *full = *fout;
  char *name = xmalloc(strlen(fout->sym) + 6);
  sprintf(name, "%s$full", fout->sym);
  full->sym = name;
  full->sym_owned = true;
  full->uw.full_body = 1;
  /* what the whole function refers to is what keeps others alive */
  xfree(so.refs);
  so.refs = full->refs; so.nrefs = full->nrefs;
  full->refs = NULL; full->nrefs = 0;
  so.full = full;
  *fout = so;
  g_stats.shrink_wrapped++;
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
  aux_u32(a, fo->entry_off);
  aux_u32(a, fo->phase);
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
  if (fo->full) {
    fout_free(fo->full);
    xfree(fo->full);
    fo->full = NULL;
  }
  xfree(fo->full_br); fo->full_br = NULL;
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

/* One function's code into the text, at its symbol; with merge, onto an
   identical body placed before; with seed, moved by WARMC_LAYOUT_SEED.
   *placed is where its code starts. */
static void commit_one(ObjBuilder *obj, FuncOut *fo, bool merge, bool seed,
                       uint64_t *placed) {
  aux_build(&g_aux, fo);
  uint64_t h = mb_hash(fo->code, fo->code_len);
  h ^= mb_hash(g_aux.p, g_aux.len) * 1099511628211ull;
  uint64_t at = merge ? mb_find(obj, fo, &g_aux, h) : UINT64_MAX;
  *placed = at;
  if (at != UINT64_MAX) {

    obj_intern_sym(obj, fo->sym, SYM_LOCAL_FUNC, 0, at + fo->entry_off);
    uint32_t si = obj_find_sym(obj, fo->sym);
    obj->syms[si].value = at + fo->entry_off;
    obj->syms[si].size = fo->code_len - fo->entry_off;
    g_mb_hits++;
    g_mb_bytes += fo->code_len;
    g_stats.insts_emitted += fo->insts_emitted;
    xfree(fo->uw.sites); xfree(fo->uw.clauses);
    xfree(fo->uw.gcsites); xfree(fo->uw.gcroots);
    xfree(fo->code); xfree(fo->relocs);
    return;
  }
  if (!seed) goto placed_seed;
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
placed_seed:
  if (fo->want_align)
    while (((obj->text.len + fo->entry_off) & (ALIGN_BYTES - 1)) != fo->phase)
      g_target->pad_nop(&obj->text);
  else if (fo->align8)
    while ((obj->text.len + fo->entry_off) & 7)
      g_target->pad_nop(&obj->text);
  uint64_t base = obj->text.len;
  *placed = base;
  buf_bytes(&obj->text, fo->code, fo->code_len);
  for (uint32_t i = 0; i < fo->nrelocs; i++)
    obj_add_reloc(obj, fo->relocs[i].kind, 0, base + fo->relocs[i].offset,
                  fo->relocs[i].sym, fo->relocs[i].addend);
  obj_intern_sym(obj, fo->sym, SYM_LOCAL_FUNC, 0, base + fo->entry_off);
  uint32_t si = obj_find_sym(obj, fo->sym);
  obj->syms[si].value = base + fo->entry_off;
  obj->syms[si].size = fo->code_len - fo->entry_off;
  /* the lead ahead of the symbol (isel.c emit_entry_check) is a symbol of
     its own, so that a disassembly or a profile does not file it under the
     function before */
  if (fo->entry_off) {
    char name[256];
    snprintf(name, sizeof name, "%s$entry", fo->sym);
    uint32_t li = obj_intern_sym(obj, name, SYM_LOCAL_FUNC, 0, base);
    obj->syms[li].value = base;
    obj->syms[li].size = fo->entry_off;
    if (obj->syms[si].display && !obj->syms[li].display) {
      snprintf(name, sizeof name, "%s$entry", obj->syms[si].display);
      obj->syms[li].display = xstrndup(name, strlen(name));
    }
  }
  g_stats.insts_emitted += fo->insts_emitted;
  codegen_note_unwind(&fo->uw);
  if (merge) mb_add(base, fo, &g_aux, h);
  xfree(fo->uw.sites); xfree(fo->uw.clauses);
  xfree(fo->uw.gcsites); xfree(fo->uw.gcroots);
  xfree(fo->code); xfree(fo->relocs); xfree(fo->refs);
}

void codegen_commit_function(ObjBuilder *obj, FuncOut *fo) {
  uint64_t at;
  if (!fo->full) {
    commit_one(obj, fo, g_merge_on, true, &at);
    return;
  }
  /* A shrink-wrapped start, and its full body right after it: where the
     body's symbol lands is known before either is placed, the start being
     a leaf that aligns nothing, and the start's branches go there.  Neither
     merges with another body, whose branches would go to another's. */
  FuncOut *x = fo->full;
  fo->full = NULL;
  if (fo->want_align || fo->align8 || x->align8)
    warmc_fatal("internal: shrink-wrapped %s wants an alignment", fo->sym);
  uint64_t start = obj->text.len;
  uint64_t p = start + fo->code_len;
  if (x->want_align)
    while (((p + x->entry_off) & (ALIGN_BYTES - 1)) != x->phase) p += 4;
  uint64_t body = p + x->entry_off;
  for (uint32_t i = 0; i < fo->nfull_br; i++) {
    uint32_t off = fo->full_br[i];
    int64_t d = (int64_t)body - (int64_t)(start + off);
    if (d & 3 || d >= (1ll << 27) || d < -(1ll << 27))
      warmc_fatal("internal: %s's start cannot reach its full body", fo->sym);
    uint32_t insn = 0x14000000u | ((uint32_t)(d >> 2) & 0x03ffffffu);
    memcpy(fo->code + off, &insn, 4);
  }
  xfree(fo->full_br); fo->full_br = NULL;
  commit_one(obj, fo, false, false, &at);
  if (at != start) warmc_fatal("internal: %s's start moved", fo->sym);
  commit_one(obj, x, false, false, &at);
  if (at + x->entry_off != body) warmc_fatal("internal: %s moved", x->sym);
  /* the full body by the function's name too, for a disassembly or a profile */
  {
    uint32_t si = obj_find_sym(obj, fo->sym), xi = obj_find_sym(obj, x->sym);
    if (si != UINT32_MAX && xi != UINT32_MAX && obj->syms[si].display && !obj->syms[xi].display) {
      const char *d = obj->syms[si].display;
      char *nm = xmalloc(strlen(d) + 6);
      sprintf(nm, "%s$full", d);
      obj->syms[xi].display = nm;
    }
  }
  if (x->sym_owned) xfree((char *)x->sym);
  xfree(x);
}
