/* x64 instruction selection.  Mirrors isel.c's structure: the shared
   machinery comes from isel_core.inc and regalloc.inc, the per-op
   emissions live in isel_x64_ops.inc, and this file holds the register
   convention, frame layout, call marshalling, and produce_body driver.

   Register convention (the plan file's table is the authority):
     scratches   TMP0=RAX TMP1=R10 S0=R11 S1=RDX (RDX never allocated;
                 doubles as arg + IDIV/MULX implicit); S2=RCX S3=R9 in
                 call-clobber contexts only
     pinned      vmctx=RSI membase=RDI memsize=RBX GC-LAB=R15 tid=R14
     args        RCX=vmctx, RDX,R8,R9 + XMM0-3; retarea pointer in R10
     FP scratch  XMM4-7 (internally volatile; wasm is only entered via
                 invoke/trampolines, which save the full Win64 CS set)
     CS GPRs     idx 0-7 = RSI,RDI,RBX,RBP,R12-R15; CS FPRs = XMM8-15
                 (64-bit saves)

   The internal calling convention is MS-adjacent: register meanings match
   the MS ABI, every outgoing area starts with the 32-byte shadow space,
   and internal stack args sit above it in 16-byte slots.  Runtime helpers
   are true MS ABI: positional register assignment and 8-byte stack slots.
   Frame shape: push CS GPRs, sub rsp,frame; saved_bytes = 8 + 8*ncs;
   entry = sp + frame + saved_bytes; next_pc at entry-8; k-th saved GPR at
   entry-16-8k.  rsp stays ≡ 8 (mod 16) at every entry, so frame gets 8
   bytes of padding when ncs is odd.  No stack probes: the PE header
   commits the whole stack. */

#include "ir.h"
#include "x64.h"
#include "target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REG_VMCTX   (s->vmctx_reg)
#define REG_MEMBASE (s->membase_reg)
#define REG_MEMSIZE X_RBX

#define CALL_TID_SHARE 0.02

#define S0   X_R11
#define S1   X_RDX
#define S2   X_RCX
#define S3   X_R9
#define TMP0 X_RAX
#define TMP1 X_R10
#define F0   X_XMM4
#define F1   X_XMM5
#define F2   X_XMM6
#define FTMP X_XMM7

static const int GPR_ALLOC[] = { X_RCX, X_R8, X_R9, X_RBP,
                                 X_R12, X_R13, X_R14, X_R15 };
static const int FPR_ALLOC[] = { 0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15 };
#define NGPR_ALLOC ((int)(sizeof GPR_ALLOC / sizeof GPR_ALLOC[0]))
#define NFPR_ALLOC ((int)(sizeof FPR_ALLOC / sizeof FPR_ALLOC[0]))

static bool gpr_is_callee_saved(int r) {
  return r == X_RSI || r == X_RDI || r == X_RBX || r == X_RBP ||
         (r >= X_R12 && r <= X_R15);
}
static bool fpr_is_callee_saved(int r) { return r >= 8 && r <= 15; }

#include "isel_core.inc"

static void slot_load(Isel *s, Buf *bf, int fp, int vec, int reg, uint32_t off) {
  (void)s;
  if (vec) x64_vmovdqu_load(bf, reg, X_RSP, X64_NOIDX, 0, (int32_t)off);
  else if (fp) x64_vmovs_load(bf, 1, reg, X_RSP, X64_NOIDX, 0, (int32_t)off);
  else x64_load64(bf, reg, X_RSP, X64_NOIDX, 0, (int32_t)off);
}

static void slot_store(Isel *s, Buf *bf, int fp, int vec, int reg, uint32_t off) {
  (void)s;
  if (vec) x64_vmovdqu_store(bf, reg, X_RSP, X64_NOIDX, 0, (int32_t)off);
  else if (fp) x64_vmovs_store(bf, 1, reg, X_RSP, X64_NOIDX, 0, (int32_t)off);
  else x64_store64(bf, reg, X_RSP, X64_NOIDX, 0, (int32_t)off);
}

static void emit_reg_move(Buf *out, int dst, int src, int fp, int wide,
                          int vec) {
  (void)wide; (void)vec;
  if (fp) x64_vmovdqa_rr(out, dst, src);
  else x64_mov_rr(out, 1, dst, src);
}

static int isel_cs_index(int r) {
  switch (r) {
    case X_RSI: return 0;
    case X_RDI: return 1;
    case X_RBX: return 2;
    case X_RBP: return 3;
    default: return (r >= X_R12 && r <= X_R15) ? r - X_R12 + 4 : -1;
  }
}

/* R10 is free during parallel copies: the retarea pointer is only placed
   there after argument marshalling, and the prologue stores the incoming
   one before the parameter copy. */
static int isel_break_reg(int bank) { return bank ? F2 : TMP1; }

/* RSI and RDI hold the instance and memory base in every function. */
static int isel_spare_gprs(Isel *s, int *out) {
  (void)s; (void)out;
  return 0;
}

static bool isel_fp_remat_ok(Isel *s, const Inst *in) {
  (void)in;
  return s->pool_ok;   /* one RIP-relative pool load */
}

#define RA_HAVE_RETAREA_REUSE 0
#define RA_RETAREA_REG 0
#define RA_ALLOC_RES_REG X_RAX
#define RA_HAVE_PIECES 0
#define RA_HAVE_RECOMP 0
#define RA_FMA_TIED 1
#define RA_ZERO_REG 0
#define RA_EXTENDU_TIED 0
#define RA_CS_TOP 0
#define RA_MEMOP_PRESERVES 0

static const int ARG_GPR[4] = { X_RCX, X_RDX, X_R8, X_R9 };

/* Once the argument registers run out, arguments go on the stack in the
   Win64 layout -- 8 bytes each, in parameter order, starting past the
   32-byte shadow area.  That is deliberate rather than incidental: a host C
   function bound as a wasm import is entered by this very sequence, so
   matching the platform ABI is what lets rt/wasi.c be plain C.  A v128 has
   no by-value Win64 form, so warmc gives it a 16-byte slot of its own,
   aligned like the shadow-area base it counts from; no host import takes
   one.  (a64 has room for eight of each in registers and uses a uniform
   16-byte slot beyond that -- see ARG_STACK_SLOT.) */
#define X64_ARG_SLOT 8u

static int32_t arg_stack_at(int32_t *cursor, bool vec) {
  if (vec) *cursor = (*cursor + 15) & ~15;
  int32_t at = *cursor;
  *cursor += vec ? 16 : (int32_t)X64_ARG_SLOT;
  return at;
}

static void emit_teardown(Isel *s);
static void emit_epilogue(Isel *s);

/* The stack arguments this function's own calls write past the shadow
   space, sized by them rather than by the module's widest signature: a wasm
   call's class by class as emit_call_args_core places them, RDX-R9 and
   XMM0-3 first; a runtime helper's by position, the first three taken by
   the instance and immediates at worst.  One that calls a wasm function at
   all also makes room for the stack arguments of any tail call (tail_bytes),
   which the callee writes where its own arrived. */
static uint32_t call_out_bytes(IrFunc *f, uint32_t tail_bytes) {
  uint32_t need = 0;
  bool wasm_call = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      uint16_t op = f->insts[v].op;
      if (!op_clobbers_caller_saved(op) || op == I_RETURN_CALL ||
          op == I_RETURN_CALL_INDIRECT || op == I_RETURN_CALL_REF)
        continue;
      bool wc = op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF;
      wasm_call |= wc;
      uint32_t nargs = f->insts[v].nargs, bytes = 0;
      if (wc) {
        int32_t cursor = 0;
        uint32_t ints = 0, flts = 0;
        for (uint32_t j = 0; j < nargs; j++) {
          Ref a = ir_args(f, v)[j];
          bool fp = is_fp_value(f, a), vec = is_vec_value(f, a);
          bool on_stack = fp ? flts >= 4 : ints >= 3;
          if (fp) flts++; else ints++;
          if (on_stack) bytes = (uint32_t)(arg_stack_at(&cursor, vec) +
                                           (vec ? 16 : (int32_t)X64_ARG_SLOT));
        }
      } else if (3 + nargs > 4) {
        bytes = 8u * (3 + nargs - 4);
      }
      if (bytes > need) need = bytes;
    }
  }
  if (wasm_call && tail_bytes > need) need = tail_bytes;
  return (need + 15u) & ~15u;
}
static void emit_block(Isel *s, uint32_t bi, uint32_t next_bi);

/* ---- relocations, branches, traps ----------------------------------- */

static void emit_call_sym(Isel *s, const char *name) {
  uint32_t sym = obj_intern_sym(s->o, name, SYM_UNDEF_FUNC, -1, 0);
  x64_call_rel32(B);
  isel_reloc(s, REL_PC32, B->len - 4, sym, -4);
}

/* Branch sites are the offset just past the jcc/jmp; the rel32 field is
   the 4 bytes before it. */
static void br_patch(Isel *s, size_t site, size_t target) {
  x64_patch_rel32(B, site - 4, target);
}

static size_t br_cc(Isel *s, int kind, int cc, int zw, int zreg) {
  if (kind == 1) { x64_test_rr(B, zw, zreg, zreg); cc = XCC_E; }
  else if (kind == 2) { x64_test_rr(B, zw, zreg, zreg); cc = XCC_NE; }
  x64_jcc_rel32(B, cc);
  return B->len;
}

static void emit_trap(Isel *s, int code) {
  x64_mov_ri(B, 0, X_RCX, (uint64_t)(uint32_t)code);
  emit_call_sym(s, "warmc_trap");
  x64_int3(B);
}

static void trap_if_cc(Isel *s, int cc, int code) {
  x64_jcc_rel32(B, cc);
  trap_defer(s, B->len, false, code);
}

static void trap_if_zero(Isel *s, int w, int reg, int code) {
  x64_test_rr(B, w, reg, reg);
  trap_if_cc(s, XCC_E, code);
}

/* ---- the constant pool (RIP-relative, always in reach) --------------- */

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

static void pool_fix(Isel *s, uint32_t idx, bool vec) {
  struct PoolFix *p = xmalloc(sizeof *p);
  p->site = B->len; p->idx = idx; p->vec = vec; p->next = s->poolfix;
  s->poolfix = p;
}

