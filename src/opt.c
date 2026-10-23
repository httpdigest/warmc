#include "ir.h"

#ifndef KB_DEPTH
#define KB_DEPTH 4
#endif
#include <time.h>
#include "opcodes.h"
#include "reader.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool dominates(IrFunc *f, uint32_t a, uint32_t b) {
  return ir_dominates_checked(f, a, b);
}

static uint32_t *count_uses(IrFunc *f) {
  uint32_t *uses = xcalloc(f->ninsts ? f->ninsts : 1, sizeof(uint32_t));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      Ref *a = ir_args(f, b->insts[i]);
      for (uint32_t j = 0; j < in->nargs; j++) uses[a[j]]++;
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) uses[b->term_arg]++;
    for (uint32_t i = 0; i < b->nret; i++) uses[b->ret_args[i]]++;
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++) uses[b->succs[s].args[j]]++;
  }
  return uses;
}

static uint32_t rd_latency(uint16_t op) {
  switch (op) {

    case I_POPCNT: return 11;
    case I_DIVS: case I_DIVU: case I_REMS: case I_REMU: return 10;
    case I_MUL: return 3;
    case I_LOAD: return 4;

    case I_TRUNCS: case I_TRUNCU: case I_CONVERTS: case I_CONVERTU:
    case I_REINTERPRET: return 4;
    case I_CLZ: case I_CTZ: return 2;
    default: return 1;
  }
}

/* memo[v] is v's depth when seen[v] is the stamp of the block asked about */
static uint32_t rd_depth(IrFunc *f, Ref v, uint32_t bi, uint32_t *memo,
                         uint32_t *seen, uint32_t stamp) {
  if (v >= f->ninsts) return 0;
  if (seen[v] == stamp) return memo[v];
  seen[v] = stamp;
  memo[v] = 0;
  Inst *in = &f->insts[v];
  if (in->block != bi || in->op == I_PARAM || in->op == I_CONST) return 0;
  uint32_t worst = 0;
  Ref *a = ir_args(f, v);
  for (uint32_t j = 0; j < in->nargs; j++) {
    uint32_t d = rd_depth(f, a[j], bi, memo, seen, stamp);
    if (d > worst) worst = d;
  }
  memo[v] = worst + rd_latency(in->op);
  return memo[v];
}

static bool rd_assoc_op(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  if (vt_is_float((ValType)in->type)) return false;
  switch (in->op) {
    case I_XOR: case I_OR: case I_AND: case I_ADD: case I_MUL: return true;
    default: return false;
  }
}

/* A chain of i32 links can continue below a wrap into i64 links of the same
   op: Go's back end keeps every value in an i64 and adds in i64 until wasm
   wants an i32, so md5's step  rotl(wrap((F + a) + x) + K)  is two i64 adds
   and an i32 one, and its deepest term F first.  Each of these ops gives the
   same low half whether done in 64 bits or 32, so the rebuilt chain is all
   i32, its i64 terms wrapped where it starts. */
static bool pass_reassoc(IrFunc *f) {
  uint32_t *uses = count_uses(f);
  uint32_t cap = f->ninsts ? f->ninsts : 1;
  uint32_t *memo = xcalloc(cap, sizeof(uint32_t));
  /* a new stamp per block, and after each chain rebuilt, forgets every
     depth: clearing a flag per instruction of the function there made the
     pass blocks times instructions */
  uint32_t *seen = xcalloc(cap, sizeof(uint32_t)), stamp = 0;

  uint8_t *inner = xcalloc(cap, 1);
  uint32_t *pos = xmalloc(cap * sizeof(uint32_t));
  for (uint32_t i = 0; i < cap; i++) pos[i] = 0xffffffffu;
  bool changed = false;
  Ref chain[64], leaf[65];
  uint8_t *link = xcalloc(cap, 1);
  uint32_t *tmp = NULL;
  uint32_t tmpcap = 0;

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    stamp++;
    for (uint32_t i = 0; i < b->ninsts; i++) pos[b->insts[i]] = i;
    if (b->ninsts + 65 > tmpcap) { tmpcap = b->ninsts * 2 + 72;
      tmp = xrealloc(tmp, tmpcap * sizeof(uint32_t)); }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      if (!rd_assoc_op(f, v)) continue;
      Ref l = ir_args(f, v)[0];
      if (l < f->ninsts && f->insts[l].block == bi && uses[l] == 1 &&
          f->insts[l].op == f->insts[v].op &&
          f->insts[l].type == f->insts[v].type)
        inner[l] = 1;
    }

    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref root = b->insts[i];
      if (!rd_assoc_op(f, root) || inner[root]) continue;
      uint16_t op = f->insts[root].op;
      uint8_t ty = f->insts[root].type;
      uint32_t n = 0, wide = UINT32_MAX;
      Ref wrap = REF_NONE;
      Ref cur = root;
      while (n < 64) {
        chain[n++] = cur;
        Ref l = ir_args(f, cur)[0];
        if (l >= f->ninsts || f->insts[l].block != bi) break;
        if (wide == UINT32_MAX && ty == VT_I32 && f->insts[l].op == I_WRAP &&
            uses[l] == 1) {
          Ref l2 = ir_args(f, l)[0];
          if (l2 >= f->ninsts || f->insts[l2].block != bi ||
              f->insts[l2].op != op || f->insts[l2].type != VT_I64 || uses[l2] != 1)
            break;
          wrap = l; wide = n; cur = l2;
          continue;
        }
        uint8_t lty = wide == UINT32_MAX ? ty : VT_I64;
        if (f->insts[l].op != op || f->insts[l].type != lty) break;
        if (uses[l] != 1) break;
        cur = l;
      }
      if (n < 3) continue;

      uint32_t nl = 0;
      leaf[nl++] = ir_args(f, chain[n - 1])[0];
      leaf[nl++] = ir_args(f, chain[n - 1])[1];
      for (uint32_t k = n - 1; k-- > 0;) leaf[nl++] = ir_args(f, chain[k])[1];

      uint32_t worst = 0, wi = 0;
      for (uint32_t k = 0; k < nl; k++) {
        uint32_t d = rd_depth(f, leaf[k], bi, memo, seen, stamp);
        if (d > worst) { worst = d; wi = k; }
      }
      if (wi + 1 == nl) continue;
      uint32_t last_d = rd_depth(f, leaf[nl - 1], bi, memo, seen, stamp);
      if (worst < last_d + 4) continue;

      if (n > 8) continue;

      /* The i64 terms of a chain that went through a wrap, wrapped: made
         at the end of the block, moved below to where the chain goes. */
      Ref made[65];
      uint32_t nmade = 0;
      if (wide != UINT32_MAX) {
        for (uint32_t k = 0; k < nl; k++) {
          if (f->insts[leaf[k]].type != VT_I64) continue;
          uint16_t lop = f->insts[leaf[k]].op;
          if ((lop == I_EXTENDU || lop == I_EXTENDS) && f->insts[leaf[k]].nargs == 1) {
            leaf[k] = ir_args(f, leaf[k])[0];
            continue;
          }
          Ref w = lop == I_CONST
                ? ir_emit(f, bi, I_CONST, VT_I32, NULL, 0,
                          f->insts[leaf[k]].imm & 0xffffffffu, 0)
                : ir_emit(f, bi, I_WRAP, VT_I32, &leaf[k], 1, 0, 0);
          made[nmade++] = w;
          leaf[k] = w;
        }
        b = &f->blocks[bi];
        b->ninsts -= nmade;
        for (uint32_t k = wide; k < n; k++) f->insts[chain[k]].type = VT_I32;
        ir_args(f, chain[wide - 1])[0] = chain[wide];
        f->insts[wrap].op = I_NOP;
        f->insts[wrap].nargs = 0;
        if (f->ninsts > cap) {
          uint32_t nc = f->ninsts * 2;
          memo = xrealloc(memo, nc * sizeof(uint32_t));
          seen = xrealloc(seen, nc * sizeof(uint32_t));
          inner = xrealloc(inner, nc);
          link = xrealloc(link, nc);
          pos = xrealloc(pos, nc * sizeof(uint32_t));
          uses = xrealloc(uses, nc * sizeof(uint32_t));
          memset(memo + cap, 0, (nc - cap) * sizeof(uint32_t));
          memset(seen + cap, 0, (nc - cap) * sizeof(uint32_t));
          memset(inner + cap, 0, nc - cap);
          memset(link + cap, 0, nc - cap);
          memset(uses + cap, 0, (nc - cap) * sizeof(uint32_t));
          cap = nc;
        }
        for (uint32_t k = 0; k < nmade; k++) uses[made[k]] = 1;
      }

      if (b->ninsts + nmade > tmpcap) {
        tmpcap = (b->ninsts + nmade) * 2;
        tmp = xrealloc(tmp, tmpcap * sizeof(uint32_t));
      }
      for (uint32_t k = 0; k < n; k++) link[chain[k]] = 1;
      uint32_t m = 0;
      for (uint32_t k = 0; k < b->ninsts; k++) {
        Ref u = b->insts[k];
        if (u == chain[0]) {
          for (uint32_t j = 0; j < nmade; j++) tmp[m++] = made[j];
          for (uint32_t j = n; j-- > 0;) tmp[m++] = chain[j];
          continue;
        }
        if (link[u]) continue;
        tmp[m++] = u;
      }
      for (uint32_t k = 0; k < n; k++) link[chain[k]] = 0;
      memcpy(b->insts, tmp, m * sizeof(uint32_t));
      b->ninsts = m;

      Ref a = leaf[wi];
      for (uint32_t k = wi; k + 1 < nl; k++) leaf[k] = leaf[k + 1];
      leaf[nl - 1] = a;
      ir_args(f, chain[n - 1])[0] = leaf[0];
      ir_args(f, chain[n - 1])[1] = leaf[1];
      for (uint32_t k = n - 1, li = 2; k-- > 0; li++)
        ir_args(f, chain[k])[1] = leaf[li];
      for (uint32_t k = 1; k < n; k++) inner[chain[k]] = 1;
      changed = true;
      g_stats.reassoc++;
      for (uint32_t k = 0; k < b->ninsts; k++) pos[b->insts[k]] = k;
      stamp++;
      i = pos[chain[0]];
    }
  }
  xfree(uses); xfree(memo); xfree(seen); xfree(inner); xfree(pos);
  xfree(link); xfree(tmp);
  return changed;
}

static bool is_const(IrFunc *f, Ref v, uint64_t *out) {
  if ((ValType)f->insts[v].type == VT_V128) return false;
  if (f->insts[v].op != I_CONST) return false;
  *out = f->insts[v].imm;
  return true;
}

/* The object a GC accessor really reads: ref.cast is a pure retype, so o
   and (ref.cast o) name the same object for redundancy purposes. */
static Ref gc_ref_root(IrFunc *f, Ref r) {
  r = ir_resolve(f, r);
  for (int hop = 0; hop < 8; hop++) {
    Inst *in = &f->insts[r];
    if (in->op != I_REF_CONVERT || in->nargs != 1) break;
    r = ir_resolve(f, ir_args(f, r)[0]);
  }
  return r;
}

/* Field index behind an I_FIELD_GET/SET's byte offset, from the operand's
   static type; UINT32_MAX when that type names no concrete struct.  Sound
   against subtyping: a subtype may neither change a field's mutability
   nor move it, so the statically declared type answers for the dynamic
   one. */
static uint32_t gc_field_of_offset(const Module *m, ValType obj_t,
                                   uint64_t off) {
  if (!vt_is_ref(obj_t) || !vt_concrete(obj_t)) return UINT32_MAX;
  uint32_t ti = vt_type_index(obj_t);
  if (ti >= m->ntypes || m->types[ti].kind != CT_STRUCT) return UINT32_MAX;
  const CompType *t = &m->types[ti];
  for (uint32_t j = 0; j < t->nfields; j++)
    if ((uint64_t)t->hdr_size + t->fields[j].offset == off) return j;
  return UINT32_MAX;
}

static bool gc_field_immutable(IrFunc *f, Ref access) {
  Inst *in = &f->insts[access];
  if (!in->nargs) return false;
  ValType t = (ValType)f->insts[ir_resolve(f, ir_args(f, access)[0])].type;
  uint32_t j = gc_field_of_offset(f->m, t, in->imm);
  if (j == UINT32_MAX) return false;
  return !f->m->types[vt_type_index(t)].fields[j].mutable_;
}

static bool gc_array_elem_immutable(const Module *m, uint32_t ti) {
  if (ti >= m->ntypes || m->types[ti].kind != CT_ARRAY) return false;
  return !m->types[ti].fields[0].mutable_;
}

/* Whether a GC op can trap as flagged: the null check, the array bounds
   check, or an allocation's heap-OOM. */
static bool gc_op_traps(const Inst *in) {
  switch (in->op) {
    case I_FIELD_GET: case I_FIELD_SET: case I_ARRAY_LEN: case I_I31_GET:
      return (in->imm2 & GCF_NULLCHK) != 0;
    case I_ARRAY_GET: case I_ARRAY_SET:
      return (in->imm2 & GCF_NULLCHK) || !(in->imm2 & GCF_NOBOUND);
    case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
    case I_ARRAY_OP:
      return true;
    default:
      return false;
  }
}

static Ref rot_src_by(IrFunc *f, Ref v, uint32_t width, uint64_t k) {
  Inst *in = &f->insts[v];
  if ((in->op != I_ROTR && in->op != I_ROTL) || in->nargs != 2) return REF_NONE;
  uint64_t amt;
  if (!is_const(f, ir_args(f, v)[1], &amt)) return REF_NONE;
  amt %= width;
  uint64_t want = in->op == I_ROTR ? k : (width - k) % width;
  return amt == want ? ir_args(f, v)[0] : REF_NONE;
}

static Ref and_by(IrFunc *f, Ref v, uint64_t mask) {
  Inst *in = &f->insts[v];
  if (in->op != I_AND || in->nargs != 2) return REF_NONE;
  Ref *a = ir_args(f, v);
  uint64_t m;
  for (int k = 0; k < 2; k++)
    if (is_const(f, a[k], &m) && m == mask) return a[k ^ 1];
  return REF_NONE;
}

#define BM_ZERO (-1)
typedef struct { Ref src; int8_t from[8]; } ByteMap;

static bool byte_map(IrFunc *f, Ref v, uint32_t w, ByteMap *m, int depth) {
  if (depth > 32 || v >= f->ninsts) return false;
  Inst *in = &f->insts[v];

  if ((vt_is_64((ValType)in->type) ? 8u : 4u) != w) return false;
  uint64_t all = (w == 8) ? ~0ull : 0xffffffffu;
  Ref *a = ir_args(f, v);
  uint64_t c;
  switch (in->op) {
    case I_AND: {
      if (in->nargs != 2 || !is_const(f, a[1], &c)) return false;
      if (!byte_map(f, a[0], w, m, depth + 1)) return false;
      c &= all;
      for (uint32_t i = 0; i < w; i++) {
        uint8_t byte = (uint8_t)(c >> (8 * i));
        if (byte == 0) m->from[i] = BM_ZERO;
        else if (byte != 0xff) return false;
      }
      return true;
    }
    case I_SHL: case I_SHRU: case I_ROTL: case I_ROTR: {
      if (in->nargs != 2 || !is_const(f, a[1], &c)) return false;
      c &= (w == 8) ? 63u : 31u;
      if (c % 8) return false;
      uint32_t nb = (uint32_t)(c / 8);
      if (!byte_map(f, a[0], w, m, depth + 1)) return false;
      int8_t old[8];
      memcpy(old, m->from, sizeof old);
      for (uint32_t i = 0; i < w; i++) {
        switch (in->op) {
          case I_SHL:  m->from[i] = (i >= nb) ? old[i - nb] : BM_ZERO; break;
          case I_SHRU: m->from[i] = (i + nb < w) ? old[i + nb] : BM_ZERO; break;
          case I_ROTL: m->from[i] = old[(i + w - nb) % w]; break;
          default:     m->from[i] = old[(i + nb) % w]; break;
        }
      }
      return true;
    }

    case I_OR: case I_XOR: case I_ADD: {
      if (in->nargs != 2) return false;
      ByteMap l, r;
      if (!byte_map(f, a[0], w, &l, depth + 1)) return false;
      if (!byte_map(f, a[1], w, &r, depth + 1)) return false;
      bool lz = true, rz = true;
      for (uint32_t i = 0; i < w; i++) {
        if (l.from[i] != BM_ZERO) lz = false;
        if (r.from[i] != BM_ZERO) rz = false;
      }
      if (lz) { *m = r; return true; }
      if (rz) { *m = l; return true; }
      if (l.src != r.src) return false;
      m->src = l.src;
      for (uint32_t i = 0; i < w; i++) {
        if (l.from[i] == BM_ZERO) m->from[i] = r.from[i];
        else if (r.from[i] == BM_ZERO) m->from[i] = l.from[i];
        else return false;
      }
      return true;
    }
    default:

      m->src = v;
      for (uint32_t i = 0; i < w; i++) m->from[i] = (int8_t)i;
      return true;
  }
}

static Ref bswap_src(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  ValType t = (ValType)in->type;
  if (t != VT_I32 && t != VT_I64) return REF_NONE;
  uint32_t w = vt_is_64(t) ? 8u : 4u;
  ByteMap m;
  m.src = REF_NONE;
  memset(m.from, BM_ZERO, sizeof m.from);
  if (!byte_map(f, v, w, &m, 0)) return REF_NONE;
  if (m.src == REF_NONE || m.src == v) return REF_NONE;
  for (uint32_t i = 0; i < w; i++)
    if (m.from[i] != (int8_t)(w - 1 - i)) return REF_NONE;
  if (f->insts[m.src].type != in->type) return REF_NONE;
  return m.src;
}

static void replace_with_const(IrFunc *f, Ref v, uint64_t bits) {
  Inst *in = &f->insts[v];
  in->op = I_CONST;
  in->nargs = 0;
  in->imm = bits;
  in->imm2 = 0;
  if (in->type == VT_I32 || in->type == VT_F32) in->imm &= 0xffffffffu;
}

/* Rounds until no param is trivial or dead.  A round reads only the use
   counts and the edges' arguments as the previous round left them, so after
   the first (whose ir_apply_subst also settles what the caller left
   pending) the counts are kept as a recount would find them and the edges'
   arguments resolved in place, and the rest of the function is resolved
   once at the end: a recount and a full ir_apply_subst every round was
   rounds times the function, and a Go function can take hundreds.  A
   param's verdict rests on its count and its edges' arguments alone, so
   from the third round on, only the blocks where the round before changed
   one are looked at again, not every param against all its edges.  And
   the only arguments a round leaves to resolve are the params it
   replaced, so from the first round that resolves in place, an index of
   the edges each param is passed on is kept, and only those edges are
   rewritten: resolving every edge's arguments each round had become the
   largest part of the pass. */
static bool prune_params(IrFunc *f) {
  bool any = false, changed = true, lazy = false, lazy_changed = false;
  IrEdgeIdx ei = ir_edge_index(f);
  uint32_t *uses = NULL;
  uint8_t *inord = NULL, *redo = NULL;
  /* px_at/px_src/px_si: by value, the edges that passed it when the index
     was made; px_next chains to them the runs of the params it has since
     replaced, and px_tail names the last (UINT32_MAX: the value's own) */
  uint32_t *px_at = NULL, *px_src = NULL, *px_si = NULL;
  uint32_t *px_next = NULL, *px_tail = NULL;
  Ref *gone = NULL, *dropped = NULL;
  uint32_t ngone = 0, capgone = 0, ndropped = 0, capdropped = 0;
  while (changed) {
    changed = false;
    if (!uses) uses = count_uses(f);
    ngone = ndropped = 0;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      if (bi == f->entry) continue;
      if (redo) {
        if (!redo[bi]) continue;
        redo[bi] = 0;
      }
      for (uint32_t p = 0; p < b->nparams;) {
        Ref param = b->params[p];
        Ref same = REF_NONE;
        bool trivial = true;
        for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
          Block *sb = &f->blocks[ei.src[k]];
          if (!sb->reachable) continue;
          Ref arg = sb->succs[ei.si[k]].args[p];
          if (arg == param) continue;
          if (same == REF_NONE) same = arg;
          else if (same != arg) { trivial = false; break; }
        }
        bool dead = uses[param] == 0;
        if (!trivial && !dead) { p++; continue; }
        if (dead) same = REF_NONE;
        else if (same == REF_NONE) same = param;

        if (!dead) {
          if (same == param) { p++; continue; }
          ir_replace(f, param, same);
          if (lazy) {
            if (ngone == capgone) {
              capgone = capgone ? capgone * 2 : 16;
              gone = xrealloc(gone, capgone * sizeof(Ref));
            }
            gone[ngone++] = param;
          }
        }

        for (uint32_t j = p; j + 1 < b->nparams; j++) b->params[j] = b->params[j + 1];
        b->nparams--;
        for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
          Edge *e = &f->blocks[ei.src[k]].succs[ei.si[k]];
          if (lazy && e->nargs && inord[ei.src[k]]) {
            if (ndropped == capdropped) {
              capdropped = capdropped ? capdropped * 2 : 16;
              dropped = xrealloc(dropped, capdropped * sizeof(Ref));
            }
            dropped[ndropped++] = e->args[p < e->nargs ? p : e->nargs - 1];
          }
          for (uint32_t j = p; j + 1 < e->nargs; j++) e->args[j] = e->args[j + 1];
          if (e->nargs) e->nargs--;
        }
        g_stats.params_removed++;
        changed = any = true;
      }
    }
    if (!changed) break;
    if (!lazy) {
      ir_apply_subst(f);
      xfree(uses);
      uses = NULL;
      lazy = true;
      inord = xcalloc(f->nblocks ? f->nblocks : 1, 1);
      for (uint32_t oi = 0; oi < f->norder; oi++) inord[f->order[oi]] = 1;
      continue;
    }
    /* the counts and edge arguments ir_apply_subst and count_uses would
       leave: the removed arguments uncounted, a replaced param's uses its
       replacement's */
    lazy_changed = true;
    if (!redo) redo = xcalloc(f->nblocks ? f->nblocks : 1, 1);
#define PP_REDO(r) do { const Inst *x_ = &f->insts[r];                      \
                        if (x_->op == I_PARAM) redo[x_->block] = 1; } while (0)
    for (uint32_t k = 0; k < ndropped; k++) {
      uses[dropped[k]]--;
      PP_REDO(dropped[k]);
    }
    for (uint32_t k = 0; k < ngone; k++) {
      Ref g = gone[k], to = ir_resolve(f, g);
      uses[to] += uses[g];
      uses[g] = 0;
      PP_REDO(to);
    }
#undef PP_REDO
    if (!px_at) {
      uint32_t ni = f->ninsts;
      px_at = xcalloc((size_t)ni + 2, sizeof(uint32_t));
      for (uint32_t bi = 0; bi < f->nblocks; bi++) {
        const Block *b = &f->blocks[bi];
        for (uint32_t k = 0; k < b->nsuccs; k++)
          for (uint32_t j = 0; j < b->succs[k].nargs; j++) {
            Ref a = b->succs[k].args[j];
            if (a < ni && f->insts[a].op == I_PARAM) px_at[a + 2]++;
          }
      }
      for (uint32_t v = 0; v < ni; v++) px_at[v + 2] += px_at[v + 1];
      uint32_t total = px_at[ni + 1];
      px_src = xmalloc((total ? total : 1) * sizeof(uint32_t));
      px_si = xmalloc((total ? total : 1) * sizeof(uint32_t));
      for (uint32_t bi = 0; bi < f->nblocks; bi++) {
        const Block *b = &f->blocks[bi];
        for (uint32_t k = 0; k < b->nsuccs; k++)
          for (uint32_t j = 0; j < b->succs[k].nargs; j++) {
            Ref a = b->succs[k].args[j];
            if (a < ni && f->insts[a].op == I_PARAM) {
              uint32_t t = px_at[a + 1]++;
              px_src[t] = bi; px_si[t] = k;
            }
          }
      }
      px_next = xmalloc(((size_t)ni ? ni : 1) * sizeof(uint32_t));
      px_tail = xmalloc(((size_t)ni ? ni : 1) * sizeof(uint32_t));
      memset(px_next, 0xff, (size_t)ni * sizeof(uint32_t));
      memset(px_tail, 0xff, (size_t)ni * sizeof(uint32_t));
    }
    for (uint32_t k = 0; k < ngone; k++) {
      Ref g = gone[k], r = ir_resolve(f, g);
      for (uint32_t u = g; u != UINT32_MAX; u = px_next[u])
        for (uint32_t t = px_at[u]; t < px_at[u + 1]; t++) {
          Edge *e = &f->blocks[px_src[t]].succs[px_si[t]];
          for (uint32_t j = 0; j < e->nargs; j++)
            if (e->args[j] == g) { e->args[j] = r; redo[e->target] = 1; }
        }
      if (r != g && f->insts[r].op == I_PARAM) {
        uint32_t rt = px_tail[r] == UINT32_MAX ? r : px_tail[r];
        px_next[rt] = g;
        px_tail[r] = px_tail[g] == UINT32_MAX ? g : px_tail[g];
      }
    }
  }
  if (lazy_changed) ir_apply_subst(f);
  xfree(px_at); xfree(px_src); xfree(px_si); xfree(px_next); xfree(px_tail);
  xfree(uses); xfree(inord); xfree(redo); xfree(gone); xfree(dropped);
  ir_edge_index_free(&ei);
  return any;
}

static uint64_t trunc_to(ValType t, uint64_t v) {
  return (t == VT_I32) ? (v & 0xffffffffu) : v;
}

static bool fold_int_binop(uint16_t op, ValType t, uint64_t a, uint64_t b, uint64_t *out) {
  bool w64 = (t == VT_I64);
  uint64_t mask = w64 ? ~0ull : 0xffffffffu;
  int bits = w64 ? 64 : 32;
  int64_t sa = w64 ? (int64_t)a : (int64_t)(int32_t)(uint32_t)a;
  int64_t sb = w64 ? (int64_t)b : (int64_t)(int32_t)(uint32_t)b;
  uint64_t ua = a & mask, ub = b & mask;
  uint64_t r;
  switch (op) {
    case I_ADD: r = ua + ub; break;
    case I_SUB: r = ua - ub; break;
    case I_MUL: r = ua * ub; break;
    case I_MULHU: r = w64 ? (uint64_t)(((unsigned __int128)ua * ub) >> 64) : (ua * ub) >> 32; break;
    case I_MULHS: r = w64 ? (uint64_t)(((__int128)sa * sb) >> 64) : (uint64_t)((sa * sb) >> 32); break;
    case I_AND: r = ua & ub; break;
    case I_OR:  r = ua | ub; break;
    case I_XOR: r = ua ^ ub; break;
    case I_SHL: r = ua << (ub % (uint64_t)bits); break;
    case I_SHRU: r = ua >> (ub % (uint64_t)bits); break;
    case I_SHRS: {
      int sh = (int)(ub % (uint64_t)bits);
      r = (uint64_t)(sa >> sh);
      break;
    }
    case I_ROTL: { int sh = (int)(ub % (uint64_t)bits);
                   r = sh ? ((ua << sh) | (ua >> (bits - sh))) : ua; break; }
    case I_ROTR: { int sh = (int)(ub % (uint64_t)bits);
                   r = sh ? ((ua >> sh) | (ua << (bits - sh))) : ua; break; }
    case I_DIVS: if (ub == 0) return false;
                 if (sb == -1 && sa == (w64 ? INT64_MIN : (int64_t)INT32_MIN)) return false;
                 r = (uint64_t)(sa / sb); break;
    case I_DIVU: if (ub == 0) return false; r = ua / ub; break;
    case I_REMS: if (ub == 0) return false;
                 r = (sb == -1) ? 0 : (uint64_t)(sa % sb); break;
    case I_REMU: if (ub == 0) return false; r = ua % ub; break;
    case I_EQ:  r = (ua == ub); break;
    case I_NE:  r = (ua != ub); break;
    case I_LTS: r = (sa < sb); break;
    case I_LTU: r = (ua < ub); break;
    case I_GTS: r = (sa > sb); break;
    case I_GTU: r = (ua > ub); break;
    case I_LES: r = (sa <= sb); break;
    case I_LEU: r = (ua <= ub); break;
    case I_GES: r = (sa >= sb); break;
    case I_GEU: r = (ua >= ub); break;
    default: return false;
  }
  *out = r & mask;
  return true;
}

static bool fold_int_unop(uint16_t op, ValType t, uint64_t a, uint64_t *out) {
  bool w64 = (t == VT_I64);
  uint64_t mask = w64 ? ~0ull : 0xffffffffu;
  uint64_t ua = a & mask;
  int bits = w64 ? 64 : 32;
  uint64_t r;
  switch (op) {
    case I_CLZ: {
      if (ua == 0) { r = (uint64_t)bits; break; }
      int n = 0;
      for (int i = bits - 1; i >= 0; i--) { if (ua & (1ull << i)) break; n++; }
      r = (uint64_t)n; break;
    }
    case I_CTZ: {
      if (ua == 0) { r = (uint64_t)bits; break; }
      int n = 0;
      for (int i = 0; i < bits; i++) { if (ua & (1ull << i)) break; n++; }
      r = (uint64_t)n; break;
    }
    case I_POPCNT: {
      int n = 0;
      for (int i = 0; i < bits; i++) if (ua & (1ull << i)) n++;
      r = (uint64_t)n; break;
    }
    case I_BSWAP: {
      r = 0;
      for (int i = 0; i < bits; i += 8) r |= ((ua >> i) & 0xffull) << (bits - 8 - i);
      break;
    }
    case I_EQZ: r = (ua == 0); break;
    case I_WRAP: r = ua & 0xffffffffu; break;
    case I_EXTENDS: r = (uint64_t)(int64_t)(int32_t)(uint32_t)ua; break;
    case I_EXTENDU: r = ua & 0xffffffffu; break;
    case I_EXT8S: r = (uint64_t)(int64_t)(int8_t)(uint8_t)ua & mask; break;
    case I_EXT16S: r = (uint64_t)(int64_t)(int16_t)(uint16_t)ua & mask; break;
    case I_EXT32S: r = (uint64_t)(int64_t)(int32_t)(uint32_t)ua; break;
    case I_REF_IS_NULL: r = (ua == 0); break;
    default: return false;
  }

  *out = r;
  return true;
}

static Ref emit_before(IrFunc *f, uint32_t bi, uint32_t *idx, IrOp op, ValType t,
                       const Ref *args, uint32_t nargs, uint64_t imm) {
  Ref r = ir_emit(f, bi, op, t, args, nargs, imm, 0);
  Block *b = &f->blocks[bi];
  for (uint32_t j = b->ninsts - 1; j > *idx; j--) b->insts[j] = b->insts[j - 1];
  b->insts[*idx] = r;
  (*idx)++;
  return r;
}

/* A value a loop carries round, made by a chain of one associative op --
   acc = ((acc + a) + b) + c -- waits on every link of the chain each
   iteration, so the loop runs no faster than one op per link.  The same sum
   as ((a + b) + c) + acc waits on one: the other terms are added among
   themselves, where the chain added them before, so they are consumed when
   they always were, while the previous iteration finishes.  LLVM writes
   n += popcount(v) + clz(v) as (n + clz) + popcount, and an unrolled copy of
   the body makes the chain four times as long.  Integer ops only, which are
   associative and commutative mod 2^n -- in a v128 too, lane by lane, when
   every link has the same lanes: Zig's unrolled vector sum adds four vectors
   into one accumulator a link at a time, four add.4s each waiting on the
   last, where native keeps four. */
#define RC_LINKS 64
static uint32_t rc_path(IrFunc *f, Ref v, Ref p, uint16_t op, uint8_t ty,
                        uint64_t lane, const uint32_t *uses, uint32_t lo,
                        Ref *link, uint32_t n) {
  if (n == RC_LINKS) return 0;
  link[n] = v;
  Ref *a = ir_args(f, v);
  if (a[0] == p || a[1] == p) return n + 1;
  for (int k = 0; k < 2; k++) {
    Inst *in = &f->insts[a[k]];
    /* a link computed before the loop, its invariant part, stays a term */
    if (in->op != op || in->type != ty || in->imm2 != lane || in->nargs != 2 ||
        uses[a[k]] != 1 || f->blocks[in->block].rpo < lo)
      continue;
    uint32_t m = rc_path(f, a[k], p, op, ty, lane, uses, lo, link, n + 1);
    if (m) return m;
  }
  return 0;
}

static bool pass_reassoc_carried(IrFunc *f) {
  uint32_t *uses = count_uses(f);
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t lb = f->order[oi];
    for (uint32_t s = 0; s < f->blocks[lb].nsuccs; s++) {
      uint32_t h = f->blocks[lb].succs[s].target;
      if (f->blocks[h].rpo > f->blocks[lb].rpo) continue;
      for (uint32_t j = 0; j < f->blocks[lb].succs[s].nargs && j < f->blocks[h].nparams; j++) {
        Ref r = f->blocks[lb].succs[s].args[j], p = f->blocks[h].params[j];
        Inst *ri = &f->insts[r];
        uint16_t op = ri->op;
        ValType ty = (ValType)ri->type;
        bool vec = ty == VT_V128;
        if (vec ? op != I_VADD && op != I_VMUL && op != I_VAND && op != I_VOR && op != I_VXOR
                : op != I_ADD && op != I_MUL && op != I_AND && op != I_OR && op != I_XOR)
          continue;
        if (ri->nargs != 2 || (ty != VT_I32 && ty != VT_I64 && !vec)) continue;
        uint64_t lane = ri->imm2;
        if (vec && VLANE_IS_FLOAT(lane)) continue;
        /* link[0] is r, link[n - 1] the one that reads p */
        Ref link[RC_LINKS], term[RC_LINKS];
        uint32_t n = rc_path(f, r, p, op, ty, lane, uses, f->blocks[h].rpo, link, 0);
        if (n < 2) continue;
        bool twice = false;
        for (uint32_t k = 0; k < n; k++) {
          Ref *a = ir_args(f, link[k]);
          Ref below = k + 1 < n ? link[k + 1] : p;
          term[k] = a[0] == below ? a[1] : a[0];
          if (term[k] == p) twice = true;
        }
        if (twice) continue;

        /* the constants among the terms are one constant: an unrolled
           counter's steps are */
        uint64_t kv = 0, c;
        bool have_k = false;
        for (uint32_t k = 0; k < n && !vec; k++)
          if (is_const(f, term[k], &c)) {
            if (!have_k) kv = c;
            else fold_int_binop(op, ty, kv, c, &kv);
            have_k = true;
          }
        uint64_t ident = vec ? 0 : op == I_MUL ? 1 : op == I_AND ? trunc_to(ty, ~0ull) : 0;

        Ref acc = REF_NONE;
        for (uint32_t k = n; k-- > 1;) {
          if (!vec && is_const(f, term[k], &c)) continue;
          if (acc == REF_NONE) { acc = term[k]; continue; }
          Ref *a = ir_args(f, link[k]);
          a[0] = acc;
          a[1] = term[k];
          acc = link[k];
        }
        uint32_t rb = ri->block, at = 0;
        Block *b = &f->blocks[rb];
        while (at < b->ninsts && b->insts[at] != r) at++;
        if (at == b->ninsts) continue;
        if (vec || !is_const(f, term[0], &c)) {
          Ref pr[2] = { acc, term[0] };
          if (acc == REF_NONE) acc = term[0];
          else {
            acc = emit_before(f, rb, &at, (IrOp)op, ty, pr, 2, 0);
            f->insts[acc].imm2 = lane;
          }
        }
        if (have_k && (trunc_to(ty, kv) != ident || acc == REF_NONE)) {
          Ref kc = emit_before(f, rb, &at, I_CONST, ty, NULL, 0, trunc_to(ty, kv));
          Ref pr[2] = { acc, kc };
          acc = acc == REF_NONE ? kc : emit_before(f, rb, &at, (IrOp)op, ty, pr, 2, 0);
        }
        Ref *ra = ir_args(f, r);
        bool kl = !vec && is_const(f, acc, &c);
        ra[0] = kl ? p : acc;
        ra[1] = kl ? acc : p;
        changed = true;
        g_stats.reassoc++;
      }
    }
  }
  xfree(uses);
  return changed;
}

/* A br_table with few distinct edges as a tree of compares: the index
   ranges that share an edge -- consecutive cases with the same target and
   arguments, and the default past the last case -- searched with unsigned
   compares.  A jump table makes the indirect branch wait on the index's
   load, its scaling and the table's own load, so a mispredicted one, which
   a random selector makes every time, resolves two loads later than a
   compare tree on the index; Zig's 6-way tagged-union dispatch ran 1.23x
   native through the table and 1.05x through native's tree, and LLVM
   builds such switches as trees on AArch64.  Past BRT_TREE_MAX ranges the
   table's one branch is worth its loads.

   An index that cannot reach the default -- cbench/fnptr's x >> 29 picks one
   of eight cases -- leaves the default no range: the last case's range runs
   on past it instead, which is the same for every index the code can see.
   fnptr's eight cases are then a tree of eight ranges, where the default's
   ninth was one past BRT_TREE_MAX and kept the table, and the tree's leaves
   are the arms pass_ifconvert collapses into selects, level by level. */
#define BRT_TREE_MAX 8
typedef struct { uint64_t lo; uint32_t edge; } BrtSeg;

static bool brt_edge_eq(const Edge *a, const Edge *b) {
  if (a->target != b->target || a->nargs != b->nargs || a->eh || b->eh) return false;
  for (uint32_t i = 0; i < a->nargs; i++)
    if (a->args[i] != b->args[i]) return false;
  return true;
}

static void brt_node(IrFunc *f, uint32_t nb, Ref idx, const BrtSeg *seg,
                     uint32_t nseg, const Edge *edges) {
  uint32_t mid = nseg / 2;
  Ref k = ir_emit(f, nb, I_CONST, VT_I32, NULL, 0, seg[mid].lo, 0);
  Ref a[2] = { idx, k };
  Ref c = ir_emit(f, nb, I_LTU, VT_I32, a, 2, 0, 0);
  f->blocks[nb].term = T_BRIF;
  f->blocks[nb].term_arg = c;
  for (int side = 0; side < 2; side++) {
    const BrtSeg *s = side ? seg + mid : seg;
    uint32_t n = side ? nseg - mid : mid;
    if (n == 1) {
      const Edge *e = &edges[s[0].edge];
      ir_add_edge(f, nb, e->target, e->args, e->nargs);
      continue;
    }
    uint32_t child = ir_new_block(f);
    ir_add_edge(f, nb, child, NULL, 0);
    brt_node(f, child, idx, s, n, edges);
  }
}

static bool pass_brtable_tree(IrFunc *f, bool *jumped) {
  bool changed = false;
  uint32_t nb = f->nblocks;
  for (uint32_t bi = 0; bi < nb; bi++) {
    Block *b = &f->blocks[bi];
    if (!b->reachable || b->term != T_BRTABLE || b->nsuccs < 2) continue;
    uint32_t n = b->nsuccs - 1;
    BrtSeg seg[BRT_TREE_MAX + 1];
    uint32_t nseg = 0;
    bool ok = true;
    uint32_t last = n;
    if (n && ir_value_max(f, b->term_arg, 6) < n) last = n - 1;
    for (uint32_t i = 0; i <= last && ok; i++) {
      if (b->succs[i].eh) ok = false;
      else if (nseg && brt_edge_eq(&b->succs[seg[nseg - 1].edge], &b->succs[i])) continue;
      else if (nseg == BRT_TREE_MAX) ok = false;
      else seg[nseg++] = (BrtSeg){ i, i };
    }
    if (ok && nseg == 1 && last < n) {
      /* every case the index can pick is the same edge */
      b->succs[0] = b->succs[seg[0].edge];
      b->nsuccs = 1;
      b->term = T_JUMP;
      changed = *jumped = true;
      continue;
    }
    if (!ok || nseg < 2) continue;
    /* the edges are copied out: the block's own list is rebuilt */
    Edge *edges = xmalloc((n + 1) * sizeof(Edge));
    memcpy(edges, b->succs, (n + 1) * sizeof(Edge));
    Ref idx = b->term_arg;
    b->nsuccs = 0;
    brt_node(f, bi, idx, seg, nseg, edges);
    xfree(edges);
    changed = true;
  }
  if (changed) ir_recompute_preds(f);
  return changed;
}

/* ---- if-conversion ---------------------------------------------------------

   A two-way branch whose arms are a few pure instructions that meet again
   becomes straight-line code: both arms run, and the join's parameters are
   selects on the branch's condition.  A branch the data decides costs a
   pipeline refill each time it is guessed wrong, about fourteen cycles on the
   M-series cores, where csel costs one.  LLVM folds the smallest such joins
   to selects before the wasm is written, and its AArch64 back end runs early
   if-conversion over what is left; warmc saw only the branches.
   cbench/fnptr is the case: clang turned table[x >> 29](s, x) into an
   eight-way br_table of one- to three-op bodies, native computes all eight
   and picks with seven csels, and warmc jumped through the table on a
   random index, 3.0x native.

   The shapes, B ending in br_if:
     diamond   B -> X0 -> J and B -> X1 -> J
     triangle  B -> X -> J and B -> J
     a branch both of whose edges go to J, with different arguments.
   An arm X has B as its only predecessor and no parameters, ends in a jump,
   is not on a path the compiler made rare, and holds only instructions safe
   to run when the branch would not have run them: pure and unable to trap
   -- no load (a guard page or a check before it is what made it safe where
   it was), store, call, global.set, division that can trap, trapping
   truncation, GC access or allocation -- and of no reference type, so
   nothing moves past what the collector's maps describe.  The arms'
   instructions are appended to B, and B jumps to J with a select on the
   condition for each parameter the two edges pass differently, the first
   operand the value the taken edge passed, as wasm's select takes it.  A
   compare that only the branch read is moved down to just before the
   selects, so that each csel reads the flags it set (flags_only in isel).

   Blocks are visited in post-order, so an inner diamond is collapsed before
   the branch around it: its head is then a block that jumps to the join,
   which is an arm of its own head in turn, and a compare tree that
   pass_brtable_tree made goes level by level into a tree of selects, as
   native's does for fnptr.  A join left with B as its one predecessor is
   merged into B, which makes B an arm of the next branch out when the join
   went on to the outer join -- an `if` with a result nested in another.

   What it costs, in the spirit of LLVM's SimplifyCFG and early
   if-conversion, without a profile to say which branches are unpredictable:
     * each arm's issue slots (ifc_cost: a constant is free, a multiply two)
       are at most IFC_ARM_COST for every arm of the source the arm was made
       of, and the selects count too, against a triangle's one arm or a
       diamond's two together; so a tree of cheap leaves collapses whole
       however deep it is, while an arm doing real work -- what a branch
       should guard -- stays one.  The selects count because compiler-rt's
       128-bit divide normalises with a six-op arm and three selects behind
       a branch that almost always goes one way (zigbench/wide128, +2.7%
       instructions when they did not); a triangle's empty side earns
       nothing because a chain of early exits, LLVM's unrolled remainder
       loop with each step leaving for the join, would otherwise collapse a
       step at a time into one block running every step (rsbench/itadapt,
       +6.6%);
     * each select's operands must be ready within IFC_CRIT cycles of the
       earlier of them, counting the condition: the cycles a correctly
       guessed branch would not have waited (LLVM's limit is half the
       misprediction penalty);
     * in a loop, a condition that waits on a load in the loop the values do
       not wait on keeps its branch: a guessed branch runs ahead of the load
       where the select would wait for it, and a load decides the step of a
       decoding loop -- with gobench/utf8scan's rune width a select, it ran
       1.20x, since the index the next load reads came from it.  Applied
       outside loops as well it changed 35 kernels, nearly all GHC's tag
       tests, for nothing (1.003);
     * so does a condition computed from the value the select would hand
       round the loop, when it is ready later than the values: the select
       then adds the condition's latency to every iteration's recurrence
       (ifc_recurrence; hsbench/fusion's Collatz step, 1.017x);
     * a join that branches again on a value the two edges pass keeps both
       branches (see the test in ifc_try);
     * at most IFC_MAX_SEL selects per join. */
#define IFC_ARM_COST 6
#define IFC_ARM_MAX  32
#define IFC_CRIT     8
#define IFC_MAX_SEL  6
#define IFC_LOAD_SLACK 2
#define IFC_DEPTH_LIM 48

/* WARMC_NO_IFCONV turns the pass off as -fno-ifconv does; WARMC_IFC_DEBUG=1
   prints each conversion and =2 each branch turned down too, with why;
   WARMC_IFC_ARM, _CRIT and _SEL set the three limits. */
static pthread_once_t g_ifc_once = PTHREAD_ONCE_INIT;
static struct { bool off; int dbg, arm, crit, sel; long only; } g_ifc;

static void ifc_init(void) {
  g_ifc.off = warmc_getenv("WARMC_NO_IFCONV") != NULL;
  const char *d = warmc_getenv("WARMC_IFC_DEBUG");
  g_ifc.dbg = d ? (d[0] == '2' ? 2 : 1) : 0;
  const char *e = warmc_getenv("WARMC_IFC_ARM");
  g_ifc.arm = e ? atoi(e) : IFC_ARM_COST;
  e = warmc_getenv("WARMC_IFC_CRIT");
  g_ifc.crit = e ? atoi(e) : IFC_CRIT;
  e = warmc_getenv("WARMC_IFC_SEL");
  g_ifc.sel = e ? atoi(e) : IFC_MAX_SEL;
  if (g_ifc.sel > 16) g_ifc.sel = 16;
  /* one function's conversions only, by index, for bisecting */
  e = warmc_getenv("WARMC_IFC_ONLY");
  g_ifc.only = e ? atol(e) : -1;
}

static bool lh_can_trap(IrFunc *f, Ref v);

/* The issue slots speculating v takes and the cycles its result takes
   (*lat), or -1 when v cannot run where its branch did not. */
static int ifc_cost(IrFunc *f, Ref v, uint32_t *lat) {
  Inst *in = &f->insts[v];
  ValType t = (ValType)in->type;
  if (vt_is_ref(t)) return -1;
  *lat = 1;
  switch (in->op) {
    case I_CONST: case I_NOP:
      *lat = 0;
      return 0;
    case I_ADD: case I_SUB: case I_AND: case I_OR: case I_XOR:
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTL: case I_ROTR:
    case I_CLZ: case I_BSWAP:
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_WRAP: case I_EXTENDS: case I_EXTENDU:
    case I_EXT8S: case I_EXT16S: case I_EXT32S:
      return 1;
    case I_SELECT:
      return t == VT_V128 ? 3 : 1;
    case I_CTZ:
      *lat = 2;
      return 2;
    case I_MUL: case I_MULHU:
      *lat = 3;
      return 2;
    case I_POPCNT:
      *lat = 8;
      return 4;
    case I_FABS: case I_FNEG: case I_REINTERPRET:
      *lat = 2;
      return 1;
    /* The float pipes are fewer than the integer ones, and a diamond of
       float arithmetic is mostly the same polynomial on two inputs:
       asbench/mathf's sin and cos, run twice for a sign that holds for a
       stretch of the sweep at a time, +1.9% instructions at one each. */
    case I_FADD: case I_FSUB: case I_FMUL: case I_FMIN: case I_FMAX:
    case I_FCOPYSIGN:
    case I_FCEIL: case I_FFLOOR: case I_FTRUNC: case I_FNEAREST:
    case I_CONVERTS: case I_CONVERTU: case I_DEMOTE: case I_PROMOTE:
    case I_TRUNC_SATS: case I_TRUNC_SATU:
      *lat = 3;
      return 2;
    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE:
      *lat = 2;
      return 1;
    case I_GLOBAL_GET:
      *lat = 4;
      return 1;
    case I_DIVS: case I_DIVU: case I_REMS: case I_REMU:
      /* by a constant that is neither 0 nor, signed, -1 */
      if (lh_can_trap(f, v)) return -1;
      *lat = 10;
      return 4;
    default:
      if (in->op >= I_VADD && in->op <= I_VDOTI7ADD && ir_is_pure(in)) {
        *lat = 3;
        return 1;
      }
      return -1;
  }
}

typedef struct {
  uint32_t *uses; uint32_t ucap;   /* reads of each value */
  uint32_t *dep; uint32_t dcap;    /* ifc_depth's, IFC_UNSEEN before */
  uint32_t *leaves;                /* arms of the source each block is made of */
} IfcCtx;

#define IFC_UNSEEN 0xffffffffu
#define IFC_LOADED 0x80000000u

static void ifc_grow(IfcCtx *cx, uint32_t n) {
  if (n <= cx->ucap) return;
  uint32_t c = cx->ucap * 2 > n ? cx->ucap * 2 : n;
  cx->uses = xrealloc(cx->uses, c * sizeof(uint32_t));
  memset(cx->uses + cx->ucap, 0, (c - cx->ucap) * sizeof(uint32_t));
  cx->ucap = c;
}

/* The cycles v is ready after the block parameters it is computed from --
   a loop's header parameters start each of its iterations -- with
   IFC_LOADED set when a load in a loop is on the way.  The instructions
   speculated keep theirs wherever they move. */
static uint32_t ifc_depth(IrFunc *f, IfcCtx *cx, Ref v, int lim) {
  v = ir_resolve(f, v);
  if (v >= cx->dcap) {
    uint32_t c = f->ninsts + f->ninsts / 2 + 64;
    cx->dep = xrealloc(cx->dep, c * sizeof(uint32_t));
    memset(cx->dep + cx->dcap, 0xff, (c - cx->dcap) * sizeof(uint32_t));
    cx->dcap = c;
  }
  if (cx->dep[v] != IFC_UNSEEN) return cx->dep[v];
  Inst *in = &f->insts[v];
  if (in->op == I_PARAM || in->op == I_CONST || lim == 0) return 0;
  uint32_t d = 0, ld = 0;
  for (uint32_t j = 0; j < in->nargs; j++) {
    uint32_t x = ifc_depth(f, cx, ir_args(f, v)[j], lim - 1);
    ld |= x & IFC_LOADED;
    if ((x & ~IFC_LOADED) > d) d = x & ~IFC_LOADED;
  }
  uint32_t lat;
  if (ifc_cost(f, v, &lat) < 0) lat = rd_latency(in->op);
  switch (in->op) {
    case I_LOAD: case I_VLOAD: case I_FIELD_GET: case I_ARRAY_GET:
    case I_ARRAY_LEN: case I_TABLE_GET:
      lat = 4;
      if (f->blocks[in->block].loop_depth) ld = IFC_LOADED;
      break;
    default:
      break;
  }
  if (d + lat < IFC_LOADED) d += lat;
  cx->dep[v] = d | ld;
  return cx->dep[v];
}

/* Whether x is an arm of the branch ending b, and the block it joins. */
static bool ifc_arm(IrFunc *f, uint32_t b, uint32_t x, uint32_t *join) {
  Block *xb = &f->blocks[x];
  if (x == b || x == f->entry || xb->is_pad || xb->cold) return false;
  if (xb->npreds != 1 || xb->preds[0] != b || xb->nparams) return false;
  if (xb->term != T_JUMP || xb->nsuccs != 1 || xb->succs[0].eh) return false;
  *join = xb->succs[0].target;
  return true;
}

static void ifc_drop_pred(Block *t, uint32_t p) {
  for (uint32_t i = 0; i < t->npreds; i++)
    if (t->preds[i] == p) {
      t->preds[i] = t->preds[--t->npreds];
      return;
    }
}

static void ifc_kill(Block *x) {
  x->reachable = false;
  x->ninsts = 0;
  x->nsuccs = 0;
  x->npreds = 0;
  x->nparams = 0;
  x->nret = 0;
  x->term = T_NONE;
}

static void ifc_append(IrFunc *f, uint32_t bi, Ref v) {
  Block *b = &f->blocks[bi];
  if (b->ninsts == b->cap_insts) {
    uint32_t oc_ = b->cap_insts;
    b->cap_insts = b->cap_insts ? b->cap_insts * 2 : 8;
    b->insts = ir_grow(f, b->insts, (size_t)oc_ * sizeof(uint32_t),
                       (size_t)b->cap_insts * sizeof(uint32_t));
  }
  b->insts[b->ninsts++] = v;
  f->insts[v].block = bi;
}

static bool ifc_is_compare(uint16_t op) {
  return (op >= I_EQ && op <= I_EQZ) || (op >= I_FEQ && op <= I_FGE);
}

/* A join that only bi enters any more is bi's own code: its parameters are
   what bi passes, its instructions and its way out bi's. */
static bool ifc_merge_join(IrFunc *f, uint32_t bi, IfcCtx *cx, bool *subst) {
  Block *b = &f->blocks[bi];
  if (b->term != T_JUMP || b->nsuccs != 1) return false;
  uint32_t ji = b->succs[0].target;
  Block *jb = &f->blocks[ji];
  if (ji == bi || ji == f->entry || jb->is_pad || jb->npreds != 1 ||
      jb->preds[0] != bi || ir_block_has_eh(f, ji) ||
      b->succs[0].nargs != jb->nparams)
    return false;
  for (uint32_t p = 0; p < jb->nparams; p++) {
    Ref par = jb->params[p], to = ir_resolve(f, b->succs[0].args[p]);
    ifc_grow(cx, f->ninsts);
    cx->uses[to] += cx->uses[par];
    ir_replace(f, par, to);
    *subst = true;
  }
  for (uint32_t i = 0; i < jb->ninsts; i++) ifc_append(f, bi, jb->insts[i]);
  b->nsuccs = 0;
  b->term = jb->term;
  b->term_arg = jb->term_arg;
  b->term_imm = jb->term_imm;
  b->ret_args = jb->ret_args;
  b->nret = jb->nret;
  b->cap_ret = jb->cap_ret;
  jb->ret_args = NULL;
  jb->cap_ret = 0;
  for (uint32_t s = 0; s < jb->nsuccs; s++) {
    Edge *e = &jb->succs[s];
    ir_add_edge(f, bi, e->target, e->args, e->nargs);
    ifc_drop_pred(&f->blocks[e->target], ji);
  }
  ifc_kill(jb);
  cx->leaves[bi] += cx->leaves[ji];
  return true;
}

static bool ifc_reaches(IrFunc *f, Ref v, const Ref *hp, uint32_t n, int lim) {
  v = ir_resolve(f, v);
  for (uint32_t i = 0; i < n; i++)
    if (v == hp[i]) return true;
  Inst *in = &f->insts[v];
  if (lim == 0 || in->op == I_PARAM || in->op == I_CONST) return false;
  for (uint32_t j = 0; j < in->nargs; j++)
    if (ifc_reaches(f, ir_args(f, v)[j], hp, n, lim - 1)) return true;
  return false;
}

/* Whether what join ji's parameter p receives goes round a loop into a
   header parameter that cond is computed from: either ji is the header, or
   it passes p, or a value it makes of p, along a back edge.  A select there
   is on the loop's own recurrence, and its condition's latency is paid
   every time round, where a guessed branch would not wait for it. */
static bool ifc_recurrence(IrFunc *f, uint32_t ji, uint32_t p, Ref cond) {
  Block *jb = &f->blocks[ji];
  Ref hp[8], der[16];
  uint32_t n = 0, nd = 0;
  for (uint32_t i = 0; i < jb->npreds; i++)
    if (f->blocks[jb->preds[i]].rpo >= jb->rpo) { hp[n++] = jb->params[p]; break; }
  der[nd++] = jb->params[p];
  for (uint32_t i = 0; i < jb->ninsts && nd < 16; i++) {
    Ref v = jb->insts[i];
    Inst *in = &f->insts[v];
    for (uint32_t j = 0; j < in->nargs; j++) {
      Ref a = ir_resolve(f, ir_args(f, v)[j]);
      uint32_t k = 0;
      while (k < nd && der[k] != a) k++;
      if (k < nd) { der[nd++] = v; break; }
    }
  }
  for (uint32_t s = 0; s < jb->nsuccs; s++) {
    Block *tb = &f->blocks[jb->succs[s].target];
    if (tb->rpo > jb->rpo) continue;
    for (uint32_t a = 0; a < jb->succs[s].nargs && a < tb->nparams && n < 8; a++) {
      Ref v = ir_resolve(f, jb->succs[s].args[a]);
      for (uint32_t k = 0; k < nd; k++)
        if (der[k] == v) { hp[n++] = tb->params[a]; break; }
    }
  }
  return n && ifc_reaches(f, cond, hp, n, 8);
}

#define IFC_REJ(why) do { if (g_ifc.dbg > 1) \
    fprintf(stderr, "ifc %s b%u reject: %s (depth %u)\n", \
            f->m->funcs[f->func_index].sym, bi, why, b->loop_depth); \
    return false; } while (0)

static bool ifc_try(IrFunc *f, uint32_t bi, IfcCtx *cx) {
  Block *b = &f->blocks[bi];
  if (!b->reachable || b->term != T_BRIF || b->nsuccs != 2) return false;
  if (b->succs[0].eh || b->succs[1].eh) return false;
  uint32_t s0 = b->succs[0].target, s1 = b->succs[1].target, j0 = 0, j1 = 0;
  uint32_t x[2] = { REF_NONE, REF_NONE }, ji;
  bool a0 = b->succs[0].nargs == 0 && ifc_arm(f, bi, s0, &j0);
  bool a1 = b->succs[1].nargs == 0 && ifc_arm(f, bi, s1, &j1);
  if (a0 && a1 && j0 == j1) { x[0] = s0; x[1] = s1; ji = j0; }
  else if (a0 && j0 == s1) { x[0] = s0; ji = s1; }
  else if (a1 && j1 == s0) { x[1] = s1; ji = s0; }
  else if (s0 == s1) ji = s0;
  else return false;
  Block *jb = &f->blocks[ji];
  if (ji == bi || jb->is_pad) IFC_REJ("join");
  /* the edges into the join: through the arm, or straight from b */
  Edge *e[2];
  for (int k = 0; k < 2; k++)
    e[k] = x[k] != REF_NONE ? &f->blocks[x[k]].succs[0] : &b->succs[k];
  uint32_t np = jb->nparams;
  if (e[0]->nargs != np || e[1]->nargs != np) IFC_REJ("arity");

  Ref cond = ir_resolve(f, b->term_arg);
  int cost[2] = { 0, 0 };
  uint32_t leaves[2] = { 1, 1 };
  for (int k = 0; k < 2; k++) {
    if (x[k] == REF_NONE) continue;
    Block *xb = &f->blocks[x[k]];
    uint32_t n = 0;
    for (uint32_t i = 0; i < xb->ninsts; i++) {
      Ref v = xb->insts[i];
      uint32_t lat;
      int c = ifc_cost(f, v, &lat);
      if (c < 0) {
        if (g_ifc.dbg > 1)
          fprintf(stderr, "ifc %s b%u reject: arm op %s (depth %u)\n",
                  f->m->funcs[f->func_index].sym, bi,
                  ir_op_name(f->insts[v].op), b->loop_depth);
        return false;
      }
      cost[k] += c;
      if (c && ++n > IFC_ARM_MAX) IFC_REJ("arm too long");
    }
    leaves[k] = cx->leaves[x[k]];
    if (cost[k] > g_ifc.arm * (int)leaves[k]) IFC_REJ("arm cost");
  }

  /* one select for each pair of values the two edges pass, made below */
  Ref pt[16], pf[16];
  ValType pty[16];
  uint32_t nsel = 0, dcl = ifc_depth(f, cx, cond, IFC_DEPTH_LIM);
  uint32_t dc = dcl & ~IFC_LOADED;
  for (uint32_t p = 0; p < np; p++) {
    Ref tv = ir_resolve(f, e[0]->args[p]), fv = ir_resolve(f, e[1]->args[p]);
    if (tv == fv) continue;
    ValType t = (ValType)f->insts[jb->params[p]].type;
    if (t != VT_I32 && t != VT_I64 && t != VT_F32 && t != VT_F64 && t != VT_V128)
      IFC_REJ("reference join");
    /* A join that branches again on what the two edges passed is a branch
       on a branch, which jump threading takes apart where it can; a select
       there makes the second branch wait on the first's condition where
       both were guessed.  Go's compiler materialises every && and || so,
       three quarters of stdgo's conversions before this test, and
       ktbench/arrobj's `i >= 0 && ...` ran 1.05x with the select. */
    if (jb->term == T_BRIF && ir_resolve(f, jb->term_arg) == jb->params[p])
      IFC_REJ("join branches on it");
    uint32_t q = 0;
    while (q < nsel && !(pt[q] == tv && pf[q] == fv)) q++;
    if (q < nsel) continue;
    if ((int)nsel >= g_ifc.sel) IFC_REJ("selects");
    uint32_t dt = ifc_depth(f, cx, tv, IFC_DEPTH_LIM) & ~IFC_LOADED;
    uint32_t df = ifc_depth(f, cx, fv, IFC_DEPTH_LIM) & ~IFC_LOADED;
    uint32_t lo = dt < df ? dt : df, hi = dt < df ? df : dt;
    /* a condition a load in the loop decides, picking between values
       that do not wait for it */
    if (b->loop_depth && (dcl & IFC_LOADED) && dc > hi + IFC_LOAD_SLACK)
      IFC_REJ("condition waits on a load");
    /* a condition computed from what the select hands round the loop, and
       later than the values: hsbench/fusion's Collatz step, n even ? n / 2
       : 3n + 1, whose parity test is four of the cycles each step */
    if (b->loop_depth && dc > hi && ifc_recurrence(f, ji, p, cond))
      IFC_REJ("condition is the loop's own");
    if (dc > hi) hi = dc;
    if (hi + 1 - lo > (uint32_t)g_ifc.crit) IFC_REJ("latency");
    pt[nsel] = tv; pf[nsel] = fv; pty[nsel] = t; nsel++;
  }

  /* the selects are work both ways run: a triangle's arm pays for them out
     of its own budget, a diamond's two out of the two together */
  if (cost[0] + cost[1] + (int)nsel >
      g_ifc.arm * (int)((x[0] != REF_NONE ? leaves[0] : 0) + (x[1] != REF_NONE ? leaves[1] : 0) +
                        (x[0] == REF_NONE && x[1] == REF_NONE ? 1 : 0)))
    IFC_REJ("cost with selects");

  if (g_ifc.dbg)
    fprintf(stderr, "ifc %s b%u: %s arms %d/%d leaves %u/%u sels %u depth %u\n",
            f->m->funcs[f->func_index].sym, bi,
            x[0] != REF_NONE && x[1] != REF_NONE ? "diamond" :
            x[0] != REF_NONE || x[1] != REF_NONE ? "triangle" : "edges",
            cost[0], cost[1], leaves[0], leaves[1], nsel, b->loop_depth);

  /* the compare only the branch read goes down to the selects */
  ifc_grow(cx, f->ninsts);
  bool mv = cx->uses[cond] == 1 && f->insts[cond].block == bi &&
            ifc_is_compare(f->insts[cond].op) && nsel;
  if (mv) {
    uint32_t k = 0;
    for (uint32_t i = 0; i < b->ninsts; i++)
      if (b->insts[i] != cond) b->insts[k++] = b->insts[i];
    b->ninsts = k;
  }
  for (int k = 0; k < 2; k++) {
    if (x[k] == REF_NONE) continue;
    Block *xb = &f->blocks[x[k]];
    for (uint32_t i = 0; i < xb->ninsts; i++) ifc_append(f, bi, xb->insts[i]);
  }
  if (mv) ifc_append(f, bi, cond);
  Ref *na = xmalloc((np ? np : 1) * sizeof(Ref)), sel[16];
  for (uint32_t q = 0; q < nsel; q++) {
    Ref sa[3] = { pt[q], pf[q], cond };
    sel[q] = ir_emit(f, bi, I_SELECT, pty[q], sa, 3, 0, 0);
    ifc_grow(cx, f->ninsts);
    cx->uses[pt[q]]++; cx->uses[pf[q]]++; cx->uses[cond]++;
  }
  for (uint32_t p = 0; p < np; p++) {
    Ref tv = ir_resolve(f, e[0]->args[p]), fv = ir_resolve(f, e[1]->args[p]);
    na[p] = tv;
    for (uint32_t q = 0; q < nsel && tv != fv; q++)
      if (pt[q] == tv && pf[q] == fv) na[p] = sel[q];
  }
  cx->uses[cond]--;
  for (int k = 0; k < 2; k++) {
    ifc_drop_pred(jb, x[k] != REF_NONE ? x[k] : bi);
    if (x[k] != REF_NONE) ifc_kill(&f->blocks[x[k]]);
  }
  b->nsuccs = 0;
  b->term = T_JUMP;
  ir_add_edge(f, bi, ji, na, np);
  xfree(na);
  /* the arms of the source now in bi: a triangle's empty side is none,
     so a chain of early exits -- LLVM's unrolled remainder loop, each step
     leaving for the join -- does not earn a budget a step at a time */
  cx->leaves[bi] = (x[0] != REF_NONE ? leaves[0] : 0) + (x[1] != REF_NONE ? leaves[1] : 0);
  if (!cx->leaves[bi]) cx->leaves[bi] = 1;
  g_stats.branch_simplified++;
  return true;
}

#undef IFC_REJ

static bool pass_ifconvert(IrFunc *f) {
  pthread_once(&g_ifc_once, ifc_init);
  if (g_ifc.off || (g_ifc.only >= 0 && f->func_index != (uint32_t)g_ifc.only))
    return false;
  IfcCtx cx = { 0 };
  bool changed = false, subst = false;
  for (uint32_t oi = f->norder; oi-- > 0;) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (!b->reachable || b->term != T_BRIF || ir_block_has_eh(f, bi)) continue;
    if (!cx.uses) {
      cx.uses = count_uses(f);
      cx.ucap = f->ninsts ? f->ninsts : 1;
      cx.dcap = f->ninsts + 64;
      cx.dep = xmalloc(cx.dcap * sizeof(uint32_t));
      memset(cx.dep, 0xff, cx.dcap * sizeof(uint32_t));
      cx.leaves = xmalloc((f->nblocks ? f->nblocks : 1) * sizeof(uint32_t));
      for (uint32_t i = 0; i < f->nblocks; i++) cx.leaves[i] = 1;
    }
    for (int r = 0; r < 8 && ifc_try(f, bi, &cx); r++) {
      changed = true;
      if (!ifc_merge_join(f, bi, &cx, &subst)) break;
    }
  }
  if (cx.uses) {
    xfree(cx.uses); xfree(cx.dep); xfree(cx.leaves);
  }
  if (!changed) return false;
  if (subst) ir_apply_subst(f);
  ir_recompute_preds(f);
  ir_order(f);
  return true;
}

static Magic magic_u32(uint32_t d) {
  Magic mg = { 0, 0, 0 };
  uint32_t nc, delta, q1, r1, q2, r2;
  int p;
  nc = (uint32_t)(-1) - (uint32_t)(-(int64_t)d) % d;
  p = 31;
  q1 = 0x80000000u / nc;      r1 = 0x80000000u - q1 * nc;
  q2 = 0x7fffffffu / d;       r2 = 0x7fffffffu - q2 * d;
  do {
    p++;
    if (r1 >= nc - r1) { q1 = 2 * q1 + 1; r1 = 2 * r1 - nc; }
    else               { q1 = 2 * q1;     r1 = 2 * r1; }
    if (r2 + 1 >= d - r2) {
      if (q2 >= 0x7fffffffu) mg.add = 1;
      q2 = 2 * q2 + 1; r2 = 2 * r2 + 1 - d;
    } else {
      if (q2 >= 0x80000000u) mg.add = 1;
      q2 = 2 * q2; r2 = 2 * r2 + 1;
    }
    delta = d - 1 - r2;
  } while (p < 64 && (q1 < delta || (q1 == delta && r1 == 0)));
  mg.m = (uint32_t)(q2 + 1);
  mg.sh = p - 32;
  return mg;
}

static Magic magic_u64(uint64_t d) {
  Magic mg = { 0, 0, 0 };
  uint64_t nc, delta, q1, r1, q2, r2;
  const uint64_t two63 = (uint64_t)1 << 63;
  int p;
  nc = ~(uint64_t)0 - (uint64_t)(-(int64_t)d) % d;
  p = 63;
  q1 = two63 / nc;            r1 = two63 - q1 * nc;
  q2 = (two63 - 1) / d;       r2 = (two63 - 1) - q2 * d;
  do {
    p++;
    if (r1 >= nc - r1) { q1 = 2 * q1 + 1; r1 = 2 * r1 - nc; }
    else               { q1 = 2 * q1;     r1 = 2 * r1; }
    if (r2 + 1 >= d - r2) {
      if (q2 >= two63 - 1) mg.add = 1;
      q2 = 2 * q2 + 1; r2 = 2 * r2 + 1 - d;
    } else {
      if (q2 >= two63) mg.add = 1;
      q2 = 2 * q2; r2 = 2 * r2 + 1;
    }
    delta = d - 1 - r2;
  } while (p < 128 && (q1 < delta || (q1 == delta && r1 == 0)));
  mg.m = q2 + 1;
  mg.sh = p - 64;
  return mg;
}

Magic opt_magic_unsigned(uint64_t d, bool wide) {
  Magic none = { 0, 0, 0 };
  if (!(wide ? d : (uint32_t)d)) return none;
  return wide ? magic_u64(d) : magic_u32((uint32_t)d);
}

/* Signed magic numbers (Hacker's Delight, figure 10-1) for 2 <= |d| <
   2^(w-1), d not a power of two: q = mulhs(n, m), plus n where d > 0 > m
   and minus n where d < 0 < m, shifted right by sh, plus one where that is
   negative.  m is the w-bit pattern. */
static Magic magic_s32(int32_t d) {
  Magic mg = { 0, 0, 0 };
  const uint32_t two31 = 0x80000000u;
  uint32_t ad = d < 0 ? 0u - (uint32_t)d : (uint32_t)d;
  uint32_t t = two31 + ((uint32_t)d >> 31);
  uint32_t anc = t - 1 - t % ad;
  uint32_t q1 = two31 / anc, r1 = two31 - q1 * anc;
  uint32_t q2 = two31 / ad, r2 = two31 - q2 * ad, delta;
  int p = 31;
  do {
    p++;
    q1 *= 2; r1 *= 2;
    if (r1 >= anc) { q1++; r1 -= anc; }
    q2 *= 2; r2 *= 2;
    if (r2 >= ad) { q2++; r2 -= ad; }
    delta = ad - r2;
  } while (q1 < delta || (q1 == delta && r1 == 0));
  mg.m = d < 0 ? 0u - (q2 + 1) : q2 + 1;
  mg.sh = p - 32;
  return mg;
}

static Magic magic_s64(int64_t d) {
  Magic mg = { 0, 0, 0 };
  const uint64_t two63 = (uint64_t)1 << 63;
  uint64_t ad = d < 0 ? 0u - (uint64_t)d : (uint64_t)d;
  uint64_t t = two63 + ((uint64_t)d >> 63);
  uint64_t anc = t - 1 - t % ad;
  uint64_t q1 = two63 / anc, r1 = two63 - q1 * anc;
  uint64_t q2 = two63 / ad, r2 = two63 - q2 * ad, delta;
  int p = 63;
  do {
    p++;
    q1 *= 2; r1 *= 2;
    if (r1 >= anc) { q1++; r1 -= anc; }
    q2 *= 2; r2 *= 2;
    if (r2 >= ad) { q2++; r2 -= ad; }
    delta = ad - r2;
  } while (q1 < delta || (q1 == delta && r1 == 0));
  mg.m = d < 0 ? 0u - (q2 + 1) : q2 + 1;
  mg.sh = p - 64;
  return mg;
}

static Ref emit_magic_udiv(IrFunc *f, uint32_t bi, uint32_t *idx, Ref n,
                           ValType t, uint64_t d) {
  bool wide = vt_is_64(t);
  Magic mg = opt_magic_unsigned(d, wide);
  if (!wide && mg.add) {
    /* A 33-bit magic number needs the add fix-up, four steps after the
       multiply.  A 32-bit n and d have one that needs none in 64 bits:
       with M = floor(2^64 / d) + 1 = (2^64 + e) / d, 0 < e < d,
       n * M / 2^64 = n / d + n * e / (d * 2^64), and n * e < 2^64 keeps the
       excess below 1 / d, too little to reach the next multiple. */
    Ref n64 = emit_before(f, bi, idx, I_EXTENDU, VT_I64, &n, 1, 0);
    Ref mc = emit_before(f, bi, idx, I_CONST, VT_I64, NULL, 0, ~(uint64_t)0 / d + 1);
    Ref ma[2] = { n64, mc };
    Ref q = emit_before(f, bi, idx, I_MULHU, VT_I64, ma, 2, 0);
    return emit_before(f, bi, idx, I_WRAP, VT_I32, &q, 1, 0);
  }
  Ref mc = emit_before(f, bi, idx, I_CONST, t, NULL, 0, mg.m);
  Ref ma[2] = { n, mc };
  Ref q = emit_before(f, bi, idx, I_MULHU, t, ma, 2, 0);
  int sh = mg.sh;
  if (mg.add) {
    Ref sa[2] = { n, q };
    Ref diff = emit_before(f, bi, idx, I_SUB, t, sa, 2, 0);
    Ref one = emit_before(f, bi, idx, I_CONST, t, NULL, 0, 1);
    Ref sh1a[2] = { diff, one };
    Ref half = emit_before(f, bi, idx, I_SHRU, t, sh1a, 2, 0);
    Ref aa[2] = { half, q };
    q = emit_before(f, bi, idx, I_ADD, t, aa, 2, 0);
    sh -= 1;
  }
  if (sh > 0) {
    Ref sc = emit_before(f, bi, idx, I_CONST, t, NULL, 0, (uint64_t)sh);
    Ref qa[2] = { q, sc };
    q = emit_before(f, bi, idx, I_SHRU, t, qa, 2, 0);
  }
  return q;
}

static Ref emit_magic_sdiv(IrFunc *f, uint32_t bi, uint32_t *idx, Ref n,
                           ValType t, uint64_t dc) {
  bool wide = vt_is_64(t);
  int64_t d = wide ? (int64_t)dc : (int64_t)(int32_t)(uint32_t)dc;
  Magic mg = wide ? magic_s64(d) : magic_s32((int32_t)d);
  int64_t m = wide ? (int64_t)mg.m : (int64_t)(int32_t)(uint32_t)mg.m;
  Ref mc = emit_before(f, bi, idx, I_CONST, t, NULL, 0, mg.m);
  Ref ma[2] = { n, mc };
  Ref q = emit_before(f, bi, idx, I_MULHS, t, ma, 2, 0);
  if ((d > 0 && m < 0) || (d < 0 && m > 0)) {
    Ref fa[2] = { q, n };
    q = emit_before(f, bi, idx, d > 0 ? I_ADD : I_SUB, t, fa, 2, 0);
  }
  if (mg.sh > 0) {
    Ref sc = emit_before(f, bi, idx, I_CONST, t, NULL, 0, (uint64_t)mg.sh);
    Ref sa[2] = { q, sc };
    q = emit_before(f, bi, idx, I_SHRS, t, sa, 2, 0);
  }
  Ref wc = emit_before(f, bi, idx, I_CONST, t, NULL, 0, wide ? 63 : 31);
  Ref ga[2] = { q, wc };
  Ref neg = emit_before(f, bi, idx, I_SHRU, t, ga, 2, 0);
  Ref aa[2] = { q, neg };
  return emit_before(f, bi, idx, I_ADD, t, aa, 2, 0);
}

/* n >= d for a divisor of 2^(w-1) or more, which goes into n at most once,
   as a value of n's type. */
static Ref emit_geu_const(IrFunc *f, uint32_t bi, uint32_t *idx, Ref n,
                          ValType t, uint64_t d) {
  Ref dc = emit_before(f, bi, idx, I_CONST, t, NULL, 0, d);
  Ref ca[2] = { n, dc };
  Ref c = emit_before(f, bi, idx, I_GEU, VT_I32, ca, 2, 0);
  return t == VT_I64 ? emit_before(f, bi, idx, I_EXTENDU, VT_I64, &c, 1, 0) : c;
}

static bool is_pow2(uint64_t v, int *log2out) {
  if (v == 0 || (v & (v - 1))) return false;
  int n = 0;
  while ((v >> n) != 1) n++;
  *log2out = n;
  return true;
}

/* The compare that takes its operands the other way round. */
static uint16_t cmp_mirror(uint16_t op) {
  switch (op) {
    case I_LTS: return I_GTS;  case I_GTS: return I_LTS;
    case I_LES: return I_GES;  case I_GES: return I_LES;
    case I_LTU: return I_GTU;  case I_GTU: return I_LTU;
    case I_LEU: return I_GEU;  case I_GEU: return I_LEU;
    default: return 0;
  }
}

static uint16_t cmp_inverse(uint16_t op) {
  switch (op) {
    case I_EQ:  return I_NE;   case I_NE:  return I_EQ;
    case I_LTS: return I_GES;  case I_GES: return I_LTS;
    case I_GTS: return I_LES;  case I_LES: return I_GTS;
    case I_LTU: return I_GEU;  case I_GEU: return I_LTU;
    case I_GTU: return I_LEU;  case I_LEU: return I_GTU;
    default: return 0;
  }
}

/* x plus 2^k - 1 where x is negative: arithmetic shifted right by k, the
   quotient rounded toward zero. */
static Ref emit_divs_pow2_sum(IrFunc *f, uint32_t bi, uint32_t *idx, Ref x,
                              ValType t, int k) {
  int w = (t == VT_I64) ? 64 : 32;
  Ref bias;
  if (k == 1) {

    Ref c = emit_before(f, bi, idx, I_CONST, t, NULL, 0, (uint64_t)(w - 1));
    Ref args[2] = { x, c };
    bias = emit_before(f, bi, idx, I_SHRU, t, args, 2, 0);
  } else {
    Ref cw = emit_before(f, bi, idx, I_CONST, t, NULL, 0, (uint64_t)(w - 1));
    Ref sargs[2] = { x, cw };
    Ref sign = emit_before(f, bi, idx, I_SHRS, t, sargs, 2, 0);
    Ref ck = emit_before(f, bi, idx, I_CONST, t, NULL, 0, (uint64_t)(w - k));
    Ref bargs[2] = { sign, ck };
    bias = emit_before(f, bi, idx, I_SHRU, t, bargs, 2, 0);
  }
  Ref aargs[2] = { x, bias };
  return emit_before(f, bi, idx, I_ADD, t, aargs, 2, 0);
}

static void strength_divs_pow2(IrFunc *f, uint32_t bi, uint32_t *idx, Ref v, int k) {
  ValType t = (ValType)f->insts[v].type;
  Ref sum = emit_divs_pow2_sum(f, bi, idx, ir_args(f, v)[0], t, k);
  Ref shc = emit_before(f, bi, idx, I_CONST, t, NULL, 0, (uint64_t)k);

  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  a[0] = sum; a[1] = shc;
  in->op = I_SHRS;
}

/* y ^ (y >> k), where y is x ^ (x >> k) and 2k is at least the width, is x:
   the second shift sees only bits of x that the first xor left alone, so the
   two xors cancel.  Logical shifts either way (an arithmetic one fills with
   the sign, which does not cancel).  murmur's fmix64 begins and ends with
   x ^= x >> 33, so iterating it -- a hash of a hash, as gobench/mix does --
   applies that step twice in a row, two cycles of a latency chain each time. */
static Ref xorshift_undone(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  uint64_t w = vt_is_64((ValType)in->type) ? 64 : 32;
  for (int s = 0; s < 2; s++) {
    Ref y = a[s], sh = a[s ^ 1];
    Inst *si = &f->insts[sh];
    uint64_t k, k2;
    if ((si->op != I_SHRU && si->op != I_SHL) || ir_args(f, sh)[0] != y ||
        !is_const(f, ir_args(f, sh)[1], &k) || 2 * (k % w) < w)
      continue;
    Inst *yi = &f->insts[y];
    if (yi->op != I_XOR || yi->type != in->type) continue;
    Ref *ya = ir_args(f, y);
    for (int t = 0; t < 2; t++) {
      Ref x = ya[t], sh2 = ya[t ^ 1];
      Inst *s2 = &f->insts[sh2];
      if (s2->op == si->op && ir_args(f, sh2)[0] == x &&
          is_const(f, ir_args(f, sh2)[1], &k2) && k2 % w == k % w)
        return x;
    }
  }
  return REF_NONE;
}

static bool is_commutative(uint16_t op) {
  return op == I_ADD || op == I_MUL || op == I_AND || op == I_OR ||
         op == I_XOR || op == I_EQ || op == I_NE;
}
static bool is_associative(uint16_t op) {
  return op == I_ADD || op == I_MUL || op == I_AND || op == I_OR || op == I_XOR;
}

static bool gc_fold_known(IrFunc *f, Ref v);

/* Whether v leaves block bi as a successor argument. */
static bool passed_on(const IrFunc *f, uint32_t bi, Ref v) {
  const Block *b = &f->blocks[bi];
  for (uint32_t s = 0; s < b->nsuccs; s++)
    for (uint32_t j = 0; j < b->succs[s].nargs; j++)
      if (b->succs[s].args[j] == v) return true;
  return false;
}

/* Whether a test may read the operands of in0 in place of in0 itself: always
   when nothing else needs in0, and when something does only within its block
   and not when in0 is what the block hands on, so that i + 1 < n stays a test
   of the counter the loop carries -- the vectoriser and the unroller look for
   that -- and the operands live a few instructions longer at most. */
static bool test_may_look_through(const IrFunc *f, uint32_t bi, Ref in0,
                                  const uint32_t *uses) {
  if (!uses) return false;
  if (uses[in0] == 1) return true;
  return f->insts[in0].block == bi && !passed_on(f, bi, in0);
}

/* A shift reads only the low log2(w) bits of its amount, so masking them
   first, as Rust's wrapping shifts and C's rotates do, does nothing. */
static bool fold_shift_mask(IrFunc *f, Ref v) {
  Ref *a = ir_args(f, v);
  const Inst *ai = &f->insts[a[1]];
  uint64_t m, w = vt_is_64((ValType)f->insts[v].type) ? 64u : 32u;
  if (ai->op != I_AND || ai->nargs != 2 || !is_const(f, ir_args(f, a[1])[1], &m))
    return false;
  if ((m & (w - 1)) != w - 1) return false;
  a[1] = ir_args(f, a[1])[0];
  return true;
}

/* x + (0 - y) is x - y, and x - (0 - y) is x + y. */
static bool fold_add_neg(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  for (int k = in->op == I_ADD ? 0 : 1; k < 2; k++) {
    const Inst *ni = &f->insts[a[k]];
    uint64_t z;
    if (ni->op != I_SUB || ni->nargs != 2 || !is_const(f, ir_args(f, a[k])[0], &z) ||
        trunc_to((ValType)in->type, z) != 0)
      continue;
    Ref y = ir_args(f, a[k])[1];
    if (k == 0) a[0] = a[1];
    a[1] = y;
    in->op = in->op == I_ADD ? I_SUB : I_ADD;
    return true;
  }
  return false;
}

/* A compare of two i32s widened the same way is the compare of the i32s:
   both sign-extended keep every order, and both zero-extended are never
   negative, so a signed test of them is the unsigned test of the i32s. */
static bool fold_cmp_widened(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  if (in->nargs != 2) return false;
  uint16_t e = f->insts[a[0]].op;
  if ((e != I_EXTENDU && e != I_EXTENDS) || f->insts[a[1]].op != e) return false;
  if (f->insts[a[0]].nargs != 1 || f->insts[a[1]].nargs != 1) return false;
  if (e == I_EXTENDU) {
    switch (in->op) {
      case I_LTS: in->op = I_LTU; break;
      case I_GTS: in->op = I_GTU; break;
      case I_LES: in->op = I_LEU; break;
      case I_GES: in->op = I_GEU; break;
      default: break;
    }
  }
  a[0] = ir_args(f, a[0])[0];
  a[1] = ir_args(f, a[1])[0];
  return true;
}

/* eq or ne of x + c1, x - c1 or x ^ c1 against c2 is the same test of x
   against one constant, and of x - y or x ^ y against zero the test of x
   against y: each is a bijection on the integers mod 2^n, so nothing is lost.
   When nothing else needs the arithmetic it goes; when something does, as
   with GHC's stack and heap pointers, the test still stops waiting on it,
   and 0 - y != 0 becomes a cbnz on y. */
static bool fold_cmp_arith(IrFunc *f, uint32_t bi, uint32_t *idx, Ref v,
                           const uint32_t *uses) {
  Ref *a = ir_args(f, v);
  uint64_t c2, c1;
  if (f->insts[v].nargs != 2 || !is_const(f, a[1], &c2)) return false;
  Ref in0 = a[0];
  const Inst *ii = &f->insts[in0];
  if (ii->nargs != 2 || (ii->op != I_ADD && ii->op != I_SUB && ii->op != I_XOR)) return false;
  if (!test_may_look_through(f, bi, in0, uses)) return false;
  ValType t = (ValType)ii->type;
  Ref x = ir_args(f, in0)[0], y = ir_args(f, in0)[1];
  if (is_const(f, y, &c1)) {
    uint64_t c = ii->op == I_ADD ? c2 - c1 : ii->op == I_SUB ? c2 + c1 : c2 ^ c1;
    Ref k = emit_before(f, bi, idx, I_CONST, t, NULL, 0, trunc_to(t, c));
    a = ir_args(f, v);
    a[0] = x;
    a[1] = k;
    return true;
  }
  if (trunc_to(t, c2) != 0 || ii->op == I_ADD) return false;
  a[0] = x;
  a[1] = y;
  return true;
}

/* eq or ne of a bool -- a value only bit 0 of which can be set -- against 0
   or 1: the bool itself, or its eqz. */
static bool fold_bool_cmp(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  uint64_t c;
  if (in->nargs != 2 || !is_const(f, a[1], &c) || c > 1) return false;
  if ((ir_known_bits(f, a[0], KB_DEPTH) & ~1ull) != 0) return false;
  bool same = (in->op == I_NE) == (c == 0);
  if (same) {
    if (f->insts[a[0]].type != VT_I32) return false;
    ir_replace(f, v, a[0]);
    in->op = I_NOP;
  } else {
    in->op = I_EQZ;
    in->nargs = 1;
  }
  return true;
}

/* eq or ne of a widened i32 against a constant the widening could have made
   is the same compare at i32, and ne(b, 0) of a bool b is b itself. */
static bool fold_ext_cmp(IrFunc *f, uint32_t bi, uint32_t *idx, Ref v) {
  Ref *a = ir_args(f, v);
  const Inst *si = &f->insts[a[0]];
  uint64_t c;
  if ((si->op != I_EXTENDU && si->op != I_EXTENDS) || si->nargs != 1) return false;
  if (!is_const(f, a[1], &c)) return false;
  if (si->op == I_EXTENDU ? (c >> 32) != 0 : (uint64_t)(int64_t)(int32_t)c != c)
    return false;
  Ref x = ir_args(f, a[0])[0];
  Inst *in = &f->insts[v];
  if (c == 0 && in->op == I_NE && (ir_known_bits(f, x, KB_DEPTH) & ~1ull) == 0) {
    ir_replace(f, v, x);
    in->op = I_NOP;
    return true;
  }
  if (c == 0 && in->op == I_EQ) {
    in->op = I_EQZ;
    in->nargs = 1;
    a[0] = x;
    return true;
  }
  Ref k = emit_before(f, bi, idx, I_CONST, VT_I32, NULL, 0, (uint32_t)c);
  a = ir_args(f, v);
  a[0] = x;
  a[1] = k;
  return true;
}

/* wrap(x op y) as the same op at 32 bits on wrap(x) and wrap(y), for the ops
   whose low 32 bits depend only on their operands' low 32 bits.  Go computes
   every stack address as wrap(extend(sp) + k), so the wraps and extends then
   cancel and a 64-bit add between two moves becomes one 32-bit add.  Only
   when at least one operand narrows for free, an extend or a constant, and
   the 64-bit op has no other use. */
static Ref narrow_free(IrFunc *f, uint32_t bi, uint32_t *idx, Ref r) {
  const Inst *ri = &f->insts[r];
  uint64_t c;
  if ((ri->op == I_EXTENDU || ri->op == I_EXTENDS) && ri->nargs == 1)
    return ir_args(f, r)[0];
  if (is_const(f, r, &c))
    return emit_before(f, bi, idx, I_CONST, VT_I32, NULL, 0, c & 0xffffffffu);
  return REF_NONE;
}

static bool fold_narrow(IrFunc *f, uint32_t bi, uint32_t *idx, Ref v) {
  Ref w = ir_args(f, v)[0];
  const Inst *wi = &f->insts[w];
  uint16_t op = wi->op;
  if (wi->nargs != 2 || wi->type != VT_I64) return false;
  if (op != I_ADD && op != I_SUB && op != I_MUL && op != I_AND && op != I_OR &&
      op != I_XOR && op != I_SHL) return false;
  Ref x = ir_args(f, w)[0], y = ir_args(f, w)[1];
  uint64_t c;
  if (op == I_SHL) {
    if (!is_const(f, y, &c) || (c & 63) >= 32) return false;
  } else {
    const Inst *xi = &f->insts[x], *yi = &f->insts[y];
    bool fx = xi->op == I_EXTENDU || xi->op == I_EXTENDS || xi->op == I_CONST;
    bool fy = yi->op == I_EXTENDU || yi->op == I_EXTENDS || yi->op == I_CONST;
    if (!fx && !fy) return false;
  }
  Ref nx = narrow_free(f, bi, idx, x);
  if (nx == REF_NONE) nx = emit_before(f, bi, idx, I_WRAP, VT_I32, &x, 1, 0);
  Ref ny = narrow_free(f, bi, idx, y);
  if (ny == REF_NONE) ny = emit_before(f, bi, idx, I_WRAP, VT_I32, &y, 1, 0);
  Ref na[2] = { nx, ny };
  Ref r = emit_before(f, bi, idx, (IrOp)op, VT_I32, na, 2, 0);
  ir_replace(f, v, r);
  f->insts[v].op = I_NOP;
  return true;
}

/* What x - (x - y), x + (y - x) or (x + y) - y cancels to, or REF_NONE. */
static Ref sub_cancels(IrFunc *f, uint16_t op, const Ref *a) {
  if (op == I_SUB) {
    const Inst *ai = &f->insts[a[0]];
    if (ai->op == I_ADD && ai->nargs == 2 && ai->type == f->insts[a[1]].type) {
      const Ref *aa = ir_args(f, a[0]);
      if (aa[1] == a[1]) return aa[0];
      if (aa[0] == a[1]) return aa[1];
    }
  }
  for (int k = 0; k < 2; k++) {
    if (op == I_SUB && k == 1) break;
    Ref inner = a[k ^ 1], outer = a[k];
    const Inst *ii = &f->insts[inner];
    if (ii->op != I_SUB || ii->nargs != 2 || ii->type != f->insts[outer].type) continue;
    const Ref *ia = ir_args(f, inner);
    if (op == I_SUB && ia[0] == outer) return ia[1];
    if (op == I_ADD && ia[1] == outer) return ia[0];
  }
  return REF_NONE;
}

/* ---- 64x64 multiplies written in 32-bit halves ----------------------------

   wasm has no high multiply, so a 128-bit product -- Rust's u128 and i128,
   C's and Zig's through __multi3, Go's bits.Mul64 -- arrives written in
   32-bit halves.  With xl, xh, yl, yh the halves of x and y, ll = xl*yl,
   hh = xh*yh and the cross products xh*yl and xl*yh are each exact in 64
   bits, and two shapes put them together.  Rust's compiler_builtins adds
   the cross products and catches the carries with compares, c being either
   of them:

     mid = xh*yl + xl*yh                       (mod 2^64)
     lo  = ll + (mid << 32)
     hi  = hh + ((zext(mid <u c) << 32) | (mid >> 32)) + zext(lo <u ll)

   compiler-rt's __mulddi3, Zig's and Go's add them one at a time, where
   nothing can carry ({c1, c2} the two cross products):

     t  = (ll >> 32) + c1
     u  = (t & 0xffffffff) + c2
     lo = (u << 32) | (ll & 0xffffffff)
     hi = hh + (t >> 32) + (u >> 32)

   Either way lo is x * y and hi is mulhu(x, y): a sum holding hi's terms
   becomes mulhu plus the rest of it, lo becomes a mul, and the halves die --
   fifteen instructions for umulh's one in Rust's u128 division and its i128
   dot product, which on wasm ran 7x native's.  A signed product is the
   unsigned one of sign-extended operands, so x*(y >>s 63) + y*(x >>s 63)
   beside mulhu(x, y) makes it mulhs(x, y): i64::saturating_mul. */
#define MW_TERMS 24

/* v is the i64 x & 0xffffffff, or the zero extension of x wrapped */
static bool mw_low(IrFunc *f, Ref v, Ref *x) {
  const Inst *in = &f->insts[v];
  if (in->type != VT_I64) return false;
  if (in->op == I_AND && in->nargs == 2) {
    const Ref *a = ir_args(f, v);
    uint64_t c;
    for (int k = 0; k < 2; k++)
      if (is_const(f, ir_resolve(f, a[k]), &c) && c == 0xffffffffu) {
        *x = ir_resolve(f, a[1 - k]);
        return true;
      }
    return false;
  }
  if (in->op == I_EXTENDU && in->nargs == 1) {
    Ref w = ir_resolve(f, ir_args(f, v)[0]);
    if (f->insts[w].op != I_WRAP || f->insts[w].nargs != 1) return false;
    *x = ir_resolve(f, ir_args(f, w)[0]);
    return f->insts[*x].type == VT_I64;
  }
  return false;
}

/* v is the i64 x >> k, k the constant given */
static bool mw_shr(IrFunc *f, Ref v, uint32_t k, IrOp op, Ref *x) {
  const Inst *in = &f->insts[v];
  uint64_t c;
  if (in->op != op || in->nargs != 2 || in->type != VT_I64) return false;
  const Ref *a = ir_args(f, v);
  if (!is_const(f, ir_resolve(f, a[1]), &c) || (c & 63) != k) return false;
  *x = ir_resolve(f, a[0]);
  return true;
}

/* v is the i64 x << 32 */
static bool mw_shl32(IrFunc *f, Ref v, Ref *x) {
  return mw_shr(f, v, 32, I_SHL, x);
}

/* the operands of the i64 binary op v, resolved */
static bool mw_bin(IrFunc *f, Ref v, IrOp op, Ref *l, Ref *r) {
  const Inst *in = &f->insts[v];
  if (in->op != op || in->nargs != 2 || in->type != VT_I64) return false;
  const Ref *a = ir_args(f, v);
  *l = ir_resolve(f, a[0]);
  *r = ir_resolve(f, a[1]);
  return true;
}

/* v = half of p times half of q, as 2 * (p's high half) + (q's high half),
   or -1.  For p = q the two cross products are one kind, 1. */
static int mw_prod(IrFunc *f, Ref v, Ref p, Ref q) {
  Ref m[2], x[2];
  int h[2];
  if (!mw_bin(f, v, I_MUL, &m[0], &m[1])) return -1;
  for (int k = 0; k < 2; k++) {
    if (mw_low(f, m[k], &x[k])) h[k] = 0;
    else if (mw_shr(f, m[k], 32, I_SHRU, &x[k])) h[k] = 1;
    else return -1;
  }
  int kind;
  if (x[0] == p && x[1] == q) kind = 2 * h[0] + h[1];
  else if (x[0] == q && x[1] == p) kind = 2 * h[1] + h[0];
  else return -1;
  return p == q && kind == 2 ? 1 : kind;
}

/* the two cross products of p and q, one of each kind (both kind 1 for a
   square) */
static bool mw_crosses(IrFunc *f, Ref a, Ref b, Ref p, Ref q) {
  int ka = mw_prod(f, a, p, q), kb = mw_prod(f, b, p, q);
  if (p == q) return ka == 1 && kb == 1;
  return (ka == 1 && kb == 2) || (ka == 2 && kb == 1);
}

/* v = ll of some x and y: xl * yl */
static bool mw_ll(IrFunc *f, Ref v, Ref *x, Ref *y) {
  Ref m0, m1;
  return mw_bin(f, v, I_MUL, &m0, &m1) && mw_low(f, m0, x) && mw_low(f, m1, y);
}

/* v = mid, the sum of p's and q's cross products */
static bool mw_mid(IrFunc *f, Ref v, Ref p, Ref q) {
  Ref c1, c2;
  return mw_bin(f, v, I_ADD, &c1, &c2) && mw_crosses(f, c1, c2, p, q);
}

/* v = zext(a <u b) */
static bool mw_carry(IrFunc *f, Ref v, Ref *a, Ref *b) {
  const Inst *in = &f->insts[v];
  if (in->op != I_EXTENDU || in->nargs != 1 || in->type != VT_I64) return false;
  Ref c = ir_resolve(f, ir_args(f, v)[0]);
  const Inst *ci = &f->insts[c];
  if (ci->nargs != 2 || (ci->op != I_LTU && ci->op != I_GTU)) return false;
  const Ref *ca = ir_args(f, c);
  Ref l = ir_resolve(f, ca[0]), r = ir_resolve(f, ca[1]);
  if (f->insts[l].type != VT_I64) return false;
  *a = ci->op == I_LTU ? l : r;
  *b = ci->op == I_LTU ? r : l;
  return true;
}

/* Rust's (zext(mid <u c) << 32) | (mid >> 32) */
static bool mw_rs_carry(IrFunc *f, Ref v, Ref p, Ref q) {
  Ref o[2];
  if (!mw_bin(f, v, I_OR, &o[0], &o[1])) return false;
  for (int k = 0; k < 2; k++) {
    Ref mid, e, a, b, c1, c2;
    if (!mw_shr(f, o[1 - k], 32, I_SHRU, &mid) || !mw_shl32(f, o[k], &e) ||
        !mw_carry(f, e, &a, &b) || a != mid ||
        !mw_bin(f, mid, I_ADD, &c1, &c2) || (b != c1 && b != c2) ||
        !mw_crosses(f, c1, c2, p, q))
      continue;
    return true;
  }
  return false;
}

/* x * y: a mul of p and q, or Rust's ll + (mid << 32) */
static bool mw_rs_lo(IrFunc *f, Ref v, Ref p, Ref q) {
  Ref l, r, x, y, mid;
  if (mw_bin(f, v, I_MUL, &l, &r)) return (l == p && r == q) || (l == q && r == p);
  if (!mw_bin(f, v, I_ADD, &l, &r)) return false;
  for (int k = 0; k < 2; k++) {
    Ref ll = k ? r : l, sh = k ? l : r;
    if (mw_ll(f, ll, &x, &y) && ((x == p && y == q) || (x == q && y == p)) &&
        mw_shl32(f, sh, &mid) && mw_mid(f, mid, p, q))
      return true;
  }
  return false;
}

/* Rust's zext(lo <u ll) */
static bool mw_rs_locarry(IrFunc *f, Ref v, Ref p, Ref q) {
  Ref lo, ll, x, y;
  return mw_carry(f, v, &lo, &ll) && mw_ll(f, ll, &x, &y) &&
         ((x == p && y == q) || (x == q && y == p)) && mw_rs_lo(f, lo, p, q);
}

/* the other shape's t = (ll >> 32) + c1, its cross product's kind in *kind */
static bool mw_hd_t(IrFunc *f, Ref t, Ref p, Ref q, int *kind) {
  Ref o[2], ll, x, y;
  if (!mw_bin(f, t, I_ADD, &o[0], &o[1])) return false;
  for (int k = 0; k < 2; k++) {
    if (!mw_shr(f, o[k], 32, I_SHRU, &ll) || !mw_ll(f, ll, &x, &y) ||
        !((x == p && y == q) || (x == q && y == p)))
      continue;
    int c = mw_prod(f, o[1 - k], p, q);
    if (c != 1 && c != 2) continue;
    *kind = c;
    return true;
  }
  return false;
}

/* u = (t & 0xffffffff) + c2, with t as above and c2 the other cross product */
static bool mw_hd_u(IrFunc *f, Ref u, Ref p, Ref q, Ref *t) {
  Ref o[2];
  if (!mw_bin(f, u, I_ADD, &o[0], &o[1])) return false;
  for (int k = 0; k < 2; k++) {
    int k1, k2 = mw_prod(f, o[1 - k], p, q);
    if (!mw_low(f, o[k], t) || !mw_hd_t(f, *t, p, q, &k1)) continue;
    if (p == q ? k1 == 1 && k2 == 1 : k1 + k2 == 3) return true;
  }
  return false;
}

/* the leaves of the i64 add tree at v, or false past MW_TERMS */
static bool mw_terms(IrFunc *f, Ref v, Ref *term, uint32_t *n) {
  Ref stk[MW_TERMS];
  uint32_t sp = 0;
  *n = 0;
  stk[sp++] = v;
  while (sp) {
    Ref x = stk[--sp], l, r;
    if (mw_bin(f, x, I_ADD, &l, &r)) {
      if (sp + 2 > MW_TERMS) return false;
      stk[sp++] = r;
      stk[sp++] = l;
      continue;
    }
    if (*n == MW_TERMS) return false;
    term[(*n)++] = x;
  }
  return true;
}

/* v, an i64 add or or, rewritten as a high or low multiply where it is one
   (the shapes above); the new instructions go before position *idx */
static bool fold_mulwide(IrFunc *f, uint32_t bi, uint32_t *idx, Ref v) {
  Ref term[MW_TERMS], l, r, x, y, t;
  uint32_t n;
  /* lo: Rust's ll + (mid << 32), the other's (u << 32) | (ll & 0xffffffff) */
  if ((f->insts[v].op == I_ADD || f->insts[v].op == I_OR) &&
      mw_bin(f, v, (IrOp)f->insts[v].op, &l, &r)) {
    for (int k = 0; k < 2; k++) {
      Ref a = k ? r : l, b = k ? l : r, u, ll, mid;
      bool hit = false;
      if (f->insts[v].op == I_ADD && mw_ll(f, a, &x, &y) && mw_shl32(f, b, &mid) &&
          mw_mid(f, mid, x, y))
        hit = true;
      else if (mw_shl32(f, a, &u) && mw_low(f, b, &ll) && mw_ll(f, ll, &x, &y) &&
               mw_hd_u(f, u, x, y, &t)) {
        Ref t0, t1, sl, x2, y2;
        /* the t inside u starts from this ll */
        mw_bin(f, t, I_ADD, &t0, &t1);
        hit = (mw_shr(f, t0, 32, I_SHRU, &sl) || mw_shr(f, t1, 32, I_SHRU, &sl)) &&
              sl == ll && mw_ll(f, sl, &x2, &y2);
      }
      if (!hit) continue;
      Ref ma[2] = { x, y };
      Ref m = emit_before(f, bi, idx, I_MUL, VT_I64, ma, 2, 0);
      ir_replace(f, v, m);
      f->insts[v].op = I_NOP;
      return true;
    }
  }
  if (f->insts[v].op != I_ADD || !mw_terms(f, v, term, &n)) return false;
  bool used[MW_TERMS] = { false };
  IrOp op = I_NOP;
  bool hi = false;   /* hi's terms found, not a mulhu already there */
  Ref p = REF_NONE, q = REF_NONE;
  for (uint32_t h = 0; h < n && op == I_NOP; h++) {
    if (!mw_bin(f, term[h], I_MUL, &l, &r) || !mw_shr(f, l, 32, I_SHRU, &p) ||
        !mw_shr(f, r, 32, I_SHRU, &q))
      continue;
    /* Rust's shape: hh, the mid carry, the lo carry */
    int ic = -1, il = -1;
    for (uint32_t k = 0; k < n; k++) {
      if (k == h) continue;
      if (ic < 0 && mw_rs_carry(f, term[k], p, q)) ic = (int)k;
      else if (il < 0 && mw_rs_locarry(f, term[k], p, q)) il = (int)k;
    }
    if (ic >= 0 && il >= 0) {
      used[h] = used[ic] = used[il] = true;
      op = I_MULHU;
      hi = true;
      break;
    }
    /* the other: hh, t >> 32, u >> 32, the t inside u */
    for (uint32_t iu = 0; iu < n && op == I_NOP; iu++) {
      Ref u;
      if (iu == h || !mw_shr(f, term[iu], 32, I_SHRU, &u) || !mw_hd_u(f, u, p, q, &t))
        continue;
      for (uint32_t it = 0; it < n; it++) {
        Ref t2;
        if (it == h || it == iu || !mw_shr(f, term[it], 32, I_SHRU, &t2) || t2 != t)
          continue;
        used[h] = used[iu] = used[it] = true;
        op = I_MULHU;
        hi = true;
        break;
      }
    }
  }
  /* beside mulhu(p, q), p * (q >>s 63) + q * (p >>s 63) makes it mulhs */
  for (uint32_t k = 0; k < n && op == I_NOP; k++)
    if (mw_bin(f, term[k], I_MULHU, &p, &q)) {
      used[k] = true;
      op = I_MULHU;
    }
  int ix = -1, iy = -1;
  for (uint32_t k = 0; k < n && op != I_NOP; k++) {
    if (used[k] || !mw_bin(f, term[k], I_MUL, &l, &r)) continue;
    for (int s = 0; s < 2; s++) {
      Ref a = s ? r : l, sh = s ? l : r, src;
      if (!mw_shr(f, sh, 63, I_SHRS, &src)) continue;
      if (ix < 0 && a == p && src == q) { ix = (int)k; break; }
      if (iy < 0 && a == q && src == p && (int)k != ix) { iy = (int)k; break; }
    }
  }
  bool sign = ix >= 0 && iy >= 0 && ix != iy;
  /* a mulhu already there and no sign terms: the same sum again */
  if (op == I_NOP || (!hi && !sign)) return false;
  if (sign) {
    used[ix] = used[iy] = true;
    op = I_MULHS;
  }
  Ref ma[2] = { p, q };
  Ref s = emit_before(f, bi, idx, op, VT_I64, ma, 2, 0);
  for (uint32_t k = 0; k < n; k++) {
    if (used[k]) continue;
    Ref aa[2] = { s, term[k] };
    s = emit_before(f, bi, idx, I_ADD, VT_I64, aa, 2, 0);
  }
  ir_replace(f, v, s);
  f->insts[v].op = I_NOP;
  return true;
}

static bool fold_block(IrFunc *f, uint32_t bi, const OptOpts *o, const uint32_t *uses) {
  Block *b = &f->blocks[bi];
  bool changed = false;
  for (uint32_t i = 0; i < b->ninsts; i++) {
    Ref v = b->insts[i];
    Inst *in = &f->insts[v];
    Ref *a = ir_args(f, v);
    ValType t = (ValType)in->type;
    uint64_t x = 0, y = 0, r;

    if (in->op == I_CONST || in->op == I_NOP) continue;

    switch (in->op) {
      case I_FIELD_GET: case I_ARRAY_GET: case I_ARRAY_LEN: case I_REF_TEST:
      case I_REF_IS_NULL: case I_TRAP_IF:
        if (gc_fold_known(f, v)) {
          g_stats.folded++;
          changed = true;
          continue;
        }
        break;
      default:
        break;
    }

    if (in->nargs == 2 && !vt_is_float((ValType)f->insts[a[0]].type) &&
        is_const(f, a[0], &x) && is_const(f, a[1], &y)) {
      ValType ot = (ValType)f->insts[a[0]].type;
      if (fold_int_binop(in->op, ot, x, y, &r)) {
        replace_with_const(f, v, r);
        g_stats.folded++;
        changed = true;
        continue;
      }
    }
    if (o->mulwide && t == VT_I64 && (in->op == I_ADD || in->op == I_OR) &&
        fold_mulwide(f, bi, &i, v)) {
      g_stats.mul_wide++;
      changed = true;
      continue;
    }
    /* A reinterpreted constant is a constant of the other type with the same
       bits.  GHC writes every double it uses as an i64.const and an
       f64.reinterpret_i64, which left alone lives in an integer register and
       crosses to the FP file with an fmov each time: spectral's 1.0 did that
       on every iteration of its inner loop. */
    if (in->op == I_REINTERPRET && in->nargs == 1 && (ValType)in->type != VT_V128 &&
        is_const(f, a[0], &x)) {
      replace_with_const(f, v, x);
      g_stats.folded++;
      changed = true;
      continue;
    }
    if (in->nargs == 1 && !vt_is_float((ValType)f->insts[a[0]].type) && is_const(f, a[0], &x)) {
      ValType ot = (ValType)f->insts[a[0]].type;
      if (fold_int_unop(in->op, ot, x, &r)) {
        replace_with_const(f, v, r);
        g_stats.folded++;
        changed = true;
        continue;
      }
    }

    if (in->nargs == 2 && is_commutative(in->op) &&
        is_const(f, a[0], &x) && !is_const(f, a[1], &y)) {
      Ref tmp = a[0]; a[0] = a[1]; a[1] = tmp;
      changed = true;
    }
    /* c < x is x > c, so the constant can be an immediate: Go tests a
       length with 0 > n, GHC's code with c > x. */
    if (in->nargs == 2 && cmp_mirror(in->op) &&
        is_const(f, a[0], &x) && !is_const(f, a[1], &y)) {
      Ref tmp = a[0]; a[0] = a[1]; a[1] = tmp;
      in->op = cmp_mirror(in->op);
      changed = true;
    }

    if (in->nargs == 2 && is_associative(in->op) && is_const(f, a[1], &y) &&
        f->insts[a[0]].op == in->op && f->insts[a[0]].nargs == 2 &&
        uses[a[0]] == 1 && f->insts[a[0]].type == in->type) {
      Ref *ia = ir_args(f, a[0]);
      uint64_t c1;
      uint64_t merged;
      if (is_const(f, ia[1], &c1) && !is_const(f, ia[0], &x) &&
          fold_int_binop(in->op, t, c1, y, &merged)) {
        Ref m = ir_emit(f, bi, I_CONST, t, NULL, 0, merged, 0);
        b->ninsts--;
        for (uint32_t j = b->ninsts; j > i; j--) b->insts[j] = b->insts[j - 1];
        b->insts[i] = m;
        b->ninsts++;
        in = &f->insts[v]; a = ir_args(f, v);
        Ref inner_lhs = ir_args(f, a[0])[0];
        a[0] = inner_lhs;
        a[1] = m;
        g_stats.folded++;
        changed = true;
        i++;
        continue;
      }
    }

    /* (x & c1) & c2 with no bit in common, or (x | c1) | c2 covering every
       bit, is a constant whatever else reads x & c1: GHC's pointer tagging
       leaves the first in its tag tests. */
    if (in->nargs == 2 && (in->op == I_AND || in->op == I_OR) && is_const(f, a[1], &y) &&
        f->insts[a[0]].op == in->op && f->insts[a[0]].nargs == 2 &&
        f->insts[a[0]].type == in->type) {
      uint64_t c1;
      if (is_const(f, ir_args(f, a[0])[1], &c1)) {
        uint64_t all = trunc_to(t, ~0ull);
        if (in->op == I_AND && (c1 & y & all) == 0) {
          replace_with_const(f, v, 0); g_stats.folded++; changed = true; continue;
        }
        if (in->op == I_OR && ((c1 | y) & all) == all) {
          replace_with_const(f, v, all); g_stats.folded++; changed = true; continue;
        }
      }
    }
    /* (x + c1) - c2: x when they are equal, else x + (c1 - c2) once nothing
       else needs x + c1. */
    if (in->op == I_SUB && in->nargs == 2 && is_const(f, a[1], &y) &&
        f->insts[a[0]].op == I_ADD && f->insts[a[0]].nargs == 2 &&
        f->insts[a[0]].type == in->type) {
      uint64_t c1;
      Ref *ia = ir_args(f, a[0]);
      if (is_const(f, ia[1], &c1)) {
        if (trunc_to(t, c1) == trunc_to(t, y)) {
          ir_replace(f, v, ia[0]); in->op = I_NOP;
          g_stats.folded++; changed = true; continue;
        }
        if (uses && uses[a[0]] == 1) {
          Ref x0 = ia[0];
          Ref m = emit_before(f, bi, &i, I_CONST, t, NULL, 0, trunc_to(t, c1 - y));
          in = &f->insts[v]; a = ir_args(f, v);
          a[0] = x0; a[1] = m;
          in->op = I_ADD;
          g_stats.folded++; changed = true; continue;
        }
      }
    }

    bool has_c1 = in->nargs == 2 && is_const(f, a[1], &y);
    bool has_c0 = in->nargs == 2 && is_const(f, a[0], &x);
    switch (in->op) {
      case I_ADD: case I_SUB: case I_OR: case I_XOR:
        if (has_c1 && y == 0) { ir_replace(f, v, a[0]); in->op = I_NOP; changed = true; g_stats.folded++; }
        else if (in->op != I_SUB && has_c0 && x == 0) { ir_replace(f, v, a[1]); in->op = I_NOP; changed = true; g_stats.folded++; }
        else if ((in->op == I_SUB || in->op == I_ADD) && in->nargs == 2 &&
                 sub_cancels(f, in->op, a) != REF_NONE) {
          /* x - (x - y) and x + (y - x) are y: Go writes LeadingZeros64 as
             64 - Len64, and Len64 as 64 - clz. */
          ir_replace(f, v, sub_cancels(f, in->op, a));
          in->op = I_NOP;
          changed = true;
          g_stats.folded++;
        }
        else if ((in->op == I_SUB || in->op == I_ADD) && in->nargs == 2 && fold_add_neg(f, v)) {
          changed = true;
          g_stats.folded++;
        }
        else if (in->op == I_XOR && xorshift_undone(f, v) != REF_NONE) {
          ir_replace(f, v, xorshift_undone(f, v));
          in->op = I_NOP;
          changed = true;
          g_stats.folded++;
        }
        else if (in->op == I_OR || in->op == I_XOR) {

          Ref sr = REF_NONE, sl = REF_NONE;
          for (int k = 0; k < 2; k++) {
            uint16_t op = f->insts[a[k]].op;
            if (op == I_SHRU) sr = a[k];
            else if (op == I_SHL) sl = a[k];
          }
          uint64_t kr, kl;
          uint32_t width = vt_is_64(t) ? 64u : 32u;
          if (sr != REF_NONE && sl != REF_NONE &&
              f->insts[sr].type == in->type && f->insts[sl].type == in->type &&
              ir_args(f, sr)[0] == ir_args(f, sl)[0] &&
              is_const(f, ir_args(f, sr)[1], &kr) &&
              is_const(f, ir_args(f, sl)[1], &kl) &&
              (kr % width) != 0 && (kr % width) + (kl % width) == width) {
            Ref *sa = ir_args(f, sr);
            Ref src = sa[0], amt = sa[1];
            in->op = I_ROTR;
            a[0] = src;
            a[1] = amt;
            g_stats.strength_reduced++;
            changed = true;
          } else {

            const uint32_t width = 32u;
            const uint64_t mask = 0x00ff00ffu;

            Ref src = bswap_src(f, v);
            if (src == REF_NONE && vt_is_64(t)) break;

            for (int k = 0; src == REF_NONE && k < 2; k++) {
              Ref lo = a[k], hi = a[k ^ 1];
              Ref x1 = rot_src_by(f, lo, width, 8);
              if (x1 == REF_NONE) continue;
              x1 = and_by(f, x1, mask);
              if (x1 == REF_NONE) continue;
              Ref x2 = and_by(f, hi, mask);
              if (x2 == REF_NONE) continue;
              x2 = rot_src_by(f, x2, width, width - 8);
              if (x2 == REF_NONE || x2 != x1) continue;
              if (f->insts[x1].type != in->type) continue;
              src = x1;
              break;
            }
            if (src != REF_NONE) {
              in->op = I_BSWAP;
              in->nargs = 1;
              a[0] = src;
              g_stats.strength_reduced++;
              changed = true;
            }
          }
        }
        break;
      case I_MUL:
        if (has_c1 && y == 1) { ir_replace(f, v, a[0]); in->op = I_NOP; changed = true; g_stats.folded++; }
        else if (has_c1 && trunc_to(t, y) == trunc_to(t, ~0ull)) {
          Ref x0 = a[0];
          Ref z = emit_before(f, bi, &i, I_CONST, t, NULL, 0, 0);
          in = &f->insts[v]; a = ir_args(f, v);
          a[0] = z; a[1] = x0;
          in->op = I_SUB;
          changed = true;
          g_stats.folded++;
        }
        else if (has_c1 && y == 0) { replace_with_const(f, v, 0); changed = true; g_stats.folded++; }
        else if (has_c0 && x == 1) { ir_replace(f, v, a[1]); in->op = I_NOP; changed = true; g_stats.folded++; }
        else if (has_c0 && x == 0) { replace_with_const(f, v, 0); changed = true; g_stats.folded++; }
        else if (o->strength && has_c1) {
          int k;
          uint64_t c = trunc_to(t, y);
          if (is_pow2(c, &k) && k > 0) {
            Ref sh = emit_before(f, bi, &i, I_CONST, t, NULL, 0, (uint64_t)k);
            in = &f->insts[v]; a = ir_args(f, v);
            a[1] = sh;
            in->op = I_SHL;
            g_stats.strength_reduced++;
            changed = true;
          } else if (c >= 3 && is_pow2(c - 1, &k) && k >= 1) {

            Ref x0 = a[0];
            Ref shc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, (uint64_t)k);
            Ref sargs[2] = { x0, shc };
            Ref sh = emit_before(f, bi, &i, I_SHL, t, sargs, 2, 0);
            in = &f->insts[v]; a = ir_args(f, v);
            a[0] = x0; a[1] = sh;
            in->op = I_ADD;
            g_stats.strength_reduced++;
            changed = true;
          }
        }
        break;
      case I_AND:
        if (has_c1 && y == 0) { replace_with_const(f, v, 0); changed = true; g_stats.folded++; }
        else if (has_c1 && trunc_to(t, y) == trunc_to(t, ~0ull)) {
          ir_replace(f, v, a[0]); in->op = I_NOP; changed = true; g_stats.folded++;
        } else if (has_c1 &&
                   (ir_known_bits(f, a[0], KB_DEPTH) & ~trunc_to(t, y)) == 0) {

          ir_replace(f, v, a[0]);
          in->op = I_NOP;
          changed = true;
          g_stats.folded++;
        }
        break;
      case I_SHL: case I_SHRU: case I_SHRS: case I_ROTL: case I_ROTR:
        if (!has_c1 && in->nargs == 2 && fold_shift_mask(f, v)) {
          changed = true;
          g_stats.folded++;
        } else if (in->op == I_SHL && has_c1 && uses && uses[a[0]] == 1 &&
                   f->insts[a[0]].op == I_MUL && f->insts[a[0]].nargs == 2 &&
                   is_const(f, ir_args(f, a[0])[1], &x)) {
          /* (x * c1) << c2 is x * (c1 << c2), one multiply. */
          Ref x0 = ir_args(f, a[0])[0];
          uint64_t w = vt_is_64(t) ? 64u : 32u;
          Ref k = emit_before(f, bi, &i, I_CONST, t, NULL, 0, trunc_to(t, x << (y % w)));
          in = &f->insts[v]; a = ir_args(f, v);
          a[0] = x0; a[1] = k;
          in->op = I_MUL;
          changed = true;
          g_stats.folded++;
        } else if (has_c1 && (y % (uint64_t)(t == VT_I64 ? 64 : 32)) == 0) {
          ir_replace(f, v, a[0]); in->op = I_NOP; changed = true; g_stats.folded++;
        } else if (in->op == I_SHRU && has_c1 &&
                   (ir_known_bits(f, a[0], KB_DEPTH) >> (y % (vt_is_64(t) ? 64u : 32u))) == 0) {
          replace_with_const(f, v, 0); changed = true; g_stats.folded++;
        } else if (in->op == I_SHL && has_c1 && uses && uses[a[0]] == 1 &&
                   f->insts[a[0]].op == I_SHRU && f->insts[a[0]].nargs == 2) {
          /* (x >> c) << c clears the low c bits: one and. */
          uint64_t c2, w = vt_is_64(t) ? 64u : 32u;
          Ref src = ir_args(f, a[0])[0];
          if (is_const(f, ir_args(f, a[0])[1], &c2) && c2 % w == y % w) {
            uint64_t mask = trunc_to(t, ~0ull << (y % w));
            Ref mc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, mask);
            in = &f->insts[v]; a = ir_args(f, v);
            a[0] = src; a[1] = mc;
            in->op = I_AND;
            changed = true;
            g_stats.folded++;
          }
        } else if ((in->op == I_SHL || in->op == I_SHRU || in->op == I_SHRS) && has_c1 &&
                   uses && (uses[a[0]] == 1 || f->insts[a[0]].block == bi) &&
                   f->insts[a[0]].op == in->op &&
                   f->insts[a[0]].nargs == 2) {
          /* Two shifts the same way by constants are one, or all of x gone:
             zero for a logical shift, the sign everywhere for an arithmetic
             one. */
          uint64_t c1, w = vt_is_64(t) ? 64u : 32u;
          Ref x0 = ir_args(f, a[0])[0];
          if (is_const(f, ir_args(f, a[0])[1], &c1)) {
            uint64_t tot = (c1 % w) + (y % w);
            if (tot >= w && in->op != I_SHRS) {
              replace_with_const(f, v, 0);
            } else {
              Ref k = emit_before(f, bi, &i, I_CONST, t, NULL, 0, tot < w ? tot : w - 1);
              in = &f->insts[v]; a = ir_args(f, v);
              a[0] = x0;
              a[1] = k;
            }
            changed = true;
            g_stats.folded++;
          }
        } else if (in->op == I_ROTL || in->op == I_ROTR) {
          /* A byte reversal that ends in swapping its halves, as Go's
             portable bits.ReverseBytes64 does, is a rotate at the top. */
          Ref src = bswap_src(f, v);
          if (src != REF_NONE) {
            in->op = I_BSWAP;
            in->nargs = 1;
            a[0] = src;
            g_stats.strength_reduced++;
            changed = true;
          }
        }
        break;
      case I_DIVU:
        if (o->strength && has_c1) {
          int k;
          uint64_t dc = trunc_to(t, y);

          if (dc > 2 && !is_pow2(dc, &k)) {
            Ref q = dc >> (vt_is_64(t) ? 63 : 31)
                        ? emit_geu_const(f, bi, &i, a[0], t, dc)
                        : emit_magic_udiv(f, bi, &i, a[0], t, dc);
            in = &f->insts[v]; a = ir_args(f, v);
            ir_replace(f, v, q);
            in->op = I_NOP;
            g_stats.strength_reduced++;
            changed = true;
            break;
          }
          if (is_pow2(trunc_to(t, y), &k)) {
            if (k == 0) { ir_replace(f, v, a[0]); in->op = I_NOP; }
            else {
              Ref sh = ir_emit(f, bi, I_CONST, t, NULL, 0, (uint64_t)k, 0);
              b->ninsts--;
              for (uint32_t j = b->ninsts; j > i; j--) b->insts[j] = b->insts[j - 1];
              b->insts[i] = sh;
              b->ninsts++;
              in = &f->insts[v]; a = ir_args(f, v);
              a[1] = sh;
              in->op = I_SHRU;
              i++;
            }
            g_stats.strength_reduced++;
            changed = true;
          }
        }
        break;
      case I_DIVS:
        if (o->strength && has_c1) {
          int k;
          uint64_t c = trunc_to(t, y);
          if (c == 1) { ir_replace(f, v, a[0]); in->op = I_NOP; changed = true; g_stats.strength_reduced++; }

          else if (is_pow2(c, &k) && k <= (vt_is_64(t) ? 62 : 30)) {
            strength_divs_pow2(f, bi, &i, v, k);
            in = &f->insts[v]; a = ir_args(f, v);
            g_stats.strength_reduced++;
            changed = true;
          } else if (c != 0) {
            /* every other divisor: -1 is a negation that traps on the
               minimum, the minimum itself goes into n only when n is the
               minimum, -2^k is the negated quotient by 2^k, and the rest
               take a magic number */
            int w = vt_is_64(t) ? 64 : 32;
            uint64_t smin = (uint64_t)1 << (w - 1);
            uint64_t ac = (c & smin) ? trunc_to(t, 0 - c) : c;
            Ref n = a[0], q;
            if (c == trunc_to(t, ~0ull)) {
              Ref mc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, smin);
              Ref ca[2] = { n, mc };
              Ref ok = emit_before(f, bi, &i, I_NE, VT_I32, ca, 2, 0);
              emit_before(f, bi, &i, I_TRAP_IF, VT_BOT, &ok, 1, TRAP_DIV_OVERFLOW);
              Ref zc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, 0);
              Ref na[2] = { zc, n };
              q = emit_before(f, bi, &i, I_SUB, t, na, 2, 0);
            } else if (c == smin) {
              Ref mc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, smin);
              Ref ca[2] = { n, mc };
              q = emit_before(f, bi, &i, I_EQ, VT_I32, ca, 2, 0);
              if (t == VT_I64) q = emit_before(f, bi, &i, I_EXTENDU, VT_I64, &q, 1, 0);
            } else if (is_pow2(ac, &k)) {
              Ref sum = emit_divs_pow2_sum(f, bi, &i, n, t, k);
              Ref kc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, (uint64_t)k);
              Ref sa[2] = { sum, kc };
              Ref pq = emit_before(f, bi, &i, I_SHRS, t, sa, 2, 0);
              Ref zc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, 0);
              Ref na[2] = { zc, pq };
              q = emit_before(f, bi, &i, I_SUB, t, na, 2, 0);
            } else {
              q = emit_magic_sdiv(f, bi, &i, n, t, c);
            }
            in = &f->insts[v]; a = ir_args(f, v);
            ir_replace(f, v, q);
            in->op = I_NOP;
            g_stats.strength_reduced++;
            changed = true;
          }
        }
        break;
      case I_REMS:
        if (o->strength && has_c1) {
          /* n - q * d, the sign the dividend's: by +-1 nothing is left (and
             wasm's rem_s does not trap on the minimum), by the minimum n
             is left unless it is the minimum, by +-2^k the bits under 2^k
             of n rounded toward zero, and the rest through the magic
             quotient */
          int k;
          uint64_t c = trunc_to(t, y);
          int w = vt_is_64(t) ? 64 : 32;
          uint64_t smin = (uint64_t)1 << (w - 1);
          uint64_t ac = (c & smin) ? trunc_to(t, 0 - c) : c;
          if (c == 0) break;
          if (ac == 1) {
            replace_with_const(f, v, 0);
            g_stats.strength_reduced++;
            changed = true;
            break;
          }
          Ref n = a[0], r;
          if (c == smin) {
            Ref mc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, smin);
            Ref ca[2] = { n, mc };
            Ref is_min = emit_before(f, bi, &i, I_EQ, VT_I32, ca, 2, 0);
            Ref zc = emit_before(f, bi, &i, I_CONST, t, NULL, 0, 0);
            Ref sa[3] = { zc, n, is_min };
            r = emit_before(f, bi, &i, I_SELECT, t, sa, 3, 0);
          } else {
            Ref p;
            if (is_pow2(ac, &k)) {
              Ref sum = emit_divs_pow2_sum(f, bi, &i, n, t, k);
              Ref mk = emit_before(f, bi, &i, I_CONST, t, NULL, 0, trunc_to(t, ~0ull << k));
              Ref ma[2] = { sum, mk };
              p = emit_before(f, bi, &i, I_AND, t, ma, 2, 0);
            } else {
              Ref q = emit_magic_sdiv(f, bi, &i, n, t, c);
              Ref dcv = emit_before(f, bi, &i, I_CONST, t, NULL, 0, c);
              Ref mula[2] = { q, dcv };
              p = emit_before(f, bi, &i, I_MUL, t, mula, 2, 0);
            }
            Ref suba[2] = { n, p };
            r = emit_before(f, bi, &i, I_SUB, t, suba, 2, 0);
          }
          in = &f->insts[v]; a = ir_args(f, v);
          ir_replace(f, v, r);
          in->op = I_NOP;
          g_stats.strength_reduced++;
          changed = true;
        }
        break;
      case I_EXT8S: case I_EXT16S: case I_EXT32S: {
        /* The sign extension of a narrow unsigned load is the signed load:
           ldrsb where there was ldrb and sxtb. */
        Inst *li = &f->insts[a[0]];
        uint32_t want = in->op == I_EXT8S ? 0u : in->op == I_EXT16S ? 1u : 2u;
        if (li->op == I_LOAD && uses && uses[a[0]] == 1 && li->type == in->type &&
            MEMF_SIZE(li->imm2) == want && !(li->imm2 & MEMF_SIGNED)) {
          li->imm2 |= MEMF_SIGNED;
          ir_replace(f, v, a[0]);
          in->op = I_NOP;
          changed = true;
          g_stats.folded++;
        }
        break;
      }
      case I_EXTENDS: case I_EXTENDU: {
        /* An i32 load widened is the i64 load of the same bytes: ldrsw
           where there was ldr and sxtw.  A narrow signed load zero-extended
           has no such load. */
        Inst *li = &f->insts[a[0]];
        if (li->op == I_LOAD && uses && uses[a[0]] == 1 && li->type == VT_I32 &&
            MEMF_SIZE(li->imm2) <= 2) {
          bool sg = (li->imm2 & MEMF_SIGNED) != 0;
          bool two = MEMF_SIZE(li->imm2) == 2;
          if (in->op == I_EXTENDS || !sg) {
            if (two) li->imm2 = (li->imm2 & ~(uint64_t)MEMF_SIGNED) |
                                (in->op == I_EXTENDS ? MEMF_SIGNED : 0);
            li->type = VT_I64;
            ir_replace(f, v, a[0]);
            in->op = I_NOP;
            changed = true;
            g_stats.folded++;
            break;
          }
        }
        if (in->op == I_EXTENDS) break;
      }
        if (f->insts[a[0]].op == I_WRAP && f->insts[a[0]].nargs == 1) {
          Ref x0 = ir_args(f, a[0])[0];
          if (f->insts[x0].type == VT_I64 &&
              (ir_known_bits(f, x0, KB_DEPTH) >> 32) == 0) {
            ir_replace(f, v, x0);
            in->op = I_NOP;
            changed = true;
            g_stats.folded++;
          }
        }
        break;
      case I_WRAP: {
        /* Go's back end keeps every integer and bool in an i64 and narrows it
           back where wasm wants an i32, so the round trip is everywhere. */
        Inst *si = &f->insts[a[0]];
        Ref s0 = si->nargs ? ir_args(f, a[0])[0] : REF_NONE;
        uint64_t m;
        if ((si->op == I_EXTENDU || si->op == I_EXTENDS) && si->nargs == 1) {
          ir_replace(f, v, s0);
          in->op = I_NOP;
          changed = true;
          g_stats.folded++;
        } else if ((si->op == I_EXT32S && si->nargs == 1) ||
                   (si->op == I_AND && si->nargs == 2 &&
                    is_const(f, ir_args(f, a[0])[1], &m) &&
                    (uint32_t)m == 0xffffffffu)) {
          a[0] = s0;
          changed = true;
          g_stats.folded++;
        } else if (uses && uses[a[0]] == 1 && fold_narrow(f, bi, &i, v)) {
          in = &f->insts[v]; a = ir_args(f, v);
          changed = true;
          g_stats.folded++;
        }
        break;
      }
      case I_EQZ: {

        Ref src = a[0];
        uint16_t sop = f->insts[src].op;
        if ((sop == I_EXTENDU || sop == I_EXTENDS) && f->insts[src].nargs == 1) {
          a[0] = ir_args(f, src)[0];
          changed = true;
          g_stats.folded++;
          break;
        }
        if ((sop == I_SUB || sop == I_XOR || sop == I_ADD) && f->insts[src].nargs == 2 &&
            test_may_look_through(f, bi, src, uses)) {
          /* eqz(x - y) and eqz(x ^ y) are x == y; eqz(x + c) is x == -c. */
          Ref x0 = ir_args(f, src)[0], y0 = ir_args(f, src)[1];
          uint64_t cc;
          bool k = is_const(f, y0, &cc);
          if (sop != I_ADD || k) {
            ValType st = (ValType)f->insts[src].type;
            if (sop == I_ADD)
              y0 = emit_before(f, bi, &i, I_CONST, st, NULL, 0, trunc_to(st, (uint64_t)0 - cc));
            Ref ea[2] = { x0, y0 };
            Ref nv = emit_before(f, bi, &i, I_EQ, VT_I32, ea, 2, 0);
            ir_replace(f, v, nv);
            in = &f->insts[v]; a = ir_args(f, v);
            in->op = I_NOP;
            changed = true;
            g_stats.folded++;
            break;
          }
        }
        if (sop == I_EQZ && f->insts[src].nargs == 1) {
          Ref inner = ir_args(f, src)[0];
          if (f->insts[inner].type == VT_I32 &&
              (ir_known_bits(f, inner, KB_DEPTH) & ~1ull) == 0) {
            ir_replace(f, v, inner);
            in->op = I_NOP;
            changed = true;
            g_stats.folded++;
            break;
          }
        }
        if (!uses || uses[src] != 1) break;
        Ref nv;
        if (sop == I_EQZ) {
          Ref inner = ir_args(f, src)[0];
          ValType it = (ValType)f->insts[inner].type;
          Ref z = emit_before(f, bi, &i, I_CONST, it, NULL, 0, 0);
          Ref cargs[2] = { inner, z };
          nv = emit_before(f, bi, &i, I_NE, VT_I32, cargs, 2, 0);
        } else {
          uint16_t inv = cmp_inverse(sop);
          if (!inv) break;
          Ref *sa = ir_args(f, src);
          Ref cargs[2] = { sa[0], sa[1] };
          nv = emit_before(f, bi, &i, (IrOp)inv, VT_I32, cargs, 2, 0);
        }
        ir_replace(f, v, nv);
        in = &f->insts[v]; a = ir_args(f, v);
        in->op = I_NOP;
        changed = true;
        g_stats.folded++;
        break;
      }
      case I_REMU:
        if (o->strength && has_c1) {
          int k;
          uint64_t dc = trunc_to(t, y);

          if (dc > 2 && !is_pow2(dc, &k)) {
            Ref q = dc >> (vt_is_64(t) ? 63 : 31)
                        ? emit_geu_const(f, bi, &i, a[0], t, dc)
                        : emit_magic_udiv(f, bi, &i, a[0], t, dc);
            Ref dcv = emit_before(f, bi, &i, I_CONST, t, NULL, 0, dc);
            Ref mula[2] = { q, dcv };
            Ref prod = emit_before(f, bi, &i, I_MUL, t, mula, 2, 0);
            in = &f->insts[v]; a = ir_args(f, v);
            Ref suba[2] = { a[0], prod };
            Ref rem = emit_before(f, bi, &i, I_SUB, t, suba, 2, 0);
            in = &f->insts[v];
            ir_replace(f, v, rem);
            in->op = I_NOP;
            g_stats.strength_reduced++;
            changed = true;
            break;
          }
          if (is_pow2(trunc_to(t, y), &k)) {
            Ref m = ir_emit(f, bi, I_CONST, t, NULL, 0, trunc_to(t, y) - 1, 0);
            b->ninsts--;
            for (uint32_t j = b->ninsts; j > i; j--) b->insts[j] = b->insts[j - 1];
            b->insts[i] = m;
            b->ninsts++;
            in = &f->insts[v]; a = ir_args(f, v);
            a[1] = m;
            in->op = I_AND;
            g_stats.strength_reduced++;
            changed = true;
            i++;
          }
        }
        break;
      case I_MEM_ADDR:
        if (is_const(f, a[0], &x)) {
          replace_with_const(f, v, (uint64_t)(uint32_t)x + in->imm);
          g_stats.folded++;
          changed = true;
        }
        break;
      case I_SELECT: {
        uint64_t c;
        if (is_const(f, a[2], &c)) {
          ir_replace(f, v, c ? a[0] : a[1]);
          in->op = I_NOP;
          changed = true;
          g_stats.folded++;
        } else if (a[0] == a[1]) {
          ir_replace(f, v, a[0]);
          in->op = I_NOP;
          changed = true;
          g_stats.folded++;
        }
        break;
      }
      case I_EQ: case I_LES: case I_LEU: case I_GES: case I_GEU:
        if (in->nargs == 2 && a[0] == a[1]) { replace_with_const(f, v, 1); changed = true; g_stats.folded++; }
        else if (fold_cmp_widened(f, v)) { changed = true; g_stats.folded++; }
        else if (in->op == I_EQ && fold_bool_cmp(f, v)) { changed = true; g_stats.folded++; }
        else if (in->op == I_EQ && fold_cmp_arith(f, bi, &i, v, uses)) {
          in = &f->insts[v]; a = ir_args(f, v);
          changed = true; g_stats.folded++;
        }
        else if (in->op == I_LEU && has_c1 && trunc_to((ValType)f->insts[a[0]].type, y) == 0) {
          /* x <=u 0 is x == 0, a cbz. */
          in->op = I_EQZ; in->nargs = 1; changed = true; g_stats.folded++;
        }
        else if (in->op == I_GEU && has_c1 && trunc_to((ValType)f->insts[a[0]].type, y) == 1) {
          /* x >=u 1 is x != 0, a cbnz. */
          Ref z = emit_before(f, bi, &i, I_CONST, (ValType)f->insts[a[0]].type, NULL, 0, 0);
          in = &f->insts[v]; a = ir_args(f, v);
          in->op = I_NE; a[1] = z; changed = true; g_stats.folded++;
        }
        else if (in->op == I_EQ && fold_ext_cmp(f, bi, &i, v)) {
          in = &f->insts[v]; a = ir_args(f, v);
          changed = true; g_stats.folded++;
        }
        break;
      case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
        if (in->nargs == 2 && a[0] == a[1]) { replace_with_const(f, v, 0); changed = true; g_stats.folded++; }
        else if (fold_cmp_widened(f, v)) { changed = true; g_stats.folded++; }
        else if (in->op == I_NE && fold_bool_cmp(f, v)) { changed = true; g_stats.folded++; }
        else if (in->op == I_NE && fold_cmp_arith(f, bi, &i, v, uses)) {
          in = &f->insts[v]; a = ir_args(f, v);
          changed = true; g_stats.folded++;
        }
        else if (in->op == I_LTU && has_c1 && trunc_to((ValType)f->insts[a[0]].type, y) == 1) {
          /* x <u 1 is x == 0. */
          in->op = I_EQZ; in->nargs = 1; changed = true; g_stats.folded++;
        }
        else if (in->op == I_GTU && has_c1 && trunc_to((ValType)f->insts[a[0]].type, y) == 0) {
          /* x >u 0 is x != 0. */
          in->op = I_NE; changed = true; g_stats.folded++;
        }
        else if (in->op == I_NE && fold_ext_cmp(f, bi, &i, v)) {
          in = &f->insts[v]; a = ir_args(f, v);
          changed = true; g_stats.folded++;
        }
        break;
      default: break;
    }
    if (in->op == I_SUB && in->nargs == 2 && a[0] == a[1]) {
      replace_with_const(f, v, 0); changed = true; g_stats.folded++;
    }
    if (in->op == I_XOR && in->nargs == 2 && a[0] == a[1]) {
      replace_with_const(f, v, 0); changed = true; g_stats.folded++;
    }
    if ((in->op == I_AND || in->op == I_OR) && in->nargs == 2 && a[0] == a[1]) {
      ir_replace(f, v, a[0]); in->op = I_NOP; changed = true; g_stats.folded++;
    }
  }
  return changed;
}

static bool pass_fold(IrFunc *f, const OptOpts *o) {
  bool changed = false;
  uint32_t *uses = count_uses(f);
  for (uint32_t oi = 0; oi < f->norder; oi++)
    if (fold_block(f, f->order[oi], o, uses)) changed = true;
  xfree(uses);
  if (changed) ir_apply_subst(f);
  return changed;
}

static bool pass_dce(IrFunc *f) {
  uint8_t *live = xcalloc(f->ninsts ? f->ninsts : 1, 1);
  Ref *work = xmalloc((f->ninsts ? f->ninsts : 1) * sizeof(Ref));
  uint32_t nw = 0;

#define MARK(v) do { if (!live[v]) { live[v] = 1; work[nw++] = (v); } } while (0)

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];

      if (f->insts[v].op == I_MEM_LIMIT) continue;

      /* impure only for their checks; with none pending, an unused GC
         read has no effect to keep */
      if ((f->insts[v].op == I_FIELD_GET || f->insts[v].op == I_ARRAY_LEN ||
           f->insts[v].op == I_ARRAY_GET || f->insts[v].op == I_I31_GET) &&
          !gc_op_traps(&f->insts[v]))
        continue;
      if (!ir_is_pure(&f->insts[v])) MARK(v);
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) MARK(b->term_arg);
    for (uint32_t i = 0; i < b->nret; i++) MARK(b->ret_args[i]);
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++) MARK(b->succs[s].args[j]);
    for (uint32_t p = 0; p < b->nparams; p++) MARK(b->params[p]);
  }
  while (nw) {
    Ref v = work[--nw];
    Inst *in = &f->insts[v];
    Ref *a = ir_args(f, v);
    for (uint32_t j = 0; j < in->nargs; j++) MARK(a[j]);
  }
#undef MARK

  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    uint32_t k = 0;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      if (!live[v] || f->insts[v].op == I_NOP) { g_stats.dce_removed++; changed = true; continue; }
      b->insts[k++] = v;
    }
    b->ninsts = k;
  }
  xfree(live); xfree(work);
  return changed;
}

/* ---- throws caught where they are thrown ---------------------------------

   A throw whose first matching handler is a pad of its own function, one
   that catches the tag -- or anything -- without taking the exnref, is a
   jump: the pad would only unpack the values the throw packs.  Inlining is
   what puts a callee's throw beside its caller's catch, and once it is a
   jump what was thrown no longer escapes.  The unwinder tries the clauses in
   edge order and matches a tag by identity, so a clause for another tag
   index is passed over only when neither tag is imported: two imports may
   be one tag.  What the pad unpacks may be read past it directly, once the
   block it jumps to has lost its parameters to it; that block then gets a
   parameter again, which the pad and the jump each fill with their own. */
static bool lt_pad_simple(IrFunc *f, uint32_t pad) {
  Block *p = &f->blocks[pad];
  if (p->pad_kind == 1 || p->pad_kind == 3) return false;
  if (p->term == T_JUMP) {
    if (p->nsuccs != 1 || p->succs[0].eh) return false;
  } else if (p->term != T_RETURN) {
    return false;
  }
  for (uint32_t i = 0; i < p->ninsts; i++) {
    uint16_t op = f->insts[p->insts[i]].op;
    if (op != I_EXN_OBJ && op != I_EXN_PAYLOAD && op != I_NOP) return false;
  }
  return true;
}

/* What a value the pad defines is once the throw jumps instead: a payload
   is the value thrown, a parameter -- a local's value at the raise -- what
   the throw's own edge brings it.  REF_NONE for the exnref, which there is
   no making; a value from outside the pad is itself. */
static Ref lt_value(IrFunc *f, uint32_t pad, Ref r, const Ref *vals,
                    uint32_t nvals, const Edge *in) {
  r = ir_resolve(f, r);
  Inst *ri = &f->insts[r];
  if (ri->block != pad) return r;
  if (ri->op == I_EXN_PAYLOAD) return ri->imm < nvals ? vals[ri->imm] : REF_NONE;
  if (ri->op == I_PARAM) {
    Block *p = &f->blocks[pad];
    for (uint32_t k = 0; k < p->nparams; k++)
      if (p->params[k] == r) return k < in->nargs ? ir_resolve(f, in->args[k]) : REF_NONE;
    return REF_NONE;
  }
  return ri->op == I_EXN_OBJ ? REF_NONE : r;
}

/* Every read of v outside the pad, redirected to r when r is not
   REF_NONE; whether there was one. */
static bool lt_past(IrFunc *f, uint32_t pad, Ref v, Ref r) {
  bool seen = false;
#define LT_SEE(x_) do { if (ir_resolve(f, (x_)) == v) { seen = true; \
    if (r != REF_NONE) (x_) = r; } } while (0)
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    if (bi == pad) continue;
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      Ref *a = ir_args(f, b->insts[i]);
      for (uint32_t j = 0; j < in->nargs; j++) LT_SEE(a[j]);
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) LT_SEE(b->term_arg);
    for (uint32_t i = 0; i < b->nret; i++) LT_SEE(b->ret_args[i]);
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++) LT_SEE(b->succs[s].args[j]);
  }
#undef LT_SEE
  return seen;
}

static void edge_push_arg_at(IrFunc *f, uint32_t from, uint32_t si, Ref v);

static bool pass_local_throw(IrFunc *f) {
  if (!f->npads) return false;
  const Module *m = f->m;
  bool changed = false;
  Ref *out = NULL;
  uint32_t cap = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (b->term != T_TRAP || !b->ninsts) continue;
    Ref t = b->insts[b->ninsts - 1];
    if (f->insts[t].op != I_THROW) continue;
    uint32_t tag = (uint32_t)f->insts[t].imm, pad = UINT32_MAX, via = 0;
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      if (!b->succs[s].eh) continue;
      const Block *p = &f->blocks[b->succs[s].target];
      if (p->pad_kind >= 2 || p->pad_tag == tag) {
        pad = b->succs[s].target;
        via = s;
        break;
      }
      if (tag < m->nimported_tags || p->pad_tag < m->nimported_tags) break;
    }
    if (pad == UINT32_MAX || !lt_pad_simple(f, pad)) continue;

    /* what the pad hands on, as the throw has it */
    Block *p = &f->blocks[pad];
    const Edge *in = &b->succs[via];
    bool jump = p->term == T_JUMP;
    uint32_t n = jump ? p->succs[0].nargs : p->nret;
    const Ref *from = jump ? p->succs[0].args : p->ret_args;
    if (n > cap) {
      cap = n * 2;
      out = xrealloc(out, cap * sizeof(Ref));
    }
    const Ref *vals = ir_args(f, t);
    uint32_t nvals = f->insts[t].nargs;
    bool ok = true;
    for (uint32_t j = 0; j < n && ok; j++) {
      out[j] = lt_value(f, pad, from[j], vals, nvals, in);
      ok = out[j] != REF_NONE;
    }
    if (!ok) continue;

    /* the pad's values read past it: the block it jumps to, which only
       the pad reaches if anything past it can read them, takes each as a
       parameter instead */
    Ref past[16];
    uint32_t npast = 0;
    for (uint32_t k = 0; k < p->nparams + p->ninsts && ok; k++) {
      Ref v = k < p->nparams ? p->params[k] : p->insts[k - p->nparams];
      uint16_t op = f->insts[v].op;
      if (op != I_PARAM && op != I_EXN_PAYLOAD && op != I_EXN_OBJ) continue;
      if (!lt_past(f, pad, v, REF_NONE)) continue;
      ok = jump && npast < 16 && f->blocks[p->succs[0].target].npreds == 1 &&
           lt_value(f, pad, v, vals, nvals, in) != REF_NONE;
      if (ok) past[npast++] = v;
    }
    if (!ok) continue;
    if (n + npast > cap) {
      cap = (n + npast) * 2;
      out = xrealloc(out, cap * sizeof(Ref));
    }
    for (uint32_t k = 0; k < npast; k++) {
      uint32_t to = p->succs[0].target;
      Ref P = ir_add_param(f, to, (ValType)f->insts[past[k]].type);
      edge_push_arg_at(f, pad, 0, past[k]);
      lt_past(f, pad, past[k], P);
      out[n++] = lt_value(f, pad, past[k], vals, nvals, in);
    }

    f->insts[t].op = I_NOP;
    f->insts[t].nargs = 0;
    b->ninsts--;
    b->nsuccs = 0;
    if (jump) {
      b->term = T_JUMP;
      ir_add_edge(f, bi, p->succs[0].target, out, n);
    } else {
      b->term = T_RETURN;
      b->ret_args = ir_alloc(f, (n ? n : 1) * sizeof(Ref));
      memcpy(b->ret_args, out, n * sizeof(Ref));
      b->nret = b->cap_ret = n;
    }
    g_stats.throw_jumps++;
    changed = true;
  }
  xfree(out);
  if (changed) {
    ir_apply_subst(f);
    ir_recompute_preds(f);
    ir_order(f);
  }
  return changed;
}

/* ---- escape analysis and scalar replacement ---------------------------

   An allocation that never leaves the function -- never stored, passed,
   returned, put in a global or compared with something that might be it --
   need not exist.  Its fields become SSA values, carried round the CFG by
   block parameters the way pass_global_ssa carries a global, and the
   allocation, its stores and its null checks go.

   What counts as one object:
   - struct.new, and array.new / array.new_default of a constant length of at
     most ESC_MAX_ELEMS, its elements reached at constant indices.  A longer
     array (up to a megabyte) can still go when nothing reads its elements;
   - ref.cast / ref.convert of one (I_REF_CONVERT), and a block parameter
     every argument of which is the same object: a loop-carried or merged
     alias, which shares the object's fields;
   - a block parameter merging different objects -- `c ? new P(a) : new
     P(b)`, or a loop variable given a fresh object each trip.  It is an
     object of its own, its fields parameters of that block, provided each
     object flowing in is used no more once it has: no use of one is
     reachable from the merge without allocating it again.  That is what
     lets the merged value own fields that nothing else can see;
   - such a merge with null among what flows in -- `c ? new P(a) : null`,
     or a field of a replaced object that only some paths store an object
     in.  Beside its fields it carries a flag, 1 for an object and 0 for
     null, which its null checks and tests read, and everything it merges
     with carries one too.  One that escapes is no merge after all: the
     question is asked again with it a parameter like any other.

   What a use may be: field.get/set, array.get/set at a constant index in
   range, array.len, a null check (it is not null), ref.is_null, ref.test
   (its exact type is known), ref.eq/ne (nothing that is not one of its
   aliases can be it -- unless it may be null), and flowing into a
   parameter as above.  Anything else
   -- a call, being stored, returned, select -- and it escapes, together with
   every object it merges with.

   Exception edges carry no arguments, so an object whose fields would have
   to be live into a landing pad stays an allocation.

   Partial escape: a call, a store, a return or a parameter that is not an
   object need not be the end of it.  Such a use is an escape point when it
   is the object's last -- no use of it, the point's own included, can run
   after it before the object is allocated anew -- and the object is then
   made there, from its fields as they stand, and is scalars until then.  A
   path that reaches no escape point allocates nothing, and no path
   allocates more than once.  An object that every path hands over -- or
   every path but one bound for a trap -- can still gain by being made
   later, when its points need no edge split: what it is written and read
   until then is SSA values, and the writes fold into its initialising
   stores, which memopt knows alias nothing.  Short of that it keeps its
   allocation where it was. */

static void edge_push_arg_at(IrFunc *f, uint32_t from, uint32_t si, Ref v);
static uint32_t gl_split_edge(IrFunc *f, uint32_t from, uint32_t si);

#define ESC_MAX_ELEMS  32u
#define ESC_MAX_OBJS   256u
#define ESC_MAX_VARS   1024u
#define ESC_MAX_CELLS  (1u << 22)
#define ESC_DEAD_BYTES (1u << 20)
#define ESC_MAX_POINTS 8u

enum { ESK_STRUCT, ESK_ARRAY };

enum {
  ESW_COND = 1, ESW_RETURN, ESW_PARAM, ESW_FOREIGN, ESW_REUSED, ESW_SHAPE,
  ESW_PAD, ESW_BUDGET, ESW_MERGED, ESW_ELEM, ESW_SHARED, ESW_LATER, ESW_GAIN,
  ESW_OP = 1000
};

typedef struct {
  Ref def;            /* the allocation, or the parameter that merges */
  uint32_t block;
  uint32_t ti;
  uint8_t kind;
  bool phi;
  bool escaped;
  bool dead_only;     /* an array too long to replace: removable unread */
  bool benefit;       /* a use its fields answer, not just initialise */
  bool nullable;      /* a merge with null among what flows in */
  bool flagged;       /* carries the flag that says which: in such a group */
  uint16_t why;
  uint16_t pwhy;      /* what the first escape point is */
  uint32_t nvars, var0, len;
  uint32_t npts;      /* escape points */
  uint64_t shape;     /* the allocation's imm2, for making it again */
  Ref fill;           /* array.new's fill value, REF_NONE for the default */
} EsObj;

typedef struct { uint32_t obj, block; } EsUse;
typedef struct { uint32_t src, dst; } EsFlow;
/* An object's variables: its fields, then the flag if it has one. */
#define ES_NV(o_) ((o_)->nvars + ((o_)->flagged ? 1u : 0u))

/* An escape point: an instruction; with inst REF_NONE, the block's end;
   and with succ set too, that edge alone, which is split to make room. */
typedef struct { uint32_t obj, block; Ref inst; uint32_t succ; } EsPt;

static const char *es_why(uint16_t w) {
  switch (w) {
    case ESW_COND: return "a branch condition";
    case ESW_RETURN: return "returned";
    case ESW_PARAM: return "flows into a parameter that is not an object";
    case ESW_FOREIGN: return "merged with a value that is not a fresh object";
    case ESW_REUSED: return "used again after flowing into a merge";
    case ESW_SHAPE: return "merged with an object of another shape";
    case ESW_PAD: return "live into a landing pad";
    case ESW_BUDGET: return "too many fields in the function";
    case ESW_MERGED: return "merged with an object that escapes";
    case ESW_ELEM: return "an element at an index that is not a constant in range";
    case ESW_SHARED: return "handed to two merges on one edge";
    case ESW_LATER: return "used again after it escapes";
    default: return "?";
  }
}

static void es_escape(EsObj *o, uint16_t why) {
  if (o->escaped) return;
  o->escaped = true;
  o->why = why;
}

/* The runtime's warmc_ref_test, for an object whose exact type is known:
   -1 when the test is one this does not decide. */
static int es_ref_test(const Module *m, uint32_t ti, uint64_t imm, uint64_t id) {
  const CompType *t = &m->types[ti];
  uint32_t depth = RTEST_DEPTH(imm);
  switch (RTEST_KIND(imm) & ~RTK_NULLABLE) {
    case RTK_ANY: case RTK_FUNC: case RTK_EXTERN: case RTK_EXN: return 1;
    case RTK_NONE: case RTK_NOFUNC: case RTK_NOEXTERN: case RTK_NOEXN:
    case RTK_I31: case RTK_CONCRETE_FUNC: return 0;
    case RTK_EQ: return 1;
    case RTK_STRUCT: return t->kind == CT_STRUCT;
    case RTK_ARRAY: return t->kind == CT_ARRAY;
    case RTK_CONCRETE_ANY:
      return t->depth >= depth && t->display && t->display[depth] == id;
    default: return -1;
  }
}

/* The type a field or element is held in as an SSA value: i8 and i16 live
   in an i32, unmasked, and a read narrows them. */
static ValType es_var_type(const Module *m, const EsObj *o, uint32_t k) {
  const CompType *t = &m->types[o->ti];
  return vt_unpacked(o->kind == ESK_STRUCT ? t->fields[k].type : t->fields[0].type);
}

static ValType es_storage(const Module *m, const EsObj *o, uint32_t k) {
  const CompType *t = &m->types[o->ti];
  return o->kind == ESK_STRUCT ? t->fields[k].type : t->fields[0].type;
}

static void es_move_last_to(IrFunc *f, uint32_t bi, uint32_t to) {
  Block *b = &f->blocks[bi];
  uint32_t r = b->insts[b->ninsts - 1];
  memmove(&b->insts[to + 1], &b->insts[to],
          (b->ninsts - 1 - to) * sizeof(uint32_t));
  b->insts[to] = r;
}

/* A constant at the very top of the entry block, ahead of anything that
   could use it. */
static Ref es_entry_const(IrFunc *f, ValType t, uint64_t lo, uint64_t hi) {
  Ref c = ir_emit(f, f->entry, I_CONST, t, NULL, 0,
                  (t == VT_I32 || t == VT_F32) ? (lo & 0xffffffffu) : lo, hi);
  es_move_last_to(f, f->entry, 0);
  return c;
}

/* array.new's fill value as the element type holds it.  ir_build splits it
   into the two words the runtime takes, reinterpreting a float and
   extracting a v128's lanes; this undoes that, or answers REF_NONE. */
static Ref es_fill_value(IrFunc *f, ValType et, Ref lo, Ref hi) {
  Inst *l = &f->insts[lo], *h = &f->insts[hi];
  if (et == VT_F32 || et == VT_F64) {
    if (l->op == I_REINTERPRET && l->nargs == 1 &&
        f->insts[ir_resolve(f, ir_args(f, lo)[0])].type == et)
      return ir_resolve(f, ir_args(f, lo)[0]);
    if (l->op == I_CONST) return es_entry_const(f, et, l->imm, 0);
    return REF_NONE;
  }
  if (et == VT_V128) {
    if (l->op == I_CONST && h->op == I_CONST)
      return es_entry_const(f, VT_V128, l->imm, h->imm);
    if (l->op == I_VEXTRACTU && h->op == I_VEXTRACTU &&
        l->nargs == 1 && h->nargs == 1 && l->imm == 0 && h->imm == 1 &&
        ir_resolve(f, ir_args(f, lo)[0]) == ir_resolve(f, ir_args(f, hi)[0]))
      return ir_resolve(f, ir_args(f, lo)[0]);
    return REF_NONE;
  }
  return lo;
}

static uint32_t es_elem_index(IrFunc *f, const EsObj *o, Ref idx) {
  Inst *c = &f->insts[ir_resolve(f, idx)];
  if (c->op != I_CONST) return UINT32_MAX;
  uint64_t k = c->imm & 0xffffffffu;
  return k < o->len ? (uint32_t)k : UINT32_MAX;
}

/* Whether argument j of v is a use the object's fields can stand in for.
   *elem: an element access at an index this cannot place, which no
   escape point can rescue either. */
static bool es_use_local(IrFunc *f, const EsObj *o, Ref v, uint32_t j, bool *elem) {
  Module *m = f->m;
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  *elem = false;
  switch (in->op) {
    case I_FIELD_GET: case I_FIELD_SET:
      return j == 0 && o->kind == ESK_STRUCT &&
             gc_field_of_offset(m, vt_ref(false, (HeapType)o->ti), in->imm) != UINT32_MAX;
    case I_ARRAY_GET: case I_ARRAY_SET:
      if (j != 0 || o->kind != ESK_ARRAY) return false;
      *elem = o->dead_only || es_elem_index(f, o, a[1]) == UINT32_MAX;
      return true;
    case I_ARRAY_LEN: return j == 0 && o->kind == ESK_ARRAY;
    case I_TRAP_IF: return in->imm == TRAP_NULL_REF && in->nargs == 1;
    case I_REF_IS_NULL: case I_REF_CONVERT: return true;
    case I_REF_TEST: return es_ref_test(m, o->ti, in->imm, in->imm2) >= 0;
    case I_EQ: case I_NE: return in->nargs == 2 && !o->nullable;
    default: return false;
  }
}

static void es_point(EsObj *o, EsPt **pt, uint32_t *npt, uint32_t *cpt,
                     uint32_t c, uint32_t bi, Ref v, uint32_t succ, uint16_t why) {
  if (!o->npts) o->pwhy = why;
  if (o->npts > ESC_MAX_POINTS) { o->npts++; return; }   /* refused anyway */
  for (uint32_t k = *npt; k > 0; k--) {
    EsPt *p = &(*pt)[k - 1];
    if (p->obj == c && p->block == bi && p->inst == v && p->succ == succ) return;
  }
  if (*npt == *cpt) {
    *cpt = *cpt ? 2 * *cpt : 16;
    *pt = xrealloc(*pt, *cpt * sizeof **pt);
  }
  (*pt)[(*npt)++] = (EsPt){ c, bi, v, succ };
  o->npts++;
}

/* An edge out of a block that goes elsewhere too has an escape point of
   its own, not the block's end, which every way out would pass. */
static bool es_edge_alone(const Block *b) {
  uint32_t k = 0;
  for (uint32_t s = 0; s < b->nsuccs; s++) if (!b->succs[s].eh) k++;
  return k > 1;
}

/* The object made real at an escape point, at position *at of block bi:
   the allocation, then the stores of its fields as cur has them -- those
   still zero are the allocation's already.  *at ends past what it put. */
static Ref es_materialize(IrFunc *f, const EsObj *o, uint32_t bi, uint32_t *at,
                          const Ref *cur) {
  Module *m = f->m;
  const CompType *t = &m->types[o->ti];
  ValType rt = vt_ref(false, (HeapType)o->ti);
  Ref r;
  if (o->kind == ESK_STRUCT) {
    r = ir_emit(f, bi, I_STRUCT_NEW, rt, NULL, 0, o->ti, o->shape);
  } else {
    Ref len = ir_const(f, bi, VT_I32, o->len);
    es_move_last_to(f, bi, (*at)++);
    r = ir_emit(f, bi, I_ARRAY_NEW, rt, &len, 1, o->ti, o->shape);
  }
  es_move_last_to(f, bi, (*at)++);
  for (uint32_t k = 0; k < o->nvars; k++) {
    Ref val = ir_resolve(f, cur[o->var0 + k]);
    if (val == REF_NONE)
      warmc_fatal("internal: escape makes v%u with an unset field in %s",
                  o->def, m->funcs[f->func_index].sym);
    Inst *vi = &f->insts[val];
    if (vi->op == I_CONST && vi->imm == 0 && vi->imm2 == 0) continue;
    /* an initialising store, as ir_build makes one: log2 of the width */
    uint32_t w = vt_width(es_storage(m, o, k));
    uint64_t fl = 0;
    while ((1u << fl) < w) fl++;
    fl |= GCF_INIT;
    if (o->kind == ESK_STRUCT) {
      Ref sa[2] = { r, val };
      ir_emit(f, bi, I_FIELD_SET, 0, sa, 2, t->hdr_size + t->fields[k].offset, fl);
    } else {
      Ref idx = ir_const(f, bi, VT_I32, k);
      es_move_last_to(f, bi, (*at)++);
      Ref sa[3] = { r, idx, val };
      ir_emit(f, bi, I_ARRAY_SET, 0, sa, 3, o->ti, fl);
    }
    es_move_last_to(f, bi, (*at)++);
  }
  return r;
}

static bool pass_escape(IrFunc *f, bool partial) {
  uint32_t n = f->ninsts, nb = f->nblocks;
  if (!n || !f->norder) return false;
  Module *m = f->m;
  bool dbg = warmc_getenv("WARMC_ESCAPE_DEBUG") != NULL;

  EsObj *obj = NULL;
  uint32_t nobj = 0, cobj = 0;
  int32_t *own = xmalloc(n * sizeof(int32_t));
  for (uint32_t v = 0; v < n; v++) own[v] = -1;
  /* own[] covers the instructions there were; anything made since is not
     an object */
#define OWN(r_) ((uint32_t)(r_) < n ? own[(r_)] : -1)

#define ES_ADD(o_) do { \
    if (nobj == cobj) { cobj = cobj ? 2 * cobj : 16; \
                        obj = xrealloc(obj, cobj * sizeof *obj); } \
    obj[nobj++] = (o_); } while (0)

  for (uint32_t oi = 0; oi < f->norder && nobj < ESC_MAX_OBJS; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts && nobj < ESC_MAX_OBJS; i++) {
      Ref v = b->insts[i];
      /* a fill constant lands at the top of the entry block, which moves an
         allocation there to the next slot: seen once, it is skipped */
      if (v >= n || own[v] >= 0) continue;
      Inst *in = &f->insts[v];
      EsObj o;
      memset(&o, 0, sizeof o);
      o.def = v; o.block = bi; o.ti = (uint32_t)in->imm; o.fill = REF_NONE;
      o.shape = in->imm2;
      if (in->op == I_STRUCT_NEW && in->nargs == 0) {
        if (o.ti >= m->ntypes || m->types[o.ti].kind != CT_STRUCT) continue;
        o.kind = ESK_STRUCT;
        o.nvars = m->types[o.ti].nfields;
      } else if (in->op == I_ARRAY_NEW && (in->nargs == 1 || in->nargs == 3)) {
        if (o.ti >= m->ntypes || m->types[o.ti].kind != CT_ARRAY) continue;
        Ref *a = ir_args(f, v);
        Inst *lc = &f->insts[ir_resolve(f, a[0])];
        if (lc->op != I_CONST) continue;
        o.kind = ESK_ARRAY;
        o.len = (uint32_t)(lc->imm & 0xffffffffu);
        ValType et = m->types[o.ti].fields[0].type;
        uint64_t bytes = (uint64_t)m->types[o.ti].hdr_size +
                         (uint64_t)o.len * vt_width(et);
        if (bytes > ESC_DEAD_BYTES) continue;
        if (o.len > ESC_MAX_ELEMS) {
          o.dead_only = true;
        } else {
          o.nvars = o.len;
          if (in->nargs == 3 && o.len) {
            o.fill = es_fill_value(f, vt_unpacked(et), ir_resolve(f, a[1]),
                                   ir_resolve(f, a[2]));
            if (o.fill == REF_NONE) continue;
          }
        }
      } else {
        continue;
      }
      own[v] = (int32_t)nobj;
      ES_ADD(o);
    }
  }
  if (!nobj) { xfree(own); xfree(obj); return false; }

  IrEdgeIdx ei = ir_edge_index(f);
  /* merges that could be null but escaped anyway: parameters like any
     other, on the next try */
  uint8_t *demote = xcalloc(n, 1);
  uint32_t nalloc = nobj, tries = 0;
#define ES_ISNULL(r_) ((r_) < n && vt_is_ref(f->insts[r_].type) && \
                       (f->insts[r_].op == I_REF_NULL || \
                        (f->insts[r_].op == I_CONST && f->insts[r_].imm == 0)))
restart:;

  /* Which allocations can reach each reference parameter and conversion,
     to a fixpoint over the CFG: a set only grows, so cycles of parameters
     settle without guessing.  One origin and the parameter is an alias of
     it -- SSA dominance makes it the object's current instance on every
     edge; two or more, or null too, and it is a merge, an object of its
     own.  Anything else reaching it, and it is not an object at all. */
  uint32_t W = (nalloc + 63) / 64;
  uint64_t *org = xcalloc((size_t)n * W, sizeof(uint64_t));
  uint8_t *forg = xcalloc(n, 1);
  uint8_t *nul = xcalloc(n, 1);
  for (uint32_t c = 0; c < nalloc; c++)
    org[(size_t)obj[c].def * W + c / 64] |= 1ull << (c % 64);
  /* the function's own parameters come from the caller */
  for (uint32_t p = 0; p < f->blocks[f->entry].nparams; p++)
    if (f->blocks[f->entry].params[p] < n) forg[f->blocks[f->entry].params[p]] = 1;
  for (uint32_t v = 0; v < n; v++) if (demote[v]) forg[v] = 1;
#define ES_TRACKED(r_) ((r_) < n && (OWN(r_) >= 0 || f->insts[r_].op == I_PARAM || \
                                     f->insts[r_].op == I_REF_CONVERT))
#define ES_MERGE_IN(to_, from_) do { \
    Ref fr_ = (from_); bool fg_ = false; \
    if (ES_ISNULL(fr_)) { \
      if (!nul[(to_)]) { nul[(to_)] = 1; again = true; } \
    } else if (ES_TRACKED(fr_)) { \
      for (uint32_t w_ = 0; w_ < W; w_++) { \
        uint64_t nw_ = org[(size_t)(to_) * W + w_] | org[(size_t)fr_ * W + w_]; \
        if (nw_ != org[(size_t)(to_) * W + w_]) { org[(size_t)(to_) * W + w_] = nw_; again = true; } } \
      fg_ = forg[fr_]; \
      if (nul[fr_] && !nul[(to_)]) { nul[(to_)] = 1; again = true; } \
    } else fg_ = true; \
    if (fg_ && !forg[(to_)]) { forg[(to_)] = 1; again = true; } } while (0)
  for (bool again = true; again;) {
    again = false;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      if (bi != f->entry)
        for (uint32_t p = 0; p < b->nparams; p++) {
          Ref P = b->params[p];
          if (P >= n || !vt_is_ref(f->insts[P].type)) continue;
          for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
            Block *sb = &f->blocks[ei.src[k]];
            if (!sb->reachable || sb->succs[ei.si[k]].eh) continue;
            Ref a = ir_resolve(f, sb->succs[ei.si[k]].args[p]);
            if (a != P) ES_MERGE_IN(P, a);
          }
        }
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref v = b->insts[i];
        if (v < n && f->insts[v].op == I_REF_CONVERT && f->insts[v].nargs == 1)
          ES_MERGE_IN(v, ir_resolve(f, ir_args(f, v)[0]));
      }
    }
  }
#undef ES_MERGE_IN
#undef ES_TRACKED

  /* a parameter or conversion only ever null -- a loop variable that starts
     null and is not set round the loop -- is as good as a null constant */
  uint8_t *nullonly = xcalloc(n, 1);
  for (uint32_t v = 0; v < n; v++) {
    if (!nul[v] || forg[v]) continue;
    bool none = true;
    for (uint32_t w = 0; w < W && none; w++) none = !org[(size_t)v * W + w];
    nullonly[v] = none;
  }
#define ES_NULLV(r_) (ES_ISNULL(r_) || ((r_) < n && nullonly[(r_)]))

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (bi == f->entry) continue;
    for (uint32_t p = 0; p < b->nparams; p++) {
      Ref P = b->params[p];
      if (P >= n || !vt_is_ref(f->insts[P].type) || forg[P]) continue;
      int32_t one = -1;
      uint32_t count = 0;
      for (uint32_t c = 0; c < nalloc; c++)
        if (org[(size_t)P * W + c / 64] >> (c % 64) & 1) { if (one < 0) one = (int32_t)c; count++; }
      if (!count) continue;
      if (count == 1 && !nul[P]) { own[P] = one; continue; }
      if (nobj >= ESC_MAX_OBJS) continue;
      EsObj o;
      memset(&o, 0, sizeof o);
      o.def = P; o.block = bi; o.phi = true; o.fill = REF_NONE;
      o.nullable = nul[P];
      o.ti = obj[one].ti; o.kind = obj[one].kind;
      o.len = obj[one].len; o.nvars = obj[one].nvars; o.shape = obj[one].shape;
      own[P] = (int32_t)nobj;
      ES_ADD(o);
      /* every allocation that reaches it must have its shape */
      for (uint32_t c = 0; c < nalloc; c++)
        if ((org[(size_t)P * W + c / 64] >> (c % 64) & 1) &&
            (obj[c].ti != o.ti || obj[c].kind != o.kind || obj[c].len != o.len ||
             obj[c].dead_only)) {
          es_escape(&obj[c], ESW_SHAPE);
          es_escape(&obj[own[P]], ESW_SHAPE);
        }
    }
  }
  /* a conversion is the object it converts, whatever that turned out to be */
  for (bool again = true; again;) {
    again = false;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      Block *b = &f->blocks[f->order[oi]];
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref v = b->insts[i];
        if (v >= n || f->insts[v].op != I_REF_CONVERT || own[v] >= 0 ||
            f->insts[v].nargs != 1 || forg[v])
          continue;
        int32_t c = OWN(ir_resolve(f, ir_args(f, v)[0]));
        if (c >= 0) { own[v] = c; again = true; }
      }
    }
  }
  xfree(org); xfree(nul);

  /* What flows where.  An object meeting a parameter that is not one
     escapes there, at the end of the edge's block; one flowing into a merge
     is recorded; and an object handed to two parameters on one edge, a
     merge among them, would be two objects sharing fields -- it escapes. */
  EsFlow *flow = NULL;
  uint32_t nflow = 0, cflow = 0;
  EsPt *pt = NULL;
  uint32_t npt = 0, cpt = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      Edge *e = &b->succs[s];
      if (e->eh) continue;
      Block *t = &f->blocks[e->target];
      for (uint32_t j = 0; j < e->nargs && j < t->nparams; j++) {
        Ref a = ir_resolve(f, e->args[j]);
        int32_t c = OWN(a);
        Ref P = t->params[j];
        int32_t d = OWN(P);
        if (c < 0) {
          if (d >= 0 && a != P && !(obj[d].nullable && ES_NULLV(a)))
            es_escape(&obj[d], ESW_FOREIGN);
          continue;
        }
        if (d < 0) {
          es_point(&obj[c], &pt, &npt, &cpt, (uint32_t)c, bi, REF_NONE,
                   es_edge_alone(b) ? s : UINT32_MAX, ESW_PARAM);
          continue;
        }
        for (uint32_t j2 = 0; j2 < j; j2++) {
          int32_t c2 = OWN(ir_resolve(f, e->args[j2]));
          int32_t d2 = OWN(t->params[j2]);
          if (c2 == c && d2 >= 0 && (obj[d].phi || obj[d2].phi)) {
            es_escape(&obj[c], ESW_SHARED);
            es_escape(&obj[d], ESW_SHARED);
            es_escape(&obj[d2], ESW_SHARED);
          }
        }
        if (c == d) continue;
        if (!obj[d].phi || obj[d].def != P) {
          es_escape(&obj[c], ESW_SHAPE);
          es_escape(&obj[d], ESW_SHAPE);
          continue;
        }
        if (nflow == cflow) {
          cflow = cflow ? 2 * cflow : 16;
          flow = xrealloc(flow, cflow * sizeof *flow);
        }
        flow[nflow++] = (EsFlow){ (uint32_t)c, (uint32_t)d };
      }
    }
  }

  /* Every use: allowed, an escape point, or the object escapes -- and the
     first two recorded with their block. */
  EsUse *use = NULL;
  uint32_t nuse = 0, cuse = 0;
#define ES_USE(c_, b_) do { \
    if (nuse == cuse) { cuse = cuse ? 2 * cuse : 64; \
                        use = xrealloc(use, cuse * sizeof *use); } \
    use[nuse++] = (EsUse){ (uint32_t)(c_), (b_) }; } while (0)

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      Ref *a = ir_args(f, v);
      for (uint32_t j = 0; j < in->nargs; j++) {
        int32_t c = OWN(ir_resolve(f, a[j]));
        if (c < 0) continue;
        EsObj *o = &obj[c];
        bool elem;
        ES_USE(c, bi);
        if (es_use_local(f, o, v, j, &elem)) {
          if (elem) es_escape(o, ESW_ELEM);
          /* an initialising store or a conversion the allocation would
             have had to do anyway */
          bool init = (in->op == I_FIELD_SET || in->op == I_ARRAY_SET) &&
                      (in->imm2 & GCF_INIT);
          if (!init && in->op != I_REF_CONVERT) o->benefit = true;
        } else {
          es_point(o, &pt, &npt, &cpt, (uint32_t)c, bi, v, UINT32_MAX,
                   (uint16_t)(ESW_OP + in->op));
        }
      }
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) {
      int32_t c = OWN(ir_resolve(f, b->term_arg));
      if (c >= 0) es_escape(&obj[c], ESW_COND);
    }
    for (uint32_t i = 0; i < b->nret; i++) {
      int32_t c = OWN(ir_resolve(f, b->ret_args[i]));
      if (c >= 0) {
        ES_USE(c, bi);
        es_point(&obj[c], &pt, &npt, &cpt, (uint32_t)c, bi, REF_NONE, UINT32_MAX,
                 ESW_RETURN);
      }
    }
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      Edge *e = &b->succs[s];
      for (uint32_t j = 0; j < e->nargs; j++) {
        int32_t c = OWN(ir_resolve(f, e->args[j]));
        if (c >= 0) ES_USE(c, bi);   /* the fields are needed at the edge */
      }
    }
  }

  /* A merge owns its fields only if what flows in is not used again: no use
     of a source is reachable from the merge without allocating it anew.  A
     source made in the merge's own block is made anew on every entry to it,
     before anything there can use it, so nothing reachable can see the old
     one -- except through the merge, which is the point.  (The same object
     handed to both on one edge was refused above.) */
  uint8_t *reach = xmalloc(nb ? nb : 1);
  uint32_t *stack = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  for (uint32_t k = 0; k < nflow; k++) {
    EsObj *src = &obj[flow[k].src], *dst = &obj[flow[k].dst];
    if (src->escaped || dst->escaped || src->block == dst->block) continue;
    memset(reach, 0, nb);
    uint32_t sp = 0;
    reach[dst->block] = 1;
    stack[sp++] = dst->block;
    while (sp) {
      Block *b = &f->blocks[stack[--sp]];
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        uint32_t t = b->succs[s].target;
        if (t == src->block || reach[t]) continue;
        reach[t] = 1;
        stack[sp++] = t;
      }
    }
    for (uint32_t u = 0; u < nuse; u++)
      if (use[u].obj == flow[k].src && reach[use[u].block]) {
        es_escape(src, ESW_REUSED);
        es_escape(dst, ESW_REUSED);
        break;
      }
  }

  /* Each escape point must be its object's last use.  Nothing after it in
     its block may name the object -- at the block's end or on an edge,
     nothing but the other returns and parameters that make up the same
     point -- and no use, this one included, may be reachable from there
     without passing the object's block, where it is made anew. */
  for (uint32_t k = 0; k < npt; k++) {
    EsPt *p = &pt[k];
    EsObj *o = &obj[p->obj];
    if (o->escaped) continue;
    if (!partial || o->nullable || o->dead_only || o->npts > ESC_MAX_POINTS) {
      es_escape(o, o->pwhy);
      continue;
    }
    Block *b = &f->blocks[p->block];
    int32_t c = (int32_t)p->obj;
    bool later = false;
    if (p->inst != REF_NONE) {
      uint32_t i = 0;
      while (i < b->ninsts && b->insts[i] != p->inst) i++;
      for (i++; i < b->ninsts && !later; i++) {
        Inst *in = &f->insts[b->insts[i]];
        Ref *a = ir_args(f, b->insts[i]);
        for (uint32_t j = 0; j < in->nargs; j++)
          if (OWN(ir_resolve(f, a[j])) == c) later = true;
      }
      for (uint32_t i2 = 0; i2 < b->nret; i2++)
        if (OWN(ir_resolve(f, b->ret_args[i2])) == c) later = true;
    }
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      if (p->succ != UINT32_MAX && s != p->succ) continue;
      Edge *e = &b->succs[s];
      Block *t = &f->blocks[e->target];
      for (uint32_t j = 0; j < e->nargs; j++)
        if (OWN(ir_resolve(f, e->args[j])) == c &&
            (p->inst != REF_NONE || j >= t->nparams || OWN(t->params[j]) >= 0))
          later = true;
    }
    if (!later) {
      memset(reach, 0, nb);
      uint32_t sp = 0;
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        uint32_t t = b->succs[s].target;
        if (p->succ != UINT32_MAX && s != p->succ) continue;
        if (t == o->block || reach[t]) continue;
        reach[t] = 1;
        stack[sp++] = t;
      }
      while (sp) {
        Block *rb = &f->blocks[stack[--sp]];
        for (uint32_t s = 0; s < rb->nsuccs; s++) {
          uint32_t t = rb->succs[s].target;
          if (t == o->block || reach[t]) continue;
          reach[t] = 1;
          stack[sp++] = t;
        }
      }
      for (uint32_t u = 0; u < nuse && !later; u++)
        if (use[u].obj == p->obj && reach[use[u].block]) later = true;
    }
    if (later) es_escape(o, ESW_LATER);
  }

  /* And something to gain: a path from the allocation that reaches no
     escape point, back round to the allocation or out of the function, and
     not into a trap; or, with no edge to split, a use its fields answer.
     An edge split on every way out saves no allocation and costs a jump. */
  for (uint32_t c = 0; c < nobj; c++) {
    EsObj *o = &obj[c];
    if (o->escaped || !o->npts) continue;
    bool edges = false;
    for (uint32_t k = 0; k < npt; k++)
      if (pt[k].obj == c && pt[k].succ != UINT32_MAX) edges = true;
    if (o->benefit && !edges) continue;
    memset(reach, 0, nb);
    bool avoid = !f->blocks[o->block].diverges;
    for (uint32_t k = 0; k < npt; k++)
      if (pt[k].obj == c && pt[k].succ == UINT32_MAX) {
        reach[pt[k].block] = 1;
        if (pt[k].block == o->block) avoid = false;
      }
    if (avoid) {
      avoid = false;
      uint32_t sp = 0;
      stack[sp++] = o->block;
      while (sp && !avoid) {
        uint32_t rbi = stack[--sp];
        Block *rb = &f->blocks[rbi];
        if (!rb->nsuccs) avoid = true;
        for (uint32_t s = 0; s < rb->nsuccs && !avoid; s++) {
          bool cut = false;
          for (uint32_t k = 0; k < npt && !cut; k++)
            cut = pt[k].obj == c && pt[k].block == rbi && pt[k].succ == s;
          if (cut) continue;
          uint32_t t = rb->succs[s].target;
          if (t == o->block) avoid = true;
          if (reach[t] || f->blocks[t].diverges) continue;
          reach[t] = 1;
          stack[sp++] = t;
        }
      }
    }
    if (!avoid) es_escape(o, ESW_GAIN);
  }

  /* The flag a merge that may be null carries, and so everything that
     merges with it, so that each edge has one to hand on. */
  for (uint32_t c = 0; c < nobj; c++) obj[c].flagged = obj[c].nullable;
  for (bool more = true; more;) {
    more = false;
    for (uint32_t k = 0; k < nflow; k++) {
      EsObj *src = &obj[flow[k].src], *dst = &obj[flow[k].dst];
      if (src->flagged != dst->flagged) { src->flagged = dst->flagged = true; more = true; }
    }
  }

  /* Liveness, and the parameters it asks for; an escape anywhere spreads to
     everything it merges with, and asks the question again. */
  uint8_t *live = xmalloc(nb ? nb : 1);
  uint8_t *needp = xcalloc((size_t)(nb ? nb : 1) * nobj, 1);
  for (bool again = true; again;) {
    again = false;
    for (bool spread = true; spread;) {
      spread = false;
      for (uint32_t k = 0; k < nflow; k++) {
        EsObj *src = &obj[flow[k].src], *dst = &obj[flow[k].dst];
        if (src->escaped != dst->escaped) {
          es_escape(src, ESW_MERGED);
          es_escape(dst, ESW_MERGED);
          spread = true;
        }
      }
    }
    for (uint32_t c = 0; c < nobj; c++) {
      EsObj *o = &obj[c];
      memset(&needp[(size_t)c * nb], 0, nb);
      if (o->escaped || !ES_NV(o)) continue;
      memset(live, 0, nb);
      uint32_t sp = 0;
      for (uint32_t u = 0; u < nuse; u++)
        if (use[u].obj == c && use[u].block != o->block && !live[use[u].block]) {
          live[use[u].block] = 1;
          stack[sp++] = use[u].block;
        }
      while (sp) {
        uint32_t bi = stack[--sp];
        for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
          uint32_t pb = ei.src[k];
          if (!f->blocks[pb].reachable || pb == o->block || live[pb]) continue;
          live[pb] = 1;
          stack[sp++] = pb;
        }
      }
      if (live[f->entry]) { es_escape(o, ESW_PAD); again = true; continue; }
      for (uint32_t bi = 0; bi < nb && !o->escaped; bi++) {
        if (!live[bi] || bi == o->block) continue;
        uint32_t first = UINT32_MAX;
        bool merge = false, eh = false;
        for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
          Block *sb = &f->blocks[ei.src[k]];
          if (!sb->reachable) continue;
          if (sb->succs[ei.si[k]].eh) eh = true;
          if (first == UINT32_MAX) first = ei.src[k];
          else if (first != ei.src[k]) merge = true;
        }
        if (f->blocks[bi].is_pad || eh) { es_escape(o, ESW_PAD); again = true; }
        else if (merge) needp[(size_t)c * nb + bi] = 1;
      }
    }
  }

  uint32_t V = 0;
  for (uint32_t c = 0; c < nobj; c++) {
    EsObj *o = &obj[c];
    if (o->escaped) continue;
    if (V + ES_NV(o) > ESC_MAX_VARS || (uint64_t)(V + ES_NV(o)) * nb > ESC_MAX_CELLS) {
      es_escape(o, ESW_BUDGET);
      continue;
    }
    o->var0 = V;
    V += ES_NV(o);
  }
  /* a budget refusal can strand what merges with it */
  for (bool spread = true; spread;) {
    spread = false;
    for (uint32_t k = 0; k < nflow; k++) {
      EsObj *src = &obj[flow[k].src], *dst = &obj[flow[k].dst];
      if (src->escaped != dst->escaped) {
        es_escape(src, ESW_MERGED);
        es_escape(dst, ESW_MERGED);
        spread = true;
      }
    }
  }

  /* A merge that could be null and escapes anyway would take what flows
     into it down too, where on its own each would at worst have been made
     on its edge.  It goes back to being a parameter, which is how the
     question is asked again -- every merge of the kind, should the tries
     run out. */
  bool redo = false;
  for (uint32_t c = 0; c < nobj; c++)
    if (obj[c].phi && obj[c].nullable && (obj[c].escaped || tries >= 8)) {
      demote[obj[c].def] = 1;
      redo = true;
    }
  if (redo) {
    tries++;
    xfree(forg); xfree(nullonly); xfree(flow); xfree(use); xfree(pt);
    xfree(reach); xfree(stack); xfree(live); xfree(needp);
    for (uint32_t v = 0; v < n; v++) own[v] = -1;
    nobj = nalloc;
    for (uint32_t c = 0; c < nalloc; c++) {
      EsObj *o = &obj[c];
      o->escaped = o->benefit = o->flagged = false;
      o->why = o->pwhy = 0;
      o->npts = o->var0 = 0;
      own[o->def] = (int32_t)c;
    }
    goto restart;
  }

  bool any = false;
  for (uint32_t c = 0; c < nobj; c++) if (!obj[c].escaped) any = true;

  if (dbg)
    for (uint32_t c = 0; c < nobj; c++) {
      EsObj *o = &obj[c];
      const char *kind = o->nullable ? "nullable" : o->phi ? "merge"
                       : o->kind == ESK_STRUCT ? "struct" : "array";
      fprintf(stderr, "escape %s v%u %s d%u: ", m->funcs[f->func_index].sym,
              o->def, kind, f->blocks[o->block].loop_depth);
      if (!o->escaped && o->npts)
        fprintf(stderr, "partial: made at %u escape point%s\n", o->npts, o->npts == 1 ? "" : "s");
      else if (!o->escaped) fprintf(stderr, "replaced\n");
      else {
        /* nothing to gain: the escape it was refused for, on every path */
        uint16_t w = o->why == ESW_GAIN ? o->pwhy : o->why;
        if (w >= ESW_OP) fprintf(stderr, "escapes: use by %s", ir_op_name(w - ESW_OP));
        else fprintf(stderr, "escapes: %s", es_why(w));
        fprintf(stderr, o->why == ESW_GAIN ? " on every path\n" : "\n");
      }
    }

  bool changed = false;
  if (any) {
    /* The constants the walk needs, at the top of the entry block.  Every
       zero it can want is made here, before it starts: one made during it
       would land at the top of the entry block, under the walk's feet. */
    Ref *zero_cache = NULL; ValType *zero_type = NULL; uint32_t nzero = 0;
#define ES_ZERO(t_, out_) do { ValType tt_ = (t_); Ref zz_ = REF_NONE; \
      for (uint32_t z_ = 0; z_ < nzero; z_++) if (zero_type[z_] == tt_) zz_ = zero_cache[z_]; \
      if (zz_ == REF_NONE) { zz_ = es_entry_const(f, tt_, 0, 0); \
        zero_cache = xrealloc(zero_cache, (nzero + 1) * sizeof *zero_cache); \
        zero_type = xrealloc(zero_type, (nzero + 1) * sizeof *zero_type); \
        zero_type[nzero] = tt_; zero_cache[nzero++] = zz_; } \
      (out_) = zz_; } while (0)
    Ref mask8 = es_entry_const(f, VT_I32, 0xff, 0);
    Ref mask16 = es_entry_const(f, VT_I32, 0xffff, 0);
    Ref one = es_entry_const(f, VT_I32, 1, 0);

    ValType *vtype = xmalloc((V ? V : 1) * sizeof(ValType));
    for (uint32_t c = 0; c < nobj; c++)
      if (!obj[c].escaped) {
        for (uint32_t k = 0; k < obj[c].nvars; k++)
          vtype[obj[c].var0 + k] = es_var_type(m, &obj[c], k);
        if (obj[c].flagged) vtype[obj[c].var0 + obj[c].nvars] = VT_I32;
      }
    for (uint32_t k = 0; k < V; k++) {
      bool used = false;
      for (uint32_t c = 0; c < nobj && !used; c++)
        used = !obj[c].escaped && k >= obj[c].var0 && k < obj[c].var0 + ES_NV(&obj[c]);
      if (used) { Ref z; ES_ZERO(vtype[k], z); (void)z; }
    }

    /* The fields' parameters: a merge's own at its block, and one at each
       merge block a field is live into. */
    size_t cells = (size_t)nb * (V ? V : 1);
    Ref *pref = xmalloc(cells * sizeof(Ref));
    Ref *outv = xmalloc(cells * sizeof(Ref));
    for (size_t x = 0; x < cells; x++) { pref[x] = REF_NONE; outv[x] = REF_NONE; }
    uint32_t *nparams0 = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
    for (uint32_t bi = 0; bi < nb; bi++) nparams0[bi] = f->blocks[bi].nparams;
    uint32_t pv_base = f->ninsts;
    for (uint32_t c = 0; c < nobj; c++) {
      EsObj *o = &obj[c];
      if (o->escaped) continue;
      for (uint32_t bi = 0; bi < nb; bi++) {
        if (!(o->phi && bi == o->block) && !needp[(size_t)c * nb + bi]) continue;
        for (uint32_t k = 0; k < ES_NV(o); k++)
          pref[(size_t)bi * V + o->var0 + k] = ir_add_param(f, bi, vtype[o->var0 + k]);
      }
    }
    uint32_t npv = f->ninsts - pv_base;
    uint32_t *pvar = xmalloc((npv ? npv : 1) * sizeof(uint32_t));
    for (uint32_t bi = 0; bi < nb; bi++)
      for (uint32_t k = 0; k < V; k++) {
        Ref r = pref[(size_t)bi * V + k];
        if (r != REF_NONE) pvar[r - pv_base] = k;
      }

    Ref *cur = xmalloc((V ? V : 1) * sizeof(Ref));
    uint8_t *done = xcalloc(nb ? nb : 1, 1);
    uint32_t forwarded = 0, made = 0;
    /* where the live objects' escape points are */
    uint8_t *ptinst = xcalloc(n, 1);
    uint8_t *ptend = xcalloc(nb ? nb : 1, 1);
    for (uint32_t k = 0; k < npt; k++) {
      if (obj[pt[k].obj].escaped) continue;
      if (pt[k].inst != REF_NONE) ptinst[pt[k].inst] = 1;
      else if (pt[k].succ == UINT32_MAX) ptend[pt[k].block] = 1;
    }
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      uint32_t single = UINT32_MAX;
      bool merge = false;
      for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
        if (!f->blocks[ei.src[k]].reachable) continue;
        if (single == UINT32_MAX) single = ei.src[k];
        else if (single != ei.src[k]) merge = true;
      }
      for (uint32_t k = 0; k < V; k++) {
        Ref p = pref[(size_t)bi * V + k];
        cur[k] = p != REF_NONE ? p
               : (!merge && single != UINT32_MAX && done[single])
                   ? outv[(size_t)single * V + k] : REF_NONE;
      }
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref v = b->insts[i];
        if (v < n && ptinst[v]) {
          /* an escape point: each object it hands over is made here, once,
             and handed over instead */
          uint32_t na = f->insts[v].nargs;
          int32_t *mobj = xmalloc(na * sizeof(int32_t));
          Ref *mref = xmalloc(na * sizeof(Ref));
          for (uint32_t j = 0; j < na; j++) {
            mobj[j] = OWN(ir_resolve(f, ir_args(f, v)[j]));
            bool elem;
            if (mobj[j] < 0 || obj[mobj[j]].escaped ||
                es_use_local(f, &obj[mobj[j]], v, j, &elem)) { mobj[j] = -1; continue; }
            mref[j] = REF_NONE;
            for (uint32_t j2 = 0; j2 < j; j2++)
              if (mobj[j2] == mobj[j]) mref[j] = mref[j2];
            if (mref[j] == REF_NONE) {
              mref[j] = es_materialize(f, &obj[mobj[j]], bi, &i, cur);
              made++;
            }
            ir_args(f, v)[j] = mref[j];
          }
          xfree(mobj); xfree(mref);
          b = &f->blocks[bi];
        }
        Inst *in = &f->insts[v];
        Ref *a = ir_args(f, v);
        int32_t self = OWN(v);
        if (self >= 0 && obj[self].def == v && !obj[self].escaped) {
          EsObj *o = &obj[self];
          for (uint32_t k = 0; k < o->nvars; k++) {
            Ref z;
            if (o->fill != REF_NONE) z = o->fill;
            else ES_ZERO(vtype[o->var0 + k], z);
            cur[o->var0 + k] = z;
          }
          if (o->flagged) cur[o->var0 + o->nvars] = one;   /* an object, not null */
          continue;
        }
        int32_t c = in->nargs ? OWN(ir_resolve(f, a[0])) : -1;
        if (in->op == I_EQ || in->op == I_NE) {
          int32_t c1 = in->nargs == 2 ? OWN(ir_resolve(f, a[1])) : -1;
          bool live0 = c >= 0 && !obj[c].escaped, live1 = c1 >= 0 && !obj[c1].escaped;
          if (live0 || live1) {
            bool same = c == c1;
            replace_with_const(f, v, (in->op == I_EQ) == same ? 1 : 0);
          }
          continue;
        }
        if (c < 0 || obj[c].escaped) continue;
        EsObj *o = &obj[c];
        /* A merge that may be null: its flag answers for its null checks,
           and an access that would have trapped on null traps on that. */
        Ref flag = REF_NONE;
        if (o->nullable) {
          flag = ir_resolve(f, cur[o->var0 + o->nvars]);
          if (flag == REF_NONE)
            warmc_fatal("internal: escape has no flag for v%u in %s",
                        o->def, m->funcs[f->func_index].sym);
          bool access = in->op == I_FIELD_GET || in->op == I_FIELD_SET ||
                        in->op == I_ARRAY_GET || in->op == I_ARRAY_SET ||
                        in->op == I_ARRAY_LEN;
          if (access && (in->imm2 & GCF_NULLCHK)) {
            ir_emit(f, bi, I_TRAP_IF, VT_BOT, &flag, 1, TRAP_NULL_REF, 0);
            b = &f->blocks[bi];
            es_move_last_to(f, bi, i);
            i++;
            in = &f->insts[v];
            a = ir_args(f, v);
          }
        }
#define ES_IS_NULL_OF_FLAG() do { \
          Ref z_ = ir_emit(f, bi, I_EQZ, VT_I32, &flag, 1, 0, 0); \
          b = &f->blocks[bi]; \
          es_move_last_to(f, bi, i); \
          i++; \
          in = &f->insts[v]; \
          ir_replace(f, v, z_); \
          in->op = I_NOP; \
          in->nargs = 0; } while (0)
        switch (in->op) {
          case I_FIELD_GET: case I_ARRAY_GET: {
            uint32_t k = in->op == I_FIELD_GET
                       ? gc_field_of_offset(m, vt_ref(false, (HeapType)o->ti), in->imm)
                       : es_elem_index(f, o, a[1]);
            Ref val = ir_resolve(f, cur[o->var0 + k]);
            if (val == REF_NONE)
              warmc_fatal("internal: escape reads an unset field of v%u in %s",
                          o->def, m->funcs[f->func_index].sym);
            ValType st = es_storage(m, o, k);
            if (st == VT_I8 || st == VT_I16) {
              Ref nv;
              if (in->imm2 & GCF_SIGNED)
                nv = ir_emit(f, bi, st == VT_I8 ? I_EXT8S : I_EXT16S, VT_I32,
                             &val, 1, 0, 0);
              else {
                Ref aa[2] = { val, st == VT_I8 ? mask8 : mask16 };
                nv = ir_emit(f, bi, I_AND, VT_I32, aa, 2, 0, 0);
              }
              b = &f->blocks[bi];
              es_move_last_to(f, bi, i);
              i++;
              val = nv;
            }
            in = &f->insts[v];
            ir_replace(f, v, val);
            in->op = I_NOP;
            in->nargs = 0;
            forwarded++;
            break;
          }
          case I_FIELD_SET: case I_ARRAY_SET: {
            uint32_t k = in->op == I_FIELD_SET
                       ? gc_field_of_offset(m, vt_ref(false, (HeapType)o->ti), in->imm)
                       : es_elem_index(f, o, a[1]);
            cur[o->var0 + k] = ir_resolve(f, a[in->op == I_FIELD_SET ? 1 : 2]);
            in->op = I_NOP;
            in->nargs = 0;
            break;
          }
          case I_ARRAY_LEN: replace_with_const(f, v, o->len); break;
          case I_REF_IS_NULL:
            if (o->nullable) ES_IS_NULL_OF_FLAG();
            else replace_with_const(f, v, 0);
            break;
          case I_REF_TEST: {
            /* what it answers for the object, and for null */
            int r1 = es_ref_test(m, o->ti, in->imm, in->imm2);
            int r0 = (RTEST_KIND(in->imm) & RTK_NULLABLE) ? 1 : 0;
            if (!o->nullable || r1 == r0) replace_with_const(f, v, (uint64_t)r1);
            else if (r1) { ir_replace(f, v, flag); in->op = I_NOP; in->nargs = 0; }
            else ES_IS_NULL_OF_FLAG();
            break;
          }
          case I_TRAP_IF:
            if (o->nullable) a[0] = flag;
            else { in->op = I_NOP; in->nargs = 0; }
            break;
          default: break;
        }
#undef ES_IS_NULL_OF_FLAG
      }
      if (ptend[bi]) {
        /* escape points at the block's end: what it returns, and what its
           edges hand to parameters that are not objects -- each object made
           once, at the end */
        int32_t mobj[ESC_MAX_OBJS];
        Ref mref[ESC_MAX_OBJS];
        uint32_t nm = 0;
#define ES_MADE(r_) do { int32_t mc_ = OWN(ir_resolve(f, (r_))); \
          if (mc_ < 0 || obj[mc_].escaped) break; \
          uint32_t q_ = 0; \
          while (q_ < nm && mobj[q_] != mc_) q_++; \
          if (q_ == nm) { \
            uint32_t at_ = f->blocks[bi].ninsts; \
            mobj[nm] = mc_; mref[nm++] = es_materialize(f, &obj[mc_], bi, &at_, cur); \
            made++; } \
          (r_) = mref[q_]; } while (0)
        b = &f->blocks[bi];
        for (uint32_t r = 0; r < b->nret; r++) ES_MADE(b->ret_args[r]);
        for (uint32_t s = 0; s < b->nsuccs && !es_edge_alone(b); s++) {
          Edge *e = &b->succs[s];
          Block *tb = &f->blocks[e->target];
          for (uint32_t j = 0; j < e->nargs && j < nparams0[e->target]; j++)
            if (OWN(tb->params[j]) < 0) ES_MADE(e->args[j]);
        }
#undef ES_MADE
      }
      for (uint32_t k = 0; k < V; k++) outv[(size_t)bi * V + k] = cur[k];
      done[bi] = 1;
    }

    /* Arguments for the new parameters, on every edge into their blocks. */
    for (uint32_t bi = 0; bi < nb; bi++) {
      Block *b = &f->blocks[bi];
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        Edge *e = &b->succs[s];
        uint32_t t = e->target;
        Block *tb = &f->blocks[t];
        for (uint32_t j = nparams0[t]; j < tb->nparams; j++) {
          uint32_t k = pvar[tb->params[j] - pv_base];
          Ref val = REF_NONE;
          for (uint32_t c = 0; c < nobj; c++) {
            EsObj *o = &obj[c];
            if (o->escaped || k < o->var0 || k >= o->var0 + ES_NV(o)) continue;
            if (o->phi && o->block == t) {
              /* the merge's own field: whichever object came along this edge */
              uint32_t pj = 0;
              while (pj < nparams0[t] && tb->params[pj] != o->def) pj++;
              int32_t sc = pj < nparams0[t] && pj < e->nargs
                         ? OWN(ir_resolve(f, e->args[pj])) : -1;
              if (sc >= 0 && done[bi])
                val = outv[(size_t)bi * V + obj[sc].var0 + (k - o->var0)];
            } else if (done[bi]) {
              val = outv[(size_t)bi * V + k];
            }
            break;
          }
          if (val == REF_NONE) ES_ZERO(vtype[k], val);
          edge_push_arg_at(f, bi, s, ir_resolve(f, val));
        }
      }
    }

    /* The objects go: their aliases' parameters and their arguments, then
       the allocations and conversions themselves. */
    for (uint32_t bi = 0; bi < nb; bi++) {
      Block *b = &f->blocks[bi];
      for (uint32_t p = 0; p < b->nparams;) {
        Ref P = b->params[p];
        if (P >= n || OWN(P) < 0 || obj[OWN(P)].escaped) { p++; continue; }
        for (uint32_t j = p; j + 1 < b->nparams; j++) b->params[j] = b->params[j + 1];
        b->nparams--;
        for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
          Edge *e = &f->blocks[ei.src[k]].succs[ei.si[k]];
          for (uint32_t j = p; j + 1 < e->nargs; j++) e->args[j] = e->args[j + 1];
          if (e->nargs > p) e->nargs--;
        }
        f->insts[P].op = I_NOP;
        f->insts[P].nargs = 0;
      }
    }

    /* The escape points on edges: each such edge split once, and what it
       hands over made in the block that splits it, from the fields as the
       edge's source left them. */
    bool split = false;
    for (uint32_t k = 0; k < npt; k++) {
      EsPt *p = &pt[k];
      if (p->succ == UINT32_MAX || obj[p->obj].escaped) continue;
      bool seen = false;
      for (uint32_t k2 = 0; k2 < k && !seen; k2++)
        seen = pt[k2].block == p->block && pt[k2].succ == p->succ && !obj[pt[k2].obj].escaped;
      if (seen) continue;
      uint32_t sb = gl_split_edge(f, p->block, p->succ);
      split = true;
      int32_t mobj[ESC_MAX_OBJS];
      Ref mref[ESC_MAX_OBJS];
      uint32_t nm = 0;
      for (uint32_t j = 0; j < f->blocks[sb].succs[0].nargs; j++) {
        int32_t mc = OWN(ir_resolve(f, f->blocks[sb].succs[0].args[j]));
        if (mc < 0 || obj[mc].escaped) continue;
        uint32_t q = 0;
        while (q < nm && mobj[q] != mc) q++;
        if (q == nm) {
          uint32_t at = f->blocks[sb].ninsts;
          mobj[nm] = mc;
          mref[nm++] = es_materialize(f, &obj[mc], sb, &at, &outv[(size_t)p->block * V]);
          made++;
        }
        f->blocks[sb].succs[0].args[j] = mref[q];
      }
    }

    uint32_t removed = 0;
    for (uint32_t v = 0; v < n; v++) {
      if (own[v] < 0 || obj[own[v]].escaped) continue;
      Inst *in = &f->insts[v];
      if (in->op == I_STRUCT_NEW || in->op == I_ARRAY_NEW) removed++;
      if (in->op != I_PARAM) { in->op = I_NOP; in->nargs = 0; in->type = 0; }
    }
    ir_apply_subst(f);
    if (split) { ir_recompute_preds(f); ir_order(f); }

    /* Nothing that can run may still name what went. */
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Inst *in = &f->insts[b->insts[i]];
        Ref *a = ir_args(f, b->insts[i]);
        for (uint32_t j = 0; j < in->nargs; j++)
          if (a[j] < n && own[a[j]] >= 0 && !obj[own[a[j]]].escaped)
            warmc_fatal("internal: escape left v%u using v%u in %s",
                        b->insts[i], a[j], m->funcs[f->func_index].sym);
      }
      for (uint32_t s = 0; s < b->nsuccs; s++)
        for (uint32_t j = 0; j < b->succs[s].nargs; j++) {
          Ref r = b->succs[s].args[j];
          if (r < n && own[r] >= 0 && !obj[own[r]].escaped)
            warmc_fatal("internal: escape left an edge b%u->b%u passing v%u in %s",
                        bi, b->succs[s].target, r, m->funcs[f->func_index].sym);
        }
      if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg < n &&
          own[b->term_arg] >= 0 && !obj[own[b->term_arg]].escaped)
        warmc_fatal("internal: escape left b%u branching on v%u", bi, b->term_arg);
      for (uint32_t i = 0; i < b->nret; i++)
        if (b->ret_args[i] < n && own[b->ret_args[i]] >= 0 &&
            !obj[own[b->ret_args[i]]].escaped)
          warmc_fatal("internal: escape left b%u returning v%u", bi, b->ret_args[i]);
    }

    g_stats.escape_removed += removed;
    g_stats.escape_forwarded += forwarded;
    g_stats.escape_made += made;
    changed = true;
#undef ES_ZERO
    xfree(vtype); xfree(pref); xfree(outv); xfree(nparams0); xfree(pvar);
    xfree(cur); xfree(done); xfree(zero_cache); xfree(zero_type);
    xfree(ptinst); xfree(ptend);
  }

#undef ES_ADD
#undef OWN
#undef ES_USE
#undef ES_ISNULL
#undef ES_NULLV
  ir_edge_index_free(&ei);
  xfree(own); xfree(obj); xfree(forg); xfree(flow); xfree(use); xfree(pt); xfree(demote);
  xfree(nullonly);
  xfree(reach); xfree(stack); xfree(live); xfree(needp);
  return changed;
}

typedef struct { uint64_t key; Ref value; } CseEnt;

static uint64_t hash_inst(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  uint64_t h = 1469598103934665603ull;
#define MIX(x) do { h ^= (uint64_t)(x); h *= 1099511628211ull; } while (0)
  MIX(in->op); MIX(in->type); MIX(in->imm); MIX(in->imm2);
  for (uint32_t j = 0; j < in->nargs; j++) MIX(a[j]);
#undef MIX
  return h ? h : 1;
}

static bool same_inst(IrFunc *f, Ref x, Ref y) {
  Inst *a = &f->insts[x], *b = &f->insts[y];
  if (a->op != b->op || a->type != b->type || a->imm != b->imm ||
      a->imm2 != b->imm2 || a->nargs != b->nargs) return false;
  Ref *aa = ir_args(f, x), *ba = ir_args(f, y);
  for (uint32_t j = 0; j < a->nargs; j++) if (aa[j] != ba[j]) return false;
  return true;
}

static bool cse_candidate(IrFunc *f, Ref v, bool gcpure) {
  Inst *in = &f->insts[v];
  /* GC reads that nothing can change: an array's length is fixed at
     allocation, and an immutable field or element is written only by its
     initialiser.  The trap flags live in imm2, which the hash keys on, so
     a match reuses a dominating twin carrying the very same checks --
     dropping the later copy drops a check the survivor already made. */
  switch (in->op) {
    case I_ARRAY_LEN:
      return gcpure;
    case I_FIELD_GET:
      return gcpure && gc_field_immutable(f, v);
    case I_ARRAY_GET:
      return gcpure && gc_array_elem_immutable(f->m, (uint32_t)in->imm);
    default: break;
  }
  if (!ir_is_pure(in)) return false;
  if (in->op == I_PARAM || in->op == I_NOP) return false;

  if (in->op == I_LOAD || in->op == I_GLOBAL_GET) return false;

  if (in->op == I_EXN_OBJ) return false;
  return in->type != 0;
}

/* The instructions in the order's blocks: what a table of them needs room
   for, where f->ninsts counts every instruction ever made. */
static uint32_t live_insts(const IrFunc *f) {
  uint32_t n = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) n += f->blocks[f->order[oi]].ninsts;
  return n;
}

/* The table is sized by what can go in it.  Entries of one hash lie along
   its probe path in the order they went in, so a lookup finds the first
   that matches and dominates whatever the table's size. */
static bool pass_cse(IrFunc *f, const OptOpts *o) {
  uint32_t cap = 1;
  while (cap < live_insts(f) * 2 + 16) cap *= 2;
  CseEnt *tab = xcalloc(cap, sizeof(CseEnt));
  bool changed = false;

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (!cse_candidate(f, v, o->gcpure)) continue;
      uint64_t h = hash_inst(f, v);
      uint32_t slot = (uint32_t)(h & (cap - 1));
      bool reused = false;
      for (uint32_t probe = 0; probe < cap; probe++) {
        CseEnt *e = &tab[(slot + probe) & (cap - 1)];
        if (e->key == 0) {
          e->key = h; e->value = v;
          break;
        }
        bool ok = e->key == h && same_inst(f, e->value, v) &&
                  dominates(f, f->insts[e->value].block, bi);
        if (ok) {
          ir_replace(f, v, e->value);
          in->op = I_NOP;
          g_stats.cse_removed++;
          changed = reused = true;
          break;
        }
      }
      (void)reused;
    }
  }
  xfree(tab);
  if (changed) ir_apply_subst(f);
  return changed;
}

static bool pass_branch(IrFunc *f) {
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];

    if (ir_block_has_eh(f, bi)) continue;
    if (b->term == T_BRIF) {
      uint64_t c;
      if (is_const(f, b->term_arg, &c)) {

        uint32_t keep = c ? 0 : 1;
        Edge kept = b->succs[keep];
        b->succs[0] = kept;
        b->nsuccs = 1;
        b->term = T_JUMP;
        g_stats.branch_simplified++;
        changed = true;
      } else if (b->succs[0].target == b->succs[1].target &&
                 b->succs[0].nargs == b->succs[1].nargs) {
        bool same = true;
        for (uint32_t j = 0; j < b->succs[0].nargs; j++)
          if (b->succs[0].args[j] != b->succs[1].args[j]) { same = false; break; }
        if (same) {
          b->nsuccs = 1;
          b->term = T_JUMP;
          g_stats.branch_simplified++;
          changed = true;
        }
      }
    } else if (b->term == T_BRTABLE) {
      uint64_t c;
      if (is_const(f, b->term_arg, &c)) {
        uint32_t n = b->nsuccs - 1;
        uint32_t pick = (c < n) ? (uint32_t)c : n;
        Edge kept = b->succs[pick];
        b->succs[0] = kept;
        b->nsuccs = 1;
        b->term = T_JUMP;
        g_stats.branch_simplified++;
        changed = true;
      }
    }
  }

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      uint32_t t = b->succs[s].target;
      Block *tb = &f->blocks[t];
      if (t == bi || t == f->entry) continue;

      if (b->succs[s].eh || tb->is_pad) continue;
      if (tb->ninsts != 0 || tb->term != T_JUMP || tb->nparams != 0) continue;
      if (tb->succs[0].target == t) continue;

      {
        Block *gb = &f->blocks[tb->succs[0].target];
        bool target_is_header = false;
        for (uint32_t p = 0; p < gb->npreds; p++)
          if (f->blocks[gb->preds[p]].rpo >= gb->rpo) { target_is_header = true; break; }
        if (target_is_header && tb->rpo < gb->rpo) continue;
      }

      uint32_t walk = tb->succs[0].target, steps = 0;
      bool cycle = false;
      while (steps++ < 64) {
        Block *wb = &f->blocks[walk];
        if (walk == t) { cycle = true; break; }
        if (wb->ninsts != 0 || wb->term != T_JUMP || wb->nparams != 0) break;
        walk = wb->succs[0].target;
      }
      if (cycle) continue;

      Edge *fwd = &tb->succs[0];
      Ref *na = fwd->nargs ? ir_alloc(f, fwd->nargs * sizeof(Ref)) : NULL;
      for (uint32_t j = 0; j < fwd->nargs; j++) na[j] = fwd->args[j];
      b->succs[s].args = na;
      b->succs[s].nargs = fwd->nargs;
      b->succs[s].cap = fwd->nargs;
      b->succs[s].target = fwd->target;
      g_stats.branch_simplified++;
      changed = true;
    }
  }
  if (changed) ir_recompute_preds(f);
  if (changed) { ir_order(f); }
  return changed;
}

/* ---- tests a dominating branch has decided ------------------------------

   A block with one predecessor, which reaches it on one arm of a two-way
   branch, runs only where that branch's condition had the arm's value, and
   so does every block it dominates: the last time the branch ran before
   them it took that arm, and no value the condition read has been made
   again since.  A test there of the same value against a constant is then
   often known.  Rotation leaves exactly that behind a loop whose body was
   inlined with its lengths constant: rsbench's f32ops loop continues on
   i != 2047 and then checks i + 1 == 2048 against each slice's length, a
   bounds check that can never fail, twice, and those two exits out of the
   body were all that kept the loop from the vectoriser. */
#define ET_DEPTH 8

/* eq, ne or eqz of a value, or of the value plus or minus a constant,
   against a constant: a test of whether *x is *k, mod 2^n, for equality
   when *eq. */
static bool et_form(IrFunc *f, Ref v, Ref *x, uint64_t *k, bool *eq) {
  Inst *in = &f->insts[v];
  uint64_t c, d;
  Ref y;
  if (in->op == I_EQZ && in->nargs == 1) {
    y = ir_args(f, v)[0];
    c = 0;
    *eq = true;
  } else if ((in->op == I_EQ || in->op == I_NE) && in->nargs == 2) {
    Ref *a = ir_args(f, v);
    if (is_const(f, a[1], &c)) y = a[0];
    else if (is_const(f, a[0], &c)) y = a[1];
    else return false;
    *eq = in->op == I_EQ;
  } else {
    return false;
  }
  ValType t = (ValType)f->insts[y].type;
  if (t != VT_I32 && t != VT_I64) return false;
  Inst *yi = &f->insts[y];
  if ((yi->op == I_ADD || yi->op == I_SUB) && yi->nargs == 2 &&
      is_const(f, ir_args(f, y)[1], &d)) {
    c = yi->op == I_ADD ? c - d : c + d;
    y = ir_args(f, y)[0];
  }
  *x = y;
  *k = trunc_to(t, c);
  return true;
}

/* The condition the branch into b decided, and *truth its value there, or
   REF_NONE when b is not one arm of a two-way branch it alone reaches. */
static Ref et_edge_fact(IrFunc *f, uint32_t b, bool *truth) {
  const Block *bb = &f->blocks[b];
  if (bb->npreds != 1 || bb->is_pad || bb->preds[0] == b) return REF_NONE;
  const Block *pb = &f->blocks[bb->preds[0]];
  if (pb->term != T_BRIF || pb->nsuccs != 2 ||
      pb->succs[0].target == pb->succs[1].target ||
      pb->succs[0].eh || pb->succs[1].eh)
    return REF_NONE;
  *truth = pb->succs[0].target == b;
  return pb->term_arg;
}

/* v's value where c is truth: 0 or 1, or -1 for either. */
static int et_decide(IrFunc *f, Ref v, Ref c, bool truth) {
  if (v == c) return truth;
  if (f->insts[v].op == I_EQZ && f->insts[v].nargs == 1 && ir_args(f, v)[0] == c)
    return !truth;
  Ref xv, xc;
  uint64_t kv, kc;
  bool ev, ec;
  if (!et_form(f, v, &xv, &kv, &ev) || !et_form(f, c, &xc, &kc, &ec) || xv != xc)
    return -1;
  if (ec == truth) return ev == (kv == kc);
  if (kv == kc) return !ev;
  return -1;
}

/* v's value throughout block b, as the branches into the blocks that
   dominate it decided it, or -1. */
static int et_known(IrFunc *f, uint32_t b, Ref v) {
  uint32_t d = b;
  for (int step = 0; step < ET_DEPTH; step++) {
    bool truth;
    Ref c = et_edge_fact(f, d, &truth);
    if (c != REF_NONE) {
      int r = et_decide(f, v, c, truth);
      if (r >= 0) return r;
    }
    uint32_t up = f->blocks[d].idom;
    if (up == REF_NONE || up == d) break;
    d = up;
  }
  return -1;
}

/* A test decided where it is made becomes its constant, and a branch on
   one decided where the branch is goes the one way; pass_branch removes
   what that leaves.  Only edges that never run go, so what the walk read of
   the others stays true while it changes them.  Loops only: it is there for
   the vectoriser. */
static bool pass_edge_tests(IrFunc *f) {
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (!b->loop_depth) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      uint16_t op = f->insts[v].op;
      if (op != I_EQ && op != I_NE && op != I_EQZ) continue;
      int r = et_known(f, bi, v);
      if (r < 0) continue;
      replace_with_const(f, v, (uint64_t)r);
      g_stats.folded++;
      changed = true;
    }
    if (b->term == T_BRIF && b->nsuccs == 2 && !ir_block_has_eh(f, bi)) {
      int r = et_known(f, bi, b->term_arg);
      if (r < 0) continue;
      b->succs[0] = b->succs[r ? 0 : 1];
      b->nsuccs = 1;
      b->term = T_JUMP;
      g_stats.branch_simplified++;
      changed = true;
    }
  }
  if (changed) {
    ir_recompute_preds(f);
    ir_order(f);
  }
  return changed;
}

/* Jump threading through a dispatch block.  Go's back end compiles every
   function to a loop around a br_table over the function's resume points,
   and sends each jump inside the function -- a loop's back edge among them --
   through that dispatch with a constant index.  Left alone, every iteration
   of every loop is an indirect branch, and every block that jumps is in the
   one loop the dispatch heads, which is all the layout and the loop passes
   get to see.

   A block that does nothing but switch on one of its parameters -- or
   branch on its compare with a constant, as a merge of `c ? x : 1` into an
   `if` does, in AssemblyScript's String.__eq among many -- reached by an
   edge that hands that parameter a constant, is bypassed: the edge goes
   to a fresh block that takes the dispatch's parameters and jumps straight
   to the case the constant picks.  The dispatch's parameters can be read
   beyond its own edges -- Go's stack pointer is one -- and every such read
   was dominated by the dispatch, which it no longer is.  So each of those
   parameters is put back into SSA form: the dispatch and the fresh blocks
   define it, block parameters merge it at the iterated dominance frontier
   of those, and a walk down the dominator tree renames every read.
   prune_params removes the merges that turn out trivial, and with them the
   fresh blocks' parameters, which leaves those blocks for pass_branch.

   The dispatch still enters the function at any of its cases, so a loop
   that was threaded can have a second way in, into its middle.  Every loop
   pass finds that cycle and turns it down. */
typedef struct { uint32_t *v; uint32_t n, cap; } TdVec;

static void td_push(TdVec *x, uint32_t b) {
  if (x->n == x->cap) {
    x->cap = x->cap ? x->cap * 2 : 4;
    x->v = xrealloc(x->v, x->cap * sizeof(uint32_t));
  }
  x->v[x->n++] = b;
}

/* Dominance frontiers, and the dominator tree as child lists, of the CFG
   ir_order last saw; the blocks it does not reach that branch somewhere;
   and td_repair's working arrays, each entry of which counts only when its
   stamp is the call's. */
typedef struct {
  TdVec *df; uint32_t *kids_at, *kids, nb;
  uint32_t *unreached, nunreached;
  uint32_t stamp, *st_live, *st_inwl, *st_phi, *st_def, *st_cur, *st_done;
  Ref *phi, *def, *cur;
  uint32_t *wl, *path;
} TdDom;

static void td_dom_build(IrFunc *f, TdDom *d) {
  uint32_t nb = d->nb = f->nblocks;
  d->df = xcalloc(nb, sizeof(TdVec));
  uint32_t *last = xmalloc(nb * sizeof(uint32_t));
  for (uint32_t i = 0; i < nb; i++) last[i] = UINT32_MAX;
  for (uint32_t y = 0; y < nb; y++) {
    Block *yb = &f->blocks[y];
    if (!yb->reachable || yb->npreds < 2) continue;
    for (uint32_t pp = 0; pp < yb->npreds; pp++) {
      uint32_t r = yb->preds[pp];
      while (f->blocks[r].reachable && r != yb->idom) {
        if (last[r] != y) { last[r] = y; td_push(&d->df[r], y); }
        if (r == f->entry) break;
        r = f->blocks[r].idom;
      }
    }
  }
  xfree(last);
  d->kids_at = xcalloc(nb + 1, sizeof(uint32_t));
  d->kids = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  for (uint32_t x = 0; x < nb; x++)
    if (f->blocks[x].reachable && x != f->entry) d->kids_at[f->blocks[x].idom + 1]++;
  for (uint32_t x = 0; x < nb; x++) d->kids_at[x + 1] += d->kids_at[x];
  uint32_t *fill = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  memcpy(fill, d->kids_at, nb * sizeof(uint32_t));
  for (uint32_t x = 0; x < nb; x++)
    if (f->blocks[x].reachable && x != f->entry) d->kids[fill[f->blocks[x].idom]++] = x;
  xfree(fill);
  d->unreached = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  d->nunreached = 0;
  for (uint32_t x = 0; x < nb; x++)
    if (!f->blocks[x].reachable && f->blocks[x].nsuccs) d->unreached[d->nunreached++] = x;
  size_t n1 = nb ? nb : 1;
  d->stamp = 0;
  uint32_t **st[] = { &d->st_live, &d->st_inwl, &d->st_phi, &d->st_def,
                      &d->st_cur, &d->st_done };
  for (size_t k = 0; k < sizeof st / sizeof st[0]; k++) *st[k] = xcalloc(n1, sizeof(uint32_t));
  d->phi = xmalloc(n1 * sizeof(Ref));
  d->def = xmalloc(n1 * sizeof(Ref));
  d->cur = xmalloc(n1 * sizeof(Ref));
  d->wl = xmalloc(n1 * sizeof(uint32_t));
  d->path = xmalloc(n1 * sizeof(uint32_t));
}

static void td_dom_free(TdDom *d) {
  for (uint32_t i = 0; i < d->nb; i++) xfree(d->df[i].v);
  xfree(d->df); xfree(d->kids_at); xfree(d->kids); xfree(d->unreached);
  xfree(d->st_live); xfree(d->st_inwl); xfree(d->st_phi);
  xfree(d->st_def); xfree(d->st_cur); xfree(d->st_done);
  xfree(d->phi); xfree(d->def); xfree(d->cur); xfree(d->wl); xfree(d->path);
}

/* What reaches the end of block x in the call stamped st: its own
   definition or merge, or else what reaches the end of its immediate
   dominator.  Each block's answer is kept for the rest of the call. */
static Ref td_cur(const IrFunc *f, TdDom *d, uint32_t x, uint32_t st) {
  uint32_t n = 0;
  Ref c;
  for (;;) {
    if (d->st_cur[x] == st) { c = d->cur[x]; break; }
    if (d->st_def[x] == st) { c = d->def[x]; break; }
    if (d->st_phi[x] == st) { c = d->phi[x]; break; }
    if (x == f->entry) { c = REF_NONE; break; }
    d->path[n++] = x;
    x = f->blocks[x].idom;
  }
  d->st_cur[x] = st; d->cur[x] = c;
  while (n) { uint32_t z = d->path[--n]; d->st_cur[z] = st; d->cur[z] = c; }
  return c;
}

/* Put value p back into SSA form once more than one block defines it:
   def[k] is what block defb[k] defines it as, the blocks rising.  Merges
   go at the iterated dominance frontier of the defining blocks, and every
   read of p outside them is renamed to the value that reaches it.  A merge
   can also be reached on a path that never defined p, where nothing reads
   it either; that path brings a zero, which is why p has to be a number.

   use[] lists the blocks that read p, other than the defining ones.  A
   merge goes only where p is live: one at every frontier would merge p
   into each loop header above a definition, and those merges, reading only
   each other, are more than prune_params can see is dead.

   Only the blocks that read p or end in an edge to a merge are touched,
   each asking td_cur what reaches it: a walk of the whole dominator tree,
   and arrays the function's length cleared, for each value repaired was a
   sixth of Go's jump threading, which repairs dozens of a dispatch's
   parameters at a time.  No order of visits changes what is written,
   since the one instruction made, the zero, is made once. */
static void td_repair(IrFunc *f, Ref p, const uint32_t *defb, const Ref *def,
                      uint32_t ndef, const uint32_t *use, uint32_t nuse,
                      TdDom *d) {
  uint32_t nb = d->nb, st = ++d->stamp;
  ValType t = (ValType)f->insts[p].type;
  uint32_t *wl = d->wl, nwl = 0;
  for (uint32_t k = 0; k < ndef; k++)
    if (defb[k] < nb) { d->st_def[defb[k]] = st; d->def[defb[k]] = def[k]; }
#define TD_ISDEF(x) (d->st_def[x] == st)
  for (uint32_t i = 0; i < nuse; i++) {
    uint32_t x = use[i];
    if (x < nb && d->st_live[x] != st && !TD_ISDEF(x)) { d->st_live[x] = st; wl[nwl++] = x; }
  }
  while (nwl) {
    Block *xb = &f->blocks[wl[--nwl]];
    for (uint32_t q = 0; q < xb->npreds; q++) {
      uint32_t y = xb->preds[q];
      if (d->st_live[y] == st || TD_ISDEF(y)) continue;
      d->st_live[y] = st;
      wl[nwl++] = y;
    }
  }
  /* rising, as the defining blocks were */
  uint32_t *phib = NULL, nphi = 0, capphi = 0;
  for (uint32_t k = 0; k < ndef; k++) {
    uint32_t x = defb[k];
    if (x < nb && f->blocks[x].reachable && d->st_inwl[x] != st) {
      d->st_inwl[x] = st; wl[nwl++] = x;
    }
  }
  while (nwl) {
    uint32_t x = wl[--nwl];
    for (uint32_t k = 0; k < d->df[x].n; k++) {
      uint32_t y = d->df[x].v[k];
      if (d->st_live[y] != st || TD_ISDEF(y) || d->st_phi[y] == st) continue;
      d->st_phi[y] = st;
      d->phi[y] = ir_add_param(f, y, t);
      if (nphi == capphi) {
        capphi = capphi ? capphi * 2 : 8;
        phib = xrealloc(phib, capphi * sizeof(uint32_t));
      }
      phib[nphi++] = y;
      if (d->st_inwl[y] != st) { d->st_inwl[y] = st; wl[nwl++] = y; }
    }
  }

  Ref undef = REF_NONE;
  for (uint32_t i = 0; i < nuse; i++) {
    uint32_t x = use[i];
    if (x >= nb || TD_ISDEF(x) || !f->blocks[x].reachable || d->st_done[x] == st)
      continue;
    d->st_done[x] = st;
    Ref c = td_cur(f, d, x, st);
    if (c == REF_NONE) continue;
    Block *xb = &f->blocks[x];
    for (uint32_t i2 = 0; i2 < xb->ninsts; i2++) {
      Ref *a = ir_args(f, xb->insts[i2]);
      for (uint32_t q = 0; q < f->insts[xb->insts[i2]].nargs; q++)
        if (a[q] == p) a[q] = c;
    }
    if ((xb->term == T_BRIF || xb->term == T_BRTABLE) && xb->term_arg == p)
      xb->term_arg = c;
    for (uint32_t q = 0; q < xb->nret; q++)
      if (xb->ret_args[q] == p) xb->ret_args[q] = c;
    for (uint32_t s = 0; s < xb->nsuccs; s++) {
      Edge *e = &xb->succs[s];
      for (uint32_t q = 0; q < e->nargs; q++)
        if (e->args[q] == p) e->args[q] = c;
    }
  }
  /* the edges into merges: from the blocks the tree reaches, what reaches
     their end; from the others, which nothing reaches, the zero.  A second
     stamp marks the blocks done here. */
  uint32_t st2 = ++d->stamp;
  for (uint32_t k = 0; k < nphi; k++) {
    Block *yb = &f->blocks[phib[k]];
    for (uint32_t q = 0; q < yb->npreds; q++) {
      uint32_t x = yb->preds[q];
      if (x >= nb || !f->blocks[x].reachable || d->st_done[x] == st2) continue;
      d->st_done[x] = st2;
      Block *xb = &f->blocks[x];
      Ref c = REF_NONE;
      bool have = false;
      for (uint32_t s = 0; s < xb->nsuccs; s++) {
        uint32_t y = xb->succs[s].target;
        if (y >= nb || d->st_phi[y] != st) continue;
        if (!have) { c = td_cur(f, d, x, st); have = true; }
        if (c == REF_NONE && undef == REF_NONE)
          undef = ir_emit(f, f->entry, I_CONST, t, NULL, 0, 0, 0);
        edge_push_arg_at(f, x, s, c != REF_NONE ? c : undef);
      }
    }
  }
  for (uint32_t k = 0; k < d->nunreached && nphi; k++) {
    uint32_t x = d->unreached[k];
    Block *xb = &f->blocks[x];
    for (uint32_t s = 0; s < xb->nsuccs; s++) {
      uint32_t y = xb->succs[s].target;
      if (y >= nb || d->st_phi[y] != st || xb->succs[s].nargs >= f->blocks[y].nparams)
        continue;
      if (undef == REF_NONE) undef = ir_emit(f, f->entry, I_CONST, t, NULL, 0, 0, 0);
      edge_push_arg_at(f, x, s, undef);
    }
  }
#undef TD_ISDEF
  xfree(phib);
}

static bool td_numeric(ValType t) {
  return t == VT_I32 || t == VT_I64 || t == VT_F32 || t == VT_F64 || t == VT_V128;
}

/* The test of a block that branches on nothing but a compare of one of its
   parameters with a constant -- binaryen's `c ? x : 1` merged into the block
   whose `if` tests it -- as the opcode and constant, or false.  The compare
   is the block's one instruction and its branch the one reader (uses[], as
   counted before the pass), so an edge that bypasses the block leaves
   nothing undefined. */
static bool thread_test(IrFunc *f, uint32_t B, const uint32_t *uses,
                        uint32_t nuses, uint16_t *op, uint64_t *k) {
  Block *b = &f->blocks[B];
  Ref c = b->term_arg;
  if (b->term != T_BRIF || b->ninsts != 1 || b->insts[0] != c) return false;
  if (c >= nuses || uses[c] != 1) return false;
  Inst *ci = &f->insts[c];
  Ref *ca = ir_args(f, c);
  if (ci->op == I_EQZ) *k = 0;
  else if ((ci->op == I_EQ || ci->op == I_NE) && is_const(f, ca[1], k)) {}
  else return false;
  if (f->insts[ca[0]].op != I_PARAM || f->insts[ca[0]].block != B) return false;
  *op = ci->op;
  return true;
}

static bool thread_dispatch(IrFunc *f, uint32_t B, const uint32_t *nread,
                            uint32_t nnread) {
  Block *b = &f->blocks[B];
  if (!b->reachable || b->is_pad || B == f->entry) return false;
  if (b->term != T_BRTABLE && b->term != T_BRIF) return false;
  if (ir_block_has_eh(f, B)) return false;
  /* The switch is on a parameter, or on its compare with a constant. */
  uint16_t top = I_NOP;
  uint64_t tk = 0;
  Ref sw = b->term_arg;
  if (b->ninsts) {
    if (!thread_test(f, B, nread, nnread, &top, &tk)) return false;
    sw = ir_args(f, b->term_arg)[0];
  }
  uint32_t k = UINT32_MAX;
  for (uint32_t q = 0; q < b->nparams; q++)
    if (b->params[q] == sw) k = q;
  if (k == UINT32_MAX) return false;
  uint64_t tmask = f->insts[sw].type == VT_I64 ? ~0ull : 0xffffffffu;
  /* The repair needs a zero of each parameter's type; see td_repair. */
  for (uint32_t q = 0; q < b->nparams; q++)
    if (!td_numeric((ValType)f->insts[b->params[q]].type)) return false;

  TdVec made = {0};
  uint32_t nb0 = f->nblocks;
  for (uint32_t pi = 0; pi < nb0; pi++) {
    if (!f->blocks[pi].reachable || pi == B) continue;
    for (uint32_t s = 0; s < f->blocks[pi].nsuccs; s++) {
      Edge *e = &f->blocks[pi].succs[s];
      Block *bb = &f->blocks[B];
      uint64_t c;
      if (e->target != B || e->eh || e->nargs != bb->nparams) continue;
      if (!is_const(f, e->args[k], &c)) continue;
      c &= tmask;
      if (top == I_EQZ || top == I_EQ) c = c == (tk & tmask);
      else if (top == I_NE) c = c != (tk & tmask);
      uint32_t pick;
      if (bb->term == T_BRIF) pick = c ? 0 : 1;
      else pick = c < bb->nsuccs - 1 ? (uint32_t)c : bb->nsuccs - 1;
      uint32_t t = bb->succs[pick].target;
      if (t == B || bb->succs[pick].eh || f->blocks[t].is_pad) continue;

      uint32_t x = ir_new_block(f);
      for (uint32_t q = 0; q < f->blocks[B].nparams; q++)
        ir_add_param(f, x, (ValType)f->insts[f->blocks[B].params[q]].type);
      bb = &f->blocks[B];
      const Edge *ce = &bb->succs[pick];
      Ref *na = ce->nargs ? xmalloc(ce->nargs * sizeof(Ref)) : NULL;
      for (uint32_t a = 0; a < ce->nargs; a++) {
        Ref r = ce->args[a];
        if (f->insts[r].op == I_PARAM && f->insts[r].block == B)
          for (uint32_t q = 0; q < bb->nparams; q++)
            if (bb->params[q] == r) { r = f->blocks[x].params[q]; break; }
        na[a] = r;
      }
      f->blocks[x].term = T_JUMP;
      ir_add_edge(f, x, t, na, ce->nargs);
      xfree(na);
      f->blocks[x].reachable = true;
      f->blocks[pi].succs[s].target = x;
      td_push(&made, x);
      g_stats.branch_simplified++;
    }
  }
  if (!made.n) return false;
  if (warmc_getenv("WARMC_THREAD_STATS"))
    fprintf(stderr, "thread %s b%u: %s on a parameter, %u edge(s)\n",
            f->m->funcs[f->func_index].sym ? f->m->funcs[f->func_index].sym : "?", B,
            f->blocks[B].term == T_BRIF ? "brif" : "br_table", made.n);
  ir_order(f);

  uint32_t nb = f->nblocks, np = f->blocks[B].nparams;
  uint8_t *fresh = xcalloc(nb, 1);
  for (uint32_t i = 0; i < made.n; i++) fresh[made.v[i]] = 1;
  uint8_t *need = xcalloc(np ? np : 1, 1);
  TdVec *uses = xcalloc(np ? np : 1, sizeof(TdVec));
  bool any = false;
#define TD_READ(r) do {                                                       \
    Ref r_ = (r);                                                             \
    if (r_ != REF_NONE && f->insts[r_].op == I_PARAM && f->insts[r_].block == B) \
      for (uint32_t q = 0; q < np; q++)                                       \
        if (f->blocks[B].params[q] == r_) {                                   \
          need[q] = 1; any = true;                                            \
          if (!uses[q].n || uses[q].v[uses[q].n - 1] != x) td_push(&uses[q], x); \
          break;                                                              \
        }                                                                     \
  } while (0)
  for (uint32_t x = 0; x < nb; x++) {
    Block *xb = &f->blocks[x];
    if (x == B || fresh[x] || !xb->reachable) continue;
    for (uint32_t i = 0; i < xb->ninsts; i++) {
      const Ref *a = ir_args(f, xb->insts[i]);
      for (uint32_t q = 0; q < f->insts[xb->insts[i]].nargs; q++) TD_READ(a[q]);
    }
    if (xb->term == T_BRIF || xb->term == T_BRTABLE) TD_READ(xb->term_arg);
    for (uint32_t q = 0; q < xb->nret; q++) TD_READ(xb->ret_args[q]);
    for (uint32_t s = 0; s < xb->nsuccs; s++)
      for (uint32_t q = 0; q < xb->succs[s].nargs; q++) TD_READ(xb->succs[s].args[q]);
  }
#undef TD_READ

  if (any) {
    TdDom d;
    td_dom_build(f, &d);
    /* B and the fresh blocks define each parameter, in the order made */
    uint32_t *defb = xmalloc((made.n + 1) * sizeof(uint32_t));
    Ref *defv = xmalloc((made.n + 1) * sizeof(Ref));
    defb[0] = B;
    memcpy(defb + 1, made.v, made.n * sizeof(uint32_t));
    for (uint32_t q = 0; q < np; q++) {
      if (!need[q]) continue;
      for (uint32_t k = 0; k <= made.n; k++) defv[k] = f->blocks[defb[k]].params[q];
      td_repair(f, f->blocks[B].params[q], defb, defv, made.n + 1,
                uses[q].v, uses[q].n, &d);
    }
    xfree(defb); xfree(defv);
    td_dom_free(&d);
  }
  for (uint32_t q = 0; q < np; q++) xfree(uses[q].v);
  xfree(uses);
  xfree(fresh); xfree(need); xfree(made.v);
  return true;
}

static bool pass_thread(IrFunc *f) {
  if (f->npads) return false;
  bool changed = false;
  uint32_t *nread = count_uses(f), nnread = f->ninsts;
  for (uint32_t bi = 0, n = f->nblocks; bi < n; bi++)
    if (thread_dispatch(f, bi, nread, nnread)) {
      changed = true;
      if (warmc_getenv("WARMC_THREAD_DEBUG")) { ir_print(f, "thread"); ir_verify(f, "thread"); }
    }
  xfree(nread);
  if (changed) {
    prune_params(f);
    ir_order(f);
  }
  return changed;
}

/* Copy the blocks in[] marks, all below nb0: blocks first, then their
   contents, then every copied read pointed at the copy of what it read.
   bcopy[b] is b's copy, or UINT32_MAX, and map[v] the copy of value v below
   ni0, or REF_NONE.  A copy's edge goes to the copy of a marked target and
   to the target itself otherwise; nothing yet goes to a copy. */
static void clone_blocks(IrFunc *f, const uint8_t *in, uint32_t nb0, uint32_t ni0,
                         uint32_t *bcopy, Ref *map) {
  for (uint32_t i = 0; i < ni0; i++) map[i] = REF_NONE;
  for (uint32_t x = 0; x < nb0; x++) bcopy[x] = in[x] ? ir_new_block(f) : UINT32_MAX;
  Ref tmp[64];
  for (uint32_t x = 0; x < nb0; x++) {
    if (!in[x]) continue;
    uint32_t y = bcopy[x];
    for (uint32_t q = 0; q < f->blocks[x].nparams; q++) {
      Ref op = f->blocks[x].params[q];
      map[op] = ir_add_param(f, y, (ValType)f->insts[op].type);
    }
    for (uint32_t i = 0; i < f->blocks[x].ninsts; i++) {
      Ref v = f->blocks[x].insts[i];
      Inst src = f->insts[v];
      if (src.op == I_NOP) continue;
      Ref *heap = src.nargs > 64 ? xmalloc(src.nargs * sizeof(Ref)) : NULL;
      Ref *args = heap ? heap : tmp;
      memcpy(args, ir_args(f, v), src.nargs * sizeof(Ref));
      Ref nv = ir_emit(f, y, (IrOp)src.op, (ValType)src.type, args, src.nargs,
                       src.imm, src.imm2);
      f->insts[nv].flags = src.flags;
      map[v] = nv;
      xfree(heap);
    }
    Block *xb = &f->blocks[x], *yb = &f->blocks[y];
    yb->term = xb->term;
    yb->term_arg = xb->term_arg;
    yb->term_imm = xb->term_imm;
    yb->diverges = xb->diverges;
    yb->sealed = xb->sealed;
    yb->filled = xb->filled;
    yb->reachable = true;
    if (xb->nret) {
      yb->ret_args = ir_alloc(f, xb->nret * sizeof(Ref));
      memcpy(yb->ret_args, xb->ret_args, xb->nret * sizeof(Ref));
      yb->nret = yb->cap_ret = xb->nret;
    }
    for (uint32_t s = 0; s < f->blocks[x].nsuccs; s++) {
      Edge e = f->blocks[x].succs[s];
      uint32_t t = e.target < nb0 && in[e.target] ? bcopy[e.target] : e.target;
      ir_add_edge(f, y, t, e.args, e.nargs);
      f->blocks[y].succs[f->blocks[y].nsuccs - 1].eh = e.eh;
    }
  }
#define CB_MAP(r) ((r) != REF_NONE && (r) < ni0 && map[r] != REF_NONE ? map[r] : (r))
  for (uint32_t x = 0; x < nb0; x++) {
    if (!in[x]) continue;
    Block *yb = &f->blocks[bcopy[x]];
    for (uint32_t i = 0; i < yb->ninsts; i++) {
      Ref *a = ir_args(f, yb->insts[i]);
      for (uint32_t q = 0; q < f->insts[yb->insts[i]].nargs; q++) a[q] = CB_MAP(a[q]);
    }
    if (yb->term == T_BRIF || yb->term == T_BRTABLE) yb->term_arg = CB_MAP(yb->term_arg);
    for (uint32_t q = 0; q < yb->nret; q++) yb->ret_args[q] = CB_MAP(yb->ret_args[q]);
    for (uint32_t s = 0; s < yb->nsuccs; s++)
      for (uint32_t q = 0; q < yb->succs[s].nargs; q++)
        yb->succs[s].args[q] = CB_MAP(yb->succs[s].args[q]);
  }
#undef CB_MAP
}

/* Values defined in the blocks in[] marks and read anywhere else
   reachable: esc[v] set, slot[v] its index into use[], which lists the
   blocks reading it.  Returns how many. */
static uint32_t escaping_values(IrFunc *f, const uint8_t *in, uint32_t nb0, uint32_t ni0,
                                uint8_t *esc, uint32_t *slot, TdVec **use) {
  uint32_t n = 0;
  TdVec *u = NULL;
#define EV_READ(r) do {                                                       \
    Ref r_ = (r);                                                             \
    if (r_ != REF_NONE && r_ < ni0 && f->insts[r_].block < nb0 &&             \
        in[f->insts[r_].block]) {                                             \
      if (!esc[r_]) {                                                         \
        esc[r_] = 1; slot[r_] = n++;                                          \
        u = xrealloc(u, n * sizeof(TdVec));                                   \
        memset(&u[n - 1], 0, sizeof(TdVec));                                  \
      }                                                                       \
      TdVec *v_ = &u[slot[r_]];                                               \
      if (!v_->n || v_->v[v_->n - 1] != x) td_push(v_, x);                    \
    }                                                                         \
  } while (0)
  for (uint32_t x = 0; x < nb0; x++) {
    Block *xb = &f->blocks[x];
    if (in[x] || !xb->reachable) continue;
    for (uint32_t i = 0; i < xb->ninsts; i++) {
      const Ref *a = ir_args(f, xb->insts[i]);
      for (uint32_t q = 0; q < f->insts[xb->insts[i]].nargs; q++) EV_READ(a[q]);
    }
    if (xb->term == T_BRIF || xb->term == T_BRTABLE) EV_READ(xb->term_arg);
    for (uint32_t q = 0; q < xb->nret; q++) EV_READ(xb->ret_args[q]);
    for (uint32_t s = 0; s < xb->nsuccs; s++)
      for (uint32_t q = 0; q < xb->succs[s].nargs; q++) EV_READ(xb->succs[s].args[q]);
  }
#undef EV_READ
  *use = u;
  return n;
}

/* After the copies are wired in: merge each value escaping[] marks, defined
   once in the original and once in its copy. */
static void repair_copies(IrFunc *f, uint32_t ni0, const uint8_t *esc, const uint32_t *slot,
                          const TdVec *use, const uint32_t *bcopy, const Ref *map) {
  TdDom d;
  td_dom_build(f, &d);
  for (uint32_t v = 0; v < ni0; v++) {
    if (!esc[v] || map[v] == REF_NONE) continue;
    /* the copy is made after its original */
    uint32_t ob = f->insts[v].block;
    uint32_t defb[2] = { ob, bcopy[ob] };
    Ref defv[2] = { v, map[v] };
    td_repair(f, v, defb, defv, 2, use[slot[v]].v, use[slot[v]].n, &d);
  }
  td_dom_free(&d);
}

/* Node splitting, for a cycle with more than one way in.  After jump
   threading, a Go function's dispatch still enters it at every case, and so
   in the middle of every loop: each loop has its preheader, and the
   dispatch beside it reaching any of its blocks.  No loop pass takes such a
   cycle, and every block the dispatch reaches carries a merge of all the
   values live there, one more for each way in.

   So the dispatch is given a copy of the code it enters: every block on a
   cycle that is reachable from where it enters, up to the first block on no
   cycle, which is where the copy rejoins the original.  The originals are
   left with the one way in their own predecessors give them, and are loops
   again; the copy is entered only from the dispatch, which in Go means only
   when a goroutine resumes, and keeps the shape the dispatch gave it.  A
   value the copied blocks define and something past a rejoining block reads
   is defined twice now, and td_repair merges it.

   A cycle is split only when one of the ways into it is a block that enters
   it at one place, which is then its header, and every other way in comes
   from a block on no cycle.  The budget is a copy no larger than the function
   was. */
#define NS_MAX_COPY 50000u

/* Tarjan's strongly connected components over the reachable blocks,
   without recursion; comp[b] numbers b's component. */
static uint32_t ns_scc(IrFunc *f, uint32_t *comp) {
  uint32_t nb = f->nblocks, counter = 0, ncomp = 0, sp = 0, csp = 0;
  uint32_t *idx = xmalloc(nb * sizeof(uint32_t)), *low = xmalloc(nb * sizeof(uint32_t));
  uint32_t *stk = xmalloc(nb * sizeof(uint32_t));
  uint32_t *cs = xmalloc(nb * sizeof(uint32_t)), *ci = xmalloc(nb * sizeof(uint32_t));
  uint8_t *on = xcalloc(nb, 1);
  for (uint32_t i = 0; i < nb; i++) { idx[i] = UINT32_MAX; comp[i] = UINT32_MAX; }
  for (uint32_t root = 0; root < nb; root++) {
    if (!f->blocks[root].reachable || idx[root] != UINT32_MAX) continue;
    cs[csp] = root; ci[csp] = 0; csp++;
    idx[root] = low[root] = counter++;
    stk[sp++] = root; on[root] = 1;
    while (csp) {
      uint32_t v = cs[csp - 1];
      Block *vb = &f->blocks[v];
      if (ci[csp - 1] < vb->nsuccs) {
        uint32_t w = vb->succs[ci[csp - 1]++].target;
        if (idx[w] == UINT32_MAX) {
          idx[w] = low[w] = counter++;
          stk[sp++] = w; on[w] = 1;
          cs[csp] = w; ci[csp] = 0; csp++;
        } else if (on[w] && idx[w] < low[v]) {
          low[v] = idx[w];
        }
        continue;
      }
      csp--;
      if (csp && low[v] < low[cs[csp - 1]]) low[cs[csp - 1]] = low[v];
      if (low[v] == idx[v]) {
        uint32_t w;
        do { w = stk[--sp]; on[w] = 0; comp[w] = ncomp; } while (w != v);
        ncomp++;
      }
    }
  }
  xfree(idx); xfree(low); xfree(stk); xfree(cs); xfree(ci); xfree(on);
  return ncomp;
}

static bool pass_nodesplit(IrFunc *f) {
  if (f->npads) return false;
  uint32_t nb0 = f->nblocks, ni0 = f->ninsts;
  uint32_t *comp = xmalloc((nb0 ? nb0 : 1) * sizeof(uint32_t));
  uint32_t ncomp = ns_scc(f, comp);

  /* A block is on a cycle when its component has another block or it
     branches to itself. */
  uint32_t *csize = xcalloc(ncomp ? ncomp : 1, sizeof(uint32_t));
  uint8_t *cyc = xcalloc(nb0 ? nb0 : 1, 1);
  for (uint32_t b = 0; b < nb0; b++) if (comp[b] != UINT32_MAX) csize[comp[b]]++;
  for (uint32_t b = 0; b < nb0; b++) {
    if (comp[b] == UINT32_MAX) continue;
    if (csize[comp[b]] > 1) cyc[b] = 1;
    for (uint32_t s = 0; s < f->blocks[b].nsuccs; s++)
      if (f->blocks[b].succs[s].target == b) cyc[b] = 1;
  }

  /* Per cycle: the header a block entering at one place names, and whether
     every other way in can be redirected. */
  uint32_t *hdr = xmalloc((ncomp ? ncomp : 1) * sizeof(uint32_t));
  uint8_t *bad = xcalloc(ncomp ? ncomp : 1, 1);
  for (uint32_t c = 0; c < ncomp; c++) hdr[c] = UINT32_MAX;
  for (uint32_t p = 0; p < nb0; p++) {
    Block *pb = &f->blocks[p];
    if (!pb->reachable) continue;
    for (uint32_t s = 0; s < pb->nsuccs; s++) {
      uint32_t t = pb->succs[s].target, c = comp[t];
      if (!cyc[t] || comp[p] == c) continue;
      bool one = true;
      for (uint32_t s2 = 0; s2 < pb->nsuccs; s2++) {
        uint32_t t2 = pb->succs[s2].target;
        if (comp[t2] == c && t2 != t) one = false;
      }
      if (!one || cyc[p]) continue;
      if (hdr[c] == UINT32_MAX) hdr[c] = t;
      else if (hdr[c] != t) bad[c] = 1;
    }
  }
  /* Which edges move to the copy: every way into a cycle that is not its
     header's own, from a block that enters the cycle somewhere else too or
     at a block other than the header. */
  uint8_t *moves = xcalloc(nb0 ? nb0 : 1, 1);
  uint8_t *inR = xcalloc(nb0 ? nb0 : 1, 1);
  uint32_t *wl = xmalloc((nb0 ? nb0 : 1) * sizeof(uint32_t)), nwl = 0;
  bool any = false;
  for (uint32_t p = 0; p < nb0; p++) {
    Block *pb = &f->blocks[p];
    if (!pb->reachable) continue;
    for (uint32_t s = 0; s < pb->nsuccs; s++) {
      uint32_t t = pb->succs[s].target, c = comp[t];
      if (!cyc[t] || comp[p] == c || bad[c] || hdr[c] == UINT32_MAX) continue;
      bool side = false;
      for (uint32_t s2 = 0; s2 < pb->nsuccs; s2++) {
        uint32_t t2 = pb->succs[s2].target;
        if (comp[t2] == c && t2 != hdr[c]) side = true;
      }
      if (!side) continue;
      if (cyc[p] || pb->succs[s].eh) { bad[c] = 1; continue; }
      moves[p] = 1;
      any = true;
    }
  }
  if (any) {
    /* The copied region: closed over successors on a cycle. */
    for (uint32_t p = 0; p < nb0; p++) {
      if (!moves[p]) continue;
      Block *pb = &f->blocks[p];
      for (uint32_t s = 0; s < pb->nsuccs; s++) {
        uint32_t t = pb->succs[s].target, c = comp[t];
        if (!cyc[t] || comp[p] == c || bad[c] || hdr[c] == UINT32_MAX) continue;
        if (!inR[t]) { inR[t] = 1; wl[nwl++] = t; }
      }
    }
    uint32_t nR = 0, ncopy = 0;
    while (nwl) {
      uint32_t x = wl[--nwl];
      nR++;
      ncopy += f->blocks[x].ninsts + f->blocks[x].nparams;
      Block *xb = &f->blocks[x];
      for (uint32_t s = 0; s < xb->nsuccs; s++) {
        uint32_t t = xb->succs[s].target;
        if (cyc[t] && !inR[t]) { inR[t] = 1; wl[nwl++] = t; }
      }
    }
    if (ncopy > NS_MAX_COPY || ncopy > ni0) any = false;
    (void)nR;
  }

  /* The values the region defines and something outside it reads, which
     will need merging; each has to be a number for td_repair. */
  uint8_t *esc = NULL;
  uint32_t *eslot = NULL, nesc = 0;
  TdVec *euse = NULL;
  if (any) {
    esc = xcalloc(ni0 ? ni0 : 1, 1);
    eslot = xmalloc((ni0 ? ni0 : 1) * sizeof(uint32_t));
    nesc = escaping_values(f, inR, nb0, ni0, esc, eslot, &euse);
    for (uint32_t v = 0; v < ni0 && any; v++)
      if (esc[v] && !td_numeric((ValType)f->insts[v].type)) any = false;
  }
  if (!any) {
    for (uint32_t i = 0; i < nesc; i++) xfree(euse[i].v);
    xfree(comp); xfree(csize); xfree(cyc); xfree(hdr); xfree(bad);
    xfree(moves); xfree(inR); xfree(wl); xfree(esc); xfree(eslot); xfree(euse);
    return false;
  }

  Ref *map = xmalloc(ni0 * sizeof(Ref));
  uint32_t *bcopy = xmalloc(nb0 * sizeof(uint32_t));
  clone_blocks(f, inR, nb0, ni0, bcopy, map);
  for (uint32_t x = 0; x < nb0; x++)
    if (inR[x]) f->blocks[bcopy[x]].cold = true;

  /* The ways in that move. */
  for (uint32_t p = 0; p < nb0; p++) {
    if (!moves[p]) continue;
    Block *pb = &f->blocks[p];
    for (uint32_t s = 0; s < pb->nsuccs; s++) {
      uint32_t t = pb->succs[s].target;
      if (t < nb0 && inR[t]) pb->succs[s].target = bcopy[t];
    }
  }
  ir_order(f);
  repair_copies(f, ni0, esc, eslot, euse, bcopy, map);
  for (uint32_t i = 0; i < nesc; i++) xfree(euse[i].v);
  xfree(eslot); xfree(euse);
  g_stats.branch_simplified++;

  xfree(comp); xfree(csize); xfree(cyc); xfree(hdr); xfree(bad);
  xfree(moves); xfree(inR); xfree(wl); xfree(esc); xfree(map); xfree(bcopy);
  prune_params(f);
  ir_order(f);
  return true;
}

typedef struct {
  Ref addr; uint64_t off; uint32_t size; uint32_t mem; Ref value; bool sgn;
  bool valid; bool pending; uint32_t origin; uint8_t vtype;
  /* only the value's low 1 << size bytes are what memory holds: an i64
     stored as 4 bytes, whose high half nothing knows */
  bool low;
} MemEnt;
typedef struct {
  Ref addr; uint64_t off; uint32_t size; uint32_t mem; Ref inst;
} PendStore;

/* Accesses that ran, without a trap, on every path to a point: a scan's
   entries, valid or not, and its pending stores.  same_access_after takes
   them as witnesses that an address did not wrap. */
typedef struct {
  const MemEnt *ent; uint32_t nent;
  const PendStore *pend; uint32_t npend;
} MemRan;

#define MEMOPT_ENTS 64

/* A MemEnt with this mem index tracks a GC struct field keyed by
   (object root, byte offset) instead of a linear-memory slot.  The two
   classes never alias, which the mem inequality already encodes, and
   object refs pass through access_base/access_root unchanged, so
   same_access and may_alias work for these entries as written. */
#define GC_FIELD_MEM 0xfffffffeu

#define MEMOPT_PREDS 32

#define MEMOPT_SWEEPS 8

#define MAX_TRACKED_GLOBALS 64

/* A block's memory state at its end.  Entries and pending stores take arrays
   of their own size, the tracked globals a mask and, once one is held, an
   array of values: a fixed-size state was 4.5 KB a block, and keeping one
   per block and a copy of each for the sweep test cost a function of 50,000
   blocks 450 MB, most of it copied and compared every sweep. */
typedef struct {
  MemEnt *ent;
  PendStore *pend;
  Ref *gval;
  uint32_t nent, npend, cap_ent, cap_pend;
  uint64_t gok;
  bool valid;
} MemState;
_Static_assert(MAX_TRACKED_GLOBALS <= 64, "MemState.gok is a 64-bit mask");

static void memstate_free(MemState *s) {
  xfree(s->ent); xfree(s->pend); xfree(s->gval);
}

/* Whether a block's end state, as the scan's arrays now hold it, is the one
   the block ended with last sweep. */
static bool memstate_same(const MemState *a, const MemEnt *ent, uint32_t nent,
                          const PendStore *pend, uint32_t npend,
                          const Ref *gval, uint64_t gok) {
  if (!a->valid || a->nent != nent || a->npend != npend) return false;
  for (uint32_t i = 0; i < nent; i++) {
    const MemEnt *x = &a->ent[i], *y = &ent[i];
    if (x->addr != y->addr || x->off != y->off || x->size != y->size ||
        x->mem != y->mem ||
        x->value != y->value || x->sgn != y->sgn || x->valid != y->valid ||
        x->pending != y->pending || x->origin != y->origin ||
        x->vtype != y->vtype || x->low != y->low)
      return false;
  }
  for (uint32_t i = 0; i < npend; i++) {
    const PendStore *x = &a->pend[i], *y = &pend[i];
    if (x->addr != y->addr || x->off != y->off || x->size != y->size ||
        x->mem != y->mem || x->inst != y->inst)
      return false;
  }
  if (a->gok != gok) return false;
  for (uint32_t g = 0; g < MAX_TRACKED_GLOBALS; g++)
    if ((gok >> g) & 1 && a->gval[g] != gval[g]) return false;
  return true;
}

static void memstate_store(MemState *d, const MemEnt *ent, uint32_t nent,
                           const PendStore *pend, uint32_t npend,
                           const Ref *gval, uint64_t gok) {
  if (nent > d->cap_ent) {
    d->cap_ent = nent;
    d->ent = xrealloc(d->ent, nent * sizeof(MemEnt));
  }
  if (nent) memcpy(d->ent, ent, nent * sizeof(MemEnt));
  d->nent = nent;
  if (npend > d->cap_pend) {
    d->cap_pend = npend;
    d->pend = xrealloc(d->pend, npend * sizeof(PendStore));
  }
  if (npend) memcpy(d->pend, pend, npend * sizeof(PendStore));
  d->npend = npend;
  if (gok) {
    if (!d->gval) d->gval = xmalloc(MAX_TRACKED_GLOBALS * sizeof(Ref));
    memcpy(d->gval, gval, MAX_TRACKED_GLOBALS * sizeof(Ref));
  }
  d->gok = gok;
  d->valid = true;
}

static void access_base(IrFunc *f, Ref addr, Ref *base, uint64_t *off) {
  Inst *in = &f->insts[addr];
  if (in->op == I_MEM_ADDR) { *base = ir_args(f, addr)[0]; *off = in->imm; }

  else if (in->op == I_MEM_PTR) { *base = ir_args(f, addr)[0]; *off = 0; }
  else if (in->op == I_CONST) { *base = REF_NONE; *off = in->imm; }
  else { *base = addr; *off = 0; }
  /* A constant index is a constant address, however it is spelled: the
     pointer mem_ptr made for a run of globals before a loop was unrolled,
     and the constants the unrolled copies compute, name the same slots. */
  if (*base != REF_NONE && f->insts[*base].op == I_CONST &&
      (ValType)f->insts[*base].type != VT_V128) {
    uint64_t c = f->insts[*base].imm;
    *off += vt_is_64((ValType)f->insts[*base].type) ? c : (uint64_t)(uint32_t)c;
    *base = REF_NONE;
  }
}

#define ADDR_WALK_DEPTH 8
static void access_root(IrFunc *f, Ref addr, Ref *root, int64_t *off) {
  Ref base;
  uint64_t o;
  access_base(f, addr, &base, &o);
  int64_t acc = (int64_t)o;
  for (int step = 0; step < ADDR_WALK_DEPTH && base != REF_NONE; step++) {
    Inst *in = &f->insts[base];
    if (in->nargs != 2) break;
    if (in->op != I_ADD && in->op != I_SUB) break;
    Ref *a = ir_args(f, base);
    if (f->insts[a[1]].op != I_CONST) break;
    int64_t c = (int64_t)(int32_t)(uint32_t)f->insts[a[1]].imm;
    if (vt_is_64((ValType)in->type)) c = (int64_t)f->insts[a[1]].imm;
    acc += (in->op == I_ADD) ? c : -c;
    base = a[0];
  }
  *root = base;
  *off = acc;
}

static bool may_alias(IrFunc *f, Ref addr_a, uint64_t imm_a, uint32_t size_a,
                      uint32_t mem_a,
                      Ref addr_b, uint64_t imm_b, uint32_t size_b,
                      uint32_t mem_b) {

  if (mem_a != mem_b) return false;
  Ref ra, rb;
  int64_t oa, ob;
  access_root(f, addr_a, &ra, &oa);
  access_root(f, addr_b, &rb, &ob);
  if (ra != rb) return true;
  oa += (int64_t)imm_a;
  ob += (int64_t)imm_b;
  int64_t d = oa - ob;
  int64_t wa = (int64_t)1 << size_a, wb = (int64_t)1 << size_b;
  if (d >= wb || -d >= wa) {

    int64_t ad = d < 0 ? -d : d;
    if ((int64_t)0x100000000ll - ad >= (wa > wb ? wa : wb)) return false;
  }
  return true;
}

/* An address as root + arith + imm: arith the constants added to the root
   in its own width, wrapping, and imm the offsets an access adds without
   wrapping.  Two accesses with one root are at one address for every value
   of it exactly when the ariths agree in that width and the imms agree. */
static void access_parts(IrFunc *f, Ref addr, Ref *root, uint64_t *arith,
                         uint64_t *imm, bool *wide) {
  Ref base;
  access_base(f, addr, &base, imm);
  *arith = 0;
  *wide = base != REF_NONE && vt_is_64((ValType)f->insts[base].type);
  for (int step = 0; step < ADDR_WALK_DEPTH && base != REF_NONE; step++) {
    Inst *in = &f->insts[base];
    if (in->nargs != 2 || (in->op != I_ADD && in->op != I_SUB)) break;
    Ref *a = ir_args(f, base);
    if (f->insts[a[1]].op != I_CONST) break;
    uint64_t c = f->insts[a[1]].imm;
    *arith += in->op == I_ADD ? c : (uint64_t)0 - c;
    base = a[0];
  }
  *root = base;
}

static bool same_access(IrFunc *f, Ref addr_a, uint64_t imm_a, uint32_t size_a,
                        uint32_t mem_a,
                        Ref addr_b, uint64_t imm_b, uint32_t size_b,
                        uint32_t mem_b) {
  if (size_a != size_b || mem_a != mem_b) return false;
  Ref ba, bb;
  uint64_t oa, ob;
  access_base(f, addr_a, &ba, &oa);
  access_base(f, addr_b, &bb, &ob);
  if (ba == bb && oa + imm_a == ob + imm_b) return true;
  /* Go's stack slots, once a call is inlined: the callee stores its result
     at (sp - 8) + 24 and the caller loads it from sp + 16. */
  Ref ra, rb;
  uint64_t ca, cb, ia, ib;
  bool wa, wb;
  access_parts(f, addr_a, &ra, &ca, &ia, &wa);
  access_parts(f, addr_b, &rb, &cb, &ib, &wb);
  if (ra == REF_NONE || ra != rb || wa != wb) return false;
  uint64_t mask = wa ? ~0ull : 0xffffffffull;
  return ((ca - cb) & mask) == 0 && ia + imm_a == ib + imm_b;
}

/* same_access, for an access b known to run after a, on every path to it,
   with a having run without a trap.  Then a constant may sit in the i32
   add of one and the offset of the other: a at (r + cs) + is and b at
   (r + cl) + il, with cs + is = cl + il, are at one address when the later
   one's add is the larger by d = cl - cs (mod 2^32) with d <= is, because a
   reached zext(r + cs) + is < 2^32 without trapping and so r + cs + d
   cannot wrap.  After inlining, a caller's store at sp[416] and the
   callee's load from (sp + 416)[0] are such a pair -- C's struct copies,
   Go's arguments at (sp - 8) + 8, Rust's i128 results -- and none of them
   was forwarded.  32-bit memories only, and not where neither access ran
   first (two predecessors' entries at a join).

   The later one's add may also be the smaller, by e = cs - cl < 2^16, with
   il = is + e: Go's callee loads an argument the caller stored at sp + 8
   from (sp - 8) + 16.  r + cl wraps below zero only if r + cs < e, and then
   zext(r + cl) >= 2^32 - 2^16, past any memory the runtime makes (it caps
   wasm32 memories at 65535 pages, rt.c make_memory).  So an access at
   zext(r + cl) that ran -- in `ran`, as the callee's store of its return
   PC at (sp - 8)[0] is -- proves the wrap did not happen. */
static bool addr_ran(IrFunc *f, const MemRan *ran, Ref r, uint64_t c) {
  for (uint32_t pass = 0; pass < 2; pass++) {
    uint32_t n = pass ? ran->npend : ran->nent;
    for (uint32_t k = 0; k < n; k++) {
      Ref ra;
      uint64_t ca, ia;
      bool wa;
      access_parts(f, pass ? ran->pend[k].addr : ran->ent[k].addr, &ra, &ca, &ia, &wa);
      if (ra == r && !wa && ((ca - c) & 0xffffffffull) == 0) return true;
    }
  }
  return false;
}

static bool same_access_after(IrFunc *f, Ref addr_a, uint64_t imm_a, uint32_t size_a,
                              uint32_t mem_a,
                              Ref addr_b, uint64_t imm_b, uint32_t size_b,
                              uint32_t mem_b, const MemRan *ran) {
  if (same_access(f, addr_a, imm_a, size_a, mem_a, addr_b, imm_b, size_b, mem_b))
    return true;
  if (size_a != size_b || mem_a != mem_b) return false;
  Ref ra, rb;
  uint64_t ca, cb, ia, ib;
  bool wa, wb;
  access_parts(f, addr_a, &ra, &ca, &ia, &wa);
  access_parts(f, addr_b, &rb, &cb, &ib, &wb);
  if (ra == REF_NONE || ra != rb || wa || wb) return false;
  uint64_t is = ia + imm_a, il = ib + imm_b;
  uint64_t d = (cb - ca) & 0xffffffffull;
  if (d <= is) return is - d == il;
  uint64_t e = (ca - cb) & 0xffffffffull;
  return ran && e < 0x10000 && il == is + e && addr_ran(f, ran, rb, cb);
}

#define FRAME_SPAN ((int64_t)1 << 30)

typedef struct {
  uint8_t state;
  bool writes_globals;

  uint64_t gmask;
  bool gany;
  bool writes_mem;
  bool frame_only;

  /* GC writes the callee (transitively) can make: struct-field offsets
     hashed into a mask, and whether any array element is written.
     Initialising stores are excluded -- they write objects that did not
     exist when any value the caller tracks was fixed. */
  uint64_t gc_field_mask;
  bool gc_array_write;
  uint32_t frame_global;
  int64_t lo, hi;
} CallFx;

static const CallFx fx_unknown = { .state = 2, .writes_globals = true,
                                  .gany = true, .writes_mem = true,
                                  .gc_field_mask = ~(uint64_t)0,
                                  .gc_array_write = true };

/* The one hash for field-offset clobber masks; every producer and
   consumer must agree on it. */
static uint64_t gc_off_bit(uint64_t off) {
  return (uint64_t)1 << ((off >> 2) & 63);
}

#define FX_GBITS 64

static void fx_mark_global(uint64_t *mask, bool *any, uint32_t g) {
  if (g < FX_GBITS) *mask |= (uint64_t)1 << g;
  else *any = true;
}

#define FX_MAX_DEPTH 32
#define FX_MAX_CALLEES 64

typedef struct { uint32_t ti, fld, fi; } VtStore;

typedef struct {
  bool scanned;
  bool has_body;
  bool callees_capped;
  bool writes_globals, gany, writes_mem, frame_only;
  uint64_t gmask;
  uint64_t gc_field_mask;
  bool gc_array_write;
  uint32_t fg;
  int64_t lo, hi;
  uint32_t callees[FX_MAX_CALLEES];
  uint32_t ncallees;
  VtStore *vtst;
  uint32_t nvtst, capvtst;
} DirectFx;

static Module *fx_mod;
static const OptOpts *fx_opts;
static CallFx *fx_func;
static DirectFx *fx_direct;
static bool fx_resolved;
static bool fx_warming;
static int fx_indirect_done;
static CallFx fx_indirect;

typedef struct {
  bool computed;
  bool frozen;
  uint32_t nentries;
  uint32_t init_fi;
  const uint32_t *entry;   /* the module's Table0Facts own it */
} FxTable0;

#define FX_TBL_NULL TABLE0_NULL

static FxTable0 fx_table0;

/* One slot per struct field: which function a func-ref field can hold,
   unioned over every creation site in the module and, by construction,
   over the type's whole subtype cone (every contribution is applied to
   the type and all its ancestors). */
enum { FX_VT_EMPTY = 0, FX_VT_MONO, FX_VT_POLY };

#define FX_VT_NULLV 0xfffffffeu
#define FX_VT_UNK   0xfffffffdu
#define FX_GFUNC_NONE 0xffffffffu

typedef struct {
  uint32_t fi;
  uint8_t state;
  uint8_t null_seen;
} VtSlot;

static VtSlot *fx_vt;
static uint32_t *fx_vtbase;
static uint32_t *fx_gfunc;
static uint32_t *fx_ncallers;

/* Constant objects: per immutable global whose expression indexes, where
   its instructions' entries start in fx_cg_first (FX_CG_NONE otherwise);
   per instruction, where its operands start in fx_cg_opnd; and the
   operands, as instruction indices in the same expression. */
#define FX_CG_NONE 0xffffffffu
static uint32_t *fx_cg_at, *fx_cg_first, *fx_cg_opnd;
static bool fx_vt_poisoned;
static uint32_t fx_nfuncs;

static bool fx_field_is_funcref(const Module *m, ValType t) {
  if (!vt_is_ref(t)) return false;
  if (vt_concrete(t)) return m->types[vt_type_index(t)].kind == CT_FUNC;
  HeapType h = vt_heap(t);
  return h == HT_FUNC || h == HT_NOFUNC;
}

static bool fx_type_has_funcref_field(const Module *m, uint32_t ti) {
  const CompType *t = &m->types[ti];
  if (t->kind != CT_STRUCT) return false;
  for (uint32_t j = 0; j < t->nfields; j++)
    if (fx_field_is_funcref(m, t->fields[j].type)) return true;
  return false;
}

static void fx_vt_push(DirectFx *d, uint32_t ti, uint32_t fld, uint32_t fi) {
  if (d->nvtst == d->capvtst) {
    d->capvtst = d->capvtst ? d->capvtst * 2 : 8;
    d->vtst = xrealloc(d->vtst, d->capvtst * sizeof(VtStore));
  }
  d->vtst[d->nvtst++] = (VtStore){ ti, fld, fi };
}

bool opt_effects_workers_running;

static void fx_scan_direct(Module *m, const OptOpts *o, uint32_t fi);
static const CallFx *call_effects(Module *m, const OptOpts *o, uint32_t fi,
                                  int depth);
static const CallFx *indirect_effects(Module *m, const OptOpts *o);

void opt_effects_begin(Module *m, const OptOpts *o) {
  if (fx_mod == m && fx_opts == o) return;
  xfree(fx_func);
  if (fx_direct)
    for (uint32_t i = 0; i < fx_nfuncs; i++) xfree(fx_direct[i].vtst);
  xfree(fx_direct);
  memset(&fx_table0, 0, sizeof(fx_table0));
  xfree(fx_vt); fx_vt = NULL;
  xfree(fx_vtbase); fx_vtbase = NULL;
  xfree(fx_gfunc); fx_gfunc = NULL;
  xfree(fx_ncallers); fx_ncallers = NULL;
  xfree(fx_cg_at); fx_cg_at = NULL;
  xfree(fx_cg_first); fx_cg_first = NULL;
  xfree(fx_cg_opnd); fx_cg_opnd = NULL;
  fx_vt_poisoned = false;
  fx_mod = m;
  fx_opts = o;
  fx_nfuncs = m->nfuncs;
  fx_func = xcalloc(m->nfuncs ? m->nfuncs : 1, sizeof(CallFx));
  fx_direct = xcalloc(m->nfuncs ? m->nfuncs : 1, sizeof(DirectFx));
  fx_indirect_done = 0;
  fx_resolved = false;
}

void opt_effects_scan(Module *m, const OptOpts *o, uint32_t fi) {
  if (fx_mod != m || fx_opts != o || fi >= m->nfuncs) return;
  if (!fx_direct[fi].scanned) fx_scan_direct(m, o, fi);
}

static void fx_facts_build(Module *m);

void opt_effects_end(Module *m, const OptOpts *o) {
  if (fx_mod != m || fx_opts != o || fx_resolved) return;
  fx_warming = true;
  for (uint32_t fi = 0; fi < m->nfuncs; fi++) call_effects(m, o, fi, 0);
  indirect_effects(m, o);
  fx_facts_build(m);
  fx_warming = false;
  fx_resolved = true;
}

static void fx_need(Module *m, const OptOpts *o) {
  if (fx_mod == m && fx_opts == o && (fx_resolved || fx_warming)) return;
  if (opt_effects_workers_running)
    warmc_fatal("internal: call effects for a module the workers were started "
                "without warming");
  opt_effects_begin(m, o);
  fx_warming = true;
  for (uint32_t fi = 0; fi < m->nfuncs; fi++) opt_effects_scan(m, o, fi);
  fx_warming = false;
  opt_effects_end(m, o);
}

static const CallFx *call_effects(Module *m, const OptOpts *o, uint32_t fi,
                                  int depth);

static bool is_call_op(uint16_t op) {
  return op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF ||
         op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
         op == I_RETURN_CALL_REF;
}

static void fx_scan_direct(Module *m, const OptOpts *o, uint32_t fi) {
  DirectFx *d = &fx_direct[fi];
  d->scanned = true;
  d->fg = 0xffffffffu;
  d->frame_only = true;
  if (m->funcs[fi].imported || !m->funcs[fi].body) return;
  d->has_body = true;

  /* every block is read, reached or not, and nothing here asks the order */
  IrFunc *g = ir_build_unordered(m, fi, o);
  uint32_t *callees = d->callees;
  uint32_t ncallees = 0;
  bool frame_only = true, writes_globals = false, writes_mem = false;
  uint64_t gmask = 0;
  uint64_t gc_fields = 0;
  bool gc_arrays = false;
  bool gany = false;
  uint32_t fg = 0xffffffffu;

  int64_t lo = 0, hi = -FRAME_SPAN;

  for (uint32_t bi = 0; bi < g->nblocks; bi++) {
    Block *b = &g->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &g->insts[v];
      switch (in->op) {
        case I_GLOBAL_SET:

          writes_globals = true;
          frame_only = false;
          fx_mark_global(&gmask, &gany, (uint32_t)in->imm);
          break;
        case I_GLOBAL_GET:

          fx_mark_global(&gmask, &gany, (uint32_t)in->imm);
          break;
        case I_MEM_GROW: case I_MEM_COPY: case I_MEM_FILL: case I_MEM_INIT:
          writes_mem = true;
          frame_only = false;
          break;

        case I_CALL_INDIRECT: case I_RETURN_CALL_INDIRECT:
        case I_CALL_REF: case I_RETURN_CALL_REF:
          writes_globals = true;
          gany = true;
          writes_mem = true;
          frame_only = false;
          gc_fields = ~(uint64_t)0;
          gc_arrays = true;
          break;
        case I_CALL: case I_RETURN_CALL:
          if (ncallees < FX_MAX_CALLEES) callees[ncallees++] = (uint32_t)in->imm;
          else {
            d->callees_capped = true;
            writes_globals = true; gany = true;
            writes_mem = true; frame_only = false;
            gc_fields = ~(uint64_t)0;
            gc_arrays = true;
          }
          break;
        case I_FIELD_SET:
          /* an initialising store writes an object younger than anything
             a caller could be tracking */
          if (!(in->imm2 & GCF_INIT)) gc_fields |= gc_off_bit(in->imm);
          break;
        case I_ARRAY_SET:
          if (!(in->imm2 & GCF_INIT)) gc_arrays = true;
          break;
        case I_ARRAY_OP:
          gc_arrays = true;
          break;
        case I_STRUCT_NEW: {
          uint32_t ti = (uint32_t)in->imm;
          if (ti >= m->ntypes) break;
          CompType *t = &m->types[ti];
          if (t->kind != CT_STRUCT || !fx_type_has_funcref_field(m, ti)) break;

          /* Fresh ir_build output: struct.new is followed by one GCF_INIT
             field set per field, in field order; struct.new_default emits
             none, which reads as a null in every field. */
          uint32_t i2 = i + 1;
          bool has_sets = false;
          if (i2 < b->ninsts) {
            Inst *si = &g->insts[b->insts[i2]];
            has_sets = si->op == I_FIELD_SET && (si->imm2 & GCF_INIT) &&
                       ir_args(g, b->insts[i2])[0] == v;
          }
          for (uint32_t j = 0; j < t->nfields; j++) {
            bool isf = fx_field_is_funcref(m, t->fields[j].type);
            uint32_t got = FX_VT_NULLV;
            if (has_sets) {
              got = FX_VT_UNK;
              if (i2 < b->ninsts) {
                Ref w = b->insts[i2];
                Inst *si = &g->insts[w];
                if (si->op == I_FIELD_SET && (si->imm2 & GCF_INIT) &&
                    ir_args(g, w)[0] == v &&
                    si->imm == (uint64_t)t->hdr_size + t->fields[j].offset) {
                  if (isf) {
                    /* ref.null in a body is an I_CONST of ref type, value 0 */
                    Inst *vi = &g->insts[ir_resolve(g, ir_args(g, w)[1])];
                    bool isnull = vi->op == I_REF_NULL ||
                                  (vi->op == I_CONST && vi->imm == 0 &&
                                   vt_is_ref((ValType)vi->type));
                    got = vi->op == I_REF_FUNC ? (uint32_t)vi->imm
                          : isnull             ? FX_VT_NULLV
                                               : FX_VT_UNK;
                  }
                  i2++;
                }
              }
            }
            if (isf) fx_vt_push(d, ti, j, got);
          }
          break;
        }
        case I_STORE: {
          writes_mem = true;

          if (MEMF_MEM(in->imm2) != 0) { frame_only = false; break; }
          Ref root;
          int64_t off;
          access_root(g, ir_args(g, v)[0], &root, &off);
          if (root == REF_NONE || g->insts[root].op != I_GLOBAL_GET) {
            frame_only = false;
            break;
          }
          uint32_t gi = (uint32_t)g->insts[root].imm;
          if (fg == 0xffffffffu) fg = gi;
          if (fg != gi) { frame_only = false; break; }
          int64_t d = off + (int64_t)in->imm;
          int64_t w = (int64_t)1 << MEMF_SIZE(in->imm2);
          if (d + w > 0 || d < -FRAME_SPAN) { frame_only = false; break; }
          if (d < lo) lo = d;
          if (d + w > hi) hi = d + w;
          break;
        }
        default: break;
      }
    }
  }
  ir_free(g);

  d->writes_globals = writes_globals;
  d->gany = gany;
  d->gmask = gmask;
  d->gc_field_mask = gc_fields;
  d->gc_array_write = gc_arrays;
  d->writes_mem = writes_mem;
  d->frame_only = frame_only;
  d->fg = fg;
  d->lo = lo;
  d->hi = hi;
  d->ncallees = ncallees;
}

static void analyse_effects(Module *m, const OptOpts *o, uint32_t fi, int depth,
                            CallFx *out) {
  *out = fx_unknown;
  if (fi >= m->nfuncs) return;
  if (depth >= FX_MAX_DEPTH) return;

  if (!fx_direct[fi].scanned) fx_scan_direct(m, o, fi);
  const DirectFx *d = &fx_direct[fi];
  if (!d->has_body) return;

  const uint32_t *callees = d->callees;
  uint32_t ncallees = d->ncallees;
  bool frame_only = d->frame_only, writes_globals = d->writes_globals;
  bool writes_mem = d->writes_mem, gany = d->gany;
  uint64_t gmask = d->gmask;
  uint64_t gc_fields = d->gc_field_mask;
  bool gc_arrays = d->gc_array_write;
  uint32_t fg = d->fg;
  int64_t lo = d->lo, hi = d->hi;

  for (uint32_t k = 0; k < ncallees; k++) {
    const CallFx *c = call_effects(m, o, callees[k], depth + 1);
    if (c->writes_globals) writes_globals = true;
    gmask |= c->gmask;
    if (c->gany) gany = true;
    gc_fields |= c->gc_field_mask;
    if (c->gc_array_write) gc_arrays = true;
    if (c->writes_mem) writes_mem = true;
    if (!c->frame_only) { frame_only = false; continue; }
    if (!c->writes_mem) continue;
    if (fg == 0xffffffffu) fg = c->frame_global;
    if (fg != c->frame_global) { frame_only = false; continue; }

    if (c->lo < lo) lo = c->lo;
    if (c->hi > hi) hi = c->hi;
  }

  out->writes_globals = writes_globals;
  out->gmask = gmask;
  out->gany = gany;
  out->gc_field_mask = gc_fields;
  out->gc_array_write = gc_arrays;
  out->writes_mem = writes_mem;
  out->frame_only = frame_only && !writes_globals;
  out->frame_global = (fg == 0xffffffffu) ? 0 : fg;
  out->lo = writes_mem ? lo : 0;
  out->hi = writes_mem ? hi : 0;
}

static const CallFx *call_effects(Module *m, const OptOpts *o, uint32_t fi,
                                  int depth) {
  fx_need(m, o);
  if (fi >= m->nfuncs) return &fx_unknown;
  CallFx *e = &fx_func[fi];
  if (e->state == 2) return e;
  if (e->state == 1) return &fx_unknown;

  if (depth >= FX_MAX_DEPTH) return &fx_unknown;
  e->state = 1;
  CallFx tmp;
  analyse_effects(m, o, fi, depth, &tmp);
  tmp.state = 2;
  *e = tmp;
  return e;
}

/* One walk of a body for table 0's facts: does it write a table (an
   undecodable body counts as one), and which types do its call_indirects
   through table 0 name. */
static bool scan_table_ops(const uint8_t *p, const uint8_t *end,
                           uint32_t *sites, uint32_t ntypes) {
  Rd r = rd_make(p, end, p);
  while (!rd_eof(&r)) {
    uint32_t op = wasm_read_op(&r);
    switch (op) {
      case 0x26:
      case OP_PREFIXED(0xfc, 12):
      case OP_PREFIXED(0xfc, 14):
      case OP_PREFIXED(0xfc, 15):
      case OP_PREFIXED(0xfc, 17):
        return true;
      case 0x11: case 0x13: {
        uint32_t ti = rd_u32(&r), tab = rd_u32(&r);
        if (tab == 0 && ti < ntypes) sites[ti]++;
        continue;
      }
      default: break;
    }
    if (!wasm_skip_imm(&r, op)) return true;
  }
  return false;
}

typedef struct { Module *m; uint32_t **sites; _Atomic bool writes; } T0Scan;

static void t0_scan_one(void *ctx, uint32_t k, uint32_t w) {
  T0Scan *sc = ctx;
  if (atomic_load(&sc->writes)) return;
  const Func *fn = &sc->m->funcs[sc->m->nimported_funcs + k];
  if (fn->body && scan_table_ops(fn->body, fn->body_end, sc->sites[w], sc->m->ntypes))
    atomic_store(&sc->writes, true);
}

/* Table 0 is frozen when nothing can change it after instantiation: it is
   the module's only table, module-private, never a table-write target, and
   filled only by single-instruction ref.func/ref.null elem items.  A table
   init expression is a writer the elem walk never sees — its default value
   reaches every slot no segment covers — so it must be a single ref.null or
   ref.func too, and the ref.func case is kept for the effect meet.  Known
   once per module, and first asked for by compile_module before any worker
   runs: the devirtualiser and module_data.c's code tables read the same
   answer. */
const Table0Facts *module_table0_facts(Module *m) {
  if (m->t0) return m->t0;
  Table0Facts *t0 = xcalloc(1, sizeof *t0);
  m->t0 = t0;
  t0->init_fi = TABLE0_NULL;

  if (m->ntables != 1 || m->tables[0].imported) return t0;
  for (uint32_t i = 0; i < m->nexports; i++)
    if (m->exports[i].kind == EK_TABLE) return t0;
  t0->sites = xcalloc(m->ntypes ? m->ntypes : 1, sizeof(uint32_t));
  /* Every body is walked, on the compiler's threads (15 ms of a serial
     front end for stdgo otherwise), each counting its call_indirects into
     its own array.  The counts are read only of a frozen table, for which
     no walk stopped early, so their sum is what one walk in order counts. */
  uint32_t nb = m->nfuncs - m->nimported_funcs;
  uint32_t nt = compile_want_threads(m, nb);
  T0Scan sc = { m, xcalloc(nt, sizeof(uint32_t *)), false };
  for (uint32_t w = 0; w < nt; w++)
    sc.sites[w] = xcalloc(m->ntypes ? m->ntypes : 1, sizeof(uint32_t));
  warmc_par_for(nb, nt, t0_scan_one, &sc);
  for (uint32_t w = 0; w < nt; w++) {
    for (uint32_t ti = 0; ti < m->ntypes; ti++) t0->sites[ti] += sc.sites[w][ti];
    xfree(sc.sites[w]);
  }
  xfree(sc.sites);
  if (atomic_load(&sc.writes)) return t0;

  Table *t = &m->tables[0];
  if (t->has_init) {
    if (t->init.n != 1) return t0;
    if (t->init.ins[0].op == IOP_REF_FUNC)
      t0->init_fi = (uint32_t)t->init.ins[0].lo;
    else if (t->init.ins[0].op != IOP_REF_NULL)
      return t0;
  }
  for (uint32_t s = 0; s < m->nelems; s++) {
    ElemSeg *e = &m->elems[s];
    if (e->mode != SEG_ACTIVE) continue;
    if (e->table_index != 0) return t0;
    for (uint32_t k = 0; k < e->n; k++) {
      const InitExpr *it = &e->items[k];
      if (it->n != 1) return t0;
      if (it->ins[0].op != IOP_REF_FUNC && it->ins[0].op != IOP_REF_NULL)
        return t0;
    }
  }
  t0->frozen = true;

  if (t->min > (1u << 20)) return t0;
  uint32_t n = (uint32_t)t->min;
  uint32_t *entry = xmalloc((n ? n : 1) * sizeof(uint32_t));
  for (uint32_t i = 0; i < n; i++) entry[i] = t0->init_fi;
  for (uint32_t s = 0; s < m->nelems; s++) {
    ElemSeg *e = &m->elems[s];
    if (e->mode != SEG_ACTIVE) continue;
    if (e->offset.n != 1 || e->offset.ins[0].op != IOP_I32_CONST ||
        e->offset.ins[0].lo + (uint64_t)e->n > n) {
      xfree(entry);
      return t0;
    }
    for (uint32_t k = 0; k < e->n; k++)
      entry[(uint32_t)e->offset.ins[0].lo + k] =
          e->items[k].ins[0].op == IOP_REF_FUNC ? (uint32_t)e->items[k].ins[0].lo
                                                : TABLE0_NULL;
  }
  t0->n = n;
  t0->entry = entry;
  return t0;
}

static void fx_table_freeze(Module *m) {
  if (fx_table0.computed) return;
  fx_table0.computed = true;
  const Table0Facts *t0 = module_table0_facts(m);
  fx_table0.frozen = t0->frozen;
  fx_table0.init_fi = t0->init_fi;
  fx_table0.nentries = t0->entry ? t0->n : 0;
  fx_table0.entry = t0->entry;
}

static void fx_meet(CallFx *meet, const CallFx *c) {
  if (c->writes_globals) meet->writes_globals = true;
  meet->gmask |= c->gmask;
  if (c->gany) meet->gany = true;
  meet->gc_field_mask |= c->gc_field_mask;
  if (c->gc_array_write) meet->gc_array_write = true;
  if (!c->frame_only) meet->frame_only = false;
  if (!c->writes_mem) return;
  meet->writes_mem = true;
  if (meet->frame_global == 0xffffffffu) meet->frame_global = c->frame_global;
  if (meet->frame_global != c->frame_global) meet->frame_only = false;
  if (c->lo < meet->lo) meet->lo = c->lo;
  if (c->hi > meet->hi) meet->hi = c->hi;
}

/* Apply one creation-site contribution to a (type, field) slot and to the
   same field of every ancestor, so a lookup at a supertype-typed receiver
   already unions the whole cone below it. */
static void fx_vt_note(const Module *m, uint32_t ti, uint32_t j, uint32_t fi) {
  for (;;) {
    const CompType *t = &m->types[ti];
    if (t->kind != CT_STRUCT || j >= t->nfields) return;
    VtSlot *s = &fx_vt[fx_vtbase[ti] + j];
    if (fi == FX_VT_NULLV) s->null_seen = 1;
    else if (fi == FX_VT_UNK) s->state = FX_VT_POLY;
    else if (s->state == FX_VT_EMPTY) { s->state = FX_VT_MONO; s->fi = fi; }
    else if (s->state == FX_VT_MONO && s->fi != fi) s->state = FX_VT_POLY;
    if (!t->has_super) return;
    ti = t->super;
  }
}

static int fx_id_cmp(const void *a, const void *b) {
  uint64_t x = ((const uint64_t *)a)[0], y = ((const uint64_t *)b)[0];
  if (x != y) return x < y ? -1 : 1;
  uint64_t p = ((const uint64_t *)a)[1], q = ((const uint64_t *)b)[1];
  return p < q ? -1 : p > q ? 1 : 0;
}

#define FX_VT_CSTACK 64

static void fx_vt_init_scan(const Module *m, const InitExpr *e) {
  enum { CELL_OTHER = 0, CELL_NULL, CELL_FN };
  uint8_t kind[FX_VT_CSTACK];
  uint32_t fn[FX_VT_CSTACK];
  uint32_t sp = 0;
  if (fx_vt_poisoned) return;
  for (uint32_t i = 0; i < e->n; i++) {
    const InitInsn *p = &e->ins[i];
    uint32_t pops = 0;
    uint8_t push = CELL_OTHER;
    uint32_t pushfi = 0;
    switch (p->op) {
      case IOP_I32_CONST: case IOP_I64_CONST: case IOP_F32_CONST:
      case IOP_F64_CONST: case IOP_V128_CONST:
        break;
      case IOP_GLOBAL_GET: {
        uint32_t gi = (uint32_t)p->lo;
        if (gi < m->nglobals && fx_gfunc[gi] != FX_GFUNC_NONE) {
          push = CELL_FN;
          pushfi = fx_gfunc[gi];
        }
        break;
      }
      case IOP_REF_NULL: push = CELL_NULL; break;
      case IOP_REF_FUNC: push = CELL_FN; pushfi = (uint32_t)p->lo; break;
      case IOP_I32_ADD: case IOP_I32_SUB: case IOP_I32_MUL:
      case IOP_I64_ADD: case IOP_I64_SUB: case IOP_I64_MUL:
      case IOP_ARRAY_NEW:
        pops = 2;
        break;
      case IOP_REF_I31: case IOP_REF_CONVERT: case IOP_ARRAY_NEW_DEFAULT:
        pops = 1;
        break;
      case IOP_ARRAY_NEW_FIXED: pops = (uint32_t)p->hi; break;
      case IOP_STRUCT_NEW: case IOP_STRUCT_NEW_DEFAULT: {
        uint32_t ti = (uint32_t)p->lo;
        if (ti >= m->ntypes || m->types[ti].kind != CT_STRUCT) {
          fx_vt_poisoned = true;
          return;
        }
        const CompType *t = &m->types[ti];
        if (p->op == IOP_STRUCT_NEW) {
          if (t->nfields > sp) { fx_vt_poisoned = true; return; }
          sp -= t->nfields;
          for (uint32_t j = 0; j < t->nfields; j++) {
            if (!fx_field_is_funcref(m, t->fields[j].type)) continue;
            uint32_t c = kind[sp + j] == CELL_FN     ? fn[sp + j]
                         : kind[sp + j] == CELL_NULL ? FX_VT_NULLV
                                                     : FX_VT_UNK;
            fx_vt_note(m, ti, j, c);
          }
        } else {
          for (uint32_t j = 0; j < t->nfields; j++)
            if (fx_field_is_funcref(m, t->fields[j].type))
              fx_vt_note(m, ti, j, FX_VT_NULLV);
        }
        break;
      }
      default:
        fx_vt_poisoned = true;
        return;
    }
    if (pops) {
      if (pops > sp) { fx_vt_poisoned = true; return; }
      sp -= pops;
    }
    if (sp >= FX_VT_CSTACK) { fx_vt_poisoned = true; return; }
    kind[sp] = push;
    fn[sp] = pushfi;
    sp++;
  }
}

static void fx_scan_ref_funcs(const Module *m, const uint8_t *p,
                              const uint8_t *end) {
  Rd r = rd_make(p, end, p);
  while (!rd_eof(&r)) {
    uint32_t op = wasm_read_op(&r);
    if (op == 0xd2) {

      /* body bytes carry pre-compaction indices; validation bounds them
         within func_map's original domain */
      uint32_t fi = rd_u32(&r);
      if (m->func_map) fi = m->func_map[fi];
      if (fi < m->nfuncs) fx_ncallers[fi] = 0xffffffffu;
      continue;
    }
    if (!wasm_skip_imm(&r, op)) return;
  }
}

/* Direct call sites per callee, from the call graph the warm phase already
   built. A count is only trustworthy when every caller of the function is
   one of those counted sites, so anything with another way in -- an export,
   the start function, a funcref taken anywhere, a capped caller's untracked
   sites -- reads as unknown rather than as a small number. */
static void fx_count_callers(const Module *m) {
  fx_ncallers = xmalloc((m->nfuncs ? m->nfuncs : 1) * sizeof(uint32_t));
  bool capped = false;
  for (uint32_t fi = 0; fi < m->nfuncs && !capped; fi++)
    capped = fx_direct[fi].callees_capped;
  if (capped) {
    for (uint32_t fi = 0; fi < m->nfuncs; fi++) fx_ncallers[fi] = 0xffffffffu;
    return;
  }
  memset(fx_ncallers, 0, m->nfuncs * sizeof(uint32_t));
  for (uint32_t fi = 0; fi < m->nfuncs; fi++) {
    const DirectFx *d = &fx_direct[fi];
    for (uint32_t k = 0; k < d->ncallees; k++)
      if (d->callees[k] < m->nfuncs && fx_ncallers[d->callees[k]] < 0xfffffffeu)
        fx_ncallers[d->callees[k]]++;
  }
  for (uint32_t i = 0; i < m->nexports; i++)
    if (m->exports[i].kind == EK_FUNC && m->exports[i].index < m->nfuncs)
      fx_ncallers[m->exports[i].index] = 0xffffffffu;
  if (m->has_start && m->start_func < m->nfuncs)
    fx_ncallers[m->start_func] = 0xffffffffu;

  for (uint32_t gi = 0; gi < m->nglobals; gi++)
    for (uint32_t i = 0; i < m->globals[gi].init.n; i++)
      if (m->globals[gi].init.ins[i].op == IOP_REF_FUNC &&
          m->globals[gi].init.ins[i].lo < m->nfuncs)
        fx_ncallers[m->globals[gi].init.ins[i].lo] = 0xffffffffu;
  for (uint32_t t = 0; t < m->ntables; t++)
    for (uint32_t i = 0; i < m->tables[t].init.n; i++)
      if (m->tables[t].init.ins[i].op == IOP_REF_FUNC &&
          m->tables[t].init.ins[i].lo < m->nfuncs)
        fx_ncallers[m->tables[t].init.ins[i].lo] = 0xffffffffu;
  for (uint32_t s = 0; s < m->nelems; s++)
    for (uint32_t k = 0; k < m->elems[s].n; k++)
      for (uint32_t i = 0; i < m->elems[s].items[k].n; i++)
        if (m->elems[s].items[k].ins[i].op == IOP_REF_FUNC &&
            m->elems[s].items[k].ins[i].lo < m->nfuncs)
          fx_ncallers[m->elems[s].items[k].ins[i].lo] = 0xffffffffu;

  for (uint32_t fi = m->nimported_funcs; fi < m->nfuncs; fi++)
    if (m->funcs[fi].body)
      fx_scan_ref_funcs(m, m->funcs[fi].body, m->funcs[fi].body_end);
}

uint32_t opt_direct_callsite_count(const Module *m, uint32_t fi) {
  if (fx_mod != m || !fx_resolved || !fx_ncallers || fi >= m->nfuncs)
    return 0xffffffffu;
  return fx_ncallers[fi];
}

bool opt_call_frame_only(const Module *m, uint32_t fi) {
  if (fx_mod != m || !fx_resolved || fi >= m->nfuncs) return false;
  return fx_func[fi].state == 2 && fx_func[fi].frame_only;
}

/* ---- constant objects ---------------------------------------------------

   An immutable global initialised by struct.new or array.new holds one
   object for the instance's life, and an immutable field or element of that
   object holds what the constant expression put there: a number, a null, a
   function, another global, or an object built inside the same expression.
   A read through such an object folds to it.  Kotlin builds its vtables and
   itables this way, so this is what resolves a virtual or interface call on
   an object whose class is known: the vtable read off an allocation is the
   one it was made with (gc_init_value), and the rest of the dispatch reads
   through that. */

/* The operands a constant-expression instruction takes; UINT32_MAX for one
   the index does not cover. */
static uint32_t fx_cg_pops(const Module *m, const InitInsn *p) {
  switch (p->op) {
    case IOP_I32_CONST: case IOP_I64_CONST: case IOP_F32_CONST:
    case IOP_F64_CONST: case IOP_V128_CONST: case IOP_GLOBAL_GET:
    case IOP_REF_NULL: case IOP_REF_FUNC: case IOP_STRUCT_NEW_DEFAULT:
      return 0;
    case IOP_REF_I31: case IOP_REF_CONVERT: case IOP_ARRAY_NEW_DEFAULT:
      return 1;
    case IOP_I32_ADD: case IOP_I32_SUB: case IOP_I32_MUL:
    case IOP_I64_ADD: case IOP_I64_SUB: case IOP_I64_MUL: case IOP_ARRAY_NEW:
      return 2;
    case IOP_ARRAY_NEW_FIXED: return (uint32_t)p->hi;
    case IOP_STRUCT_NEW:
      return p->lo < m->ntypes && m->types[p->lo].kind == CT_STRUCT
                 ? m->types[p->lo].nfields
                 : UINT32_MAX;
    default: return UINT32_MAX;
  }
}

static void fx_cg_build(const Module *m) {
  uint64_t total = 0;
  uint32_t deepest = 1;
  for (uint32_t gi = 0; gi < m->nglobals; gi++) {
    const Global *g = &m->globals[gi];
    if (g->imported || g->mutable_) continue;
    total += g->init.n;
    if (g->init.n > deepest) deepest = g->init.n;
  }
  fx_cg_at = xmalloc((m->nglobals ? m->nglobals : 1) * sizeof(uint32_t));
  fx_cg_first = xmalloc((total ? total : 1) * sizeof(uint32_t));
  fx_cg_opnd = xmalloc((total ? total : 1) * sizeof(uint32_t));
  uint32_t *stack = xmalloc(deepest * sizeof(uint32_t));
  uint32_t nf = 0, no = 0;
  for (uint32_t gi = 0; gi < m->nglobals; gi++) {
    fx_cg_at[gi] = FX_CG_NONE;
    const Global *g = &m->globals[gi];
    if (g->imported || g->mutable_ || !g->init.n) continue;
    uint32_t sp = 0, no0 = no;
    bool ok = true;
    for (uint32_t i = 0; i < g->init.n; i++) {
      uint32_t pops = fx_cg_pops(m, &g->init.ins[i]);
      if (pops == UINT32_MAX || pops > sp) { ok = false; break; }
      fx_cg_first[nf + i] = no;
      sp -= pops;
      memcpy(&fx_cg_opnd[no], &stack[sp], pops * sizeof(uint32_t));
      no += pops;
      stack[sp++] = i;
    }
    if (!ok || sp != 1) { no = no0; continue; }
    fx_cg_at[gi] = nf;
    nf += g->init.n;
  }
  xfree(stack);
}

static bool cg_indexed(const Module *m, uint32_t g) {
  return fx_cg_at && g < m->nglobals && fx_cg_at[g] != FX_CG_NONE;
}

static const uint32_t *cg_ops(uint32_t g, uint32_t n) {
  return &fx_cg_opnd[fx_cg_first[fx_cg_at[g] + n]];
}

/* Follow an expression node through global.get and conversions to the
   allocation it is. */
static bool cg_object(const Module *m, uint32_t *g, uint32_t *n) {
  for (int d = 0; d < 16; d++) {
    const InitInsn *p = &m->globals[*g].init.ins[*n];
    switch (p->op) {
      case IOP_STRUCT_NEW: case IOP_STRUCT_NEW_DEFAULT: case IOP_ARRAY_NEW:
      case IOP_ARRAY_NEW_DEFAULT: case IOP_ARRAY_NEW_FIXED:
        return true;
      case IOP_REF_CONVERT:
        *n = cg_ops(*g, *n)[0];
        continue;
      case IOP_GLOBAL_GET:
        if (!cg_indexed(m, (uint32_t)p->lo)) return false;
        *g = (uint32_t)p->lo;
        *n = m->globals[*g].init.n - 1;
        continue;
      default:
        return false;
    }
  }
  return false;
}

static bool cg_obj_of(IrFunc *f, Ref r, uint32_t *g, uint32_t *n, int depth);

/* The expression node an immutable field or element read yields when the
   object read is a constant one, or *zero for a default value. */
static bool cg_read(IrFunc *f, Ref v, uint32_t *g, uint32_t *n, bool *zero,
                    int depth) {
  const Module *m = f->m;
  Inst *in = &f->insts[v];
  if ((in->op != I_FIELD_GET && in->op != I_ARRAY_GET) || !in->nargs)
    return false;
  uint32_t og, on;
  if (!cg_obj_of(f, ir_args(f, v)[0], &og, &on, depth + 1)) return false;
  const InitInsn *p = &m->globals[og].init.ins[on];
  if (p->lo >= m->ntypes) return false;
  const CompType *t = &m->types[p->lo];
  const uint32_t *ops = cg_ops(og, on);
  *g = og;
  *zero = false;
  if (in->op == I_FIELD_GET) {
    if (t->kind != CT_STRUCT) return false;
    uint32_t j = 0;
    while (j < t->nfields &&
           (uint64_t)t->hdr_size + t->fields[j].offset != in->imm)
      j++;
    if (j == t->nfields || t->fields[j].mutable_ ||
        vt_is_packed(t->fields[j].type))
      return false;
    if (p->op == IOP_STRUCT_NEW_DEFAULT) *zero = true;
    else *n = ops[j];
    return true;
  }
  if (t->kind != CT_ARRAY || t->fields[0].mutable_ ||
      vt_is_packed(t->fields[0].type))
    return false;
  uint64_t idx;
  if (!is_const(f, ir_args(f, v)[1], &idx)) return false;
  idx &= 0xffffffffu;
  if (p->op == IOP_ARRAY_NEW_FIXED) {
    if (idx >= p->hi) return false;
    *n = ops[idx];
    return true;
  }
  if (p->op != IOP_ARRAY_NEW && p->op != IOP_ARRAY_NEW_DEFAULT) return false;
  const InitInsn *len =
      &m->globals[og].init.ins[ops[p->op == IOP_ARRAY_NEW ? 1 : 0]];
  if (len->op != IOP_I32_CONST || idx >= (uint32_t)len->lo) return false;
  if (p->op == IOP_ARRAY_NEW_DEFAULT) *zero = true;
  else *n = ops[0];
  return true;
}

/* The constant object an IR value is: a global.get of an indexed global,
   or an immutable read off another constant object, through conversions. */
static bool cg_obj_of(IrFunc *f, Ref r, uint32_t *g, uint32_t *n, int depth) {
  const Module *m = f->m;
  if (fx_mod != m || !fx_resolved || !fx_cg_at || depth > 16) return false;
  for (int d = 0; d < 16; d++) {
    r = ir_resolve(f, r);
    Inst *in = &f->insts[r];
    if (in->op == I_REF_CONVERT && in->nargs == 1) {
      r = ir_args(f, r)[0];
      continue;
    }
    if (in->op == I_GLOBAL_GET) {
      if (!cg_indexed(m, (uint32_t)in->imm)) return false;
      *g = (uint32_t)in->imm;
      *n = m->globals[*g].init.n - 1;
      return cg_object(m, g, n);
    }
    bool zero;
    if (!cg_read(f, r, g, n, &zero, depth) || zero) return false;
    return cg_object(m, g, n);
  }
  return false;
}

bool opt_const_object(IrFunc *f, Ref r) {
  uint32_t g, n;
  return cg_obj_of(f, r, &g, &n, 0);
}

/* The exact type of a value that is one known object -- a fresh allocation
   or a constant object, through conversions -- which is never null. */
static bool gc_exact_type(IrFunc *f, Ref r, uint32_t *ti) {
  const Module *m = f->m;
  for (int d = 0; d < 16; d++) {
    r = ir_resolve(f, r);
    Inst *in = &f->insts[r];
    if (in->op == I_REF_CONVERT && in->nargs == 1) {
      r = ir_args(f, r)[0];
      continue;
    }
    if (in->op == I_STRUCT_NEW || in->op == I_ARRAY_NEW) {
      if (in->imm >= m->ntypes) return false;
      *ti = (uint32_t)in->imm;
      return true;
    }
    uint32_t g, n;
    if (!cg_obj_of(f, r, &g, &n, 0)) return false;
    uint64_t t = m->globals[g].init.ins[n].lo;
    if (t >= m->ntypes) return false;
    *ti = (uint32_t)t;
    return true;
  }
  return false;
}

/* The value an immutable field read off a fresh allocation yields: what
   the allocation's initialising store put there, which nothing after it can
   change -- the store follows the allocation in its block, before anything
   can read the object.  A field it does not store is left alone. */
static Ref gc_init_value(IrFunc *f, Ref v) {
  const Module *m = f->m;
  Inst *in = &f->insts[v];
  if (in->op != I_FIELD_GET || !in->nargs) return REF_NONE;
  Ref r = ir_args(f, v)[0];
  for (int d = 0; d < 16; d++) {
    r = ir_resolve(f, r);
    if (f->insts[r].op != I_REF_CONVERT || f->insts[r].nargs != 1) break;
    r = ir_args(f, r)[0];
  }
  Inst *ai = &f->insts[r];
  if (ai->op != I_STRUCT_NEW || ai->imm >= m->ntypes) return REF_NONE;
  const CompType *t = &m->types[ai->imm];
  if (t->kind != CT_STRUCT) return REF_NONE;
  uint32_t j = 0;
  while (j < t->nfields && (uint64_t)t->hdr_size + t->fields[j].offset != in->imm)
    j++;
  if (j == t->nfields || t->fields[j].mutable_ || vt_is_packed(t->fields[j].type))
    return REF_NONE;
  Block *b = &f->blocks[ai->block];
  for (uint32_t i = 0; i < b->ninsts; i++) {
    Ref sv = b->insts[i];
    Inst *st = &f->insts[sv];
    if (st->op == I_FIELD_SET && st->nargs >= 2 && (st->imm2 & GCF_INIT) &&
        st->imm == in->imm && ir_resolve(f, ir_args(f, sv)[0]) == r)
      return ir_resolve(f, ir_args(f, sv)[1]);
  }
  return REF_NONE;
}

/* An access to a known object is never to null, and an element of a
   constant array at a constant index it has is in bounds: a read left
   standing without its checks is one DCE can drop. */
static bool gc_known_checks(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  uint32_t ti;
  if (!in->nargs || !gc_exact_type(f, ir_args(f, v)[0], &ti)) return false;
  uint64_t was = in->imm2;
  in->imm2 &= ~(uint64_t)GCF_NULLCHK;
  uint32_t g, n;
  uint64_t idx;
  if (in->op == I_ARRAY_GET && cg_obj_of(f, ir_args(f, v)[0], &g, &n, 0) &&
      is_const(f, ir_args(f, v)[1], &idx)) {
    const Module *m = f->m;
    const InitInsn *p = &m->globals[g].init.ins[n];
    uint64_t len = UINT64_MAX;
    if (p->op == IOP_ARRAY_NEW_FIXED) {
      len = p->hi;
    } else if (p->op == IOP_ARRAY_NEW || p->op == IOP_ARRAY_NEW_DEFAULT) {
      const InitInsn *l =
          &m->globals[g].init.ins[cg_ops(g, n)[p->op == IOP_ARRAY_NEW ? 1 : 0]];
      if (l->op == IOP_I32_CONST) len = (uint32_t)l->lo;
    }
    if (len != UINT64_MAX && (idx & 0xffffffffu) < len) in->imm2 |= GCF_NOBOUND;
  }
  if (in->imm2 == was) return false;
  g_stats.checks_removed++;
  return true;
}

/* Reads through constant objects and immutable reads off fresh ones, and
   what a known object answers: its tests, its null check and its length. */
static bool gc_fold_known(IrFunc *f, Ref v) {
  const Module *m = f->m;
  Inst *in = &f->insts[v];
  uint32_t ti;
  switch (in->op) {
    case I_FIELD_GET: case I_ARRAY_GET: {
      uint32_t g, n;
      bool zero;
      Ref init = gc_init_value(f, v);
      if (init != REF_NONE) {
        ir_replace(f, v, init);
        in->op = I_NOP;
        in->nargs = 0;
        g_stats.loads_forwarded++;
        return true;
      }
      if (!cg_read(f, v, &g, &n, &zero, 0)) return gc_known_checks(f, v);
      if (zero) {
        if (in->type == VT_V128) return false;
        replace_with_const(f, v, 0);
      } else {
        const InitInsn *p = &m->globals[g].init.ins[n];
        switch (p->op) {
          case IOP_I32_CONST: case IOP_I64_CONST: case IOP_F32_CONST:
          case IOP_F64_CONST:
            replace_with_const(f, v, p->lo);
            break;
          case IOP_REF_NULL:
            replace_with_const(f, v, 0);
            break;
          case IOP_REF_FUNC: case IOP_GLOBAL_GET:
            in->op = p->op == IOP_REF_FUNC ? I_REF_FUNC : I_GLOBAL_GET;
            in->nargs = 0;
            in->imm = p->lo;
            in->imm2 = 0;
            break;
          default:
            return gc_known_checks(f, v);
        }
      }
      g_stats.const_reads++;
      return true;
    }
    case I_ARRAY_LEN: {
      uint32_t g, n;
      if (!in->nargs || !cg_obj_of(f, ir_args(f, v)[0], &g, &n, 0))
        return gc_known_checks(f, v);
      const InitInsn *p = &m->globals[g].init.ins[n];
      uint64_t len;
      if (p->op == IOP_ARRAY_NEW_FIXED) {
        len = p->hi;
      } else if (p->op == IOP_ARRAY_NEW || p->op == IOP_ARRAY_NEW_DEFAULT) {
        const InitInsn *l =
            &m->globals[g].init.ins[cg_ops(g, n)[p->op == IOP_ARRAY_NEW ? 1 : 0]];
        if (l->op != IOP_I32_CONST) return false;
        len = (uint32_t)l->lo;
      } else {
        return false;
      }
      replace_with_const(f, v, len);
      g_stats.const_reads++;
      return true;
    }
    case I_REF_TEST: {
      if (in->nargs != 1 || !gc_exact_type(f, ir_args(f, v)[0], &ti)) return false;
      int r = es_ref_test(m, ti, in->imm, in->imm2);
      if (r < 0) return false;
      replace_with_const(f, v, (uint64_t)r);
      return true;
    }
    case I_REF_IS_NULL:
      if (in->nargs != 1 || !gc_exact_type(f, ir_args(f, v)[0], &ti)) return false;
      replace_with_const(f, v, 0);
      return true;
    case I_TRAP_IF: {
      if (in->nargs != 1) return false;
      uint64_t c;
      bool never = (is_const(f, ir_args(f, v)[0], &c) && c != 0) ||
                   (in->imm == TRAP_NULL_REF &&
                    gc_exact_type(f, ir_args(f, v)[0], &ti));
      if (!never) return false;
      in->op = I_NOP;
      in->nargs = 0;
      return true;
    }
    default:
      return false;
  }
}

static void fx_facts_build(Module *m) {
  fx_count_callers(m);
  fx_cg_build(m);
  fx_gfunc = xmalloc((m->nglobals ? m->nglobals : 1) * sizeof(uint32_t));
  for (uint32_t gi = 0; gi < m->nglobals; gi++) {
    const Global *g = &m->globals[gi];
    fx_gfunc[gi] = !g->imported && !g->mutable_ && g->init.n == 1 &&
                           g->init.ins[0].op == IOP_REF_FUNC
                       ? (uint32_t)g->init.ins[0].lo
                       : FX_GFUNC_NONE;
  }

  fx_vtbase = xmalloc(((uint64_t)m->ntypes + 1) * sizeof(uint32_t));
  uint32_t total = 0;
  for (uint32_t ti = 0; ti < m->ntypes; ti++) {
    fx_vtbase[ti] = total;
    if (m->types[ti].kind == CT_STRUCT) total += m->types[ti].nfields;
  }
  fx_vtbase[m->ntypes] = total;
  fx_vt = xcalloc(total ? total : 1, sizeof(VtSlot));

  for (uint32_t ti = 0; ti < m->ntypes; ti++) {
    const CompType *t = &m->types[ti];
    if (t->kind != CT_STRUCT) continue;
    for (uint32_t j = 0; j < t->nfields; j++)
      if (t->fields[j].mutable_ && fx_field_is_funcref(m, t->fields[j].type))
        fx_vt[fx_vtbase[ti] + j].state = FX_VT_POLY;
  }

  /* Structurally identical struct types are the same wasm type under two
     indices, and values cross between them freely, so slots keyed by index
     cannot be trusted for either: poison both. */
  if (m->ntypes > 1) {
    uint64_t (*ids)[2] = xmalloc((uint64_t)m->ntypes * sizeof(*ids));
    uint32_t n = 0;
    for (uint32_t ti = 0; ti < m->ntypes; ti++)
      if (m->types[ti].kind == CT_STRUCT && m->types[ti].nfields) {
        ids[n][0] = m->types[ti].id;
        ids[n][1] = ti;
        n++;
      }
    qsort(ids, n, sizeof(*ids), fx_id_cmp);
    for (uint32_t i = 0; i < n; i++) {
      bool dup = (i > 0 && ids[i][0] == ids[i - 1][0]) ||
                 (i + 1 < n && ids[i][0] == ids[i + 1][0]);
      if (!dup) continue;
      uint32_t ti = (uint32_t)ids[i][1];
      const CompType *t = &m->types[ti];
      for (uint32_t j = 0; j < t->nfields; j++)
        if (fx_field_is_funcref(m, t->fields[j].type))
          fx_vt[fx_vtbase[ti] + j].state = FX_VT_POLY;
    }
    xfree(ids);
  }

  for (uint32_t gi = 0; gi < m->nglobals; gi++)
    if (!m->globals[gi].imported) fx_vt_init_scan(m, &m->globals[gi].init);
  for (uint32_t i = 0; i < m->ntables; i++)
    if (m->tables[i].has_init) fx_vt_init_scan(m, &m->tables[i].init);
  for (uint32_t s = 0; s < m->nelems; s++) {
    fx_vt_init_scan(m, &m->elems[s].offset);
    for (uint32_t k = 0; k < m->elems[s].n; k++)
      fx_vt_init_scan(m, &m->elems[s].items[k]);
  }
  for (uint32_t s = 0; s < m->ndatas; s++)
    fx_vt_init_scan(m, &m->datas[s].offset);

  for (uint32_t fi = 0; fi < m->nfuncs; fi++) {
    DirectFx *d = &fx_direct[fi];
    for (uint32_t k = 0; k < d->nvtst; k++)
      fx_vt_note(m, d->vtst[k].ti, d->vtst[k].fld, d->vtst[k].fi);
    xfree(d->vtst);
    d->vtst = NULL;
    d->nvtst = d->capvtst = 0;
  }

  if (warmc_getenv("WARMC_DEVIRT_DEBUG")) {
    uint32_t mono = 0, poly = 0, nulls = 0;
    for (uint32_t i = 0; i < total; i++) {
      if (fx_vt[i].state == FX_VT_MONO) mono++;
      if (fx_vt[i].state == FX_VT_POLY) poly++;
      if (fx_vt[i].null_seen) nulls++;
    }
    uint32_t ng = 0;
    for (uint32_t gi = 0; gi < m->nglobals; gi++)
      if (fx_gfunc[gi] != FX_GFUNC_NONE) ng++;
    fprintf(stderr,
            "devirt facts: table0 frozen=%d entries=%u gfunc=%u vt slots=%u "
            "mono=%u poly=%u null_seen=%u poisoned=%d\n",
            (int)fx_table0.frozen, fx_table0.entry ? fx_table0.nentries : 0,
            ng, total, mono, poly, nulls, (int)fx_vt_poisoned);
  }
}

static const CallFx *indirect_effects(Module *m, const OptOpts *o) {
  fx_need(m, o);
  if (fx_indirect_done) return &fx_indirect;
  fx_indirect_done = 1;
  fx_indirect = fx_unknown;

  fx_table_freeze(m);
  if (!fx_table0.frozen) return &fx_indirect;

  CallFx meet = { .state = 2, .frame_only = true,
                  .frame_global = 0xffffffffu, .lo = 0, .hi = -FRAME_SPAN };
  for (uint32_t s = 0; s < m->nelems; s++) {
    ElemSeg *e = &m->elems[s];
    if (e->mode != SEG_ACTIVE) continue;
    for (uint32_t k = 0; k < e->n; k++) {
      const InitExpr *it = &e->items[k];
      if (it->ins[0].op == IOP_REF_NULL) continue;
      fx_meet(&meet, call_effects(m, o, (uint32_t)it->ins[0].lo, 0));
    }
  }
  if (fx_table0.init_fi != FX_TBL_NULL)
    fx_meet(&meet, call_effects(m, o, fx_table0.init_fi, 0));
  if (meet.frame_global == 0xffffffffu) meet.frame_global = 0;
  if (!meet.writes_mem) { meet.lo = 0; meet.hi = 0; }
  meet.frame_only = meet.frame_only && !meet.writes_globals;
  fx_indirect = meet;
  return &fx_indirect;
}

static const CallFx *site_effects(IrFunc *f, const OptOpts *o, Ref v) {
  Inst *in = &f->insts[v];
  if (in->op == I_CALL || in->op == I_RETURN_CALL)
    return call_effects(f->m, o, (uint32_t)in->imm, 0);
  if (in->op == I_CALL_INDIRECT || in->op == I_RETURN_CALL_INDIRECT ||
      in->op == I_CALL_REF || in->op == I_RETURN_CALL_REF)
    return indirect_effects(f->m, o);
  return &fx_unknown;
}

#define DV_RUNG_REF      1u
#define DV_RUNG_INDIRECT 2u
#define DV_RUNG_VT       4u

static pthread_once_t g_dv_once = PTHREAD_ONCE_INIT;
static struct {
  bool off, dbg;
  uint32_t rungs;
  long only, upto;
} g_dv;

static void dv_init(void) {
  g_dv.off = warmc_getenv("WARMC_NO_DEVIRT") != NULL;
  g_dv.dbg = warmc_getenv("WARMC_DEVIRT_DEBUG") != NULL;
  const char *e = warmc_getenv("WARMC_DEVIRT_RUNGS");
  g_dv.rungs = e ? (uint32_t)strtoul(e, NULL, 0) : 0xffffffffu;
  e = warmc_getenv("WARMC_DEVIRT_ONLY");
  g_dv.only = e ? atol(e) : -1;
  e = warmc_getenv("WARMC_DEVIRT_UPTO");
  g_dv.upto = e ? atol(e) : -1;
}

/* The declared site type and the target's type may differ under function
   subtyping; the rewrite is only sound when nothing about the call ABI
   does. */
static bool devirt_abi_ok(const Module *m, uint32_t site_ti, uint32_t fi) {
  if (site_ti >= m->ntypes || fi >= m->nfuncs) return false;
  const CompType *a = &m->types[site_ti];
  const CompType *b = &m->types[m->funcs[fi].type_index];
  if (a->kind != CT_FUNC || b->kind != CT_FUNC) return false;
  if (a->nparams != b->nparams || a->nresults != b->nresults) return false;
  for (uint32_t i = 0; i < a->nparams; i++)
    if (vt_abi_code(a->params[i]) != vt_abi_code(b->params[i])) return false;
  for (uint32_t i = 0; i < a->nresults; i++)
    if (vt_abi_code(a->results[i]) != vt_abi_code(b->results[i])) return false;
  return true;
}

/* The function pass_devirt makes a call_indirect of type site_ti through
   table 0 at constant index idx into, or UINT32_MAX: for the inliner,
   which counts a callee's indirect calls on a parameter as direct ones at a
   site that passes that parameter a constant. */
uint32_t opt_table0_target(const Module *m, uint32_t site_ti, uint64_t idx) {
  pthread_once(&g_dv_once, dv_init);
  if (g_dv.off || !(g_dv.rungs & DV_RUNG_INDIRECT)) return UINT32_MAX;
  if (fx_mod != m || !fx_resolved || !fx_table0.computed || !fx_table0.frozen ||
      !fx_table0.entry || idx >= fx_table0.nentries || site_ti >= m->ntypes)
    return UINT32_MAX;
  uint32_t fi = fx_table0.entry[idx];
  if (fi == FX_TBL_NULL) return UINT32_MAX;
  if (m->types[site_ti].id != m->types[m->funcs[fi].type_index].id ||
      !devirt_abi_ok(m, site_ti, fi))
    return UINT32_MAX;
  return fi;
}

/* Follow the funcref operand of a call_ref to a compile-time-known target:
   ref.func directly, global.get of an immutable ref.func global, a read
   through a constant object, or a load from a monomorphic vtable slot,
   looking through ref casts, which pass the value unchanged. The first
   three provenances are non-null by construction;
   a vtable slot is non-null only when no creation site ever left it null.
   The slot rung needs the closed world: another instance's structs carry
   another instance's funcref cells, and a direct call would pass this one's
   vmctx. */
static uint32_t devirt_ref_target(IrFunc *f, const OptOpts *o, Ref callee,
                                  bool *keep_null) {
  *keep_null = false;
  Ref r = ir_resolve(f, callee);
  for (int depth = 0; depth < 8; depth++) {
    Inst *in = &f->insts[r];
    if (in->op == I_REF_CONVERT) {
      r = ir_resolve(f, ir_args(f, r)[0]);
      continue;
    }
    if (in->op == I_REF_FUNC) return (uint32_t)in->imm;
    if (in->op == I_GLOBAL_GET) {
      uint32_t gi = (uint32_t)in->imm;
      return gi < f->m->nglobals ? fx_gfunc[gi] : FX_GFUNC_NONE;
    }
    uint32_t cg, cn;
    bool zero;
    if (cg_read(f, r, &cg, &cn, &zero, 0) && !zero) {
      const InitInsn *p = &f->m->globals[cg].init.ins[cn];
      if (p->op == IOP_REF_FUNC) return (uint32_t)p->lo;
      if (p->op == IOP_GLOBAL_GET && p->lo < f->m->nglobals)
        return fx_gfunc[p->lo];
      return FX_GFUNC_NONE;
    }
    if (in->op == I_FIELD_GET && (g_dv.rungs & DV_RUNG_VT) &&
        o->closed_world && !fx_vt_poisoned && fx_vt) {
      const Module *m = f->m;
      ValType rt = (ValType)f->insts[ir_resolve(f, ir_args(f, r)[0])].type;
      if (!vt_is_ref(rt) || !vt_concrete(rt)) return FX_GFUNC_NONE;
      uint32_t ti = vt_type_index(rt);
      if (ti >= m->ntypes || m->types[ti].kind != CT_STRUCT)
        return FX_GFUNC_NONE;
      const CompType *t = &m->types[ti];
      for (uint32_t j = 0; j < t->nfields; j++)
        if ((uint64_t)t->hdr_size + t->fields[j].offset == in->imm) {
          const VtSlot *s = &fx_vt[fx_vtbase[ti] + j];
          if (s->state != FX_VT_MONO) return FX_GFUNC_NONE;
          *keep_null = s->null_seen != 0;
          return s->fi;
        }
      return FX_GFUNC_NONE;
    }
    return FX_GFUNC_NONE;
  }
  return FX_GFUNC_NONE;
}

/* ref.as_non_null's shape, placed just before the call so the call stays
   the last instruction of a protected block. */
static void devirt_nullcheck_before(IrFunc *f, uint32_t bi, uint32_t at,
                                    Ref callee) {
  ir_emit(f, bi, I_TRAP_IF, VT_BOT, &callee, 1, TRAP_NULL_REF, 0);
  Block *b = &f->blocks[bi];
  uint32_t ni = b->insts[b->ninsts - 1];
  memmove(&b->insts[at + 1], &b->insts[at],
          (b->ninsts - 1 - at) * sizeof(uint32_t));
  b->insts[at] = ni;
}

static void devirt_rewrite(IrFunc *f, Ref v, uint32_t fi, bool tail) {
  Inst *in = &f->insts[v];
  if (g_dv.dbg)
    fprintf(stderr, "devirt %s: %s site v%u -> %s\n",
            f->m->funcs[f->func_index].sym ? f->m->funcs[f->func_index].sym : "?",
            in->op == I_CALL_REF || in->op == I_RETURN_CALL_REF ? "call_ref"
                                                                : "call_indirect",
            v, f->m->funcs[fi].sym ? f->m->funcs[fi].sym : "?");
  in->op = tail ? I_RETURN_CALL : I_CALL;
  in->imm = fi;
  in->imm2 = 0;
  in->nargs -= 1;
  in->flags |= IRF_DEVIRT;
}

static bool pass_devirt(IrFunc *f, const OptOpts *o) {
  (void)o;
  pthread_once(&g_dv_once, dv_init);
  if (g_dv.off) return false;
  if (g_dv.only >= 0 && f->func_index != (uint32_t)g_dv.only) return false;
  if (g_dv.upto >= 0 && f->func_index > (uint32_t)g_dv.upto) return false;
  Module *m = f->m;
  if (fx_mod != m || !fx_resolved) return false;

  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t k = 0; k < b->ninsts; k++) {
      Ref v = b->insts[k];
      Inst *in = &f->insts[v];
      uint16_t op = in->op;

      /* An immutable global initialized with ref.func holds exactly what
         I_REF_FUNC computes -- this instance's cell address -- and the
         rewrite is pure where the global load is not, so it can be hoisted
         and merged. */
      if (op == I_GLOBAL_GET && (g_dv.rungs & DV_RUNG_REF) && fx_gfunc) {
        uint32_t gi = (uint32_t)in->imm;
        if (gi < m->nglobals && fx_gfunc[gi] != FX_GFUNC_NONE) {
          in->op = I_REF_FUNC;
          in->imm = fx_gfunc[gi];
          in->imm2 = 0;
          changed = true;
          continue;
        }
      }

      if ((op == I_CALL_REF || op == I_RETURN_CALL_REF) &&
          (g_dv.rungs & DV_RUNG_REF)) {
        Ref callee = ir_args(f, v)[in->nargs - 1];
        bool keep_null = false;
        uint32_t fi = devirt_ref_target(f, o, callee, &keep_null);
        if (fi == FX_GFUNC_NONE) continue;
        if (!devirt_abi_ok(m, (uint32_t)in->imm, fi)) continue;
        if (keep_null) {
          devirt_nullcheck_before(f, f->order[oi], k, callee);
          g_stats.devirt_null_kept++;
          k++;
        }
        devirt_rewrite(f, v, fi, op == I_RETURN_CALL_REF);
        g_stats.devirt_ref++;
        changed = true;
        continue;
      }

      if ((op == I_CALL_INDIRECT || op == I_RETURN_CALL_INDIRECT) &&
          (g_dv.rungs & DV_RUNG_INDIRECT)) {
        if (!fx_table0.frozen || !fx_table0.entry) continue;
        if ((uint32_t)in->imm2 != 0) continue;
        Inst *ix = &f->insts[ir_resolve(f, ir_args(f, v)[in->nargs - 1])];
        if (ix->op != I_CONST) continue;
        uint64_t idx = ix->imm;
        if (idx >= fx_table0.nentries) continue;
        uint32_t fi = fx_table0.entry[idx];
        if (fi == FX_TBL_NULL) continue;

        /* the same structural id the runtime signature check compares */
        if (m->types[(uint32_t)in->imm].id !=
            m->types[m->funcs[fi].type_index].id)
          continue;
        if (!devirt_abi_ok(m, (uint32_t)in->imm, fi)) continue;
        devirt_rewrite(f, v, fi, op == I_RETURN_CALL_INDIRECT);
        g_stats.devirt_indirect++;
        changed = true;
        continue;
      }
    }
  }
  return changed;
}

#define SP_MAX_SITES 64
#define SP_ANY_GLOBAL 0xffffffffu

typedef struct {
  IrFunc *f;
  bool built, overflow;
  uint32_t nsite;
  uint32_t site_block[SP_MAX_SITES];
  uint32_t site_idx[SP_MAX_SITES];
  uint32_t site_glob[SP_MAX_SITES];
  Ref site_val[SP_MAX_SITES];

  uint32_t get_block[MAX_TRACKED_GLOBALS];
  uint32_t get_idx[MAX_TRACKED_GLOBALS];
  Ref get_ref[MAX_TRACKED_GLOBALS];
  uint8_t *reach;
  uint32_t reach_for;
  uint32_t *stack;
} SpInfo;

static void sp_reset(SpInfo *si, IrFunc *f, uint8_t *reach, uint32_t *stack) {
  si->f = f;
  si->built = false;
  si->reach = reach;
  si->stack = stack;
  si->reach_for = REF_NONE;
}

static void sp_build(SpInfo *si, const OptOpts *o) {
  IrFunc *f = si->f;
  si->built = true;
  si->overflow = false;
  si->nsite = 0;
  for (uint32_t g = 0; g < MAX_TRACKED_GLOBALS; g++) si->get_ref[g] = REF_NONE;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op == I_GLOBAL_GET) {
        uint32_t g = (uint32_t)in->imm;
        if (g < MAX_TRACKED_GLOBALS && si->get_ref[g] == REF_NONE) {
          si->get_ref[g] = v;
          si->get_block[g] = bi;
          si->get_idx[g] = i;
        }
        continue;
      }
      uint32_t g;
      Ref val;
      if (in->op == I_GLOBAL_SET) { g = (uint32_t)in->imm; val = ir_args(f, v)[0]; }
      else if (is_call_op(in->op) &&
               site_effects(f, o, v)->writes_globals) { g = SP_ANY_GLOBAL; val = REF_NONE; }
      else continue;
      if (si->nsite == SP_MAX_SITES) { si->overflow = true; return; }
      si->site_block[si->nsite] = bi;
      si->site_idx[si->nsite] = i;
      si->site_glob[si->nsite] = g;
      si->site_val[si->nsite] = val;
      si->nsite++;
    }
  }
}

static void sp_reach(SpInfo *si, uint32_t bi) {
  if (si->reach_for == bi) return;
  IrFunc *f = si->f;
  memset(si->reach, 0, f->nblocks);
  uint32_t sp = 0;
  for (uint32_t p = 0; p < f->blocks[bi].npreds; p++) {
    uint32_t pb = f->blocks[bi].preds[p];
    if (!si->reach[pb]) { si->reach[pb] = 1; si->stack[sp++] = pb; }
  }
  while (sp) {
    uint32_t x = si->stack[--sp];
    Block *b = &f->blocks[x];
    for (uint32_t p = 0; p < b->npreds; p++)
      if (!si->reach[b->preds[p]]) {
        si->reach[b->preds[p]] = 1;
        si->stack[sp++] = b->preds[p];
      }
  }
  si->reach_for = bi;
}

static Ref global_value_at(SpInfo *si, const OptOpts *o, uint32_t bi,
                           uint32_t idx, uint32_t g) {
  IrFunc *f = si->f;
  if (!si->built) sp_build(si, o);
  if (si->overflow || g >= MAX_TRACKED_GLOBALS) return REF_NONE;
  sp_reach(si, bi);

  Ref found = REF_NONE;
  uint32_t nclobber = 0;
  for (uint32_t k = 0; k < si->nsite; k++) {
    uint32_t ob = si->site_block[k], i = si->site_idx[k];
    if (si->site_glob[k] != SP_ANY_GLOBAL && si->site_glob[k] != g) continue;
    bool before = (ob == bi) ? (i < idx) : (si->reach[ob] != 0);
    if (!before && !si->reach[ob]) continue;
    if (++nclobber > 1) return REF_NONE;
    if (si->site_val[k] == REF_NONE) return REF_NONE;
    if (!dominates(f, ob, bi)) return REF_NONE;
    if (ob == bi && i >= idx) return REF_NONE;
    found = si->site_val[k];
  }
  if (nclobber) return found;

  Ref gr = si->get_ref[g];
  if (gr == REF_NONE) return REF_NONE;
  uint32_t gb = si->get_block[g], gi = si->get_idx[g];
  bool before = (gb == bi) ? (gi < idx) : (si->reach[gb] != 0);
  if (!before || !dominates(f, gb, bi)) return REF_NONE;
  return gr;
}

static void edge_push_arg_at(IrFunc *f, uint32_t from, uint32_t si, Ref v) {
  Edge *e = &f->blocks[from].succs[si];
  if (e->nargs == e->cap) {
    uint32_t oc_ = e->cap;
    e->cap = e->cap ? e->cap * 2 : 4;
    e->args = ir_grow(f, e->args, (size_t)oc_ * sizeof(Ref),
                        (size_t)e->cap * sizeof(Ref));
  }
  e->args[e->nargs++] = v;
}

static void edge_push_arg(IrFunc *f, uint32_t from, uint32_t to, Ref v) {
  Block *b = &f->blocks[from];
  for (uint32_t s = 0; s < b->nsuccs; s++)
    if (b->succs[s].target == to) edge_push_arg_at(f, from, s, v);
}

static Ref materialise_param(IrFunc *f, MemState *st, const MemEnt *e) {
  uint32_t ob = e->origin;
  Ref vals[MEMOPT_PREDS];
  uint32_t srcs[MEMOPT_PREDS];
  uint32_t n = 0;
  for (uint32_t x = 0; x < f->nblocks; x++) {
    Block *xb = &f->blocks[x];
    if (!xb->reachable) continue;
    bool targets = false;
    for (uint32_t s = 0; s < xb->nsuccs && !targets; s++)
      if (xb->succs[s].target == ob) targets = true;
    if (!targets) continue;
    if (n == MEMOPT_PREDS || !st[x].valid) return REF_NONE;
    Ref got = REF_NONE;
    for (uint32_t k = 0; k < st[x].nent; k++) {
      const MemEnt *pe = &st[x].ent[k];
      if (!pe->valid || pe->pending) continue;
      MemRan ran = { st[x].ent, st[x].nent, st[x].pend, st[x].npend };
      if (!same_access_after(f, pe->addr, pe->off, pe->size, pe->mem,
                             e->addr, e->off, e->size, e->mem, &ran))
        continue;
      if (f->insts[pe->value].type == e->vtype && pe->sgn == e->sgn &&
          pe->low == e->low)
        got = pe->value;
      break;
    }
    if (got == REF_NONE) return REF_NONE;
    srcs[n] = x;
    vals[n] = got;
    n++;
  }
  if (!n) return REF_NONE;
  Ref p = ir_add_param(f, ob, (ValType)e->vtype);
  for (uint32_t i = 0; i < n; i++) edge_push_arg(f, srcs[i], ob, vals[i]);
  return p;
}

static bool clear_of_frame(int64_t rel, uint32_t size, int64_t lo, int64_t hi) {
  if (rel > FRAME_SPAN || rel < -FRAME_SPAN) return false;
  return rel >= hi || rel + (int64_t)size <= lo;
}

/* What a load of (lt, size, sgn) reads from a slot an entry says holds
   e->value: the same bits under another type, as Go's math.Float64bits and
   Float32frombits leave them -- a float stored and its bits loaded back, or
   the other way round.  1 when the entry's value is the load's, 2 when a
   conversion makes it, 0 when neither. */
static int mem_pun(const IrFunc *f, const MemEnt *e, ValType lt, uint32_t size,
                   bool sgn) {
  ValType et = e->pending ? (ValType)e->vtype : (ValType)f->insts[e->value].type;
  if (et == lt && e->sgn == sgn && !e->low) return 1;
  if (e->pending) return 0;
  if (size == 3)
    return (et == VT_I64 && lt == VT_F64) || (et == VT_F64 && lt == VT_I64) ? 2 : 0;
  if (size != 2) return 0;
  if (et == VT_I64) return lt == VT_I32 || lt == VT_F32 || lt == VT_I64 ? 2 : 0;
  if (et == VT_I32 || et == VT_F32)
    return lt == VT_I32 || lt == VT_F32 || lt == VT_I64 ? 2 : 0;
  return 0;
}

static Ref mem_pun_emit(IrFunc *f, uint32_t bi, uint32_t *idx, Ref x, ValType lt,
                        uint32_t size, bool sgn) {
  ValType et = (ValType)f->insts[x].type;
  if (size == 3) return emit_before(f, bi, idx, I_REINTERPRET, lt, &x, 1, et);
  if (et == VT_I64) { x = emit_before(f, bi, idx, I_WRAP, VT_I32, &x, 1, 0); et = VT_I32; }
  if (et == VT_F32 && lt != VT_F32) { x = emit_before(f, bi, idx, I_REINTERPRET, VT_I32, &x, 1, VT_F32); et = VT_I32; }
  if (lt == VT_F32 && et == VT_I32) return emit_before(f, bi, idx, I_REINTERPRET, VT_F32, &x, 1, VT_I32);
  if (lt == VT_I64) return emit_before(f, bi, idx, sgn ? I_EXTENDS : I_EXTENDU, VT_I64, &x, 1, 0);
  return x;
}

/* An entry made invalid keeps its slot until the table fills; the slots then
   go to the accesses still to come. */
static uint32_t memopt_compact(MemEnt *ent, uint32_t nent) {
  uint32_t kept = 0;
  for (uint32_t k = 0; k < nent; k++)
    if (ent[k].valid) ent[kept++] = ent[k];
  return kept;
}

/* What may lie between two writes of a global for the first to be dead:
   arithmetic, which neither reads the global nor can trap -- a trap would
   leave the first value for whoever looks at the instance after it. */
static bool gdse_transparent(uint16_t op) {
  switch (op) {
    case I_CONST: case I_NOP:
    case I_ADD: case I_SUB: case I_MUL: case I_MULHU:
    case I_AND: case I_OR: case I_XOR:
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTL: case I_ROTR:
    case I_CLZ: case I_CTZ: case I_POPCNT: case I_BSWAP:
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_WRAP: case I_EXTENDS: case I_EXTENDU:
    case I_EXT8S: case I_EXT16S: case I_EXT32S:
    case I_SELECT: case I_MEM_ADDR: case I_MEM_PTR: case I_MEM_BASE:
      return true;
    default:
      return false;
  }
}

/* A global.set that another of the same global follows in its block, with
   nothing between that reads the global, calls or can trap, writes a value
   nobody sees.  Go's return path pops its frame and then the return
   address, two stores of __stack_pointer one after the other with an add
   between (stdgo: some 360 sites a binary), wherever global_ssa leaves
   that global in memory because promoting it does not pay. */
static bool global_dse(IrFunc *f) {
  bool changed = false;
  Ref last[MAX_TRACKED_GLOBALS];
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    uint64_t live = 0;   /* bit g: last[g] is a write nothing has seen yet */
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      uint32_t g = (uint32_t)in->imm;
      if (in->op == I_GLOBAL_SET && g < MAX_TRACKED_GLOBALS) {
        if ((live >> g) & 1) {
          f->insts[last[g]].op = I_NOP;
          g_stats.stores_removed++;
          changed = true;
        }
        last[g] = v;
        live |= (uint64_t)1 << g;
      } else if (in->op == I_GLOBAL_GET) {
        if (g < MAX_TRACKED_GLOBALS) live &= ~((uint64_t)1 << g);
      } else if (!gdse_transparent(in->op)) {
        live = 0;
      }
    }
  }
  return changed;
}

static bool pass_memopt(IrFunc *f, const OptOpts *o) {
  bool changed = false;
  MemEnt ent[MEMOPT_ENTS];
  PendStore pend[MEMOPT_ENTS];
  uint32_t npend = 0;
  Ref gval[MAX_TRACKED_GLOBALS];
  uint64_t gok;   /* bit g: gval[g] is what global g holds */
  MemState *st = xcalloc(f->nblocks ? f->nblocks : 1, sizeof(MemState));
  /* Sweeps go on until no block's end state moves, then one more applies
     what they found.  Without a back edge each block's predecessors all
     come before it in the order, so the first sweep already sees their
     final states: the sweep that would only confirm them and the one that
     would then apply them are the first one.  Most functions have no loop,
     and memopt was a ninth of all compile time. */
  bool apply = true;
  for (uint32_t oi = 0; oi < f->norder && apply; oi++) {
    const Block *b = &f->blocks[f->order[oi]];
    for (uint32_t p = 0; p < b->npreds; p++)
      if (f->blocks[b->preds[p]].reachable && f->blocks[b->preds[p]].rpo >= b->rpo) {
        apply = false;
        break;
      }
  }
  /* After the first sweep a block's end state depends on its
     predecessors' alone, so a sweep that only looks redoes just the blocks
     some predecessor of which ended differently since they last ran: the
     states, the number of sweeps and so what is applied are the same.  Go's
     functions are all inside the dispatch loop, and in compiling
     stdgotests/webtext 94 of every 100 blocks a later sweep came to were
     left as they were. */
  bool *redo = NULL;
  if (!apply) {
    redo = xmalloc(f->nblocks ? f->nblocks : 1);
    memset(redo, 1, f->nblocks);
  }
  for (int sweep = 0; ; sweep++) {
    bool moved = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (!apply) {
      if (!redo[bi]) continue;
      redo[bi] = false;
    }
    uint32_t nent = 0;
    npend = 0;

    gok = 0;

    if (b->is_pad) {  }
    else if (b->npreds == 1 && st[b->preds[0]].valid &&
        f->blocks[b->preds[0]].rpo < b->rpo) {
      MemState *p = &st[b->preds[0]];
      if (p->nent) memcpy(ent, p->ent, p->nent * sizeof(MemEnt));
      nent = p->nent;
      if (p->gok) memcpy(gval, p->gval, sizeof gval);
      gok = p->gok;

      Block *pb = &f->blocks[b->preds[0]];
      if (pb->nsuccs == 1 && pb->term == T_JUMP) {
        if (p->npend) memcpy(pend, p->pend, p->npend * sizeof(PendStore));
        npend = p->npend;
      }
    } else if (b->npreds > 1) {

      bool ready = true;
      for (uint32_t p = 0; p < b->npreds && ready; p++) {
        Block *pb = &f->blocks[b->preds[p]];
        if (!pb->reachable) continue;
        if (!st[b->preds[p]].valid) ready = false;
        else if (pb->rpo >= b->rpo && sweep == 0) ready = false;
      }
      if (ready) {
        MemState *first = &st[b->preds[0]];
        for (uint32_t k = 0; k < first->nent && nent < MEMOPT_ENTS; k++) {
          if (!first->ent[k].valid || first->ent[k].pending) continue;
          MemEnt cand = first->ent[k];
          bool all = true, agree = true;
          for (uint32_t p = 1; p < b->npreds && all; p++) {
            MemState *ps = &st[b->preds[p]];
            bool found = false;
            for (uint32_t j = 0; j < ps->nent; j++) {
              if (!ps->ent[j].valid || ps->ent[j].pending) continue;
              if (!same_access(f, ps->ent[j].addr, ps->ent[j].off, ps->ent[j].size,
                               ps->ent[j].mem,
                               cand.addr, cand.off, cand.size, cand.mem)) continue;
              found = f->insts[ps->ent[j].value].type == f->insts[cand.value].type &&
                      ps->ent[j].sgn == cand.sgn && ps->ent[j].low == cand.low;
              if (found && ps->ent[j].value != cand.value) agree = false;
              break;
            }
            all = found;
          }
          if (!all) continue;
          if (agree) { ent[nent++] = cand; continue; }

          if (!apply) continue;
          cand.pending = true;
          cand.origin = bi;
          cand.vtype = f->insts[cand.value].type;
          cand.value = REF_NONE;
          ent[nent++] = cand;
        }
      }

      /* a global every reachable, settled predecessor holds, with one
         value among them all */
      const MemState *base = NULL;
      uint64_t held = ~(uint64_t)0;
      for (uint32_t p = 0; p < b->npreds; p++) {
        const MemState *ps = &st[b->preds[p]];
        if (!f->blocks[b->preds[p]].reachable || !ps->valid) continue;
        if (!base) base = ps;
        held &= ps->gok;
      }
      if (base && held) {
        for (uint32_t p = 0; p < b->npreds; p++) {
          const MemState *ps = &st[b->preds[p]];
          if (!f->blocks[b->preds[p]].reachable || !ps->valid || ps == base) continue;
          for (uint64_t m = held; m; m &= m - 1) {
            uint32_t g = (uint32_t)__builtin_ctzll(m);
            if (ps->gval[g] != base->gval[g]) held &= ~((uint64_t)1 << g);
          }
        }
        for (uint64_t m = held; m; m &= m - 1) {
          uint32_t g = (uint32_t)__builtin_ctzll(m);
          gval[g] = base->gval[g];
        }
        gok |= held;
      }
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      Ref *a = ir_args(f, v);
      MemRan ran = { ent, nent, pend, npend };
      if (in->op == I_LOAD) {
        uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
        uint32_t mem = MEMF_MEM(in->imm2);
        bool sgn = (in->imm2 & MEMF_SIGNED) != 0;
        bool have = false;

        for (uint32_t k = 0; k < npend;) {
          if (may_alias(f, pend[k].addr, pend[k].off, pend[k].size, pend[k].mem,
                        a[0], in->imm, size, mem))
            pend[k] = pend[--npend];
          else k++;
        }
        for (uint32_t k = 0; k < nent; k++) {
          if (!ent[k].valid ||
              !same_access_after(f, ent[k].addr, ent[k].off, ent[k].size, ent[k].mem,
                           a[0], in->imm, size, mem, &ran))
            continue;

          int pun = mem_pun(f, &ent[k], (ValType)in->type, size, sgn);
          if (!pun) continue;
          if (ent[k].pending) {
            Ref p = materialise_param(f, st, &ent[k]);
            if (p == REF_NONE) { ent[k].valid = false; continue; }
            ent[k].pending = false;
            ent[k].value = p;
            in = &f->insts[v];
            a = ir_args(f, v);
          }
          if (apply) {
            Ref nv = ent[k].value;
            if (pun == 2) nv = mem_pun_emit(f, bi, &i, nv, (ValType)in->type, size, sgn);
            in = &f->insts[v];
            ir_replace(f, v, nv);
            in->op = I_NOP;
            g_stats.loads_forwarded++;
            changed = true;
          }
          have = true;
          break;
        }
        if (!have && nent == MEMOPT_ENTS) nent = memopt_compact(ent, nent);
        if (!have && nent < MEMOPT_ENTS) {
          ent[nent++] = (MemEnt){ a[0], in->imm, size, mem, v, sgn, true, false, 0, 0, false };
        }
      } else if (in->op == I_STORE) {
        uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
        uint32_t mem = MEMF_MEM(in->imm2);

        /* A store of what the slot is known to hold writes nothing new, as
           GHC's code does when it puts back a register it loaded; an access
           of the same size there came first, so it traps where that did. */
        bool holds = false;
        for (uint32_t k = 0; k < nent && !holds; k++)
          holds = ent[k].valid && !ent[k].pending && ent[k].value == a[1] &&
                  same_access_after(f, ent[k].addr, ent[k].off, ent[k].size, ent[k].mem,
                              a[0], in->imm, size, mem, &ran);
        if (holds) {
          if (apply) {
            in->op = I_NOP;
            g_stats.stores_removed++;
            changed = true;
          }
          continue;
        }

        for (uint32_t k = 0; k < npend; k++) {
          if (!same_access_after(f, pend[k].addr, pend[k].off, pend[k].size, pend[k].mem,
                           a[0], in->imm, size, mem, &ran))
            continue;
          if (apply) {
            f->insts[pend[k].inst].op = I_NOP;
            g_stats.stores_removed++;
            changed = true;
          }
          pend[k] = pend[--npend];
          break;
        }
        if (npend < MEMOPT_ENTS) pend[npend++] = (PendStore){ a[0], in->imm, size, mem, v };
        for (uint32_t k = 0; k < nent; k++)
          if (may_alias(f, ent[k].addr, ent[k].off, ent[k].size, ent[k].mem,
                        a[0], in->imm, size, mem))
            ent[k].valid = false;

        uint32_t vw = vt_width((ValType)f->insts[a[1]].type);
        uint32_t aw = 1u << size;
        bool whole = aw == vw;
        if (!whole && aw < 8 &&
            (ir_known_bits(f, a[1], KB_DEPTH) >> (8u * aw)) == 0)
          whole = true;
        bool low = !whole && aw == 4 && vw == 8;
        if (whole || low) {
          bool found = false;
          for (uint32_t k = 0; k < nent; k++)
            if (same_access_after(f, ent[k].addr, ent[k].off, ent[k].size, ent[k].mem,
                            a[0], in->imm, size, mem, &ran)) {
              ent[k].value = a[1]; ent[k].valid = true; ent[k].pending = false;
              ent[k].sgn = false; ent[k].low = low;
              found = true;
            }
          if (!found && nent == MEMOPT_ENTS) nent = memopt_compact(ent, nent);
          if (!found && nent < MEMOPT_ENTS)
            ent[nent++] = (MemEnt){ a[0], in->imm, size, mem, a[1], false, true, false, 0, 0, low };
        }
      } else if (o->gcpure && in->op == I_FIELD_GET &&
                 vt_width((ValType)in->type) == (1u << GCF_SIZE(in->imm2))) {
        /* unpacked only: a packed store truncates and a packed load
           extends, so the stored SSA value is not what the load yields.
           Forwarding drops the load's null check with it, which is sound
           because the entry's producer ran on the same root first.  A
           forwarded collectable ref stays correct across safepoints: it
           is a live SSA value, so the collector heals it in place. */
        Ref root = gc_ref_root(f, a[0]);
        uint32_t size = (uint32_t)GCF_SIZE(in->imm2);
        bool have = false;
        for (uint32_t k = 0; k < nent; k++) {
          if (!ent[k].valid ||
              !same_access(f, ent[k].addr, ent[k].off, ent[k].size, ent[k].mem,
                           root, in->imm, size, GC_FIELD_MEM))
            continue;
          ValType et = ent[k].pending ? (ValType)ent[k].vtype
                                      : (ValType)f->insts[ent[k].value].type;
          /* the reference stored is the one read back, however much more
             its static type knows than the reader's view of the field */
          if (et != (ValType)in->type &&
              !(vt_is_ref(et) && vt_is_ref((ValType)in->type)))
            continue;
          if (ent[k].pending) {
            Ref p = materialise_param(f, st, &ent[k]);
            if (p == REF_NONE) { ent[k].valid = false; continue; }
            ent[k].pending = false;
            ent[k].value = p;
            in = &f->insts[v];
            a = ir_args(f, v);
          }
          if (apply) {
            ir_replace(f, v, ent[k].value);
            in->op = I_NOP;
            g_stats.loads_forwarded++;
            changed = true;
          }
          have = true;
          break;
        }
        if (!have && nent < MEMOPT_ENTS)
          ent[nent++] = (MemEnt){ root, in->imm, size, GC_FIELD_MEM, v,
                                  false, true, false, 0, 0, false };
      } else if (o->gcpure && in->op == I_FIELD_SET) {
        /* No dead-store elimination here: dropping an earlier set past a
           potentially trapping op would change post-trap state, which
           stays observable through re-entry.  Forwarding is the prize. */
        Ref root = gc_ref_root(f, a[0]);
        uint32_t size = (uint32_t)GCF_SIZE(in->imm2);
        /* an initialising store writes an object that did not exist when
           any tracked root's value was fixed, so it can alias none of
           them -- without this every allocation clobbers its offset */
        bool fresh = (in->imm2 & GCF_INIT) &&
                     f->insts[root].op == I_STRUCT_NEW;
        if (!fresh)
          for (uint32_t k = 0; k < nent; k++)
            if (may_alias(f, ent[k].addr, ent[k].off, ent[k].size, ent[k].mem,
                          root, in->imm, size, GC_FIELD_MEM))
              ent[k].valid = false;
        if (vt_width((ValType)f->insts[a[1]].type) == (1u << size)) {
          bool found = false;
          for (uint32_t k = 0; k < nent; k++)
            if (same_access(f, ent[k].addr, ent[k].off, ent[k].size,
                            ent[k].mem, root, in->imm, size, GC_FIELD_MEM)) {
              ent[k].value = a[1]; ent[k].valid = true; ent[k].pending = false;
              found = true;
            }
          if (!found && nent < MEMOPT_ENTS)
            ent[nent++] = (MemEnt){ root, in->imm, size, GC_FIELD_MEM, a[1],
                                    false, true, false, 0, 0, false };
        }
      } else if (in->op == I_GLOBAL_GET) {
        uint32_t g = (uint32_t)in->imm;
        if (g < MAX_TRACKED_GLOBALS) {
          if ((gok >> g) & 1 && f->insts[gval[g]].type == in->type) {
            if (apply) {
              ir_replace(f, v, gval[g]);
              in->op = I_NOP;
              g_stats.loads_forwarded++;
              changed = true;
            }
          } else {
            gval[g] = v; gok |= (uint64_t)1 << g;
          }
        }
      } else if (in->op == I_GLOBAL_SET) {
        uint32_t g = (uint32_t)in->imm;
        if (g < MAX_TRACKED_GLOBALS) { gval[g] = a[0]; gok |= (uint64_t)1 << g; }
      } else if (is_call_op(in->op) ||
                 in->op == I_MEM_GROW || in->op == I_MEM_COPY ||
                 in->op == I_MEM_FILL || in->op == I_MEM_INIT) {

        /* GC field entries and tracked globals survive what cannot write
           them: the bulk memory ops touch linear memory only, and a call
           clobbers only what its summary says it can reach -- gany means
           unknown, so everything.  The collector itself never changes a
           field's or a global's value, just its address. */
        const CallFx *fx = is_call_op(in->op) ? site_effects(f, o, v) : NULL;
        uint64_t gc_clobber = 0;
        if (fx) gc_clobber = o->gcpure ? fx->gc_field_mask : ~(uint64_t)0;
        uint32_t kept = 0;
        for (uint32_t k = 0; k < nent; k++)
          if (ent[k].mem == GC_FIELD_MEM && ent[k].valid &&
              !(gc_clobber & gc_off_bit(ent[k].off)))
            ent[kept++] = ent[k];
        nent = kept;
        npend = 0;
        if (fx) {
          if (fx->gany) gok = 0;
          else if (fx->writes_globals) gok &= ~fx->gmask;
        }
      }
    }
    if (!apply && !memstate_same(&st[bi], ent, nent, pend, npend, gval, gok)) {
      moved = true;
      for (uint32_t s = 0; s < b->nsuccs; s++) redo[b->succs[s].target] = true;
    }
    memstate_store(&st[bi], ent, nent, pend, npend, gval, gok);
  }
    if (apply) break;

    if (!moved || sweep + 1 >= MEMOPT_SWEEPS) apply = true;
  }
  for (uint32_t i = 0; i < f->nblocks; i++) memstate_free(&st[i]);
  xfree(st);
  xfree(redo);
  if (changed) ir_apply_subst(f);
  return changed;
}

typedef struct { Ref addr; uint64_t covered; uint32_t mem; } ChkEnt;

static Edge *iv_edge(IrFunc *f, uint32_t from, uint32_t to);

static uint64_t array_known_len(IrFunc *f, Ref r, int depth) {
  if (depth <= 0) return 0;
  Inst *in = &f->insts[r];
  Ref *a = ir_args(f, r);
  switch (in->op) {
    case I_ARRAY_NEW:
      if (f->insts[a[0]].op != I_CONST) return 0;
      return f->insts[a[0]].imm & 0xffffffffu;
    case I_ARRAY_NEW_SEG:
      if (in->nargs < 2 || f->insts[a[1]].op != I_CONST) return 0;
      return f->insts[a[1]].imm & 0xffffffffu;

    case I_REF_CONVERT:
      return array_known_len(f, a[0], depth - 1);
    case I_PARAM: {
      /* an array flowing through a merge is at least as long as the
         shortest any pred supplies -- min, where ir_value_max meets with
         max, because this is a lower bound */
      Block *b = &f->blocks[in->block];
      if (b->npreds == 0 || b->npreds > 4) return 0;
      uint32_t pi = UINT32_MAX;
      for (uint32_t j = 0; j < b->nparams; j++)
        if (b->params[j] == r) { pi = j; break; }
      if (pi == UINT32_MAX) return 0;
      uint64_t best = 0;
      for (uint32_t p = 0; p < b->npreds; p++) {
        Edge *e = iv_edge(f, b->preds[p], in->block);
        if (!e || pi >= e->nargs) return 0;
        uint64_t l = array_known_len(f, e->args[pi], depth - 1);
        if (!l) return 0;
        if (!best || l < best) best = l;
      }
      return best;
    }
    default:
      return 0;
  }
}

static bool pass_array_bounds(IrFunc *f) {
  bool changed = false;
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op == I_ARRAY_LEN && !(in->imm2 & GCF_NULLCHK)) {
        /* the length is fixed at allocation, so a visible allocation
           makes it a constant; with a null check pending the trap must
           stay, so leave those alone */
        uint64_t n = array_known_len(f, ir_args(f, v)[0], 4);
        if (n) {
          replace_with_const(f, v, n);
          changed = true;
          g_stats.folded++;
        }
        continue;
      }
      if (in->op != I_ARRAY_GET && in->op != I_ARRAY_SET) continue;
      if (in->imm2 & GCF_NOBOUND) continue;
      Ref *a = ir_args(f, v);
      uint64_t len = array_known_len(f, a[0], 4);
      if (!len) continue;
      if (ir_value_max(f, a[1], 6) >= len) continue;
      in->imm2 |= GCF_NOBOUND;
      changed = true;
      g_stats.checks_removed++;
    }
  }
  return changed;
}

typedef struct { Ref idx, len; } GcBnd;

/* The SSA value that is this array's length, when its allocation is
   visible: array.new takes it first, array.new_data/elem second. */
static Ref gc_array_len_value(IrFunc *f, Ref obj) {
  Inst *in = &f->insts[obj];
  if (in->op == I_ARRAY_NEW && in->nargs >= 1)
    return ir_resolve(f, ir_args(f, obj)[0]);
  if (in->op == I_ARRAY_NEW_SEG && in->nargs >= 2)
    return ir_resolve(f, ir_args(f, obj)[1]);
  return REF_NONE;
}

/* When cond on the given branch arm proves idx <u bound directly: an
   unsigned compare between the two. */
static bool gc_inbounds_cond(IrFunc *f, Ref cond, bool taken,
                             Ref *idx, Ref *len) {
  Inst *c = &f->insts[cond];
  if (c->nargs != 2) return false;
  Ref a0 = ir_resolve(f, ir_args(f, cond)[0]);
  Ref a1 = ir_resolve(f, ir_args(f, cond)[1]);
  if ((c->op == I_LTU && taken) || (c->op == I_GEU && !taken)) {
    *idx = a0; *len = a1; return true;
  }
  if ((c->op == I_GTU && taken) || (c->op == I_LEU && !taken)) {
    *idx = a1; *len = a0; return true;
  }
  return false;
}

/* The counted-loop form: the header tests its own param x against a
   bound B fixed above the loop, and x walks 0, 1, 2, ... -- it cannot
   skip B, so wherever x has been tested unequal it is also below.  On
   the continue side of x != B that gives x <u B. */
static bool gc_counted_cond(IrFunc *f, uint32_t hb, Ref cond, bool taken,
                            Ref *idx, Ref *len) {
  Inst *c = &f->insts[cond];
  if (c->nargs != 2) return false;
  if (!((c->op == I_NE && taken) || (c->op == I_EQ && !taken))) return false;
  Block *h = &f->blocks[hb];
  if (h->npreds != 2) return false;
  Ref a0 = ir_resolve(f, ir_args(f, cond)[0]);
  Ref a1 = ir_resolve(f, ir_args(f, cond)[1]);
  for (int side = 0; side < 2; side++) {
    Ref x = side ? a1 : a0, B = side ? a0 : a1;
    if (f->insts[x].op != I_PARAM || f->insts[x].block != hb) continue;
    uint32_t pi = UINT32_MAX;
    for (uint32_t j = 0; j < h->nparams; j++)
      if (h->params[j] == x) { pi = j; break; }
    if (pi == UINT32_MAX) continue;
    /* B fixed for the whole loop: defined strictly above the header */
    uint32_t bb = f->insts[B].block;
    if (bb == hb || !dominates(f, bb, hb)) continue;
    bool from_zero = false, by_one = false;
    for (uint32_t p = 0; p < h->npreds; p++) {
      Edge *e = iv_edge(f, h->preds[p], hb);
      if (!e || pi >= e->nargs) return false;
      Ref arg = ir_resolve(f, e->args[pi]);
      Inst *ai = &f->insts[arg];
      if (ai->op == I_CONST && ai->imm == 0) { from_zero = true; continue; }
      if (ai->op == I_ADD && ai->nargs == 2 &&
          ir_resolve(f, ir_args(f, arg)[0]) == x) {
        Inst *st = &f->insts[ir_resolve(f, ir_args(f, arg)[1])];
        if (st->op == I_CONST && st->imm == 1) { by_one = true; continue; }
      }
      return false;
    }
    if (!from_zero || !by_one) continue;
    *idx = x;
    *len = B;
    return true;
  }
  return false;
}

/* What a dominating test or cast proved of an object: that it is null or an
   ht. */
typedef struct { Ref obj; HeapType ht; } GcTy;

/* A ref.test of heap type t, asked of an object of heap type h, null aside:
   1 when every such object passes, 0 when none does, -1 when the object's
   own type decides.  The any hierarchy is a tree -- eq over i31, struct and
   array, each concrete type under its one supertype, the chain the runtime's
   displays compare -- so of two heap types in it neither of which is below
   the other, no value is both. */
static int gc_test_given(const Module *m, HeapType h, HeapType t) {
  if (heap_subtype(m, h, t)) return 1;
  if (heap_subtype(m, t, h)) return -1;
  if (heap_subtype(m, h, HT_ANY) && heap_subtype(m, t, HT_ANY)) return 0;
  return -1;
}

/* The answer the facts give a ref.test, or -1.  nonnull says its object is
   not null; each fact on the object's root, and the static type of every
   value between the operand and the root, says it is null or of that type --
   a ref.convert's type is its cast's, which a later test of the same or a
   wider type repeats.  A subtype tested and a supertype asked answers only
   for the supertype; a nullable proof answers a non-nullable test only once
   the object is known not to be null. */
static int gc_test_known(IrFunc *f, Ref v, bool nonnull, const GcTy *ty,
                         uint32_t nty) {
  const Module *m = f->m;
  const Inst *in = &f->insts[v];
  HeapType t = RTEST_HEAP(in->imm);
  bool nullable = (RTEST_KIND(in->imm) & RTK_NULLABLE) != 0;
  bool pass = false, fail = false;
  Ref r = ir_resolve(f, ir_args(f, v)[0]);
  for (int hop = 0; ; hop++) {
    ValType vt = (ValType)f->insts[r].type;
    if (vt_is_ref(vt)) {
      if (!vt_nullable(vt)) nonnull = true;
      int g = gc_test_given(m, vt_heap(vt), t);
      pass |= g == 1;
      fail |= g == 0;
    }
    /* the walk gc_ref_root makes, so r ends on the root the facts name */
    if (hop == 8 || f->insts[r].op != I_REF_CONVERT || f->insts[r].nargs != 1)
      break;
    r = ir_resolve(f, ir_args(f, r)[0]);
  }
  for (uint32_t k = 0; k < nty; k++) {
    if (ty[k].obj != r) continue;
    int g = gc_test_given(m, ty[k].ht, t);
    pass |= g == 1;
    fail |= g == 0;
  }
  /* both: no type fits, so the object is null */
  if (pass && fail) return nullable ? 1 : 0;
  if (pass && (nonnull || nullable)) return 1;
  if (fail && (nonnull || !nullable)) return 0;
  return -1;
}

static void gc_root_add(Ref *set, uint32_t *n, uint32_t cap, Ref obj) {
  for (uint32_t k = 0; k < *n; k++)
    if (set[k] == obj) return;
  if (*n < cap) set[(*n)++] = obj;
}

/* What a ref.test that passed -- the true arm of its branch, or past its
   cast's trap -- proves of its object: the type, and with a non-nullable
   test that it is not null. */
static void gc_test_passed(IrFunc *f, Ref test, Ref *set, uint32_t *n,
                           GcTy *ty, uint32_t *nty, uint32_t cap) {
  const Inst *in = &f->insts[test];
  Ref obj = gc_ref_root(f, ir_args(f, test)[0]);
  if (!(RTEST_KIND(in->imm) & RTK_NULLABLE)) gc_root_add(set, n, cap, obj);
  if (*nty < cap) {
    ty[*nty].obj = obj;
    ty[*nty].ht = RTEST_HEAP(in->imm);
    (*nty)++;
  }
}

/* The facts pass_gc_nullchk holds at the end of each block it has done:
   objects not null, and the types of objects. */
typedef struct {
  Ref **sets; uint32_t *counts;
  GcTy **tsets; uint32_t *tcounts;
} GcFacts;

/* What one way to a point proves of an object: that it is null, that it is
   not, and that it is null or an ht. */
typedef struct { bool null, nonnull, typed; HeapType ht; } GcWay;

static void gc_way_type(const Module *m, GcWay *w, HeapType ht) {
  if (!w->typed || heap_subtype(m, ht, w->ht)) {
    w->typed = true;
    w->ht = ht;
  }
}

/* The ref.is_null or ref.test cond is, under any eqz, or REF_NONE. */
static Ref gc_cond_test(IrFunc *f, Ref cond, bool *taken) {
  cond = ir_resolve(f, cond);
  while (f->insts[cond].op == I_EQZ && f->insts[cond].nargs == 1) {
    cond = ir_resolve(f, ir_args(f, cond)[0]);
    *taken = !*taken;
  }
  const Inst *c = &f->insts[cond];
  if (c->nargs != 1 || (c->op != I_REF_IS_NULL && c->op != I_REF_TEST))
    return REF_NONE;
  return cond;
}

/* What cond says of y once it is known nonzero (taken) or zero. */
static void gc_way_cond(IrFunc *f, Ref cond, bool taken, Ref y, GcWay *w) {
  cond = gc_cond_test(f, cond, &taken);
  if (cond == REF_NONE || gc_ref_root(f, ir_args(f, cond)[0]) != y) return;
  const Inst *c = &f->insts[cond];
  if (c->op == I_REF_IS_NULL) {
    if (taken) w->null = true;
    else w->nonnull = true;
    return;
  }
  bool nullable = (RTEST_KIND(c->imm) & RTK_NULLABLE) != 0;
  if (taken) {
    gc_way_type(f->m, w, RTEST_HEAP(c->imm));
    if (!nullable) w->nonnull = true;
  } else if (nullable) {
    w->nonnull = true;
  }
}

/* What the way from qb to `to` proves of y: the branch it leaves qb by, the
   branches into the run of single-pred blocks it ends -- the facts keep no
   word of null, which is what the arm of a `x == null` sends -- and qb's
   facts once qb is done.  The run stops at y's definition: a branch above
   it, round a loop, spoke of the y before. */
static void gc_way_edge(IrFunc *f, const GcFacts *fx, uint32_t qb, uint32_t to,
                        Ref y, GcWay *w) {
  Block *q = &f->blocks[qb];
  if (q->term == T_BRIF && q->nsuccs >= 2 &&
      q->succs[0].target != q->succs[1].target)
    gc_way_cond(f, q->term_arg, q->succs[0].target == to, y, w);
  uint32_t yb = f->insts[y].block;
  for (uint32_t c = qb, k = 0; k < 4 && c != yb; k++) {
    Block *cb = &f->blocks[c];
    if (cb->npreds != 1) break;
    uint32_t r = cb->preds[0];
    Block *rb = &f->blocks[r];
    if (rb->term == T_BRIF && rb->nsuccs >= 2 &&
        rb->succs[0].target != rb->succs[1].target)
      gc_way_cond(f, rb->term_arg, rb->succs[0].target == c, y, w);
    else if (rb->term != T_JUMP)
      break;
    c = r;
  }
  if (!fx->sets[qb]) return;
  for (uint32_t j = 0; j < fx->counts[qb]; j++)
    if (fx->sets[qb][j] == y) w->nonnull = true;
  for (uint32_t j = 0; j < fx->tcounts[qb]; j++)
    if (fx->tsets[qb][j].obj == y) gc_way_type(f->m, w, fx->tsets[qb][j].ht);
}

/* Adds one way to what all the ways into a point prove: an object of the
   widest of their types, or null where one of them allows it.  False when
   the ways have nothing in common to say. */
static bool gc_way_join(const Module *m, GcWay *all, const GcWay *w) {
  if (w->null && w->nonnull) return true;   /* a way nothing takes */
  if (w->null) {
    all->nonnull = false;
    all->null = true;
    return true;
  }
  if (!w->typed) return false;
  if (!w->nonnull) all->nonnull = false;
  if (!all->typed || heap_subtype(m, all->ht, w->ht)) {
    all->typed = true;
    all->ht = w->ht;
    return true;
  }
  return heap_subtype(m, w->ht, all->ht);
}

/* Ways that join speak of the y every one of them reaches only when y is
   defined above the point they meet at. */
static bool gc_way_above(IrFunc *f, Ref y, uint32_t at) {
  uint32_t yb = f->insts[y].block;
  return yb != at && dominates(f, yb, at);
}

/* A branch on a merge's param, as `x != null && x is T` leaves it when the
   null arm hands the merge a constant and the other the test's answer.  An
   arm of the branch is reached by the ways into the merge whose value can
   take it, so it knows of the object what all of those ways do; each way's
   knowledge is its value and what gc_way_edge reads off it. */
static bool gc_merge_cond(IrFunc *f, const GcFacts *fx, uint32_t pb, Ref cond,
                          bool taken, Ref *yp, GcWay *out) {
  const Inst *ci = &f->insts[cond];
  Block *p = &f->blocks[pb];
  if (ci->op != I_PARAM || ci->block != pb) return false;
  uint32_t pi = UINT32_MAX;
  for (uint32_t j = 0; j < p->nparams && pi == UINT32_MAX; j++)
    if (p->params[j] == cond) pi = j;
  if (pi == UINT32_MAX) return false;
  Ref y = REF_NONE;
  for (uint32_t k = 0; k < p->npreds; k++) {
    Edge *e = iv_edge(f, p->preds[k], pb);
    if (!e || e->eh || pi >= e->nargs) return false;
    bool t = taken;
    Ref c = gc_cond_test(f, e->args[pi], &t);
    if (y == REF_NONE && c != REF_NONE) y = gc_ref_root(f, ir_args(f, c)[0]);
  }
  if (y == REF_NONE || !gc_way_above(f, y, pb)) return false;
  GcWay all = { false, true, false, 0 };
  for (uint32_t k = 0; k < p->npreds; k++) {
    uint32_t qb = p->preds[k];
    Ref a = ir_resolve(f, iv_edge(f, qb, pb)->args[pi]);
    GcWay w = { false, false, false, 0 };
    if (f->insts[a].op == I_CONST) {
      if ((f->insts[a].imm != 0) != taken) continue;
    } else {
      gc_way_cond(f, a, taken, y, &w);
    }
    gc_way_edge(f, fx, qb, pb, y, &w);
    if (!gc_way_join(f->m, &all, &w)) return false;
  }
  if (!all.typed) return false;
  *yp = y;
  *out = all;
  return true;
}

/* The same at a block that ways meet at -- what jump threading makes of
   that merge, or a `when` arm that two tests share: the objects their
   branches test, and what all the ways prove of each.  Returns how many of
   ys it found. */
#define GC_JOIN_MAX 4
static uint32_t gc_join_ways(IrFunc *f, const GcFacts *fx, uint32_t bi,
                             Ref *ys, GcWay *outs) {
  Block *b = &f->blocks[bi];
  Ref cand[GC_JOIN_MAX];
  uint32_t nc = 0, n = 0;
  for (uint32_t k = 0; k < b->npreds; k++) {
    Edge *e = iv_edge(f, b->preds[k], bi);
    if (!e || e->eh) return 0;
    Block *q = &f->blocks[b->preds[k]];
    if (q->term != T_BRIF) continue;
    bool t = true;
    Ref c = gc_cond_test(f, q->term_arg, &t);
    if (c == REF_NONE) continue;
    Ref y = gc_ref_root(f, ir_args(f, c)[0]);
    bool have = false;
    for (uint32_t j = 0; j < nc; j++) have |= cand[j] == y;
    if (!have && nc < GC_JOIN_MAX) cand[nc++] = y;
  }
  for (uint32_t j = 0; j < nc; j++) {
    if (!gc_way_above(f, cand[j], bi)) continue;
    GcWay all = { false, true, false, 0 };
    bool ok = true;
    for (uint32_t k = 0; k < b->npreds && ok; k++) {
      GcWay w = { false, false, false, 0 };
      gc_way_edge(f, fx, b->preds[k], bi, cand[j], &w);
      ok = gc_way_join(f->m, &all, &w);
    }
    if (!ok || !all.typed) continue;
    ys[n] = cand[j];
    outs[n++] = all;
  }
  return n;
}

static void gc_way_add(const GcWay *w, Ref y, Ref *set, uint32_t *n,
                       GcTy *ty, uint32_t *nty, uint32_t cap) {
  if (w->nonnull) gc_root_add(set, n, cap, y);
  if (w->typed && *nty < cap) {
    ty[*nty].obj = y;
    ty[*nty].ht = w->ht;
    (*nty)++;
  }
}

/* Clears redundant GC checks a dominating proof already answered.  Null
   checks: an earlier flagged access on the same object (it trapped or it
   passed), the ref.as_non_null trap, the allocation that defined it, the
   non-null arm of a br_on_null, or a passed non-nullable ref.test -- the
   build sets GCF_NULLCHK from static nullability alone, so all
   flow-sensitive redundancy survives to here.  Tests and casts: the true
   arm of a ref.test's branch and the code past a cast's trap know their
   object's type, and the false arm of a nullable test that it is not null;
   a later ref.test those facts decide becomes its answer (gc_test_known),
   so the cast Kotlin repeats after each `is` -- the same test again, made
   nullable -- folds to 1 and its trap goes.  Where ways meet -- a block
   more than one reaches, or the arms of a branch on a merge's param, which
   is what `x != null && x is T` makes -- an object is what every way that
   gets there proves (gc_join_ways, gc_merge_cond).  Bounds checks: a
   dominating branch on idx <u array.len(o) -- lengths are fixed at
   allocation, so the proof survives anything in between, including calls,
   as an object's type does.  Same dominator-inheritance shape as pass_bce:
   sets copy from the idom in RPO, so membership means a strictly
   dominating (or earlier same-block) proof; branch facts ride single-pred
   edges. */
static bool pass_gc_nullchk(IrFunc *f) {
  /* Only an access, an i31.get or a ref.test can change, so without one --
     every function of a module in linear memory -- there is nothing to do,
     and three sets for every block to build: 70 ms of compiling
     stdgotests/webtext. */
  bool any = false;
  for (uint32_t oi = 0; oi < f->norder && !any; oi++) {
    const Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts && !any; i++)
      switch (f->insts[b->insts[i]].op) {
        case I_FIELD_GET: case I_FIELD_SET: case I_ARRAY_GET:
        case I_ARRAY_SET: case I_ARRAY_LEN: case I_I31_GET: case I_REF_TEST:
          any = true;
          break;
        default: break;
      }
  }
  if (!any) return false;

  bool changed = false;
  Ref **sets = xcalloc(f->nblocks, sizeof(Ref *));
  uint32_t *counts = xcalloc(f->nblocks, sizeof(uint32_t));
  GcBnd **bsets = xcalloc(f->nblocks, sizeof(GcBnd *));
  uint32_t *bcounts = xcalloc(f->nblocks, sizeof(uint32_t));
  GcTy **tsets = xcalloc(f->nblocks, sizeof(GcTy *));
  uint32_t *tcounts = xcalloc(f->nblocks, sizeof(uint32_t));
  GcFacts fx = { sets, counts, tsets, tcounts };
  const uint32_t CAP = 64;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    sets[bi] = xmalloc(CAP * sizeof(Ref));
    counts[bi] = 0;
    bsets[bi] = xmalloc(CAP * sizeof(GcBnd));
    bcounts[bi] = 0;
    tsets[bi] = xmalloc(CAP * sizeof(GcTy));
    tcounts[bi] = 0;
    uint32_t d = b->idom;
    if (bi != f->entry && d != REF_NONE && sets[d]) {
      counts[bi] = counts[d];
      memcpy(sets[bi], sets[d], counts[d] * sizeof(Ref));
      bcounts[bi] = bcounts[d];
      memcpy(bsets[bi], bsets[d], bcounts[d] * sizeof(GcBnd));
      tcounts[bi] = tcounts[d];
      memcpy(tsets[bi], tsets[d], tcounts[d] * sizeof(GcTy));
    }
    /* where ways meet, what every one of them proves of an object */
    if (b->npreds >= 2) {
      Ref ys[GC_JOIN_MAX];
      GcWay ws[GC_JOIN_MAX];
      uint32_t n = gc_join_ways(f, &fx, bi, ys, ws);
      for (uint32_t k = 0; k < n; k++)
        gc_way_add(&ws[k], ys[k], sets[bi], &counts[bi], tsets[bi],
                   &tcounts[bi], CAP);
    }
    /* branch facts: the non-null arm of a br_on_null, both arms of a
       ref.test, a branch on a merge of them (gc_merge_cond), and the
       in-bounds arm of an index-below-length test */
    if (b->npreds == 1) {
      Block *p = &f->blocks[b->preds[0]];
      if (p->term == T_BRIF && p->nsuccs >= 2 &&
          p->succs[0].target != p->succs[1].target &&
          (p->succs[0].target == bi || p->succs[1].target == bi)) {
        bool taken = p->succs[0].target == bi;
        Ref cond = ir_resolve(f, p->term_arg);
        while (f->insts[cond].op == I_EQZ && f->insts[cond].nargs == 1) {
          cond = ir_resolve(f, ir_args(f, cond)[0]);
          taken = !taken;
        }
        if (f->insts[cond].op == I_REF_IS_NULL && f->insts[cond].nargs == 1 &&
            !taken)
          gc_root_add(sets[bi], &counts[bi], CAP,
                      gc_ref_root(f, ir_args(f, cond)[0]));
        if (f->insts[cond].op == I_REF_TEST && f->insts[cond].nargs == 1) {
          if (taken)
            gc_test_passed(f, cond, sets[bi], &counts[bi], tsets[bi],
                           &tcounts[bi], CAP);
          /* null passes a nullable test, so failing it says not null */
          else if (RTEST_KIND(f->insts[cond].imm) & RTK_NULLABLE)
            gc_root_add(sets[bi], &counts[bi], CAP,
                        gc_ref_root(f, ir_args(f, cond)[0]));
        }
        Ref my;
        GcWay mw;
        if (gc_merge_cond(f, &fx, b->preds[0], cond, taken, &my, &mw))
          gc_way_add(&mw, my, sets[bi], &counts[bi], tsets[bi], &tcounts[bi],
                     CAP);
        Ref bidx, blen;
        if (bcounts[bi] < CAP &&
            (gc_inbounds_cond(f, cond, taken, &bidx, &blen) ||
             gc_counted_cond(f, b->preds[0], cond, taken, &bidx, &blen))) {
          bsets[bi][bcounts[bi]].idx = bidx;
          bsets[bi][bcounts[bi]].len = blen;
          bcounts[bi]++;
        }
      }
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      Ref obj = REF_NONE;
      switch (in->op) {
        case I_FIELD_GET: case I_FIELD_SET: case I_ARRAY_GET:
        case I_ARRAY_SET: case I_ARRAY_LEN: case I_I31_GET:
          obj = gc_ref_root(f, ir_args(f, v)[0]);
          if (in->imm2 & GCF_NULLCHK) {
            bool known = false;
            for (uint32_t k = 0; k < counts[bi] && !known; k++)
              known = sets[bi][k] == obj;
            if (known) {
              in->imm2 &= ~(uint64_t)GCF_NULLCHK;
              changed = true;
              g_stats.gc_nullchk_removed++;
            }
          }
          if ((in->op == I_ARRAY_GET || in->op == I_ARRAY_SET) &&
              !(in->imm2 & GCF_NOBOUND)) {
            Ref idx = ir_resolve(f, ir_args(f, v)[1]);
            Ref alen = gc_array_len_value(f, obj);
            for (uint32_t k = 0; k < bcounts[bi]; k++) {
              if (bsets[bi][k].idx != idx) continue;
              Ref L = bsets[bi][k].len;
              Inst *li = &f->insts[L];
              bool ours =
                  (li->op == I_ARRAY_LEN && li->nargs == 1 &&
                   gc_ref_root(f, ir_args(f, L)[0]) == obj) ||
                  (alen != REF_NONE && alen == L);
              if (!ours) continue;
              in->imm2 |= GCF_NOBOUND;
              changed = true;
              g_stats.checks_removed++;
              break;
            }
          }
          break;
        case I_TRAP_IF: {
          if (in->nargs != 1) break;
          if (in->imm == TRAP_NULL_REF) {
            obj = gc_ref_root(f, ir_args(f, v)[0]);
            break;
          }
          /* it traps on zero: past a cast's trap, its test passed */
          Ref t = ir_resolve(f, ir_args(f, v)[0]);
          if (f->insts[t].op == I_REF_TEST && f->insts[t].nargs == 1)
            gc_test_passed(f, t, sets[bi], &counts[bi], tsets[bi],
                           &tcounts[bi], CAP);
          break;
        }
        case I_REF_TEST: {
          if (in->nargs != 1) break;
          Ref root = gc_ref_root(f, ir_args(f, v)[0]);
          bool nonnull = false;
          for (uint32_t k = 0; k < counts[bi] && !nonnull; k++)
            nonnull = sets[bi][k] == root;
          int r = gc_test_known(f, v, nonnull, tsets[bi], tcounts[bi]);
          if (r < 0) break;
          replace_with_const(f, v, (uint64_t)r);
          changed = true;
          g_stats.checks_removed++;
          break;
        }
        case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
          obj = v;
          break;
        default: break;
      }
      if (obj != REF_NONE) gc_root_add(sets[bi], &counts[bi], CAP, obj);
    }
  }
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    xfree(sets[bi]);
    xfree(bsets[bi]);
    xfree(tsets[bi]);
  }
  xfree(sets);
  xfree(counts);
  xfree(bsets);
  xfree(bcounts);
  xfree(tsets);
  xfree(tcounts);
  return changed;
}

static bool pass_bce(IrFunc *f, Module *m) {
  bool changed = false;

  /* Nothing to do without a check, which with guard pages is most
     functions; a set is read only below its count. */
  bool any = false;
  for (uint32_t oi = 0; oi < f->norder && !any; oi++) {
    const Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts && !any; i++)
      any = f->insts[b->insts[i]].op == I_BOUNDS_CHECK;
  }
  if (!any) return false;

  ChkEnt **sets = xcalloc(f->nblocks, sizeof(ChkEnt *));
  uint32_t *counts = xcalloc(f->nblocks, sizeof(uint32_t));
  const uint32_t CAP = 64;

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    sets[bi] = xmalloc(CAP * sizeof(ChkEnt));
    counts[bi] = 0;
    uint32_t d = b->idom;
    if (bi != f->entry && d != REF_NONE && sets[d]) {
      counts[bi] = counts[d];
      memcpy(sets[bi], sets[d], counts[d] * sizeof(ChkEnt));
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op != I_BOUNDS_CHECK) continue;
      Ref *a = ir_args(f, v);

      Ref base = a[0];
      uint64_t need = in->imm2;
      uint32_t mem = (uint32_t)in->imm;
      if (f->insts[base].op == I_MEM_ADDR) {
        need += f->insts[base].imm;
        base = ir_args(f, base)[0];
      }

      uint64_t mx = ir_value_max(f, base, 6);
      uint64_t floor_ = mem_guaranteed_bytes(m, mem);
      if (mx <= UINT64_MAX - need && mx + need <= floor_) {
        in->op = I_NOP;
        g_stats.checks_removed++;
        changed = true;
        continue;
      }
      bool covered = false;
      for (uint32_t k = 0; k < counts[bi]; k++)
        if (sets[bi][k].addr == base && sets[bi][k].mem == mem &&
            sets[bi][k].covered >= need) { covered = true; break; }
      if (covered) {
        in->op = I_NOP;
        g_stats.checks_removed++;
        changed = true;
        continue;
      }
      bool updated = false;
      for (uint32_t k = 0; k < counts[bi]; k++)
        if (sets[bi][k].addr == base && sets[bi][k].mem == mem) {
          if (sets[bi][k].covered < need) sets[bi][k].covered = need;
          updated = true;
          break;
        }
      if (!updated && counts[bi] < CAP)
        sets[bi][counts[bi]++] = (ChkEnt){ base, need, mem };
    }
  }
  for (uint32_t i = 0; i < f->nblocks; i++) xfree(sets[i]);
  xfree(sets); xfree(counts);
  return changed;
}

typedef struct {
  bool writes_mem, writes_anywhere;

  /* which globals the loop can write: a bit per tracked index, and "any"
     when one is beyond the mask or a callee is unknown -- fx_unknown says
     gany, so any means assume everything */
  uint64_t glob_mask;
  bool glob_any;

  bool may_grow;
  bool frame_call;

  /* GC storage classes are disjoint: struct fields and array elements
     cannot alias each other, so each write kind clobbers only its own.
     Field writes clobber by offset, hashed into a mask -- two accesses at
     different byte offsets are different slots whatever their types. */
  bool gc_array_clobber;
  uint64_t gc_field_set_mask;
  Ref frame_root;
  int64_t frame_base;
  int64_t lo, hi;
} LoopFx;

static int lc_u32_cmp(const void *a, const void *b) {
  uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
  return x < y ? -1 : x > y;
}

static int lc_u64_cmp(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

static void loop_effects(IrFunc *f, const OptOpts *o, const uint32_t *lblk,
                         uint32_t nlbl, LoopFx *lf, SpInfo *si) {
  memset(lf, 0, sizeof *lf);
  lf->frame_root = REF_NONE;
  for (uint32_t li = 0; li < nlbl; li++) {
    uint32_t bi = lblk[li];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      switch (f->insts[v].op) {
        case I_STORE:
          lf->writes_mem = true;
          break;
        case I_MEM_COPY: case I_MEM_FILL: case I_MEM_INIT:
          lf->writes_mem = lf->writes_anywhere = true;
          break;
        case I_MEM_GROW:
          lf->writes_mem = lf->writes_anywhere = lf->may_grow = true;
          break;
        case I_GLOBAL_SET:
          fx_mark_global(&lf->glob_mask, &lf->glob_any,
                         (uint32_t)f->insts[v].imm);
          break;
        case I_FIELD_SET:
          lf->gc_field_set_mask |= gc_off_bit(f->insts[v].imm);
          break;
        case I_ARRAY_SET: case I_ARRAY_OP:
          lf->gc_array_clobber = true;
          break;
        case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
        case I_RETURN_CALL: case I_RETURN_CALL_INDIRECT:
        case I_RETURN_CALL_REF: {
          const CallFx *fx = site_effects(f, o, v);
          lf->may_grow = true;
          lf->gc_field_set_mask |= fx->gc_field_mask;
          if (fx->gc_array_write) lf->gc_array_clobber = true;
          lf->glob_mask |= fx->gmask;
          if (fx->gany) lf->glob_any = true;
          if (!fx->frame_only) {
            lf->writes_mem = lf->writes_anywhere = true;
            break;
          }
          if (!fx->writes_mem) break;
          Ref sp = global_value_at(si, o, bi, i, fx->frame_global);
          Ref root = REF_NONE;
          int64_t base = 0;
          if (sp != REF_NONE) access_root(f, sp, &root, &base);
          if (root == REF_NONE) {
            lf->writes_mem = lf->writes_anywhere = true;
            break;
          }
          lf->writes_mem = true;
          if (!lf->frame_call) {
            lf->frame_call = true;
            lf->frame_root = root;
            lf->frame_base = base;
            lf->lo = fx->lo;
            lf->hi = fx->hi;
          } else if (lf->frame_root != root || lf->frame_base != base) {
            lf->writes_anywhere = true;
          } else {
            if (fx->lo < lf->lo) lf->lo = fx->lo;
            if (fx->hi > lf->hi) lf->hi = fx->hi;
          }
          break;
        }
        default: break;
      }
    }
  }
}

/* Counts into a caller-owned, all-zero buffer and records which entries it
   touched, so one allocation serves every loop in the function instead of
   an O(ninsts) calloc per header. */
static void const_reg_uses(IrFunc *f, const uint32_t *lblk, uint32_t nlbl,
                           uint32_t *n, uint32_t *touched, uint32_t *nt) {
  *nt = 0;
#define CRU_BUMP(r) do { if (n[r]++ == 0) touched[(*nt)++] = (r); } while (0)
  for (uint32_t li = 0; li < nlbl; li++) {
    uint32_t bi = lblk[li];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      Ref *a = ir_args(f, u);
      for (uint32_t j = 0; j < in->nargs; j++) {
        Inst *ci = &f->insts[a[j]];
        if (ci->op != I_CONST || vt_is_float((ValType)ci->type)) continue;
        if (!ir_imm_ok(f, u, j, ci->imm, (ValType)ci->type)) CRU_BUMP(a[j]);
      }
    }

    if (b->term == T_BRIF || b->term == T_BRTABLE) CRU_BUMP(b->term_arg);
    for (uint32_t i = 0; i < b->nret; i++) CRU_BUMP(b->ret_args[i]);
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++)
        CRU_BUMP(b->succs[s].args[j]);
  }
#undef CRU_BUMP
}

static bool stops_block(const Inst *in) {
  switch (in->op) {
    case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
    case I_RETURN_CALL: case I_RETURN_CALL_INDIRECT: case I_RETURN_CALL_REF:

    case I_THROW: case I_THROW_REF:
    case I_TRAP: case I_TRAP_IF:
    case I_DIVS: case I_DIVU: case I_REMS: case I_REMU:
    case I_TRUNCS: case I_TRUNCU:
    case I_LOAD: case I_STORE: case I_BOUNDS_CHECK:
    case I_MEM_COPY: case I_MEM_FILL: case I_MEM_INIT:
    case I_TABLE_GET: case I_TABLE_SET: case I_TABLE_FILL:
    case I_TABLE_COPY: case I_TABLE_INIT:
      return true;
    default:
      /* A GC access can trap on null or bounds and an allocation on heap
         OOM; hoisting another trap past one reorders which fires. */
      return gc_op_traps(in);
  }
}

static bool stops_with_other_code(const Inst *in) {
  switch (in->op) {
    case I_LOAD: case I_STORE: case I_BOUNDS_CHECK:
    case I_MEM_COPY: case I_MEM_FILL: case I_MEM_INIT:
      return false;
    default: return stops_block(in);
  }
}

/* An access as same_access compares it: both of its tests need the access's
   offset from its base (s), and then either the same base or the same root,
   width and arithmetic.  Worked out once per access instead of once per
   pair, since a load's question goes to every access up the dominator tree. */
typedef struct {
  uint64_t s, c;
  Ref b, r;
  uint32_t size, mem;
  bool w;
} AccKey;

typedef struct {
  AccKey *k;
  uint32_t n, cap;
  bool built;
} AccList;

static AccKey acc_key(IrFunc *f, Ref addr, uint64_t imm, uint32_t size,
                      uint32_t mem) {
  AccKey k;
  uint64_t off, ia;
  access_base(f, addr, &k.b, &off);
  k.s = off + imm;
  access_parts(f, addr, &k.r, &k.c, &ia, &k.w);
  k.size = size;
  k.mem = mem;
  return k;
}

static AccKey acc_key_of(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  return acc_key(f, ir_args(f, v)[0], in->imm, (uint32_t)MEMF_SIZE(in->imm2),
                 MEMF_MEM(in->imm2));
}

/* same_access(x, q), from their keys. */
static bool acc_key_same(const AccKey *x, const AccKey *q) {
  if (x->size != q->size || x->mem != q->mem || x->s != q->s) return false;
  if (x->b == q->b) return true;
  if (x->r == REF_NONE || x->r != q->r || x->w != q->w) return false;
  uint64_t mask = x->w ? ~0ull : 0xffffffffull;
  return ((x->c - q->c) & mask) == 0;
}

static void acc_push(AccList *L, AccKey k) {
  if (L->n == L->cap) {
    L->cap = L->cap ? L->cap * 2 : 4;
    L->k = xrealloc(L->k, L->cap * sizeof(AccKey));
  }
  L->k[L->n++] = k;
}

/* Is the access the same as a load or store in a block dominating target?
   acc caches each block's accesses; the caller drops a block's list when
   its instructions change and adds to it what it hoists in. */
static bool access_dominates(IrFunc *f, AccList *acc, Ref addr, uint64_t imm,
                             uint32_t size, uint32_t mem, uint32_t target) {
  AccKey q = acc_key(f, addr, imm, size, mem);
  for (uint32_t bi = target; bi != REF_NONE; bi = f->blocks[bi].idom) {
    AccList *L = &acc[bi];
    if (!L->built) {
      Block *b = &f->blocks[bi];
      L->n = 0;
      for (uint32_t i = 0; i < b->ninsts; i++) {
        uint16_t op = f->insts[b->insts[i]].op;
        if (op == I_LOAD || op == I_STORE) acc_push(L, acc_key_of(f, b->insts[i]));
      }
      L->built = true;
    }
    for (uint32_t i = 0; i < L->n; i++)
      if (acc_key_same(&L->k[i], &q)) return true;
    if (bi == f->entry) break;
  }
  return false;
}

/* Whether a GC read moved to the end of block target still has the checks
   it was let off answered there.  Where it stood, a guard inside the loop
   may have answered them; at target they hold if its object is non-null by
   type all the way to its root -- a cast's convert hoists without its test
   -- or if an access to that root dominates target, which ran and so
   proved it not null, and for an element, read at the same index, in
   bounds.  A field or a length also takes pass_gc_nullchk's branch facts:
   the non-null arm of a null test or a ref.test above target. */
static bool gc_checks_hold_at(IrFunc *f, Ref v, uint32_t target) {
  Inst *in = &f->insts[v];
  Ref r = gc_ref_root(f, ir_args(f, v)[0]);
  Ref idx = in->op == I_ARRAY_GET ? ir_resolve(f, ir_args(f, v)[1]) : REF_NONE;
  ValType rt = (ValType)f->insts[r].type;
  if (idx == REF_NONE && vt_is_ref(rt) && !vt_nullable(rt)) return true;
  uint32_t budget = 4096;
  for (uint32_t bi = target; bi != REF_NONE; bi = f->blocks[bi].idom) {
    Block *b = &f->blocks[bi];
    Block *p = b->npreds == 1 ? &f->blocks[b->preds[0]] : NULL;
    if (idx == REF_NONE && p && p->term == T_BRIF && p->nsuccs >= 2 &&
        p->succs[0].target != p->succs[1].target) {
      bool taken = p->succs[0].target == bi;
      Ref cond = ir_resolve(f, p->term_arg);
      while (f->insts[cond].op == I_EQZ && f->insts[cond].nargs == 1) {
        cond = ir_resolve(f, ir_args(f, cond)[0]);
        taken = !taken;
      }
      Inst *ci = &f->insts[cond];
      if ((ci->op == I_REF_IS_NULL || ci->op == I_REF_TEST) && ci->nargs == 1 &&
          gc_ref_root(f, ir_args(f, cond)[0]) == r) {
        if (ci->op == I_REF_IS_NULL && !taken) return true;
        /* null passes a nullable test and fails any other */
        if (ci->op == I_REF_TEST &&
            taken != ((RTEST_KIND(ci->imm) & RTK_NULLABLE) != 0))
          return true;
      }
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      if (!budget--) return false;
      Ref u = b->insts[i];
      uint16_t op = f->insts[u].op;
      bool elem = op == I_ARRAY_GET || op == I_ARRAY_SET;
      if (!elem && op != I_FIELD_GET && op != I_FIELD_SET && op != I_ARRAY_LEN)
        continue;
      if (gc_ref_root(f, ir_args(f, u)[0]) != r) continue;
      if (idx == REF_NONE || (elem && ir_resolve(f, ir_args(f, u)[1]) == idx))
        return true;
    }
    if (bi == f->entry) break;
  }
  return false;
}

/* lblk lists the loop's blocks, in index order: the stores a load must not
   alias are looked for there, not in a scan of every block per load. */
static bool inst_hoistable(IrFunc *f, Ref v, bool reach_mem, bool reach_any,
                           const LoopFx *lf, const uint8_t *inloop,
                           const uint32_t *lblk, uint32_t nlbl, AccList *acc,
                           const uint32_t *const_reg_use, uint32_t preheader,
                           bool gcpure) {
  bool writes_mem = lf->writes_mem;
  bool writes_anywhere = lf->writes_anywhere;
  Inst *in = &f->insts[v];
  if (in->op == I_PARAM || in->op == I_NOP) return false;

  if (in->op == I_CONST) {

    if (vt_is_float((ValType)in->type)) return in->imm != 0;

    if ((ValType)in->type == VT_V128) return true;

    if (const_reg_use[v] == 0) return true;
    if (const_reg_use[v] >= 2) return true;
    return ir_const_cost(in->imm, vt_is_64((ValType)in->type)) > 1;
  }

  if (in->op == I_MEM_ADDR) return true;
  if (in->op == I_LOAD) {

    uint32_t lsz = (uint32_t)MEMF_SIZE(in->imm2);
    Ref laddr = ir_args(f, v)[0];

    if (!reach_mem &&
        !access_dominates(f, acc, laddr, in->imm, lsz, MEMF_MEM(in->imm2), preheader))
      return false;
    if (writes_anywhere) return false;
    if (!writes_mem) return true;
    for (uint32_t li = 0; li < nlbl; li++) {
      Block *lb = &f->blocks[lblk[li]];
      for (uint32_t i = 0; i < lb->ninsts; i++) {
        Inst *si = &f->insts[lb->insts[i]];
        if (si->op != I_STORE) continue;
        if (may_alias(f, laddr, in->imm, lsz, MEMF_MEM(in->imm2),
                      ir_args(f, lb->insts[i])[0], si->imm,
                      (uint32_t)MEMF_SIZE(si->imm2), MEMF_MEM(si->imm2)))
          return false;
      }
    }

    if (lf->frame_call) {
      Ref lroot;
      int64_t loff;
      access_root(f, laddr, &lroot, &loff);
      if (lroot != lf->frame_root) return false;
      if (!clear_of_frame(loff + (int64_t)in->imm - lf->frame_base, 1u << lsz,
                          lf->lo, lf->hi))
        return false;
    }
    return true;
  }
  if (in->op == I_GLOBAL_GET)
    return !lf->glob_any &&
           (in->imm >= FX_GBITS ||
            !(lf->glob_mask & ((uint64_t)1 << in->imm)));

  if (in->op == I_EXN_OBJ) return false;

  if (in->op == I_MEM_LIMIT) return !lf->may_grow;

  /* GC reads hoist like any invariant when nothing in the loop can write
     them: array lengths never change, immutable fields and elements are
     written only at initialisation, and a mutable one is safe when the
     loop makes no call and no write to its storage class -- fields and
     array elements cannot alias each other.  Traps ride the same rule as
     the divides below -- only from a position the loop always reaches.
     Kotlin's array wrappers re-read their storage field, with its null
     check and load barrier, on every iteration; this is that lever.
     A check the access no longer carries was answered by a dominating
     proof, and past the header's always-reached stretch that proof may be
     a guard inside the loop -- the access in the non-null arm of a test --
     so there it moves only where the preheader answers the same checks
     (gc_checks_hold_at).  Kotlin's joinToString read a vtable from null. */
  if (gcpure && (in->op == I_ARRAY_LEN || in->op == I_FIELD_GET ||
                 in->op == I_ARRAY_GET)) {
    if (gc_op_traps(in) && !reach_any) return false;
    if (!reach_any) {
      for (uint32_t j = 0; j < in->nargs; j++)
        if (inloop[f->insts[ir_args(f, v)[j]].block]) return false;
      if (!gc_checks_hold_at(f, v, preheader)) return false;
    }
    if (in->op == I_ARRAY_LEN) return true;
    if (in->op == I_FIELD_GET)
      return gc_field_immutable(f, v) ||
             !(lf->gc_field_set_mask & gc_off_bit(in->imm));
    return gc_array_elem_immutable(f->m, (uint32_t)in->imm) ||
           !lf->gc_array_clobber;
  }

  if (in->type == 0 && !reach_any) return false;
  if (ir_is_pure(in)) return true;
  if (!reach_any) return false;
  if (in->op == I_BOUNDS_CHECK) return !lf->may_grow;
  /* A trap_if on an invariant condition fires on the first trip or never,
     as a divide by an invariant zero does: Kotlin's cast of an invariant
     receiver to its itable type, rechecked on every call. */
  return in->op == I_DIVS || in->op == I_DIVU ||
         in->op == I_REMS || in->op == I_REMU || in->op == I_TRAP_IF;
}

bool ir_op_is_gc_safepoint(uint16_t op) {
  switch (op) {
    case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
    case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
    case I_THROW:
      return true;
    default:
      return false;
  }
}

static uint32_t loop_body(IrFunc *f, uint32_t hb, uint8_t *inloop,
                          uint32_t *stack) {
  Block *h = &f->blocks[hb];
  bool is_header = false;
  uint32_t preheader = REF_NONE, nfwd = 0;
  for (uint32_t p = 0; p < h->npreds; p++) {
    uint32_t pb = h->preds[p];
    if (f->blocks[pb].rpo >= h->rpo) is_header = true;
    else { nfwd++; preheader = pb; }
  }
  if (!is_header || nfwd != 1 || preheader == REF_NONE) return REF_NONE;

  if (ir_block_has_eh(f, preheader)) return REF_NONE;

  memset(inloop, 0, f->nblocks);
  inloop[hb] = 1;
  uint32_t sp = 0;
  for (uint32_t p = 0; p < h->npreds; p++) {
    uint32_t pb = h->preds[p];
    if (f->blocks[pb].rpo >= h->rpo && !inloop[pb]) { inloop[pb] = 1; stack[sp++] = pb; }
  }
  while (sp) {
    uint32_t cur = stack[--sp];
    Block *c = &f->blocks[cur];
    for (uint32_t p = 0; p < c->npreds; p++)
      if (!inloop[c->preds[p]]) { inloop[c->preds[p]] = 1; stack[sp++] = c->preds[p]; }
  }

  if (inloop[preheader] || inloop[f->entry]) return REF_NONE;
  return preheader;
}

static bool loop_is_safepoint_free(IrFunc *f, const uint8_t *inloop) {
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    if (!inloop[bi]) continue;
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++)
      if (ir_op_is_gc_safepoint(f->insts[b->insts[i]].op)) return false;
  }
  return true;
}

static bool lab_cannot_escape(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  switch (in->op) {
    case I_PARAM: case I_CONST: case I_NOP:
    case I_ADD: case I_SUB: case I_MUL:
    case I_AND: case I_OR: case I_XOR:
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTL: case I_ROTR:
    case I_CLZ: case I_CTZ: case I_POPCNT: case I_BSWAP:
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_WRAP: case I_EXTENDS: case I_EXTENDU:
    case I_EXT8S: case I_EXT16S: case I_EXT32S:
    case I_TRUNC_SATS: case I_TRUNC_SATU:
    case I_SELECT:
    case I_GLOBAL_GET: case I_GLOBAL_SET:
    case I_REF_NULL: case I_REF_IS_NULL: case I_REF_I31:
      return true;

    case I_FIELD_GET:
      return !(in->imm2 & GCF_NULLCHK);
    case I_FIELD_SET:
      return !(in->imm2 & GCF_NULLCHK);
    case I_ARRAY_GET:
      return !(in->imm2 & GCF_NULLCHK) && (in->imm2 & GCF_NOBOUND);
    case I_ARRAY_SET:
      return !(in->imm2 & GCF_NULLCHK) && (in->imm2 & GCF_NOBOUND);
    case I_ARRAY_LEN:
      return !(in->imm2 & GCF_NULLCHK);
    case I_REF_CONVERT:
      return true;
    default:
      return false;
  }
}

static bool lab_fast_alloc(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  if (in->op == I_STRUCT_NEW) return in->imm2 && in->imm2 <= 256;
  if (in->op != I_ARRAY_NEW || in->nargs != 1) return false;
  Ref len = ir_args(f, v)[0];
  if (f->insts[len].op != I_CONST) return false;
  uint64_t stride = in->imm2 & 0xff, hdr = (in->imm2 >> 8) & 0xff;
  if (!stride || !hdr) return false;
  uint64_t total = hdr + (f->insts[len].imm & 0xffffffffu) * stride;
  return total && total <= 256;
}

static bool pass_lab_cache(IrFunc *f) {
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    Block *h = &f->blocks[hb];
    if (h->term != T_BRIF || h->nsuccs != 2) continue;
    uint32_t self = 2, exit = 2;
    for (uint32_t k = 0; k < 2; k++) {
      if (h->succs[k].target == hb) self = k; else exit = k;
    }
    if (self == 2 || exit == 2) continue;

    uint32_t pre = REF_NONE, npre = 0;
    for (uint32_t p = 0; p < h->npreds; p++)
      if (h->preds[p] != hb) { pre = h->preds[p]; npre++; }
    if (npre != 1 || pre == REF_NONE) continue;
    if (ir_block_has_eh(f, hb) || ir_block_has_eh(f, pre)) continue;

    bool ok = true, any = false;
    for (uint32_t i = 0; i < h->ninsts && ok; i++) {
      Ref v = h->insts[i];
      uint16_t op = f->insts[v].op;
      if (op == I_STRUCT_NEW || op == I_ARRAY_NEW) {
        if (lab_fast_alloc(f, v)) any = true; else ok = false;
      } else if (!lab_cannot_escape(f, v)) {
        ok = false;
      }
    }
    if (!ok || !any) continue;

    Edge *ex = &h->succs[exit];
    uint32_t tgt = ex->target;
    uint32_t nb = ir_new_block(f);
    h = &f->blocks[hb];
    ex = &h->succs[exit];
    Block *t = &f->blocks[tgt];
    uint32_t np = t->nparams;
    Ref *fwd = np ? xmalloc(np * sizeof(Ref)) : NULL;
    for (uint32_t k = 0; k < np; k++)
      fwd[k] = ir_add_param(f, nb, (ValType)f->insts[t->params[k]].type);
    ir_emit(f, nb, I_GC_LAB_FLUSH, 0, NULL, 0, 0, 0);
    f->blocks[nb].term = T_JUMP;
    ir_add_edge(f, nb, tgt, fwd, np);
    xfree(fwd);

    h = &f->blocks[hb];
    h->succs[exit].target = nb;

    ir_emit(f, pre, I_GC_LAB_OPEN, 0, NULL, 0, 0, 0);
    for (uint32_t i = 0; i < f->blocks[hb].ninsts; i++) {
      Ref v = f->blocks[hb].insts[i];
      uint16_t op = f->insts[v].op;
      if (op == I_STRUCT_NEW || op == I_ARRAY_NEW) f->insts[v].imm2 |= GCA_CACHED;
    }
    ir_recompute_preds(f);
    ir_order(f);
    return true;
  }
  return false;
}

static bool bar_load(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  if (in->op != I_FIELD_GET && in->op != I_ARRAY_GET && in->op != I_EXN_PAYLOAD)
    return false;
  return vt_ref_collectable(f->m, (ValType)in->type);
}

typedef struct { Ref base; uint64_t key; } BarSlot;

static uint64_t bar_slot_key(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  if (in->op == I_ARRAY_GET) return (uint64_t)ir_args(f, v)[1] | (1ull << 32);
  return in->imm;
}

static void bar_add_mask(IrFunc *f, Ref v, Ref mask) {
  Inst *in = &f->insts[v];
  uint32_t n = in->nargs;

  Ref old[2];
  if (n > 2) warmc_fatal("internal: %s takes %u arguments and cannot be given "
                         "a mask", ir_op_name(in->op), n);
  for (uint32_t k = 0; k < n; k++) old[k] = ir_args(f, v)[k];
  uint32_t oc_ = f->cap_argpool;
  while (f->nargpool + n + 1 > f->cap_argpool)
    f->cap_argpool = f->cap_argpool ? f->cap_argpool * 2 : 32;
  if (f->cap_argpool != oc_)
    f->argpool = ir_grow(f, f->argpool, (size_t)oc_ * sizeof(Ref),
                         (size_t)f->cap_argpool * sizeof(Ref));
  in = &f->insts[v];
  in->args_off = f->nargpool;
  for (uint32_t k = 0; k < n; k++) f->argpool[f->nargpool++] = old[k];
  f->argpool[f->nargpool++] = mask;
  in->nargs = n + 1;
  in->imm2 |= GCF_MASKARG;
}

static bool pass_bar_elide(IrFunc *f) {

  if (!warmc_load_barrier) return false;
  bool any = false;
  for (uint32_t bi = 0; bi < f->nblocks && !any; bi++) {
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++)
      if (bar_load(f, b->insts[i])) { any = true; break; }
  }
  if (!any) return false;

  uint8_t *inloop = xmalloc(f->nblocks);
  uint32_t *stack = xmalloc(f->nblocks * sizeof(uint32_t));
  Ref *entry_mask = xmalloc(f->nblocks * sizeof(Ref));
  for (uint32_t i = 0; i < f->nblocks; i++) entry_mask[i] = REF_NONE;
  bool changed = false;

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    if (entry_mask[hb] != REF_NONE) continue;
    uint32_t pre = loop_body(f, hb, inloop, stack);
    if (pre == REF_NONE) continue;
    if (!loop_is_safepoint_free(f, inloop)) continue;
    bool used = false;
    for (uint32_t bi = 0; bi < f->nblocks && !used; bi++) {
      if (!inloop[bi]) continue;
      Block *b = &f->blocks[bi];
      for (uint32_t i = 0; i < b->ninsts; i++)
        if (bar_load(f, b->insts[i])) { used = true; break; }
    }
    if (!used) continue;
    Ref m = ir_emit(f, pre, I_GC_BADMASK, VT_I64, NULL, 0, 0, 0);
    for (uint32_t bi = 0; bi < f->nblocks; bi++)
      if (inloop[bi]) entry_mask[bi] = m;
    g_stats.bar_masks++;
    changed = true;
  }

  BarSlot *slots = xmalloc(f->ninsts * sizeof(BarSlot));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    Ref cur = entry_mask[bi];
    uint32_t nslot = 0;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      uint16_t op = f->insts[v].op;
      if (ir_op_is_gc_safepoint(op)) { cur = REF_NONE; nslot = 0; continue; }
      if (op == I_ARRAY_OP) { nslot = 0; continue; }
      if (!bar_load(f, v)) continue;
      Ref base = ir_args(f, v)[0];
      uint64_t key = bar_slot_key(f, v);
      bool elide = false;
      for (uint32_t k = 0; k < nslot && !elide; k++)
        if (slots[k].base == base && slots[k].key == key) elide = true;
      if (elide) {
        f->insts[v].imm2 |= GCF_NOBAR;
        g_stats.bar_elided++;
        changed = true;
        continue;
      }
      slots[nslot].base = base; slots[nslot].key = key; nslot++;
      if (cur == REF_NONE) {
        cur = ir_emit(f, bi, I_GC_BADMASK, VT_I64, NULL, 0, 0, 0);

        b->ninsts--;
        for (uint32_t j = b->ninsts; j > i; j--) b->insts[j] = b->insts[j - 1];
        b->insts[i] = cur;
        b->ninsts++;
        g_stats.bar_masks++;
        i++;
      }
      bar_add_mask(f, v, cur);
      changed = true;
    }
  }
  xfree(slots);
  xfree(inloop); xfree(stack); xfree(entry_mask);
  return changed;
}

#define GS_MAX_BLOCKS 2048
#define GS_MV_UNKNOWN ((Ref)0xfffffffeu)

static bool gl_opaque(uint32_t op);

static void gs_move_to(IrFunc *f, uint32_t bi, uint32_t to) {
  Block *b = &f->blocks[bi];
  uint32_t r = b->insts[b->ninsts - 1];
  memmove(&b->insts[to + 1], &b->insts[to],
          (b->ninsts - 1 - to) * sizeof(uint32_t));
  b->insts[to] = r;
}

static bool gs_observes(IrFunc *f, const OptOpts *o, Ref v, uint32_t gidx,
                        bool *tail) {
  uint16_t op = f->insts[v].op;
  *tail = op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
          op == I_RETURN_CALL_REF;
  if (*tail) return true;
  const CallFx *fx = site_effects(f, o, v);
  if (fx->gany) return true;
  return (fx->gmask & ((uint64_t)1 << gidx)) != 0;
}

static uint64_t gs_weight(const Block *b) {
  if (b->cold) return 0;
  uint32_t d = b->loop_depth < 4 ? b->loop_depth : 4;
  return (uint64_t)1 << (3 * d);
}


/* Values the dry run stands in for what it does not create: numbered past
   every instruction, so no real value compares equal to one. */
#define GS_TOKEN(n) ((Ref)(0x80000000u + (n)))
static Ref gs_res(IrFunc *f, Ref r) {
  return r != REF_NONE && r < f->ninsts ? ir_resolve(f, r) : r;
}

/* With dry set, change nothing and answer in *dry the memory operations the
   promotion would add -- the load on entry, the stores and reloads around
   calls, the stores at returns -- weighted 8^loop-depth. */
static bool gs_promote_one(IrFunc *f, const OptOpts *o, uint32_t gidx,
                           ValType gt, uint64_t *dry) {
  uint32_t nb = f->nblocks;
  uint32_t ntok = 0;
  Ref *param = xmalloc(nb * sizeof(Ref));
  Ref *outv = xmalloc(nb * sizeof(Ref));
  Ref *memv = xmalloc((nb ? nb : 1) * sizeof(Ref));
  uint8_t *done = xcalloc(nb ? nb : 1, 1);
  for (uint32_t i = 0; i < nb; i++) { param[i] = REF_NONE; outv[i] = REF_NONE; }

  Ref seed;
  if (dry) {
    seed = GS_TOKEN(ntok++);
    *dry = gs_weight(&f->blocks[f->entry]);
  } else {
    seed = ir_emit(f, f->entry, I_GLOBAL_GET, gt, NULL, 0, gidx, 0);
    gs_move_to(f, f->entry, 0);
  }

  uint8_t *gneed = xcalloc(nb ? nb : 1, 1);
  for (bool again = true; again;) {
    again = false;
    for (uint32_t oi = f->norder; oi-- > 0;) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      if (gneed[bi]) continue;
      bool need = false;
      for (uint32_t i = 0; i < b->ninsts && !need; i++)
        if (f->insts[b->insts[i]].op == I_GLOBAL_GET &&
            (uint32_t)f->insts[b->insts[i]].imm == gidx) need = true;
      for (uint32_t sx = 0; sx < b->nsuccs && !need; sx++)
        if (b->succs[sx].target < nb && gneed[b->succs[sx].target]) need = true;
      if (need) { gneed[bi] = 1; again = true; }
    }
  }

  uint8_t *sreach = xcalloc(nb ? nb : 1, 1);
  for (bool again = true; again;) {
    again = false;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      bool out_has = sreach[bi];
      for (uint32_t i = 0; i < b->ninsts && !out_has; i++) {
        Ref r = b->insts[i];
        if (f->insts[r].op == I_GLOBAL_SET &&
            (uint32_t)f->insts[r].imm == gidx) { out_has = true; break; }

        bool t2 = false;
        if (gl_opaque(f->insts[r].op) && gs_observes(f, o, r, gidx, &t2))
          out_has = true;
      }
      if (!out_has) continue;
      for (uint32_t sx = 0; sx < b->nsuccs; sx++) {
        uint32_t t = b->succs[sx].target;
        if (t < nb && !sreach[t]) { sreach[t] = 1; again = true; }
      }
    }
  }
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    if (bi != f->entry && f->blocks[bi].npreds >= 2 && sreach[bi]) {
      if (dry) param[bi] = GS_TOKEN(ntok++);
      else param[bi] = ir_add_param(f, bi, gt);
    }
  }
  xfree(sreach);

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    /* A merge no write reaches gets what any way in carries, the value read
       on entry -- from a way in already walked: the first predecessor can be
       the latch of a loop this block heads, which a jump-threaded loop
       entered at its body has first, and has nothing yet. */
    uint32_t in = b->npreds ? b->preds[0] : bi;
    for (uint32_t p = 0; p < b->npreds; p++)
      if (done[b->preds[p]]) { in = b->preds[p]; break; }
    Ref cur = bi == f->entry ? seed
            : param[bi] != REF_NONE ? param[bi]
            : outv[in];
    /* What memory holds, which is the entry value only until the first
       write before a call: the value left for the return and the next call
       is compared with that, not with the entry value, or a function that
       restores its frame pointer after a call returns with the callee's. */
    Ref mv = GS_MV_UNKNOWN;
    if (bi == f->entry) mv = seed;
    else {
      for (uint32_t p = 0; p < b->npreds; p++) {
        uint32_t pb = b->preds[p];
        Ref pm = done[pb] ? memv[pb] : GS_MV_UNKNOWN;
        if (p == 0) mv = pm;
        else if (pm != mv) mv = GS_MV_UNKNOWN;
      }
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];

      if (r == seed) continue;
      uint16_t op = f->insts[r].op;
      if (op == I_GLOBAL_GET && (uint32_t)f->insts[r].imm == gidx) {
        if (cur == REF_NONE) { cur = r; mv = r; continue; }
        if (dry) continue;
        ir_replace(f, r, cur);
        f->insts[r].op = I_NOP;
      } else if (op == I_GLOBAL_SET && (uint32_t)f->insts[r].imm == gidx) {
        cur = ir_args(f, r)[0];
        if (!dry) f->insts[r].op = I_NOP;
      } else if (gl_opaque(op)) {
        bool tail = false;
        if (!gs_observes(f, o, r, gidx, &tail)) continue;

        if (cur != REF_NONE &&
            (mv == GS_MV_UNKNOWN || gs_res(f, cur) != gs_res(f, mv))) {
          if (dry) *dry += gs_weight(b);
          else {
            Ref v = ir_resolve(f, cur);
            ir_emit(f, bi, I_GLOBAL_SET, 0, &v, 1, gidx, 0);
            gs_move_to(f, bi, i);
            i++;
          }
        }
        bool later = false;
        for (uint32_t k = i + 1; k < b->ninsts && !later; k++)
          if (f->insts[b->insts[k]].op == I_GLOBAL_GET &&
              (uint32_t)f->insts[b->insts[k]].imm == gidx) later = true;
        for (uint32_t sx = 0; sx < b->nsuccs && !later; sx++)
          if (b->succs[sx].target < nb && gneed[b->succs[sx].target]) later = true;
        if (!tail && later && dry) {
          *dry += gs_weight(b);
          cur = mv = GS_TOKEN(ntok++);
        } else if (!tail && later) {
          ir_emit(f, bi, I_GLOBAL_GET, gt, NULL, 0, gidx, 0);
          gs_move_to(f, bi, i + 1);
          cur = f->blocks[bi].insts[i + 1];
          mv = cur;
          i++;
        } else {

          cur = REF_NONE;
          mv = GS_MV_UNKNOWN;
        }
      }
    }
    outv[bi] = cur;
    memv[bi] = mv;
    done[bi] = 1;
  }

  for (uint32_t bi = 0; bi < nb; bi++) {
    Block *b = &f->blocks[bi];
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      uint32_t t = b->succs[s].target;
      if (t >= nb || param[t] == REF_NONE) continue;
      Ref av = done[bi] ? outv[bi] : seed;
      if (dry) {
        if (av == REF_NONE) *dry += gs_weight(b);
        continue;
      }
      if (av == REF_NONE) {

        av = ir_emit(f, bi, I_GLOBAL_GET, gt, NULL, 0, gidx, 0);
      }
      edge_push_arg_at(f, bi, s, ir_resolve(f, av));
    }
  }

  for (uint32_t bi = 0; bi < nb; bi++) {
    if (!done[bi] || f->blocks[bi].term != T_RETURN) continue;
    if (outv[bi] == REF_NONE) continue;
    Ref v = gs_res(f, outv[bi]);
    if (memv[bi] != GS_MV_UNKNOWN && v == gs_res(f, memv[bi])) continue;
    if (dry) { *dry += gs_weight(&f->blocks[bi]); continue; }
    ir_emit(f, bi, I_GLOBAL_SET, 0, &v, 1, gidx, 0);
  }

  xfree(param); xfree(outv); xfree(memv); xfree(done); xfree(gneed);
  if (dry) return true;
  ir_apply_subst(f);
  g_stats.globals_ssa++;
  return true;
}

/* Whether making g a value pays, by counting memory operations on it, each
   weighted 8^loop-depth for how often its block runs: every global.get and
   global.set the promotion removes, against what a dry run of it adds -- the
   load on entry, a store before and a reload after the calls that need
   them, a store at each return.  The merges it makes are register copies
   and cost nothing measurable; charging them, even a quarter of a memory
   operation each, left stdgo/deferred 27% slower than promoting.  Go wins:
   it writes its stack pointer twice before every call, for the frame and
   for the return address, and the two become one (stdgo/recurse 17%
   faster, deferred 27%).  A shadow stack pointer that LLVM or
   AssemblyScript pushes at entry and pops at exit, around calls, is no
   fewer operations as a value, only the copies and the register that carry
   it, so it stays in memory: asbench/bintree, cbench/printf and hsbench/fib
   were 4-11% slower with every global promoted.

   A loop that goes round without such a call and touches g five times on
   the way pays however the totals fall: that is the shadow stack around
   AssemblyScript's Array#push, a store forwarded to the next load several
   times an iteration, where the calls are the rare growth path the counts
   cannot tell from the hot one (asbench/arrpush twice as fast). */
#define GS_MIN_CYCLE_USES 5

static bool gs_pays(IrFunc *f, const OptOpts *o, uint32_t gidx) {
  uint32_t nb = f->nblocks;
  uint8_t *callb = xcalloc(nb ? nb : 1, 1);
  uint32_t *touch = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  uint8_t *fwd = xmalloc(nb ? nb : 1), *bwd = xmalloc(nb ? nb : 1);
  uint32_t *stk = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint64_t saves = 0, costs = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    uint64_t w = gs_weight(b);
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];
      uint16_t op = f->insts[r].op;
      bool tail;
      if ((op == I_GLOBAL_GET || op == I_GLOBAL_SET) && (uint32_t)f->insts[r].imm == gidx) {
        touch[bi]++;
        saves += w;
      } else if (gl_opaque(op) && gs_observes(f, o, r, gidx, &tail)) {
        callb[bi] = 1;
      }
    }
  }
  gs_promote_one(f, o, gidx, (ValType)f->m->globals[gidx].type, &costs);
  bool pays = saves > costs;
  uint32_t best = 0;

  /* A loop nested in the cycle's that makes such a call is no way round
     without it: the counts take a loop to run, and one whose body calls
     pays the store and reload every time round it.  An inlined callee's
     dispatch loop is the shape -- its shadow-stack push and pop sit on a
     path that only skips the calls by running the loop zero times. */
  uint8_t *lcall = xcalloc(nb ? nb : 1, 1);
  for (uint32_t oi = 0; oi < f->norder && !pays; oi++) {
    uint32_t t = f->order[oi];
    Block *tb = &f->blocks[t];
    bool header = false;
    for (uint32_t p = 0; p < tb->npreds && !header; p++)
      header = dominates(f, t, tb->preds[p]);
    if (!header) continue;
    uint32_t sp = 0;
    memset(bwd, 0, nb);
    bwd[t] = 1;
    lcall[t] = callb[t];
    for (uint32_t p = 0; p < tb->npreds; p++) {
      uint32_t l = tb->preds[p];
      if (!bwd[l] && dominates(f, t, l)) { bwd[l] = 1; stk[sp++] = l; }
    }
    while (sp && !lcall[t]) {
      uint32_t x = stk[--sp];
      Block *xb = &f->blocks[x];
      if (callb[x]) lcall[t] = 1;
      for (uint32_t p = 0; p < xb->npreds; p++) {
        uint32_t q = xb->preds[p];
        if (bwd[q]) continue;
        bwd[q] = 1;
        stk[sp++] = q;
      }
    }
  }

  for (uint32_t oi = 0; oi < f->norder && !pays; oi++) {
    uint32_t h = f->order[oi];
    Block *hb = &f->blocks[h];
    if (callb[h]) continue;
    bool header = false;
    for (uint32_t p = 0; p < hb->npreds; p++)
      if (dominates(f, h, hb->preds[p])) header = true;
    if (!header) continue;
    /* Forward from the header through call-free blocks it dominates, then
       back from its latches within those: what is left lies on a cycle. */
    memset(fwd, 0, nb);
    memset(bwd, 0, nb);
    uint32_t sp = 0;
    fwd[h] = 1;
    stk[sp++] = h;
    while (sp) {
      Block *xb = &f->blocks[stk[--sp]];
      for (uint32_t s = 0; s < xb->nsuccs; s++) {
        uint32_t t = xb->succs[s].target;
        if (fwd[t] || callb[t] || lcall[t] || !dominates(f, h, t)) continue;
        fwd[t] = 1;
        stk[sp++] = t;
      }
    }
    for (uint32_t p = 0; p < hb->npreds; p++) {
      uint32_t l = hb->preds[p];
      if (fwd[l] && !bwd[l]) { bwd[l] = 1; stk[sp++] = l; }
    }
    while (sp) {
      Block *xb = &f->blocks[stk[--sp]];
      for (uint32_t p = 0; p < xb->npreds; p++) {
        uint32_t q = xb->preds[p];
        if (!fwd[q] || bwd[q]) continue;
        bwd[q] = 1;
        stk[sp++] = q;
      }
    }
    uint32_t uses = 0;
    for (uint32_t x = 0; x < nb; x++)
      if (fwd[x] && bwd[x]) uses += touch[x];
    if (uses > best) best = uses;
    if (uses >= GS_MIN_CYCLE_USES) pays = true;
  }
  if (warmc_getenv("WARMC_GSSA_DEBUG"))
    fprintf(stderr, "gssa %s g%u: saves %llu costs %llu cycle uses %u -> %s\n",
            f->m->funcs[f->func_index].sym ? f->m->funcs[f->func_index].sym : "?",
            gidx, (unsigned long long)saves, (unsigned long long)costs, best,
            pays ? "promote" : "leave");
  xfree(callb); xfree(touch); xfree(fwd); xfree(bwd); xfree(stk); xfree(lcall);
  return pays;
}

static bool pass_global_ssa(IrFunc *f, const OptOpts *o) {
  if (!f->m->nglobals || f->m->ntags || f->npads) return false;
  {
    /* WARMC_GSSA_FUNCS=lo-hi limits the pass to those function indices, to
       bisect a module down to the function it changes. */
    const char *r = warmc_getenv("WARMC_GSSA_FUNCS");
    unsigned lo = 0, hi = ~0u;
    if (r && sscanf(r, "%u-%u", &lo, &hi) >= 1 &&
        (f->func_index < lo || f->func_index > hi)) return false;
  }
  if (f->nblocks > GS_MAX_BLOCKS || !f->norder) return false;

  uint8_t *seen = xcalloc(f->nblocks, 1);
  bool ok = true;
  for (uint32_t oi = 0; oi < f->norder && ok; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (b->is_pad || ir_block_has_eh(f, bi)) ok = false;
    else if (bi != f->entry && b->npreds < 2 &&
             (b->npreds != 1 || !seen[b->preds[0]])) ok = false;
    seen[bi] = 1;
  }
  xfree(seen);
  if (!ok) return false;

  uint64_t reads = 0, writes = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      if (in->op != I_GLOBAL_GET && in->op != I_GLOBAL_SET) continue;
      uint32_t g = (uint32_t)in->imm;
      if (g >= FX_GBITS) continue;
      if (vt_is_ref(f->m->globals[g].type)) continue;
      if (in->op == I_GLOBAL_GET) reads |= (uint64_t)1 << g;
      else writes |= (uint64_t)1 << g;
    }
  }
  uint64_t want = reads & writes;

  if (!want) return false;

  bool changed = false;
  for (uint32_t g = 0; g < FX_GBITS; g++) {
    if (!(want & ((uint64_t)1 << g))) continue;
    if (!o->gssa_all && !gs_pays(f, o, g)) continue;
    if (gs_promote_one(f, o, g, f->m->globals[g].type, NULL)) changed = true;
  }
  if (changed) {
    ir_order(f);
    ir_verify(f, "post-global-ssa");
  }
  return changed;
}

/* An unsigned 32-bit division or remainder by a value its loop does not
   change: the hardware divider takes 7 to 9 cycles a time, one division in
   flight, where the reciprocal M = ceil(2^64 / d), made once before the loop,
   turns each one into a multiply-high (Lemire, Kaser and Kurz, "Faster
   remainder by direct computation"):

     n / d  =  mulhu64(M, n)          n % d  =  mulhu64(M * n mod 2^64, d)

   exactly, for every 32-bit n and every d from 2 up.  M is UINT64_MAX / d + 1,
   which is 0 for d = 1: the remainder comes out 0 regardless, and the quotient
   selects n.  A division by 0 still traps where it did -- the check stays in
   the loop, a compare the branch predictor never misses -- and the reciprocal
   is made from 1 instead, so making it cannot trap first.  gobench/divmod
   hashes a slice by d and by d + 1 that way, a divider-bound loop in both
   engines.  A 64-bit division whose operands both fit in 32 bits is the same
   division -- Go's compiler widens a uint32 before it divides, so stdgo's copy
   of that loop divides i64s -- and takes the 64-bit reciprocal as it is.  The
   reciprocal goes before the outermost loop the divisor is invariant in: outer
   loops are met first in rpo. */
/* A remainder of a division the block has already made is n - q*d, a
   multiply-subtract: asbench/div64 takes n / d and n % d of every pair, and
   two divisions back to back are what both engines spend that loop on.  The
   division comes first, so a divisor of 0, or a signed overflow, has trapped
   there before the remainder could be asked for. */
static bool pass_divrem(IrFunc *f) {
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Ref div[8];
    uint32_t nd = 0;
    for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++) {
      Ref v = f->blocks[bi].insts[i];
      Inst *in = &f->insts[v];
      if (in->op == I_DIVS || in->op == I_DIVU) {
        if (nd < 8) div[nd++] = v;
        continue;
      }
      if (in->op != I_REMS && in->op != I_REMU) continue;
      uint16_t want = in->op == I_REMS ? I_DIVS : I_DIVU;
      Ref *a = ir_args(f, v);
      Ref q = REF_NONE;
      for (uint32_t j = 0; j < nd && q == REF_NONE; j++) {
        Ref *da = ir_args(f, div[j]);
        if (f->insts[div[j]].op == want && f->insts[div[j]].type == in->type &&
            da[0] == a[0] && da[1] == a[1])
          q = div[j];
      }
      if (q == REF_NONE) continue;
      Ref ma[2] = { q, a[1] };
      uint32_t at = i;
      Ref m = emit_before(f, bi, &at, I_MUL, (ValType)in->type, ma, 2, 0);
      in = &f->insts[v];
      a = ir_args(f, v);
      a[1] = m;
      in->op = I_SUB;
      i = at;
      changed = true;
      g_stats.strength_reduced++;
    }
  }
  return changed;
}

#define ID_MAX 8
static bool pass_invdiv(IrFunc *f) {
  bool changed = false;
  uint8_t *inloop = xcalloc(f->nblocks, 1);
  uint32_t *stack = xmalloc(f->nblocks * sizeof(uint32_t));
  uint32_t *lblk = xmalloc(f->nblocks * sizeof(uint32_t));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    Block *h = &f->blocks[hb];
    bool is_header = false;
    uint32_t pre = REF_NONE, nfwd = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo) is_header = true;
      else { nfwd++; pre = pb; }
    }
    if (!is_header || nfwd != 1 || pre == REF_NONE) continue;
    if (ir_block_has_eh(f, pre)) continue;

    uint32_t nl = 0, sp = 0;
    inloop[hb] = 1;
    lblk[nl++] = hb;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo && !inloop[pb]) {
        inloop[pb] = 1; lblk[nl++] = pb; stack[sp++] = pb;
      }
    }
    while (sp) {
      Block *c = &f->blocks[stack[--sp]];
      for (uint32_t p = 0; p < c->npreds; p++)
        if (!inloop[c->preds[p]]) {
          inloop[c->preds[p]] = 1; lblk[nl++] = c->preds[p]; stack[sp++] = c->preds[p];
        }
    }
    if (inloop[pre] || inloop[f->entry]) {
      for (uint32_t i = 0; i < nl; i++) inloop[lblk[i]] = 0;
      continue;
    }

    Ref dv[ID_MAX], dm[ID_MAX], d64[ID_MAX], one32[ID_MAX];
    ValType dt[ID_MAX];
    uint32_t nd = 0;
    for (uint32_t li = 0; li < nl; li++) {
      uint32_t bi = lblk[li];
      for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++) {
        Ref v = f->blocks[bi].insts[i];
        Inst *in = &f->insts[v];
        if (in->op != I_DIVU && in->op != I_REMU) continue;
        Ref n = ir_args(f, v)[0], d = ir_args(f, v)[1];
        if (inloop[f->insts[d].block] || f->insts[d].op == I_CONST) continue;
        ValType t = (ValType)in->type;
        if (t == VT_I64 && (ir_value_max(f, n, 6) > 0xffffffffu ||
                            ir_value_max(f, d, 6) > 0xffffffffu))
          continue;
        if (t != VT_I32 && t != VT_I64) continue;
        uint32_t k = 0;
        while (k < nd && !(dv[k] == d && dt[k] == t)) k++;
        if (k == nd) {
          if (nd == ID_MAX) continue;
          /* d' = d ? d : 1, then M = UINT64_MAX / d' + 1, before the loop */
          Ref c1 = ir_emit(f, pre, I_CONST, t, NULL, 0, 1, 0);
          Ref sa[3] = { d, c1, d };
          Ref dd = ir_emit(f, pre, I_SELECT, t, sa, 3, 0, 0);
          Ref wide = t == VT_I64 ? dd : ir_emit(f, pre, I_EXTENDU, VT_I64, &dd, 1, 0, 0);
          Ref all = ir_emit(f, pre, I_CONST, VT_I64, NULL, 0, ~(uint64_t)0, 0);
          Ref qa[2] = { all, wide };
          Ref q0 = ir_emit(f, pre, I_DIVU, VT_I64, qa, 2, 0, 0);
          Ref c64 = ir_emit(f, pre, I_CONST, VT_I64, NULL, 0, 1, 0);
          Ref ma[2] = { q0, c64 };
          dv[k] = d;
          dm[k] = ir_emit(f, pre, I_ADD, VT_I64, ma, 2, 0, 0);
          d64[k] = wide;
          Ref ea[2] = { d, c1 };
          one32[k] = ir_emit(f, pre, I_EQ, VT_I32, ea, 2, 0, 0);
          dt[k] = t;
          nd++;
        }
        uint32_t at = i;
        emit_before(f, bi, &at, I_TRAP_IF, VT_BOT, &d, 1, TRAP_DIV_ZERO);
        Ref n64 = t == VT_I64 ? n : emit_before(f, bi, &at, I_EXTENDU, VT_I64, &n, 1, 0);
        Ref r;
        if (in->op == I_DIVU) {
          Ref ha[2] = { dm[k], n64 };
          Ref q = emit_before(f, bi, &at, I_MULHU, VT_I64, ha, 2, 0);
          if (t == VT_I32) q = emit_before(f, bi, &at, I_WRAP, VT_I32, &q, 1, 0);
          Ref sa[3] = { n, q, one32[k] };
          r = emit_before(f, bi, &at, I_SELECT, t, sa, 3, 0);
        } else {
          Ref la[2] = { dm[k], n64 };
          Ref lo = emit_before(f, bi, &at, I_MUL, VT_I64, la, 2, 0);
          Ref ha[2] = { lo, d64[k] };
          r = emit_before(f, bi, &at, I_MULHU, VT_I64, ha, 2, 0);
          if (t == VT_I32) r = emit_before(f, bi, &at, I_WRAP, VT_I32, &r, 1, 0);
        }
        in = &f->insts[v];
        ir_replace(f, v, r);
        in->op = I_NOP;
        i = at;
        changed = true;
        g_stats.strength_reduced++;
      }
    }
    for (uint32_t i = 0; i < nl; i++) inloop[lblk[i]] = 0;
  }
  xfree(inloop); xfree(stack); xfree(lblk);
  if (changed) ir_apply_subst(f);
  return changed;
}

static bool pass_licm(IrFunc *f, const OptOpts *o) {
  bool changed = false;
  uint8_t *inloop = xcalloc(f->nblocks, 1);
  uint8_t *reach = xmalloc(f->nblocks);
  uint32_t *stack = xmalloc(f->nblocks * sizeof(uint32_t));
  uint32_t *rstack = xmalloc(f->nblocks * sizeof(uint32_t));
  uint32_t *lblk = xmalloc(f->nblocks * sizeof(uint32_t));
  uint64_t *lord = xmalloc(f->nblocks * sizeof(uint64_t));
  uint32_t *const_reg_use = xcalloc(f->ninsts ? f->ninsts : 1, sizeof(uint32_t));
  uint32_t *cru_touched = xmalloc((f->ninsts ? f->ninsts : 1) * sizeof(uint32_t));
  AccList *acc = xcalloc(f->nblocks ? f->nblocks : 1, sizeof(AccList));
  SpInfo spi;
  sp_reset(&spi, f, reach, rstack);

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    Block *h = &f->blocks[hb];

    bool is_header = false;
    uint32_t preheader = REF_NONE;
    uint32_t nfwd = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo) { is_header = true; }
      else { nfwd++; preheader = pb; }
    }
    if (!is_header || nfwd != 1 || preheader == REF_NONE) continue;

    bool guarded_pre = f->blocks[preheader].term != T_JUMP;
    if (guarded_pre && f->blocks[preheader].term != T_BRIF) continue;

    if (ir_block_has_eh(f, preheader)) continue;

    /* inloop is all-zero here; the flood records what it marks so the
       unmark at the bottom costs the loop, not the function. */
    uint32_t nlbl = 0;
    inloop[hb] = 1;
    lblk[nlbl++] = hb;
    uint32_t sp = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo && !inloop[pb]) {
        inloop[pb] = 1; lblk[nlbl++] = pb; stack[sp++] = pb;
      }
    }
    while (sp) {
      uint32_t cur = stack[--sp];
      Block *c = &f->blocks[cur];
      for (uint32_t p = 0; p < c->npreds; p++)
        if (!inloop[c->preds[p]]) {
          inloop[c->preds[p]] = 1;
          lblk[nlbl++] = c->preds[p];
          stack[sp++] = c->preds[p];
        }
    }

    if (inloop[preheader] || inloop[f->entry]) {
      for (uint32_t i = 0; i < nlbl; i++) inloop[lblk[i]] = 0;
      continue;
    }

    /* Two visit orders, both matching what a full sweep saw: ascending
       block index for the effects gather (its first frame call wins), and
       rpo for the hoist scan (its order is the preheader's inst order). */
    qsort(lblk, nlbl, sizeof(uint32_t), lc_u32_cmp);
    for (uint32_t i = 0; i < nlbl; i++)
      lord[i] = ((uint64_t)f->blocks[lblk[i]].rpo << 32) | lblk[i];
    qsort(lord, nlbl, sizeof(uint64_t), lc_u64_cmp);

    LoopFx lf;

    sp_reset(&spi, f, reach, rstack);
    loop_effects(f, o, lblk, nlbl, &lf, &spi);
    uint32_t cru_nt;
    const_reg_uses(f, lblk, nlbl, const_reg_use, cru_touched, &cru_nt);

    Block *pre = &f->blocks[preheader];
    for (uint32_t bo = 0; bo < nlbl; bo++) {
      uint32_t bi = (uint32_t)lord[bo];
      Block *b = &f->blocks[bi];

      bool reach_mem = (bi == hb), reach_any = (bi == hb);
      uint32_t k = 0;
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref v = b->insts[i];
        Inst *in = &f->insts[v];
        /* Only an op that stays ends the always-reached stretch: one hoisted
           runs in the preheader, in the same order before the rest, so what
           follows it in the header is still reached first on entry --
           Kotlin's interface calls, a checked receiver load, then the
           checked load of its itable. */
        bool m = reach_mem, any = reach_any;
        bool hoist = !(guarded_pre && in->op != I_CONST) &&
                     inst_hoistable(f, v, m, any, &lf, inloop, lblk, nlbl,
                                    acc, const_reg_use, preheader, o->gcpure);
        Ref *a = ir_args(f, v);
        for (uint32_t j = 0; hoist && j < in->nargs; j++)
          if (inloop[f->insts[a[j]].block]) hoist = false;
        if (!hoist) {
          if (stops_block(in)) reach_any = false;
          if (stops_with_other_code(in)) reach_mem = false;
          b->insts[k++] = v;
          continue;
        }

        if (pre->ninsts == pre->cap_insts) {
          uint32_t oc_ = pre->cap_insts;
          pre->cap_insts = pre->cap_insts ? pre->cap_insts * 2 : 8;
          pre->insts = ir_grow(f, pre->insts, (size_t)oc_ * sizeof(uint32_t),
                               (size_t)pre->cap_insts * sizeof(uint32_t));
        }
        pre->insts[pre->ninsts++] = v;
        in->block = preheader;
        if (acc[preheader].built && (in->op == I_LOAD || in->op == I_STORE))
          acc_push(&acc[preheader], acc_key_of(f, v));
        g_stats.licm_hoisted++;
        changed = true;
      }
      b->ninsts = k;
    }
    for (uint32_t i = 0; i < cru_nt; i++) const_reg_use[cru_touched[i]] = 0;
    for (uint32_t i = 0; i < nlbl; i++) {
      inloop[lblk[i]] = 0;
      acc[lblk[i]].built = false;
    }
  }
  for (uint32_t i = 0; i < f->nblocks; i++) xfree(acc[i].k);
  xfree(acc);
  xfree(inloop); xfree(reach); xfree(stack); xfree(rstack);
  xfree(lblk); xfree(lord); xfree(const_reg_use); xfree(cru_touched);
  return changed;
}

static bool gl_offset_from(IrFunc *f, Ref v, Ref base, const Ref *subst,
                           uint64_t *off, int depth) {
  if (depth > 16) return false;
  if (v == base) { *off = 0; return true; }
  if (subst && subst[v] != REF_NONE && subst[v] != v)
    return gl_offset_from(f, subst[v], base, subst, off, depth + 1);
  Inst *in = &f->insts[v];
  if ((in->op != I_ADD && in->op != I_SUB) || in->nargs != 2) return false;
  Ref *a = ir_args(f, v);
  Inst *r = &f->insts[a[1]];
  if (r->op != I_CONST) return false;
  uint64_t sub;
  if (!gl_offset_from(f, a[0], base, subst, &sub, depth + 1)) return false;

  *off = in->op == I_ADD ? sub + r->imm : sub - r->imm;
  return true;
}

static bool gl_round_trip(IrFunc *f, Ref v, Ref base, const Ref *subst,
                          bool w64) {
  uint64_t off;
  if (!gl_offset_from(f, v, base, subst, &off, 0)) return false;
  return (w64 ? off : (off & 0xffffffffu)) == 0;
}

static bool gl_opaque(uint32_t op) {
  return op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF ||
         op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
         op == I_RETURN_CALL_REF;
}

static bool lh_writes_mem(uint32_t op) {
  return op == I_MEM_FILL || op == I_MEM_COPY || op == I_MEM_INIT ||
         op == I_MEM_GROW;
}

#define GL_MAX_GLOBALS 8

typedef struct { uint32_t from, succ; Ref val; } GlEdge;
#define GL_MAX_EDGES 32

/* An exit into a block that traps needs no write-back -- unless the block
   reads the global first: a return_call is built as the call followed by
   trap #1, and the callee, or any call before an unreachable, sees the
   global as memory has it.  GHC's tail calls out of a loop that kept Sp in
   a register made bintree hang, and a counter promoted around a loop that
   ends in return_call returned 0. */
static bool gl_exit_reads(IrFunc *f, uint32_t t, uint32_t gidx) {
  Block *b = &f->blocks[t];
  if (b->term != T_TRAP) return true;
  for (uint32_t i = 0; i < b->ninsts; i++) {
    Inst *in = &f->insts[b->insts[i]];
    if (is_call_op(in->op)) return true;
    if (in->op == I_GLOBAL_GET && (uint32_t)in->imm == gidx) return true;
  }
  return false;
}

static bool gl_walk(IrFunc *f, uint32_t hb, const uint8_t *inloop,
                    uint32_t gidx, Ref hval, Ref *cur, Ref *subst,
                    GlEdge *back, uint32_t *nback,
                    GlEdge *out, uint32_t *nout) {
  for (uint32_t i = 0; i < f->nblocks; i++) cur[i] = REF_NONE;
  for (uint32_t i = 0; i < f->ninsts; i++) subst[i] = REF_NONE;
  cur[hb] = hval;
  *nback = *nout = 0;
  for (uint32_t bo = 0; bo < f->norder; bo++) {
    uint32_t bi = f->order[bo];
    if (!inloop[bi]) continue;
    if (cur[bi] == REF_NONE) return false;
    Ref v = cur[bi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];
      Inst *in = &f->insts[r];
      if ((uint32_t)in->imm != gidx) continue;
      if (in->op == I_GLOBAL_GET) subst[r] = v;
      else if (in->op == I_GLOBAL_SET) v = ir_args(f, r)[0];
    }
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      uint32_t t = b->succs[s].target;
      if (t == hb) {
        if (*nback == GL_MAX_EDGES) return false;
        back[(*nback)++] = (GlEdge){ bi, s, v };
      } else if (inloop[t]) {
        if (cur[t] == REF_NONE) cur[t] = v;
        else if (cur[t] != v) return false;
      } else if (gl_exit_reads(f, t, gidx)) {

        if (*nout == GL_MAX_EDGES) return false;
        out[(*nout)++] = (GlEdge){ bi, s, v };
      }
    }
  }
  return true;
}

static void gl_apply(IrFunc *f, const uint8_t *inloop, uint32_t gidx,
                     const Ref *cur) {
  for (uint32_t bo = 0; bo < f->norder; bo++) {
    uint32_t bi = f->order[bo];
    if (!inloop[bi]) continue;
    Ref v = cur[bi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];
      Inst *in = &f->insts[r];
      if ((uint32_t)in->imm != gidx) continue;
      if (in->op == I_GLOBAL_GET) {
        ir_replace(f, r, v);
        in->op = I_NOP;
      } else if (in->op == I_GLOBAL_SET) {
        v = ir_args(f, r)[0];
        in->op = I_NOP;
        in->nargs = 0;
      }
    }
  }
  ir_apply_subst(f);
}

static uint32_t gl_split_edge(IrFunc *f, uint32_t from, uint32_t si) {
  Block *b = &f->blocks[from];
  uint32_t t = b->succs[si].target, nargs = b->succs[si].nargs;
  Ref *args = NULL;
  if (nargs) {
    args = xmalloc(nargs * sizeof(Ref));
    memcpy(args, b->succs[si].args, nargs * sizeof(Ref));
  }
  uint32_t nb = ir_new_block(f);
  f->blocks[nb].term = T_JUMP;
  ir_add_edge(f, nb, t, args, nargs);
  xfree(args);
  f->blocks[from].succs[si].target = nb;
  f->blocks[from].succs[si].nargs = 0;
  f->blocks[nb].reachable = true;
  return nb;
}

static bool gl_promote_one(IrFunc *f, uint32_t hb, uint32_t pre,
                           const uint8_t *inloop, uint32_t gidx,
                           ValType gtype, bool may_carry) {

  Ref entry = ir_emit(f, pre, I_GLOBAL_GET, gtype, NULL, 0, gidx, 0);
  bool w64 = vt_is_64(gtype);

  Ref *cur = xmalloc(f->nblocks * sizeof(Ref));
  Ref *subst = xmalloc(f->ninsts * sizeof(Ref));
  GlEdge back[GL_MAX_EDGES], out[GL_MAX_EDGES];
  uint32_t nback = 0, nout = 0;
  bool ok = gl_walk(f, hb, inloop, gidx, entry, cur, subst,
                    back, &nback, out, &nout);

  bool round = ok;
  for (uint32_t i = 0; i < nback && round; i++)
    if (!gl_round_trip(f, back[i].val, entry, subst, w64)) round = false;
  for (uint32_t i = 0; i < nout && round; i++)
    if (!gl_round_trip(f, out[i].val, entry, subst, w64)) round = false;

  if (!round) {
    if (!ok || !may_carry || !nback) {
      if (warmc_getenv("WARMC_GL_DEBUG"))
        fprintf(stderr, "gl %s b%u g%u: no round trip%s\n",
                f->m->funcs[f->func_index].sym, hb, gidx,
                may_carry ? " and the walk declined" : " and carrying is off");
      xfree(cur); xfree(subst); return false;
    }

    Ref p = ir_add_param(f, hb, gtype);
    for (uint32_t i = 0; i < f->nblocks; i++) if (cur[i] == entry) cur[i] = p;
    for (uint32_t i = 0; i < nback; i++) if (back[i].val == entry) back[i].val = p;
    for (uint32_t i = 0; i < nout; i++) if (out[i].val == entry) out[i].val = p;

    edge_push_arg(f, pre, hb, entry);
    for (uint32_t i = 0; i < nback; i++)
      edge_push_arg_at(f, back[i].from, back[i].succ, back[i].val);
    gl_apply(f, inloop, gidx, cur);

    for (uint32_t i = 0; i < nout; i++) {
      Ref v = ir_resolve(f, out[i].val);
      uint32_t nb = gl_split_edge(f, out[i].from, out[i].succ);
      ir_emit(f, nb, I_GLOBAL_SET, 0, &v, 1, gidx, 0);
    }
    if (warmc_getenv("WARMC_GL_DEBUG"))
      fprintf(stderr, "gl %s b%u g%u: carried, %u back %u out\n",
              f->m->funcs[f->func_index].sym, hb, gidx, nback, nout);
    xfree(cur); xfree(subst);
    g_stats.globals_promoted++;
    return true;
  }

  gl_apply(f, inloop, gidx, cur);
  xfree(cur); xfree(subst);
  g_stats.globals_promoted++;
  return true;
}

static bool gl_call_mask(IrFunc *f, const OptOpts *o, Ref v, uint64_t *mask) {
  uint16_t op = f->insts[v].op;
  if (op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
      op == I_RETURN_CALL_REF) return false;
  const CallFx *fx = site_effects(f, o, v);
  if (fx->gany) return false;
  *mask |= fx->gmask;
  return true;
}

static bool pass_global_loop(IrFunc *f, const OptOpts *o) {
  bool changed = false;
  uint32_t cap = f->nblocks;
  uint8_t *inloop = xmalloc(cap);
  uint32_t *stack = xmalloc(cap * sizeof(uint32_t));
  uint32_t nb = f->nblocks;

  bool no_throw = f->m->ntags == 0;
  for (uint32_t hb = 0; hb < nb; hb++) {

    if (f->nblocks > cap) {
      cap = f->nblocks;
      inloop = xrealloc(inloop, cap);
      stack = xrealloc(stack, cap * sizeof(uint32_t));
    }
    Block *h = &f->blocks[hb];
    if (!h->reachable || h->is_pad) continue;
    uint32_t pre = REF_NONE, nfwd = 0, nback = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      if (f->blocks[h->preds[p]].rpo >= h->rpo) nback++;
      else { nfwd++; pre = h->preds[p]; }
    }
    if (!nback || nfwd != 1) continue;

    memset(inloop, 0, f->nblocks);
    inloop[hb] = 1;
    uint32_t sp = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo && !inloop[pb]) {
        inloop[pb] = 1; stack[sp++] = pb;
      }
    }
    while (sp) {
      Block *c = &f->blocks[stack[--sp]];
      for (uint32_t p = 0; p < c->npreds; p++)
        if (!inloop[c->preds[p]]) { inloop[c->preds[p]] = 1; stack[sp++] = c->preds[p]; }
    }
    if (inloop[f->entry]) continue;

    bool clear = true;
    uint64_t callmask = 0;
    uint32_t gset[GL_MAX_GLOBALS];
    ValType gtype[GL_MAX_GLOBALS];
    uint32_t ngset = 0;
    for (uint32_t bi = 0; bi < f->nblocks && clear; bi++) {
      if (!inloop[bi]) continue;
      Block *b = &f->blocks[bi];
      if (b->is_pad || ir_block_has_eh(f, bi)) { clear = false; break; }
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Inst *in = &f->insts[b->insts[i]];
        if (gl_opaque(in->op)) {
          if (!no_throw || !gl_call_mask(f, o, b->insts[i], &callmask)) {
            clear = false; break;
          }
          continue;
        }
        if (in->op != I_GLOBAL_SET) continue;
        uint32_t g = (uint32_t)in->imm;

        if (vt_is_ref(f->m->globals[g].type)) { clear = false; break; }
        bool seen = false;
        for (uint32_t j = 0; j < ngset; j++) if (gset[j] == g) seen = true;
        if (seen) continue;
        if (ngset == GL_MAX_GLOBALS) { clear = false; break; }
        gtype[ngset] = f->m->globals[g].type;
        gset[ngset++] = g;
      }
    }
    if (warmc_getenv("WARMC_GL_DEBUG"))
      fprintf(stderr, "gl %s b%u: clear=%d ngset=%u callmask=%llx\n",
              f->m->funcs[f->func_index].sym, hb, (int)clear, ngset,
              (unsigned long long)callmask);
    if (!clear || !ngset) continue;
    uint32_t before = f->nblocks;
    for (uint32_t j = 0; j < ngset; j++) {

      if (gset[j] >= FX_GBITS || (callmask & ((uint64_t)1 << gset[j]))) continue;
      if (gl_promote_one(f, hb, pre, inloop, gset[j], gtype[j], no_throw))
        changed = true;

      if (f->nblocks > cap) {
        uint32_t grown = cap;
        cap = f->nblocks;
        inloop = xrealloc(inloop, cap);
        stack = xrealloc(stack, cap * sizeof(uint32_t));
        memset(inloop + grown, 0, cap - grown);
      }
    }

    if (f->nblocks != before) ir_order(f);
  }
  xfree(inloop); xfree(stack);
  return changed;
}

static uint32_t *lc_def_blocks(IrFunc *f) {
  uint32_t *db = xmalloc((f->ninsts ? f->ninsts : 1) * sizeof(uint32_t));
  for (uint32_t i = 0; i < f->ninsts; i++) db[i] = REF_NONE;
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    for (uint32_t p = 0; p < b->nparams; p++) db[b->params[p]] = bi;
    for (uint32_t i = 0; i < b->ninsts; i++) db[b->insts[i]] = bi;
  }
  return db;
}

static void lc_rewrite_outside(IrFunc *f, const uint8_t *inloop, Ref from, Ref to) {
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    if (inloop[bi]) continue;
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      Ref *a = ir_args(f, b->insts[i]);
      for (uint32_t j = 0; j < in->nargs; j++) if (a[j] == from) a[j] = to;
    }
    if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == from)
      b->term_arg = to;
    for (uint32_t i = 0; i < b->nret; i++) if (b->ret_args[i] == from) b->ret_args[i] = to;
    for (uint32_t sc = 0; sc < b->nsuccs; sc++)
      for (uint32_t j = 0; j < b->succs[sc].nargs; j++)
        if (b->succs[sc].args[j] == from) b->succs[sc].args[j] = to;
  }
}

static bool lc_block_uses(IrFunc *f, uint32_t bi, Ref v) {
  Block *b = &f->blocks[bi];
  for (uint32_t i = 0; i < b->ninsts; i++) {
    Inst *in = &f->insts[b->insts[i]];
    Ref *a = ir_args(f, b->insts[i]);
    for (uint32_t j = 0; j < in->nargs; j++) if (a[j] == v) return true;
  }
  if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == v) return true;
  for (uint32_t i = 0; i < b->nret; i++) if (b->ret_args[i] == v) return true;
  for (uint32_t sc = 0; sc < b->nsuccs; sc++)
    for (uint32_t j = 0; j < b->succs[sc].nargs; j++)
      if (b->succs[sc].args[j] == v) return true;
  return false;
}

static void lc_reach_without(IrFunc *f, uint32_t from, uint32_t si,
                             uint8_t *reach, uint32_t *stack) {
  memset(reach, 0, f->nblocks);
  uint32_t sp = 0;
  reach[f->entry] = 1;
  stack[sp++] = f->entry;
  while (sp) {
    uint32_t bi = stack[--sp];
    Block *b = &f->blocks[bi];
    for (uint32_t sc = 0; sc < b->nsuccs; sc++) {
      if (bi == from && sc == si) continue;
      uint32_t t = b->succs[sc].target;
      if (!reach[t]) { reach[t] = 1; stack[sp++] = t; }
    }
  }
}

static bool lc_bare_trap(IrFunc *f, uint32_t bi) {
  Block *b = &f->blocks[bi];
  return b->term == T_TRAP && b->ninsts == 0 && b->nret == 0 && b->nsuccs == 0;
}

/* Use-site index: for every value, the blocks that read it, in ascending
   block order.  Built once per pass and rebuilt only when a split adds a
   block, it turns "does anything outside the loop read this param" from a
   walk over every instruction in the function -- once per carried param
   per exit edge, the term that made a 8000-loop function quadratic -- into
   a walk over the value's own uses.  A rewrite can stale an entry, so
   every hit is re-verified against the block before it counts. */
typedef struct { uint32_t *off, *blk, nrefs, nblocks; } LcUses;

static void lc_uses_free(LcUses *u) {
  xfree(u->off);
  xfree(u->blk);
  u->off = NULL; u->blk = NULL;
}

static void lc_uses_build(IrFunc *f, LcUses *u) {
  lc_uses_free(u);
  u->nrefs = f->ninsts;
  u->nblocks = f->nblocks;
  uint32_t *cnt = xcalloc(u->nrefs + 1, sizeof(uint32_t));
#define LC_EACH_USE(stmt) \
  for (uint32_t bi_ = 0; bi_ < f->nblocks; bi_++) { \
    Block *b_ = &f->blocks[bi_]; \
    for (uint32_t i_ = 0; i_ < b_->ninsts; i_++) { \
      Inst *in_ = &f->insts[b_->insts[i_]]; \
      Ref *a_ = ir_args(f, b_->insts[i_]); \
      for (uint32_t j_ = 0; j_ < in_->nargs; j_++) { Ref r_ = a_[j_]; stmt; } \
    } \
    if (b_->term == T_BRIF || b_->term == T_BRTABLE) { \
      Ref r_ = b_->term_arg; stmt; \
    } \
    for (uint32_t i_ = 0; i_ < b_->nret; i_++) { Ref r_ = b_->ret_args[i_]; stmt; } \
    for (uint32_t sc_ = 0; sc_ < b_->nsuccs; sc_++) \
      for (uint32_t j_ = 0; j_ < b_->succs[sc_].nargs; j_++) { \
        Ref r_ = b_->succs[sc_].args[j_]; stmt; \
      } \
  }
  LC_EACH_USE(if (r_ < u->nrefs) cnt[r_ + 1]++);
  for (uint32_t i = 0; i < u->nrefs; i++) cnt[i + 1] += cnt[i];
  u->off = cnt;
  u->blk = xmalloc((cnt[u->nrefs] ? cnt[u->nrefs] : 1) * sizeof(uint32_t));
  uint32_t *fill = xcalloc(u->nrefs ? u->nrefs : 1, sizeof(uint32_t));
  LC_EACH_USE(if (r_ < u->nrefs) u->blk[u->off[r_] + fill[r_]++] = bi_);
#undef LC_EACH_USE
  xfree(fill);
}

#define LC_MAX_PARAMS 16

static bool pass_loop_close(IrFunc *f) {
  bool changed = false;
  uint32_t cap = f->nblocks;
  uint8_t *inloop = xcalloc(cap, 1);
  uint8_t *reach = xmalloc(cap);
  uint32_t *stack = xmalloc(cap * sizeof(uint32_t));
  uint32_t *lblk = xmalloc(cap * sizeof(uint32_t));
  uint32_t *db = lc_def_blocks(f);
  uint32_t dbn = f->ninsts;
  LcUses uses = {0};
  lc_uses_build(f, &uses);
  uint32_t nb0 = f->nblocks;
  for (uint32_t hb = 0; hb < nb0; hb++) {
    if (f->nblocks > cap) {
      uint32_t grown = cap;
      cap = f->nblocks;
      inloop = xrealloc(inloop, cap);
      reach = xrealloc(reach, cap);
      stack = xrealloc(stack, cap * sizeof(uint32_t));
      lblk = xrealloc(lblk, cap * sizeof(uint32_t));
      memset(inloop + grown, 0, cap - grown);
    }
    if (f->ninsts != dbn) {
      xfree(db);
      db = lc_def_blocks(f);
      dbn = f->ninsts;
    }
    if (f->nblocks != uses.nblocks) lc_uses_build(f, &uses);
    Block *h = &f->blocks[hb];
    if (!h->reachable || h->is_pad || !h->nparams) continue;
    uint32_t nfwd = 0, nback = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      if (f->blocks[h->preds[p]].rpo >= h->rpo) nback++;
      else nfwd++;
    }
    if (!nback || nfwd != 1) continue;

    /* inloop is all-zero here; the flood records what it marks so the
       unmark at the bottom is the loop's size, not the function's. */
    uint32_t nlbl = 0;
    inloop[hb] = 1;
    lblk[nlbl++] = hb;
    uint32_t sp = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo && !inloop[pb]) {
        inloop[pb] = 1; lblk[nlbl++] = pb; stack[sp++] = pb;
      }
    }
    while (sp) {
      Block *c = &f->blocks[stack[--sp]];
      for (uint32_t p = 0; p < c->npreds; p++)
        if (!inloop[c->preds[p]]) {
          inloop[c->preds[p]] = 1;
          lblk[nlbl++] = c->preds[p];
          stack[sp++] = c->preds[p];
        }
    }
    if (inloop[f->entry]) {
      for (uint32_t i = 0; i < nlbl; i++) inloop[lblk[i]] = 0;
      continue;
    }

    Ref carried[LC_MAX_PARAMS];
    uint32_t ncarried = 0;
    for (uint32_t pi = 0; pi < f->blocks[hb].nparams && ncarried < LC_MAX_PARAMS;
         pi++) {
      Ref p = f->blocks[hb].params[pi];
      for (uint32_t k = 0; k < f->blocks[hb].npreds; k++) {
        uint32_t pb = f->blocks[hb].preds[k];
        if (f->blocks[pb].rpo < f->blocks[hb].rpo) continue;
        Block *sb = &f->blocks[pb];
        bool got = false;
        for (uint32_t sc = 0; sc < sb->nsuccs && !got; sc++) {
          if (sb->succs[sc].target != hb || pi >= sb->succs[sc].nargs) continue;
          Ref arg = sb->succs[sc].args[pi];
          if (arg != p && arg < f->ninsts && db[arg] != REF_NONE && inloop[db[arg]])
            got = true;
        }
        if (got) { carried[ncarried++] = p; break; }
      }
    }
    if (ncarried) {
      /* Exit edges in ascending block order, exactly as the full sweep
         visited them -- the transform order decides value numbering. */
      qsort(lblk, nlbl, sizeof(uint32_t), lc_u32_cmp);
      for (uint32_t li = 0; li < nlbl; li++) {
        uint32_t xb = lblk[li];
        for (uint32_t xs = 0; xs < f->blocks[xb].nsuccs; xs++) {
          uint32_t t0 = f->blocks[xb].succs[xs].target;
          if (inloop[t0] || lc_bare_trap(f, t0)) continue;

          if (f->blocks[xb].succs[xs].eh) continue;

          /* Any outside use at all?  The reachability flood is pure
             scratch, so it can wait until a candidate exists -- which on
             a loop-dense function is almost never. */
          bool any_outside = false;
          for (uint32_t ci = 0; ci < ncarried && !any_outside; ci++) {
            Ref p = carried[ci];
            if (p >= uses.nrefs) continue;
            for (uint32_t k = uses.off[p]; k < uses.off[p + 1]; k++) {
              uint32_t bi = uses.blk[k];
              if (inloop[bi] || !f->blocks[bi].reachable) continue;
              if (!lc_block_uses(f, bi, p)) continue;   /* staled entry */
              any_outside = true;
              break;
            }
          }
          if (!any_outside) continue;

          lc_reach_without(f, xb, xs, reach, stack);

          Ref want[LC_MAX_PARAMS];
          uint32_t nwant = 0;
          for (uint32_t ci = 0; ci < ncarried; ci++) {
            Ref p = carried[ci];
            bool outside = false, safe = true;
            if (p >= uses.nrefs) continue;
            for (uint32_t k = uses.off[p]; k < uses.off[p + 1] && safe; k++) {
              uint32_t bi = uses.blk[k];
              if (inloop[bi] || !f->blocks[bi].reachable) continue;
              if (!lc_block_uses(f, bi, p)) continue;   /* staled entry */
              outside = true;
              if (reach[bi]) safe = false;
            }
            if (outside && safe) want[nwant++] = p;
          }
          if (!nwant) continue;

          uint32_t nb;
          if (f->blocks[t0].npreds == 1 && !f->blocks[t0].is_pad) nb = t0;
          else nb = gl_split_edge(f, xb, xs);
          if (f->nblocks > cap) {
            uint32_t grown = cap;
            cap = f->nblocks;
            inloop = xrealloc(inloop, cap);
            reach = xrealloc(reach, cap);
            stack = xrealloc(stack, cap * sizeof(uint32_t));
            lblk = xrealloc(lblk, cap * sizeof(uint32_t));
            memset(inloop + grown, 0, cap - grown);
            memset(reach + grown, 0, cap - grown);
          }
          /* A split copied the edge's args into a new block; the index
             must see those uses before this loop's next exit edge. */
          if (f->nblocks != uses.nblocks) lc_uses_build(f, &uses);
          for (uint32_t i = 0; i < nwant; i++) {
            Ref q = ir_add_param(f, nb, (ValType)f->insts[want[i]].type);
            edge_push_arg_at(f, xb, xs, want[i]);
            lc_rewrite_outside(f, inloop, want[i], q);

            g_stats.loops_closed++;
            changed = true;
          }
        }
      }
    }
    for (uint32_t i = 0; i < nlbl; i++) inloop[lblk[i]] = 0;
  }
  xfree(inloop); xfree(stack); xfree(reach); xfree(lblk); xfree(db);
  lc_uses_free(&uses);
  return changed;
}

static uint32_t lh_preheader(IrFunc *f, uint32_t hb, uint32_t pre) {
  Block *p = &f->blocks[pre];
  if (p->term == T_JUMP && p->nsuccs == 1 && p->succs[0].target == hb &&
      !ir_block_has_eh(f, pre))
    return pre;
  uint32_t s = REF_NONE;
  for (uint32_t i = 0; i < p->nsuccs; i++) {
    if (p->succs[i].target != hb) continue;
    if (s != REF_NONE) return REF_NONE;
    s = i;
  }
  if (s == REF_NONE || p->succs[s].eh) return REF_NONE;
  uint32_t nargs = p->succs[s].nargs;
  Ref *args = NULL;
  if (nargs) {
    args = xmalloc(nargs * sizeof(Ref));
    memcpy(args, p->succs[s].args, nargs * sizeof(Ref));
  }
  uint32_t ph = ir_new_block(f);
  f->blocks[ph].term = T_JUMP;
  ir_add_edge(f, ph, hb, args, nargs);
  xfree(args);
  f->blocks[pre].succs[s].target = ph;
  f->blocks[pre].succs[s].nargs = 0;
  f->blocks[ph].reachable = true;
  return ph;
}

/* The chain of pure instructions in the loop that v is computed by, all of
   which can run before the loop instead.  Pure is not invariant for what
   reads state: a mutable global's value is the loop's own when the loop sets
   it, or calls anything that may (gset is null then, or marks the globals the
   loop sets) -- wasm-smith's fuel counter is read, tested and decremented on
   every way round, and hoisting its test made the loop run forever -- and a
   vector load reads memory the loop may store. */
static bool lh_collect(IrFunc *f, Ref v, const uint8_t *inloop, uint8_t *moved,
                       uint32_t *out, uint32_t *nout, uint32_t cap, int depth,
                       const uint8_t *gset) {
  if (!inloop[f->insts[v].block]) return true;
  if (depth > 16) return false;
  Inst *in = &f->insts[v];
  if (in->op == I_PARAM || !ir_is_pure(in) || in->op == I_VLOAD) return false;
  if (in->op == I_GLOBAL_GET && in->imm < f->m->nglobals &&
      f->m->globals[in->imm].mutable_ && (!gset || gset[in->imm]))
    return false;
  if (moved[v]) return true;
  Ref *a = ir_args(f, v);
  for (uint32_t i = 0; i < in->nargs; i++)
    if (!lh_collect(f, a[i], inloop, moved, out, nout, cap, depth + 1, gset))
      return false;
  if (*nout == cap) return false;
  moved[v] = 1;
  out[(*nout)++] = v;
  return true;
}

static bool lh_can_trap(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  switch (in->op) {
    case I_DIVS: case I_DIVU: case I_REMS: case I_REMU: {

      if (in->nargs != 2) return true;
      Inst *d = &f->insts[ir_args(f, v)[1]];
      if (d->op != I_CONST) return true;
      uint64_t c = d->imm;
      if (!vt_is_64((ValType)in->type)) c &= 0xffffffffu;
      if (c == 0) return true;
      if (in->op == I_DIVS || in->op == I_REMS) {
        uint64_t neg1 = vt_is_64((ValType)in->type) ? ~(uint64_t)0 : 0xffffffffu;
        if (c == neg1) return true;
      }
      return false;
    }
    case I_LOAD: case I_STORE:

      return (in->imm2 & MEMF_NOCHECK) == 0;
    default:
      return stops_block(in);
  }
}

static bool lh_same_place(IrFunc *f, Ref aa, uint64_t ia, uint32_t sa, uint32_t ma,
                          Ref ab, uint64_t ib, uint32_t sb, uint32_t mb) {
  if (sa != sb || ma != mb) return false;
  Ref ra, rb;
  int64_t oa, ob;
  access_root(f, aa, &ra, &oa);
  access_root(f, ab, &rb, &ob);
  return ra == rb && oa + (int64_t)ia == ob + (int64_t)ib;
}

#define LH_MAX_PROLOGUE 8

static uint32_t lh_prologue(IrFunc *f, uint32_t pre, uint32_t *out) {
  uint32_t n = 0, cur = pre;
  out[n++] = cur;
  while (n < LH_MAX_PROLOGUE) {
    Block *c = &f->blocks[cur];
    if (c->npreds != 1) break;
    uint32_t pb = c->preds[0];
    if (ir_block_has_eh(f, pb)) break;
    bool straight = true;
    for (uint32_t i = 0; i < f->blocks[pb].nsuccs; i++) {
      uint32_t t = f->blocks[pb].succs[i].target;
      if (t == cur) continue;
      if (f->blocks[t].term != T_TRAP) { straight = false; break; }
    }
    if (!straight) break;
    out[n++] = pb;
    cur = pb;
  }

  for (uint32_t i = 0, j = n - 1; i < j; i++, j--) {
    uint32_t t = out[i]; out[i] = out[j]; out[j] = t;
  }
  return n;
}

static bool lh_kill_redundant(IrFunc *f, uint32_t hb, uint32_t pre,
                              const uint32_t *lblk, uint32_t nlbl) {
  (void)hb;
  bool changed = false;
  uint32_t pro[LH_MAX_PROLOGUE];
  uint32_t npro = lh_prologue(f, pre, pro);

  for (uint32_t li = 0; li < nlbl; li++) {
    uint32_t bi = lblk[li];
    Block *b = &f->blocks[bi];
    if (ir_block_has_eh(f, bi)) return false;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      uint32_t op = f->insts[b->insts[i]].op;
      if (gl_opaque(op) || lh_writes_mem(op)) return false;
    }
  }
  for (uint32_t li = 0; li < nlbl; li++) {
    uint32_t bi = lblk[li];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];
      Inst *in = &f->insts[r];
      if (in->op != I_STORE) continue;
      Ref *a = ir_args(f, r);
      uint32_t sz = (uint32_t)MEMF_SIZE(in->imm2), mem = MEMF_MEM(in->imm2);

      bool established = false;
      for (uint32_t pi = 0; pi < npro; pi++) {
        Block *p = &f->blocks[pro[pi]];
        for (uint32_t j = 0; j < p->ninsts; j++) {
          Ref o = p->insts[j];
          Inst *oi = &f->insts[o];
          if (gl_opaque(oi->op) || lh_writes_mem(oi->op)) {
            established = false;
            continue;
          }
          if (oi->op != I_STORE && oi->op != I_VSTORE) continue;
          if (oi->op == I_STORE && ir_args(f, o)[1] == a[1] &&
              lh_same_place(f, a[0], in->imm, sz, mem, ir_args(f, o)[0], oi->imm,
                            (uint32_t)MEMF_SIZE(oi->imm2), MEMF_MEM(oi->imm2))) {
            established = true;
            continue;
          }
          if (may_alias(f, a[0], in->imm, sz, mem, ir_args(f, o)[0], oi->imm,
                        (uint32_t)MEMF_SIZE(oi->imm2), MEMF_MEM(oi->imm2)))
            established = false;
        }
      }
      if (!established) continue;

      bool clear = true;
      for (uint32_t cl = 0; cl < nlbl && clear; cl++) {
        uint32_t cj = lblk[cl];
        Block *c = &f->blocks[cj];
        for (uint32_t k = 0; k < c->ninsts; k++) {
          Ref o = c->insts[k];
          if (o == r) continue;
          Inst *oi = &f->insts[o];
          if (oi->op != I_STORE && oi->op != I_VSTORE) continue;
          if (!may_alias(f, a[0], in->imm, sz, mem, ir_args(f, o)[0], oi->imm,
                         (uint32_t)MEMF_SIZE(oi->imm2), MEMF_MEM(oi->imm2)))
            continue;
          if (oi->op == I_STORE && ir_args(f, o)[1] == a[1] &&
              lh_same_place(f, a[0], in->imm, sz, mem, ir_args(f, o)[0], oi->imm,
                            (uint32_t)MEMF_SIZE(oi->imm2), MEMF_MEM(oi->imm2)))
            continue;
          clear = false; break;
        }
      }
      if (!clear) {
        if (warmc_getenv("WARMC_LH_DEBUG"))
          fprintf(stderr, "lh   store v%u established but clobbered in loop\n", r);
        continue;
      }
      if (warmc_getenv("WARMC_LH_DEBUG"))
        fprintf(stderr, "lh   store v%u already made in b%u\n", r, pre);
      in->op = I_NOP;
      in->nargs = 0;
      g_stats.stores_removed++;
      changed = true;
    }
  }
  return changed;
}

#define LH_MAX_STORES 8
#define LH_MAX_CHAIN 32

/* Whether the trap block a header's invariant guard leaves for can be
   reached from before the loop instead.  It reads what it reads where the
   guard first held, and an invariant guard that holds at all holds on the
   first iteration: so the header's parameters stand for what the preheader
   passes in -- Rust's bounds-check panic reports the index -- and nothing
   else from inside the loop may be read there.  The header must be its
   only way in, or another path would see the substitution too. */
static bool lh_trap_reads_ok(IrFunc *f, uint32_t tb, uint32_t hb,
                             const uint8_t *inloop) {
  Block *t = &f->blocks[tb];
  if (t->npreds != 1 || t->preds[0] != hb) return false;
  for (uint32_t i = 0; i < t->ninsts; i++) {
    Ref v = t->insts[i];
    Ref *a = ir_args(f, v);
    for (uint32_t j = 0; j < f->insts[v].nargs; j++) {
      uint32_t db = f->insts[a[j]].block;
      if (db == tb || !inloop[db]) continue;
      if (f->insts[a[j]].op == I_PARAM && db == hb) continue;
      return false;
    }
  }
  return true;
}

static bool lh_merge_down(IrFunc *f, uint32_t hb) {
  Block *b = &f->blocks[hb];
  if (b->term != T_JUMP || b->nsuccs != 1 || ir_block_has_eh(f, hb)) return false;
  uint32_t t = b->succs[0].target;
  if (t == hb) return false;
  Block *tb = &f->blocks[t];
  if (tb->npreds != 1 || tb->preds[0] != hb || tb->nparams || tb->is_pad)
    return false;

  if (b->succs[0].nargs) return false;
  for (uint32_t i = 0; i < tb->ninsts; i++) {
    if (b->ninsts == b->cap_insts) {
      uint32_t oc_ = b->cap_insts;
      b->cap_insts = b->cap_insts ? b->cap_insts * 2 : 8;
      b->insts = ir_grow(f, b->insts, (size_t)oc_ * sizeof(uint32_t),
                           (size_t)b->cap_insts * sizeof(uint32_t));
    }
    b->insts[b->ninsts++] = tb->insts[i];
    f->insts[tb->insts[i]].block = hb;
  }
  b->nsuccs = 0;
  b->term = tb->term;
  b->term_arg = tb->term_arg;
  b->nret = tb->nret;
  b->ret_args = tb->ret_args;
  tb->ret_args = NULL; tb->nret = 0;
  for (uint32_t i = 0; i < tb->nsuccs; i++)
    ir_add_edge(f, hb, tb->succs[i].target, tb->succs[i].args,
                tb->succs[i].nargs);
  tb->nsuccs = 0;
  tb->ninsts = 0;
  tb->term = T_NONE;
  tb->reachable = false;
  return true;
}

static bool pass_loop_hoist(IrFunc *f) {
  bool changed = false;

  uint32_t cap = f->nblocks + 1;
  uint8_t *inloop = xcalloc(cap, 1);
  uint32_t *stack = xmalloc(cap * sizeof(uint32_t));
  uint32_t *lblk = xmalloc(cap * sizeof(uint32_t));
  uint32_t mcap = f->ninsts ? f->ninsts : 1;
  uint8_t *moved = xcalloc(mcap, 1);
  uint8_t *gsetbuf = xcalloc(f->m->nglobals + 1, 1);
  uint32_t nb = f->nblocks;
  for (uint32_t hb = 0; hb < nb; hb++) {
    if (f->nblocks >= cap) {
      uint32_t grown = cap;
      cap = f->nblocks + 1;
      inloop = xrealloc(inloop, cap);
      stack = xrealloc(stack, cap * sizeof(uint32_t));
      lblk = xrealloc(lblk, cap * sizeof(uint32_t));
      memset(inloop + grown, 0, cap - grown);
    }
    if (f->ninsts > mcap) {
      xfree(moved);
      mcap = f->ninsts;
      moved = xcalloc(mcap, 1);
    }
    Block *h = &f->blocks[hb];
    if (!h->reachable || h->is_pad) continue;
    uint32_t pre = REF_NONE, nfwd = 0, nback = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      if (f->blocks[h->preds[p]].rpo >= h->rpo) nback++;
      else { nfwd++; pre = h->preds[p]; }
    }
    if (!nback || nfwd != 1) continue;

    /* inloop is all-zero here; the flood records what it marks so the
       unmark at the bottom costs the loop, not the function.  Sorted so
       every scan below visits blocks in the order the full sweep did. */
    uint32_t nlbl = 0;
    inloop[hb] = 1;
    lblk[nlbl++] = hb;
    uint32_t sp = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo && !inloop[pb]) {
        inloop[pb] = 1; lblk[nlbl++] = pb; stack[sp++] = pb;
      }
    }
    while (sp) {
      Block *c = &f->blocks[stack[--sp]];
      for (uint32_t p = 0; p < c->npreds; p++)
        if (!inloop[c->preds[p]]) {
          inloop[c->preds[p]] = 1;
          lblk[nlbl++] = c->preds[p];
          stack[sp++] = c->preds[p];
        }
    }
    /* A cycle with a second way in floods out of itself, and hoisting
       into the preheader would then take values from before the loop. */
    if (inloop[f->entry]) {
      for (uint32_t i = 0; i < nlbl; i++) inloop[lblk[i]] = 0;
      continue;
    }
    qsort(lblk, nlbl, sizeof(uint32_t), lc_u32_cmp);

    bool has_call = false;
    for (uint32_t li = 0; li < nlbl && !has_call; li++) {
      uint32_t bi = lblk[li];
      Block *b = &f->blocks[bi];
      if (ir_block_has_eh(f, bi)) { has_call = true; break; }
      for (uint32_t i = 0; i < b->ninsts; i++) {
        uint32_t op = f->insts[b->insts[i]].op;
        if (gl_opaque(op) || lh_writes_mem(op)) { has_call = true; break; }
      }
    }
    /* the globals the loop sets, when no call could set any other */
    const uint8_t *gset = NULL;
    if (!has_call) {
      memset(gsetbuf, 0, f->m->nglobals + 1);
      for (uint32_t li = 0; li < nlbl; li++) {
        Block *b = &f->blocks[lblk[li]];
        for (uint32_t i = 0; i < b->ninsts; i++) {
          Inst *gi = &f->insts[b->insts[i]];
          if (gi->op == I_GLOBAL_SET && gi->imm < f->m->nglobals) gsetbuf[gi->imm] = 1;
        }
      }
      gset = gsetbuf;
    }

    if (lh_kill_redundant(f, hb, pre, lblk, nlbl)) changed = true;

    uint32_t mv[LH_MAX_STORES], nmv = 0;
    uint32_t chain[LH_MAX_CHAIN], nchain = 0;
    if (!has_call) {
      h = &f->blocks[hb];
      for (uint32_t i = 0; i < h->ninsts && nmv < LH_MAX_STORES; i++) {
        Ref r = h->insts[i];
        Inst *in = &f->insts[r];
        if (in->op != I_STORE) continue;
        Ref *a = ir_args(f, r);
        uint32_t save = nchain;
        if (!lh_collect(f, a[0], inloop, moved, chain, &nchain, LH_MAX_CHAIN, 0, gset) ||
            !lh_collect(f, a[1], inloop, moved, chain, &nchain, LH_MAX_CHAIN, 0, gset)) {
          for (uint32_t k = save; k < nchain; k++) moved[chain[k]] = 0;
          nchain = save;
          continue;
        }
        uint32_t sz = (uint32_t)MEMF_SIZE(in->imm2), mem = MEMF_MEM(in->imm2);

        bool clear = true;
        for (uint32_t li = 0; li < nlbl && clear; li++) {
          uint32_t bi = lblk[li];
          Block *b = &f->blocks[bi];
          for (uint32_t j = 0; j < b->ninsts; j++) {
            Ref o = b->insts[j];
            if (o == r) continue;
            Inst *oi = &f->insts[o];
            if (oi->op != I_STORE && oi->op != I_VSTORE) continue;
            bool ours = false;
            for (uint32_t k = 0; k < nmv; k++) if (mv[k] == o) ours = true;
            if (ours) continue;
            if (!may_alias(f, a[0], in->imm, sz, mem, ir_args(f, o)[0], oi->imm,
                           (uint32_t)MEMF_SIZE(oi->imm2), MEMF_MEM(oi->imm2)))
              continue;

            if (oi->op == I_STORE && ir_args(f, o)[1] == a[1] &&
                lh_same_place(f, a[0], in->imm, sz, mem, ir_args(f, o)[0],
                              oi->imm, (uint32_t)MEMF_SIZE(oi->imm2),
                              MEMF_MEM(oi->imm2))) {
              continue;
            }
            if (warmc_getenv("WARMC_LH_DEBUG"))
              fprintf(stderr, "lh   store v%u blocked by store v%u\n", r, o);
            clear = false; break;
          }
        }

        if (clear) {
          Block *hh = &f->blocks[hb];
          for (uint32_t j = 0; j < hh->ninsts && clear; j++) {
            Ref o = hh->insts[j];
            if (o == r) break;
            uint32_t op = f->insts[o].op;
            if (op != I_LOAD && op != I_STORE && op != I_VLOAD && op != I_VSTORE &&
                !lh_can_trap(f, o))
              continue;
            bool ours = false;
            for (uint32_t k = 0; k < nmv; k++) if (mv[k] == o) ours = true;
            if (!ours) {
              if (warmc_getenv("WARMC_LH_DEBUG"))
                fprintf(stderr, "lh   store v%u has memory ahead of it (v%u)\n", r, o);
              clear = false;
            }
          }
        }
        if (clear) mv[nmv++] = r;
        else { for (uint32_t k = save; k < nchain; k++) moved[chain[k]] = 0;
               nchain = save; }
      }
    }

    uint32_t trap_s = REF_NONE, keep_s = REF_NONE;
    h = &f->blocks[hb];
    uint32_t gsave = nchain;

    bool guard_clear = true;
    for (uint32_t i = 0; i < h->ninsts; i++) {
      if (!lh_can_trap(f, h->insts[i])) continue;
      bool ours = false;
      for (uint32_t k = 0; k < nmv; k++) if (mv[k] == h->insts[i]) ours = true;
      if (!ours) { guard_clear = false; break; }
    }
    if (guard_clear && h->term == T_BRIF && h->nsuccs == 2 &&
        lh_collect(f, h->term_arg, inloop, moved, chain, &nchain,
                   LH_MAX_CHAIN, 0, gset)) {
      for (uint32_t i = 0; i < 2; i++) {
        uint32_t t = h->succs[i].target;
        if (!inloop[t] && f->blocks[t].term == T_TRAP && !h->succs[i].eh &&
            lh_trap_reads_ok(f, t, hb, inloop))
          trap_s = i;
        else if (inloop[t]) keep_s = i;
      }
      if (trap_s == REF_NONE || keep_s == REF_NONE) trap_s = keep_s = REF_NONE;
    }
    if (trap_s == REF_NONE) {
      for (uint32_t k = gsave; k < nchain; k++) moved[chain[k]] = 0;
      nchain = gsave;
    }
    if (warmc_getenv("WARMC_LH_DEBUG"))
      fprintf(stderr, "lh %s b%u: call=%d stores=%u guard=%d term=%d\n",
              f->m->funcs[f->func_index].sym, hb, (int)has_call, nmv,
              trap_s != REF_NONE, (int)f->blocks[hb].term);
    if (!nmv && trap_s == REF_NONE) {
      for (uint32_t k = 0; k < nchain; k++) moved[chain[k]] = 0;
      for (uint32_t i = 0; i < nlbl; i++) inloop[lblk[i]] = 0;
      continue;
    }

    uint32_t ph = lh_preheader(f, hb, pre);
    if (ph == REF_NONE) {
      for (uint32_t k = 0; k < nchain; k++) moved[chain[k]] = 0;
      for (uint32_t i = 0; i < nlbl; i++) inloop[lblk[i]] = 0;
      continue;
    }
    h = &f->blocks[hb];
    Block *p = &f->blocks[ph];

    for (uint32_t k = 0; k < nchain; k++) {
      if (p->ninsts == p->cap_insts) {
        uint32_t oc_ = p->cap_insts;
        p->cap_insts = p->cap_insts ? p->cap_insts * 2 : 8;
        p->insts = ir_grow(f, p->insts, (size_t)oc_ * sizeof(uint32_t),
                             (size_t)p->cap_insts * sizeof(uint32_t));
      }
      p->insts[p->ninsts++] = chain[k];
      f->insts[chain[k]].block = ph;
    }
    if (nchain) {
      Block *hh = &f->blocks[hb];
      uint32_t k = 0;
      for (uint32_t i = 0; i < hh->ninsts; i++)
        if (!moved[hh->insts[i]]) hh->insts[k++] = hh->insts[i];
      hh->ninsts = k;

      for (uint32_t li = 0; li < nlbl; li++) {
        uint32_t bi = lblk[li];
        if (bi == hb) continue;
        Block *b = &f->blocks[bi];
        uint32_t j = 0;
        for (uint32_t i = 0; i < b->ninsts; i++)
          if (!moved[b->insts[i]]) b->insts[j++] = b->insts[i];
        b->ninsts = j;
      }
      changed = true;
    }
    h = &f->blocks[hb];
    p = &f->blocks[ph];
    for (uint32_t k = 0; k < nmv; k++) {
      if (p->ninsts == p->cap_insts) {
        uint32_t oc_ = p->cap_insts;
        p->cap_insts = p->cap_insts ? p->cap_insts * 2 : 8;
        p->insts = ir_grow(f, p->insts, (size_t)oc_ * sizeof(uint32_t),
                             (size_t)p->cap_insts * sizeof(uint32_t));
      }
      p->insts[p->ninsts++] = mv[k];
      f->insts[mv[k]].block = ph;
    }
    if (nmv) {
      uint32_t k = 0;
      for (uint32_t i = 0; i < h->ninsts; i++) {
        bool moved = false;
        for (uint32_t j = 0; j < nmv; j++) if (mv[j] == h->insts[i]) moved = true;
        if (!moved) h->insts[k++] = h->insts[i];
      }
      h->ninsts = k;
      changed = true;
    }

    if (trap_s != REF_NONE && f->blocks[ph].term == T_JUMP) {
      uint32_t tb = h->succs[trap_s].target;
      Ref cond = h->term_arg;
      uint32_t keep_t = h->succs[keep_s].target;
      uint32_t knargs = h->succs[keep_s].nargs;
      Ref *kargs = NULL;
      if (knargs) {
        kargs = xmalloc(knargs * sizeof(Ref));
        memcpy(kargs, h->succs[keep_s].args, knargs * sizeof(Ref));
      }

      /* the header's parameters, as the preheader passes them in */
      {
        Block *tbb = &f->blocks[tb];
        Edge *pe = &f->blocks[ph].succs[0];
        for (uint32_t i = 0; i < tbb->ninsts; i++) {
          Ref *a = ir_args(f, tbb->insts[i]);
          for (uint32_t j = 0; j < f->insts[tbb->insts[i]].nargs; j++)
            for (uint32_t q = 0; q < h->nparams && q < pe->nargs; q++)
              if (a[j] == h->params[q]) { a[j] = pe->args[q]; break; }
        }
      }
      f->blocks[ph].term = T_BRIF;
      f->blocks[ph].term_arg = cond;
      if (trap_s == 0) {
        ir_add_edge(f, ph, tb, NULL, 0);
        Edge tmp = f->blocks[ph].succs[0];
        f->blocks[ph].succs[0] = f->blocks[ph].succs[1];
        f->blocks[ph].succs[1] = tmp;
      } else {
        ir_add_edge(f, ph, tb, NULL, 0);
      }

      h = &f->blocks[hb];
      h->nsuccs = 0;
      h->term = T_JUMP;
      h->term_arg = REF_NONE;
      ir_add_edge(f, hb, keep_t, kargs, knargs);
      xfree(kargs);

      uint32_t p2 = ir_new_block(f);
      f->blocks[p2].term = T_JUMP;
      f->blocks[p2].reachable = true;
      {
        uint32_t es = REF_NONE;
        for (uint32_t i = 0; i < f->blocks[ph].nsuccs; i++)
          if (f->blocks[ph].succs[i].target == hb) { es = i; break; }
        uint32_t na = es == REF_NONE ? 0 : f->blocks[ph].succs[es].nargs;
        Ref *aa = NULL;
        if (na) {
          aa = xmalloc(na * sizeof(Ref));
          memcpy(aa, f->blocks[ph].succs[es].args, na * sizeof(Ref));
        }
        ir_add_edge(f, p2, hb, aa, na);
        xfree(aa);
        if (es != REF_NONE) {
          f->blocks[ph].succs[es].target = p2;
          f->blocks[ph].succs[es].nargs = 0;
        }
      }
      changed = true;
    }
    for (uint32_t k = 0; k < nchain; k++) moved[chain[k]] = 0;
    for (uint32_t i = 0; i < nlbl; i++) inloop[lblk[i]] = 0;
  }
  xfree(inloop); xfree(stack); xfree(lblk); xfree(moved); xfree(gsetbuf);
  if (changed) { ir_recompute_preds(f); ir_order(f); }
  return changed;
}

static uint32_t nback_of(IrFunc *f, uint32_t hb) {
  uint32_t n = 0;
  Block *h = &f->blocks[hb];
  for (uint32_t p = 0; p < h->npreds; p++)
    if (f->blocks[h->preds[p]].rpo >= h->rpo) n++;
  return n;
}

#define ROT_MAX_ESCAPES 64

static uint32_t g_rot_esc_cap;

void opt_init_tunables(void) {
  const char *e = warmc_getenv("WARMC_ROT_ESC");
  g_rot_esc_cap = e ? (uint32_t)atoi(e) : 16;
  if (g_rot_esc_cap > ROT_MAX_ESCAPES) g_rot_esc_cap = ROT_MAX_ESCAPES;
}

static uint32_t rot_esc_cap(void) {
  if (!g_rot_esc_cap) opt_init_tunables();
  return g_rot_esc_cap;
}

static bool rotate_one(IrFunc *f, uint32_t hb) {
  Block *h = &f->blocks[hb];
#define RJ(w) do { if (warmc_getenv("WARMC_ROT_DEBUG")) fprintf(stderr, "rot %s b%u reject: %s\n", f->m->funcs[f->func_index].sym, hb, w); return false; } while (0)

#define RJ_OP(w, op) do { if (warmc_getenv("WARMC_ROT_DEBUG")) \
    fprintf(stderr, "rot %s b%u reject: %s %s\n", \
            f->m->funcs[f->func_index].sym, hb, w, ir_op_name(op)); \
    return false; } while (0)
  if (h->term != T_BRIF) { if (nback_of(f,hb)) RJ("not brif"); return false; }
  if (h->ninsts > 8) { if (nback_of(f,hb)) RJ("size"); return false; }

  uint32_t nback = 0, nfwd = 0;
  for (uint32_t p = 0; p < h->npreds; p++) {
    if (f->blocks[h->preds[p]].rpo >= h->rpo) nback++;
    else nfwd++;
  }
  if (nback == 0 || nfwd == 0) return false;

  if (h->is_pad) RJ("header is a landing pad");

  for (uint32_t i = 0; i < h->ninsts; i++) {
    Inst *in = &f->insts[h->insts[i]];
    if (!ir_is_pure(in) || in->nargs > 3) RJ_OP("impure/wide", in->op);
  }

  uint32_t nb = f->nblocks;

  uint8_t *inl = xcalloc(nb ? nb : 1, 1);
  uint32_t *lst = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t lsp = 0;
  inl[hb] = 1;
  for (uint32_t p = 0; p < h->npreds; p++) {
    uint32_t pb = h->preds[p];
    if (f->blocks[pb].rpo >= h->rpo && !inl[pb]) { inl[pb] = 1; lst[lsp++] = pb; }
  }
  while (lsp) {
    Block *b = &f->blocks[lst[--lsp]];
    for (uint32_t p = 0; p < b->npreds; p++)
      if (!inl[b->preds[p]]) { inl[b->preds[p]] = 1; lst[lsp++] = b->preds[p]; }
  }
  xfree(lst);
  if (inl[f->entry]) { xfree(inl); RJ("cycle has a second way in"); }

  /* Rotation copies the header's test into the latch, which keeps one way
     into the loop only if the test leaves it on one side.  A header whose
     successors both stay inside -- an if/else at the top of the body --
     would give the loop a second entry, and every loop pass after this one
     finds its body by flooding back from the latch to the header, which
     from a second entry runs on out of the loop. */
  uint32_t nstay = 0;
  for (uint32_t s = 0; s < h->nsuccs; s++)
    if (inl[h->succs[s].target]) nstay++;
  if (nstay != 1) { xfree(inl); RJ("header test does not leave the loop"); }
  bool body_calls = false, body_mem = false;
  for (uint32_t bi = 0; bi < nb; bi++) {
    if (!inl[bi] || !f->blocks[bi].reachable) continue;
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      uint32_t op = f->insts[b->insts[i]].op;
      if (op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF ||
          op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
          op == I_RETURN_CALL_REF) body_calls = true;
      else if (op == I_LOAD || op == I_STORE || op == I_VLOAD || op == I_VSTORE)
        body_mem = true;
    }
  }
  xfree(inl);

  bool rot_all = warmc_getenv("WARMC_ROT_ALL") != NULL;
  if (body_calls && !rot_all) RJ("body makes a call");

  if (!body_mem && !rot_all) RJ("body touches no memory");

  /* What rotation saves is the jump back to the header: the latch ends in
     the header's test instead, one conditional branch where there were two.
     A latch that already ends in a conditional branch -- its other side
     leaves the loop, as LLVM's loops with a break do -- has no jump to
     save.  The copied test lands after the latch's own branch, a trip
     still takes two, and the latch's way out, which fell through into the
     code after the loop, becomes a taken branch over the copy.
     vecbench/condsum's skip loop, left by every flag it keeps, ran 45%
     slower rotated.  So at least one latch must end in a jump. */
  bool jump_latch = false;
  for (uint32_t p = 0; p < h->npreds; p++)
    if (f->blocks[h->preds[p]].rpo >= h->rpo &&
        f->blocks[h->preds[p]].term == T_JUMP) jump_latch = true;
  if (!jump_latch) RJ("no latch ends in a jump");

  /* Which of the header's successors dominates each block.  Climbing the
     dominator tree from every block separately is the same walk over and
     over: everything on one climb shares its answer with the block it
     started from, so recording it on the way back down makes the whole map
     linear in the function rather than linear times its dominator depth.  On
     a large function full of small loops that is the difference between
     minutes and seconds -- see the budget on pass_rotate. */
  enum { NO_OWNER = UINT32_MAX, OWNER_UNSET = UINT32_MAX - 1 };
  uint32_t *owner = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t *opath = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  for (uint32_t bi = 0; bi < nb; bi++) owner[bi] = OWNER_UNSET;
  for (uint32_t bi = 0; bi < nb; bi++) {
    if (owner[bi] != OWNER_UNSET) continue;
    if (bi == hb || !f->blocks[bi].reachable) { owner[bi] = NO_OWNER; continue; }

    uint32_t np = 0, res = NO_OWNER, d = bi;
    for (;;) {
      if (d == hb) break;
      if (owner[d] != OWNER_UNSET) { res = owner[d]; break; }
      bool found = false;
      for (uint32_t s = 0; s < h->nsuccs && !found; s++)
        if (h->succs[s].target == d) { res = s; found = true; }
      if (found) break;
      opath[np++] = d;
      if (d == f->entry) break;
      uint32_t nd = f->blocks[d].idom;
      if (nd == REF_NONE || nd == d) break;
      d = nd;
    }
    for (uint32_t k = 0; k < np; k++) owner[opath[k]] = res;
    if (owner[bi] == OWNER_UNSET) owner[bi] = res;
  }
  xfree(opath);

  Ref esc[ROT_MAX_ESCAPES];
  uint32_t escs[ROT_MAX_ESCAPES], nesc = 0;
  bool rej = false, toomany = false;
#define ROT_ESCAPE(v, bi) do {                                                 \
    Ref v_ = (v);                                                              \
    if (f->insts[v_].block == hb) {                                            \
      if (owner[bi] == NO_OWNER) { rej = true; }                               \
      else {                                                                   \
        bool have = false;                                                     \
        for (uint32_t e_ = 0; e_ < nesc; e_++)                                 \
          if (esc[e_] == v_ && escs[e_] == owner[bi]) have = true;             \
        if (!have) {                                                           \
          if (nesc == rot_esc_cap()) { rej = true; toomany = true; }            \
          else { esc[nesc] = v_; escs[nesc] = owner[bi]; nesc++; }             \
        }                                                                      \
      }                                                                        \
    }                                                                          \
  } while (0)

  for (uint32_t bi = 0; bi < nb && !rej; bi++) {
    if (bi == hb) continue;
    Block *b = &f->blocks[bi];
    if (!b->reachable) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      Ref *a = ir_args(f, b->insts[i]);
      for (uint32_t j = 0; j < in->nargs; j++) ROT_ESCAPE(a[j], bi);
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) ROT_ESCAPE(b->term_arg, bi);
    for (uint32_t i = 0; i < b->nret; i++) ROT_ESCAPE(b->ret_args[i], bi);
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++)
        ROT_ESCAPE(b->succs[s].args[j], bi);
  }
#undef ROT_ESCAPE
  if (rej) {
    xfree(owner);
    if (toomany) RJ("too many values escape");
    RJ("value escapes past a merge point");
  }

  for (uint32_t e = 0; e < nesc; e++) {
    Block *t = &f->blocks[h->succs[escs[e]].target];
    if (t->npreds != 1 || t->preds[0] != hb) { xfree(owner); RJ("successor has other predecessors"); }
    if (t->is_pad) { xfree(owner); RJ("successor is a landing pad"); }
  }

  for (uint32_t e = 0; e < nesc; e++) {
    uint32_t s = escs[e];
    uint32_t tb = h->succs[s].target;
    Ref np = ir_add_param(f, tb, (ValType)f->insts[esc[e]].type);
    h = &f->blocks[hb];
    Edge *ed = &h->succs[s];
    if (ed->nargs == ed->cap) {
      uint32_t oc_ = ed->cap;
      ed->cap = ed->cap ? ed->cap * 2 : 4;
      ed->args = ir_grow(f, ed->args, (size_t)oc_ * sizeof(Ref),
                          (size_t)ed->cap * sizeof(Ref));
    }
    ed->args[ed->nargs++] = esc[e];

    for (uint32_t bi = 0; bi < nb; bi++) {
      if (bi == hb || owner[bi] != s || !f->blocks[bi].reachable) continue;
      Block *b = &f->blocks[bi];
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Inst *in = &f->insts[b->insts[i]];
        Ref *a = ir_args(f, b->insts[i]);
        for (uint32_t j = 0; j < in->nargs; j++) if (a[j] == esc[e]) a[j] = np;
      }
      if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == esc[e])
        b->term_arg = np;
      for (uint32_t i = 0; i < b->nret; i++)
        if (b->ret_args[i] == esc[e]) b->ret_args[i] = np;
      for (uint32_t s2 = 0; s2 < b->nsuccs; s2++)
        for (uint32_t j = 0; j < b->succs[s2].nargs; j++)
          if (b->succs[s2].args[j] == esc[e]) b->succs[s2].args[j] = np;
    }
  }
  xfree(owner);
  h = &f->blocks[hb];

  uint32_t nparams = h->nparams, ninsts = h->ninsts, nsuccs = h->nsuccs;
  uint32_t *insts = xmalloc((ninsts ? ninsts : 1) * sizeof(uint32_t));
  memcpy(insts, h->insts, ninsts * sizeof(uint32_t));
  uint32_t *ptypes = xmalloc((nparams ? nparams : 1) * sizeof(uint32_t));
  Ref *pold = xmalloc((nparams ? nparams : 1) * sizeof(Ref));
  for (uint32_t p = 0; p < nparams; p++) {
    pold[p] = h->params[p];
    ptypes[p] = f->insts[h->params[p]].type;
  }

  uint32_t cap = f->ninsts + nparams + ninsts + 8;
  Ref *vmap = xmalloc(cap * sizeof(Ref));
  for (uint32_t i = 0; i < cap; i++) vmap[i] = i;

  uint32_t h2 = ir_new_block(f);
  for (uint32_t p = 0; p < nparams; p++)
    vmap[pold[p]] = ir_add_param(f, h2, (ValType)ptypes[p]);

  Ref tmp[4];
  for (uint32_t i = 0; i < ninsts; i++) {
    Ref src = insts[i];
    Inst *si = &f->insts[src];

    if (si->op == I_CONST) { vmap[src] = src; continue; }
    Ref *sa = ir_args(f, src);
    for (uint32_t j = 0; j < si->nargs; j++) tmp[j] = vmap[sa[j]];
    vmap[src] = ir_emit(f, h2, (IrOp)si->op, (ValType)si->type, tmp, si->nargs,
                        si->imm, si->imm2);
  }

  h = &f->blocks[hb];
  f->blocks[h2].term = T_BRIF;
  f->blocks[h2].term_arg = vmap[h->term_arg];
  for (uint32_t s = 0; s < nsuccs; s++) {
    uint32_t target = f->blocks[hb].succs[s].target;
    uint32_t nargs = f->blocks[hb].succs[s].nargs;
    Ref *args = xmalloc((nargs ? nargs : 1) * sizeof(Ref));
    for (uint32_t j = 0; j < nargs; j++) args[j] = vmap[f->blocks[hb].succs[s].args[j]];
    ir_add_edge(f, h2, target, args, nargs);
    xfree(args);
  }

  uint32_t hrpo = f->blocks[hb].rpo;
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    if (bi == h2 || !b->reachable || b->rpo < hrpo) continue;
    for (uint32_t s = 0; s < b->nsuccs; s++)
      if (b->succs[s].target == hb) b->succs[s].target = h2;
  }

  xfree(insts); xfree(ptypes); xfree(pold); xfree(vmap);
  g_stats.rotated++;
  return true;
}

/* Deciding one header costs a walk over every block: the owner map climbs the
   dominator tree from each of them, and the escape scan reads every
   instruction.  Trying every header is therefore quadratic in the function,
   which is invisible until a function is large and full of small loops --
   test/run_adapter.py's abicover generates exactly that, and warmc took
   minutes on it while never compiling it in any suite.  Rotation is an
   optimisation and stopping early only costs speed, so it gets a budget, the
   way the occupancy walk in regalloc.inc does. */
#define ROT_BUDGET ((uint64_t)24 * 1024 * 1024)

static bool pass_rotate(IrFunc *f) {
  bool changed = false;
  uint32_t norig = f->nblocks;
  uint8_t *done = xcalloc(norig ? norig : 1, 1);
  uint64_t budget = ROT_BUDGET;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    if (hb >= norig || done[hb]) continue;
    if (budget < f->nblocks) break;
    budget -= f->nblocks;
    done[hb] = 1;
    if (rotate_one(f, hb)) {
      changed = true;
      ir_order(f);
      oi = 0;
    }
  }
  xfree(done);
  return changed;
}

#define IV_DEPTH 8

#define IV_MIN_BODY 10
#define IV_MIN_DEPTH 2

static bool iv_affine(IrFunc *f, const uint8_t *inloop, Ref iv, Ref v,
                      int depth, bool *dep) {
  if (v == iv) { *dep = true; return true; }
  Inst *in = &f->insts[v];
  if (in->op == I_CONST || !inloop[in->block]) { *dep = false; return true; }
  if (depth <= 0) return false;
  Ref a0 = ir_args(f, v)[0];
  Ref a1 = in->nargs > 1 ? ir_args(f, v)[1] : REF_NONE;
  bool da = false, db = false;
  uint64_t c;
  switch (in->op) {
    case I_ADD: case I_SUB:
      if (in->nargs != 2) return false;
      if (!iv_affine(f, inloop, iv, a0, depth - 1, &da)) return false;
      if (!iv_affine(f, inloop, iv, a1, depth - 1, &db)) return false;
      *dep = da || db;
      return true;
    case I_SHL:

      if (in->nargs != 2 || !is_const(f, a1, &c)) return false;
      if (!iv_affine(f, inloop, iv, a0, depth - 1, &da)) return false;
      *dep = da;
      return true;
    case I_MUL:
      if (in->nargs != 2) return false;
      if (!iv_affine(f, inloop, iv, a0, depth - 1, &da)) return false;
      if (!iv_affine(f, inloop, iv, a1, depth - 1, &db)) return false;
      if (da && db) return false;
      *dep = da || db;
      return true;
    default: return false;
  }
}

static bool iv_const_coeff(IrFunc *f, const uint8_t *inloop, Ref iv, Ref v,
                           int depth, uint64_t *out) {
  if (v == iv) { *out = 1; return true; }
  Inst *in = &f->insts[v];
  if (in->op == I_CONST || !inloop[in->block]) { *out = 0; return true; }
  if (depth <= 0 || in->nargs != 2) return false;
  Ref a0 = ir_args(f, v)[0], a1 = ir_args(f, v)[1];
  uint64_t x, y, c;
  switch (in->op) {
    case I_ADD:
      if (!iv_const_coeff(f, inloop, iv, a0, depth - 1, &x)) return false;
      if (!iv_const_coeff(f, inloop, iv, a1, depth - 1, &y)) return false;
      *out = x + y; return true;
    case I_SUB:
      if (!iv_const_coeff(f, inloop, iv, a0, depth - 1, &x)) return false;
      if (!iv_const_coeff(f, inloop, iv, a1, depth - 1, &y)) return false;
      *out = x - y; return true;
    case I_SHL:

      if (!is_const(f, a1, &c)) return false;
      if (!iv_const_coeff(f, inloop, iv, a0, depth - 1, &x)) return false;
      *out = x << (c & (vt_is_64((ValType)in->type) ? 63u : 31u));
      return true;
    case I_MUL:
      if (is_const(f, a1, &c)) {
        if (!iv_const_coeff(f, inloop, iv, a0, depth - 1, &x)) return false;
        *out = x * c; return true;
      }
      if (is_const(f, a0, &c)) {
        if (!iv_const_coeff(f, inloop, iv, a1, depth - 1, &x)) return false;
        *out = x * c; return true;
      }

      if (!iv_const_coeff(f, inloop, iv, a0, depth - 1, &x)) return false;
      if (!iv_const_coeff(f, inloop, iv, a1, depth - 1, &y)) return false;
      return false;
    default: return false;
  }
}

static uint32_t iv_chain_dead(IrFunc *f, const uint8_t *inloop, Ref iv, Ref v,
                              const uint32_t *uses, int depth, bool root) {
  if (v == iv || depth <= 0) return 0;
  Inst *in = &f->insts[v];
  if (in->op == I_CONST || !inloop[in->block]) return 0;
  if (!root && uses[v] != 1) return 0;
  uint32_t n = 1;
  for (uint32_t j = 0; j < in->nargs; j++)
    n += iv_chain_dead(f, inloop, iv, ir_args(f, v)[j], uses, depth - 1, false);
  return n;
}

static Ref iv_clone(IrFunc *f, const uint8_t *inloop, Ref iv, Ref v, Ref rep,
                    uint32_t pre, int depth) {
  if (v == iv) return rep;
  uint16_t op = f->insts[v].op;
  if (op != I_CONST && !inloop[f->insts[v].block]) return v;
  if (depth <= 0) return REF_NONE;
  uint8_t ty = f->insts[v].type;
  uint64_t imm = f->insts[v].imm, imm2 = f->insts[v].imm2;
  uint32_t n = f->insts[v].nargs;
  if (n > 3) return REF_NONE;
  Ref src[3], na[3];
  for (uint32_t j = 0; j < n; j++) src[j] = ir_args(f, v)[j];
  for (uint32_t j = 0; j < n; j++) {
    na[j] = iv_clone(f, inloop, iv, src[j], rep, pre, depth - 1);
    if (na[j] == REF_NONE) return REF_NONE;
  }
  return ir_emit(f, pre, (IrOp)op, (ValType)ty, na, n, imm, imm2);
}

static Edge *iv_edge(IrFunc *f, uint32_t from, uint32_t to) {
  Block *b = &f->blocks[from];
  Edge *found = NULL;
  for (uint32_t s = 0; s < b->nsuccs; s++) {
    if (b->succs[s].target != to) continue;
    if (found) return NULL;
    found = &b->succs[s];
  }
  return found;
}

/* A counter that runs down as `while (n--)` is tested before the decrement
   that it carries to the next trip: the block computes n - 1 and branches on
   n, so the old count and the new one are both live at the branch, cannot
   share a register, and the back edge carries a move -- in a rotated latch
   the move needs a block of its own, and a trip pays a second taken branch
   for it (AssemblyScript's String.__eq compares its u16 tail like that).
   Testing the new count instead -- n != 0 is n - 1 != -1 -- ends the old
   one at the subtraction.  Only when the old count has no other use, and
   the new one leaves along an edge, the shape of a carried counter. */
static bool pass_postdec_test(IrFunc *f) {
  uint32_t *uses = count_uses(f);
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (b->term != T_BRIF || b->nsuccs != 2) continue;
    /* The test is the count itself, its eqz, or its compare with zero. */
    Ref c = b->term_arg;
    bool zero_test = false;
    uint64_t z;
    if (f->insts[c].block == bi && uses[c] == 1) {
      uint16_t op = f->insts[c].op;
      Ref *ta = ir_args(f, c);
      if (op == I_EQZ) { c = ta[0]; zero_test = true; }
      else if ((op == I_EQ || op == I_NE) && is_const(f, ta[1], &z) && z == 0) {
        zero_test = op == I_EQ;
        c = ta[0];
      }
    }
    ValType t = (ValType)f->insts[c].type;
    if ((t != VT_I32 && t != VT_I64) || uses[c] != 2) continue;
    uint64_t ones = t == VT_I64 ? ~0ull : 0xffffffffu;
    Ref d = REF_NONE;
    for (uint32_t i = 0; i < b->ninsts && d == REF_NONE; i++) {
      Ref r = b->insts[i];
      Inst *in = &f->insts[r];
      if (in->type != t || in->nargs != 2) continue;
      Ref *a = ir_args(f, r);
      if (a[0] != c || f->insts[a[1]].op != I_CONST) continue;
      uint64_t k = f->insts[a[1]].imm & ones;
      if ((in->op == I_SUB && k == 1) || (in->op == I_ADD && k == ones)) d = r;
    }
    if (d == REF_NONE) continue;
    bool carried = false;
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++)
        if (b->succs[s].args[j] == d) carried = true;
    if (!carried) continue;
    Ref args[2] = { d, ir_const(f, bi, t, ones) };
    b->term_arg = ir_emit(f, bi, zero_test ? I_EQ : I_NE, VT_I32, args, 2, 0, 0);
    changed = true;
  }
  xfree(uses);
  return changed;
}

typedef struct { Ref addr; Ref iv; Ref entry; uint64_t step; uint16_t sop; } IvCand;

static int u64_cmp(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

/* Per loop, the work is the loop's: its body is listed as it is walked (and
   cleared from that list), its blocks visited in order by sorting the list
   by RPO, and the use counts taken again only after a loop that changed the
   function -- clearing a block map, scanning the whole order per header
   param and recounting every use per header made this blocks times loops. */
static bool pass_iv_strength(IrFunc *f) {
  bool changed = false;
  uint8_t *inloop = xcalloc(f->nblocks ? f->nblocks : 1, 1);
  uint32_t *stack = xmalloc((f->nblocks ? f->nblocks : 1) * sizeof(uint32_t));
  uint32_t *body = xmalloc((f->nblocks ? f->nblocks : 1) * sizeof(uint32_t));
  uint64_t *walk = xmalloc((f->nblocks ? f->nblocks : 1) * sizeof(uint64_t));
  uint32_t nbody = 0;
  uint32_t *uses = NULL;
  IvCand *cand = NULL;
  uint32_t ncand = 0, capcand = 0;

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    Block *h = &f->blocks[hb];
    if (!h->nparams) continue;

    uint32_t pre = REF_NONE, latch = REF_NONE, nfwd = 0, nback = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo) { nback++; latch = pb; }
      else { nfwd++; pre = pb; }
    }
    if (nfwd != 1 || nback != 1) continue;
    if (f->blocks[pre].term != T_JUMP || f->blocks[latch].term != T_JUMP) continue;

    if (ir_block_has_eh(f, pre) || ir_block_has_eh(f, latch) ||
        f->blocks[hb].is_pad) continue;

    for (uint32_t j = 0; j < nbody; j++) inloop[body[j]] = 0;
    nbody = 0;
    inloop[hb] = 1;
    body[nbody++] = hb;
    uint32_t sp = 0;
    if (!inloop[latch]) { inloop[latch] = 1; body[nbody++] = latch; stack[sp++] = latch; }
    while (sp) {
      Block *c = &f->blocks[stack[--sp]];
      for (uint32_t p = 0; p < c->npreds; p++)
        if (!inloop[c->preds[p]]) {
          inloop[c->preds[p]] = 1;
          body[nbody++] = c->preds[p];
          stack[sp++] = c->preds[p];
        }
    }
    if (inloop[pre] || inloop[f->entry]) continue;

    Edge *pe = iv_edge(f, pre, hb), *le = iv_edge(f, latch, hb);
    if (!pe || !le) continue;
    if (pe->nargs != h->nparams || le->nargs != h->nparams) continue;

    /* the body's blocks in the order, as a scan of the order would meet them */
    uint32_t nwalk = 0;
    for (uint32_t j = 0; j < nbody; j++) {
      uint32_t r = f->blocks[body[j]].rpo;
      if (r < f->norder && f->order[r] == body[j])
        walk[nwalk++] = (uint64_t)r << 32 | body[j];
    }
    qsort(walk, nwalk, sizeof(uint64_t), u64_cmp);

    if (!uses) uses = count_uses(f);
    ncand = 0;
    for (uint32_t k = 0; k < h->nparams; k++) {
      Ref p = h->params[k], back = le->args[k], entry = pe->args[k];
      Inst *bin = &f->insts[back];
      if ((bin->op != I_ADD && bin->op != I_SUB) || bin->nargs != 2) continue;
      if (!inloop[bin->block]) continue;
      Ref *ba = ir_args(f, back);
      uint64_t step;
      if (ba[0] != p || !is_const(f, ba[1], &step)) continue;
      if (inloop[f->insts[entry].block]) continue;

      for (uint32_t bo = 0; bo < nwalk; bo++) {
        uint32_t bi = (uint32_t)walk[bo];
        Block *b = &f->blocks[bi];
        for (uint32_t i = 0; i < b->ninsts; i++) {
          Ref u = b->insts[i];
          uint16_t uop = f->insts[u].op;
          if (uop != I_MEM_ADDR && uop != I_MEM_PTR) continue;
          Ref v = ir_args(f, u)[0];
          if (!inloop[f->insts[v].block]) continue;
          bool dep = false;
          if (!iv_affine(f, inloop, p, v, IV_DEPTH, &dep) || !dep) continue;

          uint32_t dead = iv_chain_dead(f, inloop, p, v, uses, IV_DEPTH, true);
          if (dead < 2) continue;
          bool dup = false;
          for (uint32_t c2 = 0; c2 < ncand && !dup; c2++)
            if (cand[c2].addr == v) dup = true;
          if (dup) continue;
          if (ncand == capcand) {
            capcand = capcand ? capcand * 2 : 8;
            cand = xrealloc(cand, capcand * sizeof(IvCand));
          }
          cand[ncand++] = (IvCand){ v, p, entry, step, bin->op };
        }
      }
    }

    uint32_t bodysz = 0;
    for (uint32_t j = 0; j < nbody; j++) bodysz += f->blocks[body[j]].ninsts;
    if (ncand < 2 || bodysz < IV_MIN_BODY ||
        f->blocks[hb].loop_depth < IV_MIN_DEPTH) ncand = 0;
    if (ncand) { xfree(uses); uses = NULL; }
    for (uint32_t c = 0; c < ncand; c++) {
      Ref v = cand[c].addr, p = cand[c].iv, entry = cand[c].entry;
      ValType vt = (ValType)f->insts[v].type;
      ValType pt = (ValType)f->insts[p].type;
      Ref b0 = iv_clone(f, inloop, p, v, entry, pre, IV_DEPTH);
      if (b0 == REF_NONE) continue;
      Ref delta;
      uint64_t coeff;
      if (iv_const_coeff(f, inloop, p, v, IV_DEPTH, &coeff)) {
        uint64_t d = coeff * cand[c].step;
        if (cand[c].sop == I_SUB) d = (uint64_t)0 - d;
        if (!vt_is_64(vt)) d &= 0xffffffffu;
        delta = ir_emit(f, pre, I_CONST, vt, NULL, 0, d, 0);
      } else {
        Ref sc = ir_emit(f, pre, I_CONST, pt, NULL, 0, cand[c].step, 0);
        Ref sa[2] = { entry, sc };
        Ref entry2 = ir_emit(f, pre, (IrOp)cand[c].sop, pt, sa, 2, 0, 0);
        Ref b1 = iv_clone(f, inloop, p, v, entry2, pre, IV_DEPTH);
        if (b1 == REF_NONE) continue;
        Ref da[2] = { b1, b0 };
        delta = ir_emit(f, pre, I_SUB, vt, da, 2, 0, 0);
      }
      Ref q = ir_add_param(f, hb, vt);
      Ref ia[2] = { q, delta };
      Ref inc = ir_emit(f, latch, I_ADD, vt, ia, 2, 0, 0);
      edge_push_arg(f, pre, hb, b0);
      edge_push_arg(f, latch, hb, inc);
      if (warmc_getenv("WARMC_IV_DEBUG"))
        fprintf(stderr, "iv: fn=%s hdr=b%u depth=%u body=%u ncand=%u v=v%u\n",
                f->m->funcs[f->func_index].sym ? f->m->funcs[f->func_index].sym : "?",
                hb, f->blocks[hb].loop_depth, bodysz, ncand, v);
      ir_replace(f, v, q);
      g_stats.strength_reduced++;
      changed = true;
    }
  }
  xfree(cand); xfree(inloop); xfree(stack); xfree(body); xfree(walk); xfree(uses);
  if (changed) ir_apply_subst(f);
  return changed;
}

static bool offset_is_free(IrFunc *f, Ref ea, const uint32_t *uses) {
  if (uses[ea] != 1) return false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      Ref *a = ir_args(f, u);
      for (uint32_t j = 0; j < in->nargs; j++) {
        if (a[j] != ea) continue;
        if (j != 0 || (in->op != I_LOAD && in->op != I_STORE)) return false;
        uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
        uint64_t off = f->insts[ea].imm;
        if (!ir_mem_disp_ok(off, size)) return false;
      }
    }
  }
  return true;
}

typedef struct {
  uint32_t *first;
  uint32_t *next;
  Ref *user;
  uint8_t *arg0;
  uint8_t *escapes;
} UseIdx;

static UseIdx use_index(IrFunc *f) {
  UseIdx x;
  uint32_t n = f->ninsts;
  size_t tot = 0, k = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) tot += f->insts[b->insts[i]].nargs;
  }
  x.first = xmalloc((n ? n : 1) * sizeof(uint32_t));
  for (uint32_t v = 0; v < n; v++) x.first[v] = UINT32_MAX;
  x.next = xmalloc((tot ? tot : 1) * sizeof(uint32_t));
  x.user = xmalloc((tot ? tot : 1) * sizeof(Ref));
  x.arg0 = xmalloc((tot ? tot : 1) * sizeof(uint8_t));
  x.escapes = xcalloc(n ? n : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      Ref *a = ir_args(f, u);
      for (uint32_t j = 0; j < in->nargs; j++) {
        if (a[j] >= n) continue;
        x.user[k] = u;
        x.arg0[k] = (j == 0);
        x.next[k] = x.first[a[j]];
        x.first[a[j]] = (uint32_t)k;
        k++;
      }
    }
    if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg < n)
      x.escapes[b->term_arg] = 1;
    for (uint32_t i = 0; i < b->nret; i++)
      if (b->ret_args[i] < n) x.escapes[b->ret_args[i]] = 1;
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++)
        if (b->succs[s].args[j] < n) x.escapes[b->succs[s].args[j]] = 1;
  }
  return x;
}

static void use_index_free(UseIdx *x) {
  xfree(x->first); xfree(x->next); xfree(x->user); xfree(x->arg0); xfree(x->escapes);
}

static bool addr_only_zero_disp_uses(IrFunc *f, const UseIdx *ui, Ref ea,
                                     uint32_t *mem) {
  bool any = false;
  if (ea >= f->ninsts || ui->escapes[ea]) return false;
  for (uint32_t k = ui->first[ea]; k != UINT32_MAX; k = ui->next[k]) {
    Inst *in = &f->insts[ui->user[k]];
    if (!ui->arg0[k] || (in->op != I_LOAD && in->op != I_STORE)) return false;
    if (in->imm != 0) return false;

    if (any && *mem != MEMF_MEM(in->imm2)) return false;
    *mem = MEMF_MEM(in->imm2);
    any = true;
  }
  return any;
}

static bool pass_addr_offsets(IrFunc *f, const OptOpts *o) {
  bool changed = false;
  uint32_t *uses = count_uses(f);

  uint32_t nv = f->ninsts;
  uint8_t *want_ptr = xcalloc(nv ? nv : 1, 1);
  uint32_t *want_mem = xcalloc(nv ? nv : 1, sizeof(uint32_t));
  if (o->guard_pages) {
    UseIdx ui = use_index(f);
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      Block *b = &f->blocks[f->order[oi]];
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref v = b->insts[i];
        Inst *in = &f->insts[v];
        if (in->op != I_MEM_ADDR || in->nargs != 1) continue;
        uint32_t mem = 0;
        want_ptr[v] = addr_only_zero_disp_uses(f, &ui, v, &mem);
        want_mem[v] = mem;
      }
    }
    use_index_free(&ui);
  }
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op != I_MEM_ADDR || in->nargs != 1) continue;

      uint32_t mem = want_mem[v];
      bool ptr = want_ptr[v];

      if (!ptr || (mem == 0 && in->imm == 0)) {
        if (in->imm == 0) continue;

        if (in->imm < 0x1000) continue;
        if (offset_is_free(f, v, uses)) continue;
      }
      Ref c = ptr ? ir_emit(f, bi, I_MEM_BASE, VT_I64, NULL, 0, in->imm, mem)
                  : ir_emit(f, bi, I_CONST, VT_I64, NULL, 0, in->imm, 0);

      b->ninsts--;
      for (uint32_t j = b->ninsts; j > i; j--) b->insts[j] = b->insts[j - 1];
      b->insts[i] = c;
      b->ninsts++;
      in = &f->insts[v];
      Ref old = ir_args(f, v)[0];
      Ref a[2] = { old, c };
      in->args_off = f->nargpool;
      in->nargs = 2;
      for (int k = 0; k < 2; k++) {
        if (f->nargpool == f->cap_argpool) {
          uint32_t oc_ = f->cap_argpool;
          f->cap_argpool = f->cap_argpool ? f->cap_argpool * 2 : 32;
          f->argpool = ir_grow(f, f->argpool, (size_t)oc_ * sizeof(Ref),
                               (size_t)f->cap_argpool * sizeof(Ref));
        }
        f->argpool[f->nargpool++] = a[k];
      }
      changed = true;
      i++;
    }
  }
  xfree(want_ptr);
  xfree(want_mem);
  xfree(uses);
  return changed;
}

static bool addr_users_are_accesses(IrFunc *f, const UseIdx *ui, Ref ea,
                                    uint64_t off, uint32_t *nusers,
                                    uint32_t *mem, bool *seen) {
  bool any = false;
  if (nusers) *nusers = 0;
  if (ea >= f->ninsts || ui->escapes[ea]) return false;
  for (uint32_t k = ui->first[ea]; k != UINT32_MAX; k = ui->next[k]) {
    Inst *in = &f->insts[ui->user[k]];
    if (!ui->arg0[k] || (in->op != I_LOAD && in->op != I_STORE)) return false;
    if (in->imm != 0) return false;
    uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
    if (!ir_mem_disp_ok(off, size)) return false;

    if (*seen && *mem != MEMF_MEM(in->imm2)) return false;
    *mem = MEMF_MEM(in->imm2);
    *seen = true;
    if (nusers) (*nusers)++;
    any = true;
  }
  return any;
}

/* An access at a constant address too far from the memory base for a
   displacement makes the back end build the address every time -- a mov and
   a movk on arm64 -- and C's globals are such addresses: nbody reads and
   writes its bodies at 0x10970 and on, fifteen of them a step.  Accesses
   near one another share one native pointer instead, made once at entry,
   and reach their slots by displacement from it.  The address each one
   makes is the one it made before, so it traps where it did. */
typedef struct { Ref inst; uint64_t addr; uint32_t size, mem, block; } ConstAcc;

static int const_acc_cmp(const void *x, const void *y) {
  const ConstAcc *a = x, *b = y;
  if (a->block != b->block) return a->block < b->block ? -1 : 1;
  if (a->mem != b->mem) return a->mem < b->mem ? -1 : 1;
  return a->addr < b->addr ? -1 : a->addr > b->addr;
}

static bool mem_ptr_consts(IrFunc *f) {
  uint32_t cap = 0, na = 0;
  ConstAcc *acc = NULL;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op != I_LOAD && in->op != I_STORE) continue;
      Ref a0 = ir_args(f, v)[0];
      if (f->insts[a0].op != I_CONST || f->insts[a0].type != VT_I64) continue;
      uint64_t addr = f->insts[a0].imm + in->imm;
      uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
      if (addr >> 32 || ir_mem_disp_ok(addr, size)) continue;
      if (na == cap) { cap = cap ? cap * 2 : 32; acc = xrealloc(acc, cap * sizeof *acc); }
      acc[na++] = (ConstAcc){ v, addr, size, MEMF_MEM(in->imm2), f->order[oi] };
    }
  }
  bool changed = false;
  if (na >= 2) {
    qsort(acc, na, sizeof *acc, const_acc_cmp);
    uint32_t entry;
    for (uint32_t s = 0; s < na;) {
      uint64_t base = acc[s].addr & ~(uint64_t)7;
      uint32_t e = s;
      while (e < na && acc[e].block == acc[s].block && acc[e].mem == acc[s].mem &&
             ir_mem_disp_ok(acc[e].addr - base, acc[e].size))
        e++;
      if (e - s >= 2) {
        /* one pointer per block: a pointer shared by the code before a loop
           and the code after it holds a register right through the loop,
           which measured worse than the moves it saves */
        entry = acc[s].block;
        Ref k = ir_emit(f, entry, I_CONST, VT_I32, NULL, 0, base, 0);
        Ref p = ir_emit(f, entry, I_MEM_PTR, VT_I64, &k, 1, 0, acc[s].mem);
        Block *eb = &f->blocks[entry];
        uint32_t at = 0;
        while (at < eb->ninsts - 2 && f->insts[eb->insts[at]].op == I_PARAM) at++;
        for (uint32_t j = eb->ninsts - 1; j >= at + 2; j--) eb->insts[j] = eb->insts[j - 2];
        eb->insts[at] = k;
        eb->insts[at + 1] = p;
        for (uint32_t q = s; q < e; q++) {
          Inst *in = &f->insts[acc[q].inst];
          in->imm = acc[q].addr - base;
          ir_args(f, acc[q].inst)[0] = p;
        }
        changed = true;
      }
      if (e == s) e++;
      s = e;
    }
  }
  xfree(acc);
  return changed;
}

static bool pass_mem_ptr(IrFunc *f) {
  bool changed = false;
  uint32_t n = f->ninsts;

  uint32_t *count = xcalloc(n ? n : 1, sizeof(uint32_t));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      if (in->op == I_MEM_ADDR && in->nargs == 1)
        count[ir_args(f, b->insts[i])[0]]++;
    }
  }

  UseIdx ui = use_index(f);
  for (uint32_t base = 0; base < n; base++) {
    if (count[base] < 2) continue;

    /* The pointer is made where the base is, so it costs an add every time
       that block runs, and on the path from the base to its loads: in a
       pointer chase that is a cycle per step.  Only accesses that run as often
       as it pays for it -- not the ones in the block a search leaves its loop
       for, which run once per search. */
    uint32_t depth = f->blocks[f->insts[base].block].loop_depth;
    bool ok = true;
    uint32_t cost = 0;
    uint32_t mem = 0;
    bool seen = false;
    for (uint32_t k = ui.first[base]; k != UINT32_MAX && ok; k = ui.next[k]) {
      {
        Ref v = ui.user[k];
        Inst *in = &f->insts[v];
        if (in->op != I_MEM_ADDR || in->nargs != 1 || !ui.arg0[k]) continue;
        uint32_t nu = 0;
        ok = addr_users_are_accesses(f, &ui, v, in->imm, &nu, &mem, &seen);

        if (ok && in->imm != 0) {
          uint32_t hot = 0;
          for (uint32_t k2 = ui.first[v]; k2 != UINT32_MAX; k2 = ui.next[k2])
            if (f->blocks[f->insts[ui.user[k2]].block].loop_depth >= depth) hot++;
          if (hot == 1) cost += 1;
          else if (hot > 1) cost += 2;
        }
      }
    }
    if (!ok || cost < 2) continue;

    uint32_t bb = f->insts[base].block;
    Block *db = &f->blocks[bb];
    uint32_t at = 0;
    bool found = f->insts[base].op == I_PARAM;
    for (uint32_t i = 0; i < db->ninsts && !found; i++)
      if (db->insts[i] == base) { at = i + 1; found = true; }
    if (!found) continue;

    if (at >= db->ninsts && ir_block_has_eh(f, bb)) continue;

    Ref p = ir_emit(f, bb, I_MEM_PTR, VT_I64, &base, 1, 0, mem);
    db = &f->blocks[bb];
    db->ninsts--;
    for (uint32_t j = db->ninsts; j > at; j--) db->insts[j] = db->insts[j - 1];
    db->insts[at] = p;
    db->ninsts++;

    for (uint32_t k2 = ui.first[base]; k2 != UINT32_MAX; k2 = ui.next[k2]) {
      Ref ea = ui.user[k2];
      Inst *ei = &f->insts[ea];
      if (ei->op != I_MEM_ADDR || ei->nargs != 1 || !ui.arg0[k2]) continue;
      for (uint32_t k3 = ui.first[ea]; k3 != UINT32_MAX; k3 = ui.next[k3]) {
        Ref u = ui.user[k3];
        Inst *in = &f->insts[u];
        if (in->op != I_LOAD && in->op != I_STORE) continue;
        if (!ui.arg0[k3]) continue;
        in->imm = ei->imm;
        ir_args(f, u)[0] = p;
      }
    }

    changed = true;
  }
  use_index_free(&ui);
  xfree(count);
  if (mem_ptr_consts(f)) changed = true;
  return changed;
}

#define VEC_BYTES    16u
#define VEC_MAX_ACC  128
#define VEC_MAX_MEMB 32
#define VEC_MAX_VEC  4
#define VEC_MAX_ILV  2
#define VEC_MAX_BLK  16
#define VEC_MAX_BODY 512
#define VEC_MAX_SEL  32
#define VEC_MAX_GRP  8
#define VEC_MAX_RED  4
#define VEC_MAX_PAY  4

#define VEC_MAX_WIDEN 3
#define VEC_MAX_MEMO 128

#define VEC_MAX_CHECK 8
#define VEC_DEPTH    12
/* The most copies of the vector body one iteration of the vector loop
   holds; see vectorize_one. */
#define VEC_MAX_COPY 4

typedef struct {
  Ref inst;
  Ref base;
  uint64_t disp;
  uint32_t pos;
  uint32_t width;
  uint32_t lane;
  uint32_t ivx;
  uint64_t coeff;
  uint64_t stride;
  bool store;
  uint32_t mem;
  bool spec;
  Ref cond;
  bool pol;
} VAcc;

typedef struct {
  Ref base;
  uint32_t ivx;
  uint64_t coeff;
  uint32_t m;
  uint32_t nv;
  uint64_t disp;
  bool stat;
  uint64_t stat_addr;
  uint32_t width;
  bool store;
  uint32_t mem;

  uint32_t n;
  Ref member[VEC_MAX_MEMB];
  uint32_t pos[VEC_MAX_MEMB];
  Ref ptr;
  Ref loaded[VEC_MAX_COPY][VEC_MAX_VEC];
  Ref lo, hi;
  bool spec;
  bool pol;
  Ref mcond[VEC_MAX_MEMB];
} VGroup;

static uint32_t v_slot(const VGroup *G, uint32_t mpv, uint32_t v, uint32_t j) {
  return G->m > 1 ? (v + j * G->m) : (v * mpv + j);
}

typedef struct {
  Ref key[VEC_MAX_MEMB];
  uint32_t n, st;
  Ref val;
} VMemo;

typedef struct { Ref param, cond, t, fv; } VSel;

typedef struct {
  IrFunc *f;
  uint32_t hb;
  const uint8_t *inloop;
  VSel sel[VEC_MAX_SEL];
  uint32_t nsel;
  uint32_t vbody;

  uint32_t vguard;
  uint32_t lane;
  uint32_t width;
  uint32_t k;

  uint32_t mpv;
  uint32_t gran;
  uint32_t nvec;
  /* the copy of the body v_build is building: which of each group's loads
     a load of the scalar body stands for */
  uint32_t copy;
  VGroup grp[VEC_MAX_GRP];
  uint32_t ngrp;
  VMemo memo[VEC_MAX_MEMO];
  uint32_t nmemo;
  bool failed;

  const char *fail;
  /* the body's accesses, for v_same_load */
  const VAcc *acc;
  uint32_t nacc;
} VecCtx;

/* Whether loads a and b of the scalar body read one address the same way.
   In the vector loop every load comes before every store, and a store
   that could overlap a load is checked apart from it, so the two read one
   value there: a load the producer repeats after a store it could not see
   past -- copies of an unrolled body reading a[i + 1] again after storing
   c[i] -- is in one group or the other, and either serves. */
static bool v_same_load(const VecCtx *c, Ref a, Ref b) {
  if (a == b) return true;
  const VAcc *x = NULL, *y = NULL;
  for (uint32_t i = 0; i < c->nacc; i++) {
    if (c->acc[i].inst == a) x = &c->acc[i];
    if (c->acc[i].inst == b) y = &c->acc[i];
  }
  if (!x || !y || x->store || y->store) return false;
  const Inst *ia = &c->f->insts[a], *ib = &c->f->insts[b];
  return x->base == y->base && x->ivx == y->ivx && x->coeff == y->coeff &&
         x->disp == y->disp && x->mem == y->mem && x->width == y->width &&
         x->spec == y->spec && x->cond == y->cond && x->pol == y->pol &&
         ia->imm2 == ib->imm2 && ia->type == ib->type;
}

static bool v_affine(IrFunc *f, const uint8_t *inloop, Ref iv, Ref v, int depth,
                     uint64_t amask, Ref *base, uint64_t *coeff, uint64_t *off) {
  if (v == iv) { *base = REF_NONE; *coeff = 1; *off = 0; return true; }
  uint64_t c;
  if (is_const(f, v, &c)) { *base = REF_NONE; *coeff = 0; *off = c & amask; return true; }
  Inst *in = &f->insts[v];
  if (!inloop[in->block]) { *base = v; *coeff = 0; *off = 0; return true; }
  /* Go computes an address in 64 bits and wraps it to the 32 the memory
     takes: the same affine form, mod 2^32, with a 64-bit base the vector
     code wraps in turn. */
  if (in->op == I_WRAP && in->nargs == 1 && amask == 0xffffffffu && depth > 0)
    return v_affine(f, inloop, iv, ir_args(f, v)[0], depth - 1, amask, base, coeff, off);
  if (depth <= 0 || in->nargs != 2) return false;
  Ref *a = ir_args(f, v);
  Ref b0, b1;
  uint64_t c0, c1, o0, o1;
  switch (in->op) {
    case I_ADD:
      if (!v_affine(f, inloop, iv, a[0], depth - 1, amask, &b0, &c0, &o0)) return false;
      if (!v_affine(f, inloop, iv, a[1], depth - 1, amask, &b1, &c1, &o1)) return false;
      if (b0 != REF_NONE && b1 != REF_NONE) return false;
      *base = b0 != REF_NONE ? b0 : b1;
      *coeff = (c0 + c1) & amask;
      *off = (o0 + o1) & amask;
      return true;
    case I_SUB:
      if (!v_affine(f, inloop, iv, a[0], depth - 1, amask, &b0, &c0, &o0)) return false;
      if (!v_affine(f, inloop, iv, a[1], depth - 1, amask, &b1, &c1, &o1)) return false;
      if (b1 != REF_NONE) return false;
      *base = b0;
      *coeff = (c0 - c1) & amask;
      *off = (o0 - o1) & amask;
      return true;
    case I_MUL: case I_SHL: {
      uint64_t s;
      if (!is_const(f, a[1], &s)) return false;
      if (!v_affine(f, inloop, iv, a[0], depth - 1, amask, &b0, &c0, &o0)) return false;

      if (b0 != REF_NONE) return false;

      uint64_t m = (in->op == I_SHL)
                 ? (1ull << (s & (amask == ~0ull ? 63u : 31u))) : s;
      *base = REF_NONE;
      *coeff = (c0 * m) & amask;
      *off = (o0 * m) & amask;
      return true;
    }
    default: return false;
  }
}

static bool v_access_addr(IrFunc *f, const uint8_t *inloop, Ref iv, Ref acc, int depth,
                          uint64_t amask, Ref *base, uint64_t *coeff, uint64_t *off) {
  Inst *in = &f->insts[acc];
  Ref ea = ir_args(f, acc)[0];
  Inst *e = &f->insts[ea];
  uint64_t extra;
  Ref addr;
  if (e->op == I_MEM_PTR) {
    if (e->nargs != 1) return false;
    addr = ir_args(f, ea)[0];
    extra = in->imm;
  } else if (e->op == I_MEM_ADDR) {
    if (in->imm != 0) return false;
    addr = ir_args(f, ea)[0];
    if (e->nargs == 1) {
      extra = e->imm;
    } else if (e->nargs == 2) {
      Ref o = ir_args(f, ea)[1];
      if (f->insts[o].op == I_MEM_BASE) extra = f->insts[o].imm;
      else if (!is_const(f, o, &extra)) return false;
    } else {
      return false;
    }
  } else {
    return false;
  }
  if (!v_affine(f, inloop, iv, addr, depth, amask, base, coeff, off)) return false;
  *off = (*off + extra) & amask;
  return true;
}

static uint32_t v_natural_width(ValType t) {
  return vt_is_64(t) ? 8u : 4u;
}

static ValType v_acc_type(IrFunc *f, Ref r) {
  Inst *in = &f->insts[r];
  return in->op == I_LOAD ? (ValType)in->type
                          : (ValType)f->insts[ir_args(f, r)[1]].type;
}

static uint32_t v_lane_of(ValType t) {
  switch (t) {
    case VT_I32: return VLANE_I32;
    case VT_I64: return VLANE_I64;
    case VT_F32: return VLANE_F32;
    default:     return VLANE_F64;
  }
}

static bool v_acc_shape(IrFunc *f, Ref r, uint32_t *width, uint32_t *lane) {
  Inst *in = &f->insts[r];
  ValType vt = v_acc_type(f, r);
  if (vt_is_ref(vt) || vt == VT_V128) return false;
  uint32_t w = 1u << MEMF_SIZE(in->imm2);
  if (w == v_natural_width(vt)) { *width = w; *lane = v_lane_of(vt); return true; }
  if (vt_is_float(vt)) return false;
  if (w != 1 && w != 2 && w != 4) return false;
  *width = w;
  *lane = (w == 1) ? VLANE_I8 : (w == 2) ? VLANE_I16 : VLANE_I32;
  return true;
}

static Ref v_abs_operand(IrFunc *f, Ref r, uint32_t bits) {
  Inst *in = &f->insts[r];
  if (in->op != I_SUB || in->nargs != 2) return REF_NONE;
  Ref *a = ir_args(f, r);
  Ref sgn = a[1];
  Inst *si = &f->insts[sgn];
  if (si->op != I_SHRS || si->nargs != 2) return REF_NONE;
  uint64_t sh;
  if (!is_const(f, ir_args(f, sgn)[1], &sh) || sh != (uint64_t)bits - 1)
    return REF_NONE;
  Ref x = ir_args(f, sgn)[0];
  Inst *xi = &f->insts[a[0]];
  if (xi->op != I_XOR || xi->nargs != 2) return REF_NONE;
  Ref *xa = ir_args(f, a[0]);
  if (!((xa[0] == x && xa[1] == sgn) || (xa[0] == sgn && xa[1] == x)))
    return REF_NONE;
  return x;
}

/* x + (x << k) or (x << k) + x, which is how strength reduction leaves
   x * (2^k + 1), or (x << k) - x for x * (2^k - 1): x, and the multiplier
   in *mul.  bits is the lane's width. */
static Ref v_mul_form(IrFunc *f, Ref r, uint32_t bits, uint64_t *mul) {
  Inst *in = &f->insts[r];
  if ((in->op != I_ADD && in->op != I_SUB) || in->nargs != 2) return REF_NONE;
  Ref *a = ir_args(f, r);
  for (int side = 0; side < 2; side++) {
    if (in->op == I_SUB && side) break;
    Ref sh = a[side], x = a[side ^ 1];
    Inst *si = &f->insts[sh];
    uint64_t k;
    if (si->op != I_SHL || si->nargs != 2 || ir_args(f, sh)[0] != x ||
        !is_const(f, ir_args(f, sh)[1], &k) || k == 0 || k >= bits)
      continue;
    uint64_t lm = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    *mul = ((1ull << k) + (in->op == I_ADD ? 1u : (uint64_t)-1)) & lm;
    return x;
  }
  return REF_NONE;
}

static Ref v_counted_mask(IrFunc *f, Ref term) {
  Inst *in = &f->insts[term];
  if (in->op != I_VAND || in->nargs != 2) return REF_NONE;
  Ref *a = ir_args(f, term);
  Ref m, o;
  uint16_t o0 = f->insts[a[0]].op, o1 = f->insts[a[1]].op;
  bool m0 = o0 == I_VCMP || o0 == I_VNOT, m1 = o1 == I_VCMP || o1 == I_VNOT;
  if (m0 && !m1) { m = a[0]; o = a[1]; }
  else if (m1 && !m0) { m = a[1]; o = a[0]; }
  else return REF_NONE;

  if (f->insts[m].op == I_VNOT && f->insts[ir_args(f, m)[0]].op != I_VCMP)
    return REF_NONE;
  Inst *oi = &f->insts[o];
  if (oi->op != I_VSPLAT || oi->nargs != 1) return REF_NONE;
  uint64_t k;
  if (!is_const(f, ir_args(f, o)[0], &k) || k != 1) return REF_NONE;
  return m;
}

static bool v_narrow_safe(uint16_t op) {
  switch (op) {
    case I_ADD: case I_SUB: case I_MUL:
    case I_AND: case I_OR: case I_XOR: case I_SHL:
      return true;
    default:
      return false;
  }
}

static Ref vemit3(IrFunc *f, uint32_t b, IrOp op, ValType t, Ref a0, Ref a1,
                  Ref a2, uint64_t imm, uint64_t imm2) {
  Ref a[3] = { a0, a1, a2 };
  return ir_emit(f, b, op, t, a, 3, imm, imm2);
}

static bool v_minmax_form(IrFunc *f, Ref r, IrOp *vop) {
  Inst *in = &f->insts[r];
  if (in->op != I_SELECT || in->nargs != 3) return false;
  Ref *a = ir_args(f, r);
  Ref x = a[0], y = a[1];
  if (x == y) return false;
  Inst *c = &f->insts[a[2]];
  if (c->nargs != 2) return false;
  bool sgn, lt;
  switch (c->op) {
    case I_LTS: case I_LES: sgn = true;  lt = true;  break;
    case I_GTS: case I_GES: sgn = true;  lt = false; break;
    case I_LTU: case I_LEU: sgn = false; lt = true;  break;
    case I_GTU: case I_GEU: sgn = false; lt = false; break;
    default: return false;
  }
  Ref *ca = ir_args(f, a[2]);
  bool cmp_lhs_is_x;
  if (ca[0] == x && ca[1] == y) cmp_lhs_is_x = true;
  else if (ca[0] == y && ca[1] == x) cmp_lhs_is_x = false;
  else return false;

  bool is_min = (lt == cmp_lhs_is_x);
  *vop = sgn ? (is_min ? I_VSMIN : I_VSMAX) : (is_min ? I_VUMIN : I_VUMAX);
  return true;
}

static bool v_is_minmax_op(IrOp op) {
  return op == I_VSMIN || op == I_VSMAX || op == I_VUMIN || op == I_VUMAX;
}

static Ref v_scalar_minmax(IrFunc *f, uint32_t blk, IrOp vop, ValType t,
                           Ref x, Ref y) {
  IrOp c = vop == I_VSMIN ? I_LTS : vop == I_VSMAX ? I_GTS
         : vop == I_VUMIN ? I_LTU : I_GTU;
  Ref ca[2] = { x, y };
  Ref cond = ir_emit(f, blk, c, VT_I32, ca, 2, 0, 0);
  Ref sa[3] = { x, y, cond };
  return ir_emit(f, blk, I_SELECT, t, sa, 3, 0, 0);
}

/* v as the counter iv plus a constant, through adds and subtracts of
   constants (32-bit, wrapping): the index an argmin keeps is the counter, or
   the counter and an offset. */
static bool v_iv_offset(IrFunc *f, Ref v, Ref iv, uint32_t *off) {
  uint32_t acc = 0;
  for (int d = 0; d < VEC_DEPTH && v != iv; d++) {
    Inst *in = &f->insts[v];
    uint64_t c;
    if ((in->op != I_ADD && in->op != I_SUB) || in->nargs != 2 ||
        (ValType)in->type != VT_I32 || !is_const(f, ir_args(f, v)[1], &c))
      return false;
    acc += in->op == I_ADD ? (uint32_t)c : (uint32_t)0 - (uint32_t)c;
    v = ir_args(f, v)[0];
  }
  *off = acc;
  return v == iv;
}

static bool v_cmp_form(uint16_t op, uint64_t *vop, bool *swap_ops, bool *swap_arms) {
  *swap_ops = false;
  *swap_arms = false;
  switch (op) {
    case I_GTS: *vop = I_GTS; return true;
    case I_LTS: *vop = I_GTS; *swap_ops = true; return true;
    case I_GES: *vop = I_GES; return true;
    case I_LES: *vop = I_GES; *swap_ops = true; return true;
    case I_GTU: *vop = I_GTU; return true;
    case I_LTU: *vop = I_GTU; *swap_ops = true; return true;
    case I_GEU: *vop = I_GEU; return true;
    case I_LEU: *vop = I_GEU; *swap_ops = true; return true;
    case I_EQ:  *vop = I_EQ;  return true;
    case I_NE:  *vop = I_EQ;  *swap_arms = true; return true;
    default: return false;
  }
}

static Ref vemit(IrFunc *f, uint32_t b, IrOp op, ValType t, Ref a0, Ref a1,
                 uint64_t imm, uint64_t imm2) {
  Ref a[2] = { a0, a1 };
  uint32_t n = (a0 == REF_NONE) ? 0u : (a1 == REF_NONE ? 1u : 2u);
  return ir_emit(f, b, op, t, a, n, imm, imm2);
}

static IrOp v_minmax_of(IrFunc *f, Ref sel) {
  Inst *in = &f->insts[sel];
  if (in->nargs != 3) return I_NOP;
  Ref *a = ir_args(f, sel);
  Inst *c = &f->insts[a[2]];
  if (c->nargs != 2) return I_NOP;
  Ref *ca = ir_args(f, a[2]);
  bool fwd;
  if (ca[0] == a[0] && ca[1] == a[1]) fwd = true;
  else if (ca[0] == a[1] && ca[1] == a[0]) fwd = false;
  else return I_NOP;
  switch (c->op) {
    case I_GTS: case I_GES: return fwd ? I_VSMAX : I_VSMIN;
    case I_LTS: case I_LES: return fwd ? I_VSMIN : I_VSMAX;
    case I_GTU: case I_GEU: return fwd ? I_VUMAX : I_VUMIN;
    case I_LTU: case I_LEU: return fwd ? I_VUMIN : I_VUMAX;
    default: return I_NOP;
  }
}

static Ref v_build(VecCtx *c, const Ref *tuple, uint32_t k, uint32_t st, int depth);

static bool v_is_arm(IrFunc *f, uint32_t b, uint32_t other) {
  Block *bb = &f->blocks[b];
  return bb->npreds == 1 && bb->term == T_JUMP && bb->nsuccs == 1 &&
         bb->nparams == 0 && bb->succs[0].target == other;
}

static const VSel *v_sel_of(VecCtx *c, Ref v) {
  for (uint32_t i = 0; i < c->nsel; i++)
    if (c->sel[i].param == v) return &c->sel[i];
  return NULL;
}

static bool v_fits_lane_d(IrFunc *f, Ref v, uint32_t lane, int depth) {
  const Inst *in = &f->insts[v];
  uint32_t bits = 8u << (uint32_t)VLANE_SIZE(lane);
  if (bits >= 64) return true;
  if (in->op == I_CONST) return in->imm < (1ull << bits);
  if (in->op == I_LOAD && !(in->imm2 & MEMF_SIGNED) &&
      MEMF_SIZE(in->imm2) == (uint32_t)VLANE_SIZE(lane))
    return true;
  if (depth <= 0) return false;

  {
    uint64_t vop;
    bool so, sa;
    if (in->nargs == 2 && v_cmp_form(in->op, &vop, &so, &sa)) return true;
  }
  if (in->op == I_EQZ && in->nargs == 1) return true;
  if (in->nargs != 2) return false;
  Ref *a = ir_args(f, v);
  uint64_t c;
  switch (in->op) {

    case I_AND:
      return (is_const(f, a[0], &c) && c < (1ull << bits)) ||
             (is_const(f, a[1], &c) && c < (1ull << bits)) ||
             v_fits_lane_d(f, a[0], lane, depth - 1) ||
             v_fits_lane_d(f, a[1], lane, depth - 1);

    case I_SHRU:
      return is_const(f, a[1], &c) && c < bits &&
             v_fits_lane_d(f, a[0], lane, depth - 1);
    default:
      return false;
  }
}

static bool v_fits_lane(IrFunc *f, Ref v, uint32_t lane) {
  return v_fits_lane_d(f, v, lane, VEC_DEPTH);
}

/* v is the lane's signed value, sign-extended: a signed load as narrow as
   the lane, a sign extension from it, or a constant in its range. */
static bool v_fits_lane_signed(IrFunc *f, Ref v, uint32_t lane) {
  const Inst *in = &f->insts[v];
  uint32_t bits = 8u << (uint32_t)VLANE_SIZE(lane);
  if (bits >= 64) return true;
  if (in->op == I_CONST) {
    int64_t x = vt_is_64((ValType)in->type) ? (int64_t)in->imm
                                            : (int64_t)(int32_t)(uint32_t)in->imm;
    return x >= -(1ll << (bits - 1)) && x < (1ll << (bits - 1));
  }
  if (in->op == I_LOAD && (in->imm2 & MEMF_SIGNED) &&
      MEMF_SIZE(in->imm2) <= (uint32_t)VLANE_SIZE(lane))
    return true;
  return (in->op == I_EXT8S && bits >= 8) || (in->op == I_EXT16S && bits >= 16);
}

/* The integer lane of a value carried narrower than its type, in a loop
   whose accesses are w bytes wide. */
static uint32_t v_int_lane(uint32_t w) {
  return w == 1 ? VLANE_I8 : w == 2 ? VLANE_I16 : w == 4 ? VLANE_I32 : VLANE_I64;
}

static bool v_is_fcmp(uint16_t op) {
  return op == I_FEQ || op == I_FNE || op == I_FLT || op == I_FGT ||
         op == I_FLE || op == I_FGE;
}

/* The mask of a tuple of compares, for a value in c->lane.  An integer
   compare is made in that lane's integer form (an f32 select on i32 compares
   compares 32-bit lanes); a float compare in its operands' lane, which must
   be as wide: NEON's and SSE's compares are wasm's lane by lane, unordered
   operands included, so an f32 compare can choose between i32 values -- the
   bounds tests TinyGo puts around a saturating conversion -- but nothing
   narrower. */
static Ref v_mask(VecCtx *c, const Ref *tuple, uint32_t k, uint32_t st, int depth,
                  bool *swap_arms) {
  IrFunc *f = c->f;
  uint16_t op0 = f->insts[tuple[0]].op;
  uint64_t vop;
  bool swap_ops;
  bool fcmp = v_is_fcmp(op0);
  if (fcmp) {
    /* ne is not eq, unordered operands included, so it is eq's mask with
       the arms the other way round, as for integers */
    vop = op0 == I_FNE ? I_FEQ : op0;
    swap_ops = false;
    *swap_arms = op0 == I_FNE;
  } else if (!v_cmp_form(op0, &vop, &swap_ops, swap_arms)) {
    return REF_NONE;
  }
  for (uint32_t j = 0; j < k; j++) {
    Inst *ij = &f->insts[tuple[j]];
    if (ij->op != op0 || ij->nargs != 2 || !c->inloop[ij->block]) return REF_NONE;
  }

  ValType ct = (ValType)f->insts[ir_args(f, tuple[0])[0]].type;
  uint32_t ml;
  if (fcmp) {
    ml = v_lane_of(ct);
    if (!vt_is_float(ct) || VLANE_SIZE(ml) != VLANE_SIZE(c->lane)) return REF_NONE;
  } else {
    ml = VLANE_IS_FLOAT(c->lane) ? c->lane - VLANE_F32 + VLANE_I32 : c->lane;
    if (v_lane_of(ct) != ml) {

      if (op0 != I_EQ && op0 != I_NE) return REF_NONE;
      for (uint32_t j = 0; j < k; j++) {
        const Ref *aj = ir_args(f, tuple[j]);
        if (!v_fits_lane(f, aj[0], ml) || !v_fits_lane(f, aj[1], ml))
          return REF_NONE;
      }
    }
  }
  Ref sub[VEC_MAX_MEMB], x, y;
  for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[swap_ops ? 1 : 0];
  x = v_build(c, sub, k, st, depth - 1);
  if (x == REF_NONE) return REF_NONE;
  for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[swap_ops ? 0 : 1];
  y = v_build(c, sub, k, st, depth - 1);
  if (y == REF_NONE) return REF_NONE;
  return vemit(f, c->vbody, I_VCMP, VT_V128, x, y, vop, ml);
}

/* The unsigned saturating add and subtract of a narrow lane, as a producer
   without SIMD writes them in the wider type, where nothing wraps: x + y
   with a min against the lane's top -- TinyGo's s := a + b; if s < a
   { s = 255 } on bytes arrives as select(255, s, s >=u 255) on the sum of
   two load8_u -- and x - y with a signed max against zero, or taken only
   where x >= y and zero elsewhere.  Each is exact when x and y fit the lane
   unsigned: the sum is at most twice the top and the difference at least
   minus it, so the clamp is the saturation, and a compare of x with y sees
   the lanes' own values, signed or not.  The lane's UQADD or UQSUB, then;
   *x and *y are the operands. */
static IrOp v_mirror(IrOp op);

static IrOp v_sat_form(IrFunc *f, Ref sel, uint32_t lane, Ref *x, Ref *y) {
  Inst *in = &f->insts[sel];
  if (in->op != I_SELECT || in->nargs != 3) return I_NOP;
  Ref *a = ir_args(f, sel);
  uint32_t bits = 8u << (uint32_t)VLANE_SIZE(lane);
  uint64_t top = (1ull << bits) - 1, cv;
  IrOp mm = v_minmax_of(f, sel);
  for (uint32_t s = 0; s < 2; s++) {
    if (!is_const(f, a[s], &cv)) continue;
    Ref o = a[s ^ 1];
    Inst *oi = &f->insts[o];
    if ((oi->op != I_ADD && oi->op != I_SUB) || oi->nargs != 2) continue;
    Ref *oa = ir_args(f, o);
    IrOp op = I_NOP;
    if (oi->op == I_ADD && cv == top && (mm == I_VUMIN || mm == I_VSMIN)) {
      op = I_VADDSATU;
    } else if (oi->op == I_SUB && cv == 0 && mm == I_VSMAX) {
      op = I_VSUBSATU;
    } else if (oi->op == I_SUB && cv == 0) {
      Inst *ci = &f->insts[a[2]];
      if (ci->nargs != 2) continue;
      Ref *ca = ir_args(f, a[2]);
      IrOp r;
      if (ca[0] == oa[0] && ca[1] == oa[1]) r = (IrOp)ci->op;
      else if (ca[0] == oa[1] && ca[1] == oa[0]) r = v_mirror((IrOp)ci->op);
      else continue;
      bool below;
      switch (r) {
        case I_LTU: case I_LEU: case I_LTS: case I_LES: below = true; break;
        case I_GTU: case I_GEU: case I_GTS: case I_GES: below = false; break;
        default: continue;
      }
      /* the zero is the arm taken where x is below y */
      if (below != (s == 0)) continue;
      op = I_VSUBSATU;
    }
    if (op == I_NOP) continue;
    if (!v_fits_lane(f, oa[0], lane) || !v_fits_lane(f, oa[1], lane)) continue;
    *x = oa[0];
    *y = oa[1];
    return op;
  }
  /* The signed forms: x + y or x - y clamped to the lane's signed range by
     a min and a max, either way round, which is how Zig's +| on i16 comes
     out.  Exact for x and y the lane's signed values, for the same reasons:
     the sum or difference is within twice the range, and the clamp is the
     saturation.  SQADD and SQSUB. */
  if (mm != I_VSMIN && mm != I_VSMAX) return I_NOP;
  bool w64 = vt_is_64((ValType)in->type);
  int64_t lo = -(1ll << (bits - 1)), hi = (1ll << (bits - 1)) - 1;
  for (uint32_t s = 0; s < 2; s++) {
    if (!is_const(f, a[s], &cv)) continue;
    int64_t outer = w64 ? (int64_t)cv : (int64_t)(int32_t)(uint32_t)cv;
    if (outer != (mm == I_VSMIN ? hi : lo)) continue;
    Ref inner = a[s ^ 1];
    if (f->insts[inner].op != I_SELECT ||
        v_minmax_of(f, inner) != (mm == I_VSMIN ? I_VSMAX : I_VSMIN))
      continue;
    Ref *ia = ir_args(f, inner);
    for (uint32_t t = 0; t < 2; t++) {
      if (!is_const(f, ia[t], &cv)) continue;
      int64_t bound = w64 ? (int64_t)cv : (int64_t)(int32_t)(uint32_t)cv;
      if (bound != (mm == I_VSMIN ? lo : hi)) continue;
      Inst *oi = &f->insts[ia[t ^ 1]];
      if ((oi->op != I_ADD && oi->op != I_SUB) || oi->nargs != 2) continue;
      Ref *oa = ir_args(f, ia[t ^ 1]);
      if (!v_fits_lane_signed(f, oa[0], lane) || !v_fits_lane_signed(f, oa[1], lane))
        continue;
      *x = oa[0];
      *y = oa[1];
      return oi->op == I_ADD ? I_VADDSATS : I_VSUBSATS;
    }
  }
  return I_NOP;
}

/* The high half of a product a narrow lane cannot hold: (x * y) >> w, for
   x and y of the lane's w bits and the shift the lane's width, which is
   fixed-point code (mulhi16's uint16(uint32(a) * uint32(b) >> 16)).  Both
   fit the lane unsigned, or both signed, so the product is exact in the
   wider type, its bits w..2w-1 are the w bits kept whichever way it is
   shifted, and those are what NEON's widening multiply of each half (UMULL
   and UMULL2, or SMULL) puts in the odd lanes of the pair, which UZP2
   gathers. */
static Ref v_mulhi(VecCtx *c, const Ref *tuple, uint32_t k, uint32_t st, int depth) {
  IrFunc *f = c->f;
  if (c->lane != VLANE_I8 && c->lane != VLANE_I16) return REF_NONE;
  uint32_t w = 8u << (uint32_t)VLANE_SIZE(c->lane);
  uint16_t op0 = f->insts[tuple[0]].op;
  if (op0 != I_SHRU && op0 != I_SHRS) return REF_NONE;
  Ref xs[VEC_MAX_MEMB], ys[VEC_MAX_MEMB];
  int sgn = -1;
  for (uint32_t j = 0; j < k; j++) {
    Inst *ij = &f->insts[tuple[j]];
    uint64_t sh;
    if (ij->op != op0 || ij->nargs != 2 || !c->inloop[ij->block] ||
        !is_const(f, ir_args(f, tuple[j])[1], &sh) ||
        (sh & (vt_is_64((ValType)ij->type) ? 63u : 31u)) != w)
      return REF_NONE;
    Ref m = ir_args(f, tuple[j])[0];
    Inst *mi = &f->insts[m];
    if (mi->op != I_MUL || mi->nargs != 2 || !c->inloop[mi->block]) return REF_NONE;
    Ref *ma = ir_args(f, m);
    int sj = (v_fits_lane(f, ma[0], c->lane) && v_fits_lane(f, ma[1], c->lane)) ? 0
           : (v_fits_lane_signed(f, ma[0], c->lane) &&
              v_fits_lane_signed(f, ma[1], c->lane)) ? 1 : -1;
    if (sj < 0 || (j && sj != sgn)) return REF_NONE;
    sgn = sj;
    xs[j] = ma[0];
    ys[j] = ma[1];
  }
  Ref x = v_build(c, xs, k, st, depth - 1);
  if (x == REF_NONE) return REF_NONE;
  Ref y = v_build(c, ys, k, st, depth - 1);
  if (y == REF_NONE) return REF_NONE;
  Ref lo = vemit(c->f, c->vbody, I_VEXTMUL, VT_V128, x, y, (uint64_t)sgn, c->lane + 1);
  Ref hi = vemit(c->f, c->vbody, I_VEXTMUL, VT_V128, x, y, (uint64_t)sgn | 2u,
                 c->lane + 1);
  return vemit(c->f, c->vbody, I_VUZP2, VT_V128, lo, hi, VLANE_SIZE(c->lane), c->lane);
}

#define VBFAIL(w) do { if (!c->fail) c->fail = (w); return REF_NONE; } while (0)

static Ref v_build(VecCtx *c, const Ref *tuple, uint32_t k, uint32_t st, int depth) {
  IrFunc *f = c->f;
  for (uint32_t i = 0; i < c->nmemo; i++) {
    if (c->memo[i].n != k || c->memo[i].st != st) continue;
    bool same = true;
    for (uint32_t j = 0; j < k && same; j++) same = c->memo[i].key[j] == tuple[j];
    if (same) return c->memo[i].val;
  }
  if (depth <= 0) VBFAIL("expression is too deep");

  ValType t = (ValType)f->insts[tuple[0]].type;
  for (uint32_t j = 1; j < k; j++)
    if ((ValType)f->insts[tuple[j]].type != t) VBFAIL("lanes have different types");

  bool narrow = c->width < v_natural_width(t);

  /* A value takes the lane of its own type where the loop's lanes are its
     natural width -- the f32 between cvtf32's i32 load and i32 store, the
     i32 an f32 loop compares -- and the loop's integer lane where it is
     carried narrower than its type.  The lanes are as wide either way, so
     a vector holds as many of each; c->lane is the lane of the value being
     built while it is built. */
  uint32_t want = c->lane;
  if (narrow) {
    if (vt_is_float(t) || vt_is_ref(t) || t == VT_V128)
      VBFAIL("a narrow access of a type that has no narrow lane");
    want = v_int_lane(c->width);
  } else if (v_lane_of(t) != c->lane) {
    if (c->width != v_natural_width(t) || vt_is_ref(t) || t == VT_V128)
      VBFAIL("value is not the loop's lane shape");
    want = v_lane_of(t);
  }
  if (want != c->lane) {
    uint32_t outer = c->lane;
    c->lane = want;
    Ref r = v_build(c, tuple, k, st, depth);
    c->lane = outer;
    return r;
  }

  Ref out = REF_NONE;

  const uint16_t op0 = f->insts[tuple[0]].op;
  const uint32_t nargs0 = f->insts[tuple[0]].nargs;
  const uint32_t block0 = f->insts[tuple[0]].block;
  const uint64_t imm0 = f->insts[tuple[0]].imm;

  bool uniform = true;
  for (uint32_t j = 1; j < k && uniform; j++) uniform = tuple[j] == tuple[0];
  if (uniform && op0 == I_CONST) {

    Ref c2 = ir_emit(f, c->vguard, I_CONST, t, NULL, 0, imm0, 0);
    out = vemit(f, c->vguard, I_VSPLAT, VT_V128, c2, REF_NONE, 0, c->lane);
    goto done;
  }
  if (uniform && !c->inloop[block0]) {
    out = vemit(f, c->vguard, I_VSPLAT, VT_V128, tuple[0], REF_NONE, 0, c->lane);
    goto done;
  }

  if (op0 == I_PARAM && v_sel_of(c, tuple[0])) {
    Ref ct[VEC_MAX_MEMB], tt[VEC_MAX_MEMB], ft[VEC_MAX_MEMB];
    for (uint32_t j = 0; j < k; j++) {
      const VSel *sv = v_sel_of(c, tuple[j]);
      if (!sv) return REF_NONE;
      ct[j] = sv->cond; tt[j] = sv->t; ft[j] = sv->fv;
    }
    bool swap_arms;
    Ref mask = v_mask(c, ct, k, st, depth, &swap_arms);
    if (mask == REF_NONE) return REF_NONE;
    Ref vt = v_build(c, swap_arms ? ft : tt, k, st, depth - 1);
    if (vt == REF_NONE) return REF_NONE;
    Ref vf = v_build(c, swap_arms ? tt : ft, k, st, depth - 1);
    if (vf == REF_NONE) return REF_NONE;
    out = vemit3(f, c->vbody, I_VSEL, VT_V128, mask, vt, vf, 0, c->lane);
    goto done;
  }

  {
    uint64_t cvop;
    bool cswap_ops, cswap_arms;
    if ((v_cmp_form(op0, &cvop, &cswap_ops, &cswap_arms) || v_is_fcmp(op0)) &&
        !vt_is_float(t)) {
      Ref mask = v_mask(c, tuple, k, st, depth, &cswap_arms);
      if (mask == REF_NONE) return REF_NONE;

      if (cswap_arms)
        mask = vemit(f, c->vbody, I_VNOT, VT_V128, mask, REF_NONE, 0, c->lane);
      Ref one = ir_emit(f, c->vguard, I_CONST, t, NULL, 0, 1, 0);
      Ref ones = vemit(f, c->vguard, I_VSPLAT, VT_V128, one, REF_NONE, 0, c->lane);
      out = vemit(f, c->vbody, I_VAND, VT_V128, mask, ones, 0, c->lane);
      goto done;
    }
  }

  if (op0 == I_LOAD) {

    if (k * st != c->mpv) return REF_NONE;
    for (uint32_t g = 0; g < c->ngrp; g++) {
      VGroup *G = &c->grp[g];
      if (G->store) continue;
      for (uint32_t v = 0; v < G->nv; v++)
        for (uint32_t off = 0; off < st; off++) {
          bool same = true;
          for (uint32_t j = 0; j < k && same; j++)
            same = v_same_load(c, G->member[v_slot(G, c->mpv, v, off + j * st)],
                               tuple[j]);
          if (!same) continue;
          Ref ld = G->loaded[c->copy][v];
          if (st == 1) { out = ld; goto done; }

          if (st != 2) return REF_NONE;
          out = vemit(f, c->vbody, off ? I_VUZP2 : I_VUZP1, VT_V128,
                      ld, ld, c->gran, c->lane);
          goto done;
        }
    }

    VBFAIL("load is in no group");
  }

  bool mixed = false;
  for (uint32_t j = 1; j < k; j++) {
    Inst *inj = &f->insts[tuple[j]];
    if (inj->op != op0 || inj->nargs != nargs0 || !c->inloop[inj->block])
      mixed = true;
  }
  if (!c->inloop[block0]) return REF_NONE;
  if (mixed) {
    if (st != 1 || (k & 1) || k < 2 || depth <= 0) return REF_NONE;
    Ref half[2], sub[VEC_MAX_MEMB];
    for (uint32_t p = 0; p < 2; p++) {
      for (uint32_t j = 0; j * 2 + p < k; j++) sub[j] = tuple[j * 2 + p];
      half[p] = v_build(c, sub, k / 2, 2, depth - 1);
      if (half[p] == REF_NONE) return REF_NONE;
    }
    out = vemit(f, c->vbody, I_VZIP1, VT_V128, half[0], half[1], c->gran, c->lane);
    goto done;
  }

  IrOp vop = I_NOP;
  bool flt = VLANE_IS_FLOAT(c->lane);
  /* A select is as narrow as its arms, given a mask exact in the lane,
     which v_mask makes sure of. */
  if (narrow && !v_narrow_safe(op0) && op0 != I_SELECT) {

    bool ok = true;
    for (uint32_t j = 0; j < k && ok; j++) {
      Inst *ij = &f->insts[tuple[j]];
      ok = ij->op == I_SHRU && ij->nargs == 2 &&
           v_fits_lane(f, ir_args(f, tuple[j])[0], c->lane);
    }
    if (!ok) {
      out = v_mulhi(c, tuple, k, st, depth);
      if (out != REF_NONE) goto done;
      VBFAIL(ir_op_name(op0));
    }
  }
  switch (op0) {
    case I_ADD: if (!flt) vop = I_VADD; break;
    case I_SUB: if (!flt) vop = I_VSUB; break;

    case I_MUL: if (!flt && c->width != 8) vop = I_VMUL; break;
    case I_AND: if (!flt) vop = I_VAND; break;
    case I_OR:  if (!flt) vop = I_VOR;  break;
    case I_XOR: if (!flt) vop = I_VXOR; break;
    case I_FADD: if (flt) vop = I_VFADD; break;
    case I_FSUB: if (flt) vop = I_VFSUB; break;
    case I_FMUL: if (flt) vop = I_VFMUL; break;
    case I_FDIV: if (flt) vop = I_VFDIV; break;
    case I_FNEG: if (flt) vop = I_VFNEG; break;
    case I_FABS: if (flt) vop = I_VFABS; break;
    case I_FSQRT: if (flt) vop = I_VFSQRT; break;
    /* f32x4.min and max are f32.min and max lane by lane: a NaN wins and
       -0 is below +0 in both */
    case I_FMIN: if (flt) vop = I_VFMIN; break;
    case I_FMAX: if (flt) vop = I_VFMAX; break;
    case I_SHL: case I_SHRU: case I_SHRS:
    case I_ROTL: case I_ROTR: if (!flt) vop = (IrOp)op0; break;
    /* REV16, REV32 or REV64 on bytes: bswap's lane form, one instruction
       for four of bits.ReverseBytes32 (vecbench/bswap) where the scalar loop
       has a REV each */
    case I_BSWAP: if (!flt) vop = I_VREV; break;
    default: break;
  }
  if (op0 == I_SELECT) {
    Ref sub[VEC_MAX_MEMB];
    /* NEON's and SSE's saturating forms are of bytes and halfwords only */
    if (narrow && (c->lane == VLANE_I8 || c->lane == VLANE_I16)) {
      Ref ys[VEC_MAX_MEMB];
      IrOp sop = v_sat_form(f, tuple[0], c->lane, &sub[0], &ys[0]);
      for (uint32_t j = 1; j < k && sop != I_NOP; j++)
        if (v_sat_form(f, tuple[j], c->lane, &sub[j], &ys[j]) != sop) sop = I_NOP;
      if (sop != I_NOP) {
        Ref x = v_build(c, sub, k, st, depth - 1);
        if (x == REF_NONE) return REF_NONE;
        Ref y = v_build(c, ys, k, st, depth - 1);
        if (y == REF_NONE) return REF_NONE;
        out = vemit(f, c->vbody, sop, VT_V128, x, y, 0, c->lane);
        goto done;
      }
    }
    vop = (flt || narrow || c->width > 4) ? I_NOP : v_minmax_of(f, tuple[0]);
    if (vop != I_NOP) {
      for (uint32_t j = 1; j < k; j++)
        if (v_minmax_of(f, tuple[j]) != vop) return REF_NONE;
      for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[0];
      Ref x = v_build(c, sub, k, st, depth - 1);
      if (x == REF_NONE) return REF_NONE;
      for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[1];
      Ref y = v_build(c, sub, k, st, depth - 1);
      if (y == REF_NONE) return REF_NONE;
      out = vemit(f, c->vbody, vop, VT_V128, x, y, 0, c->lane);
      goto done;
    }
    /* Any other select on a compare is a bitwise select on the compare's
       mask, exact for floats too: wasm's select moves bits.  f32::min and
       max arrive as x != x ? y : x and a compare of the results (rsbench's
       f32ops), and TinyGo's saturating float-to-int conversion as selects
       of the bounds on f32 compares (vecbench/cvtf32). */
    Ref ct[VEC_MAX_MEMB], tt[VEC_MAX_MEMB], ft[VEC_MAX_MEMB];
    for (uint32_t j = 0; j < k; j++) {
      Ref *aj = ir_args(f, tuple[j]);
      tt[j] = aj[0]; ft[j] = aj[1]; ct[j] = aj[2];
    }
    bool swap_arms;
    Ref mask = v_mask(c, ct, k, st, depth, &swap_arms);
    if (mask == REF_NONE) VBFAIL(ir_op_name(op0));
    Ref vt = v_build(c, swap_arms ? ft : tt, k, st, depth - 1);
    if (vt == REF_NONE) return REF_NONE;
    Ref vf = v_build(c, swap_arms ? tt : ft, k, st, depth - 1);
    if (vf == REF_NONE) return REF_NONE;
    out = vemit3(f, c->vbody, I_VSEL, VT_V128, mask, vt, vf, 0, c->lane);
    goto done;
  }
  /* i32 and f32 convert lane for lane, SCVTF and FCVTZS on .4s, rounding
     and saturating as the scalar forms do; the pairs with f64 work on half
     a vector of i32, so this pair is the only one with a lane each way. */
  if (op0 == I_CONVERTS || op0 == I_CONVERTU ||
      op0 == I_TRUNC_SATS || op0 == I_TRUNC_SATU) {
    bool tof = op0 == I_CONVERTS || op0 == I_CONVERTU;
    if (narrow || t != (tof ? VT_F32 : VT_I32)) VBFAIL(ir_op_name(op0));
    Ref sub[VEC_MAX_MEMB];
    for (uint32_t j = 0; j < k; j++) {
      if (f->insts[tuple[j]].imm != (uint64_t)(tof ? VT_I32 : VT_F32))
        VBFAIL(ir_op_name(op0));
      sub[j] = ir_args(f, tuple[j])[0];
    }
    Ref x = v_build(c, sub, k, st, depth - 1);
    if (x == REF_NONE) return REF_NONE;
    bool sgn = op0 == I_CONVERTS || op0 == I_TRUNC_SATS;
    out = vemit(f, c->vbody, tof ? I_VCONVERT : I_VTRUNCSAT, VT_V128, x,
                REF_NONE, sgn ? 1u : 0u, VLANE_F32);
    goto done;
  }
  /* The high word of a 32-bit product, which is how a division by a
     constant arrives: UMULL and UMULL2 into .2d and the odd words of the
     pair. */
  if (op0 == I_MULHU) {
    if (narrow || t != VT_I32) VBFAIL(ir_op_name(op0));
    Ref sub[VEC_MAX_MEMB];
    for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[0];
    Ref x = v_build(c, sub, k, st, depth - 1);
    if (x == REF_NONE) return REF_NONE;
    for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[1];
    Ref y = v_build(c, sub, k, st, depth - 1);
    if (y == REF_NONE) return REF_NONE;
    Ref lo = vemit(f, c->vbody, I_VEXTMUL, VT_V128, x, y, 0, VLANE_I64);
    Ref hi = vemit(f, c->vbody, I_VEXTMUL, VT_V128, x, y, 2, VLANE_I64);
    out = vemit(f, c->vbody, I_VUZP2, VT_V128, lo, hi, 2, VLANE_I32);
    goto done;
  }
  if (vop == I_NOP) VBFAIL(ir_op_name(op0));

  if (op0 == I_SHL || op0 == I_SHRU || op0 == I_SHRS) {

    uint64_t sh;
    bool allconst = is_const(f, ir_args(f, tuple[0])[1], &sh);
    for (uint32_t j = 1; j < k && allconst; j++) {
      uint64_t s2;
      if (!is_const(f, ir_args(f, tuple[j])[1], &s2) || s2 != sh) allconst = false;
    }

    if (!allconst) goto shift_var;

    sh &= (uint64_t)(vt_is_64(t) ? 63u : 31u);
    if (sh >= (uint64_t)c->width * 8u) return REF_NONE;
    Ref sub[VEC_MAX_MEMB];
    for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[0];
    Ref x = v_build(c, sub, k, st, depth - 1);
    if (x == REF_NONE) return REF_NONE;
    if (sh == 0) { out = x; goto done; }
    IrOp o = op0 == I_SHL ? I_VSHL : (op0 == I_SHRU ? I_VSHRU : I_VSHRS);
    out = vemit(f, c->vbody, o, VT_V128, x, REF_NONE, sh, c->lane);
    goto done;
  }

shift_var:
  if (op0 == I_SHL || op0 == I_SHRU || op0 == I_SHRS) {

    if (narrow) return REF_NONE;
    Ref amt = ir_args(f, tuple[0])[1];
    for (uint32_t j = 1; j < k; j++)
      if (ir_args(f, tuple[j])[1] != amt) return REF_NONE;
    if (c->inloop[f->insts[amt].block]) return REF_NONE;
    Ref sub[VEC_MAX_MEMB];
    for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[0];
    Ref x = v_build(c, sub, k, st, depth - 1);
    if (x == REF_NONE) return REF_NONE;
    /* A left shift by an amount the loop does not change is a multiply by
       2^amount, splatted once before the loop: the shift by a register
       masks and splats its amount at every use, three instructions a
       vector and twelve an iteration of four copies (vecbench/varshift went
       from 1.29x native to 0.94x). */
    if (op0 == I_SHL && ir_vec_mul_one(c->lane)) {
      Ref one = ir_emit(f, c->vguard, I_CONST, t, NULL, 0, 1, 0);
      Ref pw = vemit(f, c->vguard, I_SHL, t, one, amt, 0, 0);
      Ref ps = vemit(f, c->vguard, I_VSPLAT, VT_V128, pw, REF_NONE, 0, c->lane);
      out = vemit(f, c->vbody, I_VMUL, VT_V128, x, ps, 0, c->lane);
      goto done;
    }
    IrOp o = op0 == I_SHL ? I_VSHLV : (op0 == I_SHRU ? I_VSHRUV : I_VSHRSV);
    out = vemit(f, c->vbody, o, VT_V128, x, amt, 0, c->lane);
    goto done;
  }

  if (op0 == I_SUB && !narrow) {

    uint32_t bits = (uint32_t)c->width * 8u;
    Ref sub[VEC_MAX_MEMB];
    bool all = true;
    for (uint32_t j = 0; j < k && all; j++) {
      sub[j] = v_abs_operand(f, tuple[j], bits);
      all = sub[j] != REF_NONE;
    }
    if (all) {
      Ref x = v_build(c, sub, k, st, depth - 1);
      if (x != REF_NONE) {
        out = vemit(f, c->vbody, I_VABS, VT_V128, x, REF_NONE, 0, c->lane);
        goto done;
      }
    }
  }

  /* One multiply for the shift and add strength reduction made of a
     multiply by a constant: a loop of copies runs out of vector
     instructions first (vecbench/scale, c[i] = a[i]*3 + 7, went from 1.19x
     native to 0.86x). */
  if ((op0 == I_ADD || op0 == I_SUB) && ir_vec_mul_one(c->lane)) {
    uint32_t bits = (uint32_t)c->width * 8u;
    Ref sub[VEC_MAX_MEMB];
    uint64_t m0 = 0;
    bool all = true;
    for (uint32_t j = 0; j < k && all; j++) {
      uint64_t mj;
      sub[j] = v_mul_form(f, tuple[j], bits, &mj);
      all = sub[j] != REF_NONE && (j == 0 || mj == m0);
      m0 = mj;
    }
    if (all) {
      Ref x = v_build(c, sub, k, st, depth - 1);
      if (x != REF_NONE) {
        Ref mc = ir_emit(f, c->vguard, I_CONST, t, NULL, 0, m0, 0);
        Ref ms = vemit(f, c->vguard, I_VSPLAT, VT_V128, mc, REF_NONE, 0, c->lane);
        out = vemit(f, c->vbody, I_VMUL, VT_V128, x, ms, 0, c->lane);
        goto done;
      }
    }
  }

  if (op0 == I_ROTL || op0 == I_ROTR) {

    uint32_t bits = (uint32_t)c->width * 8u;
    uint64_t sh;
    if (!is_const(f, ir_args(f, tuple[0])[1], &sh)) return REF_NONE;
    for (uint32_t j = 1; j < k; j++) {
      uint64_t s2;
      if (!is_const(f, ir_args(f, tuple[j])[1], &s2) || s2 != sh) return REF_NONE;
    }
    sh %= (uint64_t)bits;
    Ref sub[VEC_MAX_MEMB];
    for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[0];
    Ref x = v_build(c, sub, k, st, depth - 1);
    if (x == REF_NONE) return REF_NONE;

    uint64_t l = (op0 == I_ROTL) ? sh : (uint64_t)bits - sh;
    Ref hi = vemit(f, c->vbody, I_VSHL, VT_V128, x, REF_NONE, l, c->lane);
    Ref lo = vemit(f, c->vbody, I_VSHRU, VT_V128, x, REF_NONE,
                   (uint64_t)bits - l, c->lane);
    out = vemit(f, c->vbody, I_VOR, VT_V128, hi, lo, 0, c->lane);
    goto done;
  }

  {
    Ref sub[VEC_MAX_MEMB];
    Ref x, y = REF_NONE;
    for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[0];
    x = v_build(c, sub, k, st, depth - 1);
    if (x == REF_NONE) return REF_NONE;
    if (nargs0 == 2) {
      for (uint32_t j = 0; j < k; j++) sub[j] = ir_args(f, tuple[j])[1];
      y = v_build(c, sub, k, st, depth - 1);
      if (y == REF_NONE) return REF_NONE;
    }
    out = vemit(f, c->vbody, vop, VT_V128, x, y, 0, c->lane);
  }

done:
  if (c->nmemo < VEC_MAX_MEMO) {
    VMemo *m = &c->memo[c->nmemo++];
    m->n = k; m->st = st;
    for (uint32_t j = 0; j < k; j++) m->key[j] = tuple[j];
    m->val = out;
  }
  return out;
}

/* Forgets what v_build made for one copy of the body before it builds the
   next, or another body: a load, and anything computed from one, stands for
   different elements there.  What it made in the guard -- a constant or a
   loop-invariant value splatted -- is the same for every copy and stays. */
static void v_memo_next_copy(VecCtx *c) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < c->nmemo; i++)
    if (c->f->insts[c->memo[i].val].block == c->vguard) c->memo[n++] = c->memo[i];
  c->nmemo = n;
}

static int v_lane_of_term(VecCtx *c, Ref v, int depth) {
  IrFunc *f = c->f;
  if (depth <= 0) return -1;
  Inst *in = &f->insts[v];
  if (in->op == I_LOAD) {
    for (uint32_t g = 0; g < c->ngrp; g++)
      for (uint32_t t = 0; t < c->grp[g].n; t++)
        if (c->grp[g].member[t] == v) return (int)t;
    return -1;
  }
  if (!c->inloop[in->block]) return -1;
  Ref *a = ir_args(f, v);
  for (uint32_t j = 0; j < in->nargs; j++) {
    int r = v_lane_of_term(c, a[j], depth - 1);
    if (r >= 0) return r;
  }
  return -1;
}

static IrOp v_continue_rel(IrFunc *f, Ref cmp, bool taken_continues,
                           Ref *ivside, Ref *bound) {
  Inst *in = &f->insts[cmp];
  if (in->nargs != 2) return I_NOP;
  IrOp op = (IrOp)in->op;
  if (!taken_continues) {
    switch (op) {
      case I_EQ:  op = I_NE;  break;   case I_NE:  op = I_EQ;  break;
      case I_LTU: op = I_GEU; break;   case I_GEU: op = I_LTU; break;
      case I_LTS: op = I_GES; break;   case I_GES: op = I_LTS; break;
      case I_LEU: op = I_GTU; break;   case I_GTU: op = I_LEU; break;
      case I_LES: op = I_GTS; break;   case I_GTS: op = I_LES; break;
      default: return I_NOP;
    }
  }
  *ivside = ir_args(f, cmp)[0];
  *bound = ir_args(f, cmp)[1];
  return op;
}

static IrOp v_mirror(IrOp op) {
  switch (op) {
    case I_EQ: case I_NE: return op;
    case I_LTU: return I_GTU;  case I_GTU: return I_LTU;
    case I_LTS: return I_GTS;  case I_GTS: return I_LTS;
    case I_LEU: return I_GEU;  case I_GEU: return I_LEU;
    case I_LES: return I_GES;  case I_GES: return I_LES;
    default: return I_NOP;
  }
}

static uint32_t v_log2(uint64_t x) {
  uint32_t n = 0;
  while ((1ull << n) < x) n++;
  return n;
}

/* Emits into gb the runtime count of U-wide rounds for a counted loop whose
   step is the constant K: how many full U-iteration groups run before
   the last 1..U iterations, i.e. cnt = (T-1) >> log2(U) with T the exact
   iteration count derived from |bound - q0|.  *ok_out accumulates every
   safety term (an NE bound off the step grid, no headroom for the final
   increment, a zero cnt) -- when it is false the caller must branch to the
   untouched original loop, which is also what finishes the last 1..U
   iterations when it is true.  Shared by the vectorizer (U = lanes) and
   the unroller's guarded form (U = unroll factor); rel is the normalized
   continue-relation, down the normalized direction, and bound REF_NONE
   means an implicit zero.  With whole the count is of every full group,
   T >> log2(U), for a caller that can leave past the loop when no
   iteration is left over; *T_out, if asked for, is then T itself.  A step
   that is not a power of two divides instead of shifting: a loop LLVM
   unrolled by three walks its array 12 or 24 bytes at a time. */
/* D >> ks, its shift amount made even when it is zero, as it always was */
static Ref gt_shr(IrFunc *f, uint32_t gb, ValType ct, Ref D, uint32_t ks) {
  Ref sh = ir_emit(f, gb, I_CONST, ct, NULL, 0, ks, 0);
  return ks ? vemit(f, gb, I_SHRU, ct, D, sh, 0, 0) : D;
}

static Ref guard_trip_count(IrFunc *f, uint32_t gb, ValType ct, IrOp rel,
                            uint64_t K, bool down, Ref q0, Ref bound,
                            bool clone_bound, uint32_t U, bool whole,
                            Ref *T_out, Ref *ok_out) {
  uint32_t cbits = ct == VT_I64 ? 64 : 32;
  uint32_t ks = v_log2(K), us = v_log2(U);
  Ref zero = ir_emit(f, gb, I_CONST, ct, NULL, 0, 0, 0);
  Ref N = bound == REF_NONE ? zero
        : (clone_bound ? ir_emit(f, gb, I_CONST, ct, NULL, 0,
                                 f->insts[bound].imm, 0)
                       : bound);
  Ref one = ir_emit(f, gb, I_CONST, ct, NULL, 0, 1, 0);

  Ref Dsub = down ? N : q0, Dfrom = down ? q0 : N;
  uint64_t dsubc;
  Ref D = (is_const(f, Dsub, &dsubc) && dsubc == 0)
        ? Dfrom : vemit(f, gb, I_SUB, ct, Dfrom, Dsub, 0, 0);
  Ref T, ok;
  uint64_t cmask = ct == VT_I64 ? ~0ull : 0xffffffffu;
  bool p2 = (K & (K - 1)) == 0;
  Ref Kc = p2 ? REF_NONE : ir_emit(f, gb, I_CONST, ct, NULL, 0, K & cmask, 0);
#define GT_REM() (p2 ? vemit(f, gb, I_AND, ct, D,                           \
                         ir_emit(f, gb, I_CONST, ct, NULL, 0, (K - 1) & cmask, 0), 0, 0) \
                     : vemit(f, gb, I_REMU, ct, D, Kc, 0, 0))
#define GT_QUO() (p2 ? gt_shr(f, gb, ct, D, ks)                            \
                     : vemit(f, gb, I_DIVU, ct, D, Kc, 0, 0))
  if (rel == I_NE) {

    Ref rem = GT_REM();
    Ref remz = vemit(f, gb, I_EQ, VT_I32, rem, zero, 0, 0);
    Ref dnz = vemit(f, gb, I_NE, VT_I32, D, zero, 0, 0);
    ok = vemit(f, gb, I_AND, VT_I32, remz, dnz, 0, 0);
    T = GT_QUO();
  } else {
    bool sgn = (rel == I_LTS || rel == I_LES || rel == I_GTS || rel == I_GES);
    bool incl = (rel == I_LEU || rel == I_LES || rel == I_GEU || rel == I_GES);

    Ref cap, room;
    if (!down) {
      uint64_t top = sgn ? ((1ull << (cbits - 1)) - 1 - K) : (cmask - K);
      cap = ir_emit(f, gb, I_CONST, ct, NULL, 0, top & cmask, 0);
      room = vemit(f, gb, sgn ? I_LES : I_LEU, VT_I32, N, cap, 0, 0);
      ok = vemit(f, gb, sgn ? I_GTS : I_GTU, VT_I32, N, q0, 0, 0);
    } else {
      uint64_t bot = sgn ? ((1ull << (cbits - 1)) + K) : K;
      cap = ir_emit(f, gb, I_CONST, ct, NULL, 0, bot & cmask, 0);
      room = vemit(f, gb, sgn ? I_GES : I_GEU, VT_I32, N, cap, 0, 0);
      ok = vemit(f, gb, sgn ? I_GTS : I_GTU, VT_I32, q0, N, 0, 0);
    }
    ok = vemit(f, gb, I_AND, VT_I32, ok, room, 0, 0);
    Ref quo = GT_QUO();
    if (incl) {

      T = vemit(f, gb, I_ADD, ct, quo, one, 0, 0);
    } else if (K > 1) {
      Ref rem = GT_REM();
      Ref rnz = vemit(f, gb, I_NE, VT_I32, rem, zero, 0, 0);
      if (ct == VT_I64) rnz = vemit(f, gb, I_EXTENDU, VT_I64, rnz, REF_NONE, 0, 0);
      T = vemit(f, gb, I_ADD, ct, quo, rnz, 0, 0);
    } else {
      T = quo;
    }
  }

  Ref M = whole ? T : vemit(f, gb, I_SUB, ct, T, one, 0, 0);
  Ref ush = ir_emit(f, gb, I_CONST, ct, NULL, 0, us, 0);
  Ref cnt = us ? vemit(f, gb, I_SHRU, ct, M, ush, 0, 0) : M;
  Ref cntnz = vemit(f, gb, I_NE, VT_I32, cnt, zero, 0, 0);
  ok = vemit(f, gb, I_AND, VT_I32, ok, cntnz, 0, 0);

  if (ct == VT_I64) {
    Ref cap = ir_emit(f, gb, I_CONST, ct, NULL, 0, 0x0fffffffull, 0);
    ok = vemit(f, gb, I_AND, VT_I32, ok,
               vemit(f, gb, I_LEU, VT_I32, cnt, cap, 0, 0), 0, 0);
  }
  *ok_out = ok;
  if (T_out) *T_out = T;
  return cnt;
#undef GT_REM
#undef GT_QUO
}

/* How many copies of the body an iteration of the vector loop holds where
   nothing keeps it to fewer: the target's say, or WARMC_VEC_COPIES (1, 2
   or 4), for measuring.  Four on arm64: two was the most an elementwise
   loop needed to keep up with native (vecbench/addi32 1.00x), but a
   reduction's accumulators wait on adds of two cycles or more, and with
   four, vecbench/dotint ran 0.54x native where two left it 0.72x. */
static uint32_t v_copies_wanted(void) {
  const char *e = warmc_getenv("WARMC_VEC_COPIES");
  uint32_t n = e ? (uint32_t)atoi(e) : ir_vec_copies();
  return n >= 4 ? 4u : n >= 2 ? 2u : 1u;
}

/* One vector loop: its block, the copies of the body it holds, its
   parameters and what it passes itself for the next iteration. */
typedef struct {
  uint32_t blk, R;
  Ref pptr[VEC_MAX_GRP], pcnt, pacc[VEC_MAX_RED][VEC_MAX_COPY];
  Ref ppay[VEC_MAX_PAY], ppos;
  Ref nptr[VEC_MAX_GRP], ncnt, more;
  Ref nextacc[VEC_MAX_RED][VEC_MAX_COPY], nextpay[VEC_MAX_PAY], nextpos;
} VBody;

/* What a value read past a loop is to the vector loop's exit: np for one
   from outside the loop, or a constant, which is anywhere's; p for the
   value the loop carries into its next iteration as back[p]; -1 for any
   other value of the loop. */
static int v_live_slot(IrFunc *f, const uint8_t *inloop, Ref v, const Ref *back,
                       uint32_t np) {
  const Inst *in = &f->insts[v];
  if (!inloop[in->block] || in->op == I_CONST) return (int)np;
  for (uint32_t p = 0; p < np; p++)
    if (back[p] == v) return (int)p;
  return -1;
}

#define V_EACH_USE(b, X) do {                                              \
    for (uint32_t i_ = 0; i_ < (b)->ninsts; i_++) {                        \
      Ref *a_ = ir_args(f, (b)->insts[i_]);                                \
      for (uint32_t j_ = 0; j_ < f->insts[(b)->insts[i_]].nargs; j_++)     \
        X(a_[j_]);                                                         \
    }                                                                      \
    if ((b)->term == T_BRIF || (b)->term == T_BRTABLE) X((b)->term_arg);   \
    for (uint32_t j_ = 0; j_ < (b)->nret; j_++) X((b)->ret_args[j_]);      \
    for (uint32_t s_ = 0; s_ < (b)->nsuccs; s_++)                          \
      for (uint32_t j_ = 0; j_ < (b)->succs[s_].nargs; j_++)               \
        X((b)->succs[s_].args[j_]);                                        \
  } while (0)

/* Can the vector loop's exit go past the scalar loop, to the block the
   scalar loop leaves for?  Only if every value of the loop read past it --
   on the exit edge or in any block outside the loop -- is one the loop
   carries into its next iteration, which the vector exit computes as well.
   need[p] says whether back[p] is read outside the loop other than on the
   exit edge, and so has to become a parameter of the exit block; which
   only a block the latch alone enters can have read so (then everything
   past the loop is past that block), and only such a block can take. */
static bool v_exit_live(IrFunc *f, const uint8_t *inloop, uint32_t latch,
                        uint32_t xsucc, const Ref *back, uint32_t np,
                        uint8_t *need) {
  const Edge *ex = &f->blocks[latch].succs[xsucc];
  const Block *xb = &f->blocks[ex->target];
  if (ex->eh || xb->is_pad || inloop[ex->target]) return false;
  memset(need, 0, np);
  for (uint32_t j = 0; j < ex->nargs; j++)
    if (v_live_slot(f, inloop, ex->args[j], back, np) < 0) return false;
  bool ok = true;
#define V_LIVE(x) do { int p_ = v_live_slot(f, inloop, (x), back, np);      \
    if (p_ < 0) ok = false; else if (p_ < (int)np) need[p_] = 1; } while (0)
  for (uint32_t oi = 0; oi < f->norder && ok; oi++)
    if (!inloop[f->order[oi]]) V_EACH_USE(&f->blocks[f->order[oi]], V_LIVE);
#undef V_LIVE
  for (uint32_t p = 0; p < np && ok; p++)
    if (need[p] && xb->npreds != 1) ok = false;
  return ok;
}

/* The loop's constants that something past it reads -- CSE gives the code
   after a loop the loop's own 12345 -- move to the preheader.  v_exit_live
   takes a constant as readable anywhere, but the vector loop's exit can go
   past the scalar loop, or a trip count that fills every vector replace it
   whole, and a constant defined only on the scalar path was then read where
   it was never made: the next loop added garbage, or ran out of memory on
   a bound it never had.  The preheader is above both paths. */
static void v_hoist_out_consts(IrFunc *f, const uint8_t *inloop, uint32_t pre) {
#define V_HOIST(x) do {                                                       \
    Ref r_ = (x);                                                             \
    if (r_ < f->ninsts && f->insts[r_].op == I_CONST &&                       \
        inloop[f->insts[r_].block]) {                                         \
      Block *ob_ = &f->blocks[f->insts[r_].block];                            \
      uint32_t k_ = 0;                                                        \
      for (uint32_t j_ = 0; j_ < ob_->ninsts; j_++)                           \
        if (ob_->insts[j_] != r_) ob_->insts[k_++] = ob_->insts[j_];          \
      ob_->ninsts = k_;                                                       \
      ifc_append(f, pre, r_);                                                 \
    }                                                                         \
  } while (0)
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (inloop[bi]) {
      /* what the loop's exits hand on, which the vector exit hands on too */
      for (uint32_t s = 0; s < b->nsuccs; s++)
        if (!inloop[b->succs[s].target])
          for (uint32_t j = 0; j < b->succs[s].nargs; j++) V_HOIST(b->succs[s].args[j]);
      continue;
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Ref *a = ir_args(f, v);
      for (uint32_t j = 0; j < f->insts[v].nargs; j++) V_HOIST(a[j]);
      b = &f->blocks[bi];
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) V_HOIST(b->term_arg);
    for (uint32_t j = 0; j < b->nret; j++) V_HOIST(b->ret_args[j]);
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++) V_HOIST(b->succs[s].args[j]);
  }
#undef V_HOIST
}

/* Every read of back[p] outside the loop now reads the exit block's
   parameter par[p]: in the blocks v_exit_live looked at, the ones reached
   before the vector loop was made (f->order is not yet redone). */
static void v_exit_rewrite(IrFunc *f, const uint8_t *inloop, const Ref *back,
                           uint32_t np, const Ref *par) {
#define V_SWAP(x) do { for (uint32_t p_ = 0; p_ < np; p_++)                 \
    if (par[p_] != REF_NONE && (x) == back[p_]) { (x) = par[p_]; break; } \
  } while (0)
  for (uint32_t oi = 0; oi < f->norder; oi++)
    if (!inloop[f->order[oi]]) V_EACH_USE(&f->blocks[f->order[oi]], V_SWAP);
#undef V_SWAP
}
#undef V_EACH_USE

/* The factor to unroll a loop by whose stride of Sa bytes neither divides a
   vector nor is a whole number of them -- LLVM's unroll by three of an i32
   or an f64 loop, 12 or 24 bytes -- so that it spans a whole number of
   vectors, at most VEC_MAX_VEC; 0 if none does. */
static uint32_t v_unroll_for(uint64_t Sa) {
  for (uint32_t u = 2; u <= 8; u *= 2)
    if ((Sa * u) % VEC_BYTES == 0)
      return Sa * u / VEC_BYTES <= VEC_MAX_VEC ? u : 0;
  return 0;
}

/* The accesses like group G's at disp + t * width, t < kG, into pick: ones
   no group has yet, or with reuse a load another group holds where no such
   one is there (and it is speculated as spec, pol say).  How many were not
   yet in a group, or -1 if a place has none. */
static int v_window(const VAcc *acc, uint32_t nacc, const uint8_t *used,
                    const VGroup *G, uint64_t disp, uint32_t kG, uint32_t width,
                    uint64_t amask, bool reuse, bool spec, bool pol,
                    uint32_t *pick) {
  int fresh = 0;
  for (uint32_t t = 0; t < kG; t++) {
    uint64_t want = (disp + (uint64_t)t * width) & amask;
    uint32_t found = UINT32_MAX, again = UINT32_MAX;
    for (uint32_t j = 0; j < nacc && found == UINT32_MAX; j++) {
      if (acc[j].store != G->store || acc[j].base != G->base ||
          acc[j].mem != G->mem || acc[j].ivx != G->ivx || acc[j].disp != want)
        continue;
      if (!used[j]) found = j;
      else if (reuse && again == UINT32_MAX && !acc[j].store &&
               acc[j].spec == spec && acc[j].pol == pol)
        again = j;
    }
    if (found == UINT32_MAX) found = again;
    if (found == UINT32_MAX) return -1;
    if (!used[found]) fresh++;
    pick[t] = found;
  }
  return fresh;
}

/* *want, when the stride is all that stands in the way, is v_unroll_for's
   factor (see pass_vectorize). */
static bool vectorize_one(IrFunc *f, uint32_t hb, uint8_t *reach, uint8_t *inloop,
                          uint32_t *want) {
#define VREJ(w) do { if (warmc_getenv("WARMC_VEC_DEBUG")) \
    fprintf(stderr, "vec %s b%u reject: %s\n", \
            f->m->funcs[f->func_index].sym, hb, w); return false; } while (0)

#define VREJ_OP2(w, w2) do { if (warmc_getenv("WARMC_VEC_DEBUG")) \
    fprintf(stderr, "vec %s b%u reject: %s: %s\n", \
            f->m->funcs[f->func_index].sym, hb, w, w2); return false; } while (0)
#define VREJ_OP(w, op) do { if (warmc_getenv("WARMC_VEC_DEBUG")) \
    fprintf(stderr, "vec %s b%u reject: %s %s\n", \
            f->m->funcs[f->func_index].sym, hb, w, ir_op_name(op)); \
    return false; } while (0)

#define ACC_IDX64(mem) \
    ((mem) < f->m->nmemories && f->m->memories[mem].index64)
#define ANEG(x) (((uint64_t)0 - (uint64_t)(x)) & amask)
  Block *h = &f->blocks[hb];

  if (!h->nparams) return false;
  if (h->nparams > 16) VREJ("too many carried values");

  uint32_t pre = REF_NONE, nfwd = 0, nback = 0, latch = REF_NONE;
  for (uint32_t p = 0; p < h->npreds; p++) {
    uint32_t pb = h->preds[p];
    if (f->blocks[pb].rpo >= h->rpo) { nback++; latch = pb; }
    else { nfwd++; pre = pb; }
  }
  if (nback != 1) return false;
  if (nfwd != 1) VREJ("more than one way into the loop");

  if (h->is_pad) VREJ("header is a landing pad");
  if (f->blocks[latch].term != T_BRIF || f->blocks[latch].nsuccs != 2)
    VREJ("loop does not end in a two-way branch");

  memset(inloop, 0, f->nblocks);
  uint32_t bl[VEC_MAX_BLK], nbl = 0;
  uint8_t guarded[VEC_MAX_BLK];
  Ref gcond[VEC_MAX_BLK];
  bool gpol[VEC_MAX_BLK];
  VSel sels[VEC_MAX_SEL];
  uint32_t nsel = 0;
  {
    uint32_t cur = hb;
    for (;;) {
      if (nbl == VEC_MAX_BLK) VREJ("body too many blocks");
      guarded[nbl] = 0; gcond[nbl] = REF_NONE; gpol[nbl] = false;
      bl[nbl] = cur; inloop[cur] = 1; nbl++;
      if (cur == latch) break;
      Block *cb = &f->blocks[cur];
      if (cb->term == T_JUMP && cb->nsuccs == 1) { cur = cb->succs[0].target; continue; }
      if (cb->term != T_BRIF || cb->nsuccs != 2)
        VREJ_OP("body not a two-way branch, term",
                (uint16_t)(1000 + cb->term));
      uint32_t x = cb->succs[0].target, y = cb->succs[1].target;
      uint32_t arm, arm2 = REF_NONE, mrg;
      bool taken_is_arm;

      if (v_is_arm(f, x, y)) { arm = x; mrg = y; taken_is_arm = true; }
      else if (v_is_arm(f, y, x)) { arm = y; mrg = x; taken_is_arm = false; }

      else if (f->blocks[x].npreds == 1 && f->blocks[y].npreds == 1 &&
               f->blocks[x].term == T_JUMP && f->blocks[y].term == T_JUMP &&
               f->blocks[x].nsuccs == 1 && f->blocks[y].nsuccs == 1 &&
               f->blocks[x].nparams == 0 && f->blocks[y].nparams == 0 &&
               f->blocks[x].succs[0].target == f->blocks[y].succs[0].target) {
        arm = x; arm2 = y; mrg = f->blocks[x].succs[0].target;
        taken_is_arm = true;
      }
      else {

        Block *bx = &f->blocks[x], *by = &f->blocks[y];
        uint32_t lrpo = f->blocks[latch].rpo, hrpo = h->rpo;
        bool xout = bx->rpo > lrpo || bx->rpo < hrpo;
        bool yout = by->rpo > lrpo || by->rpo < hrpo;
        if (xout || yout) VREJ("body branches out of the loop");
        else if (x == latch || y == latch) VREJ("body branches to the latch");
        else if (bx->rpo <= cb->rpo || by->rpo <= cb->rpo)
          VREJ("body has a nested loop");
        else if (bx->npreds != 1 && by->npreds != 1)
          VREJ("body arms are shared");
        else if ((bx->term != T_JUMP || bx->nsuccs != 1) ||
                 (by->term != T_JUMP || by->nsuccs != 1)) {
          Block *bad = (bx->term != T_JUMP || bx->nsuccs != 1) ? bx : by;
          static const char *tn[] = {"none","jump","brif","brtable","return","trap"};
          if (warmc_getenv("WARMC_VEC_DEBUG"))
            fprintf(stderr, "vec %s b%u reject: body arm ends in %s (nsuccs=%u)\n",
                    f->m->funcs[f->func_index].sym, hb,
                    bad->term <= T_TRAP ? tn[bad->term] : "?", bad->nsuccs);
          return false;
        }
        else if (bx->nparams || by->nparams)
          VREJ("body arm takes parameters");
        else VREJ("body arms do not rejoin");
      }
      if (f->blocks[mrg].npreds != 2) VREJ("merge has other predecessors");
      if (nbl + (arm2 != REF_NONE ? 2u : 1u) > VEC_MAX_BLK)
        VREJ("body too many blocks");

      guarded[nbl] = 1; gcond[nbl] = cb->term_arg; gpol[nbl] = taken_is_arm;
      bl[nbl] = arm; inloop[arm] = 1; nbl++;
      if (arm2 != REF_NONE) {
        guarded[nbl] = 1; gcond[nbl] = cb->term_arg; gpol[nbl] = !taken_is_arm;
        bl[nbl] = arm2; inloop[arm2] = 1; nbl++;
      }

      Edge *ea = iv_edge(f, arm, mrg);
      Edge *ed = iv_edge(f, arm2 != REF_NONE ? arm2 : cur, mrg);
      Block *mb = &f->blocks[mrg];
      if (!ea || !ed || ea->nargs != mb->nparams || ed->nargs != mb->nparams)
        VREJ("merge arity");
      for (uint32_t q = 0; q < mb->nparams; q++) {
        if (nsel == VEC_MAX_SEL) VREJ("too many merges");
        sels[nsel].param = mb->params[q];
        sels[nsel].cond = cb->term_arg;
        sels[nsel].t = taken_is_arm ? ea->args[q] : ed->args[q];
        sels[nsel].fv = taken_is_arm ? ed->args[q] : ea->args[q];
        nsel++;
      }
      cur = mrg;
    }
  }
  /* A body block entered from outside the loop is no body the vector loop
     can stand in for.  Jump threading leaves such a block wherever a
     dispatch can resume into the middle of a loop. */
  for (uint32_t i = 1; i < nbl; i++) {
    const Block *bb = &f->blocks[bl[i]];
    for (uint32_t p = 0; p < bb->npreds; p++)
      if (!inloop[bb->preds[p]]) VREJ("a body block has a second way in");
  }

  Block *lb = &f->blocks[latch];
  bool taken_continues;
  if (lb->succs[0].target == hb && lb->succs[1].target != hb) taken_continues = true;
  else if (lb->succs[1].target == hb && lb->succs[0].target != hb) taken_continues = false;
  else VREJ("both arms re-enter the loop");
  Edge *back = &lb->succs[taken_continues ? 0 : 1];
  Edge *pe = iv_edge(f, pre, hb);
  if (!pe || pe->nargs != h->nparams || back->nargs != h->nparams)
    VREJ("edge arity");

  Ref body[VEC_MAX_BODY];
  uint8_t bspec[VEC_MAX_BODY];
  Ref bcond[VEC_MAX_BODY];
  uint8_t bpol[VEC_MAX_BODY];
  uint32_t nbody = 0;
  for (uint32_t i = 0; i < nbl; i++) {
    Block *b = &f->blocks[bl[i]];
    for (uint32_t j = 0; j < b->ninsts; j++) {
      if (nbody == VEC_MAX_BODY) VREJ("body too large");
      body[nbody] = b->insts[j];
      bspec[nbody] = guarded[i];
      bcond[nbody] = gcond[i];
      bpol[nbody] = gpol[i];
      nbody++;
    }
  }

  uint32_t np = h->nparams;
  Ref entry_args[16], back_args[16], hparam[16];
  for (uint32_t p = 0; p < np; p++) {
    entry_args[p] = pe->args[p];
    back_args[p] = back->args[p];
    hparam[p] = h->params[p];
  }

  for (uint32_t i = 0; i < nbody; i++) {
    Inst *in = &f->insts[body[i]];
    switch (in->op) {
      case I_CONST: case I_ADD: case I_SUB: case I_MUL:
      case I_AND: case I_OR: case I_XOR:
      case I_SHL: case I_SHRU: case I_SHRS: case I_ROTL: case I_ROTR:
      case I_FADD: case I_FSUB: case I_FMUL: case I_FDIV:
      case I_FNEG: case I_FABS: case I_FSQRT: case I_FMIN: case I_FMAX:
      case I_LOAD: case I_STORE: case I_MEM_ADDR: case I_MEM_PTR:

      case I_MEM_BASE:
      case I_EQ: case I_NE: case I_LTU: case I_LTS: case I_GTU: case I_GTS:
      case I_LEU: case I_LES: case I_GEU: case I_GES: case I_EQZ:
      case I_SELECT:
      case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE:
      case I_BSWAP: case I_MULHU:
      case I_CONVERTS: case I_CONVERTU: case I_TRUNC_SATS: case I_TRUNC_SATU:

      case I_EXTENDU: case I_EXTENDS: case I_WRAP:
      case I_NOP: case I_PARAM:
        break;
      default: VREJ_OP("body op", in->op);
    }
  }

  Ref iventry[16];
  uint64_t ivstep[16];
  bool ivadd[16], isiv[16];
  uint32_t niv = 0;
  for (uint32_t p = 0; p < np; p++) {
    Ref par = hparam[p], nx = back_args[p];
    Inst *n = &f->insts[nx];
    uint64_t stp;
    isiv[p] = false;
    if ((n->op == I_ADD || n->op == I_SUB) && n->nargs == 2 &&
        inloop[n->block] && ir_args(f, nx)[0] == par &&
        is_const(f, ir_args(f, nx)[1], &stp) && stp != 0) {
      iventry[p] = entry_args[p];
      ivstep[p] = stp;
      ivadd[p] = n->op == I_ADD;
      isiv[p] = true;
      niv++;
    }
  }
  if (!niv) VREJ("no induction variable");

  VAcc acc[VEC_MAX_ACC];
  uint32_t nacc = 0;
  for (uint32_t i = 0; i < nbody; i++) {
    Ref r = body[i];
    Inst *in = &f->insts[r];
    if (in->op != I_LOAD && in->op != I_STORE) continue;
    if (nacc == VEC_MAX_ACC) VREJ("too many accesses");
    acc[nacc].spec = bspec[i];
    acc[nacc].cond = bcond[i];
    acc[nacc].pol = bpol[i];
    uint32_t w, ln;
    if (!v_acc_shape(f, r, &w, &ln)) VREJ("access shape");
    acc[nacc].inst = r;
    acc[nacc].pos = i;
    acc[nacc].store = in->op == I_STORE;
    acc[nacc].mem = MEMF_MEM(in->imm2);
    acc[nacc].width = w;
    acc[nacc].lane = ln;
    nacc++;
  }
  if (!nacc) VREJ("no memory access");

  bool idx64 = ACC_IDX64(acc[0].mem);
  for (uint32_t i = 1; i < nacc; i++)
    if (ACC_IDX64(acc[i].mem) != idx64) VREJ("mixed index spaces");
  const uint64_t amask = idx64 ? ~0ull : 0xffffffffu;
  const uint64_t asign = idx64 ? (1ull << 63) : (1ull << 31);
  const ValType at = idx64 ? VT_I64 : VT_I32;

  uint32_t lane = acc[0].lane;
  uint32_t width = acc[0].width;
  /* Accesses as wide as one another with an integer lane beside a float
     one -- an i32 array converted into an f32 one -- hold as many elements
     to a vector, and each value is built in its own type's lane.  Only the
     reductions assume a single lane, so such a loop has none. */
  bool mixlane = false;
  for (uint32_t i = 1; i < nacc; i++) {
    if (acc[i].width != width) VREJ("mixed lane shape");
    if (acc[i].lane != lane) mixlane = true;
  }

  for (uint32_t i = 0; i < nacc; i++) {
    bool found = false;
    for (uint32_t p = 0; p < np && !found; p++) {
      ValType ivt = (ValType)f->insts[hparam[p]].type;
      if (!isiv[p] || (ivt != at && !(at == VT_I32 && ivt == VT_I64))) continue;
      Ref b;
      uint64_t cf, of;
      if (!v_access_addr(f, inloop, hparam[p], acc[i].inst, VEC_DEPTH, amask,
                         &b, &cf, &of) || cf == 0) continue;
      uint64_t st = ivadd[p] ? ivstep[p] : (uint64_t)0 - ivstep[p];
      acc[i].stride = (cf * st) & amask;
      acc[i].ivx = p; acc[i].coeff = cf; acc[i].base = b; acc[i].disp = of;
      found = true;
    }
    if (!found) VREJ("address is not affine in any counter");
  }

  uint64_t S = 0;
  for (uint32_t i = 0; i < nacc; i++) {
    uint64_t sa = (acc[i].stride & asign) ? ANEG(acc[i].stride) : acc[i].stride;
    if (sa == 0) VREJ("stride is zero");
    if (i == 0 || sa < S) S = sa;
  }
  bool dir0 = (acc[0].stride & asign) != 0;
  for (uint32_t i = 0; i < nacc; i++) {
    if (((acc[i].stride & asign) != 0) != dir0)
      VREJ("counters march in opposite directions");
    uint64_t sa = dir0 ? ANEG(acc[i].stride) : acc[i].stride;
    if (sa % S) VREJ("strides are not multiples of one another");
  }
  S = dir0 ? ANEG(S) : S;

  bool backward = (S & asign) != 0;
  uint64_t Sa = backward ? ANEG(S) : S;
  if (Sa == 0 || Sa % width) VREJ("stride is not a multiple of the element");
  uint32_t U, nvec;
  if (Sa <= VEC_BYTES) {
    if (VEC_BYTES % Sa) { *want = v_unroll_for(Sa); VREJ("stride"); }
    U = (uint32_t)(VEC_BYTES / Sa);
    nvec = 1;
  } else {
    if (Sa % VEC_BYTES) { *want = v_unroll_for(Sa); VREJ("stride"); }
    U = 1;
    nvec = (uint32_t)(Sa / VEC_BYTES);
    if (nvec > VEC_MAX_VEC) VREJ("stride spans too many vectors");
  }
  uint32_t k = (uint32_t)(Sa / width);
  uint32_t mpv = k / nvec;
  uint32_t W = (uint32_t)(Sa * U);

  uint32_t gran = v_log2(width);
  if (k == 0 || k > VEC_MAX_MEMB) VREJ("group width");

  VecCtx ctx;
  memset(&ctx, 0, sizeof ctx);
  ctx.f = f; ctx.hb = hb; ctx.inloop = inloop;
  memcpy(ctx.sel, sels, nsel * sizeof(VSel));
  ctx.nsel = nsel;
  ctx.lane = lane; ctx.width = width; ctx.k = k;
  ctx.mpv = mpv; ctx.nvec = nvec; ctx.gran = gran;
  ctx.acc = acc; ctx.nacc = nacc;

  uint8_t used[VEC_MAX_ACC];
  memset(used, 0, sizeof used);

  for (;;) {
    uint32_t i = 0xffffffffu;
    for (uint32_t j = 0; j < nacc; j++) if (!used[j]) { i = j; break; }
    if (i == 0xffffffffu) break;
    if (ctx.ngrp == VEC_MAX_GRP) VREJ("too many groups");
    VGroup *G = &ctx.grp[ctx.ngrp++];
    memset(G, 0, sizeof *G);
    G->base = acc[i].base;
    G->ivx = acc[i].ivx;
    G->coeff = acc[i].coeff;
    G->width = width; G->store = acc[i].store; G->mem = acc[i].mem;
    {
      uint64_t sa = dir0 ? ANEG(acc[i].stride) : acc[i].stride;
      G->m = (uint32_t)(sa / Sa);
    }
    if (G->m > VEC_MAX_ILV) VREJ("interleave factor");
    if (G->m > 1 && nvec > 1) VREJ("interleaved group across several vectors");
    G->nv = G->m > 1 ? G->m : nvec;

    uint32_t lowest = i;
    for (uint32_t step = 0; step < nacc; step++) {
      uint64_t prev = (acc[lowest].disp - width) & amask;
      uint32_t found = 0xffffffffu;
      for (uint32_t j = 0; j < nacc; j++)
        if (!used[j] && acc[j].store == G->store && acc[j].base == G->base &&
            acc[j].mem == G->mem &&
            acc[j].ivx == G->ivx && acc[j].disp == prev) { found = j; break; }
      if (found == 0xffffffffu) break;
      lowest = found;
    }
    uint32_t kG = G->m * mpv * nvec;
    if (kG > VEC_MAX_MEMB) VREJ("group width");
    uint32_t pick[VEC_MAX_MEMB];
    uint64_t gdisp = acc[lowest].disp;
    if (v_window(acc, nacc, used, G, gdisp, kG, width, amask, false, false,
                 false, pick) < 0) {
      /* A load the producer shares between copies -- copy c's a[i + 1] the
         same instruction as copy c + 1's a[i], once nothing between could
         have changed it -- is wanted by two groups, a window apart, and
         this one may take it again: of the windows holding the first
         access no group has yet, the one with most such accesses. */
      int best = 0;
      if (!G->store && G->m == 1)
        for (uint32_t t0 = 0; t0 < kG; t0++) {
          uint64_t st = (acc[i].disp - (uint64_t)t0 * width) & amask;
          uint32_t tmp[VEC_MAX_MEMB];
          int fr = v_window(acc, nacc, used, G, st, kG, width, amask, true,
                            acc[i].spec, acc[i].pol, tmp);
          if (fr > best) { best = fr; gdisp = st; memcpy(pick, tmp, kG * sizeof *pick); }
        }
      if (best <= 0) VREJ("group not contiguous");
    }
    G->disp = gdisp;

    uint64_t ev;
    G->stat = G->base == REF_NONE && is_const(f, iventry[G->ivx], &ev);
    if (G->stat) G->stat_addr = (G->coeff * ev + G->disp) & amask;
    G->spec = acc[pick[0]].spec; G->pol = acc[pick[0]].pol;
    for (uint32_t t = 0; t < kG; t++) {
      uint32_t j = pick[t];
      G->member[t] = acc[j].inst;
      G->pos[t] = acc[j].pos;
      G->mcond[t] = acc[j].cond;
      if (t && (acc[j].spec != G->spec || acc[j].pol != G->pol))
        VREJ("group partly speculated");
      G->n = t + 1;
      used[j] = 1;
    }
  }

  if (warmc_getenv("WARMC_VEC_DEBUG2")) {
    fprintf(stderr, "vec %s b%u: Sb=%llu U=%u nvec=%u mpv=%u width=%u groups=%u\n",
            f->m->funcs[f->func_index].sym, hb, (unsigned long long)Sa,
            U, nvec, mpv, width, ctx.ngrp);
    for (uint32_t g = 0; g < ctx.ngrp; g++)
      fprintf(stderr, "   grp%u %s base=%d ivx=%u disp=%llu m=%u nv=%u n=%u\n",
              g, ctx.grp[g].store ? "store" : "load ", (int)ctx.grp[g].base,
              ctx.grp[g].ivx, (unsigned long long)ctx.grp[g].disp,
              ctx.grp[g].m, ctx.grp[g].nv, ctx.grp[g].n);
  }

  uint32_t chk[VEC_MAX_CHECK][2];
  uint32_t nchk = 0;
  /* the most vector iterations' worth of elements a store and another
     access through the same pointer are apart */
  uint64_t rcap = ~0ull;
  for (uint32_t g = 0; g < ctx.ngrp; g++) {
    for (uint32_t g2 = g + 1; g2 < ctx.ngrp; g2++) {
      VGroup *A = &ctx.grp[g], *Bg = &ctx.grp[g2];
      if (!A->store && !Bg->store) continue;

      if (A->mem != Bg->mem) continue;

      bool same_root = A->base == Bg->base && A->ivx == Bg->ivx && A->m == Bg->m;
      if (!same_root && !(A->stat && Bg->stat)) {
        if (nchk == VEC_MAX_CHECK) VREJ("too many alias checks");
        chk[nchk][0] = g;
        chk[nchk][1] = g2;
        nchk++;
        continue;
      }
      uint64_t d = same_root ? ((A->disp - Bg->disp) & amask)
                             : ((A->stat_addr - Bg->stat_addr) & amask);
      uint64_t dist = d < asign ? d : ANEG(d);
      if (dist == 0) {

        bool a_first = A->pos[0] < Bg->pos[0];
        for (uint32_t t = 1; t < k; t++)
          if ((A->pos[t] < Bg->pos[t]) != a_first) VREJ("order differs by lane");
        if ((a_first ? A : Bg)->store) VREJ("store read back in the same iteration");
      } else if (dist < W * A->m) {
        VREJ("overlapping groups");
      } else if (dist / ((uint64_t)W * A->m) < rcap) {
        rcap = dist / ((uint64_t)W * A->m);
      }
    }
  }

  Ref redparam[VEC_MAX_RED], redterm[VEC_MAX_RED][VEC_MAX_MEMB];
  IrOp redop[VEC_MAX_RED];

  uint32_t redwiden[VEC_MAX_RED];
  bool redsigned[VEC_MAX_RED];

  bool redmul[VEC_MAX_RED];
  /* a min/max reduction's compares and which arm of its selects the element
     is on, by lane, for an index carried beside it */
  Ref redcmp[VEC_MAX_RED][VEC_MAX_MEMB];
  bool redtt[VEC_MAX_RED][VEC_MAX_MEMB];
  uint32_t pdef[16], npdef = 0;
  uint32_t nred = 0;
  for (uint32_t p = 0; p < np; p++) {
    if (isiv[p]) continue;
    if (mixlane) VREJ("mixed lane shape");
    Ref par = hparam[p], nx = back_args[p];
    if (nx == par) VREJ("carried value with no update");
    ValType pt = (ValType)f->insts[par].type;
    Inst *n = &f->insts[nx];
    /* A float sum is not reassociable, but min and max are, exactly: wasm's
       f32.min returns a NaN if either side is one, whatever the order, and
       -0 below +0, so a lane-wise min of the elements and then of the lanes
       is the min of them all.  Only the NaN's payload may differ, and wasm
       leaves that open. */
    bool is_fmm = vt_is_float(pt) && (n->op == I_FMIN || n->op == I_FMAX);
    if (vt_is_float(pt) && !is_fmm) VREJ("carried value is not a lane");
    IrOp mmop = (IrOp)0;
    bool is_mm = !is_fmm && inloop[n->block] && v_minmax_form(f, nx, &mmop);
    if (!is_mm && !is_fmm && inloop[n->block] && n->op == I_SELECT && pt == VT_I32) {
      /* perhaps an index kept beside a min or max: settled after them */
      if (npdef == 16) VREJ("too many index reductions");
      pdef[npdef++] = p;
      continue;
    }
    if (!inloop[n->block] ||
        (!is_mm && !is_fmm && n->op != I_ADD && n->op != I_AND &&
         n->op != I_OR && n->op != I_XOR))
      VREJ("carried value is not a reduction");

    if (is_mm && v_lane_of(pt) == VLANE_I64) VREJ("64-bit min/max reduction");

    uint32_t plane = v_lane_of(pt);
    uint32_t widen = 0;
    if (plane != lane) {
      if (plane < lane || VLANE_IS_FLOAT(plane)) VREJ("carried value is not a lane");
      if (n->op != I_ADD) VREJ("widening reduction is not a sum");
      widen = plane - lane;
      if (widen > VEC_MAX_WIDEN) VREJ("widening reduction is too wide");
    }
    if (nred == VEC_MAX_RED) VREJ("too many reductions");
    IrOp op = is_mm ? mmop : (IrOp)n->op;

    memset(reach, 0, f->ninsts);
    reach[par] = 1;
    for (uint32_t i = 0; i < nbody; i++) {
      Ref r = body[i];
      Inst *ri = &f->insts[r];
      Ref *ra = ir_args(f, r);
      for (uint32_t j = 0; j < ri->nargs; j++)
        if (reach[ra[j]]) { reach[r] = 1; break; }
    }

    uint32_t nterm = 0;
    Ref cur = nx;

    Ref mmcmp[VEC_MAX_MEMB];
    bool tsd[VEC_MAX_MEMB];
    uint32_t nmmcmp = 0;
    while (cur != par) {
      Inst *ci = &f->insts[cur];
      if (is_mm) {
        IrOp got;
        if (!inloop[ci->block] || !v_minmax_form(f, cur, &got) || got != op)
          VREJ("reduction chain");
        if (nmmcmp == VEC_MAX_MEMB) VREJ("reduction chain too long");
        mmcmp[nmmcmp++] = ir_args(f, cur)[2];
      } else if (ci->op != op || ci->nargs != 2 || !inloop[ci->block]) {
        VREJ("reduction chain");
      }
      Ref x = ir_args(f, cur)[0], y = ir_args(f, cur)[1];
      Ref next, term;
      if (reach[x] && reach[y]) VREJ("accumulator on both sides");
      else if (reach[x]) { next = x; term = y; }
      else if (reach[y]) { next = y; term = x; }
      else VREJ("reduction chain end");
      if (nterm == k) VREJ("reduction chain too long");
      tsd[nterm] = term == x;
      redterm[nred][nterm++] = term;
      cur = next;
    }
    if (nterm != k) VREJ("reduction chain length");
    {

      Ref ord[VEC_MAX_MEMB];
      uint8_t got[VEC_MAX_MEMB];
      memset(got, 0, sizeof got);
      for (uint32_t t = 0; t < k; t++) {
        int l = v_lane_of_term(&ctx, redterm[nred][t], VEC_DEPTH);
        if (l < 0 || (uint32_t)l >= k || got[l]) VREJ("reduction terms do not line up");
        ord[l] = redterm[nred][t];
        if (is_mm) { redcmp[nred][l] = mmcmp[t]; redtt[nred][l] = tsd[t]; }
        got[l] = 1;
      }
      memcpy(redterm[nred], ord, k * sizeof(Ref));
    }

    uint32_t seen = 0;
    for (uint32_t i = 0; i < nbody; i++) {
      Inst *in = &f->insts[body[i]];
      Ref *a = ir_args(f, body[i]);
      bool ischaincmp = false;
      for (uint32_t t = 0; t < nmmcmp; t++)
        if (mmcmp[t] == body[i]) { ischaincmp = true; break; }
      if (ischaincmp) continue;
      for (uint32_t j = 0; j < in->nargs; j++)
        if (a[j] == par) seen++;
    }
    if (seen != 1 || lb->term_arg == par) VREJ("accumulator read elsewhere");
    for (uint32_t q = 0; q < np; q++)
      if (q != p && back_args[q] == par) VREJ("accumulator carried twice");

    bool ismul = false;
    if (widen) {
      bool sg = false;
      for (uint32_t t = 0; t < k; t++) {
        Ref tr = redterm[nred][t];
        Inst *ti = &f->insts[tr];

        if ((ti->op == I_EXTENDU || ti->op == I_EXTENDS) && ti->nargs == 1 &&
            (ValType)ti->type == VT_I64 && pt == VT_I64) {
          Ref nx = ir_args(f, tr)[0];
          Inst *ni = &f->insts[nx];
          if ((ValType)ni->type != VT_I32) VREJ("widening extend is not from i32");
          if (ismul) VREJ("widening terms are not all multiplies");
          bool nsg;
          if (ni->op == I_LOAD && (ni->imm2 & MEMF_SIGNED) &&
              MEMF_SIZE(ni->imm2) == (uint32_t)VLANE_SIZE(lane)) {
            if (ti->op != I_EXTENDS) VREJ("widening extend disagrees with its load");
            nsg = true;
          } else if (lane == v_lane_of(VT_I32)) {

            nsg = ti->op == I_EXTENDS;
          } else if (v_fits_lane(f, nx, lane)) {
            nsg = false;
          } else {
            VREJ("widening extend is not of a lane value");
          }
          if (t && nsg != sg) VREJ("widening terms disagree about sign");
          sg = nsg;
          redterm[nred][t] = nx;
          continue;
        }
        if ((ValType)ti->type != pt) VREJ("widening term has the wrong type");

        if (ti->op == I_MUL && ti->nargs == 2 && widen == 1) {
          Ref *ma = ir_args(f, redterm[nred][t]);
          Inst *m0 = &f->insts[ma[0]], *m1 = &f->insts[ma[1]];

          if (m0->op != I_LOAD || m1->op != I_LOAD)
            VREJ("widening multiply is not of two loads");
          if (MEMF_SIZE(m0->imm2) != (uint32_t)VLANE_SIZE(lane) ||
              MEMF_SIZE(m1->imm2) != (uint32_t)VLANE_SIZE(lane))
            VREJ("widening term is not the lane's width");
          bool sa = (m0->imm2 & MEMF_SIGNED) != 0;
          if (sa != ((m1->imm2 & MEMF_SIGNED) != 0))
            VREJ("widening terms disagree about sign");
          if (t && (!ismul || sa != sg)) VREJ("widening terms disagree about sign");
          ismul = true;
          sg = sa;
          continue;
        }

        if (ismul) VREJ("widening terms are not all multiplies");

        uint64_t cvop;
        bool cso, csa;
        if (v_cmp_form(ti->op, &cvop, &cso, &csa)) {
          if (t && sg) VREJ("widening terms disagree about sign");
          sg = false;
          continue;
        }
        if (ti->op != I_LOAD) VREJ("widening term is not a load");

        if (MEMF_SIZE(ti->imm2) != (uint32_t)VLANE_SIZE(lane))
          VREJ("widening term is not the lane's width");
        bool s1 = (ti->imm2 & MEMF_SIGNED) != 0;
        if (t && s1 != sg) VREJ("widening terms disagree about sign");
        sg = s1;
      }
      redsigned[nred] = sg;
    } else {
      redsigned[nred] = false;
    }
    redmul[nred] = ismul;
    redwiden[nred] = widen;
    redparam[nred] = par;
    redop[nred] = op;
    nred++;
  }

  /* argmin and argmax: an index carried beside a min or max reduction and
     taken where that reduction's select takes the element --
     select(i, at, v < best) beside select(v, best, v < best).  Each lane
     keeps the position of its own best element: the first of equal ones
     where the compare is strict, the last where it is not.  The exit takes,
     among the lanes that hold the overall best, the earliest position (or
     the latest), and makes the index that element had: the counter's entry
     value, the index's offset from the counter, and the position times the
     index's step.  A position is the element's place in the vector loop,
     memory order in a forward loop, and the loop's count is capped so that
     positions never wrap, however the index itself does.  The scalar entry
     value stands for the elements before the loop, which win ties when the
     first is wanted and lose them when the last is. */
  Ref payparam[VEC_MAX_PAY];
  uint32_t payred[VEC_MAX_PAY], payiv[VEC_MAX_PAY];
  uint32_t payc0[VEC_MAX_PAY], payd[VEC_MAX_PAY];
  bool payfirst[VEC_MAX_PAY], payside[VEC_MAX_PAY], payltm[VEC_MAX_PAY];
  uint16_t paycop[VEC_MAX_PAY];
  int paymap[16];
  for (uint32_t p = 0; p < 16; p++) paymap[p] = -1;
  uint32_t npay = 0;
  for (uint32_t di = 0; di < npdef; di++) {
    uint32_t p = pdef[di];
    Ref par = hparam[p], nx = back_args[p];
    if (backward || lane != VLANE_I32 || width != 4) VREJ("index reduction shape");
    if (npay == VEC_MAX_PAY) VREJ("too many index reductions");
    memset(reach, 0, f->ninsts);
    reach[par] = 1;
    for (uint32_t i = 0; i < nbody; i++) {
      Ref r = body[i];
      Inst *ri = &f->insts[r];
      Ref *ra = ir_args(f, r);
      for (uint32_t j = 0; j < ri->nargs; j++)
        if (reach[ra[j]]) { reach[r] = 1; break; }
    }
    Ref term[VEC_MAX_MEMB], cnd[VEC_MAX_MEMB], link[VEC_MAX_MEMB];
    bool side[VEC_MAX_MEMB];
    uint32_t nt = 0;
    for (Ref cur = nx; cur != par;) {
      Inst *ci = &f->insts[cur];
      if (ci->op != I_SELECT || ci->nargs != 3 || !inloop[ci->block])
        VREJ("carried value is not a reduction");
      Ref *ca = ir_args(f, cur);
      if (reach[ca[0]] == reach[ca[1]] || reach[ca[2]]) VREJ("index reduction chain");
      if (nt == k) VREJ("index reduction chain too long");
      side[nt] = !reach[ca[0]];
      term[nt] = side[nt] ? ca[0] : ca[1];
      cnd[nt] = ca[2];
      link[nt] = cur;
      cur = side[nt] ? ca[1] : ca[0];
      nt++;
    }
    if (nt != k) VREJ("index reduction chain length");

    /* the min/max reduction the compares belong to, lane by lane */
    uint32_t r = 0xffffffffu, lof[VEC_MAX_MEMB];
    uint8_t got[VEC_MAX_MEMB];
    memset(got, 0, sizeof got);
    for (uint32_t t = 0; t < nt; t++) {
      uint32_t hr = 0xffffffffu, hl = 0;
      for (uint32_t rr = 0; rr < nred && hr == 0xffffffffu; rr++) {
        if (!v_is_minmax_op(redop[rr]) || redwiden[rr]) continue;
        for (uint32_t l = 0; l < k; l++)
          if (redcmp[rr][l] == cnd[t]) { hr = rr; hl = l; break; }
      }
      if (hr == 0xffffffffu || (r != 0xffffffffu && hr != r))
        VREJ("index reduction compare");
      if (got[hl] || side[t] != redtt[hr][hl] || side[t] != side[0])
        VREJ("index reduction arms");
      r = hr; got[hl] = 1; lof[t] = hl;
    }
    uint16_t cop = f->insts[cnd[0]].op;
    bool ltm = ir_args(f, cnd[0])[0] == redterm[r][lof[0]];
    for (uint32_t t = 0; t < nt; t++)
      if (f->insts[cnd[t]].op != cop ||
          (ir_args(f, cnd[t])[0] == redterm[r][lof[t]]) != ltm)
        VREJ("index reduction compares differ");
    bool strict = cop == I_LTS || cop == I_LTU || cop == I_GTS || cop == I_GTU;
    if (!strict && cop != I_LES && cop != I_LEU && cop != I_GES && cop != I_GEU)
      VREJ("index reduction compare");

    /* the index is the counter plus an offset that grows by one step per
       element */
    uint32_t q = 0xffffffffu, off[VEC_MAX_MEMB];
    for (uint32_t qq = 0; qq < np && q == 0xffffffffu; qq++) {
      if (!isiv[qq] || (ValType)f->insts[hparam[qq]].type != VT_I32) continue;
      bool all = true;
      for (uint32_t t = 0; t < nt && all; t++)
        all = v_iv_offset(f, term[t], hparam[qq], &off[lof[t]]);
      if (all) q = qq;
    }
    if (q == 0xffffffffu) VREJ("index is not the counter");
    uint32_t step = ivadd[q] ? (uint32_t)ivstep[q] : (uint32_t)0 - (uint32_t)ivstep[q];
    uint32_t d = k == 1 ? step : off[1] - off[0];
    for (uint32_t l = 0; l < k; l++)
      if (off[l] != off[0] + l * d) VREJ("index does not step with the elements");
    if (d * k != step) VREJ("index does not step with the elements");

    /* nothing else reads the index or its links */
    if (lb->term_arg == par) VREJ("accumulator read elsewhere");
    for (uint32_t qq = 0; qq < np; qq++)
      if (qq != p && back_args[qq] == par) VREJ("accumulator carried twice");
    for (uint32_t t = 0; t <= nt; t++) {
      Ref want = t < nt ? link[t] : par;
      uint32_t uses = 0;
      for (uint32_t i = 0; i < nbody; i++) {
        Inst *in = &f->insts[body[i]];
        Ref *a = ir_args(f, body[i]);
        for (uint32_t j = 0; j < in->nargs; j++) if (a[j] == want) uses++;
      }
      if (uses != (t == 0 ? 0u : 1u) || lb->term_arg == want)
        VREJ("accumulator read elsewhere");
    }

    payparam[npay] = par; payred[npay] = r; payiv[npay] = q;
    payc0[npay] = off[0]; payd[npay] = d;
    payfirst[npay] = side[0] ? strict : !strict;
    payside[npay] = side[0]; payltm[npay] = ltm; paycop[npay] = cop;
    paymap[p] = (int)npay;
    npay++;
  }

  Ref ivside, bound;
  IrOp rel;
  {
    Ref cond = lb->term_arg;
    bool cont = taken_continues;

    if (f->insts[cond].op == I_EQZ && f->insts[cond].nargs == 1) {
      cond = ir_args(f, cond)[0];
      cont = !cont;
    }
    bool is_counter = false;
    for (uint32_t p = 0; p < np && !is_counter; p++)
      if (isiv[p] && back_args[p] == cond) is_counter = true;
    if (is_counter) {

      rel = cont ? I_NE : I_EQ;
      ivside = cond;
      bound = REF_NONE;
    } else {
      rel = v_continue_rel(f, cond, cont, &ivside, &bound);
    }
  }
  if (rel == I_NOP) VREJ("exit test");
  uint32_t tp = 0xffffffffu;
  for (uint32_t p = 0; p < np; p++)
    if (isiv[p] && back_args[p] == ivside) { tp = p; break; }
  if (tp == 0xffffffffu && bound != REF_NONE) {
    rel = v_mirror(rel);
    Ref t = ivside; ivside = bound; bound = t;
    for (uint32_t p = 0; p < np; p++)
      if (isiv[p] && back_args[p] == ivside) { tp = p; break; }
  }
  /* Rotation leaves a loop that tests its counter before the step it
     carries round, i != N going on with i + S (rsbench's f32ops once its
     bounds checks are gone): the test of the next value against N + S,
     mod 2^n, since adding S is a bijection.  With N a constant, so is the
     bound the trip count is then made from. */
  bool prestep = false;
  uint64_t pre_bound = 0;
  if (tp == 0xffffffffu && rel == I_NE && bound != REF_NONE) {
    uint64_t nc;
    for (uint32_t p = 0; p < np && tp == 0xffffffffu; p++) {
      if (!isiv[p]) continue;
      if (hparam[p] == bound && is_const(f, ivside, &nc)) {
        Ref t = ivside; ivside = bound; bound = t;
      }
      if (hparam[p] != ivside || !is_const(f, bound, &nc)) continue;
      tp = p;
      prestep = true;
      pre_bound = nc + (ivadd[p] ? ivstep[p] : (uint64_t)0 - ivstep[p]);
      if ((ValType)f->insts[hparam[p]].type != VT_I64) pre_bound &= 0xffffffffu;
    }
  }
  if (tp == 0xffffffffu || rel == I_NOP) VREJ("exit test counter");

  bool clone_bound = false;
  if (!prestep && bound != REF_NONE && inloop[f->insts[bound].block]) {
    if (f->insts[bound].op != I_CONST) VREJ("bound is not loop-invariant");
    clone_bound = true;
  }
  if (rel != I_NE && rel != I_LTU && rel != I_LTS && rel != I_LEU &&
      rel != I_LES && rel != I_GTU && rel != I_GTS && rel != I_GEU &&
      rel != I_GES)
    VREJ("exit relation");
  ValType ct = (ValType)f->insts[hparam[tp]].type;
  if (ct != VT_I32 && ct != VT_I64) VREJ("counter type");
  uint32_t cbits = ct == VT_I64 ? 64 : 32;

  uint64_t K = ivstep[tp];
  bool down = !ivadd[tp];
  if (!down && (K & (1ull << (cbits - 1)))) {
    down = true;
    K = ((cbits == 64) ? 0ull : (1ull << cbits)) - K;
  }

  bool gform = (rel == I_GTU || rel == I_GTS || rel == I_GEU || rel == I_GES);
  if (down && !gform && rel != I_NE) VREJ("counter runs down");
  if (!down && gform) VREJ("counter runs up against a downward test");
  if (K == 0) VREJ("counter step is zero");
  if (K >= (1ull << (cbits - 2))) VREJ("counter step");

  uint64_t tmax = ~0ull;
  {
    uint64_t dmax = ~0ull, zc;
    if (prestep) {
      if (down ? pre_bound == 0 : (is_const(f, iventry[tp], &zc) && zc == 0))
        dmax = down ? ir_value_max(f, iventry[tp], 6) : pre_bound;
    } else if (down) {
      if (bound == REF_NONE || (is_const(f, bound, &zc) && zc == 0))
        dmax = ir_value_max(f, iventry[tp], 6);
    } else if (bound != REF_NONE && is_const(f, iventry[tp], &zc) && zc == 0) {
      dmax = ir_value_max(f, bound, 6);
    }
    if (dmax != ~0ull && dmax / K + 1 <= (uint64_t)U)
      VREJ("trip count cannot fill a vector");
    if (dmax != ~0ull) tmax = dmax / K + 1;
  }

  /* Copies.  A loop of one vector an iteration bumps every group's pointer
     once per sixteen bytes, and Apple's cores retire such a loop at one
     iteration a cycle however little it computes: vecbench/addi32 took
     1.06 cycles a vector where native, two vectors off each pointer and one
     bump, takes 0.80.  A reduction's one accumulator is worse, a chain of
     two-cycle adds (vecbench/dotint 1.9 cycles a vector, 1.0 with two).  So
     where the scalar body's U iterations fill one vector, an iteration of
     the vector loop holds R copies of them, 16 bytes apart off the same
     pointers, and each reduction keeps an accumulator per copy, the copies
     combined after the loop.  The guard still counts single vectors, and a
     second loop of one copy takes the last count % R of them, so a loop too
     short for R vectors runs as many vectors as it did.  Fewer copies where
     the count is known to fill no more than R vectors; none where an index
     is kept beside a min or max (its positions are one vector's), nor where
     a store and another access through the same pointer are less than R
     vectors apart -- the copies' loads all come before their stores. */
  uint32_t R = 1;
  if (nvec == 1 && !npay) {
    R = v_copies_wanted();
    while (R > 1 && (rcap < R || tmax <= (uint64_t)U * R)) R >>= 1;
  }

  /* Past the scalar loop.  The guard leaves the scalar loop 1..U
     iterations, because the vector loop's exit can only go on into it; for
     a loop of 4096 bytes that is sixteen scalar iterations after 255
     vectors, 7% of vecbench/addi8.  Where the loop hands on nothing but
     values it carries, which the vector exit computes too (v_exit_live),
     the vector loop takes every whole vector and its exit goes straight to
     the scalar loop's exit block when nothing is left. */
  uint32_t xsucc = taken_continues ? 1u : 0u;
  uint32_t xb = lb->succs[xsucc].target;
  uint8_t xneed[16];
  bool skip = v_exit_live(f, inloop, latch, xsucc, back_args, np, xneed);
  v_hoist_out_consts(f, inloop, pre);

  uint32_t vguard = ir_new_block(f);
  uint32_t vbody = ir_new_block(f);
  uint32_t vexit = ir_new_block(f);
  ctx.vbody = vbody;
  ctx.vguard = vguard;
  h = &f->blocks[hb];

  if (prestep) bound = ir_emit(f, vguard, I_CONST, ct, NULL, 0, pre_bound, 0);
  uint32_t us = v_log2(U);
  Ref q0 = iventry[tp];
  Ref ok, T;
  Ref cnt = guard_trip_count(f, vguard, ct, rel, K, down, q0, bound,
                             clone_bound, U, skip, &T, &ok);
  if (npay) {
    /* at most 2^24 vector iterations, so a position stays below 2^28 */
    Ref cap = ir_emit(f, vguard, I_CONST, ct, NULL, 0, 0x00ffffffu, 0);
    ok = vemit(f, vguard, I_AND, VT_I32, ok,
               vemit(f, vguard, I_LEU, VT_I32, cnt, cap, 0, 0), 0, 0);
  }

  Ref cnt32 = ct == VT_I64 ? vemit(f, vguard, I_WRAP, VT_I32, cnt, REF_NONE, 0, 0) : cnt;
  Ref cnt64 = ct == VT_I64 ? cnt
            : vemit(f, vguard, I_EXTENDU, VT_I64, cnt, REF_NONE, 0, 0);

  Ref bytes = ((W & (W - 1)) == 0)
      ? vemit(f, vguard, I_SHL, VT_I64, cnt64,
              ir_emit(f, vguard, I_CONST, VT_I64, NULL, 0, v_log2(W), 0), 0, 0)
      : vemit(f, vguard, I_MUL, VT_I64, cnt64,
              ir_emit(f, vguard, I_CONST, VT_I64, NULL, 0, W, 0), 0, 0);
  Ref limit = idx64 ? REF_NONE
            : ir_emit(f, vguard, I_CONST, VT_I64, NULL, 0, 0x100000000ull, 0);

  Ref memsz[VEC_MAX_GRP];
  for (uint32_t g = 0; g < ctx.ngrp; g++) memsz[g] = REF_NONE;
  for (uint32_t g = 0; g < ctx.ngrp; g++) {
    if (!ctx.grp[g].spec && !idx64) continue;

    for (uint32_t q = 0; q < g; q++)
      if (memsz[q] != REF_NONE && ctx.grp[q].mem == ctx.grp[g].mem)
        memsz[g] = memsz[q];
    if (memsz[g] != REF_NONE) continue;

    Ref pages = ir_emit(f, vguard, I_MEM_SIZE, at, NULL, 0, ctx.grp[g].mem, 0);
    Ref p64 = idx64 ? pages
            : vemit(f, vguard, I_EXTENDU, VT_I64, pages, REF_NONE, 0, 0);
    memsz[g] = vemit(f, vguard, I_SHL, VT_I64, p64,
                     ir_emit(f, vguard, I_CONST, VT_I64, NULL, 0, 16, 0), 0, 0);
  }

  for (uint32_t g = 0; g < ctx.ngrp; g++) {
    VGroup *G = &ctx.grp[g];
    Ref p0 = iventry[G->ivx];
    uint64_t coeff = G->coeff;

    Ref a0 = REF_NONE;
    uint64_t cst = G->disp, p0v;
    if (is_const(f, p0, &p0v)) {
      cst = (cst + coeff * p0v) & amask;
    } else {
      a0 = p0;
      if ((ValType)f->insts[a0].type != at)
        a0 = vemit(f, vguard, I_WRAP, at, a0, REF_NONE, 0, 0);
      if (coeff != 1)
        a0 = vemit(f, vguard, I_MUL, at, a0,
                   ir_emit(f, vguard, I_CONST, at, NULL, 0, coeff, 0), 0, 0);
    }
    if (G->base != REF_NONE) {
      Ref gb = G->base;
      if ((ValType)f->insts[gb].type != at)
        gb = vemit(f, vguard, I_WRAP, at, gb, REF_NONE, 0, 0);
      a0 = a0 == REF_NONE ? gb : vemit(f, vguard, I_ADD, at, a0, gb, 0, 0);
    }

    /* An interleaved group moves m times the stride an iteration, and
       spans m times the bytes: backward, its first vectors start m times
       as far below the first iteration's elements, and either way it
       reaches m times as far as the others.  With one vector's extent the
       guard took an output just past it for one apart from it
       (test/cases/veccopies.wat pairs_alias), and backward the vectors
       were off by the difference (pairsrev). */
    if (backward) cst = (cst + (uint64_t)G->m * (Sa - W)) & amask;
    if (cst || a0 == REF_NONE) {
      Ref dc = ir_emit(f, vguard, I_CONST, at, NULL, 0, cst, 0);
      a0 = a0 == REF_NONE ? dc : vemit(f, vguard, I_ADD, at, a0, dc, 0, 0);
    }
    Ref a64 = idx64 ? a0
            : vemit(f, vguard, I_EXTENDU, VT_I64, a0, REF_NONE, 0, 0);
    Ref gbytes = G->m == 1 ? bytes
               : vemit(f, vguard, I_MUL, VT_I64, bytes,
                       ir_emit(f, vguard, I_CONST, VT_I64, NULL, 0, G->m, 0), 0, 0);
    Ref lo, hi;
    if (backward) {

      hi = vemit(f, vguard, I_ADD, VT_I64, a64,
                 ir_emit(f, vguard, I_CONST, VT_I64, NULL, 0, (uint64_t)W * G->m, 0),
                 0, 0);
      lo = vemit(f, vguard, I_SUB, VT_I64, hi, gbytes, 0, 0);
      ok = vemit(f, vguard, I_AND, VT_I32, ok,
                 vemit(f, vguard, I_LEU, VT_I32, lo, hi, 0, 0), 0, 0);
    } else {
      lo = a64;
      hi = vemit(f, vguard, I_ADD, VT_I64, a64, gbytes, 0, 0);

      if (idx64)
        ok = vemit(f, vguard, I_AND, VT_I32, ok,
                   vemit(f, vguard, I_LEU, VT_I32, lo, hi, 0, 0), 0, 0);
    }

    Ref fits = vemit(f, vguard, I_LEU, VT_I32, hi,
                     idx64 ? memsz[g] : limit, 0, 0);
    ok = vemit(f, vguard, I_AND, VT_I32, ok, fits, 0, 0);
    if (G->spec && !idx64)
      ok = vemit(f, vguard, I_AND, VT_I32, ok,
                 vemit(f, vguard, I_LEU, VT_I32, hi, memsz[g], 0, 0), 0, 0);
    G->lo = lo;
    G->hi = hi;
    G->ptr = vemit(f, vguard, I_MEM_PTR, VT_I64, a0, REF_NONE, 0, G->mem);
  }

  for (uint32_t i = 0; i < nchk; i++) {
    VGroup *A = &ctx.grp[chk[i][0]], *Bg = &ctx.grp[chk[i][1]];
    Ref before = vemit(f, vguard, I_LEU, VT_I32, A->hi, Bg->lo, 0, 0);
    Ref after = vemit(f, vguard, I_LEU, VT_I32, Bg->hi, A->lo, 0, 0);
    Ref apart = vemit(f, vguard, I_OR, VT_I32, before, after, 0, 0);
    ok = vemit(f, vguard, I_AND, VT_I32, ok, apart, 0, 0);
  }

  IrOp redvop[VEC_MAX_RED];
  for (uint32_t r = 0; r < nred; r++)
    redvop[r] = v_is_minmax_op(redop[r]) ? redop[r]
              : redop[r] == I_FMIN ? I_VFMIN
              : redop[r] == I_FMAX ? I_VFMAX
              : redop[r] == I_ADD ? I_VADD
              : redop[r] == I_AND ? I_VAND
              : redop[r] == I_OR  ? I_VOR : I_VXOR;

  /* A group at the very addresses of an earlier one -- the store back of
     an in-place update -- goes through that group's pointer, one bump
     fewer an iteration (vecbench/inplace). */
  uint32_t gown[VEC_MAX_GRP];
  for (uint32_t g = 0; g < ctx.ngrp; g++) {
    const VGroup *G = &ctx.grp[g];
    gown[g] = g;
    for (uint32_t q = 0; q < g && gown[g] == g; q++) {
      const VGroup *Q = &ctx.grp[q];
      if (Q->base == G->base && Q->ivx == G->ivx && Q->coeff == G->coeff &&
          Q->disp == G->disp && Q->m == G->m && Q->mem == G->mem)
        gown[g] = q;
    }
  }

  /* The bodies: the loop of R copies, and with more than one the loop of
     one copy after it. */
  uint32_t vbody1 = R > 1 ? ir_new_block(f) : REF_NONE;
  VBody bod[2];
  Ref accval[VEC_MAX_RED][VEC_MAX_COPY][VEC_MAX_VEC];
  Ref accbefore[VEC_MAX_RED][VEC_MAX_VEC];
  for (uint32_t bi = 0; bi < (R > 1 ? 2u : 1u); bi++) {
    VBody *B = &bod[bi];
    uint32_t vb = bi ? vbody1 : vbody, RC = bi ? 1u : R;
    B->blk = vb;
    B->R = RC;
    ctx.vbody = vb;
    ctx.copy = 0;
    if (bi) v_memo_next_copy(&ctx);

    for (uint32_t g = 0; g < ctx.ngrp; g++)
      B->pptr[g] = gown[g] == g ? ir_add_param(f, vb, VT_I64) : B->pptr[gown[g]];
    B->pcnt = ir_add_param(f, vb, ct);

    for (uint32_t r = 0; r < nred; r++)
      for (uint32_t c = 0; c < RC; c++) B->pacc[r][c] = ir_add_param(f, vb, VT_V128);
    B->ppos = REF_NONE;
    for (uint32_t i = 0; i < npay; i++) B->ppay[i] = ir_add_param(f, vb, VT_V128);
    if (npay) B->ppos = ir_add_param(f, vb, VT_V128);

    /* every copy's loads first, a group's side by side, which the back end
       pairs */
    for (uint32_t g = 0; g < ctx.ngrp; g++) {
      VGroup *G = &ctx.grp[g];
      if (G->store) continue;
      Ref raw[VEC_MAX_COPY][VEC_MAX_VEC];
      for (uint32_t c = 0; c < RC; c++)
        for (uint32_t v = 0; v < G->nv; v++)
          raw[c][v] = vemit(f, vb, I_VLOAD, VT_V128, B->pptr[g], REF_NONE,
                            (c * G->nv + v) * VEC_BYTES, lane);
      for (uint32_t c = 0; c < RC; c++) {
        if (G->m == 1) {
          for (uint32_t v = 0; v < G->nv; v++) G->loaded[c][v] = raw[c][v];
        } else {

          G->loaded[c][0] = vemit(f, vb, I_VUZP1, VT_V128, raw[c][0], raw[c][1], gran, lane);
          G->loaded[c][1] = vemit(f, vb, I_VUZP2, VT_V128, raw[c][0], raw[c][1], gran, lane);
        }
      }
    }

    Ref stored[VEC_MAX_GRP][VEC_MAX_COPY][VEC_MAX_VEC];
    Ref mulx[VEC_MAX_RED][VEC_MAX_COPY][VEC_MAX_VEC];
    Ref muly[VEC_MAX_RED][VEC_MAX_COPY][VEC_MAX_VEC];
    for (uint32_t c = 0; c < RC; c++) {
      ctx.copy = c;
      if (c) v_memo_next_copy(&ctx);
      bool bad = false;
      for (uint32_t g = 0; g < ctx.ngrp && !bad; g++) {
        VGroup *G = &ctx.grp[g];
        if (!G->store) continue;
        for (uint32_t v = 0; v < G->nv && !bad; v++) {
          Ref tuple[VEC_MAX_MEMB];
          for (uint32_t t = 0; t < mpv; t++)
            tuple[t] = ir_args(f, G->member[v_slot(G, mpv, v, t)])[1];
          stored[g][c][v] = v_build(&ctx, tuple, mpv, 1, VEC_DEPTH);
          if (stored[g][c][v] == REF_NONE) bad = true;
        }
      }

      for (uint32_t r = 0; r < nred && !bad; r++)
        for (uint32_t v = 0; v < nvec && !bad; v++) {
          if (redmul[r]) {
            Ref xs[VEC_MAX_MEMB], ys[VEC_MAX_MEMB];
            for (uint32_t t = 0; t < mpv; t++) {
              Ref *ma = ir_args(f, redterm[r][v * mpv + t]);
              xs[t] = ma[0]; ys[t] = ma[1];
            }
            mulx[r][c][v] = v_build(&ctx, xs, mpv, 1, VEC_DEPTH);
            muly[r][c][v] = v_build(&ctx, ys, mpv, 1, VEC_DEPTH);
            accval[r][c][v] = REF_NONE;
            if (mulx[r][c][v] == REF_NONE || muly[r][c][v] == REF_NONE) bad = true;
            continue;
          }
          accval[r][c][v] = v_build(&ctx, redterm[r] + v * mpv, mpv, 1, VEC_DEPTH);
          if (accval[r][c][v] == REF_NONE) bad = true;
        }
      if (bad) {

        VREJ_OP2("body is not vectorisable", ctx.fail ? ctx.fail : "no reason recorded");
      }

      for (uint32_t g = 0; g < ctx.ngrp && !bad; g++) {
        VGroup *G = &ctx.grp[g];
        if (!G->store || !G->spec) continue;
        for (uint32_t v = 0; v < G->nv && !bad; v++) {
          Ref ct[VEC_MAX_MEMB];
          for (uint32_t t = 0; t < mpv; t++)
            ct[t] = G->mcond[v_slot(G, mpv, v, t)];
          bool swap_arms = false;
          Ref mask = v_mask(&ctx, ct, mpv, 1, VEC_DEPTH, &swap_arms);
          if (mask == REF_NONE) { bad = true; break; }
          Ref old = vemit(f, vb, I_VLOAD, VT_V128, B->pptr[g], REF_NONE,
                          (c * G->nv + v) * VEC_BYTES, lane);

          bool keep_new = G->pol != swap_arms;
          stored[g][c][v] = vemit3(f, vb, I_VSEL, VT_V128, mask,
                                   keep_new ? stored[g][c][v] : old,
                                   keep_new ? old : stored[g][c][v], 0, lane);
        }
      }
      if (bad) VREJ("conditional store");
    }

    for (uint32_t g = 0; g < ctx.ngrp; g++) {
      VGroup *G = &ctx.grp[g];
      if (!G->store) continue;
      for (uint32_t c = 0; c < RC; c++) {
        uint64_t co = (uint64_t)c * G->nv * VEC_BYTES;
        if (G->m == 1) {
          for (uint32_t v = 0; v < G->nv; v++)
            vemit(f, vb, I_VSTORE, (ValType)0, B->pptr[g], stored[g][c][v],
                  co + v * VEC_BYTES, lane);
        } else {
          Ref z0 = vemit(f, vb, I_VZIP1, VT_V128, stored[g][c][0], stored[g][c][1],
                         gran, lane);
          Ref z1 = vemit(f, vb, I_VZIP2, VT_V128, stored[g][c][0], stored[g][c][1],
                         gran, lane);
          vemit(f, vb, I_VSTORE, (ValType)0, B->pptr[g], z0, co, lane);
          vemit(f, vb, I_VSTORE, (ValType)0, B->pptr[g], z1, co + VEC_BYTES, lane);
        }
      }
    }

    for (uint32_t r = 0; r < nred; r++) {
      IrOp vop = redvop[r];
      uint32_t sg = redsigned[r] ? 1u : 0u;
      for (uint32_t c = 0; c < RC; c++) {
        Ref acc = B->pacc[r][c];
        for (uint32_t v = 0; v < nvec; v++) {
          if (!c) accbefore[r][v] = acc;
          if (!redwiden[r]) {
            Ref cm = vop == I_VADD ? v_counted_mask(f, accval[r][c][v]) : REF_NONE;
            if (cm != REF_NONE)
              acc = vemit(f, vb, I_VSUB, VT_V128, acc, cm, 0, lane);
            else
              acc = vemit(f, vb, vop, VT_V128, acc, accval[r][c][v], 0, lane);
            continue;
          }

          Ref cur;
          if (redmul[r]) {

            Ref lo = vemit(f, vb, I_VEXTMUL, VT_V128, mulx[r][c][v], muly[r][c][v],
                           sg, lane + 1);
            Ref hi = vemit(f, vb, I_VEXTMUL, VT_V128, mulx[r][c][v], muly[r][c][v],
                           sg | 2u, lane + 1);
            cur = vemit(f, vb, I_VADD, VT_V128, lo, hi, 0, lane + 1);
          } else {
            /* A sum does not care which elements meet first: each step
               adds neighbouring lanes into one twice as wide, one
               instruction where widening the halves and adding them took
               three (vecbench/widen8 was bound by them). */
            cur = accval[r][c][v];
            for (uint32_t st = 0; st < redwiden[r]; st++)
              cur = vemit(f, vb, I_VEXTADD, VT_V128, cur, REF_NONE, sg,
                          lane + st + 1);
          }
          acc = vemit(f, vb, I_VADD, VT_V128, acc, cur, 0, lane + redwiden[r]);
        }
        B->nextacc[r][c] = acc;
      }
    }

    /* An index takes the position of an element where its reduction's
       compare, against the accumulator as it was before that vector, takes
       the element. */
    B->nextpos = REF_NONE;
    if (npay) {
      uint32_t lv = VEC_BYTES / width;
      for (uint32_t i = 0; i < npay; i++) {
        uint32_t r = payred[i];
        uint64_t vop;
        bool swap_ops, swap_arms;
        v_cmp_form(paycop[i], &vop, &swap_ops, &swap_arms);
        B->nextpay[i] = B->ppay[i];
        for (uint32_t v = 0; v < nvec; v++) {
          Ref pos = B->ppos;
          if (v) {
            Ref c = ir_emit(f, vguard, I_CONST, VT_I32, NULL, 0, v * lv, 0);
            pos = vemit(f, vb, I_VADD, VT_V128, B->ppos,
                        vemit(f, vguard, I_VSPLAT, VT_V128, c, REF_NONE, 0, lane),
                        0, lane);
          }
          Ref x = payltm[i] ? accval[r][0][v] : accbefore[r][v];
          Ref y = payltm[i] ? accbefore[r][v] : accval[r][0][v];
          if (swap_ops) { Ref t = x; x = y; y = t; }
          Ref mask = vemit(f, vb, I_VCMP, VT_V128, x, y, vop, lane);
          bool take_on_mask = payside[i] != swap_arms;
          B->nextpay[i] = vemit3(f, vb, I_VSEL, VT_V128, mask,
                                 take_on_mask ? pos : B->nextpay[i],
                                 take_on_mask ? B->nextpay[i] : pos, 0, lane);
        }
      }
      Ref c = ir_emit(f, vguard, I_CONST, VT_I32, NULL, 0, nvec * lv, 0);
      B->nextpos = vemit(f, vb, I_VADD, VT_V128, B->ppos,
                         vemit(f, vguard, I_VSPLAT, VT_V128, c, REF_NONE, 0, lane),
                         0, lane);
    }

    Ref step1 = ir_emit(f, vb, I_CONST, VT_I64, NULL, 0, (uint64_t)W * RC, 0);
    Ref stepm = REF_NONE;
    for (uint32_t g = 0; g < ctx.ngrp; g++) {
      if (gown[g] != g) { B->nptr[g] = B->nptr[gown[g]]; continue; }
      Ref st = step1;
      if (ctx.grp[g].m > 1) {
        if (stepm == REF_NONE)
          stepm = ir_emit(f, vb, I_CONST, VT_I64, NULL, 0,
                          (uint64_t)W * ctx.grp[g].m * RC, 0);
        st = stepm;
      }
      B->nptr[g] = vemit(f, vb, backward ? I_SUB : I_ADD, VT_I64, B->pptr[g], st, 0, 0);
    }
    Ref vone = ir_emit(f, vb, I_CONST, ct, NULL, 0, 1, 0);
    Ref vzero = ir_emit(f, vb, I_CONST, ct, NULL, 0, 0, 0);
    B->ncnt = vemit(f, vb, I_SUB, ct, B->pcnt, vone, 0, 0);
    B->more = vemit(f, vb, I_NE, VT_I32, B->ncnt, vzero, 0, 0);
  }

  Ref eacc[VEC_MAX_RED], epay[VEC_MAX_PAY];
  for (uint32_t r = 0; r < nred; r++)
    eacc[r] = ir_add_param(f, vexit, VT_V128);
  for (uint32_t i = 0; i < npay; i++)
    epay[i] = ir_add_param(f, vexit, VT_V128);

  for (uint32_t s = 0; s < f->blocks[pre].nsuccs; s++)
    if (f->blocks[pre].succs[s].target == hb) {
      f->blocks[pre].succs[s].target = vguard;
      f->blocks[pre].succs[s].nargs = 0;
    }

  Ref vb_args[VEC_MAX_GRP + 1 + VEC_MAX_RED * VEC_MAX_COPY + VEC_MAX_PAY + 1];
  Ref ident[VEC_MAX_RED];
  uint32_t nvb = 0;
  for (uint32_t g = 0; g < ctx.ngrp; g++)
    if (gown[g] == g) vb_args[nvb++] = ctx.grp[g].ptr;
  vb_args[nvb++] = cnt;
  for (uint32_t r = 0; r < nred; r++) {

    ValType st = (ValType)f->insts[redparam[r]].type;
    bool w32 = st == VT_I32 || st == VT_F32;

    /* +inf and -inf are the identities of a float min and max: no NaN, and
       min(+inf, -0) is -0 */
    uint64_t idv = redop[r] == I_FMIN ? (w32 ? 0x7f800000u : 0x7ff0000000000000ull)
                 : redop[r] == I_FMAX ? (w32 ? 0xff800000u : 0xfff0000000000000ull)
                 : redop[r] == I_AND ? (w32 ? 0xffffffffu : ~0ull)
                 : redop[r] == I_VSMIN ? (w32 ? 0x7fffffffu : 0x7fffffffffffffffull)
                 : redop[r] == I_VSMAX ? (w32 ? 0x80000000u : 0x8000000000000000ull)
                 : redop[r] == I_VUMIN ? (w32 ? 0xffffffffu : ~0ull)
                 : 0;
    Ref sc = ir_emit(f, vguard, I_CONST, st, NULL, 0, idv, 0);
    ident[r] = vemit(f, vguard, I_VSPLAT, VT_V128, sc, REF_NONE, 0,
                     lane + redwiden[r]);
    vb_args[nvb++] = ident[r];
  }
  if (npay) {
    /* a lane whose best is still the identity never took a position, and the
       exit never picks it */
    Ref z = ir_emit(f, vguard, I_CONST, VT_I32, NULL, 0, 0, 0);
    Ref zs = vemit(f, vguard, I_VSPLAT, VT_V128, z, REF_NONE, 0, lane);
    for (uint32_t i = 0; i < npay; i++) vb_args[nvb++] = zs;
    vb_args[nvb++] = ir_emit(f, vguard, I_CONST, VT_V128, NULL, 0,
                             0x0000000100000000ull, 0x0000000300000002ull);
  }
  f->blocks[vguard].term = T_BRIF;
  f->blocks[vguard].term_arg = ok;
  if (R == 1) {
    ir_add_edge(f, vguard, vbody, vb_args, nvb);
    ir_add_edge(f, vguard, hb, entry_args, np);
  } else {
    /* count / R iterations of R copies if there are any, the rest one copy
       at a time */
    uint32_t vsel = ir_new_block(f);
    ir_add_edge(f, vguard, vsel, NULL, 0);
    ir_add_edge(f, vguard, hb, entry_args, np);
    Ref cntR = vemit(f, vsel, I_SHRU, ct, cnt,
                     ir_emit(f, vsel, I_CONST, ct, NULL, 0, v_log2(R), 0), 0, 0);
    Ref zero = ir_emit(f, vsel, I_CONST, ct, NULL, 0, 0, 0);
    f->blocks[vsel].term = T_BRIF;
    f->blocks[vsel].term_arg = vemit(f, vsel, I_NE, VT_I32, cntR, zero, 0, 0);
    /* backward, the copies' pointer is the lowest of their R vectors */
    nvb = 0;
    for (uint32_t g = 0; g < ctx.ngrp; g++)
      if (gown[g] == g)
        vb_args[nvb++] = !backward ? ctx.grp[g].ptr
          : vemit(f, vsel, I_SUB, VT_I64, ctx.grp[g].ptr,
                  ir_emit(f, vsel, I_CONST, VT_I64, NULL, 0,
                          (uint64_t)(R - 1) * W * ctx.grp[g].m, 0), 0, 0);
    vb_args[nvb++] = cntR;
    for (uint32_t r = 0; r < nred; r++)
      for (uint32_t c = 0; c < R; c++) vb_args[nvb++] = ident[r];
    ir_add_edge(f, vsel, vbody, vb_args, nvb);
    nvb = 0;
    for (uint32_t g = 0; g < ctx.ngrp; g++)
      if (gown[g] == g) vb_args[nvb++] = ctx.grp[g].ptr;
    vb_args[nvb++] = cnt;
    for (uint32_t r = 0; r < nred; r++) vb_args[nvb++] = ident[r];
    ir_add_edge(f, vsel, vbody1, vb_args, nvb);
  }

  for (uint32_t bi = 0; bi < (R > 1 ? 2u : 1u); bi++) {
    VBody *B = &bod[bi];
    nvb = 0;
    for (uint32_t g = 0; g < ctx.ngrp; g++)
      if (gown[g] == g) vb_args[nvb++] = B->nptr[g];
    vb_args[nvb++] = B->ncnt;
    for (uint32_t r = 0; r < nred; r++)
      for (uint32_t c = 0; c < B->R; c++) vb_args[nvb++] = B->nextacc[r][c];
    for (uint32_t i = 0; i < npay; i++) vb_args[nvb++] = B->nextpay[i];
    if (npay) vb_args[nvb++] = B->nextpos;
    f->blocks[B->blk].term = T_BRIF;
    f->blocks[B->blk].term_arg = B->more;
    ir_add_edge(f, B->blk, B->blk, vb_args, nvb);
    if (B->R > 1) continue;

    Ref ex_args[VEC_MAX_RED + VEC_MAX_PAY];
    for (uint32_t r = 0; r < nred; r++) ex_args[r] = B->nextacc[r][0];
    for (uint32_t i = 0; i < npay; i++) ex_args[nred + i] = B->nextpay[i];
    ir_add_edge(f, B->blk, vexit, ex_args, nred + npay);
  }
  if (R > 1) {
    /* the copies' accumulators, combined pairwise; then the vectors the
       copies left, if any */
    uint32_t vmid = ir_new_block(f);
    Ref mptr[VEC_MAX_GRP], macc[VEC_MAX_RED][VEC_MAX_COPY];
    for (uint32_t g = 0; g < ctx.ngrp; g++)
      mptr[g] = gown[g] == g ? ir_add_param(f, vmid, VT_I64) : mptr[gown[g]];
    for (uint32_t r = 0; r < nred; r++)
      for (uint32_t c = 0; c < R; c++) macc[r][c] = ir_add_param(f, vmid, VT_V128);
    nvb = 0;
    for (uint32_t g = 0; g < ctx.ngrp; g++)
      if (gown[g] == g) vb_args[nvb++] = bod[0].nptr[g];
    for (uint32_t r = 0; r < nred; r++)
      for (uint32_t c = 0; c < R; c++) vb_args[nvb++] = bod[0].nextacc[r][c];
    ir_add_edge(f, vbody, vmid, vb_args, nvb);

    for (uint32_t r = 0; r < nred; r++)
      for (uint32_t sp = 1; sp < R; sp <<= 1)
        for (uint32_t c = 0; c + sp < R; c += 2 * sp)
          macc[r][c] = vemit(f, vmid, redvop[r], VT_V128, macc[r][c], macc[r][c + sp],
                             0, lane + redwiden[r]);
    Ref rem = vemit(f, vmid, I_AND, ct, cnt,
                    ir_emit(f, vmid, I_CONST, ct, NULL, 0, R - 1, 0), 0, 0);
    Ref zero = ir_emit(f, vmid, I_CONST, ct, NULL, 0, 0, 0);
    f->blocks[vmid].term = T_BRIF;
    f->blocks[vmid].term_arg = vemit(f, vmid, I_NE, VT_I32, rem, zero, 0, 0);
    nvb = 0;
    for (uint32_t g = 0; g < ctx.ngrp; g++)
      if (gown[g] == g)
        vb_args[nvb++] = !backward ? mptr[g]
          : vemit(f, vmid, I_ADD, VT_I64, mptr[g],
                  ir_emit(f, vmid, I_CONST, VT_I64, NULL, 0,
                          (uint64_t)(R - 1) * W * ctx.grp[g].m, 0), 0, 0);
    vb_args[nvb++] = rem;
    for (uint32_t r = 0; r < nred; r++) vb_args[nvb++] = macc[r][0];
    ir_add_edge(f, vmid, vbody1, vb_args, nvb);
    for (uint32_t r = 0; r < nred; r++) vb_args[r] = macc[r][0];
    ir_add_edge(f, vmid, vexit, vb_args, nred);
  }

  Ref exit_args[16];
  Ref done_iters = us
      ? vemit(f, vexit, I_SHL, VT_I32, cnt32,
              ir_emit(f, vexit, I_CONST, VT_I32, NULL, 0, us, 0), 0, 0)
      : cnt32;
  uint32_t ri = 0;
  for (uint32_t p = 0; p < np; p++) {
    ValType pt = (ValType)f->insts[hparam[p]].type;
    if (isiv[p]) {
      Ref adv = pt == VT_I64
              ? vemit(f, vexit, I_EXTENDU, VT_I64, done_iters, REF_NONE, 0, 0)
              : done_iters;
      uint64_t st = ivstep[p];
      Ref delta;
      if ((st & (st - 1)) == 0) {
        delta = st == 1 ? adv
              : vemit(f, vexit, I_SHL, pt, adv,
                      ir_emit(f, vexit, I_CONST, pt, NULL, 0, v_log2(st), 0), 0, 0);
      } else {
        Ref sc = ir_emit(f, vexit, I_CONST, pt, NULL, 0, st, 0);
        delta = vemit(f, vexit, I_MUL, pt, adv, sc, 0, 0);
      }
      exit_args[p] = vemit(f, vexit, ivadd[p] ? I_ADD : I_SUB, pt,
                           entry_args[p], delta, 0, 0);
    } else if (paymap[p] >= 0) {
      uint32_t i = (uint32_t)paymap[p], r = payred[i];
      uint32_t rp = 0;
      while (hparam[rp] != redparam[r]) rp++;
      ValType rt = (ValType)f->insts[redparam[r]].type;
      Ref m = vemit(f, vexit, I_VREDUCE, rt, eacc[r], REF_NONE, redop[r], lane);
      uint64_t eqop;
      bool so, sa;
      v_cmp_form(I_EQ, &eqop, &so, &sa);
      Ref best = vemit(f, vexit, I_VCMP, VT_V128, eacc[r],
                       vemit(f, vexit, I_VSPLAT, VT_V128, m, REF_NONE, 0, lane),
                       eqop, lane);
      Ref none = ir_emit(f, vexit, I_CONST, VT_I32, NULL, 0,
                         payfirst[i] ? 0xffffffffu : 0, 0);
      Ref cand = vemit3(f, vexit, I_VSEL, VT_V128, best, epay[i],
                        vemit(f, vexit, I_VSPLAT, VT_V128, none, REF_NONE, 0, lane),
                        0, lane);
      Ref pos = vemit(f, vexit, I_VREDUCE, VT_I32, cand, REF_NONE,
                      payfirst[i] ? I_VUMIN : I_VUMAX, lane);
      Ref base = vemit(f, vexit, I_ADD, VT_I32, entry_args[payiv[i]],
                       ir_emit(f, vexit, I_CONST, VT_I32, NULL, 0, payc0[i], 0), 0, 0);
      Ref idx = vemit(f, vexit, I_ADD, VT_I32, base,
                      vemit(f, vexit, I_MUL, VT_I32, pos,
                            ir_emit(f, vexit, I_CONST, VT_I32, NULL, 0, payd[i], 0),
                            0, 0), 0, 0);
      bool mn = redop[r] == I_VSMIN || redop[r] == I_VUMIN;
      bool sg = redop[r] == I_VSMIN || redop[r] == I_VSMAX;
      IrOp bop = mn ? (payfirst[i] ? (sg ? I_LTS : I_LTU) : (sg ? I_LES : I_LEU))
                    : (payfirst[i] ? (sg ? I_GTS : I_GTU) : (sg ? I_GES : I_GEU));
      Ref better = vemit(f, vexit, bop, VT_I32, m, entry_args[rp], 0, 0);
      Ref sa3[3] = { idx, entry_args[p], better };
      exit_args[p] = ir_emit(f, vexit, I_SELECT, VT_I32, sa3, 3, 0, 0);
    } else if (redop[ri] == I_FMIN || redop[ri] == I_FMAX) {
      /* The lanes one by one, onto the value the loop came in with: the
         loop leaves once, and both back ends extract a float lane already. */
      Ref acc = entry_args[p];
      uint32_t nl = VEC_BYTES >> VLANE_SIZE(lane);
      for (uint32_t l = 0; l < nl; l++) {
        Ref e = vemit(f, vexit, I_VEXTRACTU, pt, eacc[ri], REF_NONE, l, lane);
        acc = vemit(f, vexit, redop[ri], pt, acc, e, 0, 0);
      }
      exit_args[p] = acc;
      ri++;
    } else {

      Ref red = vemit(f, vexit, I_VREDUCE, pt, eacc[ri], REF_NONE,
                      redop[ri], lane + redwiden[ri]);

      exit_args[p] = v_is_minmax_op(redop[ri])
          ? v_scalar_minmax(f, vexit, redop[ri], pt, entry_args[p], red)
          : vemit(f, vexit, redop[ri], pt, entry_args[p], red, 0, 0);
      ri++;
    }
  }
  if (!skip) {
    f->blocks[vexit].term = T_JUMP;
    ir_add_edge(f, vexit, hb, exit_args, np);
  } else {
    /* The exit block takes, after what it took, each carried value read
       past the loop: from the latch the value itself, from the vector exit
       what the exit made of it. */
    Edge *ex = &f->blocks[latch].succs[xsucc];
    uint32_t nx = ex->nargs;
    Ref *xargs = xmalloc((nx + np + 1) * sizeof(Ref));
    for (uint32_t j = 0; j < nx; j++) {
      int p = v_live_slot(f, inloop, ex->args[j], back_args, np);
      xargs[j] = p < (int)np ? exit_args[p] : ex->args[j];
    }
    Ref xpar[16];
    for (uint32_t p = 0; p < np; p++) {
      xpar[p] = REF_NONE;
      if (!xneed[p]) continue;
      xpar[p] = ir_add_param(f, xb, (ValType)f->insts[back_args[p]].type);
      edge_push_arg_at(f, latch, xsucc, back_args[p]);
      xargs[nx++] = exit_args[p];
    }
    v_exit_rewrite(f, inloop, back_args, np, xpar);
    if (U == 1) {
      f->blocks[vexit].term = T_JUMP;
      ir_add_edge(f, vexit, xb, xargs, nx);
    } else {
      Ref left = vemit(f, vexit, I_AND, ct, T,
                       ir_emit(f, vexit, I_CONST, ct, NULL, 0, U - 1, 0), 0, 0);
      Ref zero = ir_emit(f, vexit, I_CONST, ct, NULL, 0, 0, 0);
      f->blocks[vexit].term = T_BRIF;
      f->blocks[vexit].term_arg = vemit(f, vexit, I_NE, VT_I32, left, zero, 0, 0);
      ir_add_edge(f, vexit, hb, exit_args, np);
      ir_add_edge(f, vexit, xb, xargs, nx);
    }
    xfree(xargs);
  }

  if (warmc_getenv("WARMC_VEC_DEBUG"))
    fprintf(stderr, "vec: fn=%s b%u lane=%u width=%u k=%u U=%u groups=%u red=%u"
            " copies=%u past=%d\n",
            f->m->funcs[f->func_index].sym, hb, lane, width, k, U, ctx.ngrp, nred,
            R, (int)skip);
  g_stats.vectorized++;
  return true;
#undef VREJ
}

typedef struct {
  uint32_t latch;
  uint32_t entry;
  Ref iv;
  Ref ivnext;
  Ref base;
  Ref bound;
  Ref want;
  uint32_t lane;
  uint32_t width;
  uint64_t memflags;
  IrOp pred;
  bool exit_on_true;
  /* LLVM's unrolled form (vs_match_unrolled): the element is at
     (base + iv) + disp, summed in i32 as the scalar code sums it; the latch
     is a != test, so the count left is bound - iv however the two compare;
     and a bound of REF_NONE is zero, for a latch that branches on the
     stepped counter itself. */
  uint64_t disp;
  uint64_t want_imm;
  bool wrap32;
  bool ne_latch;
} VSearch;

static bool vs_pure(uint16_t op) {
  switch (op) {
    case I_CONST: case I_ADD: case I_SUB: case I_MUL:
    case I_AND: case I_OR: case I_XOR:
    case I_SHL: case I_SHRU: case I_SHRS:
    case I_EQ: case I_NE: case I_EQZ:
    case I_LTS: case I_LES: case I_GTS: case I_GES:
    case I_LTU: case I_LEU: case I_GTU: case I_GEU:
    case I_EXTENDU: case I_EXTENDS: case I_WRAP:
    case I_EXT8S: case I_EXT16S: case I_EXT32S:
    case I_MEM_ADDR: case I_LOAD:
      return true;
    default:
      return false;
  }
}

static uint64_t vs_addk(IrFunc *f, Ref r, Ref x) {
  if (f->insts[r].op != I_ADD || f->insts[r].nargs != 2) return 0;
  Ref *a = ir_args(f, r);
  uint64_t c;
  if (a[0] == x && is_const(f, a[1], &c)) return c;
  if (a[1] == x && is_const(f, a[0], &c)) return c;
  return 0;
}

static bool vs_invariant(IrFunc *f, const uint8_t *inloop, Ref r) {
  uint32_t b = f->insts[r].block;
  return b < f->nblocks && !inloop[b];
}

#define VSREJ(w) do { if (warmc_getenv("WARMC_VEC_DEBUG")) \
    fprintf(stderr, "vecsearch %s b%u reject: %s\n", \
            f->m->funcs[f->func_index].sym, hb, w); return false; } while (0)

static bool vs_match(IrFunc *f, uint32_t hb, uint8_t *inloop, VSearch *S) {
  Block *h = &f->blocks[hb];
  if (h->nparams != 1 || h->is_pad) VSREJ("header does not carry exactly one value");
  if (h->term != T_BRIF || h->nsuccs != 2) VSREJ("header does not end in a two-way branch");

  uint32_t nback = 0, nfwd = 0;
  S->latch = S->entry = UINT32_MAX;
  for (uint32_t i = 0; i < h->npreds; i++) {
    uint32_t p = h->preds[i];
    if (!f->blocks[p].reachable) continue;
    if (f->blocks[p].rpo >= h->rpo) { S->latch = p; nback++; }
    else { S->entry = p; nfwd++; }
  }
  if (nback != 1 || nfwd != 1) VSREJ("more than one way in or round");
  uint32_t lrpo = f->blocks[S->latch].rpo;
  for (uint32_t b = 0; b < f->nblocks; b++)
    inloop[b] = f->blocks[b].reachable && f->blocks[b].rpo >= h->rpo &&
                f->blocks[b].rpo <= lrpo;
  for (uint32_t b = 0; b < f->nblocks; b++) {
    if (!inloop[b] || b == hb) continue;
    for (uint32_t p = 0; p < f->blocks[b].npreds; p++)
      if (!inloop[f->blocks[b].preds[p]]) VSREJ("a body block has a second way in");
  }

  uint32_t t = h->succs[0].target, e = h->succs[1].target;
  uint32_t cont;
  if (inloop[t] && !inloop[e]) { cont = t; S->exit_on_true = false; }
  else if (inloop[e] && !inloop[t]) { cont = e; S->exit_on_true = true; }
  else VSREJ("neither arm of the header leaves the loop");

  uint32_t cur = cont, guardn = 0;
  while (cur != S->latch) {
    Block *cb = &f->blocks[cur];
    if (cb->term != T_JUMP || cb->nsuccs != 1 || cb->nparams != 0) VSREJ("body is not a straight run to the latch");
    if (++guardn > 8) VSREJ("body is too long");
    cur = cb->succs[0].target;
    if (!inloop[cur]) VSREJ("body leaves the loop early");
  }

  for (uint32_t b = 0; b < f->nblocks; b++) {
    if (!inloop[b]) continue;
    Block *bb = &f->blocks[b];
    for (uint32_t i = 0; i < bb->ninsts; i++)
      if (!vs_pure(f->insts[bb->insts[i]].op)) VSREJ("body does something besides compute");
  }

  S->iv = h->params[0];
  ValType ivt = (ValType)f->insts[S->iv].type;
  if (ivt != VT_I32) VSREJ("counter is not i32");

  Block *lb = &f->blocks[S->latch];
  if (lb->term != T_BRIF || lb->nsuccs != 2) VSREJ("latch does not end in a two-way branch");
  uint32_t back = lb->succs[0].target == hb ? 0 : 1;
  if (lb->succs[back].target != hb || lb->succs[!back].target == hb) VSREJ("latch does not branch back");
  if (lb->succs[back].nargs != 1) VSREJ("latch carries the wrong number of values");
  S->ivnext = lb->succs[back].args[0];
  if (vs_addk(f, S->ivnext, S->iv) != 1) VSREJ("counter does not step by one");
  Ref lc = lb->term_arg;
  Inst *li = &f->insts[lc];
  if (li->nargs != 2) VSREJ("latch test has the wrong shape");
  Ref *la = ir_args(f, lc);

  bool ok_latch = false;
  if (li->op == I_EQ && back == 1) ok_latch = true;
  else if (li->op == I_NE && back == 0) ok_latch = true;
  else if ((li->op == I_LTU || li->op == I_LTS) && back == 0 &&
           la[0] == S->ivnext) ok_latch = true;
  else if ((li->op == I_GTU || li->op == I_GTS) && back == 0 &&
           la[1] == S->ivnext) ok_latch = true;
  if (!ok_latch) VSREJ("latch test is not a counted one");
  { if (la[0] == S->ivnext) S->bound = la[1];
    else if (la[1] == S->ivnext) S->bound = la[0];
    else VSREJ("bound is not the counter compared with something"); }
  if (!vs_invariant(f, inloop, S->bound)) VSREJ("bound is not loop-invariant");
  if ((ValType)f->insts[S->bound].type != VT_I32) VSREJ("bound is not i32");

  Ref cond = h->term_arg, ld;
  if (f->insts[cond].op == I_LOAD) {
    ld = cond;
    S->pred = S->exit_on_true ? I_NE : I_EQ;
    S->want = REF_NONE;
  } else if (f->insts[cond].op == I_EQ || f->insts[cond].op == I_NE) {
    Ref *ca = ir_args(f, cond);
    if (f->insts[ca[0]].op == I_LOAD) { ld = ca[0]; S->want = ca[1]; }
    else if (f->insts[ca[1]].op == I_LOAD) { ld = ca[1]; S->want = ca[0]; }
    else return false;
    if (!vs_invariant(f, inloop, S->want)) VSREJ("compared value is not invariant");
    bool is_eq = f->insts[cond].op == I_EQ;
    S->pred = (is_eq == S->exit_on_true) ? I_EQ : I_NE;
  } else return false;
  if (f->insts[ld].block != hb) VSREJ("load is not in the header");

  S->memflags = f->insts[ld].imm2;
  uint32_t lg = (uint32_t)MEMF_SIZE(S->memflags);
  if (f->insts[ld].imm != 0) return false;
  if (S->memflags & MEMF_SIGNED) VSREJ("load is signed");
  if ((ValType)f->insts[ld].type != VT_I32) VSREJ("load is not i32");
  if (lg > 2) VSREJ("element is wider than a lane");
  S->width = 1u << lg;
  S->lane = lg;

  if (S->want != REF_NONE) {
    uint64_t wc;
    if (!is_const(f, S->want, &wc)) VSREJ("compared value is not a constant");
    if (S->width < 4 && (wc >> (8 * S->width)) != 0) VSREJ("compared value does not fit the lane");
  }

  if (f->insts[ld].nargs != 1) VSREJ("load takes no address");
  Ref ma = ir_args(f, ld)[0];
  if (f->insts[ma].op != I_MEM_ADDR || f->insts[ma].nargs != 1) VSREJ("address is not a memory address");
  if (f->insts[ma].imm != 0) VSREJ("address has a displacement");
  Ref sum = ir_args(f, ma)[0];
  if (f->insts[sum].op != I_ADD || f->insts[sum].nargs != 2) VSREJ("address is not a sum");
  { Ref *sa = ir_args(f, sum);
    Ref off;
    if (vs_invariant(f, inloop, sa[0])) { S->base = sa[0]; off = sa[1]; }
    else if (vs_invariant(f, inloop, sa[1])) { S->base = sa[1]; off = sa[0]; }
    else return false;

    if (S->width == 1) { if (off != S->iv) return false; }
    else {
      if (f->insts[off].op != I_SHL || f->insts[off].nargs != 2) VSREJ("index is not the counter scaled");
      Ref *oa = ir_args(f, off);
      uint64_t sh;
      if (oa[0] != S->iv || !is_const(f, oa[1], &sh) || sh != S->lane) VSREJ("index scale is wrong");
    }
  }

  for (uint32_t b = 0; b < f->nblocks; b++) {
    if (!inloop[b]) continue;
    Block *bb = &f->blocks[b];
    for (uint32_t i = 0; i < bb->ninsts; i++)
      if (f->insts[bb->insts[i]].op == I_LOAD && bb->insts[i] != ld)
        VSREJ("the loop loads something besides the element it searches");
  }
  return true;
}

/* The same search as LLVM unrolls it, TinyGo's -opt=2 among others: the
   counter steps by U, and each of U blocks in a row tests one byte, at
   (base + iv) + C + k, and leaves the loop when it matches.  vs_emit's
   vector loop finds the sixteen-byte block the match is in and enters this
   loop at that block's first byte, where the unrolled tests say which byte
   it was -- so U has to divide sixteen, for the vector loop's counter to be
   one the scalar loop reaches too. */
static bool vs_match_unrolled(IrFunc *f, uint32_t hb, uint8_t *inloop,
                              VSearch *S) {
  Block *h = &f->blocks[hb];
  if (h->nparams != 1 || h->is_pad || h->term != T_BRIF || h->nsuccs != 2)
    return false;
  uint32_t nback = 0, nfwd = 0;
  S->latch = S->entry = UINT32_MAX;
  for (uint32_t i = 0; i < h->npreds; i++) {
    uint32_t p = h->preds[i];
    if (!f->blocks[p].reachable) continue;
    if (f->blocks[p].rpo >= h->rpo) { S->latch = p; nback++; }
    else { S->entry = p; nfwd++; }
  }
  if (nback != 1 || nfwd != 1) return false;
  uint32_t lrpo = f->blocks[S->latch].rpo;
  for (uint32_t b = 0; b < f->nblocks; b++)
    inloop[b] = f->blocks[b].reachable && f->blocks[b].rpo >= h->rpo &&
                f->blocks[b].rpo <= lrpo;
  for (uint32_t b = 0; b < f->nblocks; b++) {
    if (!inloop[b] || b == hb) continue;
    for (uint32_t p = 0; p < f->blocks[b].npreds; p++)
      if (!inloop[f->blocks[b].preds[p]]) VSREJ("unrolled: a body block has a second way in");
  }
  S->iv = h->params[0];
  if ((ValType)f->insts[S->iv].type != VT_I32) VSREJ("unrolled: counter is not i32");

  /* The tests, header first, each one block. */
  uint32_t U = 0, cur = hb;
  uint64_t want = 0;
  Ref row = REF_NONE;
  while (cur != S->latch) {
    Block *cb = &f->blocks[cur];
    if (cb->term != T_BRIF || cb->nsuccs != 2) VSREJ("unrolled: a test is not a two-way branch");
    if (cur != hb && cb->nparams) VSREJ("unrolled: a test block takes values");
    uint32_t a = cb->succs[0].target, b = cb->succs[1].target, next, ni;
    bool exit_true;
    if (!inloop[a] && inloop[b]) { exit_true = true; next = b; ni = 1; }
    else if (inloop[a] && !inloop[b]) { exit_true = false; next = a; ni = 0; }
    else VSREJ("unrolled: a test does not leave the loop");
    if (cb->succs[ni].nargs) VSREJ("unrolled: a test passes values on");
    if (U == 16) VSREJ("unrolled: more than sixteen tests");

    Ref cond = cb->term_arg, ld;
    uint64_t w = 0;
    bool is_eq;
    Inst *ci = &f->insts[cond];
    if (ci->op == I_LOAD) { ld = cond; is_eq = false; }
    else if (ci->op == I_EQZ && ci->nargs == 1) { ld = ir_args(f, cond)[0]; is_eq = true; }
    else if ((ci->op == I_EQ || ci->op == I_NE) && ci->nargs == 2) {
      Ref *ca = ir_args(f, cond);
      Ref other;
      if (f->insts[ca[0]].op == I_LOAD) { ld = ca[0]; other = ca[1]; }
      else { ld = ca[1]; other = ca[0]; }
      if (!is_const(f, other, &w)) VSREJ("unrolled: compared value is not a constant");
      is_eq = ci->op == I_EQ;
    } else VSREJ("unrolled: a test is not of an element");
    IrOp pred = (is_eq == exit_true) ? I_EQ : I_NE;
    if (f->insts[ld].op != I_LOAD || f->insts[ld].block != cur || f->insts[ld].nargs != 1)
      VSREJ("unrolled: the element is not loaded where it is tested");
    uint64_t mf = f->insts[ld].imm2;
    if (MEMF_SIZE(mf) != 0 || (mf & MEMF_SIGNED) || f->insts[ld].imm != 0 ||
        (ValType)f->insts[ld].type != VT_I32)
      VSREJ("unrolled: the element is not an unsigned byte");
    if (w > 0xff) VSREJ("unrolled: compared value does not fit a byte");

    Ref ma = ir_args(f, ld)[0];
    if (f->insts[ma].op != I_MEM_ADDR || f->insts[ma].nargs != 1 || f->insts[ma].imm != 0)
      VSREJ("unrolled: address is not a plain memory address");
    Ref s = ir_args(f, ma)[0];
    uint64_t c = 0;
    Inst *si = &f->insts[s];
    if (si->op == I_ADD && si->nargs == 2 && is_const(f, ir_args(f, s)[1], &c))
      s = ir_args(f, s)[0];
    else if (si->op == I_ADD && si->nargs == 2 && is_const(f, ir_args(f, s)[0], &c))
      s = ir_args(f, s)[1];
    if (U == 0) {
      Inst *ri = &f->insts[s];
      if (ri->op != I_ADD || ri->nargs != 2) VSREJ("unrolled: address is not base plus counter");
      Ref *ra = ir_args(f, s);
      if (ra[0] == S->iv && vs_invariant(f, inloop, ra[1])) S->base = ra[1];
      else if (ra[1] == S->iv && vs_invariant(f, inloop, ra[0])) S->base = ra[0];
      else VSREJ("unrolled: address is not base plus counter");
      row = s;
      S->disp = c;
      S->pred = pred;
      S->memflags = mf;
      want = w;
    } else {
      if (s != row) VSREJ("unrolled: tests address different rows");
      if (c != ((S->disp + U) & 0xffffffffu)) VSREJ("unrolled: tests are not of consecutive bytes");
      if (pred != S->pred || w != want || MEMF_MEM(mf) != MEMF_MEM(S->memflags))
        VSREJ("unrolled: tests differ");
    }
    U++;
    cur = next;
  }
  if (U < 2 || 16 % U) VSREJ("unrolled: count does not divide sixteen");

  Block *lb = &f->blocks[S->latch];
  if (lb->term != T_BRIF || lb->nsuccs != 2) VSREJ("unrolled: latch is not a two-way branch");
  uint32_t back = lb->succs[0].target == hb ? 0 : 1;
  if (lb->succs[back].target != hb || lb->succs[!back].target == hb)
    VSREJ("unrolled: latch does not branch back");
  if (lb->succs[back].nargs != 1) VSREJ("unrolled: latch carries the wrong number of values");
  S->ivnext = lb->succs[back].args[0];
  if (vs_addk(f, S->ivnext, S->iv) != U) VSREJ("unrolled: counter does not step by the count");
  Ref lc = lb->term_arg;
  if (lc == S->ivnext && back == 0) {
    S->bound = REF_NONE;
    S->ne_latch = true;
  } else {
    Inst *li = &f->insts[lc];
    if (li->nargs != 2) VSREJ("unrolled: latch test has the wrong shape");
    Ref *la = ir_args(f, lc);
    if (li->op == I_EQ && back == 1) S->ne_latch = true;
    else if (li->op == I_NE && back == 0) S->ne_latch = true;
    else if (!(((li->op == I_LTU || li->op == I_LTS) && back == 0 && la[0] == S->ivnext) ||
               ((li->op == I_GTU || li->op == I_GTS) && back == 0 && la[1] == S->ivnext)))
      VSREJ("unrolled: latch test is not a counted one");
    if (la[0] == S->ivnext) S->bound = la[1];
    else if (la[1] == S->ivnext) S->bound = la[0];
    else VSREJ("unrolled: bound is not the counter compared with something");
    if (!vs_invariant(f, inloop, S->bound) || (ValType)f->insts[S->bound].type != VT_I32)
      VSREJ("unrolled: bound is not a loop-invariant i32");
  }

  uint32_t nloads = 0;
  for (uint32_t b = 0; b < f->nblocks; b++) {
    if (!inloop[b]) continue;
    Block *bb = &f->blocks[b];
    for (uint32_t i = 0; i < bb->ninsts; i++) {
      uint16_t op = f->insts[bb->insts[i]].op;
      if (!vs_pure(op)) VSREJ("unrolled: body does something besides compute");
      if (op == I_LOAD) nloads++;
    }
  }
  if (nloads != U) VSREJ("unrolled: the loop loads something besides the bytes it searches");

  S->want = REF_NONE;
  S->want_imm = want;
  S->width = 1;
  S->lane = 0;
  S->wrap32 = true;
  S->exit_on_true = true;
  return true;
}

static Ref vs_mem_bytes(IrFunc *f, uint32_t bi, const VSearch *S) {
  Ref pages = ir_emit(f, bi, I_MEM_SIZE, VT_I32, NULL, 0,
                      MEMF_MEM(S->memflags), 0);
  Ref pages64 = vemit(f, bi, I_EXTENDU, VT_I64, pages, REF_NONE, 0, 0);
  return vemit(f, bi, I_SHL, VT_I64, pages64,
               ir_emit(f, bi, I_CONST, VT_I64, NULL, 0, 16, 0), 0, 0);
}

static Ref vs_block_in_bounds(IrFunc *f, uint32_t bi, const VSearch *S,
                              Ref idx, Ref memsz) {
  if (S->wrap32) {
    Ref a = vemit(f, bi, I_ADD, VT_I32, S->base, idx, 0, 0);
    if (S->disp)
      a = vemit(f, bi, I_ADD, VT_I32, a,
                ir_emit(f, bi, I_CONST, VT_I32, NULL, 0, S->disp, 0), 0, 0);
    Ref a64 = vemit(f, bi, I_EXTENDU, VT_I64, a, REF_NONE, 0, 0);
    Ref end = vemit(f, bi, I_ADD, VT_I64, a64,
                    ir_emit(f, bi, I_CONST, VT_I64, NULL, 0, 16, 0), 0, 0);
    return vemit(f, bi, I_LEU, VT_I32, end, memsz, 0, 0);
  }
  Ref base64 = vemit(f, bi, I_EXTENDU, VT_I64, S->base, REF_NONE, 0, 0);
  Ref idx64 = vemit(f, bi, I_EXTENDU, VT_I64, idx, REF_NONE, 0, 0);
  Ref off64 = S->lane == 0 ? idx64
            : vemit(f, bi, I_SHL, VT_I64, idx64,
                    ir_emit(f, bi, I_CONST, VT_I64, NULL, 0, S->lane, 0), 0, 0);
  Ref start = vemit(f, bi, I_ADD, VT_I64, base64, off64, 0, 0);
  Ref end = vemit(f, bi, I_ADD, VT_I64, start,
                  ir_emit(f, bi, I_CONST, VT_I64, NULL, 0, 16, 0), 0, 0);
  return vemit(f, bi, I_LEU, VT_I32, end, memsz, 0, 0);
}

static void vs_emit(IrFunc *f, uint32_t hb, const VSearch *S) {
  uint32_t VF = 16u / S->width;
  uint32_t vguard = ir_new_block(f);
  uint32_t vbody = ir_new_block(f);
  uint32_t vnext = ir_new_block(f);
  Ref bj = ir_add_param(f, vbody, VT_I32);
  Ref nj = ir_add_param(f, vnext, VT_I32);

  Ref i0 = REF_NONE;
  for (uint32_t i = 0; i < f->blocks[S->entry].nsuccs; i++)
    if (f->blocks[S->entry].succs[i].target == hb) {
      i0 = f->blocks[S->entry].succs[i].args[0];
      f->blocks[S->entry].succs[i].target = vguard;
      f->blocks[S->entry].succs[i].nargs = 0;
      break;
    }

  Ref vfc = ir_emit(f, vguard, I_CONST, VT_I32, NULL, 0, VF, 0);

  /* A != latch counts bound - i0 however the two compare; the others
     need i0 below the bound first.  REF_NONE is a bound of zero. */
  Ref bnd = S->bound != REF_NONE ? S->bound
          : ir_emit(f, vguard, I_CONST, VT_I32, NULL, 0, 0, 0);
  Ref le = S->ne_latch ? REF_NONE : vemit(f, vguard, I_LEU, VT_I32, i0, bnd, 0, 0);
  Ref room = vemit(f, vguard, I_SUB, VT_I32, bnd, i0, 0, 0);
  Ref fits = vemit(f, vguard, I_GTU, VT_I32, room, vfc, 0, 0);
  Ref ok = S->ne_latch ? fits : vemit(f, vguard, I_AND, VT_I32, le, fits, 0, 0);

  Ref memsz = vs_mem_bytes(f, vguard, S);
  ok = vemit(f, vguard, I_AND, VT_I32, ok,
             vs_block_in_bounds(f, vguard, S, i0, memsz), 0, 0);
  f->blocks[vguard].term = T_BRIF;
  f->blocks[vguard].term_arg = ok;
  ir_add_edge(f, vguard, vbody, &i0, 1);
  ir_add_edge(f, vguard, hb, &i0, 1);

  Ref off = S->width == 1 ? bj
          : vemit(f, vbody, I_SHL, VT_I32, bj,
                  ir_emit(f, vbody, I_CONST, VT_I32, NULL, 0, S->lane, 0), 0, 0);
  Ref sum = vemit(f, vbody, I_ADD, VT_I32, S->base, off, 0, 0);
  if (S->disp)
    sum = vemit(f, vbody, I_ADD, VT_I32, sum,
                ir_emit(f, vbody, I_CONST, VT_I32, NULL, 0, S->disp, 0), 0, 0);

  Ref ptr = vemit(f, vbody, I_MEM_PTR, VT_I64, sum, REF_NONE, 0,
                  MEMF_MEM(S->memflags));
  Ref vec = vemit(f, vbody, I_VLOAD, VT_V128, ptr, REF_NONE, 0, S->lane);
  Ref wantc = S->want != REF_NONE ? S->want
            : ir_emit(f, vbody, I_CONST, VT_I32, NULL, 0, S->want_imm, 0);
  Ref spl = vemit(f, vbody, I_VSPLAT, VT_V128, wantc, REF_NONE, 0, S->lane);
  Ref m = vemit(f, vbody, I_VCMP, VT_V128, vec, spl, S->pred, S->lane);
  Ref any = vemit(f, vbody, I_VANYTRUE, VT_I32, m, REF_NONE, 0, S->lane);
  f->blocks[vbody].term = T_BRIF;
  f->blocks[vbody].term_arg = any;
  ir_add_edge(f, vbody, hb, &bj, 1);
  ir_add_edge(f, vbody, vnext, &bj, 1);

  Ref vfc2 = ir_emit(f, vnext, I_CONST, VT_I32, NULL, 0, VF, 0);
  Ref j2 = vemit(f, vnext, I_ADD, VT_I32, nj, vfc2, 0, 0);
  Ref bnd2 = S->bound != REF_NONE ? S->bound
           : ir_emit(f, vnext, I_CONST, VT_I32, NULL, 0, 0, 0);
  Ref room2 = vemit(f, vnext, I_SUB, VT_I32, bnd2, j2, 0, 0);
  Ref more = vemit(f, vnext, I_GTU, VT_I32, room2, vfc2, 0, 0);

  Ref memsz2 = vs_mem_bytes(f, vnext, S);
  more = vemit(f, vnext, I_AND, VT_I32, more,
               vs_block_in_bounds(f, vnext, S, j2, memsz2), 0, 0);
  f->blocks[vnext].term = T_BRIF;
  f->blocks[vnext].term_arg = more;
  ir_add_edge(f, vnext, vbody, &j2, 1);
  ir_add_edge(f, vnext, hb, &j2, 1);
}

static bool pass_vec_search(IrFunc *f) {
  bool changed = false;
  uint32_t norig = f->nblocks;
  uint8_t *inloop = NULL;
  uint32_t lcap = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    if (hb >= norig) continue;
    if (f->nblocks > lcap) { lcap = f->nblocks; inloop = xrealloc(inloop, lcap); }
    VSearch S;
    memset(&S, 0, sizeof S);
    if (!vs_match(f, hb, inloop, &S)) {
      memset(&S, 0, sizeof S);
      if (!vs_match_unrolled(f, hb, inloop, &S)) continue;
    }
    if (warmc_getenv("WARMC_VEC_DEBUG"))
      fprintf(stderr, "vecsearch %s b%u: width=%u lanes=%u\n",
              f->m->funcs[f->func_index].sym, hb, S.width, 16u / S.width);
    vs_emit(f, hb, &S);
    g_stats.vectorized++;
    changed = true;
    ir_order(f);
    oi = 0;
    norig = f->nblocks;
  }
  xfree(inloop);
  return changed;
}

static bool unroll_by(IrFunc *f, uint32_t hb, uint32_t want, uint32_t *newh);

static bool pass_vectorize(IrFunc *f) {
  bool changed = false;
  uint32_t norig = f->nblocks;
  uint8_t *reach = NULL, *inloop = NULL;
  /* A loop vectorised once is not again: the scalar loop the vector loop
     leaves past is entered from the guard alone, and looks like a loop
     there is still a way into. */
  uint8_t *done = xcalloc(norig ? norig : 1, 1);
  uint32_t rcap = 0, lcap = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    if (hb >= norig || done[hb]) continue;
    if (f->ninsts > rcap) { rcap = f->ninsts; reach = xrealloc(reach, rcap); }
    if (f->nblocks > lcap) { lcap = f->nblocks; inloop = xrealloc(inloop, lcap); }
    uint32_t want = 0, nh = hb;
    if (vectorize_one(f, hb, reach, inloop, &want)) {
      done[hb] = 1;
      changed = true;
      ir_order(f);
      oi = 0;
    } else if (want && unroll_by(f, hb, want, &nh)) {
      /* Unrolled to a whole number of vectors, the loop goes on; whatever
         happens to it next, it is not taken again. */
      done[hb] = 1;
      changed = true;
      if (f->ninsts > rcap) { rcap = f->ninsts; reach = xrealloc(reach, rcap); }
      if (f->nblocks > lcap) { lcap = f->nblocks; inloop = xrealloc(inloop, lcap); }
      uint32_t w2 = 0;
      if (vectorize_one(f, nh, reach, inloop, &w2)) ir_order(f);
      oi = 0;
    }
  }
  xfree(reach); xfree(inloop); xfree(done);
  return changed;
}

/* ---- scalar loop unroller ------------------------------------------------

   Serial-dependence loops the vectorizer rightly refuses (an f32 reduction
   cannot be reassociated; a divide chain has no vector form) still pay one
   branch and one exit test per iteration.  Unrolling K iterations into the
   loop keeps evaluation order instruction-for-instruction -- copy k's inputs
   are copy k-1's outputs -- so results stay bit-exact; only the test count
   changes, which is why the first rung takes exactly the loops whose trip
   count is a compile-time constant divisible by K: no guard, no remainder
   loop, no new blocks.  Runs after the vectorizer so it keeps first pick;
   a loop it versioned has two forward preds and is declined here for free.

   The 2026-08-17 sweep (K in {2,4,8} x MAX_BODY in {24,40,64}) priced the
   defaults.  K=4 takes the whole measured win: vecbench/f32red -25.0%
   icount and -11.3% cycles (min-of-15 interleaved); gobench divmod/div64
   -15%/-14% icount but only -1.8% cycles each -- divide-latency-bound, the
   removed compare-and-branch was issuing in free slots.  K=8 added nothing
   past the noise floor anywhere (f32red flat, div rows +0.2-0.5%) while
   doubling growth; its one unique catch, asbench/div64 (trip 49,999,999 =
   7 x 7,142,857, so only K'=7 fires), was -13.2% icount for -0.3% cycles.
   MAX_BODY was inert from 24 to 64: every hot body is <= 24, everything
   between is cold.  Canaries gobench/typeswitch and rsbench/vecgrow flat.
   Beware maphash as a sweep row: TinyGo's map seed makes its icount vary
   run to run. */
#define UNROLL_K          4
#define UNROLL_MAX_BODY   40
#define UNROLL_MAX_BODY_FULL 160
#define UNROLL_MAX_PARAMS 8

static pthread_once_t g_unr_once = PTHREAD_ONCE_INIT;
static struct {
  bool off, dbg, noguard, nocanon;
  long only, upto;
  uint32_t k, max_body, max_params;
} g_unr;

static void unr_init(void) {
  g_unr.off = warmc_getenv("WARMC_NO_UNROLL") != NULL;
  g_unr.noguard = warmc_getenv("WARMC_NO_UNROLL_GUARD") != NULL;
  g_unr.nocanon = warmc_getenv("WARMC_NO_UNROLL_ADDRCANON") != NULL;
  g_unr.dbg = warmc_getenv("WARMC_UNROLL_DEBUG") != NULL;
  const char *e = warmc_getenv("WARMC_UNROLL_K");
  g_unr.k = e ? (uint32_t)strtoul(e, NULL, 0) : UNROLL_K;
  if (g_unr.k < 2 || g_unr.k > 16) g_unr.k = UNROLL_K;
  e = warmc_getenv("WARMC_UNROLL_MAX_BODY");
  g_unr.max_body = e ? (uint32_t)strtoul(e, NULL, 0) : UNROLL_MAX_BODY;
  e = warmc_getenv("WARMC_UNROLL_MAX_PARAMS");
  g_unr.max_params = e ? (uint32_t)strtoul(e, NULL, 0) : UNROLL_MAX_PARAMS;
  if (g_unr.max_params > 16) g_unr.max_params = 16;
  e = warmc_getenv("WARMC_UNROLL_ONLY");
  g_unr.only = e ? atol(e) : -1;
  e = warmc_getenv("WARMC_UNROLL_UPTO");
  g_unr.upto = e ? atol(e) : -1;
}

static int64_t unr_sext(uint64_t v, uint32_t bits) {
  return bits == 64 ? (int64_t)v : (int64_t)(int32_t)(uint32_t)v;
}

static bool unr_rel_holds(IrOp rel, uint64_t a, uint64_t b, uint32_t bits) {
  int64_t sa = unr_sext(a, bits), sb = unr_sext(b, bits);
  switch (rel) {
    case I_NE:  return a != b;
    case I_LTU: return a < b;    case I_LEU: return a <= b;
    case I_GTU: return a > b;    case I_GEU: return a >= b;
    case I_LTS: return sa < sb;  case I_LES: return sa <= sb;
    case I_GTS: return sa > sb;  case I_GES: return sa >= sb;
    default:    return false;
  }
}

/* Body-execution count of a counted loop whose start, bound and step are
   constants, or false when the closed form does not hold (a wrapping
   counter, an unreachable NE bound).  "top" is a while-loop testing the
   param before the body; otherwise the test sits after the increment.
   All arithmetic is exact: distances live in uint64 (two's-complement
   subtraction of sign-extended ends is the true distance once the region
   check has ordered them), and the final counter value is bounded against
   the comparison domain instead of being materialized. */
static bool unr_trip_count(IrOp rel, bool top, bool down, uint32_t bits,
                           uint64_t step, uint64_t init, uint64_t bnd,
                           uint64_t *out) {
  uint64_t mask = bits == 64 ? ~0ull : (1ull << bits) - 1;
  init &= mask; bnd &= mask; step &= mask;
  if (!step) return false;

  if (rel == I_NE) {
    uint64_t d = (down ? init - bnd : bnd - init) & mask;
    if (d == 0) return false;        /* top: dead loop; bottom: full wrap */
    if (d % step) return false;      /* bound never hit: modular spin */
    *out = d / step;
    return true;
  }

  bool sgn = rel == I_LTS || rel == I_LES || rel == I_GTS || rel == I_GES;
  bool incl = rel == I_LEU || rel == I_LES || rel == I_GEU || rel == I_GES;

  /* Distances and headroom are exact in uint64 once both ends are extended
     the way the comparison reads them: two's-complement subtraction of the
     ordered ends is the true magnitude. */
  uint64_t init_x = sgn ? (uint64_t)unr_sext(init, bits) : init;
  uint64_t bnd_x = sgn ? (uint64_t)unr_sext(bnd, bits) : bnd;
  uint64_t dommax_x = sgn ? (uint64_t)unr_sext(mask >> 1, bits) : mask;
  uint64_t dommin_x = sgn ? (uint64_t)unr_sext((mask >> 1) + 1, bits) : 0;
  uint64_t room = down ? init_x - dommin_x : dommax_x - init_x;

  /* The first tested value must sit inside the continue region (a top test
     that fails runs nothing; a bottom loop runs once) -- both are below any
     useful K, so declining keeps the formulas clean. */
  if (top) {
    if (!unr_rel_holds(rel, init, bnd, bits)) return false;
  } else {
    if (step > room) return false;   /* first increment already wraps */
    uint64_t first = (down ? init - step : init + step) & mask;
    if (!unr_rel_holds(rel, first, bnd, bits)) return false;
  }

  uint64_t d = down ? init_x - bnd_x : bnd_x - init_x;
  uint64_t t = incl ? d / step + 1 : d / step + (d % step != 0);
  if (!t) return false;

  /* The value after the final increment is computed and tested; it must not
     wrap the comparison domain or the closed form lies. */
  if (t > room / step) return false;
  *out = t;
  return true;
}

static bool unr_in_cycle(IrFunc *f, const uint32_t *cyc, uint32_t ncyc, Ref r) {
  uint32_t blk = f->insts[r].block;
  for (uint32_t i = 0; i < ncyc; i++)
    if (cyc[i] == blk) return true;
  return false;
}

/* fixmap[r], for an original r a fired bottom-tested loop defines, is the
   value code after that loop must read instead (the last copy's clone, or
   the last copy's seed for a param); cycleid names each fired loop's blocks
   so uses inside the defining loop itself stay untouched.  One walk at pass
   end applies the lot -- doing it per fired loop would rescan the function
   once per loop. */
/* want, if not 0, is the factor the vectoriser asks for (see
   v_unroll_for): that one only, and in the guarded form a step that is not
   a power of two too.  *newh, if asked for, is the loop that holds the
   copies: hb itself, or the guarded form's new one. */
static bool unroll_one(IrFunc *f, uint32_t hb, Ref *fixmap, uint32_t *cycleid,
                       uint32_t loopord, uint32_t ninsts0, uint32_t want,
                       uint32_t *newh) {
#define UREJ(w, ctr) do { g_stats.ctr++; if (g_unr.dbg) \
    fprintf(stderr, "unroll %s b%u reject: %s\n", \
            f->m->funcs[f->func_index].sym, hb, w); return false; } while (0)
  Block *h = &f->blocks[hb];

  uint32_t pre = REF_NONE, latch = REF_NONE, nfwd = 0, nback = 0;
  for (uint32_t p = 0; p < h->npreds; p++) {
    uint32_t pb = h->preds[p];
    if (f->blocks[pb].rpo >= h->rpo) { nback++; latch = pb; }
    else { nfwd++; pre = pb; }
  }
  if (nback != 1) return false;
  if (!h->nparams) return false;
  if (nfwd != 1) UREJ("more than one way into the loop", unr_rej_shape);
  if (h->is_pad) UREJ("header is a landing pad", unr_rej_shape);
  if (h->nparams > g_unr.max_params) UREJ("too many carried values", unr_rej_body);

  /* Stage 1 cycles: the self-loop, the rotated two-block loop (test after
     the body, in the latch), and the top-tested two-block loop the rotate
     pass leaves alone when the body touches no memory. */
  uint32_t cyc[2], ncyc;
  uint32_t test_b;
  const char *shape;
  Block *lb = &f->blocks[latch];
  if (latch == hb) {
    if (h->term != T_BRIF || h->nsuccs != 2)
      UREJ("self-loop does not end in a two-way branch", unr_rej_shape);
    cyc[0] = hb; ncyc = 1; test_b = hb; shape = "self";
  } else {
    if (lb->npreds != 1 || lb->preds[0] != hb)
      UREJ("body block has a side entrance", unr_rej_shape);
    if (lb->nparams) UREJ("body block takes parameters", unr_rej_shape);
    if (lb->is_pad) UREJ("body block is a landing pad", unr_rej_shape);
    cyc[0] = hb; cyc[1] = latch; ncyc = 2;
    if (h->term == T_JUMP && h->succs[0].target == latch &&
        lb->term == T_BRIF && lb->nsuccs == 2) {
      test_b = latch; shape = "bottom";
    } else if (h->term == T_BRIF && h->nsuccs == 2 &&
               lb->term == T_JUMP && lb->succs[0].target == hb &&
               (h->succs[0].target == latch) != (h->succs[1].target == latch)) {
      test_b = hb; shape = "top";
    } else if (h->term == T_JUMP) {
      UREJ("loop body spans more than two blocks", unr_rej_shape);
    } else {
      UREJ("loop is not a simple cycle", unr_rej_shape);
    }
  }
  for (uint32_t i = 0; i < ncyc; i++) {
    Block *b = &f->blocks[cyc[i]];
    for (uint32_t s = 0; s < b->nsuccs; s++)
      if (b->succs[s].eh) UREJ("loop has exception edges", unr_rej_shape);
  }

  Block *tb = &f->blocks[test_b];
  bool taken_continues;
  {
    uint32_t on = test_b == hb && ncyc == 2 ? latch : hb;
    uint32_t t0 = tb->succs[0].target, t1 = tb->succs[1].target;
    if (t0 == on && t1 != on) taken_continues = true;
    else if (t1 == on && t0 != on) taken_continues = false;
    else UREJ("both arms re-enter the loop", unr_rej_shape);
  }
  Edge *back = test_b == latch ? &f->blocks[latch].succs[taken_continues ? 0 : 1]
                               : &f->blocks[latch].succs[0];
  Edge *pe = iv_edge(f, pre, hb);
  if (!pe || pe->nargs != h->nparams || back->nargs != h->nparams)
    UREJ("edge arity", unr_rej_shape);

  uint32_t nbody = 0;
  for (uint32_t i = 0; i < ncyc; i++) nbody += f->blocks[cyc[i]].ninsts;
  /* A body past the limit is still taken when the trip is a constant the
     unroll factor equals: the copies are then the whole loop, run once,
     and the test folds away -- the inner walks nbody's bodies make once
     their outer walk is laid out. */
  bool big = nbody > g_unr.max_body;
  if (nbody > UNROLL_MAX_BODY_FULL) UREJ("body too large", unr_rej_body);
  for (uint32_t i = 0; i < ncyc; i++) {
    Block *b = &f->blocks[cyc[i]];
    for (uint32_t j = 0; j < b->ninsts; j++) {
      uint16_t op = f->insts[b->insts[j]].op;
      if (op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF ||
          op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
          op == I_RETURN_CALL_REF || op == I_RESULT ||
          op == I_THROW || op == I_THROW_REF)
        UREJ("body makes a call", unr_rej_body);
      if (op == I_MEM_GROW || op == I_TABLE_GROW)
        UREJ("body grows state", unr_rej_body);
      if (op >= I_VLOAD && op <= I_VDOTI7ADD)
        UREJ("vector body", unr_rej_body);
      if (f->insts[b->insts[j]].nargs > 8)
        UREJ("body op too wide", unr_rej_body);
    }
  }

  uint32_t np = h->nparams;
  Ref hparam[16], entry_args[16], back_args[16];
  for (uint32_t p = 0; p < np; p++) {
    hparam[p] = h->params[p];
    entry_args[p] = pe->args[p];
    back_args[p] = back->args[p];
  }
  Ref iventry[16];
  uint64_t ivstep[16];
  bool ivadd[16], isiv[16];
  uint32_t niv = 0;
  for (uint32_t p = 0; p < np; p++) {
    Ref nx = back_args[p];
    Inst *n = &f->insts[nx];
    uint64_t stp;
    isiv[p] = false;
    if ((n->op == I_ADD || n->op == I_SUB) && n->nargs == 2 &&
        unr_in_cycle(f, cyc, ncyc, nx) && ir_args(f, nx)[0] == hparam[p] &&
        is_const(f, ir_args(f, nx)[1], &stp) && stp != 0) {
      iventry[p] = entry_args[p];
      ivstep[p] = stp;
      ivadd[p] = n->op == I_ADD;
      isiv[p] = true;
      niv++;
    }
  }
  if (!niv) UREJ("no induction variable", unr_rej_exit);

  /* The exit test, normalized to a "continue if" relation on the counter.
     A bottom test reads the counter's next value; a top test reads the
     param itself.  This is what refuses data-dependent scan loops: their
     tested value is a load, not the counter. */
  bool top = test_b != latch;
  Ref ivside, bound;
  IrOp rel;
  {
    Ref cond = tb->term_arg;
    bool cont = taken_continues;
    if (f->insts[cond].op == I_EQZ && f->insts[cond].nargs == 1) {
      cond = ir_args(f, cond)[0];
      cont = !cont;
    }
    bool is_counter = false;
    for (uint32_t p = 0; p < np && !is_counter; p++)
      if (isiv[p] && (top ? hparam[p] : back_args[p]) == cond) is_counter = true;
    if (is_counter) {
      rel = cont ? I_NE : I_EQ;
      ivside = cond;
      bound = REF_NONE;
    } else {
      rel = v_continue_rel(f, cond, cont, &ivside, &bound);
    }
  }
  if (rel == I_NOP) UREJ("exit test", unr_rej_exit);
  uint32_t tp = 0xffffffffu;
  for (uint32_t p = 0; p < np; p++)
    if (isiv[p] && (top ? hparam[p] : back_args[p]) == ivside) { tp = p; break; }
  if (tp == 0xffffffffu && bound != REF_NONE) {
    rel = v_mirror(rel);
    Ref t = ivside; ivside = bound; bound = t;
    for (uint32_t p = 0; p < np; p++)
      if (isiv[p] && (top ? hparam[p] : back_args[p]) == ivside) { tp = p; break; }
  }
  if (tp == 0xffffffffu || rel == I_NOP)
    UREJ("exit test is not the counter", unr_rej_exit);
  if (rel != I_NE && rel != I_LTU && rel != I_LTS && rel != I_LEU &&
      rel != I_LES && rel != I_GTU && rel != I_GTS && rel != I_GEU &&
      rel != I_GES)
    UREJ("exit relation", unr_rej_exit);
  ValType ct = (ValType)f->insts[hparam[tp]].type;
  if (ct != VT_I32 && ct != VT_I64) UREJ("counter type", unr_rej_exit);
  uint32_t cbits = ct == VT_I64 ? 64 : 32;
  uint64_t cmask = cbits == 64 ? ~0ull : (1ull << cbits) - 1;

  uint64_t K = ivstep[tp] & cmask;
  bool down = !ivadd[tp];
  if (K & (1ull << (cbits - 1))) {   /* a negative step runs the other way */
    down = !down;
    K = (0 - K) & cmask;
  }
  bool gform = rel == I_GTU || rel == I_GTS || rel == I_GEU || rel == I_GES;
  if (down && !gform && rel != I_NE) UREJ("counter runs down", unr_rej_exit);
  if (!down && gform) UREJ("counter runs up against a downward test", unr_rej_exit);

  /* First rung: a constant trip divisible by some factor unrolls in place,
     no guard and no remainder loop.  Cloning needs no shift arithmetic, so
     any divisor will do -- a producer-peeled loop of 49,999,999 iterations
     unrolls by 7.  Everything that falls out of this rung -- a dynamic
     bound or start, an indivisible or wrap-risky constant trip -- falls
     through to the guarded form, whose runtime ok simply bypasses to the
     untouched original loop when the closed form does not hold. */
  uint64_t c_init, c_bound = 0, trip = 0;
  bool cst = is_const(f, iventry[tp], &c_init) &&
             (bound == REF_NONE || is_const(f, bound, &c_bound));
  uint32_t uk = 0;
  bool guarded = false;
  if (cst && unr_trip_count(rel, top, down, cbits, K, c_init, c_bound, &trip))
    for (uint32_t k = want ? want : g_unr.k; k >= 2; k--) {
      if (trip >= k && trip % k == 0) { uk = k; break; }
      if (want) break;
    }
  if (big && !(uk && trip == uk)) UREJ("body too large", unr_rej_body);
  if (!uk) {
    /* Guarded form: version the loop behind a runtime trip count (the
       vectorizer's arithmetic with U := the unroll factor) and keep the
       original loop as the remainder, which always runs the last 1..K
       iterations -- so exits, escapes, and every downstream use stay
       untouched.  Top and bottom tests share the arithmetic: counting
       total body executions gives the same closed form either way, the
       empty-region terms in ok already bypass a zero-trip top loop, and
       the re-entered original re-tests before every remainder iteration. */
    guarded = true;
    if (g_unr.noguard) return false;
    if ((K & (K - 1)) && !want) UREJ("dynamic step is not a power of two", unr_rej_trip);
    if (K >= (1ull << (cbits - 2))) UREJ("dynamic step too large", unr_rej_trip);
    if (bound != REF_NONE && unr_in_cycle(f, cyc, ncyc, bound) &&
        f->insts[bound].op != I_CONST)
      UREJ("bound is not loop-invariant", unr_rej_trip);
    uk = 2;
    while (uk * 2 <= g_unr.k) uk *= 2;   /* the guard divides by shifting */
    if (want) uk = want;
    if (cst && trip && trip <= uk)
      UREJ("trip too short for the guard", unr_rej_trip);
  }

  if (guarded) {
    /* Version the loop: pre -> uguard -> (ok) ubody x cnt -> hb, with the
       bypass and the remainder both re-entering the untouched original.
       Block creation can move f->blocks, so no Block pointer from above
       survives past here; edge storage does (only the array of Blocks is
       reallocated).  Cloned instructions may reference I_CONSTs defined
       inside the original loop from a block that does not dominate them --
       the same sharing rotate_one relies on: constants are position-free. */
    uint32_t uguard = ir_new_block(f);
    uint32_t ubody = ir_new_block(f);
    f->blocks[uguard].reachable = true;
    f->blocks[ubody].reachable = true;

    Ref ok;
    Ref cnt = guard_trip_count(f, uguard, ct, rel, K, down, iventry[tp],
                               bound,
                               bound != REF_NONE &&
                                   unr_in_cycle(f, cyc, ncyc, bound),
                               uk, false, NULL, &ok);

    Ref bpar[17];
    for (uint32_t p = 0; p < np; p++)
      bpar[p] = ir_add_param(f, ubody, (ValType)f->insts[hparam[p]].type);
    Ref cpar = ir_add_param(f, ubody, ct);

    Ref *seq = xmalloc((size_t)nbody * sizeof *seq);
    uint32_t nseq = 0;
    for (uint32_t i = 0; i < ncyc; i++) {
      Block *b = &f->blocks[cyc[i]];
      for (uint32_t j = 0; j < b->ninsts; j++) seq[nseq++] = b->insts[j];
    }
    uint32_t nold = f->ninsts;
    Ref *vmap = xmalloc((size_t)nold * sizeof *vmap);
    for (uint32_t i = 0; i < nold; i++) vmap[i] = i;
    Ref cur[16];
    for (uint32_t p = 0; p < np; p++) cur[p] = bpar[p];
    for (uint32_t k = 0; k < uk; k++) {
      for (uint32_t p = 0; p < np; p++) vmap[hparam[p]] = cur[p];
      for (uint32_t i = 0; i < nseq; i++) {
        Ref r = seq[i];
        Inst tmpl = f->insts[r];
        if (tmpl.op == I_NOP) continue;
        if (tmpl.op == I_CONST) {
          /* Unlike the in-place rung, ubody runs BEFORE the original loop:
             a shared in-cycle constant would be read before its definition
             ever executes (constants get their register at the def, they
             are not position-free -- the same reason clone_bound exists).
             Clone it once; out-of-cycle values dominate pre and are safe. */
          if (k == 0)
            vmap[r] = ir_emit(f, ubody, I_CONST, (ValType)tmpl.type, NULL, 0,
                              tmpl.imm, tmpl.imm2);
          continue;
        }
        Ref tmp[8];
        Ref *a = ir_args(f, r);
        for (uint32_t x = 0; x < tmpl.nargs; x++) tmp[x] = vmap[a[x]];
        vmap[r] = ir_emit(f, ubody, (IrOp)tmpl.op, (ValType)tmpl.type,
                          tmp, tmpl.nargs, tmpl.imm, tmpl.imm2);
      }
      for (uint32_t p = 0; p < np; p++) cur[p] = vmap[back_args[p]];
    }
    Ref bone = ir_emit(f, ubody, I_CONST, ct, NULL, 0, 1, 0);
    Ref bzero = ir_emit(f, ubody, I_CONST, ct, NULL, 0, 0, 0);
    Ref sub_a[2] = { cpar, bone };
    Ref ncnt = ir_emit(f, ubody, I_SUB, ct, sub_a, 2, 0, 0);
    Ref ne_a[2] = { ncnt, bzero };
    Ref more = ir_emit(f, ubody, I_NE, VT_I32, ne_a, 2, 0, 0);
    xfree(seq);
    xfree(vmap);

    Block *pb = &f->blocks[pre];
    for (uint32_t s = 0; s < pb->nsuccs; s++)
      if (pb->succs[s].target == hb) {
        pb->succs[s].target = uguard;
        pb->succs[s].nargs = 0;
      }
    Ref ga[17];
    for (uint32_t p = 0; p < np; p++) ga[p] = entry_args[p];
    ga[np] = cnt;
    f->blocks[uguard].term = T_BRIF;
    f->blocks[uguard].term_arg = ok;
    ir_add_edge(f, uguard, ubody, ga, np + 1);
    ir_add_edge(f, uguard, hb, entry_args, np);
    for (uint32_t p = 0; p < np; p++) ga[p] = cur[p];
    ga[np] = ncnt;
    f->blocks[ubody].term = T_BRIF;
    f->blocks[ubody].term_arg = more;
    ir_add_edge(f, ubody, ubody, ga, np + 1);
    ir_add_edge(f, ubody, hb, cur, np);

    if (g_unr.dbg)
      fprintf(stderr, "unroll %s b%u: guarded %s K=%u body=%u\n",
              f->m->funcs[f->func_index].sym, hb, shape, uk, nbody);
    g_stats.unr_guarded++;
    if (newh) *newh = ubody;
    return true;
  }

  /* Values the loop defines and code after it reads.  A bottom-tested loop
     exits from the block the clones land in, so those uses are repaired
     after cloning: the last copy's value dominates everything the exit
     reaches.  A top-tested loop re-runs its header with the final params
     before leaving, so header escapes are already correct untouched -- and
     a latch value could not have dominated an outside use anyway (the
     zero-trip bypass exists); reject that as a safety net. */
  if (top) {
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      if (bi == cyc[0] || (ncyc > 1 && bi == cyc[1])) continue;
      Block *b = &f->blocks[bi];
#define UESC(r) do { Ref rr_ = (r); \
      if (f->insts[rr_].op != I_CONST && f->insts[rr_].block == latch) \
        UREJ("loop value escapes", unr_rej_escape); } while (0)
      for (uint32_t j = 0; j < b->ninsts; j++) {
        Ref r = b->insts[j];
        Ref *a = ir_args(f, r);
        for (uint32_t x = 0; x < f->insts[r].nargs; x++) UESC(a[x]);
      }
      if (b->term == T_BRIF || b->term == T_BRTABLE) UESC(b->term_arg);
      for (uint32_t x = 0; x < b->nret; x++) UESC(b->ret_args[x]);
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        Edge *e = &b->succs[s];
        for (uint32_t x = 0; x < e->nargs; x++) UESC(e->args[x]);
      }
#undef UESC
    }
  }

  /* The transform: append uk-1 clones of the one-iteration sequence to the
     back-edge source block, then hand the loop's edges the last copy's
     values.  vmap[r] is "this copy's value for original r": identity for
     everything outside the cycle, the previous copy's back-edge values for
     the header params, this copy's clones for the cycle's instructions.
     Cross-iteration flow only exists through params, so a sequence in
     chain order is def-before-use inside every copy; constants stay
     shared, exactly as rotate_one clones a header.  Copy k's compare goes
     dead for k < uk-1 and dce sweeps it; the surviving test reads the last
     copy's counter, so the loop now tests once per uk iterations. */
  Ref *seq = xmalloc((size_t)nbody * sizeof *seq);
  uint32_t nseq = 0;
  for (uint32_t i = 0; i < ncyc; i++) {
    Block *b = &f->blocks[cyc[i]];
    for (uint32_t j = 0; j < b->ninsts; j++) seq[nseq++] = b->insts[j];
  }
  uint32_t nold = f->ninsts;
  Ref *vmap = xmalloc((size_t)nold * sizeof *vmap);
  for (uint32_t i = 0; i < nold; i++) vmap[i] = i;
  Ref cur[16], lastseed[16];
  for (uint32_t p = 0; p < np; p++) {
    cur[p] = back_args[p];
    lastseed[p] = hparam[p];
  }
  for (uint32_t k = 1; k < uk; k++) {
    for (uint32_t p = 0; p < np; p++) {
      vmap[hparam[p]] = cur[p];
      if (k == uk - 1) lastseed[p] = cur[p];
    }
    for (uint32_t i = 0; i < nseq; i++) {
      Ref r = seq[i];
      Inst tmpl = f->insts[r];       /* by value: ir_emit may move f->insts */
      if (tmpl.op == I_CONST || tmpl.op == I_NOP) continue;
      Ref tmp[8];
      Ref *a = ir_args(f, r);
      for (uint32_t x = 0; x < tmpl.nargs; x++) tmp[x] = vmap[a[x]];
      vmap[r] = ir_emit(f, latch, (IrOp)tmpl.op, (ValType)tmpl.type,
                        tmp, tmpl.nargs, tmpl.imm, tmpl.imm2);
    }
    for (uint32_t p = 0; p < np; p++) cur[p] = vmap[back_args[p]];
  }
  for (uint32_t p = 0; p < np; p++) back->args[p] = cur[p];
  for (uint32_t i = 0; i < ncyc; i++) cycleid[cyc[i]] = loopord;
  if (!top) {
    Block *tb2 = &f->blocks[test_b];
    if (f->insts[tb2->term_arg].op != I_CONST && tb2->term_arg < nold)
      tb2->term_arg = vmap[tb2->term_arg];
    Edge *ex = &tb2->succs[taken_continues ? 1 : 0];
    for (uint32_t x = 0; x < ex->nargs; x++)
      if (ex->args[x] < nold) ex->args[x] = vmap[ex->args[x]];

    /* Downstream, a cycle instruction's value is the last copy's clone; a
       param's is what the last copy was seeded with (the state entering
       the final original iteration).  Both live in the latch or above it,
       and the exit only leaves the latch, so dominance is preserved. */
    for (uint32_t i = 0; i < nseq; i++) {
      Ref r = seq[i];
      if (f->insts[r].op == I_CONST || f->insts[r].op == I_NOP) continue;
      if (r < ninsts0) fixmap[r] = vmap[r];
    }
    for (uint32_t p = 0; p < np; p++)
      if (hparam[p] < ninsts0) fixmap[hparam[p]] = lastseed[p];
  }
  xfree(seq);
  xfree(vmap);

  if (g_unr.dbg)
    fprintf(stderr, "unroll %s b%u: %s trip=%llu K=%u body=%u\n",
            f->m->funcs[f->func_index].sym, hb, shape,
            (unsigned long long)trip, uk, nbody);
  g_stats.unrolled++;
  if (newh) *newh = hb;
  return true;
#undef UREJ
}

/* --- post-unroll address canonicalisation --------------------------------
   The unroller's clones each re-derive their address through the stepped
   induction chain: mem_addr(add(base, add(i, C))).  Walking the constants
   out and re-rooting every clone on an EXISTING add(base, i) gives the
   clones one shared index and adjacent offsets -- which the emitter then
   pairs into ldp/stp.  The i32 reassociation inside the walk is exact mod
   arithmetic; moving C into the access offset is NOT: the original wraps
   the index mod 2^32 where the offset add is 64-bit and wrap-free.  So a
   fold needs a WITNESS: an access off the same root at an offset <= this
   one, sitting in the root's own block after its def (straight-line order,
   so every instance of the folded access is preceded by a witness on the
   same root value), dominating every user of the folded address.  The
   runtime caps wasm32 memories at 65535 pages (rt.c make_memory), so if
   the folded form could diverge -- index within 64KiB of 2^32 -- the
   witness's own address already exceeds any reachable memory size and
   traps first, identically in both forms; memory64 bounds checks are
   overflow-checked, same argument.  Positive constants only: a negative
   step would need the mirrored proof and no measured row wants it. */

static bool uac_const(IrFunc *f, Ref r, ValType t, uint64_t *c) {
  Inst *in = &f->insts[r];
  if (in->op != I_CONST || (ValType)in->type != t) return false;
  uint64_t v = in->imm;
  if (t == VT_I32) v &= 0xffffffffu;
  if (v == 0 || v > 4096) return false;
  *c = v;
  return true;
}

static bool uac_split(IrFunc *f, Ref r, ValType t, Ref *rest, uint64_t *c) {
  Inst *in = &f->insts[r];
  if (in->op != I_ADD || in->nargs != 2 || (ValType)in->type != t) return false;
  Ref *a = ir_args(f, r);
  if (uac_const(f, a[1], t, c)) { *rest = a[0]; return true; }
  if (uac_const(f, a[0], t, c)) { *rest = a[1]; return true; }
  return false;
}

static uint32_t uac_pos(IrFunc *f, uint32_t bi, Ref v) {
  Block *b = &f->blocks[bi];
  for (uint32_t i = 0; i < b->ninsts; i++)
    if (b->insts[i] == v) return i;
  return UINT32_MAX;
}

/* Every 2-operand add, hashed by its exact operand pair, first-in-order
   wins.  A per-candidate walk of the base's use list here was quadratic on
   functions whose unrolled loops share one base value: the list is O(loops
   x K) long and every clone consulted it. */
typedef struct {
  uint64_t *key; /* (a0 << 32) | a1, 0 = empty */
  Ref *add;
  uint32_t mask;
} UacAdds;

static UacAdds uac_adds_build(IrFunc *f) {
  UacAdds m;
  uint32_t cap = 64;
  while (cap < live_insts(f) * 2) cap <<= 1;
  m.key = xcalloc(cap, sizeof(uint64_t));
  m.add = xmalloc(cap * sizeof(Ref));
  m.mask = cap - 1;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op != I_ADD || in->nargs != 2) continue;
      Ref *a = ir_args(f, v);
      uint64_t key = ((uint64_t)(a[0] + 1) << 32) | (uint32_t)(a[1] + 1);
      uint32_t h = (uint32_t)((key ^ (key >> 29)) * 0x9e3779b97f4a7c15ull >> 40) & m.mask;
      while (m.key[h] && m.key[h] != key) h = (h + 1) & m.mask;
      if (!m.key[h]) { m.key[h] = key; m.add[h] = v; }
    }
  }
  return m;
}

static Ref uac_adds_get(const UacAdds *m, Ref p, Ref r) {
  uint64_t key = ((uint64_t)(p + 1) << 32) | (uint32_t)(r + 1);
  uint32_t h = (uint32_t)((key ^ (key >> 29)) * 0x9e3779b97f4a7c15ull >> 40) & m->mask;
  while (m->key[h]) {
    if (m->key[h] == key) return m->add[h];
    h = (h + 1) & m->mask;
  }
  return REF_NONE;
}

static Ref uac_find_add(IrFunc *f, const UacAdds *m, Ref P, Ref R, ValType t,
                        uint32_t vbi, uint32_t vidx) {
  for (int o = 0; o < 2; o++) {
    Ref w = o ? uac_adds_get(m, R, P) : uac_adds_get(m, P, R);
    if (w == REF_NONE) continue;
    Inst *wi = &f->insts[w];
    if ((ValType)wi->type != t) continue;
    if (wi->block == vbi) {
      if (uac_pos(f, vbi, w) < vidx) return w;
      continue;
    }
    if (ir_dominates_checked(f, wi->block, vbi)) return w;
  }
  return REF_NONE;
}

static bool uac_dominates_access(IrFunc *f, Ref d, Ref u) {
  uint32_t db = f->insts[d].block, ub = f->insts[u].block;
  if (db == ub) return uac_pos(f, db, d) < uac_pos(f, ub, u);
  return ir_dominates_checked(f, db, ub);
}

static bool uac_witness(IrFunc *f, const UseIdx *ui, Ref root, Ref v,
                        uint64_t total) {
  uint32_t rootb = f->insts[root].block;
  uint32_t rootpos = uac_pos(f, rootb, root); /* UINT32_MAX for params */
  for (uint32_t k = ui->first[root]; k != UINT32_MAX; k = ui->next[k]) {
    Ref w = ui->user[k];
    if (w == v || !ui->arg0[k]) continue;
    Inst *wi = &f->insts[w];
    if (wi->op != I_MEM_ADDR || wi->nargs != 1) continue;
    if (wi->imm > total || total - wi->imm > 65536) continue;
    for (uint32_t k2 = ui->first[w]; k2 != UINT32_MAX; k2 = ui->next[k2]) {
      Ref d = ui->user[k2];
      Inst *di = &f->insts[d];
      if ((di->op != I_LOAD && di->op != I_STORE) || !ui->arg0[k2]) continue;
      if (di->block != rootb) continue;
      if (rootpos != UINT32_MAX && uac_pos(f, rootb, d) < rootpos) continue;
      bool all = true;
      for (uint32_t k3 = ui->first[v]; k3 != UINT32_MAX && all;
           k3 = ui->next[k3]) {
        Ref u = ui->user[k3];
        Inst *un = &f->insts[u];
        if ((un->op != I_LOAD && un->op != I_STORE) || !ui->arg0[k3] ||
            MEMF_MEM(un->imm2) != MEMF_MEM(di->imm2) ||
            !uac_dominates_access(f, d, u))
          all = false;
      }
      if (all) return true;
    }
  }
  return false;
}

/* Ops a load may move up past when regrouping (mirrors the emitter's
   pairing window): pure, non-trapping, no memory writes.  Other loads are
   fine too -- load/load reorder with no store between is value-preserving
   and either order traps identically on an unchanged state. */
static bool uac_pure(uint16_t op) {
  switch (op) {
    case I_CONST: case I_ADD: case I_SUB: case I_MUL: case I_MULHU: case I_MULHS:
    case I_AND: case I_OR: case I_XOR: case I_SHL: case I_SHRS: case I_SHRU:
    case I_ROTL: case I_ROTR: case I_CLZ: case I_CTZ: case I_POPCNT:
    case I_BSWAP:
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_FADD: case I_FSUB: case I_FMUL: case I_FDIV: case I_FMIN:
    case I_FMAX: case I_FCOPYSIGN: case I_FABS: case I_FNEG: case I_FSQRT:
    case I_FCEIL: case I_FFLOOR: case I_FTRUNC: case I_FNEAREST:
    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE:
    case I_WRAP: case I_EXTENDS: case I_EXTENDU:
    case I_TRUNC_SATS: case I_TRUNC_SATU:
    case I_CONVERTS: case I_CONVERTU: case I_DEMOTE: case I_PROMOTE:
    case I_REINTERPRET: case I_EXT8S: case I_EXT16S: case I_EXT32S:
    case I_LOAD: case I_MEM_ADDR:
      return true;
    default: return false;
  }
}

/* Move the later of two same-root loads (with its address) up next to the
   earlier one, so the emitter sees them adjacent and the allocator has not
   yet rotated an accumulator into the second destination.  A load and its
   folded mem_addr move as a unit. */
static bool uac_regroup(IrFunc *f, Block *b) {
  bool changed = false;
  for (uint32_t i = 0; i < b->ninsts; i++) {
    Ref u0 = b->insts[i];
    Inst *l0 = &f->insts[u0];
    if (l0->op != I_LOAD) continue;
    Ref m0 = ir_args(f, u0)[0];
    if (f->insts[m0].op != I_MEM_ADDR || f->insts[m0].nargs != 1) continue;
    Ref root = ir_args(f, m0)[0];
    /* the unroller's dead clone compares and stepped IV adds still pad the
       gap here (the trailer's dce runs after this pass), so the window is
       wider than the emitter's */
    uint32_t lim = i + 24 < b->ninsts ? i + 24 : b->ninsts;
    /* from i + 1: the op right after u0 is in the gap too, and a load
       moved up past a store there read memory before the store wrote it */
    for (uint32_t j = i + 1; j < lim; j++) {
      Ref u1 = b->insts[j];
      Inst *l1 = &f->insts[u1];
      if (!uac_pure(l1->op)) break;
      if (l1->op != I_LOAD || j == i + 1) continue;
      Ref m1 = ir_args(f, u1)[0];
      if (f->insts[m1].op != I_MEM_ADDR || f->insts[m1].nargs != 1) continue;
      if (ir_args(f, m1)[0] != root) continue;
      if (MEMF_MEM(l1->imm2) != MEMF_MEM(l0->imm2)) continue;
      /* m1 must be movable with u1: it sits between i and j and nothing
         between may depend on u1 or m1 (SSA: uses follow defs, so only
         position matters); m1's own operands (root, consts) precede i. */
      uint32_t mpos = UINT32_MAX;
      for (uint32_t x = i + 1; x < j; x++)
        if (b->insts[x] == m1) { mpos = x; break; }
      /* splice m1 (if inside the gap) and u1 to just after u0 */
      uint32_t at = i + 1;
      if (mpos != UINT32_MAX) {
        for (uint32_t x = mpos; x > at; x--) b->insts[x] = b->insts[x - 1];
        b->insts[at] = m1;
        at++;
      }
      for (uint32_t x = j; x > at; x--) b->insts[x] = b->insts[x - 1];
      b->insts[at] = u1;
      if (g_unr.dbg)
        fprintf(stderr, "regroup f%u: v%u after v%u (gap %u)\n",
                f->func_index, u1, u0, j - i - 1);
      changed = true;
      break;
    }
  }
  return changed;
}

static bool pass_unroll_addr(IrFunc *f) {
  if (g_unr.nocanon) return false;
  bool changed = false;
  UseIdx ui = use_index(f);
  UacAdds adds = uac_adds_build(f);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op != I_MEM_ADDR || in->nargs != 1) continue;
      Ref arg = ir_args(f, v)[0];
      ValType t = (ValType)f->insts[arg].type;
      if (t != VT_I32 && t != VT_I64) continue;
      uint64_t delta = 0, c;
      Ref P = REF_NONE, R = REF_NONE, root = REF_NONE, cur = arg, rest;
      bool pair = false;
      for (int steps = 0; steps < 16; steps++) {
        if (!pair) {
          Inst *ci = &f->insts[cur];
          if (ci->op == I_ADD && ci->nargs == 2 && (ValType)ci->type == t) {
            P = ir_args(f, cur)[0];
            R = ir_args(f, cur)[1];
            pair = true;
            continue;
          }
          root = cur;
          break;
        }
        if (uac_const(f, R, t, &c)) { delta += c; cur = P; pair = false; continue; }
        if (uac_const(f, P, t, &c)) { delta += c; cur = R; pair = false; continue; }
        if (uac_split(f, R, t, &rest, &c)) { delta += c; R = rest; continue; }
        if (uac_split(f, P, t, &rest, &c)) { delta += c; P = rest; continue; }
        root = uac_find_add(f, &adds, P, R, t, bi, i);
        break;
      }
      if (root == REF_NONE || root == arg || delta == 0 || delta > 4096)
        continue;
      if (!uac_witness(f, &ui, root, v, in->imm + delta)) continue;
      ir_args(f, v)[0] = root;
      in->imm += delta;
      g_stats.unr_addr_folded++;
      changed = true;
    }
  }
  use_index_free(&ui);
  xfree(adds.key);
  xfree(adds.add);
  for (uint32_t oi = 0; oi < f->norder; oi++)
    if (uac_regroup(f, &f->blocks[f->order[oi]])) changed = true;
  return changed;
}

/* The number of times a bottom-tested loop runs, when that is a constant:
   a param stepped by a constant from a constant, which the latch's test
   compares with a constant.  0 when it is not. */
static uint64_t loop_trip_bottom(IrFunc *f, uint32_t hb, uint32_t latch, uint32_t pre) {
  uint64_t trip = 0;
  Block *h = &f->blocks[hb], *lb = &f->blocks[latch];
  uint32_t np = h->nparams;
  uint32_t ci = lb->succs[0].target == hb ? 0 : 1;
  Edge *back = &lb->succs[ci];
  Edge *pe = iv_edge(f, pre, hb);
  if (lb->term != T_BRIF || lb->nsuccs != 2 || !pe || pe->nargs != np || back->nargs != np)
    return 0;
  {
    Ref cond = lb->term_arg;
    bool cont = ci == 0;
    if (f->insts[cond].op == I_EQZ && f->insts[cond].nargs == 1) {
      cond = ir_args(f, cond)[0];
      cont = !cont;
    }
    for (uint32_t p = 0; p < np && !trip; p++) {
      Ref nx = back->args[p];
      Inst *n = &f->insts[nx];
      uint64_t stp, c_init, c_bound = 0;
      if ((n->op != I_ADD && n->op != I_SUB) || n->nargs != 2 ||
          ir_args(f, nx)[0] != h->params[p] || !is_const(f, ir_args(f, nx)[1], &stp) ||
          !stp || !is_const(f, pe->args[p], &c_init))
        continue;
      ValType ct = (ValType)n->type;
      if (ct != VT_I32 && ct != VT_I64) continue;
      uint32_t cbits = ct == VT_I64 ? 64 : 32;
      uint64_t cmask = cbits == 64 ? ~0ull : (1ull << cbits) - 1;
      Ref ivside, bound;
      IrOp rel;
      if (cond == nx) { rel = cont ? I_NE : I_EQ; ivside = nx; bound = REF_NONE; }
      else rel = v_continue_rel(f, cond, cont, &ivside, &bound);
      if (rel != I_NOP && ivside != nx && bound == nx) {
        rel = v_mirror(rel);
        Ref tt = ivside; ivside = bound; bound = tt;
      }
      if (rel == I_NOP || ivside != nx) continue;
      if (bound != REF_NONE && !is_const(f, bound, &c_bound)) continue;
      uint64_t K = stp & cmask;
      bool down = n->op == I_SUB;
      if (K & (1ull << (cbits - 1))) { down = !down; K = (0 - K) & cmask; }
      bool gform = rel == I_GTU || rel == I_GTS || rel == I_GEU || rel == I_GES;
      if (down && !gform && rel != I_NE) continue;
      if (!down && gform) continue;
      if (rel == I_EQ) continue;
      uint64_t tc;
      if (unr_trip_count(rel, false, down, cbits, K, c_init & cmask, c_bound & cmask, &tc))
        trip = tc;
    }
  }
  return trip;
}

/* A loop that runs a few times, a constant number of them, with a loop
   inside it -- nbody's walk over its five bodies, whose inner walk over the
   bodies after each one then runs 4, 3, 2 and 1 times -- is laid out that
   many times over.  Each copy's counter is a constant, so the inner loops
   get constant trip counts the unroller then takes, and every address the
   copies make is a constant memory forwarding can follow: the velocities
   the pairs add into stay in registers for the whole step.  Only a loop
   that is itself inside a loop, entered once, bottom-tested and left only
   by its test, and small. */
#define UNEST_TRIP 8
#define UNEST_INSTS 1200

static bool unroll_nest_one(IrFunc *f, uint32_t hb) {
  Block *h = &f->blocks[hb];
  if (!h->reachable || !h->nparams || h->is_pad || h->loop_depth < 2) return false;
  uint32_t pre = UINT32_MAX, latch = UINT32_MAX, nfwd = 0, nback = 0;
  for (uint32_t p = 0; p < h->npreds; p++) {
    uint32_t pb = h->preds[p];
    if (f->blocks[pb].rpo >= h->rpo) { nback++; latch = pb; }
    else { nfwd++; pre = pb; }
  }
  if (nback != 1 || nfwd != 1 || latch == hb) return false;

  uint32_t nb0 = f->nblocks, ni0 = f->ninsts;
  uint8_t *in = xcalloc(nb0, 1);
  uint32_t *stack = xmalloc(nb0 * sizeof(uint32_t)), sp = 0;
  bool ok = true, inner = false;
  uint32_t ninst = 0;
  in[hb] = 1;
  in[latch] = 1;
  stack[sp++] = latch;
  while (sp && ok) {
    uint32_t x = stack[--sp];
    for (uint32_t p = 0; p < f->blocks[x].npreds; p++) {
      uint32_t pb = f->blocks[x].preds[p];
      if (pb == pre || pb == f->order[0]) { ok = false; break; }
      if (!in[pb]) { in[pb] = 1; stack[sp++] = pb; }
    }
  }
  for (uint32_t x = 0; x < nb0 && ok; x++) {
    if (!in[x]) continue;
    Block *b = &f->blocks[x];
    if (b->is_pad) ok = false;
    ninst += b->ninsts;
    /* as the unroller: a call site or a throw is not copied */
    for (uint32_t j = 0; j < b->ninsts && ok; j++) {
      uint16_t op = f->insts[b->insts[j]].op;
      if (is_call_op(op) || op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
          op == I_RETURN_CALL_REF || op == I_RESULT || op == I_THROW || op == I_THROW_REF)
        ok = false;
    }
    for (uint32_t p = 0; p < b->npreds && ok; p++) {
      uint32_t pb = b->preds[p];
      if (x != hb && !in[pb]) ok = false;
      if (x != hb && in[pb] && f->blocks[pb].rpo >= b->rpo) inner = true;
    }
    for (uint32_t s = 0; s < b->nsuccs && ok; s++) {
      if (b->succs[s].eh) ok = false;
      if (!in[b->succs[s].target] && x != latch) ok = false;
    }
  }
  Block *lb = &f->blocks[latch];
  if (!ok || !inner || lb->term != T_BRIF || lb->nsuccs != 2 ||
      (lb->succs[0].target == hb) == (lb->succs[1].target == hb) ||
      in[lb->succs[0].target] == in[lb->succs[1].target]) {
    xfree(in); xfree(stack);
    return false;
  }
  uint32_t ci = lb->succs[0].target == hb ? 0 : 1;
  Edge *back = &lb->succs[ci];
  Edge *pe = iv_edge(f, pre, hb);
  uint32_t np = h->nparams;
  if (!pe || pe->nargs != np || back->nargs != np) { xfree(in); xfree(stack); return false; }

  uint64_t trip = loop_trip_bottom(f, hb, latch, pre);
  if (trip < 2 || trip > UNEST_TRIP || (uint64_t)ninst * trip > UNEST_INSTS) {
    xfree(in); xfree(stack);
    return false;
  }

  if (warmc_getenv("WARMC_UNROLL_DEBUG"))
    fprintf(stderr, "unroll %s b%u: nest trip=%llu body=%u\n",
            f->m->funcs[f->func_index].sym, hb, (unsigned long long)trip, ninst);
  uint32_t *bc = xmalloc((size_t)trip * nb0 * sizeof(uint32_t));
  Ref *mp = xmalloc((size_t)trip * ni0 * sizeof(Ref));
  for (uint32_t x = 0; x < nb0; x++) bc[x] = in[x] ? x : UINT32_MAX;
  for (uint32_t v = 0; v < ni0; v++) mp[v] = v;
  for (uint64_t k = 1; k < trip; k++)
    clone_blocks(f, in, nb0, ni0, &bc[k * nb0], &mp[k * ni0]);

  /* each copy's test goes on to the next copy, the last one's out */
  for (uint64_t k = 0; k < trip; k++) {
    uint32_t L = bc[k * nb0 + latch];
    Block *b = &f->blocks[L];
    uint32_t hk = bc[k * nb0 + hb];
    uint32_t c = b->succs[0].target == hk ? 0 : 1;
    Edge keep = b->succs[k + 1 < trip ? c : 1 - c];
    if (k + 1 < trip) keep.target = bc[(k + 1) * nb0 + hb];
    b->succs[0] = keep;
    b->nsuccs = 1;
    b->term = T_JUMP;
    b->term_arg = REF_NONE;
  }

  /* what the loop made and the code after it reads is the last copy's */
  const Ref *last = &mp[(trip - 1) * ni0];
#define UN_MAP(r) do { Ref r_ = (r); if (r_ != REF_NONE && r_ < ni0 &&            \
      f->insts[r_].block < nb0 && in[f->insts[r_].block] && last[r_] != REF_NONE) \
      (r) = last[r_]; } while (0)
  for (uint32_t x = 0; x < nb0; x++) {
    Block *b = &f->blocks[x];
    if (in[x] || !b->reachable) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref *a = ir_args(f, b->insts[i]);
      for (uint32_t q = 0; q < f->insts[b->insts[i]].nargs; q++) UN_MAP(a[q]);
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) UN_MAP(b->term_arg);
    for (uint32_t q = 0; q < b->nret; q++) UN_MAP(b->ret_args[q]);
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t q = 0; q < b->succs[s].nargs; q++) UN_MAP(b->succs[s].args[q]);
  }
#undef UN_MAP
  xfree(bc); xfree(mp); xfree(in); xfree(stack);
  ir_recompute_preds(f);
  return true;
}

static bool pass_unroll_nest(IrFunc *f) {
  if (f->npads || warmc_getenv("WARMC_NO_UNROLL_NEST")) return false;
  bool changed = false;
  /* one loop per round: the rest of the orders, depths and preds are stale */
  for (uint32_t oi = 0; oi < f->norder; oi++)
    if (unroll_nest_one(f, f->order[oi])) { changed = true; break; }
  return changed;
}

/* A bottom-tested loop that runs exactly once -- the unroller's copies of
   a loop whose trip was the unroll factor, or a walk over one element --
   never takes its back edge: its test becomes the jump out. */
static bool pass_once_loops(IrFunc *f) {
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    Block *h = &f->blocks[hb];
    if (!h->reachable || !h->nparams || h->is_pad) continue;
    uint32_t pre = UINT32_MAX, latch = UINT32_MAX, nfwd = 0, nback = 0;
    for (uint32_t p = 0; p < h->npreds; p++) {
      uint32_t pb = h->preds[p];
      if (f->blocks[pb].rpo >= h->rpo) { nback++; latch = pb; }
      else { nfwd++; pre = pb; }
    }
    if (nback != 1 || nfwd != 1) continue;
    Block *lb = &f->blocks[latch];
    if (lb->term != T_BRIF || lb->nsuccs != 2) continue;
    uint32_t ci = lb->succs[0].target == hb ? 0 : 1;
    if (lb->succs[ci].target != hb || lb->succs[1 - ci].target == hb) continue;
    if (loop_trip_bottom(f, hb, latch, pre) != 1) continue;
    lb = &f->blocks[latch];
    lb->succs[0] = lb->succs[1 - ci];
    lb->nsuccs = 1;
    lb->term = T_JUMP;
    lb->term_arg = REF_NONE;
    changed = true;
  }
  if (changed) ir_recompute_preds(f);
  return changed;
}

/* Code after an unrolled loop reads what the last copy made (fixmap, by
   the loop's cycleid).  All reachable blocks, not f->order: guarded fires
   add blocks whose clones may read values an earlier in-place fire
   repaired. */
static void unroll_fix(IrFunc *f, const Ref *fixmap, const uint32_t *cycleid,
                       uint32_t nblocks0, uint32_t ninsts0) {
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    if (!b->reachable) continue;
    uint32_t cid = bi < nblocks0 ? cycleid[bi] : 0xffffffffu;
#define UFIX(r) do { if ((r) < ninsts0 && fixmap[r] != REF_NONE && \
      cycleid[f->insts[r].block] != cid) (r) = fixmap[r]; } while (0)
    for (uint32_t j = 0; j < b->ninsts; j++) {
      Ref *a = ir_args(f, b->insts[j]);
      for (uint32_t x = 0; x < f->insts[b->insts[j]].nargs; x++) UFIX(a[x]);
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) UFIX(b->term_arg);
    for (uint32_t x = 0; x < b->nret; x++) UFIX(b->ret_args[x]);
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      Edge *e = &b->succs[s];
      for (uint32_t x = 0; x < e->nargs; x++) UFIX(e->args[x]);
    }
#undef UFIX
  }
}

static bool pass_unroll(IrFunc *f) {
  pthread_once(&g_unr_once, unr_init);
  if (g_unr.off) return false;
  if (g_unr.only >= 0 && f->func_index != (uint32_t)g_unr.only) return false;
  if (g_unr.upto >= 0 && f->func_index > (uint32_t)g_unr.upto) return false;
  uint32_t ninsts0 = f->ninsts;
  uint32_t nblocks0 = f->nblocks;
  Ref *fixmap = NULL;
  uint32_t *cycleid = xmalloc((size_t)nblocks0 * sizeof *cycleid);
  memset(cycleid, 0xff, (size_t)nblocks0 * sizeof *cycleid);
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    if (!fixmap) {
      fixmap = xmalloc((size_t)ninsts0 * sizeof *fixmap);
      memset(fixmap, 0xff, (size_t)ninsts0 * sizeof *fixmap);
    }
    if (unroll_one(f, f->order[oi], fixmap, cycleid, oi, ninsts0, 0, NULL))
      changed = true;
  }
  if (changed) unroll_fix(f, fixmap, cycleid, nblocks0, ninsts0);
  xfree(fixmap);
  xfree(cycleid);
  return changed;
}

/* The copies step each counter one copy at a time, ((i + c) + c) + c, and
   a counter is what the vectoriser takes only as one add of a constant to
   its parameter: the last link, which the loop carries round and its exit
   tests, becomes i + 3c, the copies' own values left as they are. */
static void unroll_iv_sum(IrFunc *f, uint32_t hb) {
  Block *h = &f->blocks[hb];
  uint32_t latch = REF_NONE;
  for (uint32_t p = 0; p < h->npreds; p++)
    if (f->blocks[h->preds[p]].rpo >= h->rpo) latch = h->preds[p];
  if (latch == REF_NONE) return;
  Edge *back = iv_edge(f, latch, hb);
  if (!back || back->nargs != h->nparams) return;
  for (uint32_t p = 0; p < h->nparams; p++) {
    Ref par = h->params[p], cur = back->args[p];
    ValType t = (ValType)f->insts[par].type;
    if (t != VT_I32 && t != VT_I64) continue;
    uint64_t sum = 0, c;
    uint32_t links = 0;
    while (cur != par && links < 64) {
      const Inst *in = &f->insts[cur];
      if ((in->op != I_ADD && in->op != I_SUB) || in->nargs != 2 ||
          !is_const(f, ir_args(f, cur)[1], &c))
        break;
      sum += in->op == I_ADD ? c : (uint64_t)0 - c;
      cur = ir_args(f, cur)[0];
      links++;
    }
    if (cur != par || links < 2) continue;
    if (t == VT_I32) sum &= 0xffffffffu;
    Ref last = back->args[p];
    uint32_t lb = f->insts[last].block, at = UINT32_MAX;
    for (uint32_t j = 0; j < f->blocks[lb].ninsts; j++)
      if (f->blocks[lb].insts[j] == last) { at = j; break; }
    if (at == UINT32_MAX) continue;
    Ref k = emit_before(f, lb, &at, I_CONST, t, NULL, 0, sum);
    Ref *la = ir_args(f, last);
    f->insts[last].op = I_ADD;
    la[0] = par;
    la[1] = k;
    back = iv_edge(f, latch, hb);
  }
}

/* One loop unrolled by the factor the vectoriser asks for, as pass_unroll
   would with that factor, and the function's order redone; *newh is the
   loop that holds the copies. */
static bool unroll_by(IrFunc *f, uint32_t hb, uint32_t want, uint32_t *newh) {
  pthread_once(&g_unr_once, unr_init);
  if (g_unr.off) return false;
  uint32_t ninsts0 = f->ninsts, nblocks0 = f->nblocks;
  Ref *fixmap = xmalloc((size_t)ninsts0 * sizeof *fixmap);
  memset(fixmap, 0xff, (size_t)ninsts0 * sizeof *fixmap);
  uint32_t *cycleid = xmalloc((size_t)nblocks0 * sizeof *cycleid);
  memset(cycleid, 0xff, (size_t)nblocks0 * sizeof *cycleid);
  bool ok = unroll_one(f, hb, fixmap, cycleid, 0, ninsts0, want, newh);
  if (ok) {
    unroll_fix(f, fixmap, cycleid, nblocks0, ninsts0);
    ir_order(f);
    unroll_iv_sum(f, *newh);
  }
  xfree(fixmap);
  xfree(cycleid);
  return ok;
}

#define LDC_LANES 8
#define LDC_TERMS 4
#define LDC_DEPTH 6

typedef struct {
  Ref term[LDC_TERMS];
  uint32_t nterm;
  int64_t c;
  bool ok;
} AddrSum;

static void addr_sum_term(AddrSum *s, Ref t) {
  if (s->nterm >= LDC_TERMS) { s->ok = false; return; }
  uint32_t i = s->nterm;
  while (i > 0 && s->term[i - 1] > t) { s->term[i] = s->term[i - 1]; i--; }
  s->term[i] = t;
  s->nterm++;
}

static void addr_sum(IrFunc *f, Ref v, AddrSum *s, int depth) {
  Inst *in = &f->insts[v];
  if ((ValType)in->type == VT_V128) { addr_sum_term(s, v); return; }
  bool wide = vt_is_64((ValType)in->type);
  if (in->op == I_CONST) {
    s->c += wide ? (int64_t)in->imm : (int64_t)(int32_t)(uint32_t)in->imm;
    return;
  }
  if (depth > 0 && in->nargs == 2 && (in->op == I_ADD || in->op == I_SUB)) {
    Ref *a = ir_args(f, v);
    if (in->op == I_ADD) {
      addr_sum(f, a[0], s, depth - 1);
      addr_sum(f, a[1], s, depth - 1);
      return;
    }

    Inst *r = &f->insts[a[1]];
    if (r->op == I_CONST && (ValType)r->type != VT_V128) {
      addr_sum(f, a[0], s, depth - 1);
      s->c -= wide ? (int64_t)r->imm : (int64_t)(int32_t)(uint32_t)r->imm;
      return;
    }
  }
  addr_sum_term(s, v);
}

static bool addr_sum_same(const AddrSum *a, const AddrSum *b) {
  if (!a->ok || !b->ok || a->nterm != b->nterm) return false;
  for (uint32_t i = 0; i < a->nterm; i++)
    if (a->term[i] != b->term[i]) return false;
  return true;
}

static bool ldc_crossable(const Inst *in) {
  switch (in->op) {
    case I_NOP: case I_PARAM: case I_CONST:
    case I_ADD: case I_SUB: case I_MUL:
    case I_AND: case I_OR: case I_XOR:
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTL: case I_ROTR:
    case I_CLZ: case I_CTZ: case I_POPCNT: case I_BSWAP:
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_SELECT:
    case I_WRAP: case I_EXTENDS: case I_EXTENDU:
    case I_EXT8S: case I_EXT16S: case I_EXT32S:
    case I_MEM_ADDR: case I_MEM_PTR: case I_MEM_BASE: case I_MEM_LIMIT:
    case I_LOAD:
      return true;
    default:
      return false;
  }
}

typedef struct {
  Ref load;
  uint32_t shift;
  int64_t disp;
  int64_t acon;
  uint32_t pos;
} LdcLane;

static bool ldc_collect(IrFunc *f, Ref v, uint32_t bi, const uint32_t *uses,
                        uint32_t nuses, Ref *leaf, uint32_t *nleaf) {
  Ref *a = ir_args(f, v);
  for (int k = 0; k < 2; k++) {
    Ref c = a[k];
    if (c >= nuses) return false;
    Inst *ci = &f->insts[c];
    if (ci->op == I_OR && ci->nargs == 2 && ci->block == bi && uses[c] == 1 &&
        ci->type == f->insts[v].type) {
      if (!ldc_collect(f, c, bi, uses, nuses, leaf, nleaf)) return false;
    } else {
      if (*nleaf >= LDC_LANES) return false;
      leaf[(*nleaf)++] = c;
    }
  }
  return true;
}

static bool ldc_lane(IrFunc *f, Ref v, uint32_t bi, const uint32_t *uses,
                     uint32_t nuses, LdcLane *L, uint32_t *size, uint32_t *mem,
                     bool *seen) {
  if (v >= nuses) return false;
  Inst *in = &f->insts[v];
  Ref ld = v;
  L->shift = 0;
  if (in->op == I_SHL) {
    uint64_t k;
    if (in->nargs != 2 || in->block != bi || uses[v] != 1) return false;
    if (!is_const(f, ir_args(f, v)[1], &k)) return false;

    L->shift = (uint32_t)(k % (vt_is_64((ValType)in->type) ? 64u : 32u));
    ld = ir_args(f, v)[0];
    if (ld >= nuses) return false;
  }
  Inst *li = &f->insts[ld];
  if (li->op != I_LOAD || li->nargs != 1) return false;
  if (li->block != bi || uses[ld] != 1) return false;
  if (li->imm2 & MEMF_SIGNED) return false;
  if (li->type != f->insts[v].type) return false;
  uint32_t sz = (uint32_t)MEMF_SIZE(li->imm2), mm = MEMF_MEM(li->imm2);
  if (sz >= 3) return false;
  if (*seen && (sz != *size || mm != *mem)) return false;
  *size = sz; *mem = mm; *seen = true;
  L->load = ld;
  return true;
}

static Ref ldc_emit_at(IrFunc *f, uint32_t bi, uint32_t *idx, IrOp op, ValType t,
                       const Ref *args, uint32_t nargs, uint64_t imm,
                       uint64_t imm2) {
  Ref r = ir_emit(f, bi, op, t, args, nargs, imm, imm2);
  Block *b = &f->blocks[bi];
  for (uint32_t j = b->ninsts - 1; j > *idx; j--) b->insts[j] = b->insts[j - 1];
  b->insts[*idx] = r;
  (*idx)++;
  return r;
}

static bool ldc_block(IrFunc *f, uint32_t bi, const uint32_t *uses,
                      uint32_t nuses, uint8_t *or_operand, const OptOpts *o) {
  Block *b = &f->blocks[bi];
  bool changed = false;
  for (uint32_t i = 0; i < b->ninsts; i++) {
    Ref root = b->insts[i];
    if (root >= nuses || or_operand[root]) continue;
    Inst *rin = &f->insts[root];
    if (rin->op != I_OR || rin->nargs != 2) continue;
    ValType t = (ValType)rin->type;
    if (t != VT_I32 && t != VT_I64) continue;
    uint32_t width = vt_is_64(t) ? 64 : 32;

    Ref leaf[LDC_LANES];
    uint32_t n = 0;
    if (!ldc_collect(f, root, bi, uses, nuses, leaf, &n)) continue;
    if (n != 2 && n != 4 && n != 8) continue;

    LdcLane lane[LDC_LANES];
    uint32_t size = 0, mem = 0;
    bool seen = false, bad = false;
    for (uint32_t k = 0; k < n && !bad; k++)
      bad = !ldc_lane(f, leaf[k], bi, uses, nuses, &lane[k], &size, &mem, &seen);
    if (bad) continue;

    uint32_t stride = 1u << size, lane_bits = 8u * stride;
    uint64_t total = (uint64_t)n * stride;
    if (total != 2 && total != 4 && total != 8) continue;
    if (total * 8 > width) continue;

    AddrSum base = { { 0 }, 0, 0, false };
    bool const_addr = false;
    for (uint32_t k = 0; k < n && !bad; k++) {
      Inst *li = &f->insts[lane[k].load];
      Ref addr = ir_args(f, lane[k].load)[0];
      Inst *ai = &f->insts[addr];
      bool kconst = ai->op == I_CONST && (ValType)ai->type == VT_I64;
      if (!kconst && (ai->op != I_MEM_ADDR || ai->nargs != 1)) { bad = true; break; }
      if (k == 0) const_addr = kconst;
      else if (kconst != const_addr) { bad = true; break; }
      AddrSum s = { { 0 }, 0, 0, true };
      if (kconst) s.c = (int64_t)ai->imm;
      else addr_sum(f, ir_args(f, addr)[0], &s, LDC_DEPTH);
      if (!s.ok) { bad = true; break; }
      if (k == 0) base = s;
      else if (!addr_sum_same(&base, &s)) { bad = true; break; }
      lane[k].disp = s.c + (int64_t)li->imm + (kconst ? 0 : (int64_t)ai->imm);
      lane[k].acon = kconst ? 0 : s.c;
      lane[k].pos = UINT32_MAX;
      for (uint32_t j = 0; j < b->ninsts; j++)
        if (b->insts[j] == lane[k].load) { lane[k].pos = j; break; }
      if (lane[k].pos == UINT32_MAX) bad = true;
    }
    if (bad) continue;

    int64_t d0 = lane[0].disp;
    for (uint32_t k = 1; k < n; k++) if (lane[k].disp < d0) d0 = lane[k].disp;
    uint32_t at_index[LDC_LANES];
    for (uint32_t k = 0; k < n; k++) at_index[k] = UINT32_MAX;
    for (uint32_t k = 0; k < n && !bad; k++) {
      int64_t rel = lane[k].disp - d0;
      if (rel % stride) { bad = true; break; }
      int64_t j = rel / stride;
      if (j < 0 || (uint64_t)j >= n || at_index[j] != UINT32_MAX) { bad = true; break; }
      if (lane[k].shift % lane_bits) { bad = true; break; }
      if (lane[k].shift / lane_bits >= n) { bad = true; break; }
      at_index[j] = k;
    }
    if (bad) continue;

    bool little = true, big = true;
    for (uint32_t j = 0; j < n; j++) {
      uint32_t s = lane[at_index[j]].shift / lane_bits;
      if (s != j) little = false;
      if (s != n - 1 - j) big = false;
    }

    if (big && stride != 1) big = false;
    if (!little && !big) continue;

    int64_t acon_low = lane[at_index[0]].acon;
    for (uint32_t k = 0; k < n && !bad; k++) {
      int64_t d = lane[k].acon - acon_low;
      if (d < 0 || (uint64_t)d >= total) bad = true;
    }
    if (bad) continue;

    uint32_t lo = lane[0].pos, hi = lane[0].pos;
    for (uint32_t k = 1; k < n; k++) {
      if (lane[k].pos < lo) lo = lane[k].pos;
      if (lane[k].pos > hi) hi = lane[k].pos;
    }
    for (uint32_t p = lo; p <= hi && !bad; p++) {
      Ref w = b->insts[p];
      bool member = false;
      for (uint32_t k = 0; k < n; k++) if (lane[k].load == w) member = true;
      if (!member && !ldc_crossable(&f->insts[w])) bad = true;
    }
    if (bad) continue;

    Ref low = lane[at_index[0]].load;
    Ref addr = ir_args(f, low)[0];
    uint64_t off = f->insts[low].imm;
    int tsize = total == 8 ? 3 : total == 4 ? 2 : 1;

    if (!o->guard_pages) continue;
    if (mem < f->m->nmemories && f->m->memories[mem].index64) continue;
    if (const_addr) {

      if ((uint64_t)lane[at_index[0]].disp + total > 0x100000000ull) continue;
    } else if (off + f->insts[addr].imm + total > WASM_GUARD_OFFSET_MAX) {
      continue;
    }
    uint32_t at = hi;
    Ref wideld = ldc_emit_at(f, bi, &at, I_LOAD, t, &addr, 1, off,
                             MEMF_MK_AT((uint32_t)tsize, 0, mem));
    Ref result = wideld;
    if (big) {
      Ref rev = ldc_emit_at(f, bi, &at, I_BSWAP, t, &result, 1, 0, 0);
      result = rev;
      if (total * 8 < width) {

        Ref k = ldc_emit_at(f, bi, &at, I_CONST, t, NULL, 0,
                            (uint64_t)(width - total * 8), 0);
        Ref args[2] = { result, k };
        result = ldc_emit_at(f, bi, &at, I_SHRU, t, args, 2, 0, 0);
      }
    }
    ir_replace(f, root, result);

    for (uint32_t k = 0; k < n; k++) f->insts[lane[k].load].op = I_NOP;
    g_stats.loads_combined++;
    changed = true;
    i += at - hi;
  }
  return changed;
}

static bool pass_load_combine(IrFunc *f, const OptOpts *o) {
  /* every combination is rooted at an integer or */
  bool any = false;
  for (uint32_t oi = 0; oi < f->norder && !any; oi++) {
    const Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts && !any; i++) {
      const Inst *in = &f->insts[b->insts[i]];
      any = in->op == I_OR && in->nargs == 2 &&
            (in->type == VT_I32 || in->type == VT_I64);
    }
  }
  if (!any) return false;
  uint32_t nuses = f->ninsts;
  uint32_t *uses = count_uses(f);
  uint8_t *or_operand = xcalloc(nuses ? nuses : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      if (in->op != I_OR || in->nargs != 2) continue;
      Ref *a = ir_args(f, b->insts[i]);
      for (int k = 0; k < 2; k++)
        if (a[k] < nuses && f->insts[a[k]].block == in->block &&
            f->insts[a[k]].type == in->type)
          or_operand[a[k]] = 1;
    }
  }
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++)
    if (ldc_block(f, f->order[oi], uses, nuses, or_operand, o)) changed = true;
  xfree(uses);
  xfree(or_operand);
  if (changed) ir_apply_subst(f);
  return changed;
}

static bool pass_self_tail(IrFunc *f) {
  if (f->npads) return false;
  uint32_t np = f->sig->nparams;
  if (f->blocks[f->entry].nparams != np) return false;

  bool any = false;
  for (uint32_t bi = 0; bi < f->nblocks && !any; bi++) {
    Block *b = &f->blocks[bi];
    if (!b->reachable) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      if (in->op == I_RETURN_CALL && in->imm == f->func_index &&
          in->nargs == np) { any = true; break; }
    }
  }
  if (!any) return false;

  uint32_t entry = f->entry;
  Ref *eparam = xmalloc((np ? np : 1) * sizeof(Ref));
  memcpy(eparam, f->blocks[entry].params, np * sizeof(Ref));

  uint32_t hb = ir_new_block(f);
  Ref *hparam = xmalloc((np ? np : 1) * sizeof(Ref));
  for (uint32_t i = 0; i < np; i++)
    hparam[i] = ir_add_param(f, hb, f->sig->params[i]);

  Block *e = &f->blocks[entry], *h = &f->blocks[hb];
  h->insts = e->insts; h->ninsts = e->ninsts; h->cap_insts = e->cap_insts;
  e->insts = NULL; e->ninsts = e->cap_insts = 0;
  for (uint32_t i = 0; i < h->ninsts; i++) f->insts[h->insts[i]].block = hb;
  h->term = e->term; h->term_arg = e->term_arg; h->term_imm = e->term_imm;
  h->succs = e->succs; h->nsuccs = e->nsuccs; h->cap_succs = e->cap_succs;
  h->ret_args = e->ret_args; h->nret = e->nret; h->cap_ret = e->cap_ret;
  e->succs = NULL; e->nsuccs = e->cap_succs = 0;
  e->ret_args = NULL; e->nret = e->cap_ret = 0;
  h->sealed = h->filled = h->reachable = true;

  for (uint32_t i = 0; i < f->ninsts; i++) {
    Inst *in = &f->insts[i];
    Ref *a = ir_args(f, i);
    for (uint32_t j = 0; j < in->nargs; j++)
      for (uint32_t p = 0; p < np; p++)
        if (a[j] == eparam[p]) { a[j] = hparam[p]; break; }
  }
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    for (uint32_t p = 0; p < np; p++) {
      if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == eparam[p])
        b->term_arg = hparam[p];
      for (uint32_t j = 0; j < b->nret; j++)
        if (b->ret_args[j] == eparam[p]) b->ret_args[j] = hparam[p];
      for (uint32_t s = 0; s < b->nsuccs; s++)
        for (uint32_t j = 0; j < b->succs[s].nargs; j++)
          if (b->succs[s].args[j] == eparam[p]) b->succs[s].args[j] = hparam[p];
    }
  }

  f->blocks[entry].term = T_JUMP;
  ir_add_edge(f, entry, hb, eparam, np);

  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    if (bi == hb || !b->reachable) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref call = b->insts[i];
      Inst *in = &f->insts[call];
      if (in->op != I_RETURN_CALL || in->imm != f->func_index ||
          in->nargs != np) continue;
      Ref *ca = xmalloc((np ? np : 1) * sizeof(Ref));
      memcpy(ca, ir_args(f, call), np * sizeof(Ref));

      b->nsuccs = 0;
      b->nret = 0;
      b->ninsts = i;
      b->term = T_JUMP;
      ir_add_edge(f, bi, hb, ca, np);
      xfree(ca);
      in->op = I_NOP;
      break;
    }
  }

  xfree(eparam); xfree(hparam);
  ir_recompute_preds(f);
  ir_order(f);
  g_stats.self_tail++;
  return true;
}

static double pt_now(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
typedef struct { const char *nm; double ms; unsigned long calls, hits; } PassT;
#define PT_MAX 64
static PassT g_pt[PT_MAX];
static int g_npt;
static pthread_mutex_t g_pt_lock = PTHREAD_MUTEX_INITIALIZER;
static int pt_slot(const char *nm) {
  for (int i = 0; i < g_npt; i++) if (g_pt[i].nm == nm) return i;
  if (g_npt == PT_MAX) return -1;
  g_pt[g_npt].nm = nm; return g_npt++;
}
static bool g_pt_on;
void opt_passtime_report(void);
void opt_passtime_report(void) {
  if (!g_pt_on) return;
  for (int i = 0; i < g_npt; i++)
    for (int j = i + 1; j < g_npt; j++)
      if (g_pt[j].ms > g_pt[i].ms) { PassT t = g_pt[i]; g_pt[i] = g_pt[j]; g_pt[j] = t; }
  fprintf(stderr, "%-22s %9s %9s %9s\n", "pass", "ms", "calls", "hits");
  for (int i = 0; i < g_npt; i++)
    fprintf(stderr, "%-22s %9.1f %9lu %9lu\n",
            g_pt[i].nm, g_pt[i].ms, g_pt[i].calls, g_pt[i].hits);
}
#define TP(nm, call) ({ bool _r; if (g_pt_on) { \
    double _t = pt_now(); _r = (call); double _d = pt_now() - _t; \
    pthread_mutex_lock(&g_pt_lock); int _s = pt_slot(nm); \
    if (_s >= 0) { g_pt[_s].ms += _d; g_pt[_s].calls++; if (_r) g_pt[_s].hits++; } \
    pthread_mutex_unlock(&g_pt_lock); } else _r = (call); _r; })

static pthread_once_t g_pt_once = PTHREAD_ONCE_INIT;
static void pt_init(void) {
  g_pt_on = warmc_getenv("WARMC_PASSTIME") != NULL;
}

/* The cleanup rounds every inline is followed by: folding, forwarding and
   escape analysis until they stop finding work. */
/* A pass that finds nothing to do finds nothing again while the function is
   as it left it -- none of them changes the function when it reports no
   change -- so a round runs only the passes something has changed under
   since: ver counts the changes, the passes' and a reordering's, and done
   holds the change each pass last ran clean at.  The round that confirms a
   cleanup is finished used to run every pass once more. */
static void opt_cleanup(IrFunc *f, const OptOpts *o) {
  uint32_t ver = 1, done[16] = {0};
#define RUN(i, nm, call) \
  (done[i] != ver && (TP(nm, call) ? (ver++, true) : (done[i] = ver, false)))
  for (int round = 0; round < 6; round++) {
    bool changed = false;
    if (o->throwjump && RUN(0, "throwjump", pass_local_throw(f))) changed = true;
    if (o->fold && RUN(1, "fold", pass_fold(f, o))) changed = true;
    if (o->branch && RUN(2, "branch", pass_branch(f))) changed = true;
    if (o->thread && RUN(3, "thread", pass_thread(f))) changed = true;
    if (o->thread && o->nodesplit && RUN(4, "nodesplit", pass_nodesplit(f))) changed = true;
    if (o->cse && RUN(5, "cse", pass_cse(f, o))) changed = true;
    if (o->memopt && RUN(6, "memopt", pass_memopt(f, o))) changed = true;

    if (o->ldcomb && RUN(7, "ldcomb", pass_load_combine(f, o))) changed = true;
    if (o->bce && RUN(8, "bce", pass_bce(f, f->m))) changed = true;

    if (o->bce && RUN(9, "arraybounds", pass_array_bounds(f))) changed = true;
    if (o->gcpure && RUN(10, "gcnull", pass_gc_nullchk(f))) changed = true;

    if (o->escape && RUN(11, "escape", pass_escape(f, o->pescape))) changed = true;
    if (o->dce && RUN(12, "dce", pass_dce(f))) changed = true;
    if (RUN(13, "prune_params", prune_params(f))) changed = true;
    if (ir_order_changed(f)) ver++;
    if (!changed) break;
  }
#undef RUN
}

/* -ffma: a float multiply whose one reader is an add or a subtract becomes
   one fused multiply-add, rounded once.  That is not wasm's arithmetic --
   f32.mul and f32.add round each -- so a result can differ in its last bit,
   or come out finite where the separate ones overflowed: off unless asked
   for, for code that can take it, as C can by default (clang contracts
   a * b + c natively, the fmla.2d cbench/matmul's native build has and the
   wasm cannot ask for).  Scalars, and the vectoriser's lanes, last of all;
   a vector a * b - c has no one instruction and stays.

   Not where the addend is what a loop carries round and the multiply is
   off every such chain, acc = acc + a[i] * b[i]: there the add is the chain
   each iteration waits on, and a fused one waits four cycles where the add
   alone took three -- a dot product ran a third slower fused.  Where the
   product is on a chain too, x = x + dt * v with v this iteration's, it is
   the later operand, and fusing shortens the chain: cbench/nbody, 16%.  A
   value is on a chain when a loop's parameter reaches it and it reaches
   what the back edge hands that parameter (fma_cycles). */
static void fma_cycles(IrFunc *f, uint8_t *oncyc) {
  uint32_t n = f->ninsts;
  UseIdx ui = use_index(f);
  uint8_t *back = xcalloc(n ? n : 1, 1);
  Ref *stk = xmalloc((n ? n : 1) * sizeof(Ref)), *seen = xmalloc((n ? n : 1) * sizeof(Ref));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t hb = f->order[oi];
    Block *h = &f->blocks[hb];
    for (uint32_t q = 0; q < h->npreds; q++) {
      uint32_t lb = h->preds[q];
      if (f->blocks[lb].rpo < h->rpo) continue;
      Edge *e = iv_edge(f, lb, hb);
      if (!e || e->nargs != h->nparams) continue;
      for (uint32_t k = 0; k < h->nparams; k++) {
        /* what reaches the back edge's argument, inside the loop */
        uint32_t sp = 0, ns = 0;
        Ref a0 = ir_resolve(f, e->args[k]);
        if (a0 < n) { stk[sp++] = a0; back[a0] = 1; seen[ns++] = a0; }
        while (sp && ns < 4096) {
          Ref x = stk[--sp];
          const Inst *xi = &f->insts[x];
          if (xi->op == I_PARAM || !ir_dom_range(f, hb, xi->block)) continue;
          const Ref *xa = ir_args(f, x);
          for (uint32_t j = 0; j < xi->nargs; j++) {
            Ref y = ir_resolve(f, xa[j]);
            if (y < n && !back[y] && ir_dom_range(f, hb, f->insts[y].block)) {
              back[y] = 1; seen[ns++] = y; stk[sp++] = y;
            }
          }
        }
        /* of those, the ones the parameter reaches */
        Ref P = h->params[k];
        if (P < n && back[P]) {
          oncyc[P] = 1;
          sp = 0;
          stk[sp++] = P;
          while (sp) {
            Ref x = stk[--sp];
            for (uint32_t u = ui.first[x]; u != UINT32_MAX; u = ui.next[u]) {
              Ref y = ui.user[u];
              if (y < n && back[y] && !oncyc[y]) { oncyc[y] = 1; stk[sp++] = y; }
            }
          }
        }
        for (uint32_t j = 0; j < ns; j++) back[seen[j]] = 0;
      }
    }
  }
  xfree(back); xfree(stk); xfree(seen);
  use_index_free(&ui);
}

static bool pass_fma(IrFunc *f) {
  uint32_t nuses = f->ninsts;
  uint32_t *uses = count_uses(f);
  uint8_t *oncyc = xcalloc(nuses ? nuses : 1, 1);
  fma_cycles(f, oncyc);
  bool changed = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++) {
      Ref v = f->blocks[bi].insts[i];
      const Inst *in = &f->insts[v];
      bool vec = in->op == I_VFADD || in->op == I_VFSUB;
      if ((!vec && in->op != I_FADD && in->op != I_FSUB) || in->nargs != 2) continue;
      bool sub = in->op == I_FSUB || in->op == I_VFSUB;
      for (int k = 0; k < 2; k++) {
        Ref m = ir_args(f, v)[k], c = ir_args(f, v)[1 - k];
        const Inst *mi = &f->insts[m];
        if (mi->op != (vec ? I_VFMUL : I_FMUL) || mi->nargs != 2 ||
            m >= nuses || uses[m] != 1 || mi->type != in->type ||
            (vec && mi->imm2 != in->imm2))
          continue;
        Ref cr = ir_resolve(f, c);
        Ref x0 = ir_resolve(f, ir_args(f, m)[0]), x1 = ir_resolve(f, ir_args(f, m)[1]);
        bool mcyc = (x0 < nuses && oncyc[x0]) || (x1 < nuses && oncyc[x1]);
        if (cr < nuses && oncyc[cr] && v < nuses && oncyc[v] && !mcyc) continue;
        /* c - a*b where the multiply is what is taken away, a*b - c else */
        uint64_t form = !sub ? 0 : k == 1 ? 1 : 2;
        if (vec && form == 2) continue;
        Ref ma[3] = { ir_args(f, m)[0], ir_args(f, m)[1], c };
        uint64_t lane = in->imm2;
        Ref r = emit_before(f, bi, &i, vec ? I_VFMADD : I_FMA,
                            (ValType)in->type, ma, 3, form);
        f->insts[r].imm2 = vec ? lane : 0;
        ir_replace(f, v, r);
        f->insts[v].op = I_NOP;
        g_stats.fma_fused++;
        changed = true;
        break;
      }
    }
  }
  xfree(uses);
  xfree(oncyc);
  if (changed) ir_apply_subst(f);
  return changed;
}

/* Inlining, devirtualization and escape analysis feed one another: an
   inlined callee shows a dispatch the class of its receiver, the direct
   call that dispatch becomes is inlined in turn, and an allocation handed
   down a chain of calls goes only once the last of them is inlined.  Each
   further round looks only at the direct calls devirtualization made and
   at the calls an allocation leaves by. */
#define OPT_INLINE_ROUNDS 4

void ir_optimize(IrFunc *f, const OptOpts *o) {
  pthread_once(&g_pt_once, pt_init);
  if (o->level == 0) return;
  ir_order(f);
  prune_params(f);
  ir_order(f);

  if (o->devirt && TP("devirt", pass_devirt(f, o)))
    ir_verify(f, "post-devirt");

  if (o->selftail) pass_self_tail(f);

  if (o->inl && pass_inline(f, o)) {
    prune_params(f);
    ir_order(f);
    ir_verify(f, "post-inline");

    /* inlined bodies arrive raw and can carry devirtualizable sites */
    if (o->devirt && TP("devirt", pass_devirt(f, o)))
      ir_verify(f, "post-devirt2");
  }

  opt_cleanup(f, o);

  for (int r = 0; o->inl && o->reinline && r < OPT_INLINE_ROUNDS; r++) {
    if (o->devirt) TP("devirt", pass_devirt(f, o));
    if (!pass_inline_more(f, o)) break;
    prune_params(f);
    ir_order(f);
    ir_verify(f, "post-inline-more");
    if (o->devirt && TP("devirt", pass_devirt(f, o)))
      ir_verify(f, "post-devirt-more");
    opt_cleanup(f, o);
  }

  if (o->inl && pass_inline_round2(f, o)) {
    prune_params(f);
    ir_order(f);
    ir_verify(f, "post-inline2");
    if (o->devirt && TP("devirt", pass_devirt(f, o)))
      ir_verify(f, "post-devirt3");
    if (o->fold) pass_fold(f, o);
    if (o->cse) pass_cse(f, o);
    if (o->dce) pass_dce(f);
    ir_order(f);
  }

  if (o->gloop && TP("global_loop", pass_global_loop(f, o))) {
    if (o->fold) pass_fold(f, o);
    if (o->cse) pass_cse(f, o);
    if (o->dce) pass_dce(f);
    ir_order(f);
    ir_verify(f, "post-global-loop");
  }

  if (o->gssa && TP("global_ssa", pass_global_ssa(f, o))) {
    if (o->fold) pass_fold(f, o);
    if (o->cse) pass_cse(f, o);
    if (o->dce) pass_dce(f);
    prune_params(f);
    ir_order(f);
    ir_verify(f, "post-global-ssa-clean");
  }
  /* Only once global_ssa has weighed a global's writes: dropping one first
     tipped stdgo/recurse's fib into leaving its stack pointer in memory,
     25% slower. */
  if (o->memopt && TP("global_dse", global_dse(f)) && o->dce) pass_dce(f);

  for (int shape = 0; shape < 6; shape++) {
    bool moved = false;
    if (o->licm && TP("loop_hoist", pass_loop_hoist(f))) {
      for (uint32_t bi = 0; bi < f->nblocks; bi++)
        while (f->blocks[bi].reachable && lh_merge_down(f, bi))
          ir_recompute_preds(f);
      ir_recompute_preds(f);
      ir_order(f);
      if (o->branch) pass_branch(f);
      if (o->dce) pass_dce(f);
      prune_params(f);
      ir_order(f);
      ir_verify(f, "post-loop-hoist");
      moved = true;
    }
    if (o->rotate && TP("rotate", pass_rotate(f))) {
      prune_params(f);
      ir_order(f);
      if (o->dce) pass_dce(f);
      ir_order(f);
      ir_verify(f, "post-rotate");
      moved = true;
    }
    if (!moved) break;
  }

  for (int r = 0; o->unroll && r < 4 && TP("unroll_nest", pass_unroll_nest(f)); r++) {
    ir_order(f);
    opt_cleanup(f, o);
    ir_verify(f, "post-unroll-nest");
  }

  if (o->strength && TP("iv_strength", pass_iv_strength(f))) {
    prune_params(f);
    ir_order(f);
    if (o->cse) pass_cse(f, o);
    if (o->fold) pass_fold(f, o);
    if (o->dce) pass_dce(f);
    ir_order(f);
    ir_verify(f, "post-iv-early");
  }
  if (o->memptr && o->guard_pages && TP("mem_ptr", pass_mem_ptr(f))) {
    if (o->dce) pass_dce(f);
    ir_order(f);
  }
  if (o->addroff && TP("addr_offsets", pass_addr_offsets(f, o))) ir_order(f);
  if (o->licm) {
    if (TP("licm", pass_licm(f, o))) {
      ir_order(f);
      if (o->dce) pass_dce(f);
      if (o->cse) pass_cse(f, o);
      if (o->dce) pass_dce(f);
      ir_order(f);
    }
  }
  if (o->strength && TP("invdiv", pass_invdiv(f))) {
    if (o->dce) pass_dce(f);
    ir_order(f);
  }
  if (o->strength) TP("divrem", pass_divrem(f));

  if (o->strength && pass_iv_strength(f)) {
    prune_params(f);
    ir_order(f);
    if (o->cse) pass_cse(f, o);
    if (o->fold) pass_fold(f, o);
    if (o->dce) pass_dce(f);
    ir_order(f);
    ir_verify(f, "post-iv");
  }
  if (o->dce) pass_dce(f);

  /* the bounds checks rotation leaves decided behind a loop's own exit
     test, which are body exits to the vectoriser */
  if (o->vectorize && o->edgetests && o->guard_pages && o->branch &&
      TP("edge_tests", pass_edge_tests(f))) {
    pass_branch(f);
    if (o->dce) pass_dce(f);
    pass_branch(f);
    prune_params(f);
    ir_order(f);
    ir_verify(f, "post-edge-tests");
  }

  if (o->vectorize && o->guard_pages && TP("vec_search", pass_vec_search(f))) {
    ir_order(f);
  }
  if (o->vectorize && o->guard_pages && TP("vectorize", pass_vectorize(f))) {
    ir_order(f);
    /* the guards a vector loop is entered by are often constants */
    if (o->fold) pass_fold(f, o);
    if (o->branch) pass_branch(f);
    if (o->dce) pass_dce(f);
    ir_order(f);
    ir_verify(f, "post-vectorize");
  }

  if (o->unroll && TP("unroll", pass_unroll(f))) {
    ir_order(f);
    TP("unroll_addr", pass_unroll_addr(f));
    if (o->fold) pass_fold(f, o);
    if (o->branch) pass_branch(f);
    if (o->cse) pass_cse(f, o);
    /* the copies of a body read what the copy before them read or wrote */
    if (o->memopt) TP("memopt", pass_memopt(f, o));
    if (o->bce) pass_bce(f, f->m);
    if (o->dce) pass_dce(f);
    prune_params(f);
    ir_order(f);
    ir_verify(f, "post-unroll");
  }
  /* once the copies' counter steps are folded into one, a loop unrolled
     by its whole trip shows that it runs once */
  if (o->unroll && TP("once_loops", pass_once_loops(f))) {
    /* the params of a header left with one way in become what flows in --
       constant counters and addresses -- and fold and memory forwarding
       need a round after that */
    ir_order(f);
    opt_cleanup(f, o);
    ir_verify(f, "post-once-loops");
  }

  if (o->strength && TP("postdec_test", pass_postdec_test(f)) && o->dce)
    pass_dce(f);
  if (prune_params(f)) ir_order(f);
  if (o->labcache) pass_lab_cache(f);

  if (o->loopclose && TP("loop_close", pass_loop_close(f))) ir_order(f);
  /* A chain rebuilt through a wrap leaves the wrap, and an extend under it,
     unread. */
  bool reassoced = o->reassoc && TP("reassoc", pass_reassoc(f));
  if (o->reassoc && TP("reassoc_carried", pass_reassoc_carried(f))) reassoced = true;
  if (reassoced && o->dce) pass_dce(f);

  if (o->barelide) TP("bar_elide", pass_bar_elide(f));
  bool brt_jumped = false;
  if (!warmc_getenv("WARMC_NO_BRT_TREE") &&
      TP("brtable_tree", pass_brtable_tree(f, &brt_jumped))) {
    ir_order(f);
    /* a table made a jump leaves its index unread */
    if (brt_jumped && o->dce) pass_dce(f);
  }
  /* Last, after the loop passes, and after the compare trees that
     pass_brtable_tree has just made.  Run before the loop passes instead --
     after the inline rounds, so that the vectoriser would see selects in
     loop bodies -- it vectorised nothing more anywhere in the ten corpora
     (vectorize_one takes a diamond's arms as guarded already, and the
     producers that leave selects to warmc leave loads in those arms), and
     timed the same: 0.999 geomean over the 183 kernels it changed. */
  if (o->ifconv && TP("ifconvert", pass_ifconvert(f)) && o->dce) {
    pass_dce(f);
    ir_order(f);
  }
  if (o->fma && TP("fma", pass_fma(f)) && o->dce) pass_dce(f);
  if (o->print_ir) ir_print(f, "optimised");
  ir_verify(f, "post-opt");
}
