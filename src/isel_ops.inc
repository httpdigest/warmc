static uint64_t f32bits_(float x) { uint32_t b; memcpy(&b, &x, 4); return b; }
static uint64_t f64bits_(double x) { uint64_t b; memcpy(&b, &x, 8); return b; }

static void load_fconst(Isel *s, int ft, int fr, uint64_t bits) {
  uint32_t imm8;
  if (a64_fmov_imm(bits, ft, &imm8)) { a64_fmov_imm_emit(B, ft, fr, imm8); return; }

  if (s->pool_ok) { pool_load(s, ft, fr, bits); return; }
  a64_load_imm(B, ft == FT_D ? SF64 : SF32, TMP0, bits);
  a64_fmov_from_gpr(B, ft == FT_D ? SF64 : SF32, ft, fr, TMP0);
}

static void load_vconst(Isel *s, int fr, uint64_t lo, uint64_t hi) {
  if (lo == hi) {
    uint64_t b0 = lo & 0xffu;
    uint64_t splat = b0 * 0x0101010101010101ull;
    if (lo == splat) { a64_vmovi_b(B, fr, (uint32_t)b0); return; }
    a64_load_imm(B, SF64, TMP0, lo);
    a64_vdup_gpr(B, 3, fr, TMP0);
    return;
  }
  a64_load_imm(B, SF64, TMP0, lo);
  a64_fmov_from_gpr(B, SF64, FT_D, fr, TMP0);
  a64_load_imm(B, SF64, TMP0, hi);
  a64_ins_d1_gpr(B, fr, TMP0);
}

static int cc_for(uint16_t op) {
  switch (op) {
    case I_EQ: return CC_EQ;   case I_NE: return CC_NE;
    case I_LTS: return CC_LT;  case I_LTU: return CC_LO;
    case I_GTS: return CC_GT;  case I_GTU: return CC_HI;
    case I_LES: return CC_LE;  case I_LEU: return CC_LS;
    case I_GES: return CC_GE;  case I_GEU: return CC_HS;
    case I_FEQ: return CC_EQ;  case I_FNE: return CC_NE;
    case I_FLT: return CC_MI;  case I_FGT: return CC_GT;
    case I_FLE: return CC_LS;  case I_FGE: return CC_GE;
  }
  return CC_AL;
}

static int vec_base(Isel *s, int p, uint64_t off, uint32_t *disp) {
  if ((off & 15u) == 0 && (off >> 4) <= 0xfffu) { *disp = (uint32_t)off; return p; }
  if (off <= 0xfffu) a64_add_imm(B, SF64, TMP0, p, (uint32_t)off, 0);
  else { a64_load_imm(B, SF64, TMP0, off); a64_add_reg(B, SF64, TMP0, p, TMP0); }
  *disp = 0;
  return TMP0;
}

/* ld2 to ld4 take a bare register for the address */
static int ldn_base(Isel *s, int p, uint64_t off) {
  if (!off) return p;
  if (off <= 0xfffu) a64_add_imm(B, SF64, TMP0, p, (uint32_t)off, 0);
  else { a64_load_imm(B, SF64, TMP0, off); a64_add_reg(B, SF64, TMP0, p, TMP0); }
  return TMP0;
}

static uint32_t global_off(uint32_t i) {
  return VMCTX_GLOBALS + GLOBAL_SLOT_SIZE * i;
}

static bool is_int_cmp(uint16_t op) { return op >= I_EQ && op <= I_GEU; }
static bool is_flt_cmp(uint16_t op) { return op >= I_FEQ && op <= I_FGE; }

static bool cmp_operand_stable(Isel *s, Ref r) {
  int l = s->loc[r];
  return l != LOC_NONE && !IS_SLOT(l);
}

static bool describe_compare(Isel *s, Ref v, int *kind, Ref *ra, Ref *rb,
                             uint64_t *imm, int *sf) {
  IrFunc *f = s->f;
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  *rb = REF_NONE;
  *imm = 0;

  if (s->folded && s->folded[a[0]] && f->insts[a[0]].op == I_AND) {
    Ref *ca = ir_args(f, a[0]);
    *kind = 1; *ra = ca[0]; *rb = ca[1];
    *sf = is64(f, a[0]) ? SF64 : SF32;
    return cmp_operand_stable(s, ca[0]) &&
           (f->insts[ca[1]].op == I_CONST || cmp_operand_stable(s, ca[1]));
  }

  if (in->op == I_EQZ) {
    *kind = 2; *ra = a[0]; *imm = 0;
    *sf = is64(f, a[0]) ? SF64 : SF32;
    return cmp_operand_stable(s, a[0]);
  }
  if (is_flt_cmp(in->op)) {
    *kind = 3; *ra = a[0]; *rb = a[1];
    *sf = is64(f, a[0]) ? FT_D : FT_S;
    return cmp_operand_stable(s, a[0]) && cmp_operand_stable(s, a[1]);
  }
  *sf = is64(f, a[0]) ? SF64 : SF32;
  if (is_imm_at(s, v, 1, a[1])) {
    uint64_t c = imm_of(s, a[1]);
    uint64_t m = *sf ? c : (c & 0xffffffffu);
    uint64_t neg = (uint64_t)(-(int64_t)(*sf ? (int64_t)m : (int64_t)(int32_t)m));

    if (m > 0xfff && neg > 0xfff) return false;
    *kind = 2; *ra = a[0]; *imm = m;
    return cmp_operand_stable(s, a[0]);
  }
  *kind = 5; *ra = a[0]; *rb = a[1];
  return cmp_operand_stable(s, a[0]) && cmp_operand_stable(s, a[1]);
}

static void emit_compare_raw(Isel *s, Ref v);

/* Whether a compare's 0/1 is never read: every use of it is a select on it,
   and those selects are the instructions right after it.  Each then reads the
   flags the compare set -- a select and the operand reads before it emit
   loads, moves and csel-family instructions, none of which write the flags --
   so the cset would be dead, and it is what argmin's loop would spend an
   issue slot on per element. */
static bool flags_only(Isel *s, Ref v) {
  IrFunc *f = s->f;
  Block *b = &f->blocks[s->cur_block];
  uint32_t n = s->uses[v], at = s->cur_pos;
  if (n == 0 || at + n >= b->ninsts || b->insts[at] != v) return false;
  for (uint32_t k = 1; k <= n; k++) {
    Ref u = b->insts[at + k];
    Ref *a = ir_args(f, u);
    if (f->insts[u].op != I_SELECT || a[2] != v || a[0] == v || a[1] == v ||
        s->paired[u])
      return false;
  }
  return true;
}

static void emit_compare(Isel *s, Ref v) {
  int kind, sf;
  Ref ra, rb;
  uint64_t imm;
  bool known = describe_compare(s, v, &kind, &ra, &rb, &imm, &sf);
  if (known && s->fl_kind && s->fl_epoch == B->flags_epoch &&
      kind == s->fl_kind && ra == s->fl_a && rb == s->fl_b &&
      imm == s->fl_imm && sf == s->fl_sf) {
    g_stats.cmp_reused++;
    return;
  }
  emit_compare_raw(s, v);
  if (known) {
    s->fl_kind = kind; s->fl_a = ra; s->fl_b = rb;
    s->fl_imm = imm; s->fl_sf = sf; s->fl_epoch = B->flags_epoch;
  } else {
    s->fl_kind = 0;
  }
}

static void emit_compare_raw(Isel *s, Ref v) {
  IrFunc *f = s->f;
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);

  if (s->folded && s->folded[a[0]] && f->insts[a[0]].op == I_AND) {
    Ref *ca = ir_args(f, a[0]);
    int csf = is64(f, a[0]) ? SF64 : SF32;
    int x = rd(s, ca[0], S0);
    uint32_t enc;
    if (f->insts[ca[1]].op == I_CONST &&
        a64_logical_imm(csf ? f->insts[ca[1]].imm
                            : (f->insts[ca[1]].imm & 0xffffffffu), csf, &enc))
      a64_tst_imm(B, csf, x, enc);
    else a64_tst_reg(B, csf, x, rd(s, ca[1], S1));
    return;
  }
  if (in->op == I_EQZ) {
    int x = rd(s, a[0], S0);
    a64_cmp_imm(B, is64(f, a[0]) ? SF64 : SF32, x, 0);
    return;
  }
  if (is_flt_cmp(in->op)) {
    int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
    a64_fcmp(B, is64(f, a[0]) ? FT_D : FT_S, x, y);
    return;
  }
  int sf = is64(f, a[0]) ? SF64 : SF32;
  if (is_imm_at(s, v, 1, a[1])) {
    /* an immediate compare's register 31 is sp: a zero is made */
    int x = rd(s, a[0], S0);
    uint64_t c = imm_of(s, a[1]);
    uint64_t m = sf ? c : (c & 0xffffffffu);
    if (m <= 0xfff) { a64_cmp_imm(B, sf, x, (uint32_t)m); return; }
    uint64_t neg = (uint64_t)(-(int64_t)(sf ? (int64_t)m : (int64_t)(int32_t)m));
    if (neg <= 0xfff) { a64_adds_imm(B, sf, RZR, x, (uint32_t)neg, 0); return; }
    a64_cmp_reg(B, sf, x, rd_zr(s, a[1], S1));
    return;
  }
  int x = rd_zr(s, a[0], S0);
  int y = rd_zr(s, a[1], S1);
  a64_cmp_reg(B, sf, x, y);
}

/* Apple's M4 issues a 32-bit cmp or cmn and the csel right after it as one
   operation when the two read at most two registers between them, the
   minimum and the clamp (cmp a, b; csel d, a, b, cc, or cmp a, #k with any
   second register): the select is a cycle sooner on a chain through it.
   Measured, any condition does; csinc, csinv, csneg, cset, a 64-bit pair, a
   flag setter other than cmp and cmn, and a pair with a third register --
   wzr counts -- do not.  cbench/vla's edit-distance cell, min(cur + 1, t),
   costs native code three cycles (add, cmp, csinc) and this two.  Whether
   select v is such a pair, with its compare: a plain csel of two i32 values
   on an i32 compare, not one of the forms below that become cset or csinc,
   whose compare and operands come to two registers at most.  *imm is the
   compare's second operand as an immediate, or UINT64_MAX for a register.
   A condition that is no compare is the compare of itself with 0. */
static bool sel_fuses(Isel *s, Ref v, uint64_t *imm) {
  IrFunc *f = s->f;
  Ref *a = ir_args(f, v);
  if (is64(f, v) || is_fp_value(f, v) || is_vec_value(f, v)) return false;
  Ref c = a[2];
  Inst *ci = &f->insts[c];
  bool icmp = (ci->op >= I_EQ && ci->op <= I_GEU) || ci->op == I_EQZ;
  bool value = !icmp && !is_flt_cmp(ci->op);
  if (!icmp && !value) return false;
  if (value && s->folded && s->folded[c]) return false;
  Ref *ca = value ? &a[2] : ir_args(f, c);
  if (is64(f, ca[0]) || is_fp_value(f, ca[0])) return false;
  if (!value && s->folded && s->folded[ca[0]] && f->insts[ca[0]].op == I_AND) return false;
  uint64_t m = 0xffffffffu;
  bool xc = f->insts[a[0]].op == I_CONST, yc = f->insts[a[1]].op == I_CONST;
  uint64_t xv = f->insts[a[0]].imm & m, yv = f->insts[a[1]].imm & m;
  if ((xc && yc && (yv == 0 || xv == 0)) || (s->csinc && s->csinc[v]) ||
      (xc && !yc && (xv == 1 || xv == m)) || (yc && !xc && (yv == 1 || yv == m)) ||
      (xc && yc && (xv == ((yv + 1) & m) || yv == ((xv + 1) & m))))
    return false;
  Ref regs[4];
  uint32_t n = 0;
#define SF_ADD(r) do { Ref r_ = (r); bool seen_ = false; \
    for (uint32_t k_ = 0; k_ < n; k_++) seen_ |= regs[k_] == r_; \
    if (!seen_) regs[n++] = r_; } while (0)
  SF_ADD(ca[0]);
  *imm = UINT64_MAX;
  if (value || ci->op == I_EQZ) *imm = 0;
  else if (is_imm_at(s, c, 1, ca[1])) {
    uint64_t k = imm_of(s, ca[1]) & m;
    uint64_t neg = (uint64_t)(-(int64_t)(int32_t)k) & m;
    if (k <= 0xfff || neg <= 0xfff) *imm = k;
  }
  if (*imm == UINT64_MAX) SF_ADD(ca[1]);
  /* a zero operand is wzr, a register of its own */
  SF_ADD(xc && xv == 0 ? REF_NONE : a[0]);
  SF_ADD(yc && yv == 0 ? REF_NONE : a[1]);
#undef SF_ADD
  if (n > 2) return false;
  /* The compare is made again at the select, so what it reads must be live
     there: folded into the select, or read by the select too. */
  if (!value && !(s->folded && s->folded[c]))
    for (uint32_t k = 0; k < (*imm == UINT64_MAX ? 2u : 1u); k++)
      if (ca[k] != a[0] && ca[k] != a[1]) return false;
  return true;
}

static int cond_of(Isel *s, Ref v) {
  Inst *in = &s->f->insts[v];
  if (in->op == I_EQZ) return CC_EQ;
  return cc_for(in->op);
}

static bool is_condition(Isel *s, Ref v) {
  Inst *in = &s->f->insts[v];
  return in->op == I_EQZ || is_int_cmp(in->op) || is_flt_cmp(in->op);
}

static bool divisor_nonzero(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  int width = vt_is_64((ValType)in->type) ? 64 : 32;
  uint64_t all = (width == 64) ? ~0ull : 0xffffffffu;
  Ref *a = ir_args(f, v);
  switch (in->op) {
    case I_CONST: return (in->imm & all) != 0;
    case I_OR:
      return (f->insts[a[1]].op == I_CONST && (f->insts[a[1]].imm & all) != 0) ||
             (f->insts[a[0]].op == I_CONST && (f->insts[a[0]].imm & all) != 0);
    case I_ADD: {
      if (f->insts[a[1]].op != I_CONST) return false;
      uint64_t c = f->insts[a[1]].imm & all;
      if (c == 0) return false;

      return ir_value_max(f, a[0], 4) <= all - c;
    }
    default: return false;
  }
}

static bool is_const_zero(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  if (in->op != I_CONST) return false;
  uint64_t all = vt_is_64((ValType)in->type) ? ~0ull : 0xffffffffu;
  return (in->imm & all) == 0;
}

static bool nonzero_by_dominating_branch(IrFunc *f, uint32_t blk, Ref d) {
  uint32_t b = blk;
  for (int steps = 0; steps < 32; steps++) {
    if (b == REF_NONE) return false;
    uint32_t p = f->blocks[b].idom;

    if (p == REF_NONE || p == b) return false;
    Block *pb = &f->blocks[p];
    if (f->blocks[b].npreds == 1 && pb->term == T_BRIF && pb->nsuccs == 2) {
      bool taken = pb->succs[0].target == b;
      bool fallthru = pb->succs[1].target == b;

      if (taken != fallthru) {
        Inst *c = &f->insts[pb->term_arg];
        Ref *ca = ir_args(f, pb->term_arg);
        switch (c->op) {

          case I_EQZ:
            if (ir_nonzero_with(f, ca[0], d) && fallthru) return true;
            break;
          case I_EQ:
            if (fallthru && ((ir_nonzero_with(f, ca[0], d) && is_const_zero(f, ca[1])) ||
                             (ir_nonzero_with(f, ca[1], d) && is_const_zero(f, ca[0]))))
              return true;
            break;
          case I_NE:
            if (taken && ((ir_nonzero_with(f, ca[0], d) && is_const_zero(f, ca[1])) ||
                          (ir_nonzero_with(f, ca[1], d) && is_const_zero(f, ca[0]))))
              return true;
            break;
          default: break;
        }
      }
    }
    b = p;
  }
  return false;
}

static bool div_guarded_earlier(Isel *s, Ref v, bool both) {
  IrFunc *f = s->f;
  Block *b = &f->blocks[s->cur_block];
  Ref *a = ir_args(f, v);
  for (uint32_t i = 0; i < b->ninsts; i++) {
    Ref u = b->insts[i];
    if (u == v) return false;
    uint16_t op = f->insts[u].op;
    if (op != I_DIVS && op != I_DIVU && op != I_REMS && op != I_REMU) continue;

    if (both && op != I_DIVS) continue;
    if (is64(f, u) != is64(f, v)) continue;
    Ref *ua = ir_args(f, u);
    if (ua[1] != a[1]) continue;
    if (both && ua[0] != a[0]) continue;
    return true;
  }
  return false;
}

static void emit_div(Isel *s, Ref v, bool sgn, bool rem) {
  IrFunc *f = s->f;
  Ref *a = ir_args(f, v);
  int sf = is64(f, v) ? SF64 : SF32;
  int x = rd(s, a[0], S0), y = rd(s, a[1], S1);
  if (x == S1) { a64_mov_reg(B, SF64, S2, x); x = S2; }

  Inst *dv = &f->insts[a[1]];
  uint64_t dc = sf ? dv->imm : (dv->imm & 0xffffffffu);
  bool known = dv->op == I_CONST;
  if (!(known ? dc != 0 : divisor_nonzero(f, a[1])) &&
      !div_guarded_earlier(s, v, false) &&
      !nonzero_by_dominating_branch(f, s->cur_block, a[1]))
    trap_if_zero(s, sf, y, TRAP_DIV_ZERO);
  if (sgn && !rem && !(known && dc != (sf ? ~0ull : 0xffffffffu)) &&
      !ir_not_int_min(f, a[0]) && !div_guarded_earlier(s, v, true)) {

    a64_adds_imm(B, sf, RZR, y, 1, 0);
    a64_ccmp_imm(B, sf, x, 1, 0, CC_EQ);
    trap_if_cc(s, CC_VS, TRAP_DIV_OVERFLOW);
  }
  int d = wr(s, v, S3);
  if (rem) {
    if (sgn) a64_sdiv(B, sf, TMP0, x, y); else a64_udiv(B, sf, TMP0, x, y);
    a64_msub(B, sf, d, TMP0, y, x);
  } else {
    if (sgn) a64_sdiv(B, sf, d, x, y); else a64_udiv(B, sf, d, x, y);
  }
  commit(s, v, d);
}