static void pool_load(Isel *s, int wide, int fr, uint64_t bits) {
  uint32_t idx = pool_ref(s, wide ? bits : (uint64_t)(uint32_t)bits);
  x64_vmovs_load(B, wide, fr, X64_RIPREL, X64_NOIDX, 0, 0);
  pool_fix(s, idx, false);
}

static uint32_t vpool_ref(Isel *s, uint64_t lo, uint64_t hi) {
  for (uint32_t i = 0; i < s->nvpool; i++)
    if (s->vpool[i].lo == lo && s->vpool[i].hi == hi) return i;
  if (s->nvpool == s->cap_vpool) {
    s->cap_vpool = s->cap_vpool ? s->cap_vpool * 2 : 4;
    s->vpool = xrealloc(s->vpool, s->cap_vpool * sizeof *s->vpool);
  }
  s->vpool[s->nvpool].lo = lo;
  s->vpool[s->nvpool].hi = hi;
  s->vpool[s->nvpool].at = 0;
  return s->nvpool++;
}

/* A 128-bit constant, RIP-relative out of the function's own pool.  The
   SIMD lowerings need masks that no register sequence can produce in one
   instruction, so this path is not gated on -fcpool the way scalar
   constant loads are; the load is unaligned, so pool placement is free. */
static void vconst(Isel *s, int fr, uint64_t lo, uint64_t hi) {
  uint32_t idx = vpool_ref(s, lo, hi);
  x64_vmovdqu_load(B, fr, X64_RIPREL, X64_NOIDX, 0, 0);
  pool_fix(s, idx, true);
}

/* the same value in every lane of width 8 << sz */
static void vsplat_const(Isel *s, int fr, int sz, uint64_t lane_bits) {
  uint64_t w = lane_bits;
  for (int bits = 8 << sz; bits < 64; bits *= 2) w |= w << bits;
  vconst(s, fr, w, w);
}

static void load_fconst(Isel *s, int wide, int fr, uint64_t bits) {
  if ((wide ? bits : (bits & 0xffffffffu)) == 0) {
    x64_vxorps(B, fr, fr, fr);
    return;
  }
  if (s->pool_ok) { pool_load(s, wide, fr, bits); return; }
  x64_mov_ri(B, wide, TMP0, bits);
  x64_movd_gx(B, wide, fr, TMP0);
}

/* ---- value access ---------------------------------------------------- */

static Ref addr_index(Isel *s, IrFunc *f, Ref addr) {
  return (s->zx_fuse && s->zx_fuse[addr]) ? ir_args(f, addr)[0] : addr;
}

static void load_vconst(Isel *s, int fr, uint64_t lo, uint64_t hi);

static int mem_base_reg(Isel *s, uint64_t k, int scratch);

static int mem_base_reg_fwd(Isel *s, uint64_t k, int scratch) {
  return mem_base_reg(s, k, scratch);
}

/* x86-64 has no zero register */
static int rd_zr(Isel *s, Ref v, int scratch) { return rd(s, v, scratch); }

static int rd(Isel *s, Ref v, int scratch) {
  int l = s->loc[v];
  if (l == LOC_NONE) {
    Inst *in = &s->f->insts[v];

    if (in->op == I_MEM_BASE) {
      int mb = mem_base_reg_fwd(s, in->imm2, TMP0);
      x64_lea(B, 1, scratch, mb, X64_NOIDX, 0, (int32_t)in->imm);
      return scratch;
    }
    if (in->op == I_GLOBAL_GET) {
      uint32_t off = VMCTX_GLOBALS + GLOBAL_SLOT_SIZE * (uint32_t)in->imm;
      if ((ValType)in->type == VT_V128)
        x64_vmovdqu_load(B, scratch, REG_VMCTX, X64_NOIDX, 0, (int32_t)off);
      else if (vt_is_float((ValType)in->type))
        x64_vmovs_load(B, is64(s->f, v), scratch, REG_VMCTX, X64_NOIDX, 0,
                       (int32_t)off);
      else if (is64(s->f, v))
        x64_load64(B, scratch, REG_VMCTX, X64_NOIDX, 0, (int32_t)off);
      else
        x64_load32(B, scratch, REG_VMCTX, X64_NOIDX, 0, (int32_t)off);
      return scratch;
    }
    if (in->op != I_CONST)
      warmc_fatal("internal: value v%u has no location", v);
    if ((ValType)in->type == VT_V128) {
      load_vconst(s, scratch, in->imm, in->imm2);
      return scratch;
    }
    if (vt_is_float((ValType)in->type)) {
      load_fconst(s, is64(s->f, v), scratch, in->imm);
    } else {
      x64_mov_ri(B, is64(s->f, v), scratch, in->imm);
    }
    return scratch;
  }
  if (!IS_SLOT(l)) return l;
  slot_load(s, B, is_fp_value(s->f, v), is_vec_value(s->f, v), scratch,
            slot_off(s, SLOT_OF(l)));
  g_stats.reloads++;
  return scratch;
}

static void reload_size(Isel *s) {
  if (s->mem_valid || !s->needs_memsize) return;
  x64_load64(B, REG_MEMSIZE, REG_VMCTX, X64_NOIDX, 0, VMCTX_MEM_SIZE);
  s->mem_valid = true;
}

/* dst := dst OP src via two-address x64 forms: pick the destination, copy
   x into it, and return it.  When the picked register aliases the second
   operand, fall back to the scratch so the copy cannot clobber it. */
static int rmw2(Isel *s, Ref v, int x, int y, int scratch) {
  int d = wr(s, v, scratch);
  if (d == y && d != x) d = scratch;
  if (d != x) x64_mov_rr(B, 1, d, x);
  return d;
}

/* ---- vmctx / memory addressing --------------------------------------- */

static int vmctx_at(Isel *s, uint32_t off, int size, uint32_t *disp,
                    int scratch) {
  (void)size;
  if (off <= 0x7fffffffu) {
    *disp = off;
    return REG_VMCTX;
  }
  x64_mov_ri(B, 1, scratch, off);
  x64_add_rr(B, 1, scratch, REG_VMCTX);
  *disp = 0;
  return scratch;
}

static uint32_t mem_slot_off(IrFunc *f, uint64_t k) {
  return VMCTX_MEMS(f->m->nglobals, f->m->nimported_funcs, f->m->ntables) +
         MEM_SLOT_SIZE * (uint32_t)k;
}

static int mem_base_reg(Isel *s, uint64_t k, int scratch) {
  if (k == 0) return REG_MEMBASE;
  uint32_t disp;
  int base = vmctx_at(s, mem_slot_off(s->f, k) + MEM_SLOT_BASE, 3, &disp, scratch);
  x64_load64(B, scratch, base, X64_NOIDX, 0, (int32_t)disp);
  return scratch;
}

/* ---- typed loads/stores (base + idx*2^scale + disp) ------------------ */

static void gload(Buf *b, int size, int sgn, int wide, int d, int base,
                  int idx, int scl, int32_t disp) {
  switch (size) {
    case 0:
      if (sgn) x64_load8sx(b, wide, d, base, idx, scl, disp);
      else x64_load8zx(b, 0, d, base, idx, scl, disp);
      break;
    case 1:
      if (sgn) x64_load16sx(b, wide, d, base, idx, scl, disp);
      else x64_load16zx(b, 0, d, base, idx, scl, disp);
      break;
    case 2:
      if (sgn && wide) x64_load32sx(b, d, base, idx, scl, disp);
      else x64_load32(b, d, base, idx, scl, disp);
      break;
    default:
      x64_load64(b, d, base, idx, scl, disp);
  }
}

static void gstore(Buf *b, int size, int src, int base, int idx, int scl,
                   int32_t disp) {
  switch (size) {
    case 0: x64_store8(b, src, base, idx, scl, disp); break;
    case 1: x64_store16(b, src, base, idx, scl, disp); break;
    case 2: x64_store32(b, src, base, idx, scl, disp); break;
    default: x64_store64(b, src, base, idx, scl, disp);
  }
}

/* size 2 = f32, 3 = f64, 4 = v128; 0/1 are the narrow v128 loads, which
   go through a zero-extending scalar load (upper lanes zero, matching the
   a64 ldr b/h semantics). */
static void fload(Buf *b, int size, int d, int base, int idx, int scl,
                  int32_t disp) {
  if (size == 4) x64_vmovdqu_load(b, d, base, idx, scl, disp);
  else if (size >= 2) x64_vmovs_load(b, size == 3, d, base, idx, scl, disp);
  else {
    if (size == 0) x64_load8zx(b, 0, TMP0, base, idx, scl, disp);
    else x64_load16zx(b, 0, TMP0, base, idx, scl, disp);
    x64_movd_gx(b, 0, d, TMP0);
  }
}

static void fstore(Buf *b, int size, int src, int base, int idx, int scl,
                   int32_t disp) {
  if (size == 4) x64_vmovdqu_store(b, src, base, idx, scl, disp);
  else x64_vmovs_store(b, size == 3, src, base, idx, scl, disp);
}

/* ---- call argument marshalling --------------------------------------- */

/* Internal convention (ms=false): class-wise like the a64 backend --
   int args take RCX,RDX,R8,R9 from first_pos, fp args take XMM0-3, and
   overflow goes to 16-byte slots above the 32-byte shadow space.  Runtime
   helpers (ms=true) are true MS ABI: positional assignment, 8-byte stack
   slots.  Stack args are written RSP-relative (base_disp covers the tail
   call's frame+saved offset), so no base register is consumed. */
