#include "ir.h"
#include "a64.h"
#include "target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

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

static bool gpr_is_callee_saved(int r) { return r >= R19 && r <= R30; }
static bool fpr_is_callee_saved(int r) { return r >= 8 && r <= 15; }

#include "isel_core.inc"

/* A load or store of 1 << size bytes at sp + off, or of a q register when
   vec.  An offset past the scaled immediate's reach -- a slot in a frame
   over 32 KB, or a stack parameter above one -- goes through a register
   holding sp plus the offset's 4 KB pages: a load into a GPR builds that in
   its own destination, anything else in tmp when the caller has a register
   free, and otherwise borrows TMP0 (TMP1 when TMP0 is the value), parked in
   the frame's near save slot and put back after, so that every scratch
   register comes through as the caller left it. */
static void sp_access(Isel *s, Buf *bf, bool st, int size, int fp, int vec,
                      int reg, uint32_t off, int tmp) {
  int base = RSP, t = -1;
  bool park = false;
  if ((off >> (vec ? 4 : size)) > 0xfff) {
    if (!st && !fp && !vec) t = reg;
    else if (tmp >= 0) t = tmp;
    else {
      if (!s->far_frame)
        warmc_fatal("internal: a stack access past reach in a near frame");
      t = (!fp && !vec && reg == TMP0) ? TMP1 : TMP0;
      a64_str_imm(bf, 3, t, RSP, s->far_save);
      park = true;
    }
    a64_add_imm(bf, SF64, t, RSP, off >> 12, 1);
    base = t;
    off &= 0xfff;
  }
  if (vec) {
    if (st) a64_str_q(bf, reg, base, off);
    else a64_ldr_q(bf, reg, base, off);
  } else if (fp) {
    if (st) a64_fstr_imm(bf, size, reg, base, off);
    else a64_fldr_imm(bf, size, reg, base, off);
  } else {
    if (st) a64_str_imm(bf, size, reg, base, off);
    else a64_ldr_imm(bf, size, reg, base, off);
  }
  if (park) a64_ldr_imm(bf, 3, t, RSP, s->far_save);
}

static void slot_load(Isel *s, Buf *bf, int fp, int vec, int reg, uint32_t off) {
  sp_access(s, bf, false, 3, fp, vec, reg, off, -1);
}

/* A slot holds one value after another (share_slots), so whatever the
   reload cache keeps of it is stale once anything is stored there. */
static void slot_store(Isel *s, Buf *bf, int fp, int vec, int reg, uint32_t off) {
  if (off >= s->slots_off) rc_kill_slot(s, (off - s->slots_off) / 8u);
  sp_access(s, bf, true, 3, fp, vec, reg, off, -1);
}

static void emit_reg_move(Buf *out, int dst, int src, int fp, int wide,
                          int vec) {
  if (vec) a64_vmov(out, dst, src);
  else if (fp) a64_fmov_reg(out, wide ? FT_D : FT_S, dst, src);
  else a64_mov_reg(out, SF64, dst, src);
}

static int isel_cs_index(int r) {
  return r >= R19 && r <= R28 && gpr_is_callee_saved(r) ? r - R19 : -1;
}

static int isel_break_reg(int bank) { return bank ? F3 : TMP1; }

/* x19 and x20 hold the instance and memory base.  Pinned (codegen_pinned),
   a function is entered with them there and leaves them there for the wasm
   functions it calls, so one that reads neither itself nor through such a
   call may lend them -- the stack check reads x19 in the prologue, ahead
   of any value, and a tail call or a cold call is made after the epilogue
   has put them back.  Not pinned, a leaf moves them to x9 and x10, where
   they need no save, and a function that touches neither never loads them;
   the stack check of a function with a call reads the instance from x19
   whether or not the body does, so x19 stays put there.  Whichever of the
   two is idle is a callee-saved register like x22-x28, which the allocator
   falls back on before it spills a value read in a loop (regalloc_linear):
   one save and restore per call, against a reload on every iteration.
   BLAKE3's compressPre carries its sixteen state words, a round counter and
   a base round a loop, and spilled a temporary at almost every step of
   it. */
static int isel_spare_gprs(Isel *s, int *out) {
  int n = 0;
  if (s->pin) {
    if (!s->needs_vmctx) out[n++] = R19;
    if (!s->needs_mem && !s->calls_wasm) out[n++] = R20;
  } else {
    if (s->vmctx_reg != R19 || (!s->needs_vmctx && !s->has_call)) out[n++] = R19;
    if (s->membase_reg != R20 || !s->needs_mem) out[n++] = R20;
  }
  /* Without a call x29 and x30 are two more: x30 is read only by the ret,
     after the epilogue has restored it, and the frame record x29 would
     point to is one this code never builds -- a function with calls uses
     the pair as its reload cache instead.  Saving them costs the same one
     stp and ldp as any other pair. */
  if (!s->has_call) { out[n++] = R29; out[n++] = R30; }
  return n;
}

/* Whether the frame saves x29 and x30: for the calls, or because a leaf
   allocated them. */
static bool saves_lr(const Isel *s) {
  return s->has_call || (s->used_gpr_cs & (1u << R29 | 1u << R30)) != 0;
}

static bool isel_fp_remat_ok(Isel *s, const Inst *in) {
  uint32_t imm8;
  return a64_fmov_imm(in->imm, vt_is_64((ValType)in->type) ? FT_D : FT_S,
                      &imm8) ||
         s->pool_ok;
}

#define RA_HAVE_RETAREA_REUSE 1
#define RA_RETAREA_REG R8
#define RA_ALLOC_RES_REG R0
#define RA_HAVE_PIECES 1
#define RA_HAVE_RECOMP 1

static bool recomp_addsub_imm(uint64_t k, bool w64, uint32_t *imm, int *lsl, bool *neg);
static IrOp recomp_shape(IrFunc *f, Ref v, Ref *x, Ref *y, uint64_t *c);
#define RA_FMA_TIED 0
/* a constant zero is read as wzr/xzr where it has no register (rd_zr) */
#define RA_ZERO_REG 1
/* an extend of an i32 a w-register write made is no instruction in place */
#define RA_EXTENDU_TIED 1
/* memory.copy and memory.fill of memory 0 save nothing round them
   (regalloc.inc memop_preserves) */
#define RA_MEMOP_PRESERVES 1
/* a value live across a call in a loop takes callee-saved registers from
   x28 down (regalloc_linear's cs_top) */
#define RA_CS_TOP 1

#define A64_IDX_UXTW 2
#define A64_IDX_LSL  3
static int idx_option(IrFunc *f, Ref addr) {
  return is64(f, addr) ? A64_IDX_LSL : A64_IDX_UXTW;
}

static Ref addr_index(Isel *s, IrFunc *f, Ref addr) {
  return (s->zx_fuse && s->zx_fuse[addr]) ? ir_args(f, addr)[0] : addr;
}

static void load_vconst(Isel *s, int fr, uint64_t lo, uint64_t hi);

static int mem_base_reg_fwd(Isel *s, uint64_t k, int scratch);
static void pool_load(Isel *s, int ft, int fr, uint64_t bits);
static int vmctx_at(Isel *s, uint32_t off, int size, uint32_t *disp,
                    int scratch);

/* The register a piece holds v in at the current instruction, or -1.  A
   use-cluster piece is live only while the instruction loop of emit_block
   runs, as a terminator reads after its last read; a loop piece through
   the terminator's condition as well. */
static int piece_reg(Isel *s, Ref v) {
  if ((!s->pc_live && !s->pc_term) || !s->pc_head) return -1;
  for (int32_t k = s->pc_head[v]; k >= 0; k = s->pc_next[k]) {
    const struct Piece *p = &s->pc[k];
    if (p->block != s->cur_block) continue;
    if (p->kind == PC_LOOP) return p->reg;
    if (p->kind == PC_CLUSTER && s->pc_live && p->lo <= s->cur_pos &&
        s->cur_pos <= p->hi)
      return p->reg;
  }
  return -1;
}

/* A spilled value mark_recompute found its register operands hold at
   every read: computed again into scratch instead of reloaded. */
static int recomp_read(Isel *s, Ref v, int scratch) {
  Ref x, y;
  uint64_t c = 0;
  IrOp op = recomp_shape(s->f, v, &x, &y, &c);
  int sf = is64(s->f, v) ? SF64 : SF32;
  int xr = s->loc[x], yr = y == REF_NONE ? -1 : s->loc[y];
  uint32_t imm = 0, enc = 0;
  int lsl = 0;
  bool neg = false;
  if (yr < 0 && (op == I_ADD || op == I_SUB))
    recomp_addsub_imm(op == I_SUB ? (uint64_t)0 - c : c, sf == SF64, &imm, &lsl, &neg);
  if (yr < 0 && (op == I_AND || op == I_OR || op == I_XOR))
    a64_logical_imm(c, sf, &enc);
  int sh = (int)(c & (sf == SF64 ? 63u : 31u));
  switch (op) {
    case I_ADD: case I_SUB:
      if (yr >= 0) {
        if (op == I_ADD) a64_add_reg(B, sf, scratch, xr, yr);
        else a64_sub_reg(B, sf, scratch, xr, yr);
      } else if (neg) {
        a64_sub_imm(B, sf, scratch, xr, imm, lsl);
      } else {
        a64_add_imm(B, sf, scratch, xr, imm, lsl);
      }
      break;
    case I_AND:
      if (yr >= 0) a64_and_reg(B, sf, scratch, xr, yr); else a64_and_imm(B, sf, scratch, xr, enc);
      break;
    case I_OR:
      if (yr >= 0) a64_orr_reg(B, sf, scratch, xr, yr); else a64_orr_imm(B, sf, scratch, xr, enc);
      break;
    case I_XOR:
      if (yr >= 0) a64_eor_reg(B, sf, scratch, xr, yr); else a64_eor_imm(B, sf, scratch, xr, enc);
      break;
    case I_SHL: a64_lsl_imm(B, sf, scratch, xr, sh); break;
    case I_SHRU: a64_lsr_imm(B, sf, scratch, xr, sh); break;
    case I_SHRS: a64_asr_imm(B, sf, scratch, xr, sh); break;
    default: warmc_fatal("internal: v%u is no value to recompute", v);
  }
  g_stats.recomputes++;
  return scratch;
}

static int rd(Isel *s, Ref v, int scratch) {
  if (s->walias && s->walias[v]) return rd(s, ir_args(s->f, v)[0], scratch);
  int pr = piece_reg(s, v);
  if (pr >= 0) return pr;
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
      /* A constant that takes a movz/movk chain is built once per stretch
         the reload cache lives, into one of its registers, and read from
         there after: the allocator keeps such constants out of slots, which
         cost a chain and a store wherever LICM had put the def -- at the
         entry of musl's printf_core, forty of them on every call. */
      bool wide = is64(s->f, v);
      if (s->rc_reg[0] && !s->rc_noconst && ir_const_cost(in->imm, wide) > 1) {
        uint32_t key = RC_CONST | v;
        for (int i = 0; i < 2; i++)
          if (s->rc_slot[i] == key) { s->rc_used[i] = 1; return s->rc_reg[i]; }
        int pick = -1;
        for (int i = 0; i < 2 && pick < 0; i++)
          if (!s->rc_slot[i] && !s->rc_used[i]) pick = i;
        for (int k = 0; k < 2 && pick < 0; k++) {
          int i = (s->rc_next + k) & 1;
          if (!s->rc_used[i]) pick = i;
        }
        if (pick >= 0) {
          a64_load_imm(B, wide ? SF64 : SF32, s->rc_reg[pick], in->imm);
          s->rc_slot[pick] = key;
          s->rc_used[pick] = 1;
          s->rc_next = (uint8_t)((pick + 1) & 1);
          return s->rc_reg[pick];
        }
      }
      a64_load_imm(B, wide ? SF64 : SF32, scratch, in->imm);
    }
    return scratch;
  }
  if (!IS_SLOT(l)) return l;
  uint32_t sl = SLOT_OF(l);
  /* Read by the very next instruction after its definition stored it: the
     register it was stored from still has it.  A move is cheaper than the
     reload, which waits on the store it reads back; nothing at all is
     needed when that register is this scratch, as it often is.  Where the
     reload cache takes the value, the move fills the cache instead. */
  bool fwd = s->st_v == v && s->st_end == B->len;
  if (!fwd && s->recomp && s->recomp[v]) return recomp_read(s, v, scratch);
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
      if (fwd) {
        emit_reg_move(B, s->rc_reg[pick], s->st_reg, 0, 1, 0);
      } else {
        slot_load(s, B, 0, 0, s->rc_reg[pick], slot_off(s, sl));
        g_stats.reloads++;
      }
      s->rc_slot[pick] = sl + 1;
      s->rc_used[pick] = 1;
      s->rc_next = (uint8_t)((pick + 1) & 1);
      return s->rc_reg[pick];
    }
  }
  if (s->frc_reg[0] && is_fp_value(s->f, v) && !is_vec_value(s->f, v)) {
    for (int i = 0; i < 2; i++)
      if (s->frc_slot[i] == sl + 1) {
        s->frc_used[i] = 1;
        return s->frc_reg[i];
      }
  }
  if (fwd) {
    if (s->st_reg != scratch)
      emit_reg_move(B, scratch, s->st_reg, is_fp_value(s->f, v), 1,
                    is_vec_value(s->f, v));
    return scratch;
  }
  slot_load(s, B, is_fp_value(s->f, v), is_vec_value(s->f, v), scratch,
            slot_off(s, SLOT_OF(l)));
  g_stats.reloads++;
  return scratch;
}

/* The zero register for an integer constant 0 with no register of its
   own, where the reader takes one as its value -- a store's, a result's --
   and what rd() makes of anything else.  The allocator keeps such zeros
   out of callee-saved registers (ra_zero_const), and a store of one should
   cost no more than it did from the register it had. */
static int rd_zr(Isel *s, Ref v, int scratch) {
  const Inst *in = &s->f->insts[v];
  if (in->op == I_CONST && s->loc[v] == LOC_NONE &&
      !vt_is_float((ValType)in->type) && (ValType)in->type != VT_V128 &&
      (is64(s->f, v) ? in->imm : (in->imm & 0xffffffffu)) == 0)
    return RZR;
  return rd(s, v, scratch);
}

/* Fill a piece's register before the first read of its cluster: from the
   reload cache when it holds the slot (the value was just stored, and a load
   would wait for that store), else from the slot, else by rematerialising. */
static void piece_fill(Isel *s, const struct Piece *p) {
  Ref v = p->v;
  int l = s->loc[v];
  bool fp = is_fp_value(s->f, v), vec = is_vec_value(s->f, v);
  if (IS_SLOT(l)) {
    uint32_t sl = SLOT_OF(l);
    for (int i = 0; i < 2 && !fp; i++)
      if (s->rc_reg[i] && s->rc_slot[i] == sl + 1) {
        emit_reg_move(B, p->reg, s->rc_reg[i], 0, 1, 0);
        return;
      }
    for (int i = 0; i < 2 && fp && !vec; i++)
      if (s->frc_reg[i] && s->frc_slot[i] == sl + 1) {
        emit_reg_move(B, p->reg, s->frc_reg[i], 1, 1, 0);
        return;
      }
    slot_load(s, B, fp, vec, p->reg, slot_off(s, sl));
    g_stats.reloads++;
    return;
  }
  bool live = s->pc_live;
  s->pc_live = false;
  int r = rd(s, v, p->reg);
  s->pc_live = live;
  if (r != p->reg) emit_reg_move(B, p->reg, r, fp, is64(s->f, v), vec);
}