static void emit_trunc_checked(Isel *s, Ref v, bool sgn) {
  IrFunc *f = s->f;
  Ref *a = ir_args(f, v);
  ValType from = (ValType)f->insts[v].imm;
  int ft = from == VT_F32 ? FT_S : FT_D;
  int sf = is64(f, v) ? SF64 : SF32;
  int x = rd(s, a[0], F0);

  uint64_t lo, hi;
  int lo_cond;
  if (!sf && sgn) {
    if (from == VT_F32) { lo = f32bits_(-2147483648.0f); lo_cond = CC_LT; hi = f32bits_(2147483648.0f); }
    else { lo = f64bits_(-2147483649.0); lo_cond = CC_LE; hi = f64bits_(2147483648.0); }
  } else if (!sf) {
    if (from == VT_F32) { lo = f32bits_(-1.0f); lo_cond = CC_LE; hi = f32bits_(4294967296.0f); }
    else { lo = f64bits_(-1.0); lo_cond = CC_LE; hi = f64bits_(4294967296.0); }
  } else if (sgn) {
    if (from == VT_F32) { lo = f32bits_(-9223372036854775808.0f); lo_cond = CC_LT; hi = f32bits_(9223372036854775808.0f); }
    else { lo = f64bits_(-9223372036854775808.0); lo_cond = CC_LT; hi = f64bits_(9223372036854775808.0); }
  } else {
    if (from == VT_F32) { lo = f32bits_(-1.0f); lo_cond = CC_LE; hi = f32bits_(18446744073709551616.0f); }
    else { lo = f64bits_(-1.0); lo_cond = CC_LE; hi = f64bits_(18446744073709551616.0); }
  }
  load_fconst(s, ft, FTMP, lo);
  a64_fcmp(B, ft, x, FTMP);
  trap_if_cc(s, lo_cond, TRAP_BAD_CONVERSION);
  load_fconst(s, ft, FTMP, hi);
  a64_fcmp(B, ft, x, FTMP);
  trap_if_cc(s, CC_GE, TRAP_BAD_CONVERSION);

  int d = wr(s, v, S0);
  if (sgn) a64_fcvtzs(B, sf, ft, d, x); else a64_fcvtzu(B, sf, ft, d, x);
  commit(s, v, d);
}

static void emit_copysign(Isel *s, Ref v) {
  IrFunc *f = s->f;
  Ref *a = ir_args(f, v);
  int wide = is64(f, v);
  int ft = wide ? FT_D : FT_S;
  int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
  a64_fmov_to_gpr(B, wide, ft, S0, x);
  a64_fmov_to_gpr(B, wide, ft, S1, y);
  a64_load_imm(B, wide, S2, wide ? 0x7fffffffffffffffull : 0x7fffffffull);
  a64_and_reg(B, wide, S0, S0, S2);
  a64_mvn(B, wide, S2, S2);
  a64_and_reg(B, wide, S1, S1, S2);
  a64_orr_reg(B, wide, S0, S0, S1);
  int d = wr(s, v, F2);
  a64_fmov_from_gpr(B, wide, ft, d, S0);
  commit(s, v, d);
}

static bool emit_bfx(Isel *s, Ref v) {
  if (!s->bfx || !s->bfx[v].ok) return false;
  struct Bfx *bf = &s->bfx[v];
  int sf = is64(s->f, v) ? SF64 : SF32;
  int x = rd(s, bf->src, S0);
  int d = wr(s, v, S2);
  if (bf->sign) a64_sbfm(B, sf, d, x, bf->immr, bf->imms);
  else a64_ubfm(B, sf, d, x, bf->immr, bf->imms);
  commit(s, v, d);
  return true;
}

/* After a cold call (emit_cold_frame) the function's own frame is gone:
   the trap that follows reads nothing from it, and nothing may have been
   recorded there for the collector -- the analysis left no value live
   across the call.  The result, which nothing reads, is not committed:
   its location could be a slot of the frame torn down. */
static void cold_call_done(Isel *s, Ref v) {
  if (s->ngcsites && s->gcsites[s->ngcsites - 1].pc == (uint32_t)s->body.len)
    warmc_fatal("internal: a root recorded at a cold call in %s",
                s->f->m->funcs[s->f->func_index].sym);
  if (s->csa_n && s->csa_n[v])
    warmc_fatal("internal: a value saved around a cold call in %s",
                s->f->m->funcs[s->f->func_index].sym);
  s->mem_valid = false;
}

static CompType *call_sig(Isel *s, Inst *in) {
  Module *m = s->f->m;
  if (in->op == I_CALL) return &m->types[m->funcs[in->imm].type_index];
  return &m->types[in->imm];
}

static void emit_inst(Isel *s, Ref v) {
  IrFunc *f = s->f;
  Inst *in = &f->insts[v];
  s->cur_inst = v;
  Ref *a = ir_args(f, v);
  int wide = is64(f, v);
  int sf = wide ? SF64 : SF32;
  int ft = wide ? FT_D : FT_S;

  s->rc_used[0] = s->rc_used[1] = 0;
  s->frc_used[0] = s->frc_used[1] = 0;
  if (inst_clobbers_caller_saved(f, v)) rc_clear(s);
  if (s->frc_reg[0]) {
    bool vec = in->type == VT_V128;
    for (uint32_t j = 0; j < in->nargs && !vec; j++) vec = f->insts[a[j]].type == VT_V128;
    if (vec) s->frc_slot[0] = s->frc_slot[1] = 0;
  }

  if (s->loc[v] == LOC_NONE &&
      (is_imm(s, v) || in->op == I_CONST || in->op == I_MEM_BASE ||
       in->op == I_GLOBAL_GET ||
       (s->fused && s->fused[v]) || (s->folded && s->folded[v]) ||
       (s->walias && s->walias[v]))) return;

  s->t1_was = s->t1_ok && !s->no_ldpair;
  s->t1_ok = false;

  switch (in->op) {
    case I_NOP: case I_PARAM: break;

    case I_CONST: {
      if (s->loc[v] == LOC_NONE) break;
      if ((ValType)in->type == VT_V128) {
        int d = wr(s, v, F0);
        load_vconst(s, d, in->imm, in->imm2);
        commit(s, v, d);
      } else if (vt_is_float((ValType)in->type)) {
        int d = wr(s, v, F0);

        if (in->imm == 0)
          a64_fmov_from_gpr(B, wide ? SF64 : SF32, ft, d, RZR);
        else
          load_fconst(s, ft, d, in->imm);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        a64_load_imm(B, sf, d, in->imm);
        commit(s, v, d);
      }
      break;
    }

    case I_ADD: case I_SUB: {

      if (s->folded && s->folded[a[1]]) {
        Inst *ci = &f->insts[a[1]];
        Ref *ca = ir_args(f, a[1]);
        /* the extended-register form's first register 31 is sp; the
           shifted-register form's and madd's the zero register */
        bool ext = ci->op == I_EXTENDU || ci->op == I_EXTENDS || ci->op == I_EXT32S;
        int x = ext ? rd(s, a[0], S0) : rd_zr(s, a[0], S0);
        int d = wr(s, v, S2);
        if (ci->op == I_SHL || ci->op == I_SHRU || ci->op == I_SHRS) {
          int y = rd(s, ca[0], S1);
          int sh = (int)(f->insts[ca[1]].imm % (uint64_t)(sf ? 64 : 32));
          int kind = ci->op == I_SHL ? SH_LSL : ci->op == I_SHRU ? SH_LSR : SH_ASR;
          if (in->op == I_ADD) a64_add_reg_sh(B, sf, d, x, y, kind, sh);
          else a64_sub_reg_sh(B, sf, d, x, y, kind, sh);
        } else if (ci->op == I_EXTENDU || ci->op == I_EXTENDS || ci->op == I_EXT32S) {
          int y = rd(s, ca[0], S1);
          a64_addsub_extw(B, in->op == I_SUB, d, x, y, ci->op != I_EXTENDU, 0);
        } else {
          int y = rd(s, ca[0], S1), z = rd(s, ca[1], S3);
          if (in->op == I_ADD) a64_madd(B, sf, d, y, z, x);
          else a64_msub(B, sf, d, y, z, x);
        }
        commit(s, v, d);
        break;
      }
      /* an immediate's register 31 is sp, the registers' form's zero */
      bool imm = is_imm_at(s, v, 1, a[1]);
      int x = imm ? rd(s, a[0], S0) : rd_zr(s, a[0], S0);
      int d = wr(s, v, S2);
      if (imm) {
        uint64_t c = imm_of(s, a[1]);
        uint64_t m = sf ? c : (c & 0xffffffffu);
        uint64_t neg = (uint64_t)(-(int64_t)(sf ? (int64_t)m : (int64_t)(int32_t)m));
        bool add = (in->op == I_ADD);
        if (m <= 0xfff) {
          if (add) a64_add_imm(B, sf, d, x, (uint32_t)m, 0);
          else a64_sub_imm(B, sf, d, x, (uint32_t)m, 0);
        } else {
          if (add) a64_sub_imm(B, sf, d, x, (uint32_t)neg, 0);
          else a64_add_imm(B, sf, d, x, (uint32_t)neg, 0);
        }
        commit(s, v, d);
        break;
      }
      int y = rd_zr(s, a[1], S1);
      if (in->op == I_ADD) a64_add_reg(B, sf, d, x, y);
      else a64_sub_reg(B, sf, d, x, y);
      commit(s, v, d);
      break;
    }
    case I_AND: case I_OR: case I_XOR: {

      if (emit_bfx(s, v)) break;

      if (s->bfi && s->bfi[v]) {
        int slot = s->bfi[v] - 1;
        struct Bfx *bf = &s->bfx[a[slot]];
        int x = rd(s, bf->src, S0);
        int o = rd(s, a[slot ^ 1], S1);
        int d = wr(s, v, S2);
        /* d starts as the value the field goes into; x is read after, so
           it must not be d unless the value is there already */
        int t = (d == x && d != o) ? S2 : d;
        if (t != o) a64_mov_reg(B, sf, t, o);
        a64_bfm(B, sf, t, x, bf->immr, bf->imms);
        if (t != d) a64_mov_reg(B, sf, d, t);
        commit(s, v, d);
        break;
      }

      if (s->extr && s->extr[v]) {
        Ref *la = ir_args(f, a[0]), *ra = ir_args(f, a[1]);
        int hi = rd(s, la[0], S0);
        int lo = rd(s, ra[0], S1);
        int d = wr(s, v, S2);
        a64_extr(B, sf, d, hi, lo,
                 (int)(f->insts[ra[1]].imm % (uint64_t)(sf ? 64 : 32)));
        commit(s, v, d);
        break;
      }

      if (s->folded && s->folded[a[1]] && f->insts[a[1]].op == I_SELECT) {
        Ref *sa = ir_args(f, a[1]);
        uint64_t m = sf ? ~0ull : 0xffffffffu;
        emit_compare(s, sa[2]);

        int ccm = cond_of(s, sa[2]);
        if ((f->insts[sa[0]].imm & m) != m) ccm = cc_invert(ccm);
        int x = rd(s, a[0], S0);
        int d = wr(s, v, S2);

        if (in->op == I_XOR) a64_csinv(B, sf, d, x, x, cc_invert(ccm));
        else if (in->op == I_AND) a64_csel(B, sf, d, x, RZR, ccm);
        else a64_csinv(B, sf, d, x, RZR, cc_invert(ccm));
        commit(s, v, d);
        break;
      }
      if (s->folded && s->folded[a[1]]) {
        int kind = shift_fuse_kind(f, a[1], (ValType)in->type, s->no_imm);
        int x = rd(s, a[0], S0);

        Ref *fa = ir_args(f, a[1]);
        Ref src = fa[0];
        int y = rd(s, src, S1);
        int d = wr(s, v, S2);
        if (kind >= 0) {
          int amt = shift_fuse_amount(f, a[1], (ValType)in->type);
          if (in->op == I_AND) a64_and_reg_sh(B, sf, d, x, y, kind, amt);
          else if (in->op == I_OR) a64_orr_reg_sh(B, sf, d, x, y, kind, amt);
          else a64_eor_reg_sh(B, sf, d, x, y, kind, amt);
        } else if (in->op == I_AND) a64_bic_reg(B, sf, d, x, y);
        else if (in->op == I_OR) a64_orn_reg(B, sf, d, x, y);
        else a64_eon_reg(B, sf, d, x, y);
        commit(s, v, d);
        break;
      }
      int x = rd(s, a[0], S0);
      int d = wr(s, v, S2);
      uint32_t enc;
      if (is_imm_at(s, v, 1, a[1]) &&
          (sf ? imm_of(s, a[1]) : (imm_of(s, a[1]) & 0xffffffffu)) ==
              (sf ? ~0ull : 0xffffffffu) && in->op == I_XOR) {
        a64_mvn(B, sf, d, x);
        commit(s, v, d);
        break;
      }
      if (is_imm_at(s, v, 1, a[1]) &&
          a64_logical_imm(sf ? imm_of(s, a[1]) : (imm_of(s, a[1]) & 0xffffffffu), sf, &enc)) {
        if (in->op == I_AND) a64_and_imm(B, sf, d, x, enc);
        else if (in->op == I_OR) a64_orr_imm(B, sf, d, x, enc);
        else a64_eor_imm(B, sf, d, x, enc);
        commit(s, v, d);
        break;
      }
      int y = rd(s, a[1], S1);
      if (in->op == I_AND) a64_and_reg(B, sf, d, x, y);
      else if (in->op == I_OR) a64_orr_reg(B, sf, d, x, y);
      else a64_eor_reg(B, sf, d, x, y);
      commit(s, v, d);
      break;
    }
    case I_MUL: {
      int x = rd(s, a[0], S0), y = rd(s, a[1], S1);
      int d = wr(s, v, S2);
      a64_mul(B, sf, d, x, y);
      commit(s, v, d);
      break;
    }
    case I_MULHU: case I_MULHS: {
      int x = rd(s, a[0], S0), y = rd(s, a[1], S1);
      int d = wr(s, v, S2);
      bool hs = in->op == I_MULHS;
      if (sf) { if (hs) a64_smulh(B, d, x, y); else a64_umulh(B, d, x, y); }
      else {

        if (hs) a64_smull(B, d, x, y); else a64_umull(B, d, x, y);
        a64_lsr_imm(B, SF64, d, d, 32);
      }
      commit(s, v, d);
      break;
    }
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTR: case I_ROTL: {
      int width = sf ? 64 : 32;
      if (emit_bfx(s, v)) break;
      if (in->op == I_SHRU && s->folded[a[0]] && f->insts[a[0]].op == I_MULHU) {
        Ref *ma = ir_args(f, a[0]);
        int x = rd(s, ma[0], S0), y = rd(s, ma[1], S1);
        int d = wr(s, v, S2);
        a64_umull(B, d, x, y);
        a64_lsr_imm(B, SF64, d, d, 32 + (int)(imm_of(s, a[1]) & 31));
        commit(s, v, d);
        break;
      }
      int x = rd(s, a[0], S0);
      int d = wr(s, v, S2);
      if (is_imm_at(s, v, 1, a[1])) {
        int sh = (int)(imm_of(s, a[1]) % (uint64_t)width);
        switch (in->op) {
          case I_SHL: a64_lsl_imm(B, sf, d, x, sh); break;
          case I_SHRS: a64_asr_imm(B, sf, d, x, sh); break;
          case I_SHRU: a64_lsr_imm(B, sf, d, x, sh); break;
          case I_ROTR: if (sh) a64_ror_imm(B, sf, d, x, sh); else a64_mov_reg(B, sf, d, x); break;
          default: { int r2 = (width - sh) % width;
                     if (r2) a64_ror_imm(B, sf, d, x, r2); else a64_mov_reg(B, sf, d, x); break; }
        }
        commit(s, v, d);
        break;
      }
      int y = rd(s, a[1], S1);
      switch (in->op) {
        case I_SHL: a64_lslv(B, sf, d, x, y); break;
        case I_SHRS: a64_asrv(B, sf, d, x, y); break;
        case I_SHRU: a64_lsrv(B, sf, d, x, y); break;
        case I_ROTR: a64_rorv(B, sf, d, x, y); break;
        default: a64_neg(B, sf, S3, y); a64_rorv(B, sf, d, x, S3); break;
      }
      commit(s, v, d);
      break;
    }
    case I_DIVS: emit_div(s, v, true, false); break;
    case I_DIVU: emit_div(s, v, false, false); break;
    case I_REMS: emit_div(s, v, true, true); break;
    case I_REMU: emit_div(s, v, false, true); break;

    case I_CLZ: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                  a64_clz(B, sf, d, x); commit(s, v, d); break; }
    case I_BSWAP: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                    a64_rev(B, sf, d, x); commit(s, v, d); break; }
    case I_BITREV: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                     a64_rbit(B, sf, d, x); commit(s, v, d); break; }
    case I_CTZ: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                  a64_rbit(B, sf, d, x); a64_clz(B, sf, d, d); commit(s, v, d); break; }
    case I_POPCNT: { int x = rd(s, a[0], S0);
                     a64_fmov_from_gpr(B, sf, sf ? FT_D : FT_S, FTMP, x);
                     a64_cnt_8b(B, FTMP, FTMP);
                     a64_addv_8b(B, FTMP, FTMP);
                     int d = wr(s, v, S0);
                     a64_fmov_to_gpr(B, SF32, FT_S, d, FTMP);
                     commit(s, v, d); break; }

    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE: {
      bool bare = flags_only(s, v);
      uint64_t fimm;
      /* Read only by selects, the first of which makes the compare itself,
         right before its csel: this one would be a second copy. */
      if (bare && !s->no_csel_fuse &&
          sel_fuses(s, f->blocks[s->cur_block].insts[s->cur_pos + 1], &fimm)) {
        commit(s, v, wr(s, v, S0));
        s->cmp_ref = REF_NONE;
        s->cmp_bare = v;
        break;
      }
      emit_compare(s, v);
      int d = wr(s, v, S0);
      int cc = cond_of(s, v);
      if (!bare) a64_cset(B, SF32, d, cc);
      commit(s, v, d);
      s->cmp_ref = v; s->cmp_cc = cc; s->cmp_epoch = B->flags_epoch;
      s->cmp_bare = bare ? v : REF_NONE;
      break;
    }

#define FBIN(op, fn)                                                           \
    case op: {                                                                 \
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);                            \
      int d = wr(s, v, F2);                                                    \
      fn(B, ft, d, x, y);                                                      \
      commit(s, v, d);                                                         \
      break;                                                                   \
    }
    FBIN(I_FADD, a64_fadd)
    FBIN(I_FSUB, a64_fsub)
    FBIN(I_FMUL, a64_fmul)
    FBIN(I_FDIV, a64_fdiv)
    FBIN(I_FMIN, a64_fmin)
    FBIN(I_FMAX, a64_fmax)
#undef FBIN
    case I_FMA: {
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1), c = rd(s, a[2], F2);
      int d = wr(s, v, F2);
      a64_fmadd(B, ft, (int)in->imm, d, x, y, c);
      commit(s, v, d);
      break;
    }
    case I_FCOPYSIGN: emit_copysign(s, v); break;