static void emit_call_args_core(Isel *s, const Ref *args, uint32_t nargs,
                                int32_t base_disp, int first_pos,
                                bool want_vmctx, bool ms) {
  IrFunc *f = s->f;
  Move mv[24];
  int nmv = 0;

  if (ms) {
    for (uint32_t i = 0; i < nargs; i++) {
      Ref v = args[i];
      int fp = is_fp_value(f, v), wide = is64(f, v);
      if (is_vec_value(f, v))
        warmc_fatal("internal: v128 argument in an MS-ABI helper call");
      int pos = first_pos + (int)i;
      int l = s->loc[v];
      if (pos < 4) {
        mv[nmv++] = (Move){ l, fp ? pos : ARG_GPR[pos], (uint8_t)fp,
                            (uint8_t)(fp ? wide : 1), 0, 0, 0 };
        continue;
      }
      int32_t at = base_disp + 32 + 8 * (pos - 4);
      if (!base_disp && (uint32_t)at + 8u > s->out_bytes)
        warmc_fatal("internal: a stack argument past %s's outgoing area",
                    f->m->funcs[f->func_index].sym);
      if (fp) {
        int r = IS_SLOT(l)
            ? (slot_load(s, B, 1, 0, FTMP, slot_off(s, SLOT_OF(l))), FTMP) : l;
        x64_vmovs_store(B, wide, r, X_RSP, X64_NOIDX, 0, at);
      } else {
        int r = IS_SLOT(l)
            ? (slot_load(s, B, 0, 0, TMP0, slot_off(s, SLOT_OF(l))), TMP0) : l;
        x64_store64(B, r, X_RSP, X64_NOIDX, 0, at);
      }
    }
  } else {
    int next_int = first_pos, next_flt = 0;
    int32_t cursor = 0;
    int ints = 0, flts = 0;
    for (uint32_t i = 0; i < nargs; i++) {
      Ref v = args[i];
      int fp = is_fp_value(f, v), wide = is64(f, v), vec = is_vec_value(f, v);
      int l = s->loc[v];
      bool on_stack = fp ? (flts >= 4) : (ints >= 4 - first_pos);
      if (fp) flts++; else ints++;
      if (!on_stack) continue;
      int32_t at = base_disp + 32 + arg_stack_at(&cursor, vec);
      if (!base_disp && (uint32_t)at + (vec ? 16u : X64_ARG_SLOT) > s->out_bytes)
        warmc_fatal("internal: a stack argument past %s's outgoing area",
                    f->m->funcs[f->func_index].sym);
      if (vec) {
        int r = IS_SLOT(l)
            ? (slot_load(s, B, 1, 1, FTMP, slot_off(s, SLOT_OF(l))), FTMP) : l;
        x64_vmovdqu_store(B, r, X_RSP, X64_NOIDX, 0, at);
      } else if (fp) {
        int r = IS_SLOT(l)
            ? (slot_load(s, B, 1, 0, FTMP, slot_off(s, SLOT_OF(l))), FTMP) : l;
        x64_vmovs_store(B, wide, r, X_RSP, X64_NOIDX, 0, at);
      } else {
        int r = IS_SLOT(l)
            ? (slot_load(s, B, 0, 0, TMP0, slot_off(s, SLOT_OF(l))), TMP0) : l;
        if (wide) x64_store64(B, r, X_RSP, X64_NOIDX, 0, at);
        else x64_store32(B, r, X_RSP, X64_NOIDX, 0, at);
      }
    }
    for (uint32_t i = 0; i < nargs; i++) {
      Ref v = args[i];
      int fp = is_fp_value(f, v), wide = is64(f, v), vec = is_vec_value(f, v);
      int l = s->loc[v];
      if (fp) {
        if (next_flt >= 4) { next_flt++; continue; }
        mv[nmv++] = (Move){ l, next_flt++, 1, (uint8_t)wide, (uint8_t)vec, 0, 0 };
      } else {
        if (next_int > 3) { next_int++; continue; }
        mv[nmv++] = (Move){ l, ARG_GPR[next_int++], 0, (uint8_t)wide, 0, 0, 0 };
      }
    }
  }
  if (want_vmctx) mv[nmv++] = (Move){ REG_VMCTX, X_RCX, 0, 1, 0, 0, 0 };
  parallel_copy(s, mv, nmv);
}

static void emit_call_args(Isel *s, const Ref *args, uint32_t nargs) {
  emit_call_args_core(s, args, nargs, 0, 1, true, false);
}

static void emit_call_args_novmctx(Isel *s, const Ref *args, uint32_t nargs) {
  emit_call_args_core(s, args, nargs, 0, 1, false, false);
}

static void emit_call_ret(Isel *s, Ref call, CompType *sig) {
  s->mem_valid = false;
  if (sig->nresults == 1)
    commit(s, call, vt_is_vec_reg(sig->results[0]) ? X_XMM0 : X_RAX);
}

/* Placed AFTER marshalling (R10 is the parallel-copy cycle breaker). */
static void emit_ret_area(Isel *s, CompType *sig) {
  if (sig->nresults <= 1) return;
  x64_lea(B, 1, X_R10, X_RSP, X64_NOIDX, 0, (int32_t)s->retarea_off);
}

/* One MS-positional immediate argument: a register for positions 0-3, an
   8-byte stack slot above the shadow space past that. */
static void helper_imm_at(Isel *s, int pos, uint64_t imm) {
  if (pos < 4) {
    x64_mov_ri(B, 1, ARG_GPR[pos], imm);
    return;
  }
  x64_mov_ri(B, 1, TMP0, imm);
  x64_store64(B, TMP0, X_RSP, X64_NOIDX, 0, 32 + 8 * (pos - 4));
}

static void call_helper_imm(Isel *s, const char *name, const Ref *args,
                            uint32_t nargs, uint64_t imm, Ref result,
                            bool has_result) {
  csa_save(s, s->cur_inst);
  emit_call_args_core(s, args, nargs, 0, 1, true, true);
  helper_imm_at(s, 1 + (int)nargs, imm);
  emit_call_sym(s, name);
  note_safepoint(s, s->cur_inst);
  s->mem_valid = false;
  if (has_result) commit(s, result, X_RAX);
  csa_restore(s, s->cur_inst);
}

static void call_helper_imm_first(Isel *s, const char *name, const Ref *args,
                                  uint32_t nargs, uint64_t imm, Ref result,
                                  bool has_result) {
  csa_save(s, s->cur_inst);
  emit_call_args_core(s, args, nargs, 0, 2, true, true);
  helper_imm_at(s, 1, imm);
  emit_call_sym(s, name);
  note_safepoint(s, s->cur_inst);
  s->mem_valid = false;
  if (has_result) commit(s, result, X_RAX);
  csa_restore(s, s->cur_inst);
}

static void call_helper_imm2_first(Isel *s, const char *name, const Ref *args,
                                   uint32_t nargs, uint64_t imm, uint64_t imm2,
                                   Ref result, bool has_result) {
  csa_save(s, s->cur_inst);
  emit_call_args_core(s, args, nargs, 0, 3, true, true);
  helper_imm_at(s, 1, imm);
  helper_imm_at(s, 2, imm2);
  emit_call_sym(s, name);
  note_safepoint(s, s->cur_inst);
  s->mem_valid = false;
  if (has_result) commit(s, result, X_RAX);
  csa_restore(s, s->cur_inst);
}

static void call_helper_imm2(Isel *s, const char *name, const Ref *args,
                             uint32_t nargs, uint64_t imm, uint64_t imm2) {
  csa_save(s, s->cur_inst);
  emit_call_args_core(s, args, nargs, 0, 1, true, true);
  helper_imm_at(s, 1 + (int)nargs, imm);
  helper_imm_at(s, 2 + (int)nargs, imm2);
  emit_call_sym(s, name);
  note_safepoint(s, s->cur_inst);
  s->mem_valid = false;
  csa_restore(s, s->cur_inst);
}

/* ---- indirect-call lookup -------------------------------------------- */

/* The funcref cell survives argument marshalling in R11 (S0); the code
   pointer and instance are reloaded from it right before the call, so no
   extra register has to live across the copy. */

/* The lookup and type check run BEFORE argument marshalling, so unlike
   the a64 backend (whose S2/S3 are never allocated) they must stay inside
   the non-allocatable scratches RAX/RDX/R10/R11: an argument or a
   caller-saved-around-call value may still be sitting in RCX/R8/R9. */
static void emit_type_check(Isel *s, const CompType *want) {
  int want_reg = (s->tid_reg && want->id == s->tid_val) ? s->tid_reg : -1;
  if (want->final_) {
    x64_load64(B, TMP1, S0, X64_NOIDX, 0, FUNCREF_TYPE);
    if (want_reg >= 0) {
      x64_cmp_rr(B, 1, TMP1, want_reg);
    } else {
      x64_mov_ri(B, 1, TMP0, want->id);
      x64_cmp_rr(B, 1, TMP1, TMP0);
    }
    trap_if_cc(s, XCC_NE, TRAP_INDIRECT_TYPE);
    return;
  }
  x64_load64(B, TMP1, S0, X64_NOIDX, 0, FUNCREF_RTT);
  x64_load64(B, S1, TMP1, X64_NOIDX, 0, 0);
  x64_cmp_ri(B, 1, S1, (int32_t)(want->depth + 1u));
  trap_if_cc(s, XCC_B, TRAP_INDIRECT_TYPE);
  x64_load64(B, S1, TMP1, X64_NOIDX, 0, (int32_t)(8u + 8u * want->depth));
  if (want_reg >= 0) {
    x64_cmp_rr(B, 1, S1, want_reg);
  } else {
    x64_mov_ri(B, 1, TMP0, want->id);
    x64_cmp_rr(B, 1, S1, TMP0);
  }
  trap_if_cc(s, XCC_NE, TRAP_INDIRECT_TYPE);
}

static bool table_fixed_len(const Module *m, uint32_t ti, uint64_t *out) {
  const Table *t = &m->tables[ti];
  if (t->imported || !t->has_max || t->max != t->min) return false;
  *out = t->min;
  return true;
}