static void reload_size(Isel *s) {
  if (s->mem_valid || !s->needs_memsize) return;
  a64_ldr_imm(B, 3, REG_MEMSIZE, REG_VMCTX, VMCTX_MEM_SIZE);
  s->mem_valid = true;
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

/* A label bound right after a spill store is a join: the paths arriving
   there stored the value from registers of their own. */
static void label_at(Isel *s, size_t target) {
  if (target == s->st_end) s->st_end = (size_t)-1;
}

static void patch_b26_checked(Isel *s, size_t site, size_t target) {
  label_at(s, target);
  if (a64_patch_b26_ok(B, site, target)) return;
  warmc_fatal("function %u compiles to %zu bytes of code, past the range a "
              "branch can reach", s->f->func_index, B->len);
}

static void br_patch(Isel *s, size_t site, size_t target) {
  label_at(s, target);
  if (s->far_br) { patch_b26_checked(s, site, target); return; }
  if (!a64_patch_b19_ok(B, site, target)) s->far_br_needed = true;
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
  p->site = site; p->idx = idx; p->vec = false; p->next = s->poolfix;
  s->poolfix = p;
}

static bool pool_in_reach(const IrFunc *f) {
  uint32_t ni = f->made_insts ? f->made_insts : f->ninsts;
  uint64_t bound = (uint64_t)ni * 128 + (uint64_t)f->nblocks * 128;
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

/* cmp w<reg>, #imm for a ref.test's slot numbers, which a module with more
   types than an imm12 holds can exceed */
static void rtest_cmp_imm(Isel *s, int reg, uint32_t imm) {
  if (imm <= 0xfffu) {
    a64_cmp_imm(B, SF32, reg, imm);
  } else {
    a64_load_imm(B, SF32, TMP0, imm);
    a64_cmp_reg(B, SF32, reg, TMP0);
  }
}

/* An I_REF_SLOT's test of the reference in x.  Every object its module's
   code meets is its own (Module.gc_sealed), whose header holds the
   instance's type base plus the object's slot, so the slot decides: the
   test passes for a slot in the run codegen_rtest_slots gives, fails for an
   i31 and for any other slot, and passes for null when the test takes null
   -- or traps as a null dereference does, RTK_NULLTRAP's.
   The code falls through when the test passes.  When it fails it traps with
   the given code, a cast's; given -1 it branches instead, through the b19
   sites it returns in fail[] (at most three), for its caller to place. */
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
    trap_if_zero(s, SF64, x, TRAP_NULL_REF);
  } else if (nullable) {
    to_pass = B->len;
    a64_cbz(B, SF64, x, 0);
  } else if (trap >= 0) {
    trap_if_zero(s, SF64, x, trap);
  } else {
    fail[nf++] = B->len;
    a64_cbz(B, SF64, x, 0);
  }
  if (lo > hi) {
    /* no slot passes: nothing but a null does */
    if (trap >= 0) {
      emit_trap(s, trap);
    } else {
      /* b.al, a b19 site as the others are */
      fail[nf++] = B->len;
      a64_bcond(B, CC_AL, 0);
    }
  } else {
    /* an i31 is the one reference besides an object and null, and a module
       that makes none has none */
    if (!s->f->m->gc_no_i31) {
      uint32_t tst3;
      if (!a64_logical_imm(3, SF64, &tst3))
        warmc_fatal("internal: 3 is not a logical immediate");
      a64_tst_imm(B, SF64, x, tst3);
      if (trap >= 0) {
        trap_if_cc(s, CC_NE, trap);
      } else {
        fail[nf++] = B->len;
        a64_bcond(B, CC_NE, 0);
      }
    }
    a64_ldr_imm(B, 2, S2, x, GC_HDR_WORD_OFF);
    a64_ldr_imm(B, 2, S3, REG_VMCTX, VMCTX_GC_HDR);
    a64_sub_reg(B, SF32, S2, S2, S3);
    int cc = CC_NE;
    if (lo != hi) {
      /* lo <= slot <= hi as slot - lo <= hi - lo, unsigned */
      if (lo <= 0xfffu) {
        a64_sub_imm(B, SF32, S2, S2, lo, 0);
      } else {
        a64_load_imm(B, SF32, TMP0, lo);
        a64_sub_reg(B, SF32, S2, S2, TMP0);
      }
      rtest_cmp_imm(s, S2, hi - lo);
      cc = CC_HI;
    } else {
      rtest_cmp_imm(s, S2, lo);
    }
    if (trap >= 0) {
      trap_if_cc(s, cc, trap);
    } else {
      fail[nf++] = B->len;
      a64_bcond(B, cc, 0);
    }
  }
  if (nullable) a64_patch_b19(B, to_pass, B->len);
  return nf;
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

__attribute__((unused))

/* c_abi: the arguments of a C function, which takes no vmctx and has its
   stack arguments where the platform's convention puts them, each in 8
   bytes or, on Apple's, packed at its own size's alignment.  zext: a
   runtime helper's, whose u64 parameters take an i32 operand too -- a
   memory.grow's delta, a table's index -- where an i32 has its high half
   zero; a wasm function reads an i32's low half only, and a C function's
   own i32 parameter is its low half by its convention. */
static void emit_call_args_core(Isel *s, const Ref *args, uint32_t nargs,
                                int stack_base, int first_int,
                                bool want_vmctx, bool c_abi, bool zext) {
  IrFunc *f = s->f;
  int next_int = first_int, next_flt = 0, stack_slot = 0;
  uint32_t c_at = 0;
  Move mv[24];
  int nmv = 0;
  Ref late[24];
  int late_dst[24], nlate = 0;

  int ints = 0, flts = 0;
  for (uint32_t i = 0; i < nargs; i++) {
    Ref v = args[i];
    int fp = is_fp_value(f, v), wide = is64(f, v), vec = is_vec_value(f, v);
    int l = s->loc[v];
    bool on_stack = fp ? (flts >= 8) : (ints >= 8 - first_int);
    if (fp) flts++; else ints++;
    if (!on_stack) continue;
    uint32_t at = (uint32_t)stack_slot++ * ARG_STACK_SLOT;
    if (c_abi) {
      uint32_t size = warmc_darwin_abi ? (wide ? 8u : 4u) : 8u;
      c_at = (c_at + size - 1) & ~(size - 1);
      at = c_at;
      c_at += size;
    }
    /* a tail call's go where the function's own arrived, which a frameless
       one reaches from sp too */
    if (stack_base == RSP && !s->tail_args &&
        at + ARG_STACK_SLOT > s->out_bytes)
      warmc_fatal("internal: a stack argument past %s's outgoing area",
                  f->m->funcs[f->func_index].sym);
    if (vec) {
      int r = l == LOC_NONE ? rd(s, v, FTMP)
              : IS_SLOT(l) ? (slot_load(s, B, 1, 1, FTMP, slot_off(s, SLOT_OF(l))), FTMP) : l;
      a64_str_q(B, r, stack_base, at);
    } else if (fp) {
      int r = l == LOC_NONE ? rd(s, v, FTMP)
              : IS_SLOT(l) ? (slot_load(s, B, 1, 0, FTMP, slot_off(s, SLOT_OF(l))), FTMP) : l;
      a64_fstr_imm(B, wide ? 3 : 2, r, stack_base, at);
    } else {
      int r = l == LOC_NONE ? rd(s, v, TMP0)
              : IS_SLOT(l) ? (slot_load(s, B, 0, 0, TMP0, slot_off(s, SLOT_OF(l))), TMP0) : l;
      a64_str_imm(B, wide ? 3 : 2, r, stack_base, at);
    }
  }

  /* A move or a reload copies all 64 bits, so an i32 has a high half other
     than zero in its argument register where its own register or slot had
     one: a parameter has the one its C caller gave it, and a spill slot
     keeps the whole register (memop_r_operand).  zx: those to make zero. */
  int zx[8], nzx = 0;
  for (uint32_t i = 0; i < nargs; i++) {
    Ref v = args[i];
    int fp = is_fp_value(f, v), wide = is64(f, v), vec = is_vec_value(f, v);
    int l = s->loc[v];
    if (fp) {
      if (next_flt >= 8) { next_flt++; continue; }
      if (l == LOC_NONE) { late[nlate] = v; late_dst[nlate++] = next_flt++; continue; }
      mv[nmv++] = (Move){ l, next_flt++, 1, (uint8_t)wide, (uint8_t)vec, 0, 0 };
    } else {
      if (next_int > 7) { next_int++; continue; }
      if (zext && (ValType)f->insts[v].type == VT_I32 && !w_written(f, v))
        zx[nzx++] = next_int;
      if (l == LOC_NONE) { late[nlate] = v; late_dst[nlate++] = next_int++; continue; }
      mv[nmv++] = (Move){ l, next_int++, 0, (uint8_t)wide, 0, 0, 0 };
    }
  }
  if (want_vmctx) mv[nmv++] = (Move){ REG_VMCTX, R0, 0, 1, 0, 0, 0 };
  parallel_copy(s, mv, nmv);

  /* A rematerialised argument has no location to move from: it is made in
     its own register, once every move has read the register it lands in --
     or copied there, when the reload cache already holds it. */
  for (int k = 0; k < nlate; k++) {
    int r = rd(s, late[k], late_dst[k]);
    if (r != late_dst[k]) a64_mov_reg(B, SF64, late_dst[k], r);
  }
  for (int k = 0; k < nzx; k++) a64_mov_reg(B, SF32, zx[k], zx[k]);
}

static void emit_call_args_to(Isel *s, const Ref *args, uint32_t nargs,
                              int stack_base) {
  s->tail_args = true;
  emit_call_args_core(s, args, nargs, stack_base, 1, !s->pin, false, false);
  s->tail_args = false;
}

/* A call of one of the module's own functions: the instance in x0, unless
   the callee finds it pinned in x19. */
static void emit_call_args_own(Isel *s, const Ref *args, uint32_t nargs) {
  emit_call_args_core(s, args, nargs, RSP, 1, !s->pin, false, false);
}

/* a runtime helper's arguments, after the instance */
static void emit_call_args(Isel *s, const Ref *args, uint32_t nargs) {
  emit_call_args_core(s, args, nargs, RSP, 1, true, false, true);
}

static void emit_call_args_novmctx(Isel *s, const Ref *args, uint32_t nargs) {
  emit_call_args_core(s, args, nargs, RSP, 1, false, false, false);
}

static void emit_call_args_c(Isel *s, const Ref *args, uint32_t nargs) {
  emit_call_args_core(s, args, nargs, RSP, 0, false, true, false);
}

/* a runtime helper's arguments from x<first_int> */
static void emit_call_args_at(Isel *s, const Ref *args, uint32_t nargs,
                              int first_int) {
  emit_call_args_core(s, args, nargs, RSP, first_int, true, false, true);
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

  int want_reg = (s->tid_reg && !s->tid_tab && want->id == s->tid_val)
                 ? s->tid_reg : -1;
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

/* A table nothing can grow keeps its initial length: one whose maximum is
   its minimum, or a frozen table 0, which no instruction writes and no
   other module sees. */
static bool table_fixed_len(const Module *m, uint32_t ti, uint64_t *out) {
  const Table *t = &m->tables[ti];
  if (t->imported) return false;
  if ((!t->has_max || t->max != t->min) && !(ti == 0 && m->t0 && m->t0->frozen))
    return false;
  *out = t->min;
  return true;
}

static void emit_indirect_lookup_reg(Isel *s, Inst *in, int idx) {
  IrFunc *f = s->f;
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

static void emit_indirect_lookup(Isel *s, Inst *in, Ref callee) {
  emit_indirect_lookup_reg(s, in, rd(s, callee, S0));
}

static void emit_ictab_addr(Isel *s, Buf *b, uint32_t sym, int reg, bool pro) {
  uint64_t in_pro = pro ? 0x8000000000000000ull : 0;
  isel_reloc(s, REL_ADR_PAGE21, b->len | in_pro, sym, 0);
  a64_adrp(b, reg, 0);
  isel_reloc(s, REL_ADD_LO12, b->len | in_pro, sym, 0);
  a64_add_imm(b, SF64, reg, reg, 0, 0);
}

/* call_indirect through its type's code table (Module.ict), when it has
   one: the index less the window's start against the window's length, and
   the code from the slot, left in S2 as the full lookup leaves it.  Every
   function the table holds is the module's own, so the caller passes its
   own vmctx and the cell is never read.  The address is the pinned
   register's when the function's hot type is this one, as the type id was.
   An index outside the window is out of bounds when the window is the
   whole table, and otherwise goes through the full lookup out of line,
   which traps as the call always did. */
static bool emit_ictab_lookup(Isel *s, Inst *in, Ref callee) {
  const Module *m = s->f->m;
  if (!m->ict_of || in->imm2 != 0 || in->imm >= m->ntypes ||
      m->ict_of[in->imm] == UINT32_MAX)
    return false;
  uint32_t k = m->ict_of[in->imm];
  const IcTable *t = &m->ict[k];
  int idx = rd(s, callee, S0);
  int base = S1;
  if (s->tid_tab && s->tid_ict == k) base = s->tid_reg;
  else emit_ictab_addr(s, B, t->sym, S1, false);
  int x = idx;
  if (t->lo) {
    if (t->lo <= 0xfff) a64_sub_imm(B, SF32, TMP1, idx, t->lo, 0);
    else a64_sub_imm(B, SF32, TMP1, idx, t->lo >> 12, 1);
    x = TMP1;
  }
  if (t->n <= 0xfff) a64_cmp_imm(B, SF32, x, t->n);
  else a64_subs_imm(B, SF32, RZR, x, t->n >> 12, 1);
  size_t site = 0;
  if (t->whole) trap_if_cc(s, CC_HS, TRAP_INDIRECT_OOB);
  else site = br_cc(s, 0, CC_HS, 0, 0);
  a64_ldr_reg(B, 3, S2, base, x, 2, 1);
  if (!t->whole) {
    struct IctFix *fx = xmalloc(sizeof *fx);
    fx->site = site;
    fx->back = B->len;
    fx->idx = idx;
    fx->in = in;
    fx->next = s->icts;
    s->icts = fx;
  }
  return true;
}

static void emit_ictab_slow_arms(Isel *s) {
  for (struct IctFix *fx = s->icts; fx;) {
    struct IctFix *n = fx->next;
    br_patch(s, fx->site, B->len);
    emit_indirect_lookup_reg(s, fx->in, fx->idx);
    size_t site = B->len;
    a64_b(B, 0);
    patch_b26_checked(s, site, fx->back);
    xfree(fx);
    fx = n;
  }
  s->icts = NULL;
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

/* array.new_default of a length the code works out: bumped in the
   allocation buffer as a constant length's is, the size made from the
   length -- at most 2^36 bytes from a 32-bit one, so the buffer's own bound
   sends any that does not fit to the call -- and the buffer's bytes zero
   already, every element's default.  A string's characters, a list's
   backing array: TeaVM's and Kotlin's programs made most of their arrays
   through the runtime's call, javabench/strapi 800 thousand of them.  Never
   one whose loop keeps the buffer in a register (lab_fast_alloc takes
   constant lengths alone). */
static bool emit_gc_fastnew_len(Isel *s, Ref v, uint32_t type_idx, const Ref *args) {
  const Inst *in = &s->f->insts[v];
  uint32_t stride = (uint32_t)(in->imm2 & 0xff);
  uint32_t hdrsz = (uint32_t)((in->imm2 >> 8) & 0xff);
  if (!hdrsz || !stride || (stride & (stride - 1)) || stride > 16) return false;
  if (in->imm2 & GCA_CACHED) return false;
  uint32_t enc;
  if (!a64_logical_imm(~15ull, SF64, &enc)) return false;

  int len = rd(s, args[0], TMP1);
  /* the bytes: the header and the elements, a whole number of units */
  a64_load_imm(B, SF64, TMP0, hdrsz + 15u);
  a64_add_uxtw(B, TMP0, TMP0, len, __builtin_ctz(stride));
  a64_and_imm(B, SF64, TMP0, TMP0, enc);

  a64_ldr_imm(B, 3, S0, REG_VMCTX, VMCTX_GC_LAB);
  a64_ldr_imm(B, 3, S1, S0, 0);
  a64_ldr_imm(B, 3, S2, S0, 8);
  a64_add_reg(B, SF64, S3, S1, TMP0);
  a64_cmp_reg(B, SF64, S3, S2);
  size_t bslow = br_cc(s, 0, CC_HI, 0, 0);
  a64_str_imm(B, 3, S3, S0, 0);

  /* the header, then the length, through the cursor before x0 takes the
     object: the length may be in x0 */
  a64_ldr_imm(B, 2, S2, REG_VMCTX, VMCTX_GC_HDR);
  if (type_idx) {
    uint32_t off = type_idx << GC_TYPE_SHIFT;
    if (off <= 0xfffu) {
      a64_add_imm(B, SF32, S2, S2, off, 0);
    } else {
      a64_load_imm(B, SF32, S0, off);
      a64_add_reg(B, SF32, S2, S2, S0);
    }
  }
  a64_str_imm(B, 2, S2, S1, 0);
  a64_str_imm(B, 2, len, S1, 4);
  a64_mov_reg(B, SF64, R0, S1);

  struct AllocFix *af = xmalloc(sizeof *af);
  af->site = bslow;
  af->back = B->len;
  af->args = args; af->nargs = 1;
  af->helper = "warmc_array_new"; af->type_idx = type_idx;
  af->inst = s->cur_inst;
  af->cached = false;
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

/* memory.copy and memory.fill of at most 16 bytes, inline.  libc's memcpy and
   memset are these instructions, so every short string or struct copy a C
   program makes reached the runtime through a call, and nearly all of them
   are a few bytes: snprintf's pieces, a hash table's keys, a Go or Rust
   string builder's appends.  Both ranges are checked before anything is
   written, as the instruction requires, and the bytes are moved with
   overlapping accesses that read everything before writing anything, so an
   overlapping copy is still a memmove.  Anything longer or out of bounds
   takes the call, which copies or traps exactly as before.  The op stays a
   call site to the allocator, so no live value is in a register the fast
   path could disturb, and it uses only scratch.  Returns the site of the
   fast path's branch past the call, or (size_t)-1 when there is none. */
#define SMO_CONST_MAX 256u
/* emit_small_memop's answer when it did the whole op and no call follows */
#define SMO_INLINE ((size_t)-2)
static size_t emit_const_memop(Isel *s, const Ref *a, bool fill, uint32_t n, int vb,
                               bool trap);

static size_t emit_small_memop(Isel *s, const Inst *in, const Ref *a, bool fill) {
  if (!small_memop_ok(s, in, fill)) return (size_t)-1;
  uint64_t cn;
  if (const_arg(s->f, a[2], &cn) && cn >= 1 && cn <= SMO_CONST_MAX)
    return emit_const_memop(s, a, fill, (uint32_t)cn, 16, false);

  int d = rd(s, a[0], S0);
  int x = rd(s, a[1], S1);
  int n = rd(s, a[2], S2);
  size_t slow[3];
  int nslow = 0;
  a64_cmp_imm(B, SF32, n, 16);
  slow[nslow++] = br_cc(s, 0, CC_HI, 0, 0);
  a64_ldr_imm(B, 3, TMP0, REG_VMCTX, VMCTX_MEM_SIZE);
  a64_uxtw(B, S3, d);
  a64_add_uxtw(B, S3, S3, n, 0);
  a64_cmp_reg(B, SF64, S3, TMP0);
  slow[nslow++] = br_cc(s, 0, CC_HI, 0, 0);
  if (!fill) {
    a64_uxtw(B, S3, x);
    a64_add_uxtw(B, S3, S3, n, 0);
    a64_cmp_reg(B, SF64, S3, TMP0);
    slow[nslow++] = br_cc(s, 0, CC_HI, 0, 0);
  }

  size_t done[5];
  int ndone = 0;
  /* TMP0 the destination and S0 its end; for a copy TMP1 the source and S3
     its end, for a fill S1 the byte in every lane.  d and x are read before
     anything they may share a scratch register with is written. */
  a64_add_uxtw(B, TMP0, REG_MEMBASE, d, 0);
  if (fill) {
    a64_uxtb(B, S1, x);
    a64_load_imm(B, SF64, S3, 0x0101010101010101ull);
    a64_mul(B, SF64, S1, S1, S3);
  } else {
    a64_add_uxtw(B, TMP1, REG_MEMBASE, x, 0);
    a64_add_uxtw(B, S3, TMP1, n, 0);
  }
  a64_add_uxtw(B, S0, TMP0, n, 0);

  for (int size = 3; size >= 2; size--) {
    int w = 1 << size;
    a64_cmp_imm(B, SF32, n, (uint32_t)w);
    size_t smaller = br_cc(s, 0, CC_LO, 0, 0);
    if (fill) {
      a64_str_imm(B, size, S1, TMP0, 0);
      a64_stur(B, size, S1, S0, -w);
    } else {
      a64_ldr_imm(B, size, S1, TMP1, 0);
      a64_ldur(B, size, S2, S3, -w);
      a64_str_imm(B, size, S1, TMP0, 0);
      a64_stur(B, size, S2, S0, -w);
    }
    done[ndone++] = B->len;
    a64_b(B, 0);
    br_patch(s, smaller, B->len);
  }
  /* Two or three bytes are two overlapping halfwords and one byte is one,
     every address known before the length is looked at again: a store whose
     address waits on arithmetic over n makes the loads after it guess, and
     gobench/strbuild lost 5% to a first/middle/last-byte version that did. */
  s->smo_cond_zero = ndone;
  done[ndone++] = br_cc(s, 1, 0, SF32, n);
  a64_cmp_imm(B, SF32, n, 2);
  size_t one = br_cc(s, 0, CC_LO, 0, 0);
  if (fill) {
    a64_str_imm(B, 1, S1, TMP0, 0);
    a64_stur(B, 1, S1, S0, -2);
  } else {
    a64_ldr_imm(B, 1, S1, TMP1, 0);
    a64_ldur(B, 1, S2, S3, -2);
    a64_str_imm(B, 1, S1, TMP0, 0);
    a64_stur(B, 1, S2, S0, -2);
  }
  done[ndone++] = B->len;
  a64_b(B, 0);
  br_patch(s, one, B->len);
  if (!fill) a64_ldr_imm(B, 0, S1, TMP1, 0);
  a64_str_imm(B, 0, S1, TMP0, 0);
  size_t out = B->len;
  a64_b(B, 0);

  for (int i = 0; i < nslow; i++) br_patch(s, slow[i], B->len);
  /* The branches to the end are taken after the call is emitted; record
     them in the order emit_small_memop_done expects. */
  s->smo_ndone = 0;
  for (int i = 0; i < ndone; i++) s->smo_done[s->smo_ndone++] = done[i];
  s->smo_done[s->smo_ndone++] = out;
  return out;
}

/* A length the module states, up to SMO_CONST_MAX bytes: no tests on it,
   and the bytes moved in q registers.  This is how Go copies a struct --
   by value, through its stack, at every call that takes or returns one --
   and the size of most memcpy and memset calls LLVM leaves in a module.  A
   copy loads everything before it stores anything, as the dynamic path
   does, so an overlapping one is still a memmove; the op is a call site,
   so v16-v31 hold nothing live.

   Under guard pages it tests nothing either, and makes no call: the store
   that ends highest goes first, so whichever range runs past the memory
   faults -- a load, or that store -- before a byte is written, a trap as
   the instruction requires, and once that store is in, so is every other.
   Its end is at most SMO_CONST_MAX past a 32-bit address, inside the guard
   region.  The two tests and the call path were a fifth of what C's struct
   arguments cost (cbench/structval).  That store faulting writes nothing
   on Apple's cores, but AArch64 lets one that crosses into the guard page
   write the bytes before it: with -fexact-trap the tests are made.  They
   branch to the call, which traps, or with trap to the trap itself.  The
   q registers are v16 up, or vb up for an op that is no call. */
static size_t emit_const_memop(Isel *s, const Ref *a, bool fill, uint32_t n, int vb,
                               bool trap) {
  int d = rd(s, a[0], S0);
  int x = rd(s, a[1], S1);
  bool guarded = g_guard_pages && !s->opt->exact_trap;
  if (g_guard_pages && !guarded) g_stats.exact_memop_checks++;
  size_t slow[2];
  int nslow = 0;
  if (!guarded) {
    a64_ldr_imm(B, 3, TMP0, REG_VMCTX, VMCTX_MEM_SIZE);
    a64_uxtw(B, S3, d);
    a64_add_imm(B, SF64, S3, S3, n, 0);
    a64_cmp_reg(B, SF64, S3, TMP0);
    if (trap) trap_if_cc(s, CC_HI, TRAP_MEM_OOB);
    else slow[nslow++] = br_cc(s, 0, CC_HI, 0, 0);
    if (!fill) {
      a64_uxtw(B, S3, x);
      a64_add_imm(B, SF64, S3, S3, n, 0);
      a64_cmp_reg(B, SF64, S3, TMP0);
      if (trap) trap_if_cc(s, CC_HI, TRAP_MEM_OOB);
      else slow[nslow++] = br_cc(s, 0, CC_HI, 0, 0);
    }
  }
  a64_add_uxtw(B, TMP0, REG_MEMBASE, d, 0);
  if (!fill) a64_add_uxtw(B, TMP1, REG_MEMBASE, x, 0);

  if (n < 16) {
    /* Two overlapping accesses of the widest size that fits, or one. */
    int size = n >= 8 ? 3 : n >= 4 ? 2 : n >= 2 ? 1 : 0;
    int w = 1 << size;
    if (fill) {
      a64_uxtb(B, S1, x);
      a64_load_imm(B, SF64, S3, 0x0101010101010101ull);
      a64_mul(B, SF64, S1, S1, S3);
      if ((int)n > w) a64_stur(B, size, S1, TMP0, (int)n - w);
      a64_str_imm(B, size, S1, TMP0, 0);
    } else {
      a64_ldr_imm(B, size, S1, TMP1, 0);
      if ((int)n > w) a64_ldur(B, size, S2, TMP1, (int)n - w);
      if ((int)n > w) a64_stur(B, size, S2, TMP0, (int)n - w);
      a64_str_imm(B, size, S1, TMP0, 0);
    }
  } else {
    /* Pairs of q registers, then one more, then one overlapping the end:
       at most sixteen for SMO_CONST_MAX. */
    uint32_t off[16];
    int nq = 0;
    uint32_t o = 0;
    for (; o + 32 <= n; o += 32) { off[nq++] = o; off[nq++] = o + 16; }
    if (o + 16 <= n) { off[nq++] = o; o += 16; }
    if (o < n) off[nq++] = n - 16;
    if (fill) a64_vdup_gpr(B, 0, vb, x);
    for (int pass = fill ? 1 : 0; pass < 2; pass++) {
      int base = pass ? TMP0 : TMP1;
      /* the last access ends at n: stored first */
      int lim = nq;
      if (pass) {
        int r = fill ? vb : vb + nq - 1;
        if (!(off[nq - 1] & 15)) a64_str_q(B, r, base, off[nq - 1]);
        else a64_stur_q(B, r, base, (int)off[nq - 1]);
        lim = nq - 1;
      }
      for (int i = 0; i < lim; i++) {
        int r = fill ? vb : vb + i;
        bool pair = i + 1 < lim && off[i + 1] == off[i] + 16 && !(off[i] & 15);
        if (pair) {
          int r2 = fill ? vb : vb + i + 1;
          if (pass) a64_stp_q(B, r, r2, base, (int)off[i]);
          else a64_ldp_q(B, r, r2, base, (int)off[i]);
          i++;
        } else if (!(off[i] & 15)) {
          if (pass) a64_str_q(B, r, base, off[i]);
          else a64_ldr_q(B, r, base, off[i]);
        } else {
          if (pass) a64_stur_q(B, r, base, (int)off[i]);
          else a64_ldur_q(B, r, base, (int)off[i]);
        }
      }
    }
  }
  s->smo_cond_zero = -1;
  s->smo_ndone = 0;
  if (guarded || trap) return SMO_INLINE;
  size_t out = B->len;
  a64_b(B, 0);
  for (int i = 0; i < nslow; i++) br_patch(s, slow[i], B->len);
  s->smo_done[s->smo_ndone++] = out;
  return out;
}

static void emit_small_memop_done(Isel *s) {
  for (uint32_t i = 0; i < s->smo_ndone; i++) {
    if ((int)i == s->smo_cond_zero) br_patch(s, s->smo_done[i], B->len);
    else patch_b26_checked(s, s->smo_done[i], B->len);
  }
  s->smo_ndone = 0;
}

/* The operand v in the scratch register r for warmc_memmove_r or
   warmc_memset_r, its high half zero: moved there as a w register, and
   made so in place where it was reloaded there whole and a w-register write
   did not make it -- an i32 parameter's high half is whatever its C caller
   passed, and its spill slot keeps all 64 bits. */
static void memop_r_operand(Isel *s, Ref v, int r) {
  int x = rd(s, v, r);
  if (x != r || !w_written(s->f, v)) a64_mov_reg(B, SF32, r, x);
}

/* A memory.copy or memory.fill of memory 0 that saves nothing round it
   (memop_preserves): a constant length of at most MEMOP_R_CONST_MAX inline
   in v26-v31 (memop_r_inline), its bounds tests, where it makes them,
   branching to the trap; a length up to 16 inline as emit_small_memop
   makes it; and every other a bl of warmc_memmove_r or warmc_memset_r
   (rt/start.S) with the operands in x12-x14, which checks both ranges,
   traps on one past the memory, and moves the bytes in the scratch
   registers alone. */
static void emit_memop_r(Isel *s, Ref v, const Ref *a, bool fill) {
  uint64_t cn;
  bool cst = const_arg(s->f, a[2], &cn);
  if (memop_r_inline(s, v)) {
    emit_const_memop(s, a, fill, (uint32_t)cn, F0, true);
    return;
  }
  size_t site = (size_t)-1;
  if (!cst || cn <= 16) site = emit_small_memop(s, &s->f->insts[v], a, fill);
  if (site == SMO_INLINE) return;
  memop_r_operand(s, a[2], S2);
  if (fill) {
    /* the byte, of which the routine reads only the low eight bits */
    int x = rd(s, a[1], S1);
    if (x != S1) a64_mov_reg(B, SF32, S1, x);
  } else {
    memop_r_operand(s, a[1], S1);
  }
  memop_r_operand(s, a[0], S0);
  uint32_t sym = obj_intern_sym(s->o, fill ? "warmc_memset_r" : "warmc_memmove_r",
                                SYM_UNDEF_FUNC, -1, 0);
  isel_reloc(s, REL_CALL26, B->len, sym, 0);
  a64_bl(B, 0);
  if (site != (size_t)-1) emit_small_memop_done(s);
}

#define SMALL_LOOP_INSTS 12u

static bool block_calls(IrFunc *f, const Block *b) {
  for (uint32_t i = 0; i < b->ninsts; i++) {
    uint16_t op = f->insts[b->insts[i]].op;
    if (op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF) return true;
  }
  return false;
}

/* Whether the natural loop headed by h is small: its blocks, found back
   from the latches, hold at most SMALL_LOOP_INSTS IR instructions and
   terminators -- about what fits in 64 bytes once lowered -- and no call.
   A loop that calls waits on the call, not on fetching itself: the branch
   over its padding was gobench/recurse's whole gap to clang. */
static bool small_loop(IrFunc *f, uint32_t h) {
  uint32_t nb = f->nblocks, n = 0, sp = 0;
  uint8_t *in = xcalloc(nb ? nb : 1, 1);
  uint32_t *stk = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  Block *hb = &f->blocks[h];
  in[h] = 1;
  n += hb->ninsts + 1;
  for (uint32_t p = 0; p < hb->npreds; p++) {
    uint32_t l = hb->preds[p];
    if (f->blocks[l].rpo >= hb->rpo && !in[l]) { in[l] = 1; stk[sp++] = l; }
  }
  bool ok = !block_calls(f, hb);
  while (sp && ok) {
    uint32_t x = stk[--sp];
    Block *xb = &f->blocks[x];
    n += xb->ninsts + 1;
    if (n > SMALL_LOOP_INSTS || x == f->entry || block_calls(f, xb)) ok = false;
    for (uint32_t p = 0; p < xb->npreds && ok; p++) {
      uint32_t q = xb->preds[p];
      if (!in[q]) { in[q] = 1; stk[sp++] = q; }
    }
  }
  xfree(in); xfree(stk);
  return ok && n <= SMALL_LOOP_INSTS;
}

/* The least padding after offset `at` that starts a loop of `span` bytes on
   an `align` boundary (16 or 32) and keeps it inside one 64-byte fetch
   block; just the alignment for a loop longer than a block. */
static uint32_t fit_pad(uint32_t at, uint32_t span, uint32_t align) {
  uint32_t p = (align - (at & (align - 1))) & (align - 1);
  if (span <= 64)
    while (((at + p) & 63) + span > 64) p += align;
  return p;
}

/* Where in a 64-byte block the function starts, a multiple of the loop
   alignment so that every other loop's stays: the one whose small loops of
   the deepest nest, at the offsets the first emission measured, fit with
   the least padding run on their entry -- a nop for each word of it, and
   the branch over more than 16 bytes, which costs about a cycle, as eight
   nops do.  The padding a phase puts ahead of the function is never run.
   vecbench/condsum's 28-byte inner loop, entered for almost every element
   by the loop round it, sat 16 bytes later once the prologue had shrunk,
   needed a branch over 24 bytes of padding to fit, and ran 26% slower; a
   phase of 16 puts it back.  Only a nest at least two deep counts, its
   innermost loops entered once each time round the loop around them, and
   only when every loop at that depth is a small one measured here: a phase
   moves the whole function, and the code around its loops with it, and
   asbench/virtual ran 15% slower when a phase saved the branch over the
   padding of a loop entered once a call.  rec holds (header, order index,
   offset, padding) per small loop, in order, and spans each one's bytes;
   header marks every loop header. */
static uint32_t loop_phase(IrFunc *f, const uint32_t *rec, uint32_t nrec,
                           const uint16_t *spans, uint32_t align,
                           const uint8_t *header) {
  uint32_t deep = 0;
  for (uint32_t b = 0; b < f->nblocks; b++)
    if (header[b] && f->blocks[b].loop_depth > deep) deep = f->blocks[b].loop_depth;
  if (deep < 2) return 0;
  for (uint32_t b = 0; b < f->nblocks; b++) {
    if (!header[b] || f->blocks[b].loop_depth != deep) continue;
    uint32_t i = 0;
    while (i < nrec && rec[4 * i] != b) i++;
    if (i == nrec || !spans[b]) return 0;
  }
  uint32_t best = 0;
  uint64_t best_cost = UINT64_MAX;
  for (uint32_t k = 0; k < 64; k += align) {
    uint64_t cost = 0;
    int64_t shift = 0;
    for (uint32_t i = 0; i < nrec; i++) {
      uint32_t h = rec[4 * i], at = rec[4 * i + 2], pad = rec[4 * i + 3];
      uint32_t span = spans[h];
      uint32_t np = span ? fit_pad((uint32_t)((int64_t)at + shift + k), span, align) : pad;
      if (f->blocks[h].loop_depth == deep) cost += np > 16 ? 8 : np / 4;
      shift += (int64_t)np - (int64_t)pad;
    }
    if (cost < best_cost) { best_cost = cost; best = k; }
  }
  return best;
}

/* The bytes the loop headed by h took, h at order index oi: its blocks laid
   out in a row from h on, up to the end of the last of them. */
static uint32_t loop_span(Isel *s, uint32_t h, const uint32_t *eo, uint32_t neo,
                          uint32_t oi, const uint32_t *blk_end) {
  IrFunc *f = s->f;
  uint32_t nb = f->nblocks, sp = 0;
  uint8_t *in = xcalloc(nb ? nb : 1, 1);
  uint32_t *stk = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  Block *hb = &f->blocks[h];
  in[h] = 1;
  for (uint32_t p = 0; p < hb->npreds; p++) {
    uint32_t l = hb->preds[p];
    if (f->blocks[l].rpo >= hb->rpo && !in[l]) { in[l] = 1; stk[sp++] = l; }
  }
  while (sp) {
    Block *xb = &f->blocks[stk[--sp]];
    for (uint32_t p = 0; p < xb->npreds; p++)
      if (!in[xb->preds[p]]) { in[xb->preds[p]] = 1; stk[sp++] = xb->preds[p]; }
  }
  uint32_t last = h;
  for (uint32_t j = oi; j < neo && in[eo[j]]; j++) last = eo[j];
  xfree(in); xfree(stk);
  return blk_end[last] - s->block_off[h];
}

static void emit_teardown(Isel *s);
static void emit_epilogue(Isel *s);
static void emit_cold_frame(Isel *s);
static void emit_block(Isel *s, uint32_t bi, uint32_t next_bi);

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

/* A vector shift left (or logical right) by a constant: its source, lane
   size and amount, the amount taken modulo the lane width as wasm does. */
static bool vshift_const(IrFunc *f, Ref v, bool left, Ref *src, int *sz,
                         int *k) {
  Inst *in = &f->insts[v];
  uint64_t amt;
  if (in->op == (left ? I_VSHL : I_VSHRU)) amt = in->imm;
  else if (in->op != (left ? I_VSHLV : I_VSHRUV) ||
           !const_arg(f, ir_args(f, v)[1], &amt))
    return false;
  *sz = VLANE_SIZE(in->imm2);
  *k = (int)(amt % (uint64_t)(8 << *sz));
  *src = ir_args(f, v)[0];
  return true;
}

/* ---- byte shuffles ---------------------------------------------------------

   i8x16.shuffle names each of the 16 bytes of its result by its place in x
   (0-15) or y (16-31).  a64 has no one instruction for that in general, and
   the table lookup that is (tbl) wants the 16 places in a register, and
   two-source a second table register beside the first.  It has one for
   each shape that occurs: a lane of either repeated (dup); lanes reversed
   in their 16-, 32- or 64-bit containers (rev); 16 bytes of x:y at an
   offset (ext); and two vectors' lanes interleaved, de-interleaved and
   transposed (zip, uzp, trn) -- at 8-, 16-, 32- and 64-bit lanes, of x and
   y in either order or of one with itself.  sh_plan takes the cheapest
   way it finds: one of those; a vector or one of those with a few lanes
   put in (ins); two of those, the second of the first's result; and only
   else tbl. */
enum { SH_MOV, SH_DUP, SH_REV, SH_EXT, SH_ZIP1, SH_ZIP2, SH_UZP1, SH_UZP2,
       SH_TRN1, SH_TRN2, SH_INS };
enum { SHK_PERM, SHK_INS, SHK_TBL1, SHK_TBL2 };

/* op on lanes of 1 << e bytes; a and b its operands: 0 x, 1 y, 2 the first
   step's result; k: ext's offset, rev's container (log2 bytes), dup's lane,
   ins's lane; l: ins's source lane */
typedef struct { uint8_t op, e, a, b, k, l; } ShStep;

/* SHK_PERM: st[0..n) in turn, each but the last into a scratch register;
   SHK_INS: st[0], the base, then its lanes st[1..n) put in; SHK_TBL1: a
   lookup in st[0].a; SHK_TBL2: in x and y.  cost: instructions, twice. */
typedef struct { uint8_t kind, n, cost; ShStep st[6]; } ShPlan;

#define SH_MAX_INS 4

static void sh_apply(const ShStep *st, const uint8_t *A, const uint8_t *Bm, uint8_t *out) {
  uint32_t E = 1u << st->e, n = 16u >> st->e;
  switch (st->op) {
    case SH_MOV: memcpy(out, A, 16); break;
    case SH_DUP:
      for (uint32_t i = 0; i < 16; i++) out[i] = A[st->k * E + i % E];
      break;
    case SH_REV: {
      uint32_t C = 1u << st->k, ne = C / E;
      for (uint32_t i = 0; i < 16; i++) {
        uint32_t c0 = i / C * C, w = i - c0;
        out[i] = A[c0 + (ne - 1 - w / E) * E + w % E];
      }
      break;
    }
    case SH_EXT:
      for (uint32_t i = 0; i < 16; i++) {
        uint32_t j = i + st->k;
        out[i] = j < 16 ? A[j] : Bm[j - 16];
      }
      break;
    default:
      for (uint32_t L = 0; L < n; L++) {
        const uint8_t *S;
        uint32_t s;
        switch (st->op) {
          case SH_ZIP1: case SH_ZIP2:
            S = L & 1 ? Bm : A;
            s = (st->op == SH_ZIP2 ? n / 2 : 0) + L / 2;
            break;
          case SH_UZP1: case SH_UZP2:
            s = 2 * L + (st->op == SH_UZP2);
            S = s < n ? A : Bm;
            s %= n;
            break;
          default:
            S = L & 1 ? Bm : A;
            s = (L & ~1u) + (st->op == SH_TRN2);
            break;
        }
        for (uint32_t b = 0; b < E; b++) out[L * E + b] = S[s * E + b];
      }
      break;
  }
}

typedef struct { uint8_t m[16]; ShStep st; } ShCand;

/* Every one-step result of the operands id[0..nop), whose bytes are m[id]:
   with need >= 0, only those that read operand need. */
static uint32_t sh_gen(const uint8_t (*m)[16], const uint8_t *id, uint32_t nop, int need,
                       ShCand *out) {
  uint32_t n = 0;
#define SH_ADD(OP, E, A, B, K) do { \
    ShStep st_ = { (OP), (uint8_t)(E), (A), (B), (uint8_t)(K), 0 }; \
    out[n].st = st_; sh_apply(&st_, m[A], m[B], out[n].m); n++; } while (0)
  for (uint32_t p = 0; p < nop; p++) {
    uint8_t a = id[p];
    for (uint32_t e = 0; e < 4; e++) {
      if (need < 0 || a == need) {
        for (uint32_t l = 0; l < (16u >> e); l++) SH_ADD(SH_DUP, e, a, a, l);
        for (uint32_t c = e + 1; c <= 3; c++) SH_ADD(SH_REV, e, a, a, c);
      }
      for (uint32_t q = 0; q < nop; q++) {
        uint8_t b = id[q];
        if (need >= 0 && a != need && b != need) continue;
        for (uint8_t op = SH_ZIP1; op <= SH_TRN2; op++) SH_ADD(op, e, a, b, 0);
      }
    }
    for (uint32_t q = 0; q < nop; q++) {
      uint8_t b = id[q];
      if (need >= 0 && a != need && b != need) continue;
      for (uint32_t k = 1; k < 16; k++) SH_ADD(SH_EXT, 0, a, b, k);
    }
  }
#undef SH_ADD
  return n;
}

/* The lanes base lacks of want, put in from x or y: at the widest lanes
   that are aligned lanes of x or y, which are the fewest; false where no
   size is. */
static bool sh_ins(const uint8_t *want, const uint8_t *base, uint32_t nsrc, ShPlan *p) {
  for (int e = 3; e >= 0; e--) {
    uint32_t E = 1u << e, cnt = 0;
    ShStep st[SH_MAX_INS];
    bool ok = true;
    for (uint32_t L = 0; L < (16u >> e) && ok; L++) {
      if (!memcmp(want + L * E, base + L * E, E)) continue;
      uint32_t b0 = want[L * E];
      ok = b0 % E == 0 && b0 / 16 < nsrc && cnt < SH_MAX_INS;
      for (uint32_t b = 1; b < E && ok; b++) ok = want[L * E + b] == b0 + b;
      if (ok) {
        ShStep s1 = { SH_INS, (uint8_t)e, (uint8_t)(b0 / 16), (uint8_t)(b0 / 16), (uint8_t)L,
                      (uint8_t)(b0 % 16 / E) };
        st[cnt++] = s1;
      }
    }
    if (ok) {
      p->n = (uint8_t)(cnt + 1);
      memcpy(p->st + 1, st, cnt * sizeof *st);
      return true;
    }
  }
  return false;
}

/* The cheapest plan for want (bytes of x 0-15, of y 16-31; one: x and y
   are one value, want's places taken mod 16).  from: which of x (1) and y
   (2) the original names, for the table the IR holds. */
static void sh_plan(const uint8_t *want0, bool one, uint32_t from, ShPlan *best) {
  uint8_t want[16], m[3][16];
  uint8_t id[3] = { 0, 1, 2 };
  uint32_t nsrc = one ? 1 : 2;
  for (uint32_t i = 0; i < 16; i++) {
    want[i] = one ? want0[i] & 15 : want0[i];
    m[0][i] = (uint8_t)i;
    m[1][i] = (uint8_t)(16 + i);
  }
  /* the table lookup: x's or y's alone, or both, in a register pair */
  memset(best, 0, sizeof *best);
  if (from == 3) { best->kind = SHK_TBL2; best->cost = 7; }
  else { best->kind = SHK_TBL1; best->st[0].a = from == 2; best->cost = 5; }
  best->n = 1;

  static _Thread_local ShCand c1[512], c2[512];
  uint32_t n1 = sh_gen((const uint8_t (*)[16])m, id, nsrc, -1, c1);
  /* a base with lanes put in -- x or y copied, or a step's result -- or
     with none, x or y as it is or one step */
  for (uint32_t i = 0; i < nsrc + n1; i++) {
    ShPlan p;
    const uint8_t *base = i < nsrc ? m[i] : c1[i - nsrc].m;
    p.n = 0;
    if (!sh_ins(want, base, nsrc, &p) || 2u * p.n >= best->cost) continue;
    p.kind = SHK_INS;
    p.cost = (uint8_t)(2 * p.n);
    if (i < nsrc) {
      ShStep st = { SH_MOV, 0, (uint8_t)i, (uint8_t)i, 0, 0 };
      p.st[0] = st;
    } else {
      p.st[0] = c1[i - nsrc].st;
    }
    *best = p;
  }
  if (best->cost <= 4) return;
  /* two steps */
  for (uint32_t i = 0; i < n1; i++) {
    memcpy(m[2], c1[i].m, 16);
    uint8_t ids[3] = { 2, 0, 1 };
    uint32_t n2 = sh_gen((const uint8_t (*)[16])m, ids, nsrc + 1, 2, c2);
    for (uint32_t j = 0; j < n2; j++)
      if (!memcmp(want, c2[j].m, 16)) {
        best->kind = SHK_PERM; best->n = 2; best->cost = 4;
        best->st[0] = c1[i].st; best->st[1] = c2[j].st;
        return;
      }
  }
}

/* sh_plan of shuffle v, kept by its places: a module's shuffles are few
   and alike, and the plan is asked for twice */
static void sh_plan_of(IrFunc *f, Ref v, ShPlan *p) {
  const Inst *in = &f->insts[v];
  const Ref *a = ir_args(f, v);
  uint8_t want[16];
  uint32_t from = 0;
  for (uint32_t i = 0; i < 16; i++) {
    want[i] = (uint8_t)((i < 8 ? in->imm >> (8 * i) : in->imm2 >> (8 * (i - 8))) & 0x1f);
    from |= want[i] < 16 ? 1u : 2u;
  }
  bool one = a[0] == a[1];
  static _Thread_local struct { uint64_t lo, hi; uint32_t key; ShPlan p; } memo[64];
  uint32_t key = 1u | (one ? 2u : 0u) | from << 2;
  uint32_t h = (uint32_t)((in->imm * 0x9e3779b97f4a7c15ull ^ in->imm2) >> 58);
  if (memo[h].key == key && memo[h].lo == in->imm && memo[h].hi == in->imm2) {
    *p = memo[h].p;
    return;
  }
  sh_plan(want, one, from, p);
  memo[h].lo = in->imm; memo[h].hi = in->imm2; memo[h].key = key; memo[h].p = *p;
}

/* Full adders.  A limb of GMP's generic mpn_add_n,
     s = u + v, r = s + cin, cout = (s <u u) | (r <u s), cin 0 or 1,
   is adcs r, u, v once the carry flag holds cin, then cset cout, hs -- and
   the next limb's cin, this cout, is still in the flag: a chain of adcs, a
   cycle a limb, where add, cmp, cset and orr made four (hsbench/integer).
   mpn_sub_n's, s = u - v, r = s - bin, bout = (u <u v) | (s <u bin), is
   sbcs, the flag holding not bin.  s and the compares are folded, and the
   compares and the or moved to just after r: nothing is emitted between
   the adcs and the cset, so nothing writes the flags there.  A compare may
   take any of its equivalent forms, and the or may be an add or a xor: the
   two carries are never both set. */
#define FA_R   1   /* the add or sub taking the carry in */
#define FA_OUT 2   /* the carry out */
#define FA_SUB 4   /* a borrow */
#define FA_S1  8   /* s is the add's second operand */

/* c as x <u y */
static bool fa_lt(IrFunc *f, Ref c, Ref *x, Ref *y) {
  const Inst *in = &f->insts[c];
  if (in->nargs != 2 || (in->op != I_LTU && in->op != I_GTU)) return false;
  Ref *a = ir_args(f, c);
  *x = a[in->op == I_GTU];
  *y = a[in->op != I_GTU];
  return true;
}

/* v is computed where it stands, by nothing else's instruction */
static bool fa_own(Isel *s, Ref v) {
  return !s->folded[v] && !s->fused[v] && !s->imm_fold[v] && !s->addr_fuse[v] &&
         !s->ptr_fuse[v] && !s->zx_fuse[v] && !s->bfx[v].ok && !s->csinc[v] && !s->fa[v];
}

/* v is in a register or a constant where it is read */
static bool fa_operand(Isel *s, Ref v) {
  return !s->folded[v] && !s->fused[v] && !s->addr_fuse[v] && !s->ptr_fuse[v];
}

/* The limb whose carry out is o, from its compares c1 (of u and v) and c2
   (of the carry in): its r, s, carry in and kind, or false. */
static bool fa_match(Isel *s, uint32_t bi, Ref c1, Ref c2, Ref *pr, Ref *ps, uint8_t *kind) {
  IrFunc *f = s->f;
  Ref x, y, r = REF_NONE, sv = REF_NONE, cin = REF_NONE;
  if (!fa_lt(f, c2, &x, &y)) return false;
  const Inst *xi = &f->insts[x];
  uint8_t k = 0;
  if (xi->op == I_ADD && xi->nargs == 2) {
    /* r <u s or r <u cin, r = s + cin */
    r = x;
    for (int j = 0; j < 2 && sv == REF_NONE; j++) {
      Ref t = ir_args(f, r)[j], o = ir_args(f, r)[1 - j];
      if (f->insts[t].op != I_ADD || f->insts[t].nargs != 2 || (y != t && y != o)) continue;
      sv = t; cin = o; k = j ? FA_S1 : 0;
    }
  } else if (xi->op == I_SUB && xi->nargs == 2) {
    /* s <u bin or s <u r, r = s - bin */
    sv = x;
    const Inst *yi = &f->insts[y];
    if (yi->op == I_SUB && yi->nargs == 2 && ir_args(f, y)[0] == sv) { r = y; cin = ir_args(f, y)[1]; }
    else {
      Block *b = &f->blocks[bi];
      for (uint32_t i = 0; i < b->ninsts && r == REF_NONE; i++) {
        Ref t = b->insts[i];
        if (f->insts[t].op == I_SUB && f->insts[t].nargs == 2 &&
            ir_args(f, t)[0] == sv && ir_args(f, t)[1] == y)
          r = t;
      }
      cin = y;
    }
    k = FA_SUB;
  }
  if (r == REF_NONE || sv == REF_NONE) return false;
  Ref *ua = ir_args(f, sv);
  /* c1: s <u u or s <u v; u <u v or u <u s */
  if (!fa_lt(f, c1, &x, &y)) return false;
  if (k & FA_SUB ? !(x == ua[0] && (y == ua[1] || y == sv))
                 : !(x == sv && (y == ua[0] || y == ua[1])))
    return false;
  if (ir_known_bits(f, cin, 4) & ~1ull) return false;
  *pr = r; *ps = sv; *kind = k;
  return true;
}

static void find_full_adders(Isel *s, const uint32_t *uses) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  s->fa = xcalloc(n ? n : 1, 1);
  s->fa_flag = REF_NONE;
  uint32_t *pos = xmalloc((n ? n : 1) * sizeof *pos);
  uint8_t *moved = NULL;
  struct { Ref r, c1, c2, o; } *lim = NULL;
  uint32_t cap = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    uint32_t nl = 0;
    for (uint32_t i = 0; i < b->ninsts; i++) pos[b->insts[i]] = i;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref o = b->insts[i];
      const Inst *oin = &f->insts[o];
      if ((oin->op != I_OR && oin->op != I_XOR && oin->op != I_ADD) || oin->nargs != 2 ||
          !fa_own(s, o))
        continue;
      Ref *oa = ir_args(f, o);
      for (int j = 0; j < 2; j++) {
        Ref c1 = oa[j], c2 = oa[1 - j], r, sv;
        uint8_t k;
        if (!fa_match(s, bi, c1, c2, &r, &sv, &k)) continue;
        Ref cin = ir_args(f, r)[k & FA_S1 ? 0 : 1];
        /* a widened carry the add took in as uxtw: its source is read */
        if (s->folded[cin] && f->insts[cin].op == I_EXTENDU && f->insts[cin].nargs == 1)
          cin = ir_args(f, cin)[0];
        Ref *ua = ir_args(f, sv);
        uint32_t refs = 1;
        for (uint32_t m = 0; m < 2; m++) refs += (ir_args(f, c1)[m] == sv) + (ir_args(f, c2)[m] == sv);
        Ref own[5] = { sv, c1, c2, r, o };
        bool ok = uses[c1] == 1 && uses[c2] == 1 && uses[sv] == refs &&
                  fa_operand(s, ua[0]) && fa_operand(s, ua[1]) && fa_operand(s, cin);
        for (int m = 0; m < 5 && ok; m++)
          ok = f->insts[own[m]].block == bi && fa_own(s, own[m]);
        if (!ok || pos[r] > i) continue;
        s->folded[sv] = s->folded[c1] = s->folded[c2] = 1;
        s->fa[r] = FA_R | k;
        s->fa[o] = FA_OUT | (k & FA_SUB);
        if (nl == cap) { cap = cap ? 2 * cap : 8; lim = xrealloc(lim, cap * sizeof *lim); }
        lim[nl].r = r; lim[nl].c1 = c1; lim[nl].c2 = c2; lim[nl].o = o;
        nl++;
        break;
      }
    }
    if (!nl) continue;
    /* each carry out, and the compares it is made of, to just after its r */
    if (!moved) moved = xcalloc(n ? n : 1, 1);
    for (uint32_t l = 0; l < nl; l++) moved[lim[l].c1] = moved[lim[l].c2] = moved[lim[l].o] = 1;
    Ref *out = xmalloc(b->ninsts * sizeof *out);
    uint32_t m = 0;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref t = b->insts[i];
      if (moved[t]) continue;
      out[m++] = t;
      if (s->fa[t] & FA_R)
        for (uint32_t l = 0; l < nl; l++)
          if (lim[l].r == t) { out[m++] = lim[l].c1; out[m++] = lim[l].c2; out[m++] = lim[l].o; }
    }
    if (m != b->ninsts) warmc_fatal("internal: full adders lost an instruction");
    memcpy(b->insts, out, m * sizeof *out);
    xfree(out);
    for (uint32_t l = 0; l < nl; l++) moved[lim[l].c1] = moved[lim[l].c2] = moved[lim[l].o] = 0;
  }
  xfree(lim);
  xfree(moved);
  xfree(pos);
}