#define FUN(op, fn)                                                            \
    case op: {                                                                 \
      int x = rd(s, a[0], F0);                                                 \
      int d = wr(s, v, F1);                                                    \
      fn(B, ft, d, x);                                                         \
      commit(s, v, d);                                                         \
      break;                                                                   \
    }
    FUN(I_FABS, a64_fabs)
    FUN(I_FNEG, a64_fneg)
    FUN(I_FSQRT, a64_fsqrt)
#undef FUN
    case I_FCEIL: case I_FFLOOR: case I_FTRUNC: case I_FNEAREST: {
      static const int mode[] = { 1, 2, 3, 0 };
      int x = rd(s, a[0], F0);
      int d = wr(s, v, F1);
      a64_frint(B, ft, d, x, mode[in->op - I_FCEIL]);
      commit(s, v, d);
      break;
    }

    case I_WRAP: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                   if (d != x || ((ir_known_bits(f, a[0], 4) >> 32) != 0 &&
                                  !(s->lowonly && s->lowonly[v])))
                     a64_uxtw(B, d, x);
                   commit(s, v, d); break; }
    case I_EXTENDS: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                      a64_sxtw(B, d, x); commit(s, v, d); break; }
    case I_EXTENDU: {

      if (s->zx_fuse && s->zx_fuse[v]) break;
      int x = rd(s, a[0], S0); int d = wr(s, v, S0);
      if (d != x || !w_written(f, a[0])) a64_uxtw(B, d, x);
      commit(s, v, d); break; }
    case I_EXT8S: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                    a64_sxtb(B, sf, d, x); commit(s, v, d); break; }
    case I_EXT16S: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                     a64_sxth(B, sf, d, x); commit(s, v, d); break; }
    case I_EXT32S: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                     a64_sxtw(B, d, x); commit(s, v, d); break; }

    case I_TRUNCS: emit_trunc_checked(s, v, true); break;
    case I_TRUNCU: emit_trunc_checked(s, v, false); break;
    case I_TRUNC_SATS: case I_TRUNC_SATU: {
      ValType from = (ValType)in->imm;
      int sft = from == VT_F32 ? FT_S : FT_D;
      int x = rd(s, a[0], F0);
      int d = wr(s, v, S0);
      if (in->op == I_TRUNC_SATS) a64_fcvtzs(B, sf, sft, d, x);
      else a64_fcvtzu(B, sf, sft, d, x);
      commit(s, v, d);
      break;
    }
    case I_CONVERTS: case I_CONVERTU: {
      ValType from = (ValType)in->imm;
      int fsf = vt_is_64(from) ? SF64 : SF32;
      int x = rd(s, a[0], S0);
      int d = wr(s, v, F0);
      if (in->op == I_CONVERTS) a64_scvtf(B, fsf, ft, d, x);
      else a64_ucvtf(B, fsf, ft, d, x);
      commit(s, v, d);
      break;
    }
    case I_DEMOTE: { int x = rd(s, a[0], F0); int d = wr(s, v, F1);
                     a64_fcvt(B, FT_S, FT_D, d, x); commit(s, v, d); break; }
    case I_PROMOTE: { int x = rd(s, a[0], F0); int d = wr(s, v, F1);
                      a64_fcvt(B, FT_D, FT_S, d, x); commit(s, v, d); break; }
    case I_REINTERPRET: {
      ValType from = (ValType)in->imm;
      if (vt_is_float(from)) {
        int x = rd(s, a[0], F0); int d = wr(s, v, S0);
        a64_fmov_to_gpr(B, wide, wide ? FT_D : FT_S, d, x);
        commit(s, v, d);
      } else {
        int x = rd(s, a[0], S0); int d = wr(s, v, F0);
        a64_fmov_from_gpr(B, wide, ft, d, x);
        commit(s, v, d);
      }
      break;
    }

    case I_SELECT: {
      int fp = is_fp_value(f, v);
      uint64_t fimm;
      if (!s->no_csel_fuse && sel_fuses(s, v, &fimm)) {
        /* the compare's operands and the select's first, then the pair */
        Ref c = a[2];
        Inst *ci = &f->insts[c];
        bool value = !((ci->op >= I_EQ && ci->op <= I_GEU) || ci->op == I_EQZ);
        Ref *ca = value ? &a[2] : ir_args(f, c);
        int d = wr(s, v, S3);
        int r0 = rd(s, ca[0], S0);
        int r1 = fimm == UINT64_MAX ? (ca[1] == ca[0] ? r0 : rd(s, ca[1], S1)) : -1;
        int ro[2];
        for (int k = 0; k < 2; k++) {
          Ref x = a[k];
          if (f->insts[x].op == I_CONST && (f->insts[x].imm & 0xffffffffu) == 0) ro[k] = RZR;
          else if (x == ca[0]) ro[k] = r0;
          else if (r1 >= 0 && x == ca[1]) ro[k] = r1;
          else if (k == 1 && a[1] == a[0]) ro[k] = ro[0];
          else ro[k] = rd(s, x, S1);
        }
        if (fimm == UINT64_MAX) a64_cmp_reg(B, SF32, r0, r1);
        else if (fimm <= 0xfff) a64_cmp_imm(B, SF32, r0, (uint32_t)fimm);
        else a64_adds_imm(B, SF32, RZR, r0,
                          (uint32_t)((uint64_t)(-(int64_t)(int32_t)fimm) & 0xffffffffu), 0);
        int cc = value ? CC_NE : cond_of(s, c);
        a64_csel(B, SF32, d, ro[0], ro[1], cc);
        commit(s, v, d);
        /* the flags are the compare's, for the selects after this one */
        s->cmp_ref = c; s->cmp_cc = cc; s->cmp_epoch = B->flags_epoch;
        int kind, csf;
        Ref fa, fb;
        uint64_t fk;
        if (!value && describe_compare(s, c, &kind, &fa, &fb, &fk, &csf)) {
          s->fl_kind = kind; s->fl_a = fa; s->fl_b = fb;
          s->fl_imm = fk; s->fl_sf = csf; s->fl_epoch = B->flags_epoch;
        } else {
          s->fl_kind = 0;
        }
        g_stats.csel_fused++;
        break;
      }

      int cc = CC_NE;
      bool folded_cond = s->folded && s->folded[a[2]];
      /* Not only the instruction after the compare: csel, cset and the moves
         and loads between leave the flags alone, so every select on a
         compare in the epoch it set -- argmin's index and minimum both --
         reads them, and the minimum's chain is cmp, csel rather than cmp,
         cset, cmp, csel. */
      bool flags_live = s->cmp_ref == a[2] && s->cmp_epoch == B->flags_epoch;
      if (folded_cond) {
        emit_compare(s, a[2]);
        cc = cond_of(s, a[2]);
      } else if (flags_live) {
        cc = s->cmp_cc;
      } else {
        if (a[2] == s->cmp_bare)
          warmc_fatal("internal: the flags of compare v%u were lost before its select", a[2]);
        int c = rd(s, a[2], S2);
        a64_cmp_imm(B, SF32, c, 0);
      }
      if (is_vec_value(f, v)) {

        int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
        a64_csetm(B, SF64, TMP0, cc);
        a64_vdup_gpr(B, 3, FTMP, TMP0);
        a64_vbsl(B, FTMP, x, y);
        int d = wr(s, v, F2);
        if (d != FTMP) a64_vmov(B, d, FTMP);
        commit(s, v, d);
        break;
      }
      if (fp) {
        int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
        int d = wr(s, v, F2);
        a64_fcsel(B, ft, d, x, y, cc);
        commit(s, v, d);
        break;
      }

      uint64_t m = sf ? ~0ull : 0xffffffffu;
      bool xc = f->insts[a[0]].op == I_CONST, yc = f->insts[a[1]].op == I_CONST;
      uint64_t xv = f->insts[a[0]].imm & m, yv = f->insts[a[1]].imm & m;
      int d = wr(s, v, S3);
      if (xc && yc && yv == 0 && (xv == 1 || xv == m)) {
        if (xv == 1) a64_cset(B, sf, d, cc);
        else a64_csetm(B, sf, d, cc);
      } else if (xc && yc && xv == 0 && (yv == 1 || yv == m)) {
        if (yv == 1) a64_cset(B, sf, d, cc_invert(cc));
        else a64_csetm(B, sf, d, cc_invert(cc));
      } else if (s->csinc && s->csinc[v]) {
        /* c ? x + 1 : y is y, or x + 1 where c holds: csinc increments
           where its condition fails, csneg negates and csinv inverts. */
        int slot = (s->csinc[v] & 3) - 1;
        Ref src = ir_args(f, a[slot])[(s->csinc[v] & CS_NEG) ? 1 : 0];
        int y = rd(s, a[slot ^ 1], S0), x = rd(s, src, S1);
        int c2 = slot == 0 ? cc_invert(cc) : cc;
        if (s->csinc[v] & CS_NEG) a64_csneg(B, sf, d, y, x, c2);
        else if (s->csinc[v] & CS_INV) a64_csinv(B, sf, d, y, x, c2);
        else a64_csinc(B, sf, d, y, x, c2);
      } else if (xc && !yc && (xv == 1 || xv == m)) {
        /* c ? 1 : y and c ? -1 : y: y, or the zero register plus one or
           inverted where c holds */
        int y = rd(s, a[1], S1);
        if (xv == 1) a64_csinc(B, sf, d, y, RZR, cc_invert(cc));
        else a64_csinv(B, sf, d, y, RZR, cc_invert(cc));
      } else if (yc && !xc && (yv == 1 || yv == m)) {
        int x = rd(s, a[0], S0);
        if (yv == 1) a64_csinc(B, sf, d, x, RZR, cc);
        else a64_csinv(B, sf, d, x, RZR, cc);
      } else if (xc && yc && (xv == ((yv + 1) & m) || yv == ((xv + 1) & m))) {
        /* two constants a step apart: one of them and a csinc */
        bool up = xv == ((yv + 1) & m);
        uint64_t lo = up ? yv : xv;
        int k = lo == 0 ? RZR : rd(s, a[up ? 1 : 0], S0);
        a64_csinc(B, sf, d, k, k, up ? cc_invert(cc) : cc);
      } else {
        int x = (xc && xv == 0) ? RZR : rd(s, a[0], S0);
        int y = (yc && yv == 0) ? RZR : rd(s, a[1], S1);
        a64_csel(B, sf, d, x, y, cc);
      }
      commit(s, v, d);
      break;
    }

    case I_MEM_ADDR: {
      if (s->addr_fuse && s->addr_fuse[v]) break;
      if (s->ptr_fuse && s->ptr_fuse[v]) break;
      Ref ax = addr_index(s, f, a[0]);
      int x = rd(s, ax, S0);
      if (in->nargs == 2) {

        int c = rd(s, a[1], S3);
        int d = wr(s, v, S1);
        if (is64(f, ax)) a64_add_reg(B, SF64, d, c, x);
        else a64_add_uxtw(B, d, c, x, 0);
        commit(s, v, d);
        break;
      }
      int d = wr(s, v, S1);

      if (is64(f, ax)) { if (d != x) a64_mov_reg(B, SF64, d, x); }
      else a64_uxtw(B, d, x);
      if (in->imm) {
        if (in->imm < 0x1000) a64_add_imm(B, SF64, d, d, (uint32_t)in->imm, 0);
        else { a64_load_imm(B, SF64, TMP0, in->imm); a64_add_reg(B, SF64, d, d, TMP0); }
      }
      commit(s, v, d);
      break;
    }

    case I_MEM_PTR: {
      if (is_imm_at(s, v, 0, a[0])) {
        uint64_t k = imm_of(s, a[0]) & 0xffffffffu;
        int mb = mem_base_reg(s, in->imm2, S2);
        int d = wr(s, v, S1);
        if (k <= 0xfff) a64_add_imm(B, SF64, d, mb, (uint32_t)k, 0);
        else a64_add_imm(B, SF64, d, mb, (uint32_t)(k >> 12), 1);
        commit(s, v, d);
        break;
      }
      Ref ax = addr_index(s, f, a[0]);
      int x = rd(s, ax, S0);
      int mb = mem_base_reg(s, in->imm2, S2);
      int d = wr(s, v, S1);

      if (is64(f, ax)) a64_add_reg(B, SF64, d, mb, x);
      else a64_add_uxtw(B, d, mb, x, 0);
      commit(s, v, d);
      break;
    }

    case I_MEM_BASE: {
      int d = wr(s, v, S1);
      uint64_t off = in->imm;

      if (in->imm2 != 0 && off == 0) {
        uint32_t disp;
        int at = vmctx_at(s, mem_slot_off(f, in->imm2) + MEM_SLOT_BASE, 3,
                          &disp, TMP0);
        a64_ldr_imm(B, 3, d, at, disp);
        commit(s, v, d);
        break;
      }
      int mb = mem_base_reg(s, in->imm2, S2);
      if (off <= 0xfff) a64_add_imm(B, SF64, d, mb, (uint32_t)off, 0);
      else if ((off & 0xfff) == 0 && (off >> 12) <= 0xfff)
        a64_add_imm(B, SF64, d, mb, (uint32_t)(off >> 12), 1);
      else if ((off >> 24) == 0) {
        a64_add_imm(B, SF64, d, mb, (uint32_t)(off >> 12), 1);
        a64_add_imm(B, SF64, d, d, (uint32_t)(off & 0xfff), 0);
      } else {
        a64_load_imm(B, SF64, TMP0, off);
        a64_add_reg(B, SF64, d, mb, TMP0);
      }
      commit(s, v, d);
      break;
    }

    case I_MEM_LIMIT: {
      int d = wr(s, v, S0);
      int sz;
      if (in->imm == 0) {
        reload_size(s);
        sz = REG_MEMSIZE;
      } else {
        uint32_t disp;
        int at = vmctx_at(s, mem_slot_off(f, in->imm) + MEM_SLOT_LEN, 3, &disp, d);
        a64_ldr_imm(B, 3, d, at, disp);
        sz = d;
      }
      uint64_t need = in->imm2;
      if (need <= 0xfff) {
        a64_sub_imm(B, SF64, d, sz, (uint32_t)need, 0);
      } else if ((need & 0xfff) == 0 && (need >> 12) <= 0xfff) {
        a64_sub_imm(B, SF64, d, sz, (uint32_t)(need >> 12), 1);
      } else {
        a64_load_imm(B, SF64, TMP0, need);
        a64_sub_reg(B, SF64, d, sz, TMP0);
      }
      commit(s, v, d);
      break;
    }

    case I_BOUNDS_CHECK: {

      if (in->nargs == 2) {
        int addr2 = rd(s, a[0], TMP1);
        int lim = rd(s, a[1], TMP0);
        a64_cmp_reg(B, SF64, addr2, lim);
        trap_if_cc(s, CC_HI, TRAP_MEM_OOB);
        break;
      }
      int addr = rd(s, a[0], TMP1);
      int limit = REG_MEMSIZE;
      if (in->imm == 0) reload_size(s);
      else {
        uint32_t disp;
        int mb = vmctx_at(s, mem_slot_off(f, in->imm) + MEM_SLOT_LEN, 3, &disp, S2);
        a64_ldr_imm(B, 3, S2, mb, disp);
        limit = S2;
      }
      if (f->m->memories[in->imm].index64) {

        a64_load_imm(B, SF64, S3, in->imm2);
        a64_subs_reg(B, SF64, TMP0, limit, S3);
        trap_if_cc(s, CC_CC, TRAP_MEM_OOB);
        a64_cmp_reg(B, SF64, addr, TMP0);
        trap_if_cc(s, CC_HI, TRAP_MEM_OOB);
      } else {
        a64_add_imm(B, SF64, TMP0, addr, (uint32_t)in->imm2, 0);
        a64_cmp_reg(B, SF64, TMP0, limit);
        trap_if_cc(s, CC_HI, TRAP_MEM_OOB);
      }
      break;
    }

    case I_LOAD: {
      int size = (int)MEMF_SIZE(in->imm2);
      int sgn = (in->imm2 & MEMF_SIGNED) != 0;

      int fp = vt_is_vec_reg((ValType)in->type);
      int d = fp ? wr(s, v, F0) : wr(s, v, S0);

      if (s->ptr_fuse && s->ptr_fuse[a[0]]) {

        Ref *ea = ir_args(f, a[0]);
        Ref ax = addr_index(s, f, ea[0]);
        int p = rd(s, ea[1], TMP1);
        int x = rd(s, ax, TMP0);
        int o = idx_option(f, ax);
        if (fp) a64_fldr_reg(B, size, d, p, x, o, 0);
        else if (sgn) a64_ldrs_reg(B, size, wide, d, p, x, o, 0);
        else a64_ldr_reg(B, size, d, p, x, o, 0);
        commit(s, v, d);
        break;
      }
      if (f->insts[a[0]].op == I_MEM_PTR) {

        int p = rd(s, a[0], TMP1);
        if (fp) a64_fldr_imm(B, size, d, p, (uint32_t)in->imm);
        else if (sgn) a64_ldrs_imm(B, size, wide, d, p, (uint32_t)in->imm);
        else a64_ldr_imm(B, size, d, p, (uint32_t)in->imm);
        commit(s, v, d);
        break;
      }
      int mb = mem_base_reg(s, MEMF_MEM(in->imm2), S2);
      if (is_imm_at(s, v, 0, a[0])) {
        uint32_t off = (uint32_t)imm_of(s, a[0]);
        if (fp) a64_fldr_imm(B, size, d, mb, off);
        else if (sgn) a64_ldrs_imm(B, size, wide, d, mb, off);
        else a64_ldr_imm(B, size, d, mb, off);
      } else if (s->addr_fuse && s->addr_fuse[a[0]]) {
        uint64_t off = f->insts[a[0]].imm;
        Ref aa = addr_index(s, f, ir_args(f, a[0])[0]);
        uint32_t m = MEMF_MEM(in->imm2);
        if (off == 0) {
          int x = rd(s, aa, TMP1);
          int o = idx_option(f, aa);
          if (fp) a64_fldr_reg(B, size, d, mb, x, o, 0);
          else if (sgn) a64_ldrs_reg(B, size, wide, d, mb, x, o, 0);
          else a64_ldr_reg(B, size, d, mb, x, o, 0);
        } else {
          if (!(s->t1_was && s->t1_aa == aa && s->t1_mem == m)) {
            int x = rd(s, aa, TMP1);
            if (is64(f, aa)) a64_add_reg(B, SF64, TMP1, mb, x);
            else a64_add_uxtw(B, TMP1, mb, x, 0);
          }
          if (fp) a64_fldr_imm(B, size, d, TMP1, (uint32_t)off);
          else if (sgn) a64_ldrs_imm(B, size, wide, d, TMP1, (uint32_t)off);
          else a64_ldr_imm(B, size, d, TMP1, (uint32_t)off);
          s->t1_ok = true;
          s->t1_aa = aa;
          s->t1_mem = m;
        }
      } else {
        int ea = rd(s, a[0], TMP1);
        if (fp) a64_fldr_reg(B, size, d, mb, ea, 3, 0);
        else if (sgn) a64_ldrs_reg(B, size, wide, d, mb, ea, 3, 0);
        else a64_ldr_reg(B, size, d, mb, ea, 3, 0);
      }
      commit(s, v, d);
      break;
    }

    case I_STORE: {
      int size = (int)MEMF_SIZE(in->imm2);
      int fp = is_fp_value(f, a[1]);
      int val = fp ? rd(s, a[1], F0) : rd_zr(s, a[1], S1);

      if (s->ptr_fuse && s->ptr_fuse[a[0]]) {
        Ref *ea = ir_args(f, a[0]);
        Ref ax = addr_index(s, f, ea[0]);
        int p = rd(s, ea[1], TMP1);
        int x = rd(s, ax, TMP0);
        int o = idx_option(f, ax);
        if (fp) a64_fstr_reg(B, size, val, p, x, o, 0);
        else a64_str_reg(B, size, val, p, x, o, 0);
        break;
      }
      if (f->insts[a[0]].op == I_MEM_PTR) {
        int p = rd(s, a[0], TMP1);
        if (fp) a64_fstr_imm(B, size, val, p, (uint32_t)in->imm);
        else a64_str_imm(B, size, val, p, (uint32_t)in->imm);
        break;
      }
      int mb = mem_base_reg(s, MEMF_MEM(in->imm2), S2);
      if (is_imm_at(s, v, 0, a[0])) {
        uint32_t off = (uint32_t)imm_of(s, a[0]);
        if (fp) a64_fstr_imm(B, size, val, mb, off);
        else a64_str_imm(B, size, val, mb, off);
      } else if (s->addr_fuse && s->addr_fuse[a[0]]) {
        uint64_t off = f->insts[a[0]].imm;
        Ref aa = addr_index(s, f, ir_args(f, a[0])[0]);
        uint32_t m = MEMF_MEM(in->imm2);
        if (off == 0) {
          int x = rd(s, aa, TMP1);
          int o = idx_option(f, aa);
          if (fp) a64_fstr_reg(B, size, val, mb, x, o, 0);
          else a64_str_reg(B, size, val, mb, x, o, 0);
        } else {
          if (!(s->t1_was && s->t1_aa == aa && s->t1_mem == m)) {
            int x = rd(s, aa, TMP1);
            if (is64(f, aa)) a64_add_reg(B, SF64, TMP1, mb, x);
            else a64_add_uxtw(B, TMP1, mb, x, 0);
          }
          if (fp) a64_fstr_imm(B, size, val, TMP1, (uint32_t)off);
          else a64_str_imm(B, size, val, TMP1, (uint32_t)off);
          s->t1_ok = true;
          s->t1_aa = aa;
          s->t1_mem = m;
        }
      } else {
        int ea = rd(s, a[0], TMP1);
        if (fp) a64_fstr_reg(B, size, val, mb, ea, 3, 0);
        else a64_str_reg(B, size, val, mb, ea, 3, 0);
      }
      break;
    }

    case I_VLOAD: {
      int p = rd(s, a[0], TMP1);
      uint32_t disp;
      int base = vec_base(s, p, in->imm, &disp);
      int d = wr(s, v, F0);
      a64_ldr_q(B, d, base, disp);
      commit(s, v, d);
      break;
    }

    /* ld3 or ld4 into the scratch registers that end at v31, and this
       stream copied out of its one; maybe_ldn loads the siblings beside it
       with it */
    case I_VLDN: {
      if (s->loc[v] == LOC_NONE) break;
      int m = (int)VLDN_M(in->imm2), r0 = 32 - m;
      int base = ldn_base(s, rd(s, a[0], TMP1), in->imm);
      a64_vldn(B, m, VLANE_SIZE(VLDN_LANE(in->imm2)), r0, base);
      commit(s, v, r0 + (int)VLDN_S(in->imm2));
      break;
    }

    case I_VSTORE: {
      int p = rd(s, a[0], TMP1);
      int val = rd(s, a[1], F0);
      uint32_t disp;
      int base = vec_base(s, p, in->imm, &disp);
      a64_str_q(B, val, base, disp);
      break;
    }

    case I_VADD: case I_VSUB: case I_VMUL:
    case I_VAND: case I_VOR: case I_VXOR: case I_VANDNOT:
    case I_VSMAX: case I_VSMIN: case I_VUMAX: case I_VUMIN:
    case I_VUZP1: case I_VUZP2: case I_VZIP1: case I_VZIP2: case I_VCMP:
    case I_VFADD: case I_VFSUB: case I_VFMUL: case I_VFDIV:
    case I_VFMIN: case I_VFMAX:
    case I_VADDSATS: case I_VADDSATU: case I_VSUBSATS: case I_VSUBSATU:
    case I_VAVGRU: case I_VQ15MULR: case I_VEXTMUL: case I_VSWIZZLE: {

      if (in->op == I_VMUL && in->imm2 == VLANE_I64) {
        int lx = rd(s, a[0], F0), ly = rd(s, a[1], F1);
        a64_vrev64(B, 2, F2, ly);
        a64_vmul(B, 2, F2, F2, lx);
        a64_vuaddlp(B, 2, F2, F2);
        a64_vshl_imm(B, 3, F2, F2, 32);
        a64_vxtn(B, 0, 2, FTMP, lx);
        a64_vxtn(B, 0, 2, F3, ly);
        a64_vumlal(B, 0, 2, F2, FTMP, F3);
        commit(s, v, F2);
        break;
      }
      if (in->op == I_VADD && s->vadal && s->vadal[v]) {
        Ref e = a[s->vadal[v] - 1], acc = a[2 - s->vadal[v]];
        int x = rd(s, ir_args(f, e)[0], F0), y = rd(s, acc, F1);
        int d = wr(s, v, F2);
        if (d != y && d == x) d = FTMP;
        if (d != y) a64_vmov(B, d, y);
        if (f->insts[e].imm & 1) a64_vsadalp(B, VLANE_SIZE(in->imm2) - 1, d, x);
        else a64_vuadalp(B, VLANE_SIZE(in->imm2) - 1, d, x);
        commit(s, v, d);
        break;
      }
      if (in->op == I_VOR && s->vrot && s->vrot[v]) {
        /* The shifts are folded (analyse_immediates): a[0] is the left one.
           The source dies at the earliest here, so the result never shares
           its register, which the two-instruction form needs. */
        Inst *sh = &f->insts[a[0]];
        int sz = VLANE_SIZE(sh->imm2), k = s->vrot[v], lane = 8 << sz;
        int x = rd(s, ir_args(f, a[0])[0], F0);
        int d = wr(s, v, F2);
        if (2 * k == lane && sz > 0) {
          if (sz == 1) a64_vrev16(B, d, x);
          else if (sz == 2) a64_vrev32(B, 1, d, x);
          else a64_vrev64(B, 2, d, x);
        } else {
          if (d == x) d = F2;
          a64_vushr_imm(B, sz, d, x, lane - k);
          a64_vsli_imm(B, sz, d, x, k);
        }
        commit(s, v, d);
        break;
      }
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
      int d = wr(s, v, F2);
      int sz = VLANE_SIZE(in->imm2);
      int fsz = (in->imm2 == VLANE_F64);
      switch (in->op) {
        case I_VADD:  a64_vadd(B, sz, d, x, y); break;
        case I_VSUB:  a64_vsub(B, sz, d, x, y); break;
        case I_VMUL:  a64_vmul(B, sz, d, x, y); break;
        case I_VAND:  a64_vand(B, d, x, y); break;
        case I_VOR:   a64_vorr(B, d, x, y); break;
        case I_VXOR:  a64_veor(B, d, x, y); break;
        case I_VANDNOT: a64_vbic(B, d, x, y); break;
        case I_VSMAX: a64_vsmax(B, sz, d, x, y); break;
        case I_VSMIN: a64_vsmin(B, sz, d, x, y); break;
        case I_VUMAX: a64_vumax(B, sz, d, x, y); break;
        case I_VUMIN: a64_vumin(B, sz, d, x, y); break;

        case I_VUZP1: a64_vuzp1(B, (int)in->imm, d, x, y); break;
        case I_VUZP2: a64_vuzp2(B, (int)in->imm, d, x, y); break;
        case I_VZIP1: a64_vzip1(B, (int)in->imm, d, x, y); break;
        case I_VZIP2: a64_vzip2(B, (int)in->imm, d, x, y); break;

        case I_VCMP:
          switch (in->imm) {
            case I_EQ:  a64_vcmeq(B, sz, d, x, y); break;
            case I_NE:  a64_vcmeq(B, sz, d, x, y); a64_vnot(B, d, d); break;
            case I_GTS: a64_vcmgt(B, sz, d, x, y); break;
            case I_GES: a64_vcmge(B, sz, d, x, y); break;
            case I_LTS: a64_vcmgt(B, sz, d, y, x); break;
            case I_LES: a64_vcmge(B, sz, d, y, x); break;
            case I_GTU: a64_vcmhi(B, sz, d, x, y); break;
            case I_GEU: a64_vcmhs(B, sz, d, x, y); break;
            case I_LTU: a64_vcmhi(B, sz, d, y, x); break;
            case I_LEU: a64_vcmhs(B, sz, d, y, x); break;
            case I_FEQ: a64_vfcmeq(B, fsz, d, x, y); break;
            case I_FNE: a64_vfcmeq(B, fsz, d, x, y); a64_vnot(B, d, d); break;
            case I_FGT: a64_vfcmgt(B, fsz, d, x, y); break;
            case I_FGE: a64_vfcmge(B, fsz, d, x, y); break;
            case I_FLT: a64_vfcmgt(B, fsz, d, y, x); break;
            case I_FLE: a64_vfcmge(B, fsz, d, y, x); break;
            default: warmc_fatal("internal: no vector form of %s",
                                 ir_op_name((uint16_t)in->imm));
          }
          break;
        case I_VFADD: a64_vfadd(B, fsz, d, x, y); break;
        case I_VFSUB: a64_vfsub(B, fsz, d, x, y); break;
        case I_VFMUL: a64_vfmul(B, fsz, d, x, y); break;
        case I_VFDIV: a64_vfdiv(B, fsz, d, x, y); break;

        case I_VFMIN: a64_vfmin(B, fsz, d, x, y); break;
        case I_VFMAX: a64_vfmax(B, fsz, d, x, y); break;
        case I_VADDSATS: a64_vsqadd(B, sz, d, x, y); break;
        case I_VADDSATU: a64_vuqadd(B, sz, d, x, y); break;
        case I_VSUBSATS: a64_vsqsub(B, sz, d, x, y); break;
        case I_VSUBSATU: a64_vuqsub(B, sz, d, x, y); break;
        case I_VAVGRU:   a64_vurhadd(B, sz, d, x, y); break;
        case I_VQ15MULR: a64_vsqrdmulh(B, sz, d, x, y); break;

        case I_VEXTMUL:
          if (in->imm & 1) a64_vsmull(B, (in->imm & 2) != 0, sz - 1, d, x, y);
          else a64_vumull(B, (in->imm & 2) != 0, sz - 1, d, x, y);
          break;
        case I_VSWIZZLE: a64_vtbl1(B, d, x, y); break;
        default: break;
      }
      commit(s, v, d);
      break;
    }

    case I_VSHL: case I_VSHRU: case I_VSHRS:
    case I_VFNEG: case I_VFABS: case I_VFSQRT:
    case I_VNOT: case I_VNEG: case I_VABS: case I_VPOPCNT: case I_VREV:
    case I_VEXTADD: case I_VEXTEND:
    case I_VFCEIL: case I_VFFLOOR: case I_VFTRUNC: case I_VFNEAREST:
    case I_VDEMOTE: case I_VPROMOTE: {
      int x = rd(s, a[0], F0);
      int d = wr(s, v, F1);
      int sz = VLANE_SIZE(in->imm2);
      int fsz = (in->imm2 == VLANE_F64);
      switch (in->op) {
        case I_VSHL:   a64_vshl_imm(B, sz, d, x, (int)in->imm); break;
        case I_VSHRU:  a64_vushr_imm(B, sz, d, x, (int)in->imm); break;
        case I_VSHRS:  a64_vsshr_imm(B, sz, d, x, (int)in->imm); break;
        case I_VFNEG:  a64_vfneg(B, fsz, d, x); break;
        case I_VFABS:  a64_vfabs(B, fsz, d, x); break;
        case I_VFSQRT: a64_vfsqrt(B, fsz, d, x); break;
        case I_VNOT:   a64_vnot(B, d, x); break;
        case I_VNEG:   a64_vneg(B, sz, d, x); break;
        case I_VABS:   a64_vabs(B, sz, d, x); break;
        case I_VPOPCNT: a64_vcnt(B, d, x); break;
        /* bytes reversed within each halfword, word or doubleword */
        case I_VREV:
          if (sz == 1) a64_vrev16(B, d, x);
          else if (sz == 2) a64_vrev32(B, 0, d, x);
          else a64_vrev64(B, 0, d, x);
          break;
        case I_VEXTADD:
          if (in->imm & 1) a64_vsaddlp(B, sz - 1, d, x);
          else a64_vuaddlp(B, sz - 1, d, x);
          break;
        case I_VEXTEND:
          if (in->imm & 1) a64_vsshll(B, (in->imm & 2) != 0, sz - 1, d, x, 0);
          else a64_vushll(B, (in->imm & 2) != 0, sz - 1, d, x, 0);
          break;
        case I_VFCEIL:    a64_vfrint(B, fsz, d, x, 1); break;
        case I_VFFLOOR:   a64_vfrint(B, fsz, d, x, 2); break;
        case I_VFTRUNC:   a64_vfrint(B, fsz, d, x, 3); break;
        case I_VFNEAREST: a64_vfrint(B, fsz, d, x, 0); break;

        case I_VDEMOTE:  a64_vfcvtn(B, d, x); break;
        case I_VPROMOTE: a64_vfcvtl(B, d, x); break;
        default: break;
      }
      commit(s, v, d);
      break;
    }

    case I_VSHLV: case I_VSHRUV: case I_VSHRSV: {
      int sz = VLANE_SIZE(in->imm2);
      int lane_bits = 8 << sz;
      int x = rd(s, a[0], F0);
      int d = wr(s, v, F1);
      uint64_t k;
      if (const_arg(f, a[1], &k)) {
        k %= (uint64_t)lane_bits;
        if (k == 0) a64_vmov(B, d, x);
        else if (in->op == I_VSHLV) a64_vshl_imm(B, sz, d, x, (int)k);
        else if (in->op == I_VSHRUV) a64_vushr_imm(B, sz, d, x, (int)k);
        else a64_vsshr_imm(B, sz, d, x, (int)k);
        commit(s, v, d);
        break;
      }
      int n = rd(s, a[1], TMP0);
      uint32_t enc;

      if (!a64_logical_imm((uint64_t)(lane_bits - 1), SF32, &enc))
        warmc_fatal("internal: no mask immediate for a %d-bit lane", lane_bits);
      a64_and_imm(B, SF32, TMP0, n, enc);
      if (in->op != I_VSHLV) a64_neg(B, SF32, TMP0, TMP0);
      a64_vdup_gpr(B, sz, FTMP, TMP0);
      if (in->op == I_VSHRSV) a64_vsshl_reg(B, sz, d, x, FTMP);
      else a64_vushl_reg(B, sz, d, x, FTMP);
      commit(s, v, d);
      break;
    }

    case I_VNARROW: {
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
      int sz = VLANE_SIZE(in->imm2);
      if (in->imm & 1) {
        a64_vsqxtn(B, 0, sz, FTMP, x);
        a64_vsqxtn(B, 1, sz, FTMP, y);
      } else {
        a64_vsqxtun(B, 0, sz, FTMP, x);
        a64_vsqxtun(B, 1, sz, FTMP, y);
      }
      commit(s, v, FTMP);
      break;
    }

    case I_VDOT: {
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
      int d = wr(s, v, F2);
      a64_vsmull(B, 0, 1, FTMP, x, y);
      a64_vsmull(B, 1, 1, F3, x, y);
      a64_vaddp(B, 2, d, FTMP, F3);
      commit(s, v, d);
      break;
    }

    case I_VDOTI7: case I_VDOTI7ADD: {
      /* The deterministic profile's reading, on every target (see
         simd_dot_i7 in isel_x64_ops.inc): both operands signed and adjacent
         products added with wrap-around -- smull, smull2, addp.  The i32x4
         form widens that i16x8 stage pairwise, signed, and adds c. */
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);

      int c = in->op == I_VDOTI7ADD ? rd(s, a[2], F2) : -1;
      a64_vsmull(B, 0, 0, F3, x, y);
      a64_vsmull(B, 1, 0, F4, x, y);
      a64_vaddp(B, 1, FTMP, F3, F4);
      if (in->op == I_VDOTI7ADD) {

        a64_vsaddlp(B, 1, FTMP, FTMP);
        a64_vadd(B, 2, FTMP, FTMP, c);
      }
      commit(s, v, FTMP);
      break;
    }

    case I_VFMADD: {
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1), c = rd(s, a[2], F2);
      int fsz = (in->imm2 == VLANE_F64);
      /* fmla adds into its destination: the result's own register, the
         addend's where the allocator tied them, unless it is a factor */
      int d = wr(s, v, FTMP);
      if (d == x || d == y) d = FTMP;
      if (d != c) a64_vmov(B, d, c);
      if (in->imm & 1) a64_vfmls(B, fsz, d, x, y);
      else             a64_vfmla(B, fsz, d, x, y);
      commit(s, v, d);
      break;
    }

    case I_VEXTMLA: {
      int c = rd(s, a[0], F2), x = rd(s, a[1], F0), y = rd(s, a[2], F1);
      int sz = VLANE_SIZE(in->imm2);
      bool hi = (in->imm & 2) != 0;
      /* umlal adds into its destination: the accumulator's register where
         the allocator tied them, unless it is a factor */
      int d = wr(s, v, FTMP);
      if (d == x || d == y) d = FTMP;
      if (d != c) a64_vmov(B, d, c);
      if (in->imm & 1) a64_vsmlal(B, hi, sz - 1, d, x, y);
      else a64_vumlal(B, hi, sz - 1, d, x, y);
      commit(s, v, d);
      break;
    }

    case I_VADDHN: {
      int x0 = rd(s, a[0], F0), y0 = rd(s, a[1], F1);
      int x1 = rd(s, a[2], F2), y1 = rd(s, a[3], F3);
      int sz = VLANE_SIZE(in->imm2);
      /* addhn2 keeps the low half addhn wrote */
      int d = wr(s, v, FTMP);
      if (d == x1 || d == y1) d = FTMP;
      a64_vaddhn(B, 0, sz - 1, d, x0, y0);
      a64_vaddhn(B, 1, sz - 1, d, x1, y1);
      commit(s, v, d);
      break;
    }

    case I_VFPMIN: case I_VFPMAX: {
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
      int fsz = (in->imm2 == VLANE_F64);
      if (in->op == I_VFPMIN) a64_vfcmgt(B, fsz, FTMP, x, y);
      else a64_vfcmgt(B, fsz, FTMP, y, x);
      a64_vbsl(B, FTMP, y, x);
      commit(s, v, FTMP);
      break;
    }

    case I_VTRUNCSAT: {
      int x = rd(s, a[0], F0);
      int d = wr(s, v, F1);
      bool sgn = (in->imm & 1) != 0;
      if (in->imm2 == VLANE_F32) {
        if (sgn) a64_vfcvtzs(B, 0, d, x); else a64_vfcvtzu(B, 0, d, x);
      } else {

        if (sgn) { a64_vfcvtzs(B, 1, FTMP, x); a64_vsqxtn(B, 0, 2, d, FTMP); }
        else     { a64_vfcvtzu(B, 1, FTMP, x); a64_vuqxtn(B, 0, 2, d, FTMP); }
      }
      commit(s, v, d);
      break;
    }

    case I_VCONVERT: {
      int x = rd(s, a[0], F0);
      int d = wr(s, v, F1);
      bool sgn = (in->imm & 1) != 0;
      if (in->imm2 == VLANE_F32) {
        if (sgn) a64_vscvtf(B, 0, d, x); else a64_vucvtf(B, 0, d, x);
      } else {
        if (sgn) { a64_vsshll(B, 0, 2, FTMP, x, 0); a64_vscvtf(B, 1, d, FTMP); }
        else     { a64_vushll(B, 0, 2, FTMP, x, 0); a64_vucvtf(B, 1, d, FTMP); }
      }
      commit(s, v, d);
      break;
    }

    case I_VEXTRACTS: case I_VEXTRACTU: {
      int x = rd(s, a[0], F0);
      int sz = VLANE_SIZE(in->imm2);
      int idx = (int)in->imm;
      if (VLANE_IS_FLOAT(in->imm2)) {

        int d = wr(s, v, F1);
        a64_vdup_lane(B, sz, d, x, idx);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        if (in->op == I_VEXTRACTS) a64_vsmov(B, SF32, sz, d, x, idx);
        else a64_vumov(B, sz, d, x, idx);
        commit(s, v, d);
      }
      break;
    }

    case I_VREPLACE: {
      int sz = VLANE_SIZE(in->imm2);
      int idx = (int)in->imm;
      bool flt = VLANE_IS_FLOAT(in->imm2);
      int x = rd(s, a[0], F0);
      int sc = flt ? rd(s, a[1], F2) : rd(s, a[1], S0);
      int d = wr(s, v, F1);

      int work = (flt && d == sc) ? FTMP : d;
      if (work != x) a64_vmov(B, work, x);
      if (flt) a64_vins_elem(B, sz, work, idx, sc, 0);
      else a64_vins_gpr(B, sz, work, idx, sc);
      commit(s, v, work);
      break;
    }

    case I_VSHUFFLE: {
      uint64_t lo = in->imm, hi = in->imm2;
      bool from_x = false, from_y = false;
      for (int i = 0; i < 16; i++) {
        uint32_t idx = (uint32_t)((i < 8 ? (lo >> (8 * i)) : (hi >> (8 * (i - 8)))) & 0xff);
        if (idx < 16) from_x = true; else from_y = true;
      }

      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
      int d = wr(s, v, F2);
      if (!from_y) {
        int t1 = rd(s, a[2], F3);
        a64_vtbl1(B, d, x, t1);
      } else if (!from_x) {
        int t1 = rd(s, a[2], F3);
        a64_vtbl1(B, d, y, t1);
      } else {
        int t1 = rd(s, a[2], F3);
        a64_vtbl1(B, FTMP, x, t1);
        int t2 = rd(s, a[3], F4);
        a64_vtbl1(B, F4, y, t2);
        a64_vorr(B, d, FTMP, F4);
      }
      commit(s, v, d);
      break;
    }

    case I_VANYTRUE: {
      int x = rd(s, a[0], F0);
      int d = wr(s, v, S0);
      a64_vumaxp(B, 2, FTMP, x, x);
      a64_fmov_to_gpr(B, SF64, FT_D, TMP0, FTMP);
      a64_cmp_imm(B, SF64, TMP0, 0);
      a64_cset(B, SF32, d, CC_NE);
      commit(s, v, d);
      break;
    }

    case I_VALLTRUE: {
      int x = rd(s, a[0], F0);
      int d = wr(s, v, S0);
      int sz = VLANE_SIZE(in->imm2);
      if (sz < 3) {
        a64_vuminv(B, sz, FTMP, x);
        a64_fmov_to_gpr(B, SF32, FT_S, TMP0, FTMP);
        a64_cmp_imm(B, SF32, TMP0, 0);
        a64_cset(B, SF32, d, CC_NE);
      } else {
        a64_vcmeq_zero(B, 3, FTMP, x);
        a64_vumaxp(B, 2, FTMP, FTMP, FTMP);
        a64_fmov_to_gpr(B, SF64, FT_D, TMP0, FTMP);
        a64_cmp_imm(B, SF64, TMP0, 0);
        a64_cset(B, SF32, d, CC_EQ);
      }
      commit(s, v, d);
      break;
    }

    case I_VBITMASK: {
      int x = rd(s, a[0], F0);
      int w = rd(s, a[1], F2);
      int d = wr(s, v, S0);
      int sz = VLANE_SIZE(in->imm2);
      a64_vcmlt_zero(B, sz, F1, x);
      a64_vand(B, F1, F1, w);
      if (sz == 0) {
        a64_vaddv(B, 0, 0, FTMP, F1);
        a64_fmov_to_gpr(B, SF32, FT_S, TMP0, FTMP);
        a64_vext(B, F2, F1, F1, 8);
        a64_vaddv(B, 0, 0, FTMP, F2);
        a64_fmov_to_gpr(B, SF32, FT_S, TMP1, FTMP);
        a64_orr_reg_sh(B, SF32, d, TMP0, TMP1, SH_LSL, 8);
      } else if (sz < 3) {
        a64_vaddv(B, 1, sz, FTMP, F1);
        a64_fmov_to_gpr(B, SF32, FT_S, d, FTMP);
      } else {
        a64_addp_2d(B, FTMP, F1);
        a64_fmov_to_gpr(B, SF64, FT_D, d, FTMP);
      }
      commit(s, v, d);
      break;
    }

    case I_VSEL: {
      int m = rd(s, a[0], F0), x = rd(s, a[1], F1), y = rd(s, a[2], F2);

      int d = wr(s, v, FTMP);
      if (d != m && (d == x || d == y)) d = FTMP;
      if (d != m) a64_vmov(B, d, m);
      a64_vbsl(B, d, x, y);
      commit(s, v, d);
      break;
    }

    case I_VSPLAT: {
      int d = wr(s, v, F0);

      if (vt_is_vec_reg((ValType)f->insts[a[0]].type)) {
        int x = rd(s, a[0], F1);
        a64_vdup_lane0(B, VLANE_SIZE(in->imm2), d, x);
      } else {
        int x = rd(s, a[0], S0);
        a64_vdup_gpr(B, VLANE_SIZE(in->imm2), d, x);
      }
      commit(s, v, d);
      break;
    }

    case I_VREDUCE: {
      int x = rd(s, a[0], F0);
      int d = wr(s, v, S0);
      bool w64 = (in->imm2 == VLANE_I64);
      if (in->imm == I_ADD) {
        if (w64) { a64_addp_2d(B, F1, x); a64_fmov_to_gpr(B, SF64, FT_D, d, F1); }
        else { a64_addv_4s(B, F1, x); a64_fmov_to_gpr(B, SF32, FT_S, d, F1); }
      } else if (in->imm == I_VSMIN || in->imm == I_VSMAX ||
                 in->imm == I_VUMIN || in->imm == I_VUMAX) {

        int sz = (int)in->imm2;
        switch (in->imm) {
          case I_VSMIN: a64_vsminv(B, sz, F1, x); break;
          case I_VSMAX: a64_vsmaxv(B, sz, F1, x); break;
          case I_VUMIN: a64_vuminv(B, sz, F1, x); break;
          default:      a64_vumaxv(B, sz, F1, x); break;
        }
        a64_fmov_to_gpr(B, SF32, FT_S, d, F1);
      } else {

        a64_fmov_to_gpr(B, SF64, FT_D, TMP0, x);
        a64_umov_d1(B, TMP1, x);
        int sf = w64 ? SF64 : SF64;
        switch (in->imm) {
          case I_AND: a64_and_reg(B, sf, TMP0, TMP0, TMP1); break;
          case I_OR:  a64_orr_reg(B, sf, TMP0, TMP0, TMP1); break;
          default:    a64_eor_reg(B, sf, TMP0, TMP0, TMP1); break;
        }
        if (w64) a64_mov_reg(B, SF64, d, TMP0);
        else {
          a64_lsr_imm(B, SF64, TMP1, TMP0, 32);
          switch (in->imm) {
            case I_AND: a64_and_reg(B, SF32, d, TMP0, TMP1); break;
            case I_OR:  a64_orr_reg(B, SF32, d, TMP0, TMP1); break;
            default:    a64_eor_reg(B, SF32, d, TMP0, TMP1); break;
          }
        }
      }
      commit(s, v, d);
      break;
    }

    case I_GLOBAL_GET: {

      int size = (in->type == VT_I32 || in->type == VT_F32) ? 2 : 3;
      int asize = (in->type == VT_V128) ? 4 : size;
      uint32_t off;
      int base = vmctx_at(s, global_off((uint32_t)in->imm),
                          f->m->globals[in->imm].imported ? 3 : asize, &off, S1);

      if (f->m->globals[in->imm].imported) {
        a64_ldr_imm(B, 3, S1, base, off);
        base = S1;
        off = 0;
      }
      if (in->type == VT_V128) {
        int d = wr(s, v, F0);
        a64_ldr_q(B, d, base, off);
        commit(s, v, d);
      } else if (vt_is_float((ValType)in->type)) {
        int d = wr(s, v, F0);
        a64_fldr_imm(B, size, d, base, off);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        a64_ldr_imm(B, size, d, base, off);
        commit(s, v, d);
      }
      break;
    }
    case I_GLOBAL_SET: {
      ValType t = f->m->globals[in->imm].type;
      int size = (t == VT_I32 || t == VT_F32) ? 2 : 3;
      int asize = (t == VT_V128) ? 4 : size;
      uint32_t off;
      int base = vmctx_at(s, global_off((uint32_t)in->imm),
                          f->m->globals[in->imm].imported ? 3 : asize, &off, S1);
      if (f->m->globals[in->imm].imported) {
        a64_ldr_imm(B, 3, S1, base, off);
        base = S1;
        off = 0;
      }
      if (t == VT_V128) { int x = rd(s, a[0], F0); a64_str_q(B, x, base, off); }
      else if (vt_is_float(t)) { int x = rd(s, a[0], F0); a64_fstr_imm(B, size, x, base, off); }
      else { int x = rd_zr(s, a[0], S0); a64_str_imm(B, size, x, base, off); }
      break;
    }

    case I_MEM_SIZE: {
      int d = wr(s, v, S0);
      if (in->imm == 0) {
        a64_ldr_imm(B, wide ? 3 : 2, d, REG_VMCTX, VMCTX_MEM_PAGES);
      } else {

        uint32_t disp;
        int mb = vmctx_at(s, mem_slot_off(f, in->imm) + MEM_SLOT_LEN, 3, &disp, d);
        a64_ldr_imm(B, 3, d, mb, disp);
        a64_lsr_imm(B, SF64, d, d, 16);
      }
      commit(s, v, d);
      break;
    }
    case I_MEM_GROW:
      call_helper_imm(s, "warmc_memory_grow", a, 1, in->imm, v, true);
      break;
    case I_MEM_COPY: {
      if (memop_preserves(f, in)) { emit_memop_r(s, v, a, false); break; }
      size_t site = emit_small_memop(s, in, a, false);
      if (site == SMO_INLINE) break;
      call_helper_imm2(s, "warmc_memory_copy", a, 3, in->imm, in->imm2);
      if (site != (size_t)-1) emit_small_memop_done(s);
      break;
    }
    case I_MEM_FILL: {
      if (memop_preserves(f, in)) { emit_memop_r(s, v, a, true); break; }
      size_t site = emit_small_memop(s, in, a, true);
      if (site == SMO_INLINE) break;
      call_helper_imm(s, "warmc_memory_fill", a, 3, in->imm, 0, false);
      if (site != (size_t)-1) emit_small_memop_done(s);
      break;
    }
    case I_MEM_INIT: {

      csa_save(s, v);
      emit_call_args(s, a, 3);
      a64_load_imm(B, SF32, R4, in->imm);
      a64_load_imm(B, SF32, R5, in->imm2);
      uint32_t sym = obj_intern_sym(s->o, "warmc_memory_init", SYM_UNDEF_FUNC, -1, 0);
      isel_reloc(s, REL_CALL26, B->len, sym, 0);
      a64_bl(B, 0);
      s->mem_valid = false;
      csa_restore(s, v);
      break;
    }
    case I_DATA_DROP: {
      csa_save(s, v);
      a64_mov_reg(B, SF64, R0, REG_VMCTX);
      a64_load_imm(B, SF32, R1, in->imm);
      uint32_t sym = obj_intern_sym(s->o, "warmc_data_drop", SYM_UNDEF_FUNC, -1, 0);
      isel_reloc(s, REL_CALL26, B->len, sym, 0);
      a64_bl(B, 0);
      s->mem_valid = false;
      csa_restore(s, v);
      break;
    }

    case I_TABLE_GET: call_helper_imm(s, "warmc_table_get", a, 1, in->imm, v, true); break;
    case I_TABLE_SET: call_helper_imm(s, "warmc_table_set", a, 2, in->imm, 0, false); break;
    case I_TABLE_GROW: call_helper_imm(s, "warmc_table_grow", a, 2, in->imm, v, true); break;
    case I_TABLE_FILL: call_helper_imm(s, "warmc_table_fill", a, 3, in->imm, 0, false); break;
    case I_TABLE_COPY: call_helper_imm2(s, "warmc_table_copy", a, 3, in->imm, in->imm2); break;
    case I_TABLE_INIT: call_helper_imm2(s, "warmc_table_init", a, 3, in->imm, in->imm2); break;
    case I_ELEM_DROP: {
      csa_save(s, v);
      a64_mov_reg(B, SF64, R0, REG_VMCTX);
      a64_load_imm(B, SF32, R1, in->imm);
      uint32_t sym = obj_intern_sym(s->o, "warmc_elem_drop", SYM_UNDEF_FUNC, -1, 0);
      isel_reloc(s, REL_CALL26, B->len, sym, 0);
      a64_bl(B, 0);
      csa_restore(s, v);
      break;
    }
    case I_TABLE_SIZE: {
      if (in->imm == 0) {
        int d = wr(s, v, S0);
        a64_ldr_imm(B, 3, d, REG_VMCTX, VMCTX_TABLE_LEN);
        commit(s, v, d);
      } else {
        call_helper_imm(s, "warmc_table_size", a, 0, in->imm, v, true);
      }
      break;
    }
    case I_REF_IS_NULL: {
      int x = rd(s, a[0], S0);
      int d = wr(s, v, S0);
      a64_cmp_imm(B, SF64, x, 0);
      a64_cset(B, SF32, d, CC_EQ);
      commit(s, v, d);
      break;
    }
    case I_REF_FUNC: {

      int d = wr(s, v, S0);
      uint32_t off = VMCTX_FUNCREFS(f->m->nglobals, f->m->nimported_funcs,
                                    f->m->ntables, f->m->nmemories) +
                     FUNCREF_SIZE * (uint32_t)in->imm;
      if (off <= 4095) {
        a64_add_imm(B, SF64, d, REG_VMCTX, off, 0);
      } else if ((off & 0xfff) == 0 && (off >> 12) <= 4095) {
        a64_add_imm(B, SF64, d, REG_VMCTX, off >> 12, 1);
      } else {
        a64_load_imm(B, SF64, TMP0, off);
        a64_add_reg(B, SF64, d, REG_VMCTX, TMP0);
      }
      commit(s, v, d);
      break;
    }

    case I_TRAP_IF: {

      /* a cast: its slot test branches to the trap itself */
      if (s->folded && s->folded[a[0]] && f->insts[a[0]].op == I_REF_SLOT) {
        const Inst *t = &f->insts[a[0]];
        emit_slot_test(s, t, rd(s, ir_args(f, a[0])[0], S0), (int)in->imm, NULL);
        break;
      }
      int x = rd(s, a[0], S0);
      trap_if_zero(s, is64(f, a[0]) ? SF64 : SF32, x, (int)in->imm);
      break;
    }
    case I_TRAP: emit_trap(s, (int)in->imm); break;

    case I_CALL: {
      CompType *sig = call_sig(s, in);
      csa_save(s, v);
      Func *fn = &f->m->funcs[in->imm];
      if (fn->native) {
        /* the C function of the import's name, called as C calls it: no
           vmctx, the arguments where C's convention puts them, and the
           system linker to find it */
        if (fn->native_indirect) {
          /* The address is called where it is when no argument's move writes
             that register -- one past the integer arguments, where it is
             hinted -- and else taken ahead of them, as a call_ref's is, into
             a scratch register their moves leave alone. */
          uint32_t nint = 0;
          for (uint32_t j = 0; j + 1 < in->nargs; j++) nint += !is_fp_value(f, a[j]);
          int l = s->loc[a[in->nargs - 1]];
          int p;
          if (l != LOC_NONE && !IS_SLOT(l) && l >= (int)(nint < 8 ? nint : 8) && l <= R15 &&
              l != TMP0 && l != TMP1) {
            p = l;
          } else {
            p = rd(s, a[in->nargs - 1], S2);
            if (p != S2) a64_mov_reg(B, SF64, S2, p);
            p = S2;
          }
          emit_call_args_c(s, a, in->nargs - 1);
          if (s->cold_frame) emit_cold_frame(s);
          a64_blr(B, p);
        } else {
          emit_call_args_c(s, a, in->nargs);
          if (s->cold_frame) emit_cold_frame(s);
          uint32_t sym = obj_intern_sym(s->o, f->m->imports[fn->import_index].name,
                                        SYM_UNDEF_FUNC, -1, 0);
          isel_reloc(s, REL_CALL26, B->len, sym, 0);
          a64_bl(B, 0);
        }
        note_site(s);
        note_safepoint(s, v);
        if (s->cold_frame) { cold_call_done(s, v); break; }
        emit_call_ret(s, v, sig);
        csa_restore(s, v);
        break;
      }
      emit_ret_area(s, sig);
      if (fn->imported) emit_call_args_novmctx(s, a, in->nargs);
      else emit_call_args_own(s, a, in->nargs);
      if (fn->imported) {

        uint32_t off = VMCTX_IMPORTS(f->m->nglobals) + 16u * (uint32_t)in->imm;

        uint32_t disp;
        int base = vmctx_at(s, off, 3, &disp, TMP1);
        a64_ldr_imm(B, 3, TMP0, base, disp);
        a64_ldr_imm(B, 3, R0, base, disp + 8);
        if (s->cold_frame) emit_cold_frame(s);
        a64_blr(B, TMP0);
      } else {
        if (s->cold_frame) emit_cold_frame(s);
        uint32_t sym = obj_intern_sym(s->o, fn->sym, SYM_LOCAL_FUNC, -1, 0);
        isel_reloc(s, REL_CALL26, B->len, sym, 0);
        a64_bl(B, 0);
      }
      note_site(s);
      note_safepoint(s, v);
      if (s->cold_frame) { cold_call_done(s, v); break; }
      emit_call_ret(s, v, sig);
      csa_restore(s, v);
      break;
    }

    case I_CALL_INDIRECT: case I_CALL_REF: {
      CompType *sig = call_sig(s, in);
      csa_save(s, v);
      bool own = false;
      if (in->op == I_CALL_REF) emit_ref_lookup(s, a[in->nargs - 1]);
      else if (!(own = emit_ictab_lookup(s, in, a[in->nargs - 1])))
        emit_indirect_lookup(s, in, a[in->nargs - 1]);

      emit_ret_area(s, sig);
      if (own) {
        emit_call_args_own(s, a, in->nargs - 1);
      } else {
        emit_call_args_novmctx(s, a, in->nargs - 1);
        a64_ldr_imm(B, 3, R0, S1, FUNCREF_INSTANCE);
      }
      if (s->cold_frame) emit_cold_frame(s);
      a64_blr(B, S2);
      note_site(s);
      note_safepoint(s, v);
      if (s->cold_frame) { cold_call_done(s, v); break; }
      emit_call_ret(s, v, sig);
      csa_restore(s, v);
      break;
    }

    case I_RETURN_CALL: case I_RETURN_CALL_INDIRECT: case I_RETURN_CALL_REF: {
      bool indirect = in->op == I_RETURN_CALL_INDIRECT || in->op == I_RETURN_CALL_REF;
      Module *m = f->m;
      uint32_t nargs = in->nargs - (indirect ? 1u : 0u);
      int target = -1;
      bool own = false;

      if (indirect) {
        if (in->op == I_RETURN_CALL_REF) emit_ref_lookup(s, a[in->nargs - 1]);
        else if (!(own = emit_ictab_lookup(s, in, a[in->nargs - 1])))
          emit_indirect_lookup(s, in, a[in->nargs - 1]);
        target = S2;
      }

      if (f->sig->nresults > 1) a64_ldr_imm(B, 3, R8, RSP, s->retptr_slot);

      uint32_t base_off = s->frame + s->saved_bytes;
      int base = RSP;
      if (base_off) {

        if (base_off < 0x1000) a64_add_imm(B, SF64, TMP1, RSP, base_off, 0);
        else { a64_load_imm(B, SF64, TMP0, base_off); a64_add_extx(B, TMP1, RSP, TMP0, 0); }
        base = TMP1;
      }
      emit_call_args_to(s, a, nargs, base);

      if (indirect) {
        if (!own) a64_ldr_imm(B, 3, R0, S1, FUNCREF_INSTANCE);
      } else if (m->funcs[in->imm].imported) {
        uint32_t off = VMCTX_IMPORTS(m->nglobals) + 16u * (uint32_t)in->imm;
        uint32_t disp;
        int base = vmctx_at(s, off, 3, &disp, TMP1);
        a64_ldr_imm(B, 3, TMP0, base, disp);
        a64_ldr_imm(B, 3, R0, base, disp + 8);
        target = TMP0;
      }

      emit_teardown(s);
      if (target >= 0) {
        a64_br(B, target);
      } else {
        uint32_t sym = obj_intern_sym(s->o, m->funcs[in->imm].sym, SYM_LOCAL_FUNC, -1, 0);
        isel_reloc(s, REL_CALL26, B->len, sym, 0);
        a64_b(B, 0);
      }
      s->tail_done = true;
      break;
    }

    case I_THROW: {

      for (uint32_t i = 0; i < in->nargs; i++) {
        uint32_t at = s->throw_area_off + ARG_STACK_SLOT * i;
        Ref x = a[i];

        if (is_vec_value(f, x)) a64_str_q(B, rd(s, x, FTMP), RSP, at);
        else if (is_fp_value(f, x)) a64_fstr_imm(B, 3, rd(s, x, FTMP), RSP, at);
        else a64_str_imm(B, 3, rd(s, x, TMP0), RSP, at);
      }
      a64_mov_reg(B, SF64, R0, REG_VMCTX);
      a64_load_imm(B, SF32, R1, in->imm);
      if (s->throw_area_off < 0x1000) {
        a64_add_imm(B, SF64, R2, RSP, s->throw_area_off, 0);
      } else {
        a64_load_imm(B, SF64, TMP0, s->throw_area_off);
        a64_add_extx(B, R2, RSP, TMP0, 0);
      }
      a64_load_imm(B, SF32, R3, in->nargs);
      uint32_t sym = obj_intern_sym(s->o, "warmc_throw", SYM_UNDEF_FUNC, -1, 0);
      isel_reloc(s, REL_CALL26, B->len, sym, 0);
      a64_bl(B, 0);
      note_site(s);
      note_safepoint(s, v);
      a64_brk(B, 0);
      s->tail_done = true;
      break;
    }
    case I_THROW_REF: {

      int x = rd(s, a[0], S0);
      if (x != R0) a64_mov_reg(B, SF64, R0, x);
      uint32_t sym = obj_intern_sym(s->o, "warmc_throw_ref", SYM_UNDEF_FUNC, -1, 0);
      isel_reloc(s, REL_CALL26, B->len, sym, 0);
      a64_bl(B, 0);
      note_site(s);
      a64_brk(B, 0);
      s->tail_done = true;
      break;
    }

    case I_EXN_OBJ: {
      int d = wr(s, v, S0);
      a64_ldr_imm(B, 3, d, RSP, s->exn_slot);
      commit(s, v, d);
      break;
    }
    case I_EXN_PAYLOAD: {
      int x = rd(s, a[0], S0);
      uint32_t off = EXN_VALS_OFF + ARG_STACK_SLOT * (uint32_t)in->imm;
      if ((ValType)in->type == VT_V128) {
        int d = wr(s, v, F0);
        a64_ldr_q(B, d, x, off);
        commit(s, v, d);
      } else if (vt_is_float((ValType)in->type)) {
        int d = wr(s, v, F0);
        a64_fldr_imm(B, wide ? 3 : 2, d, x, off);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);

        bool pbar = load_needs_barrier(f, in);
        int sbase = pbar ? emit_gc_load_barrier_pre(s, d, x) : x;
        a64_ldr_imm(B, wide ? 3 : 2, d, x, off);
        if (pbar) emit_gc_load_barrier(s, d, sbase, -1, off,
                                      load_barrier_mask(f, v));
        commit(s, v, d);
      }
      break;
    }

    case I_RESULT: {
      uint32_t off = s->retarea_off + RESULT_SLOT_SIZE * (uint32_t)in->imm;
      ValType rt = (ValType)in->type;
      bool vec = rt == VT_V128, fp = vt_is_float(rt);
      int d = wr(s, v, vec || fp ? F0 : S0);
      sp_access(s, B, false, wide ? 3 : 2, fp, vec, d, off, TMP0);
      commit(s, v, d);
      break;
    }

    case I_STRUCT_NEW: {
      uint64_t total;
      int64_t alen;
      if (!gc_fastnew_shape(f, v, &total, &alen) ||
          !emit_gc_fastnew(s, v, gctype_slot(f->m, (uint32_t)in->imm),
                           total, -1, "warmc_struct_new", NULL, 0))
        call_helper_imm(s, "warmc_struct_new", a, 0,
                        gctype_slot(f->m, (uint32_t)in->imm), v, true);
      break;
    }

    case I_FIELD_GET: {
      int x = rd(s, a[0], S0);
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, SF64, x, TRAP_NULL_REF);
      int size = (int)GCF_SIZE(in->imm2);
      bool sgn = (in->imm2 & GCF_SIGNED) != 0;
      uint32_t off = (uint32_t)in->imm;
      ValType t = (ValType)in->type;

      bool scaled = (off >> size) <= 0xfff;
      int base = x, ro = -1;
      if (!scaled) { a64_load_imm(B, SF64, TMP0, off); ro = TMP0; }
      if (t == VT_V128) {
        int d = wr(s, v, F0);
        if (scaled) a64_ldr_q(B, d, base, off);
        else { a64_add_reg(B, SF64, TMP0, base, ro); a64_ldr_q(B, d, TMP0, 0); }
        commit(s, v, d);
      } else if (vt_is_float(t)) {
        int d = wr(s, v, F0);
        if (scaled) a64_fldr_imm(B, size, d, base, off);
        else { a64_add_reg(B, SF64, TMP0, base, ro); a64_fldr_imm(B, size, d, TMP0, 0); }
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        bool bar = load_needs_barrier(f, in);
        int sbase = bar ? emit_gc_load_barrier_pre(s, d, base) : base;
        if (scaled) {
          if (sgn) a64_ldrs_imm(B, size, wide, d, base, off);
          else a64_ldr_imm(B, size, d, base, off);
        } else {
          if (sgn) a64_ldrs_reg(B, size, wide, d, base, ro, 3, 0);
          else a64_ldr_reg(B, size, d, base, ro, 3, 0);
        }

        if (bar) emit_gc_load_barrier(s, d, sbase, scaled ? -1 : ro,
                                      scaled ? off : 0,
                                      load_barrier_mask(f, v));
        commit(s, v, d);
      }
      break;
    }

    case I_FIELD_SET: {
      int x = rd(s, a[0], S0);
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, SF64, x, TRAP_NULL_REF);
      int size = (int)GCF_SIZE(in->imm2);
      uint32_t off = (uint32_t)in->imm;
      ValType t = (ValType)f->insts[a[1]].type;
      /* The value is read before a far field's address goes into TMP0, as
         in array.set. */
      int y = rd(s, a[1], t == VT_V128 || vt_is_float(t) ? F0 : S1);
      bool scaled = (off >> size) <= 0xfff;
      int base = x;
      if (!scaled) {
        a64_load_imm(B, SF64, TMP0, off);
        a64_add_reg(B, SF64, TMP0, base, TMP0);
        base = TMP0;
        off = 0;
      }

      if (t == VT_V128) a64_str_q(B, y, base, off);
      else if (vt_is_float(t)) a64_fstr_imm(B, size, y, base, off);
      else a64_str_imm(B, size, y, base, off);
      break;
    }

    case I_ARRAY_NEW: {

      bool fast = false;
      uint64_t total;
      int64_t alen;
      if (gc_fastnew_shape(f, v, &total, &alen))
        fast = emit_gc_fastnew(s, v, gctype_slot(f->m, (uint32_t)in->imm),
                               total, alen, "warmc_array_new", a, 1);
      else if (in->nargs == 1 && f->insts[a[0]].op != I_CONST)
        fast = emit_gc_fastnew_len(s, v, gctype_slot(f->m, (uint32_t)in->imm), a);
      if (!fast)
        call_helper_imm_first(s, in->nargs == 1 ? "warmc_array_new"
                                               : "warmc_array_new_fill",
                              a, in->nargs, gctype_slot(f->m, (uint32_t)in->imm),
                              v, true);
      break;
    }
    case I_ARRAY_NEW_SEG:

      call_helper_imm2_first(s, ARRO_KIND(in->imm2) == ARRO_INIT_DATA
                                ? "warmc_array_new_data" : "warmc_array_new_elem",
                             a, in->nargs, gctype_slot(f->m, (uint32_t)in->imm),
                             ARRO_SEG(in->imm2), v, true);
      break;
    case I_ARRAY_OP:
      switch (ARRO_KIND(in->imm2)) {
        case ARRO_FILL:
          call_helper_imm_first(s, "warmc_array_fill", a, in->nargs,
                                gctype_slot(f->m, (uint32_t)in->imm), 0, false);
          break;
        case ARRO_COPY:
          call_helper_imm_first(s, "warmc_array_copy", a, in->nargs,
                                gctype_slot(f->m, (uint32_t)in->imm), 0, false);
          break;
        default:
          call_helper_imm2_first(s, ARRO_KIND(in->imm2) == ARRO_INIT_DATA
                                    ? "warmc_array_init_data" : "warmc_array_init_elem",
                                 a, in->nargs, gctype_slot(f->m, (uint32_t)in->imm),
                                 ARRO_SEG(in->imm2), 0, false);
          break;
      }
      break;

    case I_ARRAY_GET: case I_ARRAY_SET: {
      int x = rd(s, a[0], S0);
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, SF64, x, TRAP_NULL_REF);
      int size = (int)GCF_SIZE(in->imm2);
      int idx = rd(s, a[1], S1);

      if (!(in->imm2 & GCF_NOBOUND)) {
        a64_ldr_imm(B, 2, TMP0, x, 4);
        a64_cmp_reg(B, SF32, idx, TMP0);
        trap_if_cc(s, CC_HS, TRAP_ARRAY_OOB);
      }
      /* A store's value is read before the element's address goes into
         TMP0: reading a float or vector constant may build it there. */
      int y = -1;
      ValType yt = VT_I32;
      if (in->op == I_ARRAY_SET) {
        yt = (ValType)f->insts[a[2]].type;
        y = rd(s, a[2], yt == VT_V128 || vt_is_float(yt) ? F0 : S2);
      }
      uint32_t off = f->m->types[in->imm].hdr_size;
      a64_add_uxtw(B, TMP0, x, idx, size);
      if (in->op == I_ARRAY_GET) {
        bool sgn = (in->imm2 & GCF_SIGNED) != 0;
        ValType t = (ValType)in->type;
        if (t == VT_V128) { int d = wr(s, v, F0); a64_ldr_q(B, d, TMP0, off); commit(s, v, d); }
        else if (vt_is_float(t)) { int d = wr(s, v, F0); a64_fldr_imm(B, size, d, TMP0, off); commit(s, v, d); }
        else {
          int d = wr(s, v, S0);
          if (sgn) a64_ldrs_imm(B, size, wide, d, TMP0, off);
          else a64_ldr_imm(B, size, d, TMP0, off);
          if (load_needs_barrier(f, in))
            emit_gc_load_barrier(s, d, TMP0, -1, off, load_barrier_mask(f, v));
          commit(s, v, d);
        }
      } else {
        if (yt == VT_V128) a64_str_q(B, y, TMP0, off);
        else if (vt_is_float(yt)) a64_fstr_imm(B, size, y, TMP0, off);
        else a64_str_imm(B, size, y, TMP0, off);
      }
      break;
    }

    case I_GC_LAB_OPEN:
      a64_ldr_imm(B, 3, TMP0, REG_VMCTX, VMCTX_GC_LAB);
      a64_ldr_imm(B, 3, s->lab_reg, TMP0, 0);
      break;

    case I_GC_LAB_FLUSH:
      a64_ldr_imm(B, 3, TMP0, REG_VMCTX, VMCTX_GC_LAB);
      a64_str_imm(B, 3, s->lab_reg, TMP0, 0);
      break;

    case I_GC_BADMASK: {
      int d = wr(s, v, S0);
      a64_ldr_imm(B, 3, d, REG_VMCTX, VMCTX_GC_BADMASK);
      commit(s, v, d);
      break;
    }

    case I_ARRAY_LEN: {
      int x = rd(s, a[0], S0);
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, SF64, x, TRAP_NULL_REF);
      int d = wr(s, v, S0);
      a64_ldr_imm(B, 2, d, x, 4);
      commit(s, v, d);
      break;
    }

    case I_REF_TEST: {

      /* The run-time id answers for this module's own objects: the header id
         less the instance's base is the object's slot when it is below the
         module's count, and codegen_rtest_slots says which slots pass, so a slot in
         the passing run answers yes and -- when the run is every slot that
         passes -- any other slot of the module's answers no.  An id outside
         the range is another module's object: gc_register_types hands each
         module a disjoint range, so a structurally identical object
         allocated next door carries an id this module never compares with
         -- the false negative 1a4b7db removed the old inline "no" for -- and
         only such an object asks warmc_ref_test, which compares
         structurally.  Null and the i31 tag are representation facts, not
         identities, and stay inline.  Every path leaves its answer in d and
         they meet at the end, for one commit. */
      size_t to_end[3];
      int nend = 0;
      int d = wr(s, v, S1);
      uint32_t lo, hi, nslots;
      bool exact;
      if ((RTEST_KIND(in->imm) & ~RTK_NULLABLE) == RTK_CONCRETE_ANY &&
          !warmc_getenv("WARMC_NO_RTEST_FAST") &&
          codegen_rtest_slots(f->m, in->imm, in->imm2, &lo, &hi, &exact, &nslots)) {
        int x = rd(s, a[0], S0);
        bool nullable = (RTEST_KIND(in->imm) & RTK_NULLABLE) != 0;
        size_t to_i31, to_slow[2];
        int nslow = 0;

        size_t to_null = B->len;
        a64_cbz(B, SF64, x, 0);
        uint32_t tst3;
        if (!a64_logical_imm(3, SF64, &tst3))
          warmc_fatal("internal: 3 is not a logical immediate");
        a64_tst_imm(B, SF64, x, tst3);
        to_i31 = B->len;
        a64_bcond(B, CC_NE, 0);
        a64_ldr_imm(B, 2, S2, x, GC_HDR_WORD_OFF);
        a64_ldr_imm(B, 2, S3, REG_VMCTX, VMCTX_GC_HDR);
        a64_ubfm(B, SF32, S2, S2, GC_TYPE_SHIFT, 29);
        a64_ubfm(B, SF32, S3, S3, GC_TYPE_SHIFT, 29);
        /* the slot, if the object is ours; another module's id below our
           base wraps to far above any slot */
        a64_sub_reg(B, SF32, S2, S2, S3);
        if (exact) {
          rtest_cmp_imm(s, S2, nslots);
          to_slow[nslow++] = B->len;
          a64_bcond(B, CC_HS, 0);
        }
        if (lo <= hi) {
          /* lo == hi: slot == lo; a longer run: slot - lo <= hi - lo */
          int t = S2;
          if (lo != hi && lo) {
            t = S3;
            if (lo <= 0xfffu) {
              a64_sub_imm(B, SF32, t, S2, lo, 0);
            } else {
              a64_load_imm(B, SF32, TMP0, lo);
              a64_sub_reg(B, SF32, t, S2, TMP0);
            }
          }
          rtest_cmp_imm(s, t, lo == hi ? lo : hi - lo);
          if (exact) {
            a64_cset(B, SF32, d, lo == hi ? CC_EQ : CC_LS);
          } else {
            to_slow[nslow++] = B->len;
            a64_bcond(B, lo == hi ? CC_NE : CC_HI, 0);
            a64_load_imm(B, SF32, d, 1);
          }
          to_end[nend++] = B->len;
          a64_b(B, 0);
        }
        a64_patch_b19(B, to_i31, B->len);
        a64_load_imm(B, SF32, d, 0);
        to_end[nend++] = B->len;
        a64_b(B, 0);
        a64_patch_b19(B, to_null, B->len);
        a64_load_imm(B, SF32, d, nullable ? 1 : 0);
        to_end[nend++] = B->len;
        a64_b(B, 0);
        for (int k = 0; k < nslow; k++) a64_patch_b19(B, to_slow[k], B->len);
      }
      csa_save(s, v);
      emit_call_args(s, a, 1);
      a64_load_imm(B, SF32, R2, RTEST_KIND(in->imm));
      a64_load_imm(B, SF64, R3, in->imm2);
      a64_load_imm(B, SF32, R4, RTEST_DEPTH(in->imm));
      uint32_t sym = obj_intern_sym(s->o, "warmc_ref_test", SYM_UNDEF_FUNC, -1, 0);
      isel_reloc(s, REL_CALL26, B->len, sym, 0);
      a64_bl(B, 0);
      s->mem_valid = false;
      if (d != R0) a64_mov_reg(B, SF32, d, R0);
      csa_restore(s, v);
      for (int e = 0; e < nend; e++) patch_b26_checked(s, to_end[e], B->len);
      commit(s, v, d);
      break;
    }

    case I_REF_SLOT: {
      int x = rd(s, a[0], S0);
      size_t fail[3];
      int nf = emit_slot_test(s, in, x, -1, fail);
      int d = wr(s, v, S1);
      a64_load_imm(B, SF32, d, 1);
      size_t to_end = B->len;
      a64_b(B, 0);
      for (int k = 0; k < nf; k++) a64_patch_b19(B, fail[k], B->len);
      a64_load_imm(B, SF32, d, 0);
      a64_patch_b26(B, to_end, B->len);
      commit(s, v, d);
      break;
    }

    case I_REF_CONVERT: {

      int x = rd(s, a[0], S0);
      int d = wr(s, v, S0);
      a64_mov_reg(B, SF64, d, x);
      commit(s, v, d);
      break;
    }

    case I_ANY_OF_EXTERN: case I_EXTERN_OF_ANY: {
      int d = wr(s, v, S1);
      csa_save(s, v);
      emit_call_args(s, a, 1);
      uint32_t sym = obj_intern_sym(s->o, in->op == I_ANY_OF_EXTERN ? "warmc_any_of_extern"
                                                                     : "warmc_extern_of_any",
                                    SYM_UNDEF_FUNC, -1, 0);
      isel_reloc(s, REL_CALL26, B->len, sym, 0);
      a64_bl(B, 0);
      s->mem_valid = false;
      if (d != R0) a64_mov_reg(B, SF64, d, R0);
      csa_restore(s, v);
      commit(s, v, d);
      break;
    }

    case I_REF_I31: {

      int y = rd(s, a[0], S0);
      int d = wr(s, v, S0);
      a64_ubfm(B, SF64, d, y, 63, 30);
      uint32_t enc;
      if (!a64_logical_imm(1, SF64, &enc))
        warmc_fatal("internal: 1 is not a logical immediate");
      a64_orr_imm(B, SF64, d, d, enc);
      commit(s, v, d);
      break;
    }

    case I_I31_GET: {
      int x = rd(s, a[0], S0);
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, SF64, x, TRAP_NULL_REF);
      int d = wr(s, v, S0);

      if (in->imm) a64_sbfm(B, SF32, d, x, 1, 31);
      else a64_ubfm(B, SF32, d, x, 1, 31);
      commit(s, v, d);
      break;
    }

    default:
      warmc_fatal("isel: unhandled IR op %u", in->op);
  }
}