static void emit_indirect_lookup(Isel *s, Inst *in, Ref callee) {
  IrFunc *f = s->f;
  int idx = rd(s, callee, S1);
  uint64_t flen;
  bool fixed = table_fixed_len(f->m, (uint32_t)in->imm2, &flen) &&
               flen <= 0x7fffffffull;
  if (in->imm2 == 0) {
    x64_load64(B, S0, REG_VMCTX, X64_NOIDX, 0, VMCTX_TABLE_BASE);
    if (!fixed) x64_load64(B, TMP0, REG_VMCTX, X64_NOIDX, 0, VMCTX_TABLE_LEN);
  } else {
    uint32_t off = VMCTX_TABLES(f->m->nglobals, f->m->nimported_funcs) +
                   TABLE_SLOT_SIZE * (uint32_t)in->imm2;
    uint32_t disp;
    int base = vmctx_at(s, off, 3, &disp, TMP1);
    x64_load64(B, S0, base, X64_NOIDX, 0, (int32_t)(disp + TABLE_SLOT_ENTRIES));
    if (!fixed)
      x64_load64(B, TMP0, base, X64_NOIDX, 0, (int32_t)(disp + TABLE_SLOT_LEN));
  }

  bool i64 = f->m->tables[in->imm2].index64;
  if (fixed) {
    x64_cmp_ri(B, i64 ? 1 : 0, idx, (int32_t)flen);
    trap_if_cc(s, XCC_AE, TRAP_INDIRECT_OOB);
  } else {
    /* len <= idx traps; a 32-bit index has zero upper bits, so the
       64-bit compare covers both index widths. */
    x64_cmp_rr(B, 1, TMP0, idx);
    trap_if_cc(s, XCC_BE, TRAP_INDIRECT_OOB);
  }
  x64_lea(B, 1, S0, S0, idx, 3, 0);
  x64_load64(B, S0, S0, X64_NOIDX, 0, 0);
  trap_if_zero(s, 1, S0, TRAP_INDIRECT_NULL);
  emit_type_check(s, &f->m->types[in->imm]);
}

static void emit_ref_lookup(Isel *s, Ref callee) {
  int cell = rd(s, callee, S0);
  if (cell != S0) x64_mov_rr(B, 1, S0, cell);
  trap_if_zero(s, 1, S0, TRAP_NULL_REF);
}

/* References wear their GC colour in the top byte.  AArch64 dereferences
   them as they are (top-byte-ignore); x64 must mask the colour off first,
   so every GC-object access goes through a canonicalized copy in a
   scratch.  The copy also serves as the barrier arm's base: it can never
   alias the destination register. */
static void emit_gc_canon(Isel *s, int into, int x) {
  x64_mov_rr(B, 1, into, x);
  x64_shl_ri(B, 1, into, 8);
  x64_shr_ri(B, 1, into, 8);
}

/* An I_REF_SLOT's test of the reference in x, as the a64 backend's
   emit_slot_test makes it: falls through when it passes, traps with the
   given code when it fails, or given -1 jumps instead, through the rel32
   sites it returns in fail[] (at most three). */
static int emit_slot_test(Isel *s, const Inst *in, int x, int trap, size_t *fail) {
  uint32_t lo, hi, nslots;
  bool exact;
  if (!codegen_rtest_slots(s->f->m, in->imm, in->imm2, &lo, &hi, &exact, &nslots) ||
      !exact)
    warmc_fatal("internal: a slot test of a type no run of slots decides");
  int nf = 0;
  bool nullable = (RTEST_KIND(in->imm) & RTK_NULLABLE) != 0;
  size_t to_pass = 0;
  if (RTEST_KIND(in->imm) & RTK_NULLTRAP) {
    trap_if_zero(s, 1, x, TRAP_NULL_REF);
  } else if (nullable) {
    x64_test_rr(B, 1, x, x);
    x64_jcc_rel32(B, XCC_E);
    to_pass = B->len;
  } else if (trap >= 0) {
    trap_if_zero(s, 1, x, trap);
  } else {
    x64_test_rr(B, 1, x, x);
    x64_jcc_rel32(B, XCC_E);
    fail[nf++] = B->len;
  }
  if (lo > hi) {
    if (trap >= 0) {
      emit_trap(s, trap);
    } else {
      x64_jmp_rel32(B);
      fail[nf++] = B->len;
    }
  } else {
    if (!s->f->m->gc_no_i31) {
      x64_test_ri(B, 1, x, 3);
      if (trap >= 0) {
        trap_if_cc(s, XCC_NE, trap);
      } else {
        x64_jcc_rel32(B, XCC_NE);
        fail[nf++] = B->len;
      }
    }
    emit_gc_canon(s, TMP0, x);
    x64_load32(B, TMP0, TMP0, X64_NOIDX, 0, GC_HDR_WORD_OFF);
    x64_load32(B, TMP1, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_HDR);
    x64_sub_rr(B, 0, TMP0, TMP1);
    int cc = XCC_NE;
    if (lo != hi) {
      if (lo) x64_sub_ri(B, 0, TMP0, (int32_t)lo);
      x64_cmp_ri(B, 0, TMP0, (int32_t)(hi - lo));
      cc = XCC_A;
    } else {
      x64_cmp_ri(B, 0, TMP0, (int32_t)lo);
    }
    if (trap >= 0) {
      trap_if_cc(s, cc, trap);
    } else {
      x64_jcc_rel32(B, cc);
      fail[nf++] = B->len;
    }
  }
  if (to_pass) br_patch(s, to_pass, B->len);
  return nf;
}

/* ---- the GC allocation fast path ------------------------------------- */

#define GC_FASTNEW_MAX 256u

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

  /* Only the non-allocatable scratches: the slow arm still has to read
     caller-saved-around-call values and helper arguments out of RCX/R8/R9
     when it runs. */
  bool cached = (s->f->insts[v].imm2 & GCA_CACHED) != 0;
  x64_load64(B, S0, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_LAB);

  int cur = cached ? s->lab_reg : S1;
  if (!cached) x64_load64(B, S1, S0, X64_NOIDX, 0, 0);
  x64_load64(B, TMP0, S0, X64_NOIDX, 0, 8);
  x64_lea(B, 1, TMP1, cur, X64_NOIDX, 0, (int32_t)total);
  x64_cmp_rr(B, 1, TMP1, TMP0);
  size_t bslow = br_cc(s, 0, XCC_A, 0, 0);
  if (!cached) x64_store64(B, TMP1, S0, X64_NOIDX, 0, 0);

  /* cur wears the good colour (the result must); the header stores need
     the canonical address.  S0 is dead once the cell is written back. */
  emit_gc_canon(s, S0, cur);
  x64_load32(B, TMP0, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_HDR);
  if (type_idx) x64_add_ri(B, 0, TMP0, (int32_t)((uint32_t)type_idx << GC_TYPE_SHIFT));
  x64_store32(B, TMP0, S0, X64_NOIDX, 0, 0);
  if (arr_len > 0) {
    x64_mov_ri(B, 0, TMP0, (uint64_t)arr_len);
    x64_store32(B, TMP0, S0, X64_NOIDX, 0, 4);
  }
  x64_mov_rr(B, 1, X_RAX, cur);
  if (cached) x64_mov_rr(B, 1, s->lab_reg, TMP1);

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
  commit(s, v, X_RAX);
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
      x64_load64(B, TMP0, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_LAB);
      x64_store64(B, s->lab_reg, TMP0, X64_NOIDX, 0, 0);
    }
    emit_call_args_core(s, af->args, af->nargs, 0, 2, true, true);
    helper_imm_at(s, 1, af->type_idx);
    emit_call_sym(s, af->helper);
    note_safepoint(s, af->inst);
    s->cur_inst = saved;

    if (af->cached) {
      /* NOT TMP0: the helper's result is still in RAX and stays there
         until the jump back lands on the fast path's commit move. */
      x64_load64(B, TMP1, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_LAB);
      x64_load64(B, s->lab_reg, TMP1, X64_NOIDX, 0, 0);
    }
    csa_restore(s, af->inst);
    x64_jmp_rel32(B);
    br_patch(s, B->len, back_to);
    xfree(af);
    af = n;
  }
  s->allocs = NULL;
}

/* ---- the GC load barrier ---------------------------------------------
   Stub contract (start_x64.S): address in R10, value in/out in R11,
   everything else preserved.  The out-of-line arm runs immediately on the
   taken branch, so scratches are still live there. */

static void emit_gc_load_barrier(Isel *s, int dst, int base, int off_reg,
                                 uint32_t off, Ref mask) {
  int m;
  if (mask != REF_NONE) {
    m = rd(s, mask, TMP1);
  } else {
    x64_load64(B, TMP1, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_BADMASK);
    m = TMP1;
  }
  x64_test_rr(B, 1, dst, m);
  struct BarFix *bf = xmalloc(sizeof *bf);
  bf->site = br_cc(s, 0, XCC_NE, 0, 0);
  bf->back = B->len;
  bf->dst = dst; bf->base = base; bf->off_reg = off_reg; bf->off = off;
  bf->next = s->bars;
  s->bars = bf;
}

static void emit_gc_load_stubs(Isel *s) {
  for (struct BarFix *bf = s->bars; bf;) {
    struct BarFix *n = bf->next;
    br_patch(s, bf->site, B->len);
    if (bf->off_reg >= 0)
      x64_lea(B, 1, X_R10, bf->base, bf->off_reg, 0, (int32_t)bf->off);
    else if (bf->off || bf->base != X_R10)
      x64_lea(B, 1, X_R10, bf->base, X64_NOIDX, 0, (int32_t)bf->off);
    if (bf->dst != X_R11) x64_mov_rr(B, 1, X_R11, bf->dst);
    emit_call_sym(s, "warmc_gc_load_stub");
    if (bf->dst != X_R11) x64_mov_rr(B, 1, bf->dst, X_R11);
    x64_jmp_rel32(B);
    br_patch(s, B->len, bf->back);
    xfree(bf);
    bf = n;
  }
  s->bars = 0;
}

/* ---- immediates / fusion analysis ------------------------------------ */

static bool sext32_ok(uint64_t m, int sf) {
  if (!sf) return true;
  return (uint64_t)(int64_t)(int32_t)m == m;
}

/* ---- byte shuffles ---------------------------------------------------------

   i8x16.shuffle names each of the 16 bytes of its result by its place in x
   (0-15) or y (16-31).  pshufb makes any of one vector's, its places in a
   register (a constant loaded), and two vectors' takes two of them and an
   or.  x86 has one instruction for each of the shapes that occur, most
   with the places in an immediate: dwords of one vector in any order
   (pshufd), its low or high four words (pshuflw, pshufhw), two dwords of
   each of two (shufps), a lane of one repeated (vpbroadcast), two
   vectors' low or high lanes interleaved at any size (punpckl, punpckh),
   16 bytes of them at an offset (palignr), and words or dwords of each in
   place (pblendw, pblendd), a dword of one into another (insertps).
   xs_plan takes one of those, else two, the
   second of the first's result, and only else pshufb. */