static void analyse_immediates(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  s->imm_fold = xcalloc(n ? n : 1, 1);
  s->fused = xcalloc(n ? n : 1, 1);
  s->pool_ok = s->opt->cpool && !s->far_br && pool_in_reach(f);
  s->remat_fp = !warmc_getenv("WARMC_NO_REMAT_FP");
  s->remat_v128 = !warmc_getenv("WARMC_NO_REMAT_V128");
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
  s->no_ldpair = warmc_getenv("WARMC_NO_LDPAIR") != NULL;
  s->folded = xcalloc(n ? n : 1, 1);
  uint32_t *uses = xcalloc(n ? n : 1, sizeof(uint32_t));
  s->uses = uses;
  uint8_t *blocked = xcalloc(n ? n : 1, 1);

  s->no_imm = warmc_getenv("WARMC_NO_IMM") != NULL;
  s->no_csel_fuse = warmc_getenv("WARMC_NO_CSEL_FUSE") != NULL;
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

      /* Calls make a rematerialised argument in place (see
         emit_call_args_core); the rest move every operand from a location.
         A memory.copy or memory.fill that saves nothing round it makes a
         constant operand where it reads it (emit_memop_r), in no register. */
      bool in_place = memop_preserves(f, in);
      bool via_moves = (in->op == I_MEM_GROW || in->op == I_MEM_COPY ||
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
        if (!s->imm_fold[a[j]] || in_place) continue;
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
      /* an offset each access of a shared address can scale */
      if ((in->op == I_LOAD || in->op == I_STORE) && in->nargs &&
          f->insts[a[0]].op == I_MEM_ADDR) {
        uint64_t off = f->insts[a[0]].imm;
        uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
        if (off && ((off & ((1u << size) - 1)) != 0 || (off >> size) > 0xfff))
          addr_only[a[0]] = 0;
      }
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
      /* Shared by accesses only, an address folds into each of them: with no
         offset that is free, and with one it costs an add per access where a
         register would cost one add for both -- the same for two accesses, a
         load and the store back to the same place, so there it is folded
         too, rather than held in a register from the one to the other. */
      if (uses[ea] != 1 &&
          !(addr_only[ea] && (f->insts[ea].imm == 0 || uses[ea] <= 2)))
        continue;
      uint64_t off = f->insts[ea].imm;
      uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);

      bool ok = (off == 0) ||
                ((off & ((1u << size) - 1)) == 0 && (off >> size) <= 0xfff);
      if (!ok) continue;
      if (!warmc_getenv("WARMC_NO_ADDRFUSE")) { s->addr_fuse[ea] = 1; s->folded[ea] = 1; }
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

  if (!warmc_getenv("WARMC_NO_NOTFUSE"))
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
  if (!warmc_getenv("WARMC_NO_BFX"))
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

  s->bfi = xcalloc(n ? n : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if (in->op != I_OR || in->nargs != 2 || s->folded[u] || s->bfx[u].ok) continue;
      Ref *a = ir_args(f, u);
      int width = vt_is_64((ValType)in->type) ? 64 : 32;
      for (int slot = 0; slot < 2; slot++) {
        Ref c = a[slot], o = a[slot ^ 1];
        struct Bfx *bf = &s->bfx[c];
        if (!bf->ok || bf->sign || uses[c] != 1 || blocked[c] || s->folded[c] ||
            f->insts[c].block != bi)
          continue;
        /* a constant is better an orr's immediate; a folded value is in
           no register to insert into */
        if (f->insts[o].op == I_CONST || s->folded[o]) continue;
        /* where ubfm puts the field: the low bits for an extract, at
           width - immr for an insert */
        uint64_t field = bf->imms >= bf->immr
                             ? (2ull << (bf->imms - bf->immr)) - 1
                             : ((2ull << bf->imms) - 1) << (width - bf->immr);
        if (width == 32) field &= 0xffffffffu;
        if (ir_known_bits(f, o, 4) & field) continue;
        s->folded[c] = 1;
        s->bfi[u] = (uint8_t)(slot + 1);
        break;
      }
    }
  }

  /* (x << k) | (y >> (width - k)) is the low half of x:y shifted right by
     width - k, one extr.  The two shifts' bits never overlap, so xor is the
     same.  Unrolled multi-word shifts are made of exactly this: each word of
     a bitset or a big integer shifted by one bit takes its neighbour's top
     bit. */
  s->extr = xcalloc(n ? n : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if ((in->op != I_OR && in->op != I_XOR) || in->nargs != 2) continue;
      if (s->folded[u] || s->bfx[u].ok || s->no_imm) continue;
      Ref *a = ir_args(f, u);
      int width = vt_is_64((ValType)in->type) ? 64 : 32;
      int shl = -1;
      for (int slot = 0; slot < 2; slot++)
        if (f->insts[a[slot]].op == I_SHL) shl = slot;
      if (shl < 0) continue;
      Ref l = a[shl], r = a[shl ^ 1];
      if (f->insts[r].op != I_SHRU) continue;
      bool ok = true;
      Ref sh[2] = { l, r };
      uint64_t amt[2];
      for (int k = 0; k < 2 && ok; k++) {
        Inst *ci = &f->insts[sh[k]];
        ok = uses[sh[k]] == 1 && !blocked[sh[k]] && !s->folded[sh[k]] &&
             !s->bfx[sh[k]].ok && ci->block == bi && ci->type == in->type &&
             f->insts[ir_args(f, sh[k])[1]].op == I_CONST;
        if (ok) amt[k] = f->insts[ir_args(f, sh[k])[1]].imm % (uint64_t)width;
      }
      if (!ok || amt[0] == 0 || amt[0] + amt[1] != (uint64_t)width) continue;
      s->folded[l] = s->folded[r] = 1;
      s->extr[u] = 1;
      a[0] = l; a[1] = r;
    }
  }

  if (!warmc_getenv("WARMC_NO_OPFUSE"))
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

          /* Any lsl, lsr or asr folds into an add or sub (a rotate has no
             such form).  On Apple's cores the shifted operand takes two
             cycles, as the separate shift and add do, the other operand one,
             and the fused form issues at 1.6 times the rate of the pair: an
             M4 Max measures the same for lsl #2 as for lsl #12 or lsr #31. */
          if (arith && ci->op != I_SHL && ci->op != I_SHRU && ci->op != I_SHRS) continue;
          s->folded[cand] = 1;
          if (slot == 0) { Ref t = a[0]; a[0] = a[1]; a[1] = t; }
          break;
        }
        if (arith && ci->op == I_MUL) {
          s->folded[cand] = 1;
          if (slot == 0) { Ref t = a[0]; a[0] = a[1]; a[1] = t; }
          break;
        }
        /* A 64-bit add of a widened i32 widens it in the add: the extended-
           register form reads the low half of its operand and zero- or
           sign-extends it, so the extension costs neither an instruction
           nor, on a chain like md5's rotate-then-add rounds, a cycle. */
        if (arith && vt_is_64((ValType)in->type) &&
            (ci->op == I_EXTENDU || ci->op == I_EXTENDS || ci->op == I_EXT32S)) {
          s->folded[cand] = 1;
          if (slot == 0) { Ref t = a[0]; a[0] = a[1]; a[1] = t; }
          break;
        }
      }
    }
  }

  /* An i32 high multiply shifted right again, as an unsigned divide by a
     constant is: umull leaves the product in 64 bits, and one lsr by 32 + k
     takes the quotient from it, zero above as an i32 must be.  (The signed
     one's asr would leave the sign above, so it keeps its two shifts.) */
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if (in->op != I_SHRU || in->type != VT_I32 || s->folded[u] || s->bfx[u].ok) continue;
      Ref *a = ir_args(f, u);
      Inst *mi = &f->insts[a[0]];
      if (mi->op != I_MULHU || mi->type != VT_I32 || mi->block != bi ||
          uses[a[0]] != 1 || blocked[a[0]] || s->folded[a[0]] ||
          f->insts[a[1]].op != I_CONST || (f->insts[a[1]].imm & 31) == 0)
        continue;
      s->folded[a[0]] = 1;
    }
  }

  /* A vector or of a left and a logical right shift of one value, by
     amounts that add up to the lane width, is a rotate.  Wasm has no vector
     rotate, so every one in BLAKE3's or ChaCha's lanes is spelled that way,
     and so is each scalar rotl the vectoriser widens.  Three instructions
     become a shift right and a shift-left-and-insert of the same source, and
     a rotate by half the lane is a single byte or halfword swap; the shifts
     fold into the or. */
  s->vrot = xcalloc(n ? n : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      if (f->insts[u].op != I_VOR || s->folded[u]) continue;
      Ref *a = ir_args(f, u);
      for (int slot = 0; slot < 2; slot++) {
        Ref l = a[slot], r = a[slot ^ 1], xl, xr;
        int szl, szr, kl, kr;
        if (!vshift_const(f, l, true, &xl, &szl, &kl) ||
            !vshift_const(f, r, false, &xr, &szr, &kr))
          continue;
        if (xl != xr || szl != szr || kl == 0 || kl + kr != 8 << szl) continue;
        if (uses[l] != 1 || uses[r] != 1 || blocked[l] || blocked[r] ||
            s->folded[l] || s->folded[r])
          continue;
        if (f->insts[l].block != bi || f->insts[r].block != bi) continue;
        s->folded[l] = s->folded[r] = 1;
        s->vrot[u] = (uint8_t)kl;
        if (slot) { a[0] = l; a[1] = r; }
        break;
      }
    }
  }

  /* A widening sum the vectoriser makes adds each vector's neighbouring
     lanes into lanes twice as wide, perhaps again, and those into the
     accumulator: the last two are one uadalp, which adds the pairs into the
     accumulator itself.  Only after a first widening: uadalp is slower than
     the add it replaces, and where the add is all the loop's accumulators
     wait on, the uaddlp beside it is free -- u32 into u64 (gobench/sum) ran
     20% slower fused, bytes into u32 (vecbench/widen8, three instructions
     a vector unfused) 18% faster. */
  s->vadal = xcalloc(n ? n : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if (in->op != I_VADD || in->nargs != 2 || s->folded[u]) continue;
      Ref *a = ir_args(f, u);
      for (int slot = 0; slot < 2; slot++) {
        Ref e = a[slot];
        Inst *ei = &f->insts[e];
        if (ei->op != I_VEXTADD || ei->imm2 != in->imm2 || ei->block != bi) continue;
        if (f->insts[ir_args(f, e)[0]].op != I_VEXTADD) continue;
        if (uses[e] != 1 || blocked[e] || s->folded[e]) continue;
        s->folded[e] = 1;
        s->vadal[u] = (uint8_t)(slot + 1);
        break;
      }
    }
  }

  /* A vector product added into a sum, or taken from one, is an mla or an
     mls into it, in lanes of 8, 16 or 32 bits, which mla has.  Not into a
     value a loop carries round: there the multiply joins the chain each
     iteration waits on, where with an add it is off it -- Zig's sum of
     products (zigbench/simdsum), its multiplies made one (opt.c
     fold_common_factor), ran 15% slower with the accumulator's add an mla,
     as uadalp above. */
  s->vmla = xcalloc(n ? n : 1, 1);
  if (!warmc_getenv("WARMC_NO_VMLA"))
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if ((in->op != I_VADD && in->op != I_VSUB) || in->nargs != 2 || s->folded[u] ||
          s->vadal[u] || in->imm2 > VLANE_I32)
        continue;
      Ref *a = ir_args(f, u);
      for (int slot = in->op == I_VSUB ? 1 : 0; slot < 2; slot++) {
        Ref e = a[slot];
        Inst *ei = &f->insts[e];
        if (ei->op != I_VMUL || ei->imm2 != in->imm2 || ei->block != bi) continue;
        if (uses[e] != 1 || blocked[e] || s->folded[e]) continue;
        const Inst *acc = &f->insts[a[slot ^ 1]];
        if (acc->op == I_PARAM && f->blocks[acc->block].loop_depth) continue;
        s->folded[e] = 1;
        s->vmla[u] = (uint8_t)(slot + 1);
        break;
      }
    }
  }

  /* A float splat that only vector multiplies of its block read, each with
     a vector beside it, is its scalar read by lane: an fmul by element, the
     scalar lane 0 of its register, and no dup.  The SLP pass splats each
     element of a matrix row for the row's products (rsbench/constmat),
     which clang multiplies by element.  (A use by a terminator or an edge
     is a use no multiply makes up.) */
  s->vbyel = xcalloc(n ? n : 1, 1);
  {
    uint32_t *by = xcalloc(n ? n : 1, sizeof(uint32_t));
    for (int pass = 0; pass < 2; pass++)
      for (uint32_t oi = 0; oi < f->norder; oi++) {
        uint32_t bi = f->order[oi];
        Block *b = &f->blocks[bi];
        for (uint32_t i = 0; i < b->ninsts; i++) {
          Ref u = b->insts[i];
          Inst *in = &f->insts[u];
          if (in->op != I_VFMUL || in->nargs != 2 || s->folded[u]) continue;
          Ref *a = ir_args(f, u);
          for (int slot = 0; slot < 2; slot++) {
            const Inst *ei = &f->insts[a[slot]];
            if (ei->op != I_VSPLAT || ei->imm2 != in->imm2 || ei->block != bi ||
                f->insts[a[slot ^ 1]].op == I_VSPLAT)
              continue;
            if (pass == 0) by[a[slot]]++;
            else if (by[a[slot]] == uses[a[slot]]) {
              s->folded[a[slot]] = 1;
              s->vbyel[u] = (uint8_t)(slot + 1);
            }
          }
        }
      }
    xfree(by);
  }

  /* A shuffle planned without a table lookup reads none of the index
     vectors the IR gives it: those only such shuffles read are not made. */
  {
    uint32_t *seen = xcalloc(n ? n : 1, sizeof(uint32_t));
    uint8_t *needed = xcalloc(n ? n : 1, 1);
    for (int pass = 0; pass < 2; pass++)
      for (uint32_t oi = 0; oi < f->norder; oi++) {
        Block *b = &f->blocks[f->order[oi]];
        for (uint32_t i = 0; i < b->ninsts; i++) {
          Ref u = b->insts[i];
          if (f->insts[u].op != I_VSHUFFLE) continue;
          ShPlan p;
          sh_plan_of(f, u, &p);
          Ref *a = ir_args(f, u);
          for (uint32_t j = 2; j < f->insts[u].nargs; j++) {
            if (pass == 0) {
              seen[a[j]]++;
              if (j == 2 && (p.kind == SHK_TBL1 || p.kind == SHK_TBL2)) needed[a[j]] = 1;
            } else if (!needed[a[j]] && seen[a[j]] == uses[a[j]] && !blocked[a[j]]) {
              s->folded[a[j]] = 1;
            }
          }
        }
      }
    xfree(seen);
    xfree(needed);
  }

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

  s->csinc = xcalloc(n ? n : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      if (in->op != I_SELECT || in->nargs != 3 || is_fp_value(f, u) || is_vec_value(f, u))
        continue;
      Ref *a = ir_args(f, u);
      uint64_t m = vt_is_64((ValType)in->type) ? ~0ull : 0xffffffffull;
      /* An arm one instruction from a value -- x + 1, 0 - x, x ^ -1 -- is
         that value through csinc, csneg or csinv, which do the step where
         the condition fails.  If-conversion makes these: cbench/fnptr's
         c ? s + 1 : s - a, and an absolute value from a branch. */
      for (int slot = 0; slot < 2; slot++) {
        Ref cand = a[slot];
        Inst *ci = &f->insts[cand];
        if (ci->nargs != 2 || ci->type != in->type) continue;
        if (uses[cand] != 1 || blocked[cand] || s->folded[cand] || ci->block != bi) continue;
        Ref *ca = ir_args(f, cand);
        uint8_t kind;
        if (ci->op == I_ADD && f->insts[ca[1]].op == I_CONST &&
            (f->insts[ca[1]].imm & m) == 1)
          kind = 0;
        else if (ci->op == I_SUB && f->insts[ca[0]].op == I_CONST &&
                 (f->insts[ca[0]].imm & m) == 0)
          kind = CS_NEG;
        else if (ci->op == I_XOR && f->insts[ca[1]].op == I_CONST &&
                 (f->insts[ca[1]].imm & m) == m)
          kind = CS_INV;
        else
          continue;
        /* the other arm a constant the cases below make better; and x is
           read here, so it must be a value of its own, not one a fusion
           above put into the arm (a shifted operand, an eon) */
        Ref x = ca[kind == CS_NEG ? 1 : 0];
        if (f->insts[a[slot ^ 1]].op == I_CONST || f->insts[x].op == I_CONST ||
            s->folded[x] || s->folded[a[slot ^ 1]])
          continue;
        s->folded[cand] = 1;
        s->csinc[u] = (uint8_t)((slot + 1) | kind);
        break;
      }
    }
  }

  /* A wrap leaves an i32 whose high half is whatever its i64 had.  Every
     reader below takes the low half only -- a w-register op, a 4-byte or
     narrower store, an index extended in its address -- so for those the
     uxtw a tied wrap would otherwise make is nothing to do: Go's code wraps
     every i64 it loads before using it as an i32. */
  s->lowonly = xcalloc(n ? n : 1, 1);
  for (uint32_t i = 0; i < n; i++)
    if (f->insts[i].op == I_WRAP) s->lowonly[i] = 1;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      Inst *in = &f->insts[u];
      Ref *a = ir_args(f, u);
      for (uint32_t j = 0; j < in->nargs; j++)
        if (s->lowonly[a[j]] && !ir_reads_low32(f, u, j)) s->lowonly[a[j]] = 0;
    }
    for (uint32_t r = 0; r < b->nret; r++) s->lowonly[b->ret_args[r]] = 0;
    for (uint32_t e = 0; e < b->nsuccs; e++)
      for (uint32_t j = 0; j < b->succs[e].nargs; j++) s->lowonly[b->succs[e].args[j]] = 0;
  }

  if (!warmc_getenv("WARMC_NO_MASKSEL"))
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
    if (cmpish && !warmc_getenv("WARMC_NO_FUSE")) s->fused[c] = 1;
  }

  /* A cast's slot test, which only its trap reads, branches to the trap
     itself (I_TRAP_IF) instead of making a 0 or 1 to test again. */
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

  if (!warmc_getenv("WARMC_NO_TSTFUSE"))
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref u = b->insts[i];
      if (!is_zero_test(s, u)) continue;

      /* a branch's test too: tst and b.ne leave the and's operand where it
         is, where and and cbnz took a register for its result */
      Ref cand = ir_args(f, u)[0];
      if (f->insts[cand].op != I_AND || f->insts[cand].nargs != 2) continue;
      if (uses[cand] != 1 || blocked[cand] || s->folded[cand]) continue;
      if (s->bfx[cand].ok || f->insts[cand].block != bi) continue;
      Ref *ca = ir_args(f, cand);

      if (s->folded[ca[0]] || s->folded[ca[1]]) continue;
      /* a mask some other use keeps in a register would be made again for
         the test, which reads it as an immediate (appendgrow's 3, also a
         call's argument) */
      if (f->insts[ca[1]].op == I_CONST && !s->imm_fold[ca[1]]) continue;

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

  find_full_adders(s, uses);
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

    if (r == R19) needed = !s->pin && s->needs_vmctx && s->vmctx_reg == R19;
    else if (r == R20) needed = !s->pin && s->needs_mem && s->membase_reg == R20;
    else if (r == R21) needed = s->needs_memsize;
    else needed = false;
    if (s->used_gpr_cs & (1u << r)) needed = true;
    if (needed) list[n++] = r;
  }
  if (n & 1) list[n++] = RZR;
  return n;
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

  uint32_t nsave = (uint32_t)(saves_lr(s) ? 16 : 0) + 16u * ((uint32_t)n / 2u);

  /* The pairs go from the lowest address up, list's last pair first, and
     the frame record on top, at entry - 16, where the unwinder looks for
     it.  The first store moves sp down by the whole area (pre-index), as
     native code's does, so the area costs no sub of its own: in a
     recursive fib that is one of the two dozen instructions a call is. */
  uint32_t at = 0;
  for (int i = n - 2; i >= -2; i -= 2) {
    int r1 = R29, r2 = R30;
    if (i >= 0) { r1 = list[i]; r2 = list[i + 1]; }
    else if (!saves_lr(s)) break;
    if (at == 0) a64_stp(b, SF64, r1, r2, RSP, -(int)nsave, LSP_PRE);
    else a64_stp(b, SF64, r1, r2, RSP, (int)at, LSP_OFF);
    at += 16;
  }

  if (s->frame & 0xfff) a64_sub_imm(b, SF64, RSP, RSP, s->frame & 0xfff, 0);
  if (s->frame >> 12) a64_sub_imm(b, SF64, RSP, RSP, s->frame >> 12, 1);

  int fl[8];
  int fn_ = cs_fpr_list(s, fl);
  for (int i = 0; i < fn_; i++)
    a64_fstr_imm(b, 3, fl[i], RSP, s->fpsave_off + 8u * (uint32_t)i);

  if (s->needs_vmctx && !s->pin) a64_mov_reg(b, SF64, REG_VMCTX, R0);

  /* The stack check falls through: its trap is one of the function's trap
     stubs after the body (patched by produce_body), not a branch over a
     stub here that every call took. */
  if (s->has_call) {
    a64_ldr_imm(b, 3, TMP0, REG_VMCTX, 48);
    a64_cmp_sp(b, TMP0);
    size_t site = b->len;
    if (!s->far_br) {
      a64_bcond(b, CC_LS, 0);
      s->so_site = site + 1;
      s->trap_used[TRAP_STACK_OVERFLOW] = 1;
    } else {
      a64_bcond(b, CC_HI, 0);
      a64_load_imm(b, SF32, R0, TRAP_STACK_OVERFLOW);
      uint32_t sym = obj_intern_sym(s->o, "warmc_trap", SYM_UNDEF_FUNC, -1, 0);
      isel_reloc(s, REL_CALL26, b->len | 0x8000000000000000ull, sym, 0);
      a64_bl(b, 0);
      a64_brk(b, 0);
      a64_patch_b19(b, site, b->len);
    }
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
      if (l == LOC_NONE) continue;
      /* A param bound for a spill slot is stored from its register where
         the loop below meets it, so it is a reader too: a move ahead of
         it that writes that register is a clash, and the moves then wait
         for the parallel copy, after every store. */
      dst[nd] = IS_SLOT(l) ? -1 : l; src_of[nd] = src; nd++;
      (void)fp;
    }

    for (int x = 0; x < nd && !clash; x++)
      for (int y = x + 1; y < nd; y++)
        if (dst[x] == src_of[y]) { clash = true; break; }
  }

  /* Two passes, as on x64.  A stack parameter's allocated home may be one of
     the incoming argument registers (X1-X7 and V0-V7 are all in the
     allocation pools), and with a clash the register moves are deferred to
     one parallel copy -- so a stack parameter landing in its home before
     that copy overwrites a register the copy has yet to read.  Pass 1 does
     the register params (and stack params bound for spill slots, which go
     through TMP0/FTMP and clobber nothing the copy reads); pass 2 loads the
     remaining stack params straight into their homes. */
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
      else { stack_slot++; src = FTMP; from_stack = true; }
    } else {
      if (next_int <= 7) src = next_int++;
      else { stack_slot++; src = TMP0; from_stack = true; }
    }
    int l = s->loc[p];
    if (l == LOC_NONE) continue;
    if (from_stack) {
      if (!IS_SLOT(l)) continue;   /* register home: pass 2 */
      sp_access(s, b, false, wide ? 3 : 2, fp, vec, src, at, TMP1);
      slot_store(s, b, fp, vec, src, slot_off(s, SLOT_OF(l)));
    } else if (IS_SLOT(l)) {
      slot_store(s, b, fp, vec, src, slot_off(s, SLOT_OF(l)));
    } else if (l != src) {
      if (clash) {
        if (npmv == (int)(sizeof pmv / sizeof pmv[0]))
          warmc_fatal("internal: more parameters in registers than there are");
        pmv[npmv++] = (Move){ src, l, (uint8_t)fp, (uint8_t)(fp ? wide : 1),
                              (uint8_t)vec, 0, 0 };
      } else if (vec) a64_vmov(b, l, src);
      else if (fp) a64_fmov_reg(b, wide ? FT_D : FT_S, l, src);
      else a64_mov_reg(b, SF64, l, src);
    }
  }
  if (npmv) parallel_copy_to(s, b, pmv, npmv);

  next_int = 1; next_flt = 0; stack_slot = 0;
  for (uint32_t i = 0; i < f->sig->nparams; i++) {
    Ref p = e->params[i];
    ValType t = f->sig->params[i];
    bool vec = (t == VT_V128);
    int wide = vt_is_64(t), fp = vt_is_vec_reg(t);

    if (fp) { if (next_flt < 8) { next_flt++; continue; } }
    else    { if (next_int <= 7) { next_int++; continue; } }
    uint32_t at = s->frame + s->saved_bytes + ARG_STACK_SLOT * (uint32_t)stack_slot++;
    int l = s->loc[p];
    if (l == LOC_NONE || IS_SLOT(l)) continue;
    sp_access(s, b, false, wide ? 3 : 2, fp, vec, l, at, TMP1);
  }

  if (s->needs_mem) {
    if (!s->pin) a64_ldr_imm(b, 3, REG_MEMBASE, REG_VMCTX, VMCTX_MEM_BASE);
    if (s->needs_memsize) a64_ldr_imm(b, 3, REG_MEMSIZE, REG_VMCTX, VMCTX_MEM_SIZE);
  }
  if (s->tid_tab) emit_ictab_addr(s, b, f->m->ict[s->tid_ict].sym, s->tid_reg, true);
  else if (s->tid_reg) a64_load_imm(b, SF64, s->tid_reg, s->tid_val);
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

  uint32_t nsave = (uint32_t)(saves_lr(s) ? 16 : 0) + 16u * ((uint32_t)n / 2u);

  /* From the top down, so that the pair at sp itself comes last and gives
     the area back (post-index), and x30 comes first, ahead of the ret. */
  uint32_t at = nsave;
  for (int i = -2; i < n; i += 2) {
    int r1 = R29, r2 = R30;
    if (i >= 0) { r1 = list[i]; r2 = list[i + 1]; }
    else if (!saves_lr(s)) continue;
    at -= 16;
    if (at == 0) a64_ldp(B, SF64, r1, r2, RSP, (int)nsave, LSP_POST);
    else a64_ldp(B, SF64, r1, r2, RSP, (int)at, LSP_OFF);
  }
}