static void emit_return(Isel *s, Block *b) {
  IrFunc *f = s->f;
  CompType *sig = f->sig;
  if (sig->nresults == 1) {
    Ref r = b->ret_args[0];
    if (sig->results[0] == VT_V128) {
      int x = rd(s, r, F0);
      if (x != 0) a64_vmov(B, 0, x);
    } else if (vt_is_float((ValType)f->insts[r].type)) {
      int x = rd(s, r, F0);
      if (x != 0) a64_fmov_reg(B, is64(f, r) ? FT_D : FT_S, 0, x);
    } else {
      /* made, or reloaded, where it is returned: nothing is read after */
      int x = rd(s, r, R0);
      if (x != R0) a64_mov_reg(B, SF64, R0, x);
    }
  } else if (sig->nresults > 1) {
    a64_ldr_imm(B, 3, TMP1, RSP, s->retptr_slot);
    for (uint32_t i = 0; i < sig->nresults; i++) {
      Ref r = b->ret_args[i];
      int size = (sig->results[i] == VT_I32 || sig->results[i] == VT_F32) ? 2 : 3;
      uint32_t at = (uint32_t)i * RESULT_SLOT_SIZE;
      if (sig->results[i] == VT_V128) {
        int x = rd(s, r, F0);
        a64_str_q(B, x, TMP1, at);
      } else if (vt_is_float(sig->results[i])) {
        int x = rd(s, r, F0);
        a64_fstr_imm(B, size, x, TMP1, at);
      } else {
        int x = rd(s, r, S0);
        a64_str_imm(B, size, x, TMP1, at);
      }
    }
  }
  emit_epilogue(s);
}