enum { XS_MOV, XS_PSHUFD, XS_PSHUFLW, XS_PSHUFHW, XS_BCAST, XS_SHUFPS, XS_UNPCKL,
       XS_UNPCKH, XS_PALIGNR, XS_PBLENDW, XS_PBLENDD, XS_INSERTPS };
enum { XSK_PERM, XSK_PSHUFB };

/* op, on lanes of 1 << e bytes where it has lanes; a and b its operands: 0
   x, 1 y, 2 the first step's result; imm its immediate */
typedef struct { uint8_t op, e, a, b, imm; } XsStep;

/* XSK_PERM: st[0..n) in turn, the first of two into a scratch register;
   XSK_PSHUFB: of x, of y or (n == 2) of both.  cost: instructions, twice */
typedef struct { uint8_t kind, n, cost; XsStep st[2]; } XsPlan;

static void xs_apply(const XsStep *st, const uint8_t *A, const uint8_t *Bm, uint8_t *out) {
  uint32_t E = 1u << st->e, imm = st->imm;
  switch (st->op) {
    case XS_MOV: memcpy(out, A, 16); break;
    case XS_PSHUFD:
      for (uint32_t i = 0; i < 4; i++) memcpy(out + 4 * i, A + 4 * (imm >> (2 * i) & 3), 4);
      break;
    case XS_PSHUFLW:
      for (uint32_t i = 0; i < 4; i++) memcpy(out + 2 * i, A + 2 * (imm >> (2 * i) & 3), 2);
      memcpy(out + 8, A + 8, 8);
      break;
    case XS_PSHUFHW:
      memcpy(out, A, 8);
      for (uint32_t i = 0; i < 4; i++) memcpy(out + 8 + 2 * i, A + 8 + 2 * (imm >> (2 * i) & 3), 2);
      break;
    case XS_BCAST:
      for (uint32_t i = 0; i < 16; i++) out[i] = A[i % E];
      break;
    case XS_SHUFPS:
      for (uint32_t i = 0; i < 4; i++)
        memcpy(out + 4 * i, (i < 2 ? A : Bm) + 4 * (imm >> (2 * i) & 3), 4);
      break;
    case XS_UNPCKL: case XS_UNPCKH: {
      uint32_t half = st->op == XS_UNPCKH ? 8 : 0;
      for (uint32_t L = 0; L < 16 / E; L++)
        memcpy(out + L * E, (L & 1 ? Bm : A) + half + L / 2 * E, E);
      break;
    }
    case XS_PALIGNR:
      for (uint32_t i = 0; i < 16; i++) {
        uint32_t j = i + imm;
        out[i] = j < 16 ? Bm[j] : A[j - 16];
      }
      break;
    case XS_INSERTPS:
      memcpy(out, A, 16);
      memcpy(out + 4 * (imm >> 4 & 3), Bm + 4 * (imm >> 6 & 3), 4);
      break;
    case XS_PBLENDW: case XS_PBLENDD: {
      uint32_t W = st->op == XS_PBLENDW ? 2 : 4;
      for (uint32_t i = 0; i < 16 / W; i++)
        memcpy(out + W * i, (imm >> i & 1 ? Bm : A) + W * i, W);
      break;
    }
  }
}

/* Whether T's lanes of W bytes are each some lane of P (pick: the
   immediate's two bits a lane) -- of lanes lo..hi of T from P's lanes lo..
   (each four lanes at most) */
static bool xs_lanes(const uint8_t *T, const uint8_t *P, uint32_t W, uint32_t lo, uint32_t n,
                     uint32_t *pick) {
  *pick = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t j = 0;
    while (j < 4 && memcmp(T + W * (lo + i), P + W * lo + W * j, W)) j++;
    if (j == 4) return false;
    *pick |= j << (2 * i);
  }
  return true;
}

/* One step that makes T of the operands id[0..nid) (their bytes M[id]),
   reading need where need >= 0 */
static bool xs_one(const uint8_t *T, const uint8_t (*M)[16], const uint8_t *id, uint32_t nid,
                   int need, XsStep *out) {
  uint8_t r[16];
  uint32_t pick;
  for (uint32_t p = 0; p < nid; p++) {
    uint8_t a = id[p];
    const uint8_t *P = M[a];
    if (need < 0 || a == need) {
      XsStep st = { XS_MOV, 0, a, a, 0 };
      if (!memcmp(T, P, 16)) { *out = st; return true; }
      if (xs_lanes(T, P, 4, 0, 4, &pick)) { XsStep s1 = { XS_PSHUFD, 2, a, a, (uint8_t)pick }; *out = s1; return true; }
      if (!memcmp(T + 8, P + 8, 8) && xs_lanes(T, P, 2, 0, 4, &pick)) {
        XsStep s1 = { XS_PSHUFLW, 1, a, a, (uint8_t)pick }; *out = s1; return true;
      }
      if (!memcmp(T, P, 8) && xs_lanes(T, P, 2, 4, 4, &pick)) {
        XsStep s1 = { XS_PSHUFHW, 1, a, a, (uint8_t)pick }; *out = s1; return true;
      }
      for (uint32_t e = 0; e < 4; e++) {
        XsStep s1 = { XS_BCAST, (uint8_t)e, a, a, 0 };
        xs_apply(&s1, P, P, r);
        if (!memcmp(T, r, 16)) { *out = s1; return true; }
      }
    }
    for (uint32_t q = 0; q < nid; q++) {
      uint8_t b = id[q];
      const uint8_t *Q = M[b];
      if (need >= 0 && a != need && b != need) continue;
      uint32_t p2;
      if (xs_lanes(T, P, 4, 0, 2, &pick) && xs_lanes(T + 8, Q, 4, 0, 2, &p2)) {
        XsStep s1 = { XS_SHUFPS, 2, a, b, (uint8_t)(pick | p2 << 4) }; *out = s1; return true;
      }
      for (uint32_t e = 0; e < 4; e++)
        for (uint8_t op = XS_UNPCKL; op <= XS_UNPCKH; op++) {
          XsStep s1 = { op, (uint8_t)e, a, b, 0 };
          xs_apply(&s1, P, Q, r);
          if (!memcmp(T, r, 16)) { *out = s1; return true; }
        }
      for (uint32_t k = 1; k < 16; k++) {
        XsStep s1 = { XS_PALIGNR, 0, a, b, (uint8_t)k };
        xs_apply(&s1, P, Q, r);
        if (!memcmp(T, r, 16)) { *out = s1; return true; }
      }
      /* P but for one dword, which is one of Q's */
      uint32_t diff = 0, at = 0;
      for (uint32_t i = 0; i < 4; i++)
        if (memcmp(T + 4 * i, P + 4 * i, 4)) { diff++; at = i; }
      if (diff == 1)
        for (uint32_t j = 0; j < 4; j++)
          if (!memcmp(T + 4 * at, Q + 4 * j, 4)) {
            XsStep s1 = { XS_INSERTPS, 2, a, b, (uint8_t)(j << 6 | at << 4) };
            *out = s1;
            return true;
          }
      for (uint32_t W = 4; W >= 2; W -= 2) {
        uint32_t imm = 0;
        bool ok = true;
        for (uint32_t i = 0; i < 16 / W && ok; i++) {
          if (!memcmp(T + W * i, P + W * i, W)) continue;
          if (!memcmp(T + W * i, Q + W * i, W)) imm |= 1u << i;
          else ok = false;
        }
        if (ok) {
          XsStep s1 = { W == 4 ? XS_PBLENDD : XS_PBLENDW, (uint8_t)(W == 4 ? 2 : 1), a, b, (uint8_t)imm };
          *out = s1;
          return true;
        }
      }
    }
  }
  return false;
}

/* The first steps a two-step plan may start from, of x and y */
static uint32_t xs_firsts(const uint8_t (*M)[16], uint32_t nsrc, XsStep *out) {
  uint32_t n = 0;
  for (uint8_t a = 0; a < nsrc; a++) {
    for (uint32_t imm = 0; imm < 256; imm++) {
      XsStep s1 = { XS_PSHUFD, 2, a, a, (uint8_t)imm }, s2 = { XS_PSHUFLW, 1, a, a, (uint8_t)imm },
             s3 = { XS_PSHUFHW, 1, a, a, (uint8_t)imm };
      out[n++] = s1; out[n++] = s2; out[n++] = s3;
    }
    for (uint8_t e = 0; e < 4; e++) { XsStep s1 = { XS_BCAST, e, a, a, 0 }; out[n++] = s1; }
    for (uint8_t b = 0; b < nsrc; b++) {
      for (uint32_t imm = 0; imm < 256; imm++) { XsStep s1 = { XS_SHUFPS, 2, a, b, (uint8_t)imm }; out[n++] = s1; }
      for (uint8_t e = 0; e < 4; e++)
        for (uint8_t op = XS_UNPCKL; op <= XS_UNPCKH; op++) { XsStep s1 = { op, e, a, b, 0 }; out[n++] = s1; }
      for (uint8_t k = 1; k < 16; k++) { XsStep s1 = { XS_PALIGNR, 0, a, b, k }; out[n++] = s1; }
      if (a == b) continue;
      for (uint32_t imm = 1; imm < 255; imm++) { XsStep s1 = { XS_PBLENDW, 1, a, b, (uint8_t)imm }; out[n++] = s1; }
      for (uint32_t imm = 1; imm < 15; imm++) { XsStep s1 = { XS_PBLENDD, 2, a, b, (uint8_t)imm }; out[n++] = s1; }
    }
  }
  return n;
}