static void emit_epilogue(Isel *s) {
  emit_teardown(s);
  a64_ret(B);
}

/* The frame a quasi-leaf's cold call is made in (cold_calls_only), set up
   with the arguments in their registers, just before the bl: the leaf's
   own frame comes down as at a return, so that every callee-saved
   register holds its caller's value again and x30 the return address, and
   a frame record goes in its place, with the outgoing area a callee's tail
   call may write below it -- the layout the function's unwind entry
   describes, for an exception or a collection that walks through the call.
   A callee that comes back finds a trap next, which needs no more. */
static void emit_cold_frame(Isel *s) {
  emit_teardown(s);
  a64_stp(B, SF64, R29, R30, RSP, -16, LSP_PRE);
  if (s->cold_out) a64_sub_imm(B, SF64, RSP, RSP, s->cold_out, 0);
}

static void regalloc_naive(Isel *s);
static void regalloc_linear(Isel *s);

/* Instructions the prologue and epilogue spend on the callee-saved
   registers used so far: one stp and one ldp a pair. */
static int ra_cs_frame_ops(Isel *s) {
  int list[NCS_GPR + 1];
  return cs_gpr_list(s, list);
}

#include "regalloc.inc"

/* A wrap whose every reader takes the low half only (lowonly) is no
   instruction: they read its operand's register, the operand living on to
   them in its place.  Where the operand outlived the wrap, the wrap took a
   register of its own and a move to it -- after each of Go's calls, whose
   result's low half is the stack pointer the code after reads and whose
   high half the branch after tests.  Not a branch's own operand, which
   liveness takes as it is. */