/* --- ldp/stp pairing ---------------------------------------------------
   Two same-width accesses at adjacent offsets off one base value become one
   pair instruction.  Reordering the second access up to the first is sound
   because everything allowed between is pure and non-trapping: a trap moved
   earlier over pure ops observes the same state.  A pair of stores into
   linear memory whose one half faults on the guard region can leave what
   the split stores would not have -- the first one's bytes unwritten, or the
   half the faulting first store never reached written -- which is within
   what warmc leaves after a trap without -fexact-trap.  With it a store
   wider than a byte is tested before it is made (ir_build.c's mem_ea), so
   no pair that is made can fault.  Register safety: the partner's target
   register must not be read or written between the two sites -- an interval
   that dies there may legally share it. */

static bool pair_reg(Isel *s, Ref r) {
  int l = s->loc[r];
  return l != LOC_NONE && !IS_SLOT(l);
}

/* A zero a store reads as the zero register (rd_zr), which is there at
   either site */
static bool pair_zr(Isel *s, Ref r) {
  const Inst *in = &s->f->insts[r];
  return in->op == I_CONST && s->loc[r] == LOC_NONE &&
         ((ValType)in->type == VT_I64 ? in->imm
          : (ValType)in->type == VT_I32 ? (in->imm & 0xffffffffu) : 1) == 0;
}