/* The cheapest plan for want (one: x and y one value, places mod 16) */
static void xs_plan(const uint8_t *want0, bool one, XsPlan *best) {
  uint8_t want[16], M[3][16];
  uint32_t nsrc = one ? 1 : 2, from = 0;
  for (uint32_t i = 0; i < 16; i++) {
    want[i] = one ? want0[i] & 15 : want0[i];
    from |= want[i] < 16 ? 1u : 2u;
    M[0][i] = (uint8_t)i;
    M[1][i] = (uint8_t)(16 + i);
  }
  memset(best, 0, sizeof *best);
  /* pshufb: of one (its places loaded), or of each and an or */
  best->kind = XSK_PSHUFB;
  best->n = from == 3 ? 2 : 1;
  best->st[0].a = from == 2;
  best->cost = from == 3 ? 10 : 5;
  uint8_t id[3] = { 0, 1, 2 };
  XsStep st;
  if (xs_one(want, (const uint8_t (*)[16])M, id, nsrc, -1, &st)) {
    best->kind = XSK_PERM; best->n = 1; best->cost = 2; best->st[0] = st;
    return;
  }
  static _Thread_local XsStep first[2 * (3 * 256 + 4 + 2 * (256 + 8 + 15 + 254 + 14))];
  uint32_t nf = xs_firsts((const uint8_t (*)[16])M, nsrc, first);
  uint8_t ids[3] = { 2, 0, 1 };
  for (uint32_t i = 0; i < nf; i++) {
    xs_apply(&first[i], M[first[i].a], M[first[i].b], M[2]);
    if (xs_one(want, (const uint8_t (*)[16])M, ids, nsrc + 1, 2, &st)) {
      best->kind = XSK_PERM; best->n = 2; best->cost = 4;
      best->st[0] = first[i]; best->st[1] = st;
      return;
    }
  }
}

/* xs_plan of shuffle v, kept by its places */
static void xs_plan_of(IrFunc *f, Ref v, XsPlan *p) {
  const Inst *in = &f->insts[v];
  uint8_t want[16];
  for (uint32_t i = 0; i < 16; i++)
    want[i] = (uint8_t)((i < 8 ? in->imm >> (8 * i) : in->imm2 >> (8 * (i - 8))) & 0x1f);
  bool one = ir_args(f, v)[0] == ir_args(f, v)[1];
  static _Thread_local struct { uint64_t lo, hi; uint32_t key; XsPlan p; } memo[64];
  uint32_t key = one ? 3u : 1u;
  uint32_t h = (uint32_t)((in->imm * 0x9e3779b97f4a7c15ull ^ in->imm2) >> 58);
  if (memo[h].key == key && memo[h].lo == in->imm && memo[h].hi == in->imm2) {
    *p = memo[h].p;
    return;
  }
  xs_plan(want, one, p);
  memo[h].lo = in->imm; memo[h].hi = in->imm2; memo[h].key = key; memo[h].p = *p;
}

static void analyse_immediates(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  s->imm_fold = xcalloc(n ? n : 1, 1);
  s->fused = xcalloc(n ? n : 1, 1);
  s->pool_ok = s->opt->cpool != 0;
  s->remat_fp = !warmc_getenv("WARMC_NO_REMAT_FP");
  s->remat_v128 = false;
  {
    const char *rc = warmc_getenv("WARMC_REMAT_COST");
    s->remat_cost = rc ? atoi(rc) : 2;
    if (s->remat_cost < 1) s->remat_cost = 1;
    if (s->remat_cost > 4) s->remat_cost = 4;
    const char *rw = warmc_getenv("WARMC_REMAT2_W");
    s->remat2_w = rw ? atof(rw) : 0.5;
    const char *rs = warmc_getenv("WARMC_REMAT2_SEG");
    s->remat2_seg = rs ? atof(rs) : 2.0;
    s->remat_glob = !warmc_getenv("WARMC_NO_REMAT_GLOBAL");
  }
  s->no_remat = xcalloc(n ? n : 1, 1);
  s->paired = xcalloc(n ? n : 1, 1);
  s->no_ldpair = true;
  s->folded = xcalloc(n ? n : 1, 1);
  uint32_t *uses = xcalloc(n ? n : 1, sizeof(uint32_t));
  s->uses = uses;
  uint8_t *blocked = xcalloc(n ? n : 1, 1);

  s->no_imm = warmc_getenv("WARMC_NO_IMM") != NULL;
  if (!s->no_imm)
  for (uint32_t i = 0; i < n; i++)
    if (f->insts[i].op == I_CONST && fits_gpr(f, i))
      s->imm_fold[i] = 1;

  bool no_edge_remat = warmc_getenv("WARMC_NO_EDGE_REMAT") != NULL;
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
                        in->op == I_REF_TEST || in->op == I_ANY_OF_EXTERN ||
                        in->op == I_EXTERN_OF_ANY);
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
        /* An edge makes a rematerialised argument in its target's location
           (see parallel_copy_to); a landing pad's are stored before the
           call, where the scratch registers may still be in use. */
        if (b->succs[sidx].eh || no_edge_remat)
          s->no_remat[b->succs[sidx].args[j]] = 1;
      }
  }
  for (uint32_t i = 0; i < n; i++) if (blocked[i]) s->imm_fold[i] = 0;

  /* The index vectors the IR gives a shuffle, for a64's tbl: x86 makes
     pshufb's own, so those only shuffles read are not made. */
  {
    uint32_t *seen = xcalloc(n ? n : 1, sizeof(uint32_t));
    for (int pass = 0; pass < 2; pass++)
      for (uint32_t oi = 0; oi < f->norder; oi++) {
        Block *b = &f->blocks[f->order[oi]];
        for (uint32_t i = 0; i < b->ninsts; i++) {
          Ref u = b->insts[i];
          if (f->insts[u].op != I_VSHUFFLE) continue;
          Ref *a = ir_args(f, u);
          for (uint32_t j = 2; j < f->insts[u].nargs; j++) {
            if (pass == 0) seen[a[j]]++;
            else if (seen[a[j]] == uses[a[j]] && !blocked[a[j]]) s->folded[a[j]] = 1;
          }
        }
      }
    xfree(seen);
  }

  /* [membase + index*1 + disp32] folds a single-use MEM_ADDR directly. */
  s->addr_fuse = xcalloc(n ? n : 1, 1);
  {
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
        if (f->insts[ea].imm > 0x7fffffffull) continue;
        if (!warmc_getenv("WARMC_NO_ADDRFUSE")) { s->addr_fuse[ea] = 1; s->folded[ea] = 1; }
      }
    }
    xfree(addr_only);
  }

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
      /* a folded-away op keeps its operands but reads none of them; only
         dce takes it out of the block, and -fno-dce leaves it there */
      if (in->op == I_NOP) continue;
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

  /* A single-use compare feeding a SELECT folds into its cmov. */
  if (!warmc_getenv("WARMC_NO_FUSE"))
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

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (b->term != T_BRIF) continue;
    Ref c = b->term_arg;
    if (uses[c] != 1) continue;
    if (f->insts[c].block != bi) continue;
    uint16_t op = f->insts[c].op;
    bool cmpish = (op >= I_EQ && op <= I_EQZ) || (op >= I_FEQ && op <= I_FGE);
    if (cmpish && !warmc_getenv("WARMC_NO_FUSE")) s->fused[c] = 1;
  }

  /* A cast's slot test, which only its trap reads, jumps to the trap itself
     (I_TRAP_IF). */
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      if (f->insts[u].op != I_TRAP_IF || f->insts[u].nargs != 1) continue;
      Ref c = ir_args(f, u)[0];
      if (f->insts[c].op != I_REF_SLOT || uses[c] != 1 || blocked[c]) continue;
      if (f->insts[c].block != f->order[oi] || s->folded[c]) continue;
      s->folded[c] = 1;
    }
  }

  /* AND feeding only a zero-test becomes TEST. */
  if (!warmc_getenv("WARMC_NO_TSTFUSE"))
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
      if (f->insts[cand].block != bi) continue;
      Ref *ca = ir_args(f, cand);
      if (s->folded[ca[0]] || s->folded[ca[1]]) continue;
      if (vt_is_64((ValType)f->insts[cand].type) !=
          vt_is_64((ValType)f->insts[ir_args(f, u)[0]].type)) continue;
      int sf = vt_is_64((ValType)f->insts[cand].type) ? 1 : 0;
      bool second_ok = f->insts[ca[1]].op != I_CONST ||
                       sext32_ok(sf ? f->insts[ca[1]].imm
                                    : (f->insts[ca[1]].imm & 0xffffffffu), sf);
      if (!second_ok) continue;
      s->folded[cand] = 1;
    }
  }

  s->bfx = xcalloc(n ? n : 1, sizeof *s->bfx);
  xfree(blocked);
}

/* ---- prologue / epilogue --------------------------------------------- */

static const int CS_GPR[] = { X_RSI, X_RDI, X_RBX, X_RBP,
                              X_R12, X_R13, X_R14, X_R15 };
#define NCS_GPR ((int)(sizeof CS_GPR / sizeof CS_GPR[0]))

static int cs_gpr_list(Isel *s, int *list) {
  int n = 0;
  for (int i = 0; i < NCS_GPR; i++) {
    int r = CS_GPR[i];
    bool needed;
    if (r == X_RSI) needed = s->needs_vmctx;
    else if (r == X_RDI) needed = s->needs_mem;
    else if (r == X_RBX) needed = s->needs_memsize ||
                                  (s->used_gpr_cs & (1u << X_RBX)) != 0;
    else needed = (s->used_gpr_cs & (1u << r)) != 0;
    if (needed) list[n++] = r;
  }
  return n;
}

static int cs_fpr_list(Isel *s, int *list) {
  int n = 0;
  for (int r = 8; r <= 15; r++) if (s->used_fpr_cs & (1u << r)) list[n++] = r;
  return n;
}