static void analyse_wrap_alias(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  s->walias = xcalloc(n ? n : 1, 1);
  if (warmc_getenv("WARMC_NO_WALIAS")) return;
  for (uint32_t i = 0; i < n; i++)
    if (s->lowonly[i] && f->insts[i].op == I_WRAP && !skip_location(s, i))
      s->walias[i] = 1;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    if (b->term == T_BRIF || b->term == T_BRTABLE) s->walias[b->term_arg] = 0;
  }
}

/* What the allocation leaves on the Isel, moved to the function when the
   first emission asks to run again to place its loops: nothing the second
   run's allocation reads differs from the first's -- only where the loops
   land does -- so the second takes these back instead of allocating again,
   and adds the counters the first one's allocation added.  For a function
   of 140,000 instructions that was a second of allocation repeated. */
typedef struct {
  int *loc;
  uint32_t nslots, used_gpr_cs, used_fpr_cs;
  int32_t *csa_slot;
  uint8_t *csa_on, *no_remat;
  uint32_t *csa_first, *csa_n;
  Ref *csa_vals;
  uint32_t ncsa_vals, cap_csa_vals;
  struct Piece *pc;
  uint32_t npc;
  uint32_t *pc_bfirst, *pc_bn;
  int32_t *pc_head, *pc_next;
  uint8_t *recomp;
  uint32_t *sp_first, *sp_n;
  Ref *sp_vals;
  uint32_t nsp_vals, cap_sp_vals;
  uint32_t *lv_in_off, *lv_out_off;
  Ref *lv_in, *lv_out;
  OptStats added;
} RaKeep;