static bool pair_pure_op(uint16_t op) {
  switch (op) {
    case I_CONST: case I_ADD: case I_SUB: case I_MUL: case I_MULHU: case I_MULHS:
    case I_AND: case I_OR: case I_XOR: case I_SHL: case I_SHRS: case I_SHRU:
    case I_ROTL: case I_ROTR: case I_CLZ: case I_CTZ: case I_POPCNT:
    case I_BSWAP: case I_BITREV:
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
    case I_MEM_ADDR: case I_MEM_PTR:
      return true;
    default: return false;
  }
}

typedef struct {
  int kind; /* 0 = mem_ptr + load imm, 1 = fused mem_addr index, 2 = gc ref */
  Ref base;
  int64_t off;
  int size;
  bool fp;
  uint32_t mem;
} PairShape;

static bool load_needs_barrier(const IrFunc *f, const Inst *in);

static bool pair_shape(Isel *s, Ref v, PairShape *out) {
  IrFunc *f = s->f;
  Inst *in = &f->insts[v];
  Ref *a = ir_args(f, v);
  Ref a0 = a[0];
  if (in->op == I_FIELD_GET || in->op == I_FIELD_SET) {
    int size = (int)GCF_SIZE(in->imm2);
    if (size != 2 && size != 3) return false;
    if (in->imm2 & GCF_SIGNED) return false;
    ValType t = in->op == I_FIELD_GET ? (ValType)in->type
                                      : (ValType)f->insts[a[1]].type;
    if (t == VT_V128) return false;
    if (in->op == I_FIELD_GET && !vt_is_float(t) && load_needs_barrier(f, in))
      return false;
    out->kind = 2;
    out->base = a0;
    out->off = (int64_t)in->imm;
    out->mem = 0;
    out->fp = vt_is_float(t);
    out->size = size;
    return true;
  }
  int size = (int)MEMF_SIZE(in->imm2);
  if (size != 2 && size != 3) return false;
  if (in->imm2 & MEMF_SIGNED) return false;
  if (s->ptr_fuse && s->ptr_fuse[a0]) return false;
  if (in->op == I_LOAD) {
    if ((ValType)in->type == VT_V128) return false;
    out->fp = vt_is_vec_reg((ValType)in->type);
  } else {
    if ((ValType)f->insts[a[1]].type == VT_V128) return false;
    out->fp = is_fp_value(f, a[1]);
  }
  if (f->insts[a0].op == I_MEM_PTR) {
    out->kind = 0;
    out->base = a0;
    out->off = (int64_t)in->imm;
    out->mem = 0;
  } else if (s->addr_fuse && s->addr_fuse[a0]) {
    out->kind = 1;
    out->base = addr_index(s, f, ir_args(f, a0)[0]);
    out->off = (int64_t)f->insts[a0].imm;
    out->mem = MEMF_MEM(in->imm2);
  } else {
    return false;
  }
  out->size = size;
  return true;
}