static void emit_prologue(Isel *s, Buf *b) {
  IrFunc *f = s->f;

  int list[NCS_GPR];
  int n = cs_gpr_list(s, list);
  for (int i = 0; i < n; i++) x64_push(b, list[i]);
  /* A page at a time from the top, so that each touch meets the guard page
     the last one moved down; RAX is free until the stack check below. */
  if (g_stack_probes)
    for (uint32_t off = 4096; off - 4096 < s->frame && s->frame >= 4096; off += 4096)
      x64_load32(b, TMP0, X_RSP, X64_NOIDX, 0,
                 -(int32_t)(off < s->frame ? off : s->frame));
  if (s->frame) x64_sub_ri(b, 1, X_RSP, (int32_t)s->frame);

  int fl[8];
  int fn_ = cs_fpr_list(s, fl);
  for (int i = 0; i < fn_; i++)
    x64_vmovs_store(b, 1, fl[i], X_RSP, X64_NOIDX, 0,
                    (int32_t)(s->fpsave_off + 8u * (uint32_t)i));

  if (s->has_call) {
    /* vmctx is still in RCX here, whether or not it moves to RSI. */
    x64_load64(b, TMP0, X_RCX, X64_NOIDX, 0, 48);
    x64_cmp_rr(b, 1, X_RSP, TMP0);
    x64_jcc_rel32(b, XCC_A);
    size_t site = b->len;
    x64_mov_ri(b, 0, X_RCX, TRAP_STACK_OVERFLOW);
    uint32_t sym = obj_intern_sym(s->o, "warmc_trap", SYM_UNDEF_FUNC, -1, 0);
    x64_call_rel32(b);
    isel_reloc(s, REL_PC32, (b->len - 4) | 0x8000000000000000ull, sym, -4);
    x64_int3(b);
    x64_patch_rel32(b, site - 4, b->len);
  }
  if (s->needs_vmctx) x64_mov_rr(b, 1, REG_VMCTX, X_RCX);

  if (f->sig->nresults > 1)
    x64_store64(b, X_R10, X_RSP, X64_NOIDX, 0, (int32_t)s->retptr_slot);

  Block *e = &f->blocks[f->entry];

  /* This code keeps every i32 in a register zero-extended: each 32-bit
     operation makes it so.  An i32 parameter a C caller passes -- through
     warmc_invoke, to an export or to a function whose reference the host
     holds -- has whatever high half its register had, which the C
     convention leaves to the caller, so a function C can reach makes its
     own zero before anything reads one.  Those on the stack are loaded as
     32 bits already. */
  if (f->m->escapes && f->func_index < f->m->nfuncs && f->m->escapes[f->func_index]) {
    int ni = 1;
    for (uint32_t i = 0; i < f->sig->nparams && ni <= 3; i++) {
      ValType t = f->sig->params[i];
      if (vt_is_vec_reg(t)) continue;
      int src = ARG_GPR[ni++];
      if (t == VT_I32 && s->loc[e->params[i]] != LOC_NONE) x64_mov_rr(b, 0, src, src);
    }
  }

  bool clash = false;
  {
    int ni = 1, nf = 0;
    int dst[64], nd = 0, src_of[64];
    for (uint32_t i = 0; i < f->sig->nparams && i < 64; i++) {
      ValType t = f->sig->params[i];
      int fp = vt_is_vec_reg(t);
      int src = -1;
      if (fp) { if (nf < 4) src = nf++; }
      else    { if (ni <= 3) src = ARG_GPR[ni++]; }
      if (src < 0) continue;
      int l = s->loc[e->params[i]];
      if (l == LOC_NONE) continue;
      /* A param bound for a spill slot is stored from its register where
         the loop below meets it, so it is a reader too: a move ahead of
         it that writes that register is a clash, and the moves then wait
         for the parallel copy, after every store. */
      dst[nd] = IS_SLOT(l) ? -1 : l; src_of[nd] = src; nd++;
    }
    for (int x = 0; x < nd && !clash; x++)
      for (int y = x + 1; y < nd; y++)
        if (dst[x] == src_of[y]) { clash = true; break; }
  }

  /* Two passes.  A stack parameter's allocated home may be one of the
     incoming argument registers (RCX/R8/R9 and XMM0-3 are all in the
     allocation pools), so every register-to-register move -- which reads
     those registers -- must be finished before any stack parameter lands
     in one.  Pass 1 does the register params (and any stores to spill
     slots, whose sources nothing here clobbers); pass 2 loads stack
     params straight into their homes. */
  int next_int = 1, next_flt = 0;
  int32_t cursor = 0;

  Move pmv[16];
  int npmv = 0;
  for (uint32_t i = 0; i < f->sig->nparams; i++) {
    Ref p = e->params[i];
    ValType t = f->sig->params[i];
    bool vec = (t == VT_V128);
    int wide = vt_is_64(t), fp = vt_is_vec_reg(t);
    int src;

    int32_t at = 0;
    bool from_stack = false;
    if (fp) {
      if (next_flt < 4) src = next_flt++;
      else { src = FTMP; from_stack = true; }
    } else {
      if (next_int <= 3) src = ARG_GPR[next_int++];
      else { src = TMP0; from_stack = true; }
    }
    if (from_stack)
      at = (int32_t)(s->frame + s->saved_bytes) + 32 + arg_stack_at(&cursor, vec);
    int l = s->loc[p];
    if (l == LOC_NONE) continue;
    if (from_stack) {
      if (!IS_SLOT(l)) continue;   /* register home: pass 2 */
      if (vec) x64_vmovdqu_load(b, FTMP, X_RSP, X64_NOIDX, 0, at);
      else if (fp) x64_vmovs_load(b, wide, FTMP, X_RSP, X64_NOIDX, 0, at);
      else if (wide) x64_load64(b, TMP0, X_RSP, X64_NOIDX, 0, at);
      else x64_load32(b, TMP0, X_RSP, X64_NOIDX, 0, at);
      slot_store(s, b, fp, vec, src, slot_off(s, SLOT_OF(l)));
    } else if (IS_SLOT(l)) {
      slot_store(s, b, fp, vec, src, slot_off(s, SLOT_OF(l)));
    } else if (l != src) {
      if (clash) {
        if (npmv == (int)(sizeof pmv / sizeof pmv[0]))
          warmc_fatal("internal: more parameters in registers than there are");
        pmv[npmv++] = (Move){ src, l, (uint8_t)fp, (uint8_t)(fp ? wide : 1),
                              (uint8_t)vec, 0, 0 };
      } else {
        emit_reg_move(b, l, src, fp, wide, vec);
      }
    }
  }
  if (npmv) parallel_copy_to(s, b, pmv, npmv);

  next_int = 1; next_flt = 0; cursor = 0;
  for (uint32_t i = 0; i < f->sig->nparams; i++) {
    Ref p = e->params[i];
    ValType t = f->sig->params[i];
    bool vec = (t == VT_V128);
    int wide = vt_is_64(t), fp = vt_is_vec_reg(t);

    if (fp) { if (next_flt < 4) { next_flt++; continue; } }
    else    { if (next_int <= 3) { next_int++; continue; } }
    int32_t at = (int32_t)(s->frame + s->saved_bytes) + 32 +
                 arg_stack_at(&cursor, vec);
    int l = s->loc[p];
    if (l == LOC_NONE || IS_SLOT(l)) continue;
    if (vec) x64_vmovdqu_load(b, l, X_RSP, X64_NOIDX, 0, at);
    else if (fp) x64_vmovs_load(b, wide, l, X_RSP, X64_NOIDX, 0, at);
    else if (wide) x64_load64(b, l, X_RSP, X64_NOIDX, 0, at);
    else x64_load32(b, l, X_RSP, X64_NOIDX, 0, at);
  }

  if (s->needs_mem) {
    x64_load64(b, REG_MEMBASE, REG_VMCTX, X64_NOIDX, 0, VMCTX_MEM_BASE);
    if (s->needs_memsize)
      x64_load64(b, REG_MEMSIZE, REG_VMCTX, X64_NOIDX, 0, VMCTX_MEM_SIZE);
  }
  if (s->tid_reg) x64_mov_ri(b, 1, s->tid_reg, s->tid_val);
}

static void emit_teardown(Isel *s) {
  int fl[8];
  int fn_ = cs_fpr_list(s, fl);
  for (int i = 0; i < fn_; i++)
    x64_vmovs_load(B, 1, fl[i], X_RSP, X64_NOIDX, 0,
                   (int32_t)(s->fpsave_off + 8u * (uint32_t)i));
  if (s->frame) x64_add_ri(B, 1, X_RSP, (int32_t)s->frame);
  int list[NCS_GPR];
  int n = cs_gpr_list(s, list);
  for (int i = n - 1; i >= 0; i--) x64_pop(B, list[i]);
}

static void emit_epilogue(Isel *s) {
  emit_teardown(s);
  x64_ret(B);
}

#include "isel_x64_ops.inc"

static void regalloc_naive(Isel *s);
static void regalloc_linear(Isel *s);

/* Instructions the prologue and epilogue spend on the callee-saved
   registers used so far: a push and a pop each. */
static int ra_cs_frame_ops(Isel *s) {
  int list[NCS_GPR + 1];
  return 2 * cs_gpr_list(s, list);
}

#include "regalloc.inc"

/* ---- the per-function driver ----------------------------------------- */