static void ra_keep_drop(void *p) {
  RaKeep *k = p;
  xfree(k->loc); xfree(k->csa_slot); xfree(k->csa_on); xfree(k->no_remat);
  xfree(k->csa_first); xfree(k->csa_n); xfree(k->csa_vals);
  xfree(k->pc); xfree(k->pc_bfirst); xfree(k->pc_bn); xfree(k->pc_head);
  xfree(k->pc_next); xfree(k->recomp);
  xfree(k->sp_first); xfree(k->sp_n); xfree(k->sp_vals);
  xfree(k->lv_in_off); xfree(k->lv_out_off); xfree(k->lv_in); xfree(k->lv_out);
  xfree(k);
}

/* The counters src added since base, added to acc. */
static void stats_delta_add(OptStats *acc, const OptStats *base, const OptStats *now) {
  uint64_t *a = (uint64_t *)acc;
  const uint64_t *b = (const uint64_t *)base, *c = (const uint64_t *)now;
  for (size_t i = 0; i < sizeof *acc / sizeof(uint64_t); i++) a[i] += c[i] - b[i];
}

static void ra_keep_save(Isel *s, const OptStats *added) {
  RaKeep *k = xcalloc(1, sizeof *k);
#define MOVE(fld) do { k->fld = s->fld; s->fld = NULL; } while (0)
  MOVE(loc); MOVE(csa_slot); MOVE(csa_on); MOVE(no_remat);
  MOVE(csa_first); MOVE(csa_n); MOVE(csa_vals);
  MOVE(pc); MOVE(pc_bfirst); MOVE(pc_bn); MOVE(pc_head); MOVE(pc_next);
  MOVE(recomp);
  MOVE(sp_first); MOVE(sp_n); MOVE(sp_vals);
  MOVE(lv_in_off); MOVE(lv_out_off); MOVE(lv_in); MOVE(lv_out);
#undef MOVE
  k->nslots = s->nslots;
  k->used_gpr_cs = s->used_gpr_cs;
  k->used_fpr_cs = s->used_fpr_cs;
  k->ncsa_vals = s->ncsa_vals; k->cap_csa_vals = s->cap_csa_vals;
  k->npc = s->npc;
  k->nsp_vals = s->nsp_vals; k->cap_sp_vals = s->cap_sp_vals;
  k->added = *added;
  s->f->ra_keep = k;
  s->f->ra_keep_drop = ra_keep_drop;
}

static void ra_keep_take(Isel *s) {
  RaKeep *k = s->f->ra_keep;
  s->f->ra_keep = NULL;
  xfree(s->loc);
  xfree(s->no_remat);
#define MOVE(fld) do { s->fld = k->fld; k->fld = NULL; } while (0)
  MOVE(loc); MOVE(csa_slot); MOVE(csa_on); MOVE(no_remat);
  MOVE(csa_first); MOVE(csa_n); MOVE(csa_vals);
  MOVE(pc); MOVE(pc_bfirst); MOVE(pc_bn); MOVE(pc_head); MOVE(pc_next);
  MOVE(recomp);
  MOVE(sp_first); MOVE(sp_n); MOVE(sp_vals);
  MOVE(lv_in_off); MOVE(lv_out_off); MOVE(lv_in); MOVE(lv_out);
#undef MOVE
  s->nslots = k->nslots;
  s->used_gpr_cs = k->used_gpr_cs;
  s->used_fpr_cs = k->used_fpr_cs;
  s->ncsa_vals = k->ncsa_vals; s->cap_csa_vals = k->cap_csa_vals;
  s->npc = k->npc;
  s->nsp_vals = k->nsp_vals; s->cap_sp_vals = k->cap_sp_vals;
  OptStats zero;
  memset(&zero, 0, sizeof zero);
  stats_delta_add(&g_stats, &zero, &k->added);
  ra_keep_drop(k);
}

/* 1 = emitted, 0 = a conditional branch needs the far layout, 2 = the
   allocation filed a split plan; the caller applies it and retries, 4 = the
   loops it aligned are measured; the caller runs it again to place them. */
/* The outgoing argument area this function's own calls write, sized by
   them rather than by the module's widest signature: each call's stack
   arguments as emit_call_args_core places them -- x1-x7 and v0-v7 first,
   then 16-byte slots -- a runtime helper's with x1 and x2 taken by its
   immediates.  One that calls a wasm function at all also makes room for
   the stack arguments of any tail call (tail_bytes), which the callee
   writes where its own arrived.  A function that calls nothing has none. */
static uint32_t call_out_bytes(IrFunc *f, uint32_t tail_bytes) {
  uint32_t need = 0;
  bool wasm_call = false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      uint16_t op = f->insts[v].op;
      if (!inst_clobbers_caller_saved(f, v) || op == I_RETURN_CALL ||
          op == I_RETURN_CALL_INDIRECT || op == I_RETURN_CALL_REF)
        continue;
      bool wc = op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF;
      wasm_call |= wc;
      uint32_t ints = 0, flts = 0, slots = 0;
      for (uint32_t j = 0; j < f->insts[v].nargs; j++) {
        Ref a = ir_args(f, v)[j];
        if (is_fp_value(f, a) || is_vec_value(f, a)) {
          if (flts++ >= 8) slots++;
        } else if (ints++ >= (wc ? 7u : 5u)) {
          slots++;
        }
      }
      if (slots * ARG_STACK_SLOT > need) need = slots * ARG_STACK_SLOT;
    }
  }
  if (wasm_call && tail_bytes > need) need = tail_bytes;
  return (need + 15u) & ~15u;
}

/* Whether a quasi-leaf -- each of its calls followed by nothing but a trap,
   a panic helper's -- can make every one of them in a frame of its own
   (emit_cold_frame) and be a leaf everywhere else: no frame record, no
   stack check, x29 and x30 free for values.  TinyGo's method thunks, the
   less closure sort.Slice calls and unicode's is16 call nilPanic or
   lookupPanic and nothing else, and paid the record and the four
   instructions of the stack check on every run for those calls
   (gobench/methodval, sortints, unicode).  The cold frame is built once
   the arguments are in place, so a call qualifies when they are all in
   registers and it returns at most one value -- a stack argument or a
   result area would be in the frame just torn down -- and when nothing is
   live after it: the trap that follows takes no block arguments.  Any
   other call -- a runtime helper, a load barrier -- keeps the function an
   ordinary one.  The cold frame needs no stack check of its own: it is a
   frame record and an outgoing area below a caller that made its check,
   and a callee that calls makes its own. */
static bool cold_calls_only(Isel *s) {
  IrFunc *f = s->f;
  if (s->call_other || s->lab_reg || s->opt->profile || s->out_bytes > 0xfff)
    return false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (!inst_clobbers_caller_saved(f, v)) continue;
      if (in->op != I_CALL && in->op != I_CALL_INDIRECT && in->op != I_CALL_REF)
        return false;
      if (i + 1 != b->ninsts) return false;
      if (b->term == T_JUMP) {
        const Block *t = &f->blocks[b->succs[0].target];
        if (t->term != T_TRAP || t->ninsts || b->succs[0].nargs) return false;
      } else if (b->term != T_TRAP) {
        return false;
      }
      if (call_sig(s, in)->nresults > 1) return false;
      uint32_t nargs = in->nargs - (in->op == I_CALL ? 0u : 1u);
      uint32_t ints = 0, flts = 0;
      for (uint32_t j = 0; j < nargs; j++) {
        Ref a = ir_args(f, v)[j];
        if (is_fp_value(f, a) || is_vec_value(f, a)) flts++;
        else ints++;
      }
      if (ints > 7 || flts > 8) return false;
    }
  }
  return true;
}

/* ---- an early return ahead of the prologue ----
   A function whose entry block only tests its parameters and branches to a
   return that makes no call pays the whole prologue on the way to that
   return: TinyGo's walk of a tree, `if n == nil { return 0 }`, which half
   of its calls take, ran its frame, four callee-saved pairs and the stack
   check, fifteen instructions, before the cbz (gobench/alloc).  The test
   goes ahead of the prologue instead, on the parameters' own registers,
   and branches to a return of its own that makes its result from
   constants and parameters; the entry block, emitted after the prologue as
   before, then only goes the other way.  The early path skips what the
   entry block and the return block compute but the result, so the rest of
   that must be pure and unable to trap.

   The entry block may load from memory 0 and store to it too, the loads
   first: a destructor's `p = *this; *this = 0; if (!p) return;`
   (cppbench/uniqtree's ~unique_ptr, half of whose calls are on null) or
   Rust's drop of an Option<Box<T>>.  The test makes every load of the entry,
   in order, into a register of its own (ER_LOAD0 on, from the memory base in
   er->mb) and tests what they make; the early return makes the entry's stores
   from those registers, then its result.  The other way the entry block runs
   whole after the prologue, its loads reading what the test's read: nothing
   came between to write memory, which no other thread writes either -- a
   shared memory is refused when parsing, and an embedder's calls on one
   instance take turns.  A load that traps traps in the test, as it would
   have in the entry block before any of its stores. */
#define ER_LOADS  5
#define ER_STORES 4
#define ER_LOAD0  R10
#define ER_TA     R15
#define ER_TB     TMP0
typedef struct {
  uint32_t edge;       /* the entry's edge to the return */
  int kind;            /* 1 cbz, 2 cbnz, 0 a compare and b.cc */
  int sf, cc, ra, rb;  /* rb < 0: against imm, by cmn when neg */
  uint32_t imm;
  bool neg;
  Ref res;             /* what it returns, REF_NONE for nothing */
  uint32_t nloads, nstores;
  Ref loads[ER_LOADS], stores[ER_STORES];
  Ref ta, tb;          /* operands of the test made into ER_TA and ER_TB */
  int mb;              /* memory 0's base: x20 where pinned, else loaded to x9 */
} EarlyRet;

/* The register an integer parameter of the function arrives in, or -1. */
static int entry_param_reg(IrFunc *f, Ref v) {
  const Inst *in = &f->insts[v];
  if (in->op != I_PARAM || in->block != f->entry) return -1;
  const Block *e = &f->blocks[f->entry];
  int ni = 1;
  for (uint32_t i = 0; i < f->sig->nparams && i < e->nparams; i++) {
    bool fp = vt_is_vec_reg(f->sig->params[i]);
    if (e->params[i] == v) return fp || ni > 7 ? -1 : ni;
    if (!fp) ni++;
  }
  return -1;
}

static bool early_pure_op(uint16_t op) {
  switch (op) {
    case I_CONST: case I_PARAM: case I_NOP:
    case I_ADD: case I_SUB: case I_MUL: case I_AND: case I_OR: case I_XOR:
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTL: case I_ROTR:
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_WRAP: case I_EXTENDS: case I_EXTENDU: case I_SELECT:
    case I_EXT8S: case I_EXT16S: case I_EXT32S:
    case I_MEM_ADDR: case I_MEM_PTR: case I_MEM_BASE:
    /* the shadow stack pointer TinyGo and Rust read on entry, which only
       the path past the test goes on to use */
    case I_GLOBAL_GET:
      return true;
    default:
      return false;
  }
}

/* The register v is in ahead of the prologue: a parameter's, or a load's
   the test made; -1 for anything else. */
static int early_reg(IrFunc *f, const EarlyRet *er, Ref v) {
  int p = entry_param_reg(f, v);
  if (p >= 0 || !er) return p;
  for (uint32_t i = 0; i < er->nloads; i++)
    if (er->loads[i] == v) return ER_LOAD0 + (int)i;
  return -1;
}

/* v with the return block's parameters replaced by what the edge passes */
static Ref early_arg(IrFunc *f, const Edge *ed, Ref v) {
  const Inst *in = &f->insts[v];
  if (in->op != I_PARAM || in->block != ed->target) return v;
  const Block *r = &f->blocks[ed->target];
  for (uint32_t j = 0; j < r->nparams && j < ed->nargs; j++)
    if (r->params[j] == v) return ed->args[j];
  return REF_NONE;
}

/* Makes v, an integer computed by the return block or the entry from the
   entry's parameters, the test's loads and constants, in d -- which none of
   those is in -- writing no other register; with b NULL only says whether
   it can.  Rust's fib returns acc + n, both arguments of the return block,
   and the entry passes it 0 and n. */
static bool early_make(IrFunc *f, const EarlyRet *er, const Edge *ed, Ref v, int d, Buf *b,
                       int depth) {
  const Inst *in = &f->insts[v];
  ValType t = (ValType)in->type;
  if ((t != VT_I32 && t != VT_I64) || depth > 6) return false;
  int sf = t == VT_I64 ? SF64 : SF32;
  if (in->op == I_CONST) {
    if (b) a64_load_imm(b, sf, d, in->imm);
    return true;
  }
  int p = early_reg(f, er, v);
  if (p >= 0) {
    if (b) a64_mov_reg(b, SF64, d, p);
    return true;
  }
  if (in->op == I_PARAM) {
    Ref x = early_arg(f, ed, v);
    return x != v && x != REF_NONE && early_make(f, er, ed, x, d, b, depth + 1);
  }
  if (in->block != ed->target && in->block != f->entry) return false;
  Ref *a = ir_args(f, v);
  switch (in->op) {
    case I_EXTENDU: case I_EXTENDS: {
      if ((ValType)f->insts[a[0]].type != VT_I32) return false;
      Ref x = early_arg(f, ed, a[0]);
      int q = x == REF_NONE ? -1 : early_reg(f, er, x);
      if (in->op == I_EXTENDU && q >= 0) {
        if (b) a64_mov_reg(b, SF32, d, q);
        return true;
      }
      if (!early_make(f, er, ed, a[0], d, b, depth + 1)) return false;
      if (b && in->op == I_EXTENDU) a64_uxtw(b, d, d);
      else if (b) a64_sxtw(b, d, d);
      return true;
    }
    case I_WRAP:
      return early_make(f, er, ed, a[0], d, b, depth + 1);
    case I_ADD: case I_SUB: case I_AND: case I_OR: case I_XOR: {
      Ref x = early_arg(f, ed, a[0]), y = early_arg(f, ed, a[1]);
      if (x == REF_NONE || y == REF_NONE) return false;
      if (in->op != I_SUB && f->insts[x].op == I_CONST) { Ref z = x; x = y; y = z; }
      const Inst *yi = &f->insts[y];
      if (yi->op == I_CONST) {
        uint64_t k = sf ? yi->imm : (yi->imm & 0xffffffffu);
        if (k == 0 && in->op != I_AND) return early_make(f, er, ed, x, d, b, depth + 1);
        if (in->op == I_ADD || in->op == I_SUB) {
          bool sub = in->op == I_SUB;
          uint64_t neg = sf ? 0 - k : (uint32_t)(0 - (uint32_t)k);
          uint32_t imm;
          if (k <= 0xfff) imm = (uint32_t)k;
          else if (neg <= 0xfff) { imm = (uint32_t)neg; sub = !sub; }
          else return false;
          if (!early_make(f, er, ed, x, d, b, depth + 1)) return false;
          if (b && sub) a64_sub_imm(b, sf, d, d, imm, 0);
          else if (b) a64_add_imm(b, sf, d, d, imm, 0);
          return true;
        }
        uint32_t enc;
        if (!a64_logical_imm(k, sf, &enc) || !early_make(f, er, ed, x, d, b, depth + 1))
          return false;
        if (b && in->op == I_AND) a64_and_imm(b, sf, d, d, enc);
        else if (b && in->op == I_OR) a64_orr_imm(b, sf, d, d, enc);
        else if (b) a64_eor_imm(b, sf, d, d, enc);
        return true;
      }
      int q = early_reg(f, er, y);
      if (q < 0 || !early_make(f, er, ed, x, d, b, depth + 1)) return false;
      if (!b) return true;
      switch (in->op) {
        case I_ADD: a64_add_reg(b, sf, d, d, q); break;
        case I_SUB: a64_sub_reg(b, sf, d, d, q); break;
        case I_AND: a64_and_reg(b, sf, d, d, q); break;
        case I_OR: a64_orr_reg(b, sf, d, d, q); break;
        default: a64_eor_reg(b, sf, d, d, q); break;
      }
      return true;
    }
    default:
      return false;
  }
}