static bool pair_is_fpr(IrFunc *f, Ref v) {
  return is_fp_value(f, v) || is_vec_value(f, v);
}

/* Does u read a value in reg -- directly, or through an operand folded into
   it, which is emitted with u and reads its own operands there? */
static bool pair_reads(Isel *s, Ref u, int reg, bool fp, int depth) {
  IrFunc *f = s->f;
  Inst *un = &f->insts[u];
  Ref *ua = ir_args(f, u);
  for (uint32_t t = 0; t < un->nargs; t++) {
    Ref x = ua[t];
    if (s->loc[x] == reg && pair_is_fpr(f, x) == fp) return true;
    if (((s->folded && s->folded[x]) || (s->walias && s->walias[x])) && depth < 8 &&
        pair_reads(s, x, reg, fp, depth + 1))
      return true;
  }
  return false;
}

static bool pair_load_ok(Isel *s, Block *b, uint32_t idx, uint32_t j) {
  IrFunc *f = s->f;
  Ref vj = b->insts[j];
  if (!pair_reg(s, vj)) return false;
  int dj = s->loc[vj];
  bool fj = pair_is_fpr(f, vj);
  if (dj == s->loc[b->insts[idx]]) return false; /* ldp rt == rt2 */
  /* a use-cluster piece holding dj anywhere from idx to j would see the
     partner's result early */
  for (uint32_t k = 0; s->pc_bn && k < s->pc_bn[s->cur_block]; k++) {
    const struct Piece *p = &s->pc[s->pc_bfirst[s->cur_block] + k];
    if (p->reg == dj && pair_is_fpr(f, p->v) == fj && p->lo < j && p->hi >= idx)
      return false;
  }
  for (uint32_t k = idx + 1; k < j; k++) {
    Ref u = b->insts[k];
    Inst *un = &f->insts[u];
    if (ir_has_result(un) && s->loc[u] == dj && pair_is_fpr(f, u) == fj)
      return false;
    if (pair_reads(s, u, dj, fj, 0)) return false;
  }
  return true;
}

static bool pair_store_ok(Isel *s, Block *b, uint32_t idx, uint32_t j) {
  IrFunc *f = s->f;
  Ref valj = ir_args(f, b->insts[j])[1];
  if (pair_zr(s, valj)) return true;
  if (!pair_reg(s, valj)) return false;
  bool fj = pair_is_fpr(f, valj);
  for (uint32_t k = idx + 1; k < j; k++) {
    Ref u = b->insts[k];
    if (u == valj) return false;
    if (ir_has_result(&f->insts[u]) && s->loc[u] == s->loc[valj] &&
        pair_is_fpr(f, u) == fj)
      return false;
  }
  return true;
}

static void pair_emit(Isel *s, Ref vi, Ref vj, const PairShape *si,
                      const PairShape *sj, bool st) {
  IrFunc *f = s->f;
  Ref vlo = si->off <= sj->off ? vi : vj;
  Ref vhi = vlo == vi ? vj : vi;
  int64_t lo = si->off <= sj->off ? si->off : sj->off;
  int p;
  s->t1_was = s->t1_ok;
  s->t1_ok = false;
  if (si->kind == 2) {
    p = rd(s, ir_args(f, vi)[0], S0);
    if (f->insts[vi].imm2 & GCF_NULLCHK)
      trap_if_zero(s, SF64, p, TRAP_NULL_REF);
  } else if (si->kind == 0) {
    p = rd(s, ir_args(f, vi)[0], TMP1);
  } else {
    if (!(s->t1_was && s->t1_aa == si->base && s->t1_mem == si->mem)) {
      int x = rd(s, si->base, TMP1);
      int mb = mem_base_reg(s, si->mem, S2);
      if (is64(f, si->base)) a64_add_reg(B, SF64, TMP1, mb, x);
      else a64_add_uxtw(B, TMP1, mb, x, 0);
    }
    p = TMP1;
  }
  if (st) {
    int rlo = si->fp ? rd(s, ir_args(f, vlo)[1], S1) : rd_zr(s, ir_args(f, vlo)[1], S1);
    int rhi = si->fp ? rd(s, ir_args(f, vhi)[1], S0) : rd_zr(s, ir_args(f, vhi)[1], S0);
    if (si->fp) a64_fstp(B, si->size == 3 ? FT_D : FT_S, rlo, rhi, p, (int)lo, LSP_OFF);
    else a64_stp(B, si->size == 3, rlo, rhi, p, (int)lo, LSP_OFF);
    g_stats.stpairs++;
  } else {
    int rlo = s->loc[vlo], rhi = s->loc[vhi];
    if (si->fp) a64_fldp(B, si->size == 3 ? FT_D : FT_S, rlo, rhi, p, (int)lo, LSP_OFF);
    else a64_ldp(B, si->size == 3, rlo, rhi, p, (int)lo, LSP_OFF);
    commit(s, vlo, rlo);
    commit(s, vhi, rhi);
    g_stats.ldpairs++;
  }
  if (si->kind == 1) {
    s->t1_ok = true;
    s->t1_aa = si->base;
    s->t1_mem = si->mem;
  }
}

static int nl_cmp(const void *a, const void *b) {
  const struct NarrowLoad *x = a, *y = b;
  if (x->base != y->base) return x->base < y->base ? -1 : 1;
  return (x->off > y->off) - (x->off < y->off);
}

/* Does this function load 32 bits from base+off anywhere?  A later load of
   one half of a store pair cannot be forwarded the pair's data: the load
   waits for the stp to reach the cache, and a coroutine whose state is two
   adjacent words, stored together and read back one at a time on the next
   resume, ran 40% slower for it (cppbench/coroutine).  Stores that are only
   written here -- an object being initialised -- still pair. */
static bool narrow_reloaded(Isel *s, Ref base, int64_t off) {
  IrFunc *f = s->f;
  if (!s->nl_built) {
    s->nl_built = true;
    uint32_t cap = 16;
    s->nl = xmalloc(cap * sizeof *s->nl);
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *b = &f->blocks[bi];
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref v = b->insts[i];
        PairShape ps;
        if (f->insts[v].op != I_LOAD || !pair_shape(s, v, &ps)) continue;
        if (ps.kind != 0 || ps.size != 2) continue;
        if (s->nnl == cap) { cap *= 2; s->nl = xrealloc(s->nl, cap * sizeof *s->nl); }
        s->nl[s->nnl].base = ps.base;
        s->nl[s->nnl].off = ps.off;
        s->nnl++;
      }
    }
    qsort(s->nl, s->nnl, sizeof *s->nl, nl_cmp);
  }
  struct NarrowLoad key = { base, off };
  return bsearch(&key, s->nl, s->nnl, sizeof *s->nl, nl_cmp) != NULL;
}

/* Two vector loads, or two vector stores, of neighbouring sixteen bytes
   off one pointer are one ldp or stp of q registers.  The vectoriser's
   copies of its body load and store side by side, and its pointers are its
   loop's parameters: only those pair, in a block that is its own loop.
   Anywhere else a vector access may be a struct of two v128 stored and one
   reloaded, or the reverse, which a pair cannot forward (see
   narrow_reloaded); the vector loop reads nothing it has just written. */
static bool maybe_pair_q(Isel *s, Block *b, uint32_t idx) {
  IrFunc *f = s->f;
  Ref vi = b->insts[idx];
  Inst *in = &f->insts[vi];
  bool st = in->op == I_VSTORE;
  Ref base = ir_args(f, vi)[0];
  const Inst *bi = &f->insts[base];
  if (bi->op != I_PARAM || bi->block != s->cur_block) return false;
  bool self = false;
  for (uint32_t k = 0; k < b->nsuccs && !self; k++)
    self = b->succs[k].target == s->cur_block && !b->succs[k].eh;
  if (!self) return false;
  if (st ? !pair_reg(s, ir_args(f, vi)[1]) : !pair_reg(s, vi)) return false;
  uint32_t lim = idx + 9;
  if (lim > b->ninsts) lim = b->ninsts;
  for (uint32_t j = idx + 1; j < lim; j++) {
    Ref vj = b->insts[j];
    Inst *jn = &f->insts[vj];
    if (jn->op == in->op) {
      int64_t oi = (int64_t)in->imm, oj = (int64_t)jn->imm;
      int64_t lo = oi < oj ? oi : oj, hi = oi < oj ? oj : oi;
      if (ir_args(f, vj)[0] == base && hi - lo == 16 && lo % 16 == 0 &&
          lo / 16 >= -64 && lo / 16 <= 63 &&
          (st ? pair_store_ok(s, b, idx, j) : pair_load_ok(s, b, idx, j))) {
        Ref vlo = oi <= oj ? vi : vj, vhi = vlo == vi ? vj : vi;
        s->t1_was = s->t1_ok;
        s->t1_ok = false;
        int p = rd(s, base, TMP1);
        if (st) {
          int rlo = rd(s, ir_args(f, vlo)[1], F0);
          int rhi = rd(s, ir_args(f, vhi)[1], F1);
          a64_stp_q(B, rlo, rhi, p, (int)lo);
          g_stats.stpairs++;
        } else {
          int rlo = s->loc[vlo], rhi = s->loc[vhi];
          a64_ldp_q(B, rlo, rhi, p, (int)lo);
          commit(s, vlo, rlo);
          commit(s, vhi, rhi);
          g_stats.ldpairs++;
        }
        s->paired[vj] = 1;
        return true;
      }
      /* loads may pass one another; a store is a barrier to a store */
      if (st) return false;
      continue;
    }
    if (!pair_pure_op(jn->op)) return false;
  }
  return false;
}