static int produce_body_x64(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                            uint32_t out_bytes, FuncOut *fout, bool far,
                            SplitCtl *sc, bool split_collect) {
  int split_ret = 0;
  (void)far;
  Buf pro;
  memset(&pro, 0, sizeof pro);
  Isel st;
  memset(&st, 0, sizeof st);
  Isel *s = &st;
  s->f = f; s->o = obj; s->opt = opt;
  s->split_sc = sc;
  s->split_collect = split_collect;

  /* Any call -- helpers and warmc_trap included -- may write the MS
     shadow space at the caller's [rsp, rsp+32), and MS-positional helper
     immediates can spill to 8-byte slots above it; the fixed 32-byte
     floor under the wasm out-area covers both. */
  out_bytes = call_out_bytes(f, out_bytes);
  if (out_bytes < 32) out_bytes = 32;
  s->out_bytes = out_bytes + 32;

  s->far_br = false;
  s->naive = !opt->regalloc;

  s->loc = xmalloc(f->ninsts * sizeof(int));
  s->cls = xmalloc(f->ninsts);
  for (uint32_t i = 0; i < f->ninsts; i++) {
    s->loc[i] = LOC_NONE;
    s->cls[i] = (uint8_t)(is_fp_value(f, i) ? 1 : 0);
  }

  /* No inline memory.copy/fill on x64 yet: its output only runs on
     Windows, where the suite that would check such a path runs. */
  s->no_small_memop = true;
  analyse_function(s);

  s->lab_reg = 0;
  for (uint32_t bi = 0; bi < f->nblocks && !s->lab_reg; bi++)
    for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++)
      if (f->insts[f->blocks[bi].insts[i]].op == I_GC_LAB_OPEN) {
        s->lab_reg = X_R15;
        s->used_gpr_cs |= 1u << X_R15;
        break;
      }

  s->vmctx_reg = X_RSI;
  s->membase_reg = X_RDI;

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
      int r = (s->lab_reg == X_R15) ? X_R14 : X_R15;
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

  compute_safepoints(s);

  s->slots_off = s->out_bytes;

  s->retarea_off = (s->slots_off + 8u * s->nslots + 15u) & ~15u;
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
    int list[NCS_GPR];
    int ncs = cs_gpr_list(s, list);
    /* Entry rsp ≡ 8 (mod 16), so the body is 16-aligned for calls when
       8*ncs + frame ≡ 8 (mod 16): pad the 16-aligned frame by 8 unless
       an odd push count already supplies the 8. */
    if (!(ncs & 1)) s->frame += 8;
    s->saved_bytes = 8u + 8u * (uint32_t)ncs;
  }
  if (s->frame > (1u << 27))
    warmc_fatal("function %s needs a %u byte frame, beyond the current addressing limit",
                f->m->funcs[f->func_index].sym, s->frame);
  if (s->frame > g_stats.max_frame_bytes) g_stats.max_frame_bytes = s->frame;

  s->prof_site = xcalloc(f->nblocks, sizeof(size_t));
  s->prof_start = xcalloc(f->nblocks, sizeof(size_t));
  s->block_off = xmalloc(f->nblocks * sizeof(uint32_t));
  for (uint32_t i = 0; i < f->nblocks; i++) s->block_off[i] = 0xffffffffu;

  emit_prologue(s, &pro);

  const char *lal_s = warmc_getenv("WARMC_LOOP_ALIGN");
  uint32_t lalign = lal_s ? (uint32_t)strtoul(lal_s, NULL, 0) : 16;
  if (lalign != 16 && lalign != 32) lalign = 0;

  uint32_t neo;
  const uint32_t *eo = ir_emit_order(f, UINT32_MAX, &neo);
  for (uint32_t oi = 0; oi < neo; oi++) {
    uint32_t bi = eo[oi];
    if (lalign && opt->align && f->blocks[bi].loop_depth) {
      Block *lb = &f->blocks[bi];
      bool header = false;
      for (uint32_t p = 0; p < lb->npreds && !header; p++)
        if (f->blocks[lb->preds[p]].rpo >= lb->rpo) header = true;
      if (header) {
        uint32_t pad = (uint32_t)(-(pro.len + B->len) & (lalign - 1));
        while (pad) {
          uint32_t k = pad > 15 ? 15 : pad;
          x64_nop_n(B, (int)k);
          pad -= k;
        }
      }
    }
    s->block_off[bi] = (uint32_t)B->len;
    s->mem_valid = s->mem_valid_in[bi] != 0;
    /* --profile: one bump per block, of the instructions the block runs.
       a64 patches the count into an add's imm12 and reads the block's
       length back in words; x86-64 instructions are variable length, so
       the encoder's own tally supplies the count and the immediate goes
       into a read-modify-write add -- nothing to spill around, and the
       four bytes to patch are the last four emitted.  The bump itself is
       outside the count, as it is on a64. */
    if (opt->profile) {
      x64_add_mi32(B, 1, s->vmctx_reg, X64_NOIDX, 0, 56, 0);
      s->prof_site[bi] = B->len - 4;
      s->prof_start[bi] = B->ninstr;
      s->prof_skip = 0;
    }
    emit_block(s, bi, oi + 1 < neo ? eo[oi + 1] : 0xffffffffu);
    if (opt->profile) {
      uint32_t w = (uint32_t)(B->ninstr - s->prof_start[bi]);
      w = (w > s->prof_skip) ? w - s->prof_skip : 0;
      if (bi == f->entry) w += pro.ninstr;
      memcpy(B->data + s->prof_site[bi], &w, 4);
    }
  }

  emit_alloc_slow_arms(s);
  emit_gc_load_stubs(s);

  for (struct EdgeStub *est = s->edge_stubs; est;) {
    struct EdgeStub *n = est->next;
    size_t at = B->len;
    parallel_copy(s, est->mv, est->n);
    x64_jmp_rel32(B);
    add_fix(s, B->len, 0, est->target);
    br_patch(s, est->site, at);
    xfree(est->mv); xfree(est);
    est = n;
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
    br_patch(s, t->site, trap_at[t->code]);
    xfree(t);
    t = n;
  }
  s->traps = NULL;

  for (BrFix *fx = s->fixups; fx;) {
    BrFix *n = fx->next;
    if (fx->is19 == BRFIX_TABLE) {
      int32_t w = (int32_t)((int64_t)s->block_off[fx->target] - (int64_t)fx->base);
      memcpy(B->data + fx->site, &w, 4);
    } else br_patch(s, fx->site, s->block_off[fx->target]);
    xfree(fx);
    fx = n;
  }
  s->fixups = NULL;

  if (s->npool) {
    while (B->len & 7) x64_int3(B);
    for (uint32_t i = 0; i < s->npool; i++) {
      s->pool[i].at = B->len;
      buf_u32(B, (uint32_t)s->pool[i].word);
      buf_u32(B, (uint32_t)(s->pool[i].word >> 32));
    }
  }
  if (s->nvpool) {
    while (B->len & 15) x64_int3(B);
    for (uint32_t i = 0; i < s->nvpool; i++) {
      s->vpool[i].at = B->len;
      buf_u64(B, s->vpool[i].lo);
      buf_u64(B, s->vpool[i].hi);
    }
  }
  for (struct PoolFix *p = s->poolfix; p;) {
    struct PoolFix *n = p->next;
    x64_patch_rel32(B, p->site - 4,
                    p->vec ? s->vpool[p->idx].at : s->pool[p->idx].at);
    xfree(p);
    p = n;
  }
  s->poolfix = NULL;

  bool has_loop = false;
  for (uint32_t i = 0; i < f->nblocks && !has_loop; i++)
    if (f->blocks[i].reachable && f->blocks[i].loop_depth > 0) has_loop = true;

  if (warmc_getenv("WARMC_VERIFY_RA") && s->csa_consumed != s->ncsa_vals)
    warmc_fatal("internal: %u caller-save entries built but %u emitted in %s",
                s->ncsa_vals, s->csa_consumed, f->m->funcs[f->func_index].sym);

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
  fout->insts_emitted = (uint32_t)(pro.len + s->body.len);
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
    int list[NCS_GPR];
    int ncs = cs_gpr_list(s, list);
    for (int i = 0; i < ncs; i++)
      u.saved |= 1u << (UW_GPR_SHIFT + isel_cs_index(list[i]));
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
  xfree(s->zx_fuse); xfree(s->no_remat); xfree(s->paired); xfree(s->nl); xfree(s->pool);
  xfree(s->vpool);
  xfree(s->folded); xfree(s->uses); xfree(s->bfx); xfree(s->pinned);
  xfree(s->sites); xfree(s->clauses);
  xfree(s->gcsites); xfree(s->gcroots);
  xfree(s->sp_first); xfree(s->sp_n); xfree(s->sp_vals);
  xfree(s->csa_slot); xfree(s->csa_on);
  xfree(s->csa_first); xfree(s->csa_n); xfree(s->csa_vals);
  xfree(s->lv_in); xfree(s->lv_out); xfree(s->lv_in_off); xfree(s->lv_out_off);
  if (split_ret) return split_ret;
  return 1;
}

/* ---- the x64 target descriptor --------------------------------------- */

static bool imm_ok_x64(IrFunc *f, Ref user, uint32_t idx, uint64_t cval,
                       ValType ct) {
  Inst *in = &f->insts[user];
  int sf = vt_is_64(ct) ? 1 : 0;
  uint64_t masked = sf ? cval : (cval & 0xffffffffu);
  switch (in->op) {
    case I_ADD: case I_SUB: case I_MUL:
    case I_AND: case I_OR: case I_XOR:
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU:
      return idx == 1 && sext32_ok(masked, sf);
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTR: case I_ROTL:
      return idx == 1;
    case I_SELECT: {
      if (idx > 1 || vt_is_float(ct)) return false;
      if (masked == 0) return true;
      Ref other = ir_args(f, user)[idx ^ 1u];
      if (f->insts[other].op != I_CONST) return false;
      uint64_t om = sf ? f->insts[other].imm : (f->insts[other].imm & 0xffffffffu);
      if (om != 0) return false;
      return masked == 1 || masked == (sf ? ~0ull : 0xffffffffu);
    }
    case I_LOAD: case I_STORE:
      return idx == 0 && cval <= 0x7fffffffull;
    default:
      return false;
  }
}

static int const_cost_x64(uint64_t bits, bool wide) {
  (void)bits; (void)wide;
  return 1;   /* one mov, whatever the value */
}

static int vconst_cost_x64(uint64_t lo, uint64_t hi) {
  (void)lo; (void)hi;
  return 2;   /* never rematerialised until the SIMD backend lands */
}

static bool mem_disp_ok_x64(uint64_t off, uint32_t size_log2) {
  (void)size_log2;
  return off <= 0x7fffffffull;
}

static void pad_nop_x64(Buf *b) { x64_nop(b); }

static void emit_dead_call_x64(ObjBuilder *obj, uint32_t sym) {
  buf_u8(&obj->text, 0xe9);   /* jmp rel32 */
  obj_add_reloc(obj, REL_PC32, 0, obj->text.len, sym, -4);
  buf_u32(&obj->text, 0);
}

const Target warmc_target_x64 = {
  .name = "x64",
  .is_x64 = true,
  .imm_ok = imm_ok_x64,
  .const_cost = const_cost_x64,
  .vconst_cost = vconst_cost_x64,
  .mem_disp_ok = mem_disp_ok_x64,
  .produce_body = produce_body_x64,
  .pad_nop = pad_nop_x64,
  .emit_dead_call = emit_dead_call_x64,
  .narg_int = 4,
  .narg_fp = 4,
  .arg_slot = X64_ARG_SLOT,
  .max_frame = 1u << 27,   /* disp32 addressing: no practical frame cap */
  .vec_copies = 2,         /* sixteen xmm registers, four of them scratch */
  .vec_mul_lanes = 0x2,    /* vpmullw; vpmulld is two uops, bytes eight */
};