/* Whether v is the constant 0 of its type: a store of it stores xzr. */
static bool early_zero(IrFunc *f, Ref v) {
  const Inst *vi = &f->insts[v];
  if (vi->op != I_CONST) return false;
  return ((ValType)vi->type == VT_I64 ? vi->imm : (vi->imm & 0xffffffffu)) == 0;
}

/* The access v of the entry -- an integer load or store of memory 0 -- as
   the test (a load) or the early return (a store) makes it: at the index in
   *idx (REF_NONE for none, the address all in the offset) plus *off, which
   an ldr or str carries; a store's value early_make can make or zero. */
static bool early_access_ok(IrFunc *f, const EarlyRet *er, const Edge *ed, Ref v, Ref *idx,
                            uint64_t *off) {
  const Inst *in = &f->insts[v];
  if (MEMF_MEM(in->imm2) != 0 || !f->m->nmemories || f->m->memories[0].index64) return false;
  uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
  Ref val = in->op == I_STORE ? ir_args(f, v)[1] : REF_NONE;
  ValType t = (ValType)(val == REF_NONE ? in->type : f->insts[val].type);
  if (t != VT_I32 && t != VT_I64) return false;
  Ref a0 = ir_args(f, v)[0];
  const Inst *ai = &f->insts[a0];
  /* the address, or the pointer pass_mem_ptr gives accesses at offsets from
     one base, the memory base plus the same index */
  uint64_t at = ai->imm;
  if ((ai->op == I_MEM_ADDR || (ai->op == I_MEM_PTR && ai->imm2 == 0)) && ai->nargs == 1) {
    *idx = ir_args(f, a0)[0];
  } else if (ai->op == I_MEM_ADDR && ai->nargs == 2 &&
             f->insts[ir_args(f, a0)[1]].op == I_MEM_BASE && f->insts[ir_args(f, a0)[1]].imm2 == 0) {
    /* the index on a pointer to the offset (the ptr_fuse form) */
    *idx = ir_args(f, a0)[0];
    at = f->insts[ir_args(f, a0)[1]].imm;
  } else if (ai->op == I_CONST) {
    *idx = REF_NONE;
  } else {
    return false;
  }
  if (*idx != REF_NONE && is64(f, *idx)) return false;
  *off = at + in->imm;
  if ((*off & ((1u << size) - 1u)) ? *off >= 256 : (*off >> size) > 0xfff) return false;
  if (*idx != REF_NONE && early_reg(f, er, *idx) < 0 &&
      !early_make(f, er, ed, *idx, TMP0, NULL, 0))
    return false;
  if (val != REF_NONE && !early_zero(f, val) && !early_make(f, er, ed, val, TMP1, NULL, 0))
    return false;
  return true;
}

/* The access v, which early_access_ok has passed: a load into rt, or a
   store of what rt holds, the address made in at where it takes a
   register. */
static void early_access(IrFunc *f, const EarlyRet *er, const Edge *ed, Ref v, int rt, int at,
                         Buf *b) {
  const Inst *in = &f->insts[v];
  int size = (int)MEMF_SIZE(in->imm2);
  bool load = in->op == I_LOAD;
  bool sgn = load && (in->imm2 & MEMF_SIGNED) && size < 3;
  int wide = load && (ValType)in->type == VT_I64;
  Ref idx;
  uint64_t off;
  if (!early_access_ok(f, er, ed, v, &idx, &off))
    warmc_fatal("internal: an early return's access could not be made");
  int base = er->mb;
  if (idx != REF_NONE) {
    int x = early_reg(f, er, idx);
    if (x < 0) { early_make(f, er, ed, idx, at, b, 0); x = at; }
    if (off == 0) {
      if (!load) a64_str_reg(b, size, rt, er->mb, x, A64_IDX_UXTW, 0);
      else if (sgn) a64_ldrs_reg(b, size, wide, rt, er->mb, x, A64_IDX_UXTW, 0);
      else a64_ldr_reg(b, size, rt, er->mb, x, A64_IDX_UXTW, 0);
      return;
    }
    a64_add_uxtw(b, at, er->mb, x, 0);
    base = at;
  }
  if (!load) a64_str_imm(b, size, rt, base, (uint32_t)off);
  else if (sgn) a64_ldrs_imm(b, size, wide, rt, base, (uint32_t)off);
  else a64_ldr_imm(b, size, rt, base, (uint32_t)off);
}

/* x as an operand of the test: the register it is in, or d after the test
   has made it there (noted in *made); -1 when it can be neither. */
static int early_operand(IrFunc *f, const EarlyRet *er, const Edge *ed, Ref x, int d,
                         Ref *made) {
  int r = early_reg(f, er, x);
  if (r >= 0) return r;
  if (!early_make(f, er, ed, x, d, NULL, 0)) return -1;
  *made = x;
  return d;
}

static bool early_ret_shape(Isel *s, EarlyRet *er) {
  IrFunc *f = s->f;
  if (s->far_br || s->opt->profile || f->sig->nresults > 1 || !f->norder ||
      f->order[0] != f->entry)
    return false;
  const Block *e = &f->blocks[f->entry];
  if (e->term != T_BRIF || e->npreds || e->is_pad || e->nsuccs != 2 ||
      e->succs[0].eh || e->succs[1].eh || e->succs[0].target == e->succs[1].target)
    return false;
  er->nloads = er->nstores = 0;
  er->ta = er->tb = REF_NONE;
  er->mb = s->pin ? R20 : R9;
  bool stored = false;
  for (uint32_t i = 0; i < e->ninsts; i++) {
    Ref v = e->insts[i];
    uint16_t op = f->insts[v].op;
    if (early_pure_op(op)) continue;
    Ref idx;
    uint64_t off;
    if (op == I_LOAD && !stored && er->nloads < ER_LOADS &&
        early_access_ok(f, er, &e->succs[0], v, &idx, &off)) {
      er->loads[er->nloads++] = v;
      continue;
    }
    if (op == I_STORE && er->nstores < ER_STORES &&
        early_access_ok(f, er, &e->succs[0], v, &idx, &off)) {
      er->stores[er->nstores++] = v;
      stored = true;
      continue;
    }
    return false;
  }
  int which = -1;
  for (int k = 0; k < 2 && which < 0; k++) {
    const Edge *ed = &e->succs[k];
    const Block *r = &f->blocks[ed->target];
    if (r->term != T_RETURN || r->is_pad || r->nret != f->sig->nresults) continue;
    bool ok = true;
    for (uint32_t i = 0; i < r->ninsts && ok; i++)
      if (!early_pure_op(f->insts[r->insts[i]].op)) ok = false;
    Ref res = r->nret ? r->ret_args[0] : REF_NONE;
    if (ok && res != REF_NONE)
      ok = !vt_is_float(f->sig->results[0]) && f->sig->results[0] != VT_V128 &&
           early_make(f, er, ed, res, R0, NULL, 0);
    if (ok) { which = k; er->res = res; }
  }
  if (which < 0) return false;
  er->edge = (uint32_t)which;
  bool on_true = which == 0;
  const Edge *ed = &e->succs[which];

  Ref c = e->term_arg;
  const Inst *ci = &f->insts[c];
  Ref *a = ir_args(f, c);
  er->rb = -1; er->imm = 0; er->neg = false; er->cc = CC_AL;
  if (ci->op == I_EQZ) {
    if ((er->ra = early_operand(f, er, ed, a[0], ER_TA, &er->ta)) < 0) return false;
    er->kind = on_true ? 1 : 2;
    er->sf = is64(f, a[0]) ? SF64 : SF32;
    return true;
  }
  if (!is_int_cmp(ci->op)) {
    if ((er->ra = early_operand(f, er, ed, c, ER_TA, &er->ta)) < 0) return false;
    er->kind = on_true ? 2 : 1;
    er->sf = is64(f, c) ? SF64 : SF32;
    return true;
  }
  if ((er->ra = early_operand(f, er, ed, a[0], ER_TA, &er->ta)) < 0) return false;
  er->sf = is64(f, a[0]) ? SF64 : SF32;
  if (f->insts[a[1]].op == I_CONST) {
    uint64_t m = er->sf ? f->insts[a[1]].imm : (f->insts[a[1]].imm & 0xffffffffu);
    if ((ci->op == I_EQ || ci->op == I_NE) && m == 0) {
      er->kind = (ci->op == I_EQ) == on_true ? 1 : 2;
      return true;
    }
    uint64_t neg = (uint64_t)(-(int64_t)(er->sf ? (int64_t)m : (int64_t)(int32_t)m));
    if (m <= 0xfff) er->imm = (uint32_t)m;
    else if (neg <= 0xfff) { er->imm = (uint32_t)neg; er->neg = true; }
    else return false;
  } else if ((er->rb = early_operand(f, er, ed, a[1], ER_TB, &er->tb)) < 0) {
    return false;
  }
  er->kind = 0;
  er->cc = on_true ? cc_for(ci->op) : cc_invert(cc_for(ci->op));
  return true;
}

/* The test, its branch left for early_ret_stub to aim; the branch's offset.
   The loads of the entry first, then the operands made, then the test. */
static size_t early_ret_test(IrFunc *f, Buf *b, const EarlyRet *er) {
  const Edge *ed = &f->blocks[f->entry].succs[er->edge];
  if ((er->nloads || er->nstores) && er->mb != R20)
    a64_ldr_imm(b, 3, er->mb, R0, VMCTX_MEM_BASE);
  for (uint32_t i = 0; i < er->nloads; i++)
    early_access(f, er, ed, er->loads[i], ER_LOAD0 + (int)i, ER_LOAD0 + (int)i, b);
  if (er->ta != REF_NONE) early_make(f, er, ed, er->ta, ER_TA, b, 0);
  if (er->tb != REF_NONE) early_make(f, er, ed, er->tb, ER_TB, b, 0);
  if (er->kind == 0) {
    if (er->rb >= 0) a64_cmp_reg(b, er->sf, er->ra, er->rb);
    else if (er->neg) a64_adds_imm(b, er->sf, RZR, er->ra, er->imm, 0);
    else a64_cmp_imm(b, er->sf, er->ra, er->imm);
  }
  size_t site = b->len;
  if (er->kind == 0) a64_bcond(b, er->cc, 0);
  else if (er->kind == 1) a64_cbz(b, er->sf, er->ra, 0);
  else a64_cbnz(b, er->sf, er->ra, 0);
  return site;
}

/* The return the test goes to, after the body: the entry's stores, the
   result, and ret with nothing to undo. */
static void early_ret_stub(Isel *s, const EarlyRet *er, Buf *pro, size_t site) {
  IrFunc *f = s->f;
  if (!a64_patch_b19_ok(pro, site, pro->len + B->len)) {
    s->far_br_needed = true;
    return;
  }
  const Edge *ed = &f->blocks[f->entry].succs[er->edge];
  for (uint32_t i = 0; i < er->nstores; i++) {
    Ref st = er->stores[i], val = ir_args(f, st)[1];
    int rt = RZR;
    if (!early_zero(f, val)) {
      early_make(f, er, ed, val, TMP1, B, 0);
      rt = TMP1;
    }
    early_access(f, er, ed, st, rt, TMP0, B);
  }
  if (er->res != REF_NONE && !early_make(f, er, ed, er->res, R0, B, 0))
    warmc_fatal("internal: an early return's result could not be made");
  a64_ret(B);
}

/* The lead of a function another instance may call, with the instance and
   memory base pinned (codegen_entry_off): the address the module hands
   out for it -- to its exports, its references, the imports bound to it --
   is the cmp, ENTRY_CHECK_BYTES ahead of the body, and a call from code of
   the same instance, whose x19 and x20 are already the body's, goes
   straight on into it.  One from any other, or from the host with its own
   instance in x0, takes the branch back to the stub ahead, which hands
   the body to warmc_xenter (rt/start.S) to call with the two set and to
   put the caller's back after, and the area of stack arguments to copy:
   the body's own, and room for those of any tail call the module makes,
   which go where the body's arrived.  The direct calls of the module's
   own code, and its call_indirect code tables, name the body.  The lead's
   relocation is returned through *rel. */
static void emit_entry_check(Isel *s, Buf *lead, uint32_t out_bytes, Reloc *rel) {
  const CompType *sig = s->f->sig;
  uint32_t ints = 1, flts = 0, slots = 0;
  for (uint32_t i = 0; i < sig->nparams; i++) {
    if (vt_is_vec_reg(sig->params[i])) { if (flts < 8) { flts++; continue; } }
    else if (ints <= 7) { ints++; continue; }
    slots++;
  }
  uint32_t area = ARG_STACK_SLOT * slots;
  if (out_bytes > area) area = out_bytes;
  area = (area + 15u) & ~15u;
  a64_adr(lead, R10, 0);
  a64_load_imm(lead, SF64, R9, area);
  rel->kind = REL_CALL26; rel->section = 0; rel->offset = lead->len;
  rel->sym = obj_intern_sym(s->o, "warmc_xenter", SYM_UNDEF_FUNC, -1, 0);
  rel->addend = 0;
  a64_b(lead, 0);
  a64_cmp_reg(lead, SF64, R0, R19);
  a64_bcond(lead, CC_NE, -(int32_t)(lead->len / 4));
  a64_patch_adr(lead, 0, lead->len);
}