/* The streams of one interleaved group the vectoriser loads side by side
   are one ld3 or ld4: the first lowers all of them, each copied from the
   scratch register it lands in to its own, as an ldp lowers its pair. */
static bool maybe_ldn(Isel *s, Block *b, uint32_t idx) {
  IrFunc *f = s->f;
  Ref vi = b->insts[idx];
  const Inst *in = &f->insts[vi];
  Ref base = ir_args(f, vi)[0];
  uint32_t m = VLDN_M(in->imm2);
  Ref sib[4] = { REF_NONE, REF_NONE, REF_NONE, REF_NONE };
  if (m < 2 || m > 4 || !pair_reg(s, vi)) return false;
  sib[VLDN_S(in->imm2)] = vi;
  uint32_t nsib = 1, lim = idx + 2 * m;
  if (lim > b->ninsts) lim = b->ninsts;
  for (uint32_t j = idx + 1; j < lim && nsib < m; j++) {
    Ref vj = b->insts[j];
    const Inst *jn = &f->insts[vj];
    if (jn->op == I_VLDN && ir_args(f, vj)[0] == base && jn->imm == in->imm &&
        jn->imm2 != in->imm2 && VLDN_M(jn->imm2) == m &&
        VLDN_LANE(jn->imm2) == VLDN_LANE(in->imm2) &&
        sib[VLDN_S(jn->imm2)] == REF_NONE) {
      if (!pair_load_ok(s, b, idx, j)) break;
      bool clash = false;
      for (uint32_t k = 0; k < m; k++)
        clash |= sib[k] != REF_NONE && s->loc[sib[k]] == s->loc[vj];
      if (clash) break;
      sib[VLDN_S(jn->imm2)] = vj;
      nsib++;
      continue;
    }
    if (jn->op != I_VLDN && jn->op != I_VLOAD && !pair_pure_op(jn->op)) break;
  }
  if (nsib == 1) return false;
  s->t1_was = s->t1_ok;
  s->t1_ok = false;
  /* straight into the streams' registers where the allocator gave them
     consecutive ones, as it does values made one after another */
  int r0 = 32 - (int)m;
  bool direct = nsib == m && s->loc[sib[0]] + (int)m <= r0;
  for (uint32_t k = 1; k < m && direct; k++) direct = s->loc[sib[k]] == s->loc[sib[0]] + (int)k;
  if (direct) r0 = s->loc[sib[0]];
  int p = ldn_base(s, rd(s, base, TMP1), in->imm);
  a64_vldn(B, (int)m, VLANE_SIZE(VLDN_LANE(in->imm2)), r0, p);
  for (uint32_t k = 0; k < m; k++) {
    if (sib[k] == REF_NONE) continue;
    commit(s, sib[k], r0 + (int)k);
    if (sib[k] != vi) s->paired[sib[k]] = 1;
  }
  return true;
}

static bool maybe_pair(Isel *s, Block *b, uint32_t idx) {
  IrFunc *f = s->f;
  Ref vi = b->insts[idx];
  Inst *in = &f->insts[vi];
  if (in->op == I_VLOAD || in->op == I_VSTORE) return maybe_pair_q(s, b, idx);
  if (in->op == I_VLDN) return maybe_ldn(s, b, idx);
  if (in->op != I_LOAD && in->op != I_STORE && in->op != I_FIELD_GET &&
      in->op != I_FIELD_SET)
    return false;
  bool st = in->op == I_STORE || in->op == I_FIELD_SET;
  PairShape si;
  if (!pair_shape(s, vi, &si)) return false;
  /* Raw-pointer loads read records the surrounding code just stored, and an
     ldp that only partially overlaps a pending store cannot forward from it:
     gobench/deferred paid +14% wall for -5.6% icount until this class was
     excluded, and every other row's win survives the exclusion.  Stores
     pair in all three shapes. */
  if (!st && si.kind == 0) return false;
  bool narrow_st = st && si.kind == 0 && si.size == 2;
  if (st ? !pair_reg(s, ir_args(f, vi)[1]) && !pair_zr(s, ir_args(f, vi)[1])
         : !pair_reg(s, vi))
    return false;
  uint32_t lim = idx + 9;
  if (lim > b->ninsts) lim = b->ninsts;
  for (uint32_t j = idx + 1; j < lim; j++) {
    Ref vj = b->insts[j];
    Inst *jn = &f->insts[vj];
    if (jn->op == in->op) {
      PairShape sj;
      if (pair_shape(s, vj, &sj) && sj.kind == si.kind &&
          sj.base == si.base && sj.size == si.size && sj.fp == si.fp &&
          (si.kind != 1 || sj.mem == si.mem) &&
          (si.kind != 2 || !(jn->imm2 & GCF_NULLCHK))) {
        int64_t step = (int64_t)1 << si.size;
        int64_t lo = si.off < sj.off ? si.off : sj.off;
        int64_t hi = si.off < sj.off ? sj.off : si.off;
        if (hi - lo == step && (lo % step) == 0 && lo / step >= -64 &&
            lo / step <= 63 &&
            !(narrow_st && (narrow_reloaded(s, si.base, lo) ||
                            narrow_reloaded(s, si.base, hi))) &&
            (st ? pair_store_ok(s, b, idx, j) : pair_load_ok(s, b, idx, j))) {
          pair_emit(s, vi, vj, &si, &sj, st);
          s->paired[vj] = 1;
          return true;
        }
      }
      /* A same-op access that will not pair: loads may still be passed
         (load/load reorder with no store between is value-preserving and
         either order traps identically on an unchanged state); a second
         store is an aliasing barrier. */
      if (st) return false;
      continue;
    }
    if (!pair_pure_op(jn->op)) return false;
  }
  return false;
}

static void emit_block(Isel *s, uint32_t bi, uint32_t next_bi) {
  IrFunc *f = s->f;
  Block *b = &f->blocks[bi];

  s->cur_block = bi;
  rc_clear(s);
  s->t1_ok = false;

  bool eh = ir_block_has_eh(f, bi);
  s->cmp_ref = REF_NONE;
  s->cmp_bare = REF_NONE;
  /* A block entered only from the one emitted just before it starts with the
     flags that block's last compare left -- its branch, and the moves on the
     edge, write none -- so a search loop's ltu on the key after its eq on the
     key is no second cmp.  The epoch still says whether anything since has
     written them. */
  if (b->npreds != 1 || b->preds[0] != s->prev_bi || b->is_pad) s->fl_kind = 0;
  uint32_t pck = s->pc_bn ? s->pc_bfirst[bi] : 0;
  uint32_t pce = s->pc_bn ? pck + s->pc_bn[bi] : 0;
  for (uint32_t i = 0; i < b->ninsts; i++) {
    s->cur_pos = i;
    s->pc_live = true;
    for (; pck < pce && s->pc[pck].lo == i; pck++)
      if (s->pc[pck].kind == PC_CLUSTER) piece_fill(s, &s->pc[pck]);
    s->at_eh_site = eh && i + 1 == b->ninsts;
    if (s->at_eh_site) emit_eh_stores(s, bi);
    if (s->paired[b->insts[i]]) continue;
    if (!eh && !s->no_ldpair && maybe_pair(s, b, i)) continue;
    s->cur_pos = i;
    emit_inst(s, b->insts[i]);

    /* A cache entry made while emitting this instruction -- an operand rd()
       before its bl, like an indirect call's callee -- outlives the bl that
       just clobbered x30.  x29 survives a call; the x30 half must not. */
    if (op_clobbers_caller_saved(f->insts[b->insts[i]].op))
      rc_kill_reg(s, R30);
  }
  /* the loop pieces of the loops this block enters */
  for (; pck < pce; pck++)
    if (s->pc[pck].kind == PC_FILL) piece_fill(s, &s->pc[pck]);
  s->pc_live = false;
  s->pc_term = s->pc_head != NULL;
  s->at_eh_site = false;

  if (s->tail_done) { s->tail_done = false; s->pc_term = false; return; }

  switch (b->term) {
    case T_JUMP: {
      emit_edge(s, &b->succs[0]);
      if (b->succs[0].target != next_bi) {
        add_fix(s, B->len, 0, b->succs[0].target);
        a64_b(B, 0);
      }
      break;
    }
    case T_BRIF: {
      /* The test ahead of the prologue has taken the early return; what
         gets here goes the other way. */
      if (bi == f->entry && s->early_main) {
        Edge *m = &b->succs[s->early_main - 1];
        emit_edge(s, m);
        if (m->target != next_bi) { add_fix(s, B->len, 0, m->target); a64_b(B, 0); }
        break;
      }
      Edge *t = &b->succs[0], *e = &b->succs[1];
      Ref c = b->term_arg;

      int kind = 0, cc = CC_NE, zreg = 0, zsf = SF32;
      if (s->fused && s->fused[c] && is_condition(s, c)) {
        Inst *ci = &f->insts[c];
        Ref *ca = ir_args(f, c);
        bool tst = s->folded[ca[0]] && f->insts[ca[0]].op == I_AND;
        if (ci->op == I_EQZ && !tst) {
          kind = 1; zsf = is64(f, ca[0]); zreg = rd(s, ca[0], S0);
        } else if ((ci->op == I_EQ || ci->op == I_NE) && !tst &&
                   is_imm_at(s, c, 1, ca[1]) && imm_of(s, ca[1]) == 0) {
          kind = (ci->op == I_EQ) ? 1 : 2;
          zsf = is64(f, ca[0]); zreg = rd(s, ca[0], S0);
        } else {
          emit_compare(s, c);
          cc = cond_of(s, c);
        }
      } else {

        kind = 2; zsf = SF32; zreg = rd(s, c, S0);
      }

      uint32_t tcap = f->blocks[t->target].nparams + 1;
      uint32_t ecap = f->blocks[e->target].nparams + 1;
      Move *tm = xmalloc(tcap * sizeof(Move)), *em = xmalloc(ecap * sizeof(Move));
      int tn = edge_moves(s, t, tm), en = edge_moves(s, e, em);
      int hoist = 0;
      Move *hm = xmalloc((tcap < ecap ? tcap : ecap) * sizeof(Move));
      for (int i = 0; i < tn; i++)
        for (int j = 0; j < en; j++)
          /* A rematerialised move stays on its edge: hoisted, it would
             land between the compare and the branch, and x64 zeroes a
             register with an xor that rewrites the flags. */
          if (!tm[i].remat && !em[j].remat &&
              tm[i].src == em[j].src && tm[i].dst == em[j].dst &&
              tm[i].fp == em[j].fp && tm[i].wide == em[j].wide &&
              tm[i].vec == em[j].vec) {
            hm[hoist++] = tm[i];
            break;
          }
      for (int i = 0; i < hoist && hoist; i++) {
        if (kind != 0 && !hm[i].fp && hm[i].dst == zreg) { hoist = 0; break; }
        for (int j = 0; j < tn; j++)
          if (tm[j].src == hm[i].dst && tm[j].dst != hm[i].dst) { hoist = 0; break; }
        for (int j = 0; hoist && j < en; j++)
          if (em[j].src == hm[i].dst && em[j].dst != hm[i].dst) { hoist = 0; break; }
      }
      if (hoist) {

        int k = 0;
        for (int i = 0; i < tn; i++) {
          bool common = false;
          for (int j = 0; j < hoist; j++)
            if (tm[i].src == hm[j].src && tm[i].dst == hm[j].dst) { common = true; break; }
          if (!common) tm[k++] = tm[i];
        }
        tn = k;
        k = 0;
        for (int i = 0; i < en; i++) {
          bool common = false;
          for (int j = 0; j < hoist; j++)
            if (em[i].src == hm[j].src && em[i].dst == hm[j].dst) { common = true; break; }
          if (!common) em[k++] = em[i];
        }
        en = k;
        parallel_copy(s, hm, hoist);
      }
      xfree(hm);
      bool tmoves = tn != 0, emoves = en != 0;

      bool invert = false;
      if (!tmoves && !emoves && t->target == next_bi && e->target != next_bi)
        invert = true;
      /* Neither side falls through, so the block ends in a conditional
         branch and an unconditional one, and only the side the conditional
         names costs a single branch.  Give it the likelier side: a rotated
         latch whose exit is laid out elsewhere would otherwise test for the
         exit, fall into a jump back, and spend two branches on every trip. */
      else if (!tmoves && !emoves && t->target != next_bi &&
               e->target != next_bi &&
               ir_succ_score(f, b, 1) > ir_succ_score(f, b, 0))
        invert = true;
      if (invert) {
        Edge *tmp = t; t = e; e = tmp;
        if (kind == 0) cc = cc_invert(cc);
        else kind = (kind == 1) ? 2 : 1;
        Move *mtmp = tm; tm = em; em = mtmp;
        int ntmp = tn; tn = en; en = ntmp;
      }

      {

        int s0 = ir_succ_score(f, b, 0), s1 = ir_succ_score(f, b, 1);
        bool t_next = t->target == next_bi, e_next = e->target == next_bi;
        Edge *cold = NULL; Move *cmv = NULL; int cn = 0;
        if (t_next && !e_next && !tmoves && emoves && s0 >= s1) {
          cold = e; cmv = em; cn = en;
          if (kind == 0) cc = cc_invert(cc); else kind = (kind == 1) ? 2 : 1;
        } else if (e_next && !t_next && !emoves && tmoves && s1 >= s0) {
          cold = t; cmv = tm; cn = tn;
        }
        if (cold) {
          struct EdgeStub *st = xmalloc(sizeof *st);
          st->n = cn;
          st->mv = xmalloc((cn ? (size_t)cn : 1) * sizeof(Move));
          memcpy(st->mv, cmv, (size_t)cn * sizeof(Move));
          st->target = cold->target;
          st->next = s->edge_stubs;
          s->edge_stubs = st;
          st->site = br_cc(s, kind, cc, zsf, zreg);

          xfree(tm); xfree(em);
          break;
        }
      }

      if (!tmoves) {
        size_t bsite = br_cc(s, kind, cc, zsf, zreg);
        add_fix(s, bsite, !s->far_br, t->target);
        parallel_copy(s, em, en);
        if (e->target != next_bi) { add_fix(s, B->len, 0, e->target); a64_b(B, 0); }
      } else if (!emoves) {

        if (kind == 0) cc = cc_invert(cc);
        else kind = (kind == 1) ? 2 : 1;
        size_t bsite = br_cc(s, kind, cc, zsf, zreg);
        add_fix(s, bsite, !s->far_br, e->target);
        parallel_copy(s, tm, tn);
        if (t->target != next_bi) { add_fix(s, B->len, 0, t->target); a64_b(B, 0); }
      } else {

        size_t site = br_cc(s, kind, cc, zsf, zreg);
        parallel_copy(s, em, en);
        add_fix(s, B->len, 0, e->target);
        a64_b(B, 0);
        br_patch(s, site, B->len);
        parallel_copy(s, tm, tn);
        add_fix(s, B->len, 0, t->target);
        a64_b(B, 0);
      }
      xfree(tm); xfree(em);
      break;
    }
    case T_BRTABLE: {
      uint32_t n = b->nsuccs - 1;
      /* The index is only read -- by the compare and, zero-extended in the
         address, by the table load -- so it stays where it is: neither the
         adr nor the load writes a register a value can live in. */
      int idx = rd(s, b->term_arg, S0);

      if (n <= 0xfff) {
        a64_cmp_imm(B, SF32, idx, n);
      } else {
        a64_load_imm(B, SF32, TMP0, n);
        a64_cmp_reg(B, SF32, idx, TMP0);
      }
      Edge *de = &b->succs[n];
      size_t to_default = br_cc(s, 0, CC_HS, 0, 0);
      bool default_moves = edge_has_moves(s, de);
      if (!default_moves) add_fix(s, to_default, !s->far_br, de->target);
      /* The entries are halfwords, each its target's distance from the
         table in instructions: 128 KB either way, which a function past
         that size makes too few, and is made again with its branches far,
         and its entries words (148 thousand entries, 299 KB, of a Go
         net/http server's 2271 tables) */
      bool half = !s->far_br;
      uint32_t esz = half ? 2 : 4;
      a64_adr(B, TMP0, 0);
      size_t adr_site = B->len - 4;
      if (half) {
        a64_ldrs_reg(B, 1, 1, TMP1, TMP0, idx, 2, 1);
        a64_add_reg_lsl(B, SF64, TMP0, TMP0, TMP1, 2);
      } else {
        a64_ldrs_reg(B, 2, 1, TMP1, TMP0, idx, 2, 1);
        a64_add_reg(B, SF64, TMP0, TMP0, TMP1);
      }
      a64_br(B, TMP0);
      buf_align(B, 4);
      size_t table_base = B->len;
      a64_patch_adr(B, adr_site, table_base);
      buf_zero(B, (size_t)esz * n);
      buf_align(B, 4);
      /* An edge with nothing to move needs no code of its own, so its entry
         names the target block and the dispatch is one indirect branch; only
         an edge with moves gets a stub that makes them and branches on. */
      bool direct = !warmc_getenv("WARMC_NO_DIRECT_BRTABLE");
      for (uint32_t i = 0; i < n; i++) {
        Edge *e = &b->succs[i];
        if (direct && !edge_has_moves(s, e)) {
          if (half) add_table16_fix(s, table_base + 2 * i, table_base, e->target);
          else add_table_fix(s, table_base + 4 * i, table_base, e->target);
          continue;
        }
        size_t stub = B->len;
        if (half) {
          if (!table16_put(B, table_base + 2 * i, table_base, stub)) s->far_br_needed = true;
        } else {
          uint32_t w = (uint32_t)(int32_t)(stub - table_base);
          memcpy(B->data + table_base + 4 * i, &w, 4);
        }
        emit_edge(s, e);
        add_fix(s, B->len, 0, e->target);
        a64_b(B, 0);
      }
      if (default_moves) {
        br_patch(s, to_default, B->len);
        emit_edge(s, de);
        add_fix(s, B->len, 0, de->target);
        a64_b(B, 0);
      }
      break;
    }
    case T_RETURN: emit_return(s, b); break;
    case T_TRAP: emit_trap(s, (int)b->term_imm); break;
    default: warmc_fatal("isel: block b%u has no terminator", bi);
  }
  s->pc_term = false;
}