static int produce_body(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                        uint32_t out_bytes, FuncOut *fout, bool far,
                        SplitCtl *sc, bool split_collect) {
  int split_ret = 0;
  bool refit = false;
  Buf pro;
  memset(&pro, 0, sizeof pro);
  Isel st;
  memset(&st, 0, sizeof st);
  Isel *s = &st;
  s->f = f; s->o = obj; s->opt = opt;
  s->split_sc = sc;
  s->split_collect = split_collect;
  s->out_bytes = call_out_bytes(f, out_bytes);
  s->far_br = far || warmc_getenv("WARMC_FAR_BRANCH") != NULL;
  s->naive = !opt->regalloc;

  s->loc = xmalloc(f->ninsts * sizeof(int));
  s->cls = xmalloc(f->ninsts);
  for (uint32_t i = 0; i < f->ninsts; i++) {
    s->loc[i] = LOC_NONE;
    s->cls[i] = (uint8_t)(is_fp_value(f, i) ? 1 : 0);
  }

  s->no_small_memop = warmc_getenv("WARMC_NO_SMALL_MEMOP") != NULL;
  s->no_rc_def = warmc_getenv("WARMC_NO_RC_DEF") != NULL;
  s->pin = codegen_pinned();
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
  s->st_end = (size_t)-1;
  {

    /* A call that only a trap follows -- a panic helper's -- leaves nothing
       after it to need the instance or memory registers, so they may stay in
       caller-saved registers.  "Diverges" alone is not enough: a block that
       ends in a tail call diverges too, and the code before that call reads
       the instance. */
    bool leaf = f->npads == 0, quasi = f->npads == 0;
    for (uint32_t bi = 0; bi < f->nblocks && quasi; bi++) {
      Block *bb = &f->blocks[bi];
      if (!bb->reachable) continue;
      if (bb->is_pad) { leaf = quasi = false; break; }
      for (uint32_t i = 0; i < bb->ninsts; i++)
        if (inst_clobbers_caller_saved(f, bb->insts[i])) {
          leaf = false;
          bool traps = i + 1 == bb->ninsts &&
                       (bb->term == T_TRAP || bb->term == T_JUMP);
          for (uint32_t k = 0; k < bb->nsuccs && traps; k++) {
            Block *t = &f->blocks[bb->succs[k].target];
            if (t->term != T_TRAP || t->ninsts) traps = false;
          }
          if (!traps) { quasi = false; break; }
        }
    }

    if ((leaf || quasi) && !s->pin) {
      if (s->needs_vmctx) s->vmctx_reg = R9;
      if (s->needs_mem) s->membase_reg = R10;
    }
    if (quasi && !leaf && cold_calls_only(s)) {
      s->has_call = false;
      s->cold_frame = true;
      s->cold_out = s->out_bytes;
      s->out_bytes = 0;
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
      /* The hot type's code table, when it has one, is what the register
         holds: its address is an adrp and an add at each call otherwise.
         A second register for the second hottest type's table, where one
         loop calls through both (Rust's fmt::write, rsbench/fmtfloat; a
         trait object's two methods, rsbench/dyntrait), measured nothing
         either way, and cost asbench/hofarr 23% in spills where the two
         were different loops' of one big function. */
      for (uint32_t k = 0; k < f->m->nict; k++)
        if (f->m->ict[k].type_id == best_id) { s->tid_tab = true; s->tid_ict = k; }
    }
  }

  analyse_immediates(s);
  compute_mem_validity(s);
  analyse_wrap_alias(s);

  s->pinned = xcalloc(f->ninsts ? f->ninsts : 1, 1);
  if (s->has_pads)
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *b = &f->blocks[bi];
      if (!b->reachable || !b->is_pad) continue;
      for (uint32_t p = 0; p < b->nparams; p++) s->pinned[b->params[p]] = 1;
    }

  OptStats ra_added, ra_base;
  memset(&ra_added, 0, sizeof ra_added);
  bool ra_taken = false;
  if (s->naive) regalloc_naive(s);
  else if (f->ra_keep) { ra_keep_take(s); ra_taken = true; }
  else {
    ra_base = g_stats;
    regalloc_linear(s);
    stats_delta_add(&ra_added, &ra_base, &g_stats);
  }

  if (sc && s->split_collect && sc->nflagged) { split_ret = 2; goto give_up; }
  if (sc && !s->split_collect && !sc->accept && sc->nfailed_new) {
    split_ret = 3;
    goto give_up;
  }

  if (s->has_call && !s->naive) { s->rc_reg[0] = R29; s->rc_reg[1] = R30; }
  /* F3 and F4 are scratch for a few vector sequences only, which drop what
     they hold (emit_inst), and F3 breaks move cycles, which drops it too. */
  if (!s->naive) { s->frc_reg[0] = F3; s->frc_reg[1] = F4; }
  if (!ra_taken) {
    ra_base = g_stats;
    compute_safepoints(s);
    stats_delta_add(&ra_added, &ra_base, &g_stats);
  }

  s->slots_off = s->out_bytes;

  s->retarea_off = (s->out_bytes + 8u * s->nslots + 15u) & ~15u;
  uint32_t maxres = s->needs_retarea ? module_max_results(f->m) : 0;
  s->retptr_slot = s->retarea_off + RESULT_SLOT_SIZE * maxres;
  s->fpsave_off = s->retptr_slot + (f->sig->nresults > 1 ? 8u : 0u);
  uint32_t nfp = 0;
  for (int r = 8; r <= 15; r++) if (s->used_fpr_cs & (1u << r)) nfp++;
  s->exn_slot = s->fpsave_off + 8u * nfp;

  s->throw_area_off = (s->exn_slot + (s->has_pads ? 8u : 0u) + 15u) & ~15u;
  s->frame = s->throw_area_off + s->throw_area_bytes;
  s->frame = (s->frame + 15) & ~15u;
  /* Past a scaled offset's reach the slots go last, behind everything else
     the frame holds and a save slot for sp_access, so only they and the
     stack parameters above the frame are far.  1 MB stays inside the 2 MB
     the runtime leaves below its stack limit, which a leaf, never checking,
     may run into. */
  s->far_frame = s->frame > 32000;
  if (s->far_frame) {
    s->far_save = s->out_bytes;
    s->retarea_off = (s->out_bytes + 8u + 15u) & ~15u;
    s->retptr_slot = s->retarea_off + RESULT_SLOT_SIZE * maxres;
    s->fpsave_off = s->retptr_slot + (f->sig->nresults > 1 ? 8u : 0u);
    s->exn_slot = s->fpsave_off + 8u * nfp;
    s->throw_area_off = (s->exn_slot + (s->has_pads ? 8u : 0u) + 15u) & ~15u;
    s->slots_off = (s->throw_area_off + s->throw_area_bytes + 15u) & ~15u;
    s->frame = (s->slots_off + 8u * s->nslots + 15u) & ~15u;
  }
  {
    int list[NCS_GPR + 1];
    int ncs = cs_gpr_list(s, list);
    s->saved_bytes = (uint32_t)(8 * ncs) + (saves_lr(s) ? 16u : 0u);
  }
  if (s->frame > (1u << 20))
    warmc_fatal("function %s needs a %u byte frame, beyond the current addressing limit",
                f->m->funcs[f->func_index].sym, s->frame);
  if (s->frame > g_stats.max_frame_bytes) g_stats.max_frame_bytes = s->frame;

  s->prof_site = xcalloc(f->nblocks, sizeof(size_t));
  s->prof_start = xcalloc(f->nblocks, sizeof(size_t));
  s->block_off = xmalloc(f->nblocks * sizeof(uint32_t));
  for (uint32_t i = 0; i < f->nblocks; i++) s->block_off[i] = 0xffffffffu;

  /* An empty prologue leaves an early return nothing to skip. */
  EarlyRet er;
  size_t er_site = 0;
  bool early = early_ret_shape(s, &er);
  if (early) er_site = early_ret_test(f, &pro, &er);
  size_t pro_from = pro.len;
  emit_prologue(s, &pro);
  if (early && pro.len == pro_from) { pro.len = 0; early = false; }
  if (early) s->early_main = 1u + (er.edge ^ 1u);
  /* A return block only the entry went to is gone with that edge: laid out
     after the entry, it would cost the rest of the function a branch over
     it (cbench/bintree's wrappers, dlfree). */
  uint32_t dead_ret = UINT32_MAX;
  if (early) {
    uint32_t r = f->blocks[f->entry].succs[er.edge].target;
    if (f->blocks[r].npreds == 1 && f->blocks[r].preds[0] == f->entry) dead_ret = r;
  }

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
  const char *lal_s = warmc_getenv("WARMC_LOOP_ALIGN");
  uint32_t lalign = lal_s ? (uint32_t)strtoul(lal_s, NULL, 0) : 16;
  if (lalign != 16 && lalign != 32) lalign = 0;
  /* A loop small enough to fit in one 64-byte fetch block is aligned to
     one, so it never straddles two: asbench/sum's 11-instruction loop, 16-
     aligned at ...1ff0, crossed 0x2000 and ran 43% slower than the same
     instructions at ...2020.  The size is estimated from the IR before the
     loop is emitted, at most SMALL_LOOP_INSTS instructions; more than four
     nops of padding become a branch over them, so a loop entered often pays
     one taken branch, not a row of nops. */
  bool small_align = lalign && opt->small_loop_align;
  /* Aligning to 64 pads by up to 48 bytes where the loop may well have fit
     where it was: vecbench/condsum's inner loop, 28 bytes, fit at a 32 mod
     64 offset, and the branch over the 32 bytes of padding that put it at
     the next block cost a taken branch on each of the many entries an outer
     loop makes, 17% of the kernel.  Only the emitted size tells, so the
     first emission measures each small loop it aligned (loop_bytes keeps
     the sizes on the function) and asks to be run again when some loop
     would be placed with less padding; the second places every one by
     fit_pad, the least padding that keeps it inside a block, and asks no
     more.  Off with far branches, whose sizes are not the first run's. */
  bool fit = small_align && !s->far_br;
  const uint16_t *span_of = fit ? f->loop_bytes : NULL;
  uint32_t phase = span_of ? f->loop_phase : 0;
  uint32_t *blk_end = fit && !span_of ? xmalloc(f->nblocks * sizeof(uint32_t)) : NULL;
  uint32_t *rec = NULL, nrec = 0;

  uint32_t neo, nsmall = 0;
  const uint32_t *eo = ir_emit_order(f, dead_ret, &neo);
  s->prev_bi = UINT32_MAX;
  for (uint32_t oi = 0; oi < neo; oi++) {
    uint32_t bi = eo[oi];
    if (lalign && opt->align && f->blocks[bi].loop_depth) {
      Block *lb = &f->blocks[bi];
      bool header = false;
      for (uint32_t p = 0; p < lb->npreds && !header; p++)
        if (f->blocks[lb->preds[p]].rpo >= lb->rpo) header = true;
      bool small = header && small_align && small_loop(f, bi);
      if (small) nsmall++;
      uint32_t want = small ? 64 : lalign;
      if (header) {
        uint32_t at = phase + (uint32_t)(pro.len + B->len);
        uint32_t pad = (want - (at & (want - 1))) & (want - 1);
        if (small && span_of && span_of[bi]) pad = fit_pad(at, span_of[bi], lalign);
        if (small && blk_end) {
          rec = xrealloc(rec, (nrec + 1) * 4 * sizeof(uint32_t));
          rec[4 * nrec] = bi; rec[4 * nrec + 1] = oi;
          rec[4 * nrec + 2] = at; rec[4 * nrec + 3] = pad;
          nrec++;
        }
        if (pad > 16) {
          size_t site = B->len;
          a64_b(B, 0);
          while (phase + pro.len + B->len < at + pad) a64_nop(B);
          patch_b26_checked(s, site, B->len);
        } else {
          while (phase + pro.len + B->len < at + pad) a64_nop(B);
        }
      }
    }
    s->block_off[bi] = (uint32_t)B->len;
    s->st_end = (size_t)-1;
    s->mem_valid = s->mem_valid_in[bi] != 0;
    if (opt->profile) {
      a64_ldr_imm(B, 3, TMP0, REG_VMCTX, 56);
      s->prof_site[bi] = B->len;
      a64_add_imm(B, SF64, TMP0, TMP0, 0, 0);
      a64_str_imm(B, 3, TMP0, REG_VMCTX, 56);
      s->prof_start[bi] = B->len;
      s->prof_skip = 0;
    }
    emit_block(s, bi, oi + 1 < neo ? eo[oi + 1] : 0xffffffffu);
    s->prev_bi = bi;
    if (blk_end) blk_end[bi] = (uint32_t)B->len;
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

  /* Placed again with each size known, does any loop move?  Padding is the
     only code that depends on where it lands, so the second run's offsets
     are the first's shifted by the change in padding before them. */
  if (blk_end && nrec) {
    uint16_t *spans = ir_alloc(f, f->nblocks * sizeof(uint16_t));
    memset(spans, 0, f->nblocks * sizeof(uint16_t));
    for (uint32_t k = 0; k < nrec; k++) {
      uint32_t span = loop_span(s, rec[4 * k], eo, neo, rec[4 * k + 1], blk_end);
      if (span && span <= 0xffff) spans[rec[4 * k]] = (uint16_t)span;
    }
    uint32_t ph = 0;
    if (!warmc_getenv("WARMC_NO_LOOP_PHASE")) {
      uint8_t *hdr = xcalloc(f->nblocks ? f->nblocks : 1, 1);
      for (uint32_t b = 0; b < f->nblocks; b++) {
        Block *lb = &f->blocks[b];
        if (!lb->reachable || !lb->loop_depth) continue;
        for (uint32_t p = 0; p < lb->npreds; p++)
          if (f->blocks[lb->preds[p]].rpo >= lb->rpo) hdr[b] = 1;
      }
      ph = loop_phase(f, rec, nrec, spans, lalign, hdr);
      xfree(hdr);
    }
    int64_t shift = 0;
    for (uint32_t k = 0; k < nrec; k++) {
      uint32_t h = rec[4 * k], at = rec[4 * k + 2], pad = rec[4 * k + 3];
      if (!spans[h]) continue;
      uint32_t np = fit_pad((uint32_t)((int64_t)at + shift + ph), spans[h], lalign);
      if (np != pad) refit = true;
      shift += (int64_t)np - (int64_t)pad;
    }
    if (ph) refit = true;
    if (refit) { f->loop_bytes = spans; f->loop_phase = (uint8_t)ph; }
  }
  xfree(blk_end); xfree(rec);

  if (early) early_ret_stub(s, &er, &pro, er_site);
  emit_alloc_slow_arms(s);
  emit_ictab_slow_arms(s);
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
  if (s->so_site &&
      !a64_patch_b19_ok(&pro, s->so_site - 1, pro.len + trap_at[TRAP_STACK_OVERFLOW]))
    s->far_br_needed = true;

  for (BrFix *fx = s->fixups; fx;) {
    BrFix *n = fx->next;
    if (fx->is19 == BRFIX_TABLE) {
      int32_t w = (int32_t)((int64_t)s->block_off[fx->target] - (int64_t)fx->base);
      memcpy(B->data + fx->site, &w, 4);
    } else if (fx->is19 == BRFIX_TABLE16) {
      if (!table16_put(B, fx->site, fx->base, s->block_off[fx->target]))
        s->far_br_needed = true;
    } else if (fx->is19) br_patch(s, fx->site, s->block_off[fx->target]);
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

  if (warmc_getenv("WARMC_VERIFY_RA") && s->csa_consumed != s->ncsa_vals)
    warmc_fatal("internal: %u caller-save entries built but %u emitted in %s",
                s->ncsa_vals, s->csa_consumed, f->m->funcs[f->func_index].sym);

  if (s->far_br_needed) goto give_up;
  if (refit) {
    if (!s->naive) ra_keep_save(s, &ra_added);
    split_ret = 4;
    goto give_up;
  }

  if (s->cold_frame) g_stats.cold_frames++;
  if (early) g_stats.early_rets++;
  g_stats.small_loops += nsmall;
  Buf lead;
  memset(&lead, 0, sizeof lead);
  Reloc lead_rel;
  if (s->pin && codegen_entry_off(f->m, f->func_index)) {
    emit_entry_check(s, &lead, out_bytes, &lead_rel);
    g_stats.entry_checks++;
  }
  memset(fout, 0, sizeof *fout);
  fout->want_align = opt->align && has_loop;
  fout->phase = fout->want_align ? (uint8_t)phase : 0;
  if (fout->phase) g_stats.loop_phases++;
  fout->entry_off = (uint32_t)lead.len;
  fout->code_len = (uint32_t)(lead.len + pro.len + s->body.len);
  fout->code = xmalloc(fout->code_len ? fout->code_len : 1);
  if (lead.len) memcpy(fout->code, lead.data, lead.len);
  if (pro.len) memcpy(fout->code + lead.len, pro.data, pro.len);
  if (s->body.len) memcpy(fout->code + lead.len + pro.len, s->body.data, s->body.len);
  fout->nrelocs = s->nrelocs + (lead.len ? 1u : 0u);
  fout->relocs = xmalloc((fout->nrelocs ? fout->nrelocs : 1) * sizeof(Reloc));
  for (uint32_t i = 0; i < s->nrelocs; i++) {
    Reloc *r = &s->relocs[i];
    uint64_t off = r->offset;
    if (off & 0x8000000000000000ull) off &= ~0x8000000000000000ull;
    else off += pro.len;
    fout->relocs[i] = *r;
    fout->relocs[i].section = 0;
    fout->relocs[i].offset = off + lead.len;
  }
  if (lead.len) fout->relocs[s->nrelocs] = lead_rel;
  buf_free(&lead);
  const char *sym = f->m->funcs[f->func_index].sym;
  fout->sym = sym;
  fout->insts_emitted = fout->code_len / 4;
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
    u.saved = saves_lr(s) ? UW_HAS_LR : 0u;
    int list[NCS_GPR + 1];
    int ncs = cs_gpr_list(s, list);
    for (int i = 0; i < ncs; i++)
      if (list[i] != RZR)
        u.saved |= 1u << (UW_GPR_SHIFT + list[i] - R19);
    int fl[8];
    int nfl = cs_fpr_list(s, fl);
    for (int i = 0; i < nfl; i++) u.saved |= 1u << (UW_FPR_SHIFT + fl[i] - 8);
    /* Only a cold call is ever below a frame of this function on the
       stack, so the entry describes the frame that call is made in, a
       record and the outgoing area, with no register saved in it. */
    if (s->cold_frame) {
      u.frame = s->cold_out;
      u.saved_bytes = 16;
      u.exn_slot = u.fpsave_off = 0;
      u.saved = UW_HAS_LR;
    }
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
  xfree(s->fa); s->fa = NULL;
  xfree(s->imm_fold); xfree(s->fused); xfree(s->addr_fuse); xfree(s->ptr_fuse);
  xfree(s->zx_fuse); xfree(s->no_remat); xfree(s->paired); xfree(s->nl); xfree(s->pool);
  xfree(s->folded); xfree(s->uses); xfree(s->bfx); xfree(s->pinned); xfree(s->csinc);
  xfree(s->bfi);
  xfree(s->vrot); xfree(s->vadal); xfree(s->vmla); xfree(s->vbyel);
  xfree(s->extr);
  xfree(s->pc); xfree(s->pc_bfirst); xfree(s->pc_bn); xfree(s->pc_head);
  xfree(s->recomp);
  xfree(s->pc_next);
  xfree(s->lowonly); xfree(s->walias);
  xfree(s->sites); xfree(s->clauses);
  xfree(s->gcsites); xfree(s->gcroots);
  xfree(s->sp_first); xfree(s->sp_n); xfree(s->sp_vals);
  xfree(s->csa_slot); xfree(s->csa_on);
  xfree(s->csa_first); xfree(s->csa_n); xfree(s->csa_vals);
  xfree(s->lv_in); xfree(s->lv_out); xfree(s->lv_in_off); xfree(s->lv_out_off);
  if (split_ret) return split_ret;
  return s->far_br_needed ? 0 : 1;
}

/* ---- the a64 target descriptor -------------------------------------- */

static bool imm_ok_a64(IrFunc *f, Ref user, uint32_t idx, uint64_t cval, ValType ct) {
  Inst *in = &f->insts[user];
  int sf = vt_is_64(ct) ? SF64 : SF32;
  uint64_t masked = sf ? cval : (cval & 0xffffffffu);
  switch (in->op) {
    case I_ADD: case I_SUB:
      if (idx != 1) return false;
      if (masked <= 0xfff) return true;

      return (0 - (uint64_t)(sf ? (int64_t)masked : (int64_t)(int32_t)masked)) <= 0xfff;
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU:
      if (idx != 1) return false;
      if (masked <= 0xfff) return true;
      return (0 - (uint64_t)(sf ? (int64_t)masked : (int64_t)(int32_t)masked)) <= 0xfff;
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTR: case I_ROTL:
      return idx == 1;
    /* A constant lane shift is an immediate of ushr/shl/sshr; the register
       a constant count would hold is never read. */
    case I_VSHLV: case I_VSHRUV: case I_VSHRSV:
      return idx == 1;
    case I_AND: case I_OR: case I_XOR: {
      if (idx != 1) return false;

      if (in->op == I_XOR && masked == (sf ? ~0ull : 0xffffffffu)) return true;
      uint32_t enc;
      return a64_logical_imm(masked, sf, &enc);
    }
    case I_SELECT: {

      if (idx > 1 || vt_is_float(ct)) return false;
      if (masked == 0) return true;
      if (masked != 1 && masked != (sf ? ~0ull : 0xffffffffu)) return false;
      /* 1 or -1 against a register is csinc or csinv of the zero
         register; against 0 it is cset or csetm */
      Ref other = ir_args(f, user)[idx ^ 1u];
      if (f->insts[other].op != I_CONST) return !vt_is_float((ValType)f->insts[other].type) &&
                                               f->insts[other].type != VT_V128;
      uint64_t om = sf ? f->insts[other].imm : (f->insts[other].imm & 0xffffffffu);
      return om == 0;
    }
    case I_LOAD: case I_STORE: {

      if (idx != 0) return false;
      uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
      if (cval & ((1u << size) - 1)) return false;
      return (cval >> size) <= 0xfff;
    }
    /* the memory base plus a constant below 4 KiB, or plus a page below
       16 MiB, by one add (mem_ptr_consts' pointers) */
    case I_MEM_PTR:
      return idx == 0 && (masked <= 0xfff || ((masked & 0xfff) == 0 && masked < (1u << 24)));
    default:
      return false;
  }
}

static int const_cost_a64(uint64_t bits, bool wide) {
  int nwords = wide ? 4 : 2;
  if (!wide) bits &= 0xffffffffu;
  int zeros = 0, ones = 0;
  for (int i = 0; i < nwords; i++) {
    uint16_t w = (uint16_t)(bits >> (16 * i));
    if (w != 0) zeros++;
    if (w != 0xffff) ones++;
  }
  int n = zeros < ones ? zeros : ones;
  return n < 1 ? 1 : n;
}

static int vconst_cost_a64(uint64_t lo, uint64_t hi) {
  if (lo == hi) {
    if (lo == (lo & 0xffu) * 0x0101010101010101ull) return 1;
    return const_cost_a64(lo, true) + 1;
  }
  return const_cost_a64(lo, true) + const_cost_a64(hi, true) + 2;
}

/* A displacement that is not a multiple of the access size still fits one
   instruction, the unscaled ldur/stur, below 256: TinyGo zeroes its shadow
   frame's slot pairs with an i64.store at offset 20, and one such store
   kept pass_mem_ptr from giving the whole frame a native pointer. */
static bool mem_disp_ok_a64(uint64_t off, uint32_t size_log2) {
  if (off & ((1u << size_log2) - 1)) return off < 256;
  return (off >> size_log2) <= 0xfff;
}

static void pad_nop_a64(Buf *b) { a64_nop(b); }

static void emit_dead_call_a64(ObjBuilder *obj, uint32_t sym) {
  obj_add_reloc(obj, REL_CALL26, 0, obj->text.len, sym, 0);
  a64_b(&obj->text, 0);
}

/* Entered by blr from the call site, so the branch leaves the caller's
   return address in lr, as a trap at the site would have. */
static void emit_trap_stub_a64(ObjBuilder *obj, uint32_t sym, int code) {
  a64_load_imm(&obj->text, SF32, R0, (uint64_t)code);
  obj_add_reloc(obj, REL_CALL26, 0, obj->text.len, sym, 0);
  a64_b(&obj->text, 0);
}

const Target warmc_target_a64 = {
  .name = "a64",
  .is_x64 = false,
  .wrap_alias = true,
  .pin_regs = true,
  .imm_ok = imm_ok_a64,
  .const_cost = const_cost_a64,
  .vconst_cost = vconst_cost_a64,
  .mem_disp_ok = mem_disp_ok_a64,
  .produce_body = produce_body,
  .pad_nop = pad_nop_a64,
  .emit_dead_call = emit_dead_call_a64,
  .emit_trap_stub = emit_trap_stub_a64,
  .narg_int = 8,
  .narg_fp = 8,
  .arg_slot = ARG_STACK_SLOT,
  .max_frame = 32000,
  .vec_copies = 4,
  .vec_mul_lanes = 0x7,    /* mul of 8, 16 and 32-bit lanes */
};
