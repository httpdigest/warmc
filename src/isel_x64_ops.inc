/* isel_x64_ops.inc -- per-op x64 emission, included by isel_x64.c.
   Scalar only until M7: every I_V* op fails loudly.  Structure mirrors
   isel_ops.inc; the differences are two-address ALU via rmw2(), BMI2
   shifts, RAX/RDX-implicit div/mul, cmov-based select, and the ucomis
   parity dance for float ==/!=. */

static uint64_t f32bits_(float x) { uint32_t b; memcpy(&b, &x, 4); return b; }
static uint64_t f64bits_(double x) { uint64_t b; memcpy(&b, &x, 8); return b; }

static void load_vconst(Isel *s, int fr, uint64_t lo, uint64_t hi) {
  if (lo == 0 && hi == 0) { x64_vxorps(B, fr, fr, fr); return; }
  if (s->pool_ok) { vconst(s, fr, lo, hi); return; }
  x64_mov_ri(B, 1, TMP0, lo);
  x64_movd_gx(B, 1, fr, TMP0);
  if (hi == lo) {
    x64_vpbroadcast(B, 3, fr, fr);
  } else if (hi != 0) {   /* movq already zeroed the upper lane */
    x64_mov_ri(B, 1, TMP0, hi);
    x64_movd_gx(B, 1, FTMP, TMP0);
    x64_vpunpcklqdq(B, fr, fr, FTMP);
  }
}

/* ---- SIMD helpers ---------------------------------------------------- */

static void simd_mov(Isel *s, int d, int x) {
  if (d != x) x64_vmovdqa_rr(B, d, x);
}
static void simd_zero(Isel *s, int d) { x64_vxorps(B, d, d, d); }
static void simd_ones(Isel *s, int d) { x64_vpcmpeq(B, 2, d, d, d); }

/* d = ~x.  tmp may be d itself. */
static void simd_not(Isel *s, int d, int x, int tmp) {
  simd_ones(s, tmp);
  x64_vpxor(B, d, x, tmp);
}

/* A vectorised memory access is a raw pointer plus a byte offset; only an
   offset past disp32 has to be folded into the pointer. */
static int32_t vec_disp(Isel *s, int *p, uint64_t off) {
  if (off <= 0x7fffffffull) return (int32_t)off;
  x64_mov_ri(B, 1, TMP0, off);
  x64_lea(B, 1, TMP1, *p, TMP0, 0, 0);
  *p = TMP1;
  return 0;
}

/* Immediate lane shifts.  Only word/dword/qword widths have hardware
   forms: bytes borrow the word shift plus the mask the count implies (or
   widen to words for the arithmetic one), and an arithmetic 64-bit shift
   is a logical shift of the sign-biased value with the bias taken back
   out.  Returns the register holding the result. */
static int simd_shift_imm(Isel *s, Ref v, uint16_t op, int sz, int x, int k) {
  if (sz == 0) {
    if (op == I_VSHRS) {
      x64_vpunpckl(B, 0, F1, x, x);
      x64_vpunpckh(B, 0, F2, x, x);
      x64_vpsra_i(B, 1, F1, F1, k + 8);
      x64_vpsra_i(B, 1, F2, F2, k + 8);
      x64_vpacksswb(B, F1, F1, F2);
      return F1;
    }
    uint64_t mask = op == I_VSHL ? (0xffull << k) & 0xff : 0xffull >> k;
    vsplat_const(s, FTMP, 0, mask);
    if (op == I_VSHL) x64_vpsll_i(B, 1, F1, x, k);
    else x64_vpsrl_i(B, 1, F1, x, k);
    int d = wr(s, v, F2);
    x64_vpand(B, d, F1, FTMP);
    return d;
  }
  if (op == I_VSHRS && sz == 3) {
    vsplat_const(s, FTMP, 3, 0x8000000000000000ull);
    x64_vpxor(B, F1, x, FTMP);
    x64_vpsrl_i(B, 3, F1, F1, k);
    x64_vpsrl_i(B, 3, FTMP, FTMP, k);
    int d = wr(s, v, F2);
    x64_vpsub(B, 3, d, F1, FTMP);
    return d;
  }
  int d = wr(s, v, F1);
  if (op == I_VSHL) x64_vpsll_i(B, sz, d, x, k);
  else if (op == I_VSHRU) x64_vpsrl_i(B, sz, d, x, k);
  else x64_vpsra_i(B, sz, d, x, k);
  return d;
}

/* The relaxed i8 x i7 dot product, in the reading warmc picked for every
   target -- the deterministic profile's: BOTH operands signed, with adjacent
   products added with wrap-around, as NEON's smull/smull2/addp does.
   (vpmaddubsw, the usual x86 lowering, is the spec's other choice: its
   first operand read unsigned and the sum saturated.)  Sign-extending to
   i16 and pair-adding into i32 cannot overflow, so each pair sum is exact
   there, and keeping its low half is the wrap-around: masked to 16 bits a
   sum fits u16, so the unsigned-saturating pack truncates.  Leaves the i16x8
   result in FTMP and uses F1/F2, so both operands must be dead after it. */
static int simd_dot_i7(Isel *s, int x, int y) {
  x64_vpmovsx(B, 0, FTMP, x);
  x64_vpmovsx(B, 0, F2, y);
  x64_vpmaddwd(B, FTMP, FTMP, F2);
  x64_vpsrldq(B, F2, x, 8);
  x64_vpmovsx(B, 0, F2, F2);
  x64_vpsrldq(B, F1, y, 8);
  x64_vpmovsx(B, 0, F1, F1);
  x64_vpmaddwd(B, F1, F2, F1);
  vsplat_const(s, F2, 2, 0xffff);
  x64_vpand(B, FTMP, FTMP, F2);
  x64_vpand(B, F1, F1, F2);
  x64_vpackusdw(B, FTMP, FTMP, F1);
  return FTMP;
}

/* i32x4 saturating float->int.  The hardware conversion already answers
   INT_MIN for every invalid input, so the sequences only have to repair
   positive overflow, NaN and the unsigned range. */
static int simd_truncsat(Isel *s, Ref v, int x, bool sgn, bool fd) {
  if (!fd) {
    if (sgn) {
      x64_vcmpp(B, 0, FTMP, x, x, XCMP_EQ_OQ);    /* ordered: not NaN */
      x64_vandps(B, F1, x, FTMP);                 /* NaN -> 0 */
      x64_vcvttps2dq(B, F2, F1);
      vsplat_const(s, FTMP, 2, 0x4f000000);       /* 2^31 */
      x64_vcmpp(B, 0, FTMP, F1, FTMP, XCMP_GE_OS);
      int d = wr(s, v, F1);
      x64_vpxor(B, d, F2, FTMP);                  /* INT_MIN -> INT_MAX */
      return d;
    }
    simd_zero(s, F2);
    x64_vmaxp(B, 0, F2, x, F2);                   /* negatives and NaN -> 0 */
    vsplat_const(s, FTMP, 2, 0x4f000000);
    x64_vsubp(B, 0, F1, F2, FTMP);                /* value - 2^31 */
    x64_vcmpp(B, 0, FTMP, FTMP, F1, XCMP_LE_OS);  /* value >= 2^32 */
    x64_vcvttps2dq(B, F1, F1);
    x64_vpxor(B, F1, F1, FTMP);                   /* saturate the top */
    simd_zero(s, FTMP);
    x64_vpmaxs(B, 2, F1, F1, FTMP);               /* drop the low half's sign */
    x64_vcvttps2dq(B, FTMP, F2);                  /* INT_MIN carries the 2^31 */
    int d = wr(s, v, F2);
    x64_vpadd(B, 2, d, FTMP, F1);
    return d;
  }
  if (sgn) {
    x64_vcmpp(B, 1, FTMP, x, x, XCMP_EQ_OQ);
    vsplat_const(s, F1, 3, f64bits_(2147483647.0));
    x64_vandps(B, FTMP, FTMP, F1);                /* the bound, or 0 on NaN */
    x64_vminp(B, 1, F1, x, FTMP);                 /* NaN takes the second */
    int d = wr(s, v, F2);
    x64_vcvttpd2dq(B, d, F1);
    return d;
  }
  simd_zero(s, FTMP);
  x64_vmaxp(B, 1, F1, x, FTMP);
  vsplat_const(s, FTMP, 3, f64bits_(4294967295.0));
  x64_vminp(B, 1, F1, F1, FTMP);
  x64_vroundp(B, 1, F1, F1, XRND_TRUNC);
  vsplat_const(s, FTMP, 3, f64bits_(4503599627370496.0));  /* 2^52 */
  x64_vaddp(B, 1, F1, F1, FTMP);                  /* integer into the mantissa */
  simd_zero(s, FTMP);
  int d = wr(s, v, F2);
  x64_vshufps(B, d, F1, FTMP, 0x88);
  return d;
}

static int simd_convert(Isel *s, Ref v, int x, bool sgn, bool fd) {
  if (!fd) {
    if (sgn) { int d = wr(s, v, F1); x64_vcvtdq2ps(B, d, x); return d; }
    /* both 16-bit halves convert exactly, so the single rounding at the
       final add is the correctly-rounded u32 value */
    vsplat_const(s, FTMP, 2, 0xffff);
    x64_vpand(B, F1, x, FTMP);
    x64_vpsrl_i(B, 2, F2, x, 16);
    x64_vcvtdq2ps(B, F1, F1);
    x64_vcvtdq2ps(B, F2, F2);
    vsplat_const(s, FTMP, 2, 0x47800000);         /* 65536.0f */
    x64_vmulp(B, 0, F2, F2, FTMP);
    int d = wr(s, v, F1);
    x64_vaddp(B, 0, d, F1, F2);
    return d;
  }
  if (sgn) { int d = wr(s, v, F1); x64_vcvtdq2pd(B, d, x); return d; }
  /* graft each u32 into the mantissa of 2^52 and subtract it back out */
  vconst(s, FTMP, 0x4330000043300000ull, 0x4330000043300000ull);
  x64_vpunpckl(B, 2, F1, x, FTMP);
  vsplat_const(s, FTMP, 3, 0x4330000000000000ull);
  int d = wr(s, v, F2);
  x64_vsubp(B, 1, d, F1, FTMP);
  return d;
}

static int cc_for(uint16_t op) {
  switch (op) {
    case I_EQ: return XCC_E;    case I_NE: return XCC_NE;
    case I_LTS: return XCC_L;   case I_LTU: return XCC_B;
    case I_GTS: return XCC_G;   case I_GTU: return XCC_A;
    case I_LES: return XCC_LE;  case I_LEU: return XCC_BE;
    case I_GES: return XCC_GE;  case I_GEU: return XCC_AE;
    /* Float compares emit vucomis with LT/LE operand-swapped, so the
       above/unordered-false ccs are exact; ==/!= need the parity fixup
       and never reach a bare jcc. */
    case I_FLT: case I_FGT: return XCC_A;
    case I_FLE: case I_FGE: return XCC_AE;
    case I_FEQ: return XCC_E;
    case I_FNE: return XCC_NE;
  }
  return XCC_E;
}

static uint32_t global_off(uint32_t i) {
  return VMCTX_GLOBALS + GLOBAL_SLOT_SIZE * i;
}

static bool is_int_cmp(uint16_t op) { return op >= I_EQ && op <= I_GEU; }
static bool is_flt_cmp(uint16_t op) { return op >= I_FEQ && op <= I_FGE; }
static bool is_feqne(uint16_t op) { return op == I_FEQ || op == I_FNE; }

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
    *sf = is64(f, a[0]);
    return cmp_operand_stable(s, ca[0]) &&
           (f->insts[ca[1]].op == I_CONST || cmp_operand_stable(s, ca[1]));
  }

  if (in->op == I_EQZ) {
    *kind = 2; *ra = a[0]; *imm = 0;
    *sf = is64(f, a[0]);
    return cmp_operand_stable(s, a[0]);
  }
  if (is_flt_cmp(in->op)) {
    if (is_feqne(in->op)) return false;   /* parity sequence, never cached */
    bool swap = in->op == I_FLT || in->op == I_FLE;
    *kind = 3;
    *ra = swap ? a[1] : a[0];
    *rb = swap ? a[0] : a[1];
    *sf = is64(f, a[0]);
    return cmp_operand_stable(s, a[0]) && cmp_operand_stable(s, a[1]);
  }
  *sf = is64(f, a[0]);
  if (is_imm_at(s, v, 1, a[1])) {
    uint64_t c = imm_of(s, a[1]);
    *kind = 2; *ra = a[0]; *imm = *sf ? c : (c & 0xffffffffu);
    return cmp_operand_stable(s, a[0]);
  }
  *kind = 5; *ra = a[0]; *rb = a[1];
  return cmp_operand_stable(s, a[0]) && cmp_operand_stable(s, a[1]);
}

static void emit_compare_raw(Isel *s, Ref v);

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
    int csf = is64(f, a[0]);
    int x = rd(s, ca[0], S0);
    if (f->insts[ca[1]].op == I_CONST) {
      uint64_t c = csf ? f->insts[ca[1]].imm : (f->insts[ca[1]].imm & 0xffffffffu);
      x64_test_ri(B, csf, x, (int32_t)c);
    } else {
      x64_test_rr(B, csf, x, rd(s, ca[1], S1));
    }
    return;
  }
  if (in->op == I_EQZ) {
    int x = rd(s, a[0], S0);
    x64_test_rr(B, is64(f, a[0]), x, x);
    return;
  }
  if (is_flt_cmp(in->op)) {
    bool swap = in->op == I_FLT || in->op == I_FLE;
    int x = rd(s, a[swap ? 1 : 0], F0), y = rd(s, a[swap ? 0 : 1], F1);
    x64_vucomis(B, is64(f, a[0]), x, y);
    return;
  }
  int sf = is64(f, a[0]);
  int x = rd(s, a[0], S0);
  if (is_imm_at(s, v, 1, a[1])) {
    uint64_t c = imm_of(s, a[1]);
    uint64_t m = sf ? c : (c & 0xffffffffu);
    if (m == 0) x64_test_rr(B, sf, x, x);
    else x64_cmp_ri(B, sf, x, (int32_t)m);
    return;
  }
  int y = rd(s, a[1], S1);
  x64_cmp_rr(B, sf, x, y);
}

static int cond_of(Isel *s, Ref v) {
  Inst *in = &s->f->insts[v];
  if (in->op == I_EQZ) return XCC_E;
  return cc_for(in->op);
}

static bool is_condition(Isel *s, Ref v) {
  Inst *in = &s->f->insts[v];
  return in->op == I_EQZ || is_int_cmp(in->op) || is_flt_cmp(in->op);
}

/* Materialize the 0/1 result of a compare whose flags emit_compare just
   set.  Float ==/!= fold the parity flag in; the AND/OR bumps the flags
   epoch, so no stale reuse survives it. */
static void emit_setcc(Isel *s, Ref v, int d) {
  uint16_t op = s->f->insts[v].op;
  if (is_feqne(op)) {
    x64_setcc(B, op == I_FEQ ? XCC_E : XCC_NE, d);
    x64_setcc(B, op == I_FEQ ? XCC_NP : XCC_P, TMP0);
    if (op == I_FEQ) x64_and_rr(B, 0, d, TMP0);
    else x64_or_rr(B, 0, d, TMP0);
    x64_movzx8(B, 0, d, d);
    return;
  }
  x64_setcc(B, cond_of(s, v), d);
  x64_movzx8(B, 0, d, d);
}

/* ---- div / rem ------------------------------------------------------- */

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
            if (ca[0] == d && fallthru) return true;
            break;
          case I_EQ:
            if (fallthru && ((ca[0] == d && is_const_zero(f, ca[1])) ||
                             (ca[1] == d && is_const_zero(f, ca[0]))))
              return true;
            break;
          case I_NE:
            if (taken && ((ca[0] == d && is_const_zero(f, ca[1])) ||
                          (ca[1] == d && is_const_zero(f, ca[0]))))
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
  int w = is64(f, v);
  int x = rd(s, a[0], TMP0);
  if (x != X_RAX) x64_mov_rr(B, 1, X_RAX, x);
  int y = rd(s, a[1], S0);

  Inst *dv = &f->insts[a[1]];
  uint64_t all = w ? ~0ull : 0xffffffffu;
  uint64_t dc = dv->imm & all;
  bool known = dv->op == I_CONST;
  if (!(known ? dc != 0 : divisor_nonzero(f, a[1])) &&
      !div_guarded_earlier(s, v, false) &&
      !nonzero_by_dominating_branch(f, s->cur_block, a[1]))
    trap_if_zero(s, w, y, TRAP_DIV_ZERO);

  bool need_m1 = sgn && !(known && dc != all) && !div_guarded_earlier(s, v, true);
  if (sgn && rem) {
    /* idiv faults on INT_MIN / -1; any x rem -1 is 0, so a -1 divisor
       skips the divide entirely. */
    size_t Ldo = 0, Ldone = 0;
    if (need_m1) {
      x64_cmp_ri(B, w, y, -1);
      x64_jcc_rel32(B, XCC_NE);
      Ldo = B->len;
      x64_xor_rr(B, 0, X_RDX, X_RDX);
      x64_jmp_rel32(B);
      Ldone = B->len;
      br_patch(s, Ldo, B->len);
    }
    x64_cdq_cqo(B, w);
    x64_idiv_r(B, w, y);
    if (need_m1) br_patch(s, Ldone, B->len);
    commit(s, v, X_RDX);
    return;
  }
  if (sgn) {
    if (need_m1) {
      x64_cmp_ri(B, w, y, -1);
      x64_jcc_rel32(B, XCC_NE);
      size_t Lok = B->len;
      if (w) {
        x64_mov_ri(B, 1, TMP1, 0x8000000000000000ull);
        x64_cmp_rr(B, 1, X_RAX, TMP1);
      } else {
        x64_cmp_ri(B, 0, X_RAX, (int32_t)0x80000000);
      }
      trap_if_cc(s, XCC_E, TRAP_DIV_OVERFLOW);
      br_patch(s, Lok, B->len);
    }
    x64_cdq_cqo(B, w);
    x64_idiv_r(B, w, y);
  } else {
    x64_xor_rr(B, 0, X_RDX, X_RDX);
    x64_div_r(B, w, y);
  }
  commit(s, v, rem ? X_RDX : X_RAX);
}

/* ---- checked / saturating float-to-int ------------------------------- */

static void trunc_bounds(ValType from, int sf, bool sgn, uint64_t *lo,
                         int *lo_cc, uint64_t *hi) {
  if (!sf && sgn) {
    if (from == VT_F32) { *lo = f32bits_(-2147483648.0f); *lo_cc = XCC_B; *hi = f32bits_(2147483648.0f); }
    else { *lo = f64bits_(-2147483649.0); *lo_cc = XCC_BE; *hi = f64bits_(2147483648.0); }
  } else if (!sf) {
    if (from == VT_F32) { *lo = f32bits_(-1.0f); *lo_cc = XCC_BE; *hi = f32bits_(4294967296.0f); }
    else { *lo = f64bits_(-1.0); *lo_cc = XCC_BE; *hi = f64bits_(4294967296.0); }
  } else if (sgn) {
    if (from == VT_F32) { *lo = f32bits_(-9223372036854775808.0f); *lo_cc = XCC_B; *hi = f32bits_(9223372036854775808.0f); }
    else { *lo = f64bits_(-9223372036854775808.0); *lo_cc = XCC_B; *hi = f64bits_(9223372036854775808.0); }
  } else {
    if (from == VT_F32) { *lo = f32bits_(-1.0f); *lo_cc = XCC_BE; *hi = f32bits_(18446744073709551616.0f); }
    else { *lo = f64bits_(-1.0); *lo_cc = XCC_BE; *hi = f64bits_(18446744073709551616.0); }
  }
}

/* d = (u64)x for x in [0, 2^64): direct when x < 2^63, else convert
   x - 2^63 and put the top bit back. */
static void emit_u64_of_float(Isel *s, int fw, int d, int x) {
  load_fconst(s, fw, FTMP, fw ? f64bits_(9223372036854775808.0)
                              : f32bits_(9223372036854775808.0f));
  x64_vucomis(B, fw, x, FTMP);
  x64_jcc_rel32(B, XCC_AE);
  size_t Lbig = B->len;
  x64_vcvtts2si(B, fw, 1, d, x);
  x64_jmp_rel32(B);
  size_t Ldone = B->len;
  br_patch(s, Lbig, B->len);
  x64_vsubs(B, fw, F1, x, FTMP);
  x64_vcvtts2si(B, fw, 1, d, F1);
  x64_mov_ri(B, 1, TMP1, 0x8000000000000000ull);
  x64_add_rr(B, 1, d, TMP1);
  br_patch(s, Ldone, B->len);
}

static void emit_trunc_checked(Isel *s, Ref v, bool sgn) {
  IrFunc *f = s->f;
  Ref *a = ir_args(f, v);
  ValType from = (ValType)f->insts[v].imm;
  int fw = from == VT_F64;
  int sf = is64(f, v);
  int x = rd(s, a[0], F0);

  uint64_t lo, hi;
  int lo_cc;
  trunc_bounds(from, sf, sgn, &lo, &lo_cc, &hi);
  /* An unordered ucomis sets CF and ZF, so the below/below-equal first
     check also catches NaN. */
  load_fconst(s, fw, FTMP, lo);
  x64_vucomis(B, fw, x, FTMP);
  trap_if_cc(s, lo_cc, TRAP_BAD_CONVERSION);
  load_fconst(s, fw, FTMP, hi);
  x64_vucomis(B, fw, x, FTMP);
  trap_if_cc(s, XCC_AE, TRAP_BAD_CONVERSION);

  int d = wr(s, v, S0);
  if (!sgn && sf) {
    emit_u64_of_float(s, fw, d, x);
  } else if (!sgn) {
    /* u32: convert through i64; the checked range keeps it exact and the
       result's upper 32 bits zero. */
    x64_vcvtts2si(B, fw, 1, d, x);
  } else {
    x64_vcvtts2si(B, fw, sf, d, x);
  }
  commit(s, v, d);
}

static void emit_trunc_sat(Isel *s, Ref v, bool sgn) {
  IrFunc *f = s->f;
  Ref *a = ir_args(f, v);
  ValType from = (ValType)f->insts[v].imm;
  int fw = from == VT_F64;
  int sf = is64(f, v);
  int x = rd(s, a[0], F0);
  int d = wr(s, v, S0);
  size_t ends[3];
  int nend = 0;

  x64_vucomis(B, fw, x, x);
  x64_jcc_rel32(B, XCC_P);
  size_t Lnan = B->len;

  if (sgn) {
    x64_vcvtts2si(B, fw, sf, d, x);
    /* the INDEFINITE sentinel is also the correct answer for exactly
       INT_MIN and for negative overflow; only positive overflow needs
       the fixup */
    if (sf) {
      x64_mov_ri(B, 1, TMP1, 0x8000000000000000ull);
      x64_cmp_rr(B, 1, d, TMP1);
    } else {
      x64_cmp_ri(B, 0, d, (int32_t)0x80000000);
    }
    x64_jcc_rel32(B, XCC_NE);
    ends[nend++] = B->len;
    x64_vxorps(B, FTMP, FTMP, FTMP);
    x64_vucomis(B, fw, x, FTMP);
    x64_jcc_rel32(B, XCC_BE);
    ends[nend++] = B->len;
    x64_mov_ri(B, sf, d, sf ? 0x7fffffffffffffffull : 0x7fffffffull);
  } else {
    uint64_t hi = sf ? (fw ? f64bits_(18446744073709551616.0)
                           : f32bits_(18446744073709551616.0f))
                     : (fw ? f64bits_(4294967296.0)
                           : f32bits_(4294967296.0f));
    load_fconst(s, fw, FTMP, hi);
    x64_vucomis(B, fw, x, FTMP);
    x64_jcc_rel32(B, XCC_AE);
    size_t Lmax = B->len;
    x64_vxorps(B, FTMP, FTMP, FTMP);
    x64_vucomis(B, fw, x, FTMP);
    x64_jcc_rel32(B, XCC_BE);
    size_t Lzero = B->len;
    if (sf) emit_u64_of_float(s, fw, d, x);
    else x64_vcvtts2si(B, fw, 1, d, x);
    x64_jmp_rel32(B);
    ends[nend++] = B->len;
    br_patch(s, Lmax, B->len);
    x64_mov_ri(B, sf, d, sf ? ~0ull : 0xffffffffull);
    x64_jmp_rel32(B);
    ends[nend++] = B->len;
    br_patch(s, Lzero, B->len);
    x64_xor_rr(B, 0, d, d);
    x64_jmp_rel32(B);
    ends[nend++] = B->len;
    br_patch(s, Lnan, B->len);
    x64_xor_rr(B, 0, d, d);
    for (int e = 0; e < nend; e++) br_patch(s, ends[e], B->len);
    commit(s, v, d);
    return;
  }
  x64_jmp_rel32(B);
  ends[nend++] = B->len;
  br_patch(s, Lnan, B->len);
  x64_xor_rr(B, 0, d, d);
  for (int e = 0; e < nend; e++) br_patch(s, ends[e], B->len);
  commit(s, v, d);
}

/* ---- the deterministic profile's NaN rule ---------------------------- */

/* The profile pins every NaN a floating-point instruction *generates* to
   the canonical one.  a64 gets that free from FPCR.DN, set once at
   instantiation; x86-64 has no such mode -- SSE hands back an operand's
   payload where one was NaN, and invents a NEGATIVE quiet NaN of its own
   where none was -- so the check has to be emitted after each instruction
   the rule covers.

   The pattern comes out of the test rather than the constant pool.  An
   unordered compare answers all-ones in exactly the lanes that need
   replacing, and all-ones shifted up past the mantissa and then down one
   place IS the canonical NaN: 0xff800000 >> 1 = 0x7fc00000, and
   0xfff0000000000000 >> 1 = 0x7ff8000000000000.  So five instructions and
   one scratch, with nothing to load:

     vcmpunordp FTMP, d, d    all-ones in the NaN lanes
     vandnp     d, FTMP, d    the ordered lanes; the NaN lanes cleared
     vpsll      FTMP, m       the exponent and the quiet bit, where NaN
     vpsrl      FTMP, 1       ... which is the canonical NaN, sign clear
     vorp       d, d, FTMP

   Scalar values go through the packed forms: only lane 0 is ever read back,
   and the lanes above it already held whatever the arithmetic left there.
   Flags are untouched, so this may sit inside a compare's live range. */
static void canon_nan(Isel *s, int d, int dom) {
  if (!s->opt->det_profile) return;
  int sz = dom ? 3 : 2;
  x64_vcmpp(B, dom, FTMP, d, d, XCMP_UNORD_Q);
  x64_vandnps(B, d, FTMP, d);
  x64_vpsll_i(B, sz, FTMP, FTMP, dom ? 52 : 23);
  x64_vpsrl_i(B, sz, FTMP, FTMP, 1);
  x64_vorps(B, d, d, FTMP);
}

/* ---- float min/max/copysign/abs/neg ---------------------------------- */

static void emit_fminmax(Isel *s, Ref v, bool is_min) {
  IrFunc *f = s->f;
  Ref *a = ir_args(f, v);
  int w = is64(f, v);
  int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
  int d = wr(s, v, F2);
  x64_vucomis(B, w, x, y);
  x64_jcc_rel32(B, XCC_NE);
  size_t Lgo = B->len;
  x64_jcc_rel32(B, XCC_P);
  size_t Lnan = B->len;
  /* ordered equal: merge the signs so min(+0,-0) = -0 and max = +0 */
  if (is_min) x64_vorps(B, d, x, y);
  else x64_vandps(B, d, x, y);
  x64_jmp_rel32(B);
  size_t Ldone = B->len;
  br_patch(s, Lnan, B->len);
  x64_vadds(B, w, d, x, y);   /* quiet NaN out */
  x64_jmp_rel32(B);
  size_t Ldone2 = B->len;
  br_patch(s, Lgo, B->len);
  if (is_min) x64_vmins(B, w, d, x, y);
  else x64_vmaxs(B, w, d, x, y);
  br_patch(s, Ldone, B->len);
  br_patch(s, Ldone2, B->len);
  canon_nan(s, d, w);
  commit(s, v, d);
}

static void emit_copysign(Isel *s, Ref v) {
  IrFunc *f = s->f;
  Ref *a = ir_args(f, v);
  int w = is64(f, v);
  int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
  x64_movd_xg(B, w, S0, x);
  x64_movd_xg(B, w, S1, y);
  if (w) {
    x64_mov_ri(B, 1, TMP0, 0x7fffffffffffffffull);
    x64_and_rr(B, 1, S0, TMP0);
    x64_not(B, 1, TMP0);
    x64_and_rr(B, 1, S1, TMP0);
  } else {
    x64_and_ri(B, 0, S0, 0x7fffffff);
    x64_and_ri(B, 0, S1, (int32_t)0x80000000);
  }
  x64_or_rr(B, w, S0, S1);
  int d = wr(s, v, F2);
  x64_movd_gx(B, w, d, S0);
  commit(s, v, d);
}

static CompType *call_sig(Isel *s, Inst *in) {
  Module *m = s->f->m;
  if (in->op == I_CALL) return &m->types[m->funcs[in->imm].type_index];
  return &m->types[in->imm];
}

/* ---- one instruction ------------------------------------------------- */

static void emit_inst(Isel *s, Ref v) {
  IrFunc *f = s->f;
  Inst *in = &f->insts[v];
  s->cur_inst = v;
  Ref *a = ir_args(f, v);
  int wide = is64(f, v);
  int sf = wide;

  if (s->loc[v] == LOC_NONE &&
      (is_imm(s, v) || in->op == I_CONST || in->op == I_MEM_BASE ||
       in->op == I_GLOBAL_GET ||
       (s->fused && s->fused[v]) || (s->folded && s->folded[v]))) return;

  switch (in->op) {
    case I_NOP: case I_PARAM: break;

    case I_CONST: {
      if (s->loc[v] == LOC_NONE) break;
      if ((ValType)in->type == VT_V128) {
        int d = wr(s, v, F0);
        load_vconst(s, d, in->imm, in->imm2);
        commit(s, v, d);
        break;
      }
      if (vt_is_float((ValType)in->type)) {
        int d = wr(s, v, F0);
        load_fconst(s, wide, d, in->imm);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        x64_mov_ri(B, sf, d, in->imm);
        commit(s, v, d);
      }
      break;
    }

    case I_ADD: case I_SUB: {
      int x = rd(s, a[0], S0);
      if (is_imm_at(s, v, 1, a[1])) {
        int32_t c = (int32_t)imm_of(s, a[1]);
        int d = rmw2(s, v, x, -1, S1);
        if (in->op == I_ADD) x64_add_ri(B, sf, d, c);
        else x64_sub_ri(B, sf, d, c);
        commit(s, v, d);
        break;
      }
      int y = rd(s, a[1], S1);
      int d = rmw2(s, v, x, y, TMP1);
      if (in->op == I_ADD) x64_add_rr(B, sf, d, y);
      else x64_sub_rr(B, sf, d, y);
      commit(s, v, d);
      break;
    }
    case I_AND: case I_OR: case I_XOR: {
      int x = rd(s, a[0], S0);
      if (is_imm_at(s, v, 1, a[1])) {
        uint64_t m = sf ? imm_of(s, a[1]) : (imm_of(s, a[1]) & 0xffffffffu);
        int d = rmw2(s, v, x, -1, S1);
        if (in->op == I_XOR && m == (sf ? ~0ull : 0xffffffffu)) {
          x64_not(B, sf, d);
        } else if (in->op == I_AND) x64_and_ri(B, sf, d, (int32_t)m);
        else if (in->op == I_OR) x64_or_ri(B, sf, d, (int32_t)m);
        else x64_xor_ri(B, sf, d, (int32_t)m);
        commit(s, v, d);
        break;
      }
      int y = rd(s, a[1], S1);
      int d = rmw2(s, v, x, y, TMP1);
      if (in->op == I_AND) x64_and_rr(B, sf, d, y);
      else if (in->op == I_OR) x64_or_rr(B, sf, d, y);
      else x64_xor_rr(B, sf, d, y);
      commit(s, v, d);
      break;
    }
    case I_MUL: {
      int x = rd(s, a[0], S0);
      if (is_imm_at(s, v, 1, a[1])) {
        int d = wr(s, v, S1);
        x64_imul_rri(B, sf, d, x, (int32_t)imm_of(s, a[1]));
        commit(s, v, d);
        break;
      }
      int y = rd(s, a[1], S1);
      int d = rmw2(s, v, x, y, TMP1);
      x64_imul_rr(B, sf, d, y);
      commit(s, v, d);
      break;
    }
    case I_MULHU: {
      int x = rd(s, a[0], S0), y = rd(s, a[1], S1);
      int src = y;
      if (x != X_RDX) {
        if (y == X_RDX) src = x;
        else x64_mov_rr(B, 1, X_RDX, x);
      }
      int d = wr(s, v, S0);
      x64_mulx(B, sf, d, TMP0, src);
      commit(s, v, d);
      break;
    }
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTR: case I_ROTL: {
      int width = sf ? 64 : 32;
      int x = rd(s, a[0], S0);
      if (is_imm_at(s, v, 1, a[1])) {
        int sh = (int)(imm_of(s, a[1]) % (uint64_t)width);
        if (in->op == I_ROTR || in->op == I_ROTL) {
          int r = (in->op == I_ROTR) ? sh : (width - sh) % width;
          int d = wr(s, v, S1);
          if (r) x64_rorx(B, sf, d, x, r);
          else if (d != x) x64_mov_rr(B, 1, d, x);
          commit(s, v, d);
          break;
        }
        int d = rmw2(s, v, x, -1, S1);
        if (sh) {
          if (in->op == I_SHL) x64_shl_ri(B, sf, d, sh);
          else if (in->op == I_SHRS) x64_sar_ri(B, sf, d, sh);
          else x64_shr_ri(B, sf, d, sh);
        } else if (!sf && d == x) {
          x64_mov_rr(B, 0, d, d);   /* i32 shift by 0 still renormalizes */
        }
        commit(s, v, d);
        break;
      }
      int y = rd(s, a[1], S1);
      if (in->op == I_ROTR || in->op == I_ROTL) {
        /* rot(x, n) = shift one way by n, the other by width-n (both
           counts auto-masked), or the halves together. */
        x64_mov_rr(B, 0, TMP1, y);
        if (in->op == I_ROTL) x64_shlx(B, sf, TMP0, x, TMP1);
        else x64_shrx(B, sf, TMP0, x, TMP1);
        x64_neg(B, sf, TMP1);
        int d = wr(s, v, S1);
        if (in->op == I_ROTL) x64_shrx(B, sf, d, x, TMP1);
        else x64_shlx(B, sf, d, x, TMP1);
        x64_or_rr(B, sf, d, TMP0);
        commit(s, v, d);
        break;
      }
      int d = wr(s, v, TMP1);
      if (in->op == I_SHL) x64_shlx(B, sf, d, x, y);
      else if (in->op == I_SHRS) x64_sarx(B, sf, d, x, y);
      else x64_shrx(B, sf, d, x, y);
      commit(s, v, d);
      break;
    }
    case I_DIVS: emit_div(s, v, true, false); break;
    case I_DIVU: emit_div(s, v, false, false); break;
    case I_REMS: emit_div(s, v, true, true); break;
    case I_REMU: emit_div(s, v, false, true); break;

    case I_CLZ: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                  x64_lzcnt(B, sf, d, x); commit(s, v, d); break; }
    case I_CTZ: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                  x64_tzcnt(B, sf, d, x); commit(s, v, d); break; }
    case I_POPCNT: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                     x64_popcnt(B, sf, d, x); commit(s, v, d); break; }
    case I_BSWAP: { int x = rd(s, a[0], S0);
                    int d = rmw2(s, v, x, -1, S1);
                    x64_bswap(B, sf, d); commit(s, v, d); break; }

    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE: {
      emit_compare(s, v);
      int d = wr(s, v, S0);
      if (is_feqne(in->op)) {
        emit_setcc(s, v, d);
        commit(s, v, d);
        break;
      }
      int cc = cond_of(s, v);
      x64_setcc(B, cc, d);
      x64_movzx8(B, 0, d, d);
      commit(s, v, d);
      s->cmp_ref = v; s->cmp_cc = cc; s->cmp_epoch = B->flags_epoch;
      break;
    }

#define FBIN(op, fn)                                                           \
    case op: {                                                                 \
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);                            \
      int d = wr(s, v, F2);                                                    \
      fn(B, wide, d, x, y);                                                    \
      canon_nan(s, d, wide);                                                   \
      commit(s, v, d);                                                         \
      break;                                                                   \
    }
    FBIN(I_FADD, x64_vadds)
    FBIN(I_FSUB, x64_vsubs)
    FBIN(I_FMUL, x64_vmuls)
    FBIN(I_FDIV, x64_vdivs)
#undef FBIN
    case I_FMIN: emit_fminmax(s, v, true); break;
    case I_FMAX: emit_fminmax(s, v, false); break;
    case I_FCOPYSIGN: emit_copysign(s, v); break;

    case I_FABS: case I_FNEG: {
      int x = rd(s, a[0], F0);
      x64_movd_xg(B, wide, S0, x);
      if (wide) {
        x64_mov_ri(B, 1, TMP0, in->op == I_FNEG ? 0x8000000000000000ull
                                                : 0x7fffffffffffffffull);
        if (in->op == I_FNEG) x64_xor_rr(B, 1, S0, TMP0);
        else x64_and_rr(B, 1, S0, TMP0);
      } else {
        if (in->op == I_FNEG) x64_xor_ri(B, 0, S0, (int32_t)0x80000000);
        else x64_and_ri(B, 0, S0, 0x7fffffff);
      }
      int d = wr(s, v, F1);
      x64_movd_gx(B, wide, d, S0);
      commit(s, v, d);
      break;
    }
    case I_FSQRT: {
      int x = rd(s, a[0], F0);
      int d = wr(s, v, F1);
      x64_vsqrts(B, wide, d, x, x);
      canon_nan(s, d, wide);
      commit(s, v, d);
      break;
    }
    case I_FCEIL: case I_FFLOOR: case I_FTRUNC: case I_FNEAREST: {
      /* roundss imm: 8|mode, bit 3 suppressing precision exceptions */
      static const int mode[] = { 10, 9, 11, 8 };
      int x = rd(s, a[0], F0);
      int d = wr(s, v, F1);
      x64_vrounds(B, wide, d, x, x, mode[in->op - I_FCEIL]);
      canon_nan(s, d, wide);
      commit(s, v, d);
      break;
    }

    case I_WRAP: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                   x64_mov_rr(B, 0, d, x); commit(s, v, d); break; }
    case I_EXTENDS: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                      x64_movsxd(B, d, x); commit(s, v, d); break; }
    case I_EXTENDU: {
      if (s->zx_fuse && s->zx_fuse[v]) break;
      int x = rd(s, a[0], S0); int d = wr(s, v, S0);
      x64_mov_rr(B, 0, d, x); commit(s, v, d); break; }
    case I_EXT8S: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                    x64_movsx8(B, sf, d, x); commit(s, v, d); break; }
    case I_EXT16S: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                     x64_movsx16(B, sf, d, x); commit(s, v, d); break; }
    case I_EXT32S: { int x = rd(s, a[0], S0); int d = wr(s, v, S0);
                     x64_movsxd(B, d, x); commit(s, v, d); break; }

    case I_TRUNCS: emit_trunc_checked(s, v, true); break;
    case I_TRUNCU: emit_trunc_checked(s, v, false); break;
    case I_TRUNC_SATS: emit_trunc_sat(s, v, true); break;
    case I_TRUNC_SATU: emit_trunc_sat(s, v, false); break;

    case I_CONVERTS: case I_CONVERTU: {
      ValType from = (ValType)in->imm;
      int fsf = vt_is_64(from) ? 1 : 0;
      int x = rd(s, a[0], S0);
      int d = wr(s, v, F0);
      if (in->op == I_CONVERTS) {
        x64_vcvtsi2s(B, wide, fsf, d, d, x);
      } else if (!fsf) {
        /* u32: the upper-32-zero invariant makes a 64-bit signed convert
           exact */
        x64_vcvtsi2s(B, wide, 1, d, d, x);
      } else {
        x64_test_rr(B, 1, x, x);
        x64_jcc_rel32(B, XCC_S);
        size_t Lneg = B->len;
        x64_vcvtsi2s(B, wide, 1, d, d, x);
        x64_jmp_rel32(B);
        size_t Ldone = B->len;
        br_patch(s, Lneg, B->len);
        x64_mov_rr(B, 1, TMP1, x);
        x64_and_ri(B, 1, TMP1, 1);
        x64_mov_rr(B, 1, TMP0, x);
        x64_shr_ri(B, 1, TMP0, 1);
        x64_or_rr(B, 1, TMP0, TMP1);
        x64_vcvtsi2s(B, wide, 1, d, d, TMP0);
        x64_vadds(B, wide, d, d, d);
        br_patch(s, Ldone, B->len);
      }
      commit(s, v, d);
      break;
    }
    case I_DEMOTE: { int x = rd(s, a[0], F0); int d = wr(s, v, F1);
                     x64_vcvtsd2ss(B, d, x, x);
                     canon_nan(s, d, 0); commit(s, v, d); break; }
    case I_PROMOTE: { int x = rd(s, a[0], F0); int d = wr(s, v, F1);
                      x64_vcvtss2sd(B, d, x, x);
                      canon_nan(s, d, 1); commit(s, v, d); break; }
    case I_REINTERPRET: {
      ValType from = (ValType)in->imm;
      if (vt_is_float(from)) {
        int x = rd(s, a[0], F0); int d = wr(s, v, S0);
        x64_movd_xg(B, wide, d, x);
        commit(s, v, d);
      } else {
        int x = rd(s, a[0], S0); int d = wr(s, v, F0);
        x64_movd_gx(B, wide, d, x);
        commit(s, v, d);
      }
      break;
    }

    case I_SELECT: {
      /* v128 selects take the fp arm: vmovdqa moves the whole register and
         leaves the flags alone, which is all the branch below needs */
      int fp = is_fp_value(f, v);
      int cc = XCC_NE;
      bool folded_cond = s->folded && s->folded[a[2]];
      bool flags_live = s->cmp_ref == a[2] && s->cmp_epoch == B->flags_epoch;
      if (folded_cond) {
        emit_compare(s, a[2]);
        uint16_t cop = f->insts[a[2]].op;
        if (is_feqne(cop)) {
          emit_setcc(s, a[2], TMP1);
          x64_test_rr(B, 0, TMP1, TMP1);
          cc = XCC_NE;
        } else {
          cc = cond_of(s, a[2]);
        }
      } else if (flags_live) {
        cc = s->cmp_cc;
      } else {
        int c = rd(s, a[2], TMP1);
        x64_test_rr(B, 0, c, c);
      }
      if (fp) {
        int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
        int d = wr(s, v, F2);
        /* moves leave the flags alone, so a short branch does the pick */
        if (d == y) {
          x64_jcc_rel32(B, xcc_invert(cc));
          size_t Lover = B->len;
          x64_vmovdqa_rr(B, d, x);
          br_patch(s, Lover, B->len);
        } else {
          if (d != x) x64_vmovdqa_rr(B, d, x);
          x64_jcc_rel32(B, cc);
          size_t Lover = B->len;
          x64_vmovdqa_rr(B, d, y);
          br_patch(s, Lover, B->len);
        }
        commit(s, v, d);
        break;
      }
      uint64_t m = sf ? ~0ull : 0xffffffffu;
      bool xc = f->insts[a[0]].op == I_CONST, yc = f->insts[a[1]].op == I_CONST;
      uint64_t xv = f->insts[a[0]].imm & m, yv = f->insts[a[1]].imm & m;
      int d = wr(s, v, S1);
      if (xc && yc && yv == 0 && (xv == 1 || xv == m)) {
        x64_setcc(B, cc, d);
        x64_movzx8(B, 0, d, d);
        if (xv == m) x64_neg(B, sf, d);
      } else if (xc && yc && xv == 0 && (yv == 1 || yv == m)) {
        x64_setcc(B, xcc_invert(cc), d);
        x64_movzx8(B, 0, d, d);
        if (yv == m) x64_neg(B, sf, d);
      } else {
        /* rd of a constant is a flag-preserving mov */
        int x = rd(s, a[0], S0);
        int y = rd(s, a[1], S1);
        d = wr(s, v, TMP1);
        if (d == x) {
          x64_cmovcc(B, sf, xcc_invert(cc), d, y);
        } else {
          if (d != y) x64_mov_rr(B, 1, d, y);
          x64_cmovcc(B, sf, cc, d, x);
        }
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
        int c = rd(s, a[1], TMP1);
        int d = wr(s, v, S1);
        x64_lea(B, 1, d, c, x, 0, 0);
        commit(s, v, d);
        break;
      }
      int d = wr(s, v, S1);
      if (in->imm) {
        if (in->imm <= 0x7fffffffull) {
          x64_lea(B, 1, d, x, X64_NOIDX, 0, (int32_t)in->imm);
        } else {
          x64_mov_ri(B, 1, TMP0, in->imm);
          if (d != x) x64_mov_rr(B, 1, d, x);
          x64_add_rr(B, 1, d, TMP0);
        }
      } else if (is64(f, ax)) {
        if (d != x) x64_mov_rr(B, 1, d, x);
      } else {
        x64_mov_rr(B, 0, d, x);
      }
      commit(s, v, d);
      break;
    }

    case I_MEM_PTR: {
      Ref ax = addr_index(s, f, a[0]);
      int x = rd(s, ax, S0);
      int mb = mem_base_reg(s, in->imm2, TMP1);
      int d = wr(s, v, S1);
      x64_lea(B, 1, d, mb, x, 0, 0);
      commit(s, v, d);
      break;
    }

    case I_MEM_BASE: {
      int d = wr(s, v, S0);
      uint64_t off = in->imm;
      if (in->imm2 != 0 && off == 0) {
        uint32_t disp;
        int at = vmctx_at(s, mem_slot_off(f, in->imm2) + MEM_SLOT_BASE, 3,
                          &disp, TMP0);
        x64_load64(B, d, at, X64_NOIDX, 0, (int32_t)disp);
        commit(s, v, d);
        break;
      }
      int mb = mem_base_reg(s, in->imm2, S1);
      if (off <= 0x7fffffffull) {
        x64_lea(B, 1, d, mb, X64_NOIDX, 0, (int32_t)off);
      } else {
        x64_mov_ri(B, 1, TMP0, off);
        x64_lea(B, 1, d, mb, TMP0, 0, 0);
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
        x64_load64(B, d, at, X64_NOIDX, 0, (int32_t)disp);
        sz = d;
      }
      uint64_t need = in->imm2;
      if (d != sz) x64_mov_rr(B, 1, d, sz);
      if (need <= 0x7fffffffull) {
        x64_sub_ri(B, 1, d, (int32_t)need);
      } else {
        x64_mov_ri(B, 1, TMP0, need);
        x64_sub_rr(B, 1, d, TMP0);
      }
      commit(s, v, d);
      break;
    }

    case I_BOUNDS_CHECK: {
      if (in->nargs == 2) {
        int addr2 = rd(s, a[0], TMP1);
        int lim = rd(s, a[1], TMP0);
        x64_cmp_rr(B, 1, addr2, lim);
        trap_if_cc(s, XCC_A, TRAP_MEM_OOB);
        break;
      }
      int addr = rd(s, a[0], TMP1);
      int limit = REG_MEMSIZE;
      if (in->imm == 0) reload_size(s);
      else {
        uint32_t disp;
        int mb = vmctx_at(s, mem_slot_off(f, in->imm) + MEM_SLOT_LEN, 3, &disp, S1);
        x64_load64(B, S1, mb, X64_NOIDX, 0, (int32_t)disp);
        limit = S1;
      }
      if (f->m->memories[in->imm].index64) {
        x64_mov_ri(B, 1, S0, in->imm2);
        x64_mov_rr(B, 1, TMP0, limit);
        x64_sub_rr(B, 1, TMP0, S0);
        trap_if_cc(s, XCC_B, TRAP_MEM_OOB);
        x64_cmp_rr(B, 1, addr, TMP0);
        trap_if_cc(s, XCC_A, TRAP_MEM_OOB);
      } else {
        if (in->imm2 <= 0x7fffffffull) {
          x64_lea(B, 1, TMP0, addr, X64_NOIDX, 0, (int32_t)in->imm2);
        } else {
          x64_mov_ri(B, 1, TMP0, in->imm2);
          x64_add_rr(B, 1, TMP0, addr);
        }
        x64_cmp_rr(B, 1, TMP0, limit);
        trap_if_cc(s, XCC_A, TRAP_MEM_OOB);
      }
      break;
    }

    case I_LOAD: {
      int size = (int)MEMF_SIZE(in->imm2);
      int sgn = (in->imm2 & MEMF_SIGNED) != 0;
      int fp = vt_is_vec_reg((ValType)in->type);
      /* fp/vec loads take the access size directly: a v128-typed load of
         size < 4 is a narrow (zero-filled) vector load */
      int fsz = size;
      int d = fp ? wr(s, v, F0) : wr(s, v, S0);

      if (s->ptr_fuse && s->ptr_fuse[a[0]]) {
        Ref *ea = ir_args(f, a[0]);
        Ref ax = addr_index(s, f, ea[0]);
        int p = rd(s, ea[1], TMP1);
        int x = rd(s, ax, TMP0);
        if (fp) fload(B, fsz, d, p, x, 0, 0);
        else gload(B, size, sgn, wide, d, p, x, 0, 0);
        commit(s, v, d);
        break;
      }
      if (f->insts[a[0]].op == I_MEM_PTR) {
        int p = rd(s, a[0], TMP1);
        if (in->imm > 0x7fffffffull) {
          x64_mov_ri(B, 1, TMP0, in->imm);
          x64_lea(B, 1, TMP1, p, TMP0, 0, 0);
          p = TMP1;
        }
        int32_t disp = in->imm > 0x7fffffffull ? 0 : (int32_t)in->imm;
        if (fp) fload(B, fsz, d, p, X64_NOIDX, 0, disp);
        else gload(B, size, sgn, wide, d, p, X64_NOIDX, 0, disp);
        commit(s, v, d);
        break;
      }
      int mb = mem_base_reg(s, MEMF_MEM(in->imm2), S1);
      if (is_imm_at(s, v, 0, a[0])) {
        int32_t off = (int32_t)imm_of(s, a[0]);
        if (fp) fload(B, fsz, d, mb, X64_NOIDX, 0, off);
        else gload(B, size, sgn, wide, d, mb, X64_NOIDX, 0, off);
      } else if (s->addr_fuse && s->addr_fuse[a[0]]) {
        int32_t off = (int32_t)f->insts[a[0]].imm;
        Ref aa = addr_index(s, f, ir_args(f, a[0])[0]);
        int x = rd(s, aa, TMP1);
        if (fp) fload(B, fsz, d, mb, x, 0, off);
        else gload(B, size, sgn, wide, d, mb, x, 0, off);
      } else {
        int ea = rd(s, a[0], TMP1);
        if (fp) fload(B, fsz, d, mb, ea, 0, 0);
        else gload(B, size, sgn, wide, d, mb, ea, 0, 0);
      }
      commit(s, v, d);
      break;
    }

    case I_STORE: {
      int size = (int)MEMF_SIZE(in->imm2);
      int fp = is_fp_value(f, a[1]);
      int fsz = size;
      int val = fp ? rd(s, a[1], F0) : rd(s, a[1], S0);

      if (s->ptr_fuse && s->ptr_fuse[a[0]]) {
        Ref *ea = ir_args(f, a[0]);
        Ref ax = addr_index(s, f, ea[0]);
        int p = rd(s, ea[1], TMP1);
        int x = rd(s, ax, TMP0);
        if (fp) fstore(B, fsz, val, p, x, 0, 0);
        else gstore(B, size, val, p, x, 0, 0);
        break;
      }
      if (f->insts[a[0]].op == I_MEM_PTR) {
        int p = rd(s, a[0], TMP1);
        if (in->imm > 0x7fffffffull) {
          x64_mov_ri(B, 1, TMP0, in->imm);
          x64_lea(B, 1, TMP1, p, TMP0, 0, 0);
          p = TMP1;
        }
        int32_t disp = in->imm > 0x7fffffffull ? 0 : (int32_t)in->imm;
        if (fp) fstore(B, fsz, val, p, X64_NOIDX, 0, disp);
        else gstore(B, size, val, p, X64_NOIDX, 0, disp);
        break;
      }
      int mb = mem_base_reg(s, MEMF_MEM(in->imm2), S1);
      if (is_imm_at(s, v, 0, a[0])) {
        int32_t off = (int32_t)imm_of(s, a[0]);
        if (fp) fstore(B, fsz, val, mb, X64_NOIDX, 0, off);
        else gstore(B, size, val, mb, X64_NOIDX, 0, off);
      } else if (s->addr_fuse && s->addr_fuse[a[0]]) {
        int32_t off = (int32_t)f->insts[a[0]].imm;
        Ref aa = addr_index(s, f, ir_args(f, a[0])[0]);
        int x = rd(s, aa, TMP1);
        if (fp) fstore(B, fsz, val, mb, x, 0, off);
        else gstore(B, size, val, mb, x, 0, off);
      } else {
        int ea = rd(s, a[0], TMP1);
        if (fp) fstore(B, fsz, val, mb, ea, 0, 0);
        else gstore(B, size, val, mb, ea, 0, 0);
      }
      break;
    }

    case I_VLOAD: {
      int p = rd(s, a[0], TMP1);
      int32_t disp = vec_disp(s, &p, in->imm);
      int d = wr(s, v, F0);
      x64_vmovdqu_load(B, d, p, X64_NOIDX, 0, disp);
      commit(s, v, d);
      break;
    }

    case I_VSTORE: {
      int val = rd(s, a[1], F0);
      int p = rd(s, a[0], TMP1);
      int32_t disp = vec_disp(s, &p, in->imm);
      x64_vmovdqu_store(B, val, p, X64_NOIDX, 0, disp);
      break;
    }

    /* Two-operand SIMD.  A one-instruction lowering writes wr()'s register
       directly -- VEX is three-operand, so a destination aliasing a source
       is harmless as long as it is written by the final instruction.
       Anything longer computes in F2/FTMP, which rd() never hands out, and
       commits at the end.  Once x and y have had their last read their
       scratches are free too (and if an operand lived in a register of its
       own, its scratch was never used), so F0/F1 serve as extra temporaries
       from that point on. */
    case I_VADD: case I_VSUB: case I_VMUL:
    case I_VAND: case I_VOR: case I_VXOR: case I_VANDNOT:
    case I_VSMAX: case I_VSMIN: case I_VUMAX: case I_VUMIN:
    case I_VUZP1: case I_VUZP2: case I_VZIP1: case I_VZIP2: case I_VCMP:
    case I_VFADD: case I_VFSUB: case I_VFMUL: case I_VFDIV:
    case I_VFMIN: case I_VFMAX: case I_VFPMIN: case I_VFPMAX:
    case I_VADDSATS: case I_VADDSATU: case I_VSUBSATS: case I_VSUBSATU:
    case I_VAVGRU: case I_VQ15MULR: case I_VEXTMUL: case I_VSWIZZLE:
    case I_VNARROW: case I_VDOT: case I_VDOTI7: {
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
      int sz = VLANE_SIZE(in->imm2);
      int fd = (in->imm2 == VLANE_F64);
      int res = -1;
      switch (in->op) {
        case I_VADD: res = wr(s, v, F2); x64_vpadd(B, sz, res, x, y); break;
        case I_VSUB: res = wr(s, v, F2); x64_vpsub(B, sz, res, x, y); break;
        case I_VAND: res = wr(s, v, F2); x64_vpand(B, res, x, y); break;
        case I_VOR:  res = wr(s, v, F2); x64_vpor(B, res, x, y); break;
        case I_VXOR: res = wr(s, v, F2); x64_vpxor(B, res, x, y); break;
        /* ANDNOT is x & ~y; vpandn negates its FIRST source */
        case I_VANDNOT: res = wr(s, v, F2); x64_vpandn(B, res, y, x); break;

        case I_VMUL:
          if (sz == 1) { res = wr(s, v, F2); x64_vpmullw(B, res, x, y); }
          else if (sz == 2) { res = wr(s, v, F2); x64_vpmulld(B, res, x, y); }
          else if (sz == 0) {
            /* the low byte of a word product is the byte product, so the
               even lanes come out of one vpmullw and the odd lanes out of
               another on the shifted-down halves */
            x64_vpsrl_i(B, 1, FTMP, x, 8);
            x64_vpsrl_i(B, 1, F2, y, 8);
            x64_vpmullw(B, FTMP, FTMP, F2);
            x64_vpsll_i(B, 1, FTMP, FTMP, 8);
            x64_vpmullw(B, F2, x, y);
            x64_vpsll_i(B, 1, F2, F2, 8);
            x64_vpsrl_i(B, 1, F2, F2, 8);
            x64_vpor(B, FTMP, FTMP, F2);
            res = FTMP;
          } else {
            /* 64-bit lanes: al*bl + ((ah*bl + bh*al) << 32) */
            x64_vpsrl_i(B, 3, FTMP, x, 32);
            x64_vpmuludq(B, FTMP, FTMP, y);
            x64_vpsrl_i(B, 3, F2, y, 32);
            x64_vpmuludq(B, F2, F2, x);
            x64_vpadd(B, 3, FTMP, FTMP, F2);
            x64_vpsll_i(B, 3, FTMP, FTMP, 32);
            x64_vpmuludq(B, F2, x, y);
            x64_vpadd(B, 3, FTMP, FTMP, F2);
            res = FTMP;
          }
          break;

        case I_VSMAX: case I_VSMIN: case I_VUMAX: case I_VUMIN: {
          bool sgn = in->op == I_VSMAX || in->op == I_VSMIN;
          bool mx = in->op == I_VSMAX || in->op == I_VUMAX;
          if (sz < 3) {
            res = wr(s, v, F2);
            if (sgn && mx) x64_vpmaxs(B, sz, res, x, y);
            else if (sgn) x64_vpmins(B, sz, res, x, y);
            else if (mx) x64_vpmaxu(B, sz, res, x, y);
            else x64_vpminu(B, sz, res, x, y);
            break;
          }
          /* no 64-bit min/max: compare and blend (unsigned biases first) */
          int cx = x, cy = y;
          if (!sgn) {
            vsplat_const(s, FTMP, 3, 0x8000000000000000ull);
            x64_vpxor(B, F2, x, FTMP);
            x64_vpxor(B, FTMP, y, FTMP);
            cx = F2; cy = FTMP;
          }
          int m = sgn ? FTMP : F1;
          x64_vpcmpgt(B, 3, m, cx, cy);
          res = wr(s, v, F2);
          if (mx) x64_vpblendvb(B, res, y, x, m);
          else x64_vpblendvb(B, res, x, y, m);
          break;
        }

        case I_VZIP1: case I_VZIP2: {
          int g = (int)in->imm;
          res = wr(s, v, F2);
          if (in->op == I_VZIP1) x64_vpunpckl(B, g, res, x, y);
          else x64_vpunpckh(B, g, res, x, y);
          break;
        }

        case I_VUZP1: case I_VUZP2: {
          int g = (int)in->imm;
          bool odd = in->op == I_VUZP2;
          if (g == 3) {
            res = wr(s, v, F2);
            if (odd) x64_vpunpckh(B, 3, res, x, y);
            else x64_vpunpckl(B, 3, res, x, y);
          } else if (g == 2) {
            res = wr(s, v, F2);
            x64_vshufps(B, res, x, y, odd ? 0xdd : 0x88);
          } else {
            /* gather every other byte or word of each operand into its low
               half, then splice the two halves together */
            uint64_t lo = 0;
            for (int i = 0; i < 8 >> g; i++) {
              uint64_t b0 = (uint64_t)((2 << g) * i + (odd ? (1 << g) : 0));
              for (int j = 0; j < 1 << g; j++)
                lo |= (b0 + (uint64_t)j) << (8 * ((1 << g) * i + j));
            }
            vconst(s, FTMP, lo, 0x8080808080808080ull);
            x64_vpshufb(B, F2, x, FTMP);
            x64_vpshufb(B, FTMP, y, FTMP);
            x64_vpunpckl(B, 3, F2, F2, FTMP);
            res = F2;
          }
          break;
        }

        case I_VCMP: {
          uint16_t c = (uint16_t)in->imm;
          if (is_flt_cmp(c)) {
            int pred = XCMP_EQ_OQ;
            int cx = x, cy = y;
            switch (c) {
              case I_FEQ: pred = XCMP_EQ_OQ; break;
              case I_FNE: pred = XCMP_NEQ_UQ; break;
              case I_FLT: pred = XCMP_LT_OS; break;
              case I_FLE: pred = XCMP_LE_OS; break;
              case I_FGT: pred = XCMP_GT_OS; break;
              default:    pred = XCMP_GE_OS; break;
            }
            res = wr(s, v, F2);
            x64_vcmpp(B, fd, res, cx, cy, pred);
            break;
          }
          switch (c) {
            case I_EQ: res = wr(s, v, F2); x64_vpcmpeq(B, sz, res, x, y); break;
            case I_GTS: res = wr(s, v, F2); x64_vpcmpgt(B, sz, res, x, y); break;
            case I_LTS: res = wr(s, v, F2); x64_vpcmpgt(B, sz, res, y, x); break;
            case I_NE:
              x64_vpcmpeq(B, sz, FTMP, x, y);
              simd_not(s, F2, FTMP, F2);
              res = F2;
              break;
            case I_GES:
              x64_vpcmpgt(B, sz, FTMP, y, x);
              simd_not(s, F2, FTMP, F2);
              res = F2;
              break;
            case I_LES:
              x64_vpcmpgt(B, sz, FTMP, x, y);
              simd_not(s, F2, FTMP, F2);
              res = F2;
              break;
            case I_GEU: case I_LEU: case I_GTU: case I_LTU: {
              if (sz == 3) {
                /* bias into signed space: no unsigned 64-bit compare */
                vsplat_const(s, FTMP, 3, 0x8000000000000000ull);
                x64_vpxor(B, F2, x, FTMP);
                x64_vpxor(B, FTMP, y, FTMP);
                int bx = F2, by = FTMP;
                if (c == I_GTU) { res = wr(s, v, F1); x64_vpcmpgt(B, 3, res, bx, by); }
                else if (c == I_LTU) { res = wr(s, v, F1); x64_vpcmpgt(B, 3, res, by, bx); }
                else {
                  x64_vpcmpgt(B, 3, F1, c == I_GEU ? by : bx, c == I_GEU ? bx : by);
                  simd_not(s, F2, F1, F2);
                  res = F2;
                }
                break;
              }
              /* min/max are exact unsigned predicates:
                 minu(x,y) == x  <=>  x <= y,  maxu(x,y) == x  <=>  x >= y */
              bool eqform = c == I_GEU || c == I_LEU;
              if (c == I_GEU || c == I_LTU) x64_vpmaxu(B, sz, FTMP, x, y);
              else x64_vpminu(B, sz, FTMP, x, y);
              if (eqform) {
                res = wr(s, v, F2);
                x64_vpcmpeq(B, sz, res, FTMP, x);
              } else {
                x64_vpcmpeq(B, sz, FTMP, FTMP, x);
                simd_not(s, F2, FTMP, F2);
                res = F2;
              }
              break;
            }
            default:
              warmc_fatal("internal: no vector form of %s",
                          ir_op_name((uint16_t)in->imm));
          }
          break;
        }

        case I_VFADD: res = wr(s, v, F2); x64_vaddp(B, fd, res, x, y);
          canon_nan(s, res, fd); break;
        case I_VFSUB: res = wr(s, v, F2); x64_vsubp(B, fd, res, x, y);
          canon_nan(s, res, fd); break;
        case I_VFMUL: res = wr(s, v, F2); x64_vmulp(B, fd, res, x, y);
          canon_nan(s, res, fd); break;
        case I_VFDIV: res = wr(s, v, F2); x64_vdivp(B, fd, res, x, y);
          canon_nan(s, res, fd); break;

        /* wasm min/max keep NaNs and distinguish -0 from +0; the hardware
           min/max do neither, so both are folded out of the two operand
           orders and the result is re-quieted. */
        case I_VFMIN:
          x64_vminp(B, fd, FTMP, x, y);
          x64_vminp(B, fd, F2, y, x);
          x64_vorps(B, F2, F2, FTMP);
          x64_vcmpp(B, fd, FTMP, F2, F2, XCMP_UNORD_Q);
          x64_vorps(B, F2, F2, FTMP);
          x64_vpsrl_i(B, fd ? 3 : 2, FTMP, FTMP, fd ? 13 : 10);
          x64_vandnps(B, F2, FTMP, F2);
          canon_nan(s, F2, fd);
          res = F2;
          break;
        case I_VFMAX:
          x64_vmaxp(B, fd, FTMP, x, y);
          x64_vmaxp(B, fd, F2, y, x);
          x64_vxorps(B, F2, F2, FTMP);
          x64_vorps(B, FTMP, FTMP, F2);
          x64_vsubp(B, fd, FTMP, FTMP, F2);
          x64_vcmpp(B, fd, F2, FTMP, FTMP, XCMP_UNORD_Q);
          x64_vpsrl_i(B, fd ? 3 : 2, F2, F2, fd ? 13 : 10);
          x64_vandnps(B, F2, F2, FTMP);
          canon_nan(s, F2, fd);
          res = F2;
          break;

        /* pmin/pmax are "b < a ? b : a", which is exactly the hardware
           min/max with the operands the other way round */
        case I_VFPMIN: res = wr(s, v, F2); x64_vminp(B, fd, res, y, x); break;
        case I_VFPMAX: res = wr(s, v, F2); x64_vmaxp(B, fd, res, y, x); break;

        case I_VADDSATS: res = wr(s, v, F2); x64_vpadds(B, sz, res, x, y); break;
        case I_VADDSATU: res = wr(s, v, F2); x64_vpaddus(B, sz, res, x, y); break;
        case I_VSUBSATS: res = wr(s, v, F2); x64_vpsubs(B, sz, res, x, y); break;
        case I_VSUBSATU: res = wr(s, v, F2); x64_vpsubus(B, sz, res, x, y); break;
        case I_VAVGRU: res = wr(s, v, F2); x64_vpavg(B, sz, res, x, y); break;
        case I_VDOT: res = wr(s, v, F2); x64_vpmaddwd(B, res, x, y); break;

        case I_VDOTI7: res = simd_dot_i7(s, x, y); break;

        case I_VQ15MULR:
          x64_vpmulhrsw(B, FTMP, x, y);
          vsplat_const(s, F2, 1, 0x8000);
          x64_vpcmpeq(B, 1, F2, FTMP, F2);
          x64_vpxor(B, FTMP, FTMP, F2);   /* the one saturating case */
          res = FTMP;
          break;

        case I_VNARROW: {
          bool sgn = (in->imm & 1) != 0;
          res = wr(s, v, F2);
          if (sz == 0) {
            if (sgn) x64_vpacksswb(B, res, x, y);
            else x64_vpackuswb(B, res, x, y);
          } else {
            if (sgn) x64_vpackssdw(B, res, x, y);
            else x64_vpackusdw(B, res, x, y);
          }
          break;
        }

        case I_VEXTMUL: {
          bool sgn = (in->imm & 1) != 0, high = (in->imm & 2) != 0;
          if (sz == 1) {
            if (high) {
              x64_vpsrldq(B, FTMP, x, 8);
              x64_vpsrldq(B, F2, y, 8);
            } else {
              simd_mov(s, FTMP, x);
              simd_mov(s, F2, y);
            }
            if (sgn) { x64_vpmovsx(B, 0, FTMP, FTMP); x64_vpmovsx(B, 0, F2, F2); }
            else { x64_vpmovzx(B, 0, FTMP, FTMP); x64_vpmovzx(B, 0, F2, F2); }
            x64_vpmullw(B, FTMP, FTMP, F2);
          } else if (sz == 2) {
            x64_vpmullw(B, FTMP, x, y);
            if (sgn) x64_vpmulhw(B, F2, x, y);
            else x64_vpmulhuw(B, F2, x, y);
            if (high) x64_vpunpckh(B, 1, FTMP, FTMP, F2);
            else x64_vpunpckl(B, 1, FTMP, FTMP, F2);
          } else {
            x64_vpshufd(B, FTMP, x, high ? 0xfa : 0x50);
            x64_vpshufd(B, F2, y, high ? 0xfa : 0x50);
            if (sgn) x64_vpmuldq(B, FTMP, FTMP, F2);
            else x64_vpmuludq(B, FTMP, FTMP, F2);
          }
          res = FTMP;
          break;
        }

        /* out-of-range swizzle indices must read zero; adding 0x70 with
           unsigned saturation pushes every index >= 16 past 0x7f, which is
           what vpshufb treats as "write zero" */
        case I_VSWIZZLE:
          vsplat_const(s, FTMP, 0, 0x70);
          x64_vpaddus(B, 0, FTMP, y, FTMP);
          res = wr(s, v, F2);
          x64_vpshufb(B, res, x, FTMP);
          break;

        default: break;
      }
      commit(s, v, res);
      break;
    }

    /* One-operand SIMD.  Only a[0] is read, so F1/F2/FTMP are all free. */
    case I_VSHL: case I_VSHRU: case I_VSHRS:
    case I_VFNEG: case I_VFABS: case I_VFSQRT:
    case I_VNOT: case I_VNEG: case I_VABS: case I_VPOPCNT:
    case I_VEXTADD: case I_VEXTEND:
    case I_VFCEIL: case I_VFFLOOR: case I_VFTRUNC: case I_VFNEAREST:
    case I_VDEMOTE: case I_VPROMOTE:
    case I_VTRUNCSAT: case I_VCONVERT: {
      int x = rd(s, a[0], F0);
      int sz = VLANE_SIZE(in->imm2);
      int fd = (in->imm2 == VLANE_F64);
      int res = -1;
      switch (in->op) {
        case I_VSHL: case I_VSHRU: case I_VSHRS: {
          int k = (int)in->imm;
          res = simd_shift_imm(s, v, in->op, sz, x, k);
          break;
        }
        case I_VFNEG:
          vsplat_const(s, FTMP, fd ? 3 : 2,
                       fd ? 0x8000000000000000ull : 0x80000000ull);
          res = wr(s, v, F1);
          x64_vxorps(B, res, x, FTMP);
          break;
        case I_VFABS:
          vsplat_const(s, FTMP, fd ? 3 : 2,
                       fd ? 0x7fffffffffffffffull : 0x7fffffffull);
          res = wr(s, v, F1);
          x64_vandps(B, res, x, FTMP);
          break;
        case I_VFSQRT: res = wr(s, v, F1); x64_vsqrtp(B, fd, res, x);
          canon_nan(s, res, fd); break;
        case I_VNOT:
          res = wr(s, v, F1);
          simd_not(s, res, x, FTMP);
          break;
        case I_VNEG:
          simd_zero(s, FTMP);
          res = wr(s, v, F1);
          x64_vpsub(B, sz, res, FTMP, x);
          break;
        case I_VABS:
          if (sz < 3) { res = wr(s, v, F1); x64_vpabs(B, sz, res, x); }
          else {
            x64_vpshufd(B, FTMP, x, 0xf5);      /* both halves of each lane */
            x64_vpsra_i(B, 2, FTMP, FTMP, 31);  /* sign mask per 64-bit lane */
            x64_vpxor(B, F1, x, FTMP);
            res = wr(s, v, F2);
            x64_vpsub(B, 3, res, F1, FTMP);
          }
          break;
        case I_VPOPCNT:
          /* nibble lookup: two vpshufb of the popcount table */
          vsplat_const(s, FTMP, 0, 0x0f);
          x64_vpand(B, F2, x, FTMP);
          x64_vpsrl_i(B, 1, F1, x, 4);
          x64_vpand(B, F1, F1, FTMP);
          vconst(s, FTMP, 0x0302020102010100ull, 0x0403030203020201ull);
          x64_vpshufb(B, F2, FTMP, F2);
          x64_vpshufb(B, F1, FTMP, F1);
          res = wr(s, v, FTMP);
          x64_vpadd(B, 0, res, F2, F1);
          break;
        case I_VEXTADD: {
          bool sgn = (in->imm & 1) != 0;
          if (sz == 1) {
            /* pairwise byte sums: multiply-add against a vector of ones,
               with the signed operand on the side that needs the sign */
            vsplat_const(s, FTMP, 0, 1);
            res = wr(s, v, F1);
            if (sgn) x64_vpmaddubsw(B, res, FTMP, x);
            else x64_vpmaddubsw(B, res, x, FTMP);
          } else if (sgn) {
            vsplat_const(s, FTMP, 1, 1);
            res = wr(s, v, F1);
            x64_vpmaddwd(B, res, x, FTMP);
          } else {
            vsplat_const(s, FTMP, 2, 0xffff);
            x64_vpand(B, F2, x, FTMP);
            x64_vpsrl_i(B, 2, F1, x, 16);
            res = wr(s, v, FTMP);
            x64_vpadd(B, 2, res, F2, F1);
          }
          break;
        }
        case I_VEXTEND: {
          bool sgn = (in->imm & 1) != 0, high = (in->imm & 2) != 0;
          int src = x;
          if (high) { x64_vpsrldq(B, FTMP, x, 8); src = FTMP; }
          res = wr(s, v, F1);
          if (sgn) x64_vpmovsx(B, sz - 1, res, src);
          else x64_vpmovzx(B, sz - 1, res, src);
          break;
        }
        case I_VFCEIL: case I_VFFLOOR: case I_VFTRUNC: case I_VFNEAREST: {
          int mode = in->op == I_VFCEIL ? XRND_CEIL
                   : in->op == I_VFFLOOR ? XRND_FLOOR
                   : in->op == I_VFTRUNC ? XRND_TRUNC : XRND_NEAREST;
          res = wr(s, v, F1);
          x64_vroundp(B, fd, res, x, mode);
          canon_nan(s, res, fd);
          break;
        }
        case I_VDEMOTE: res = wr(s, v, F1); x64_vcvtpd2ps(B, res, x);
          canon_nan(s, res, fd); break;
        case I_VPROMOTE: res = wr(s, v, F1); x64_vcvtps2pd(B, res, x);
          canon_nan(s, res, fd); break;

        case I_VTRUNCSAT: res = simd_truncsat(s, v, x, in->imm & 1, fd); break;
        case I_VCONVERT: res = simd_convert(s, v, x, in->imm & 1, fd); break;
        default: break;
      }
      commit(s, v, res);
      break;
    }

    /* Shifts by a run-time scalar.  The count is masked to the lane width
       and broadcast through an xmm, the shape the hardware shifts want. */
    case I_VSHLV: case I_VSHRUV: case I_VSHRSV: {
      int sz = VLANE_SIZE(in->imm2);
      int lane_bits = 8 << sz;
      int x = rd(s, a[0], F0);
      uint64_t k;
      if (const_arg(f, a[1], &k)) {
        int op = in->op == I_VSHLV ? I_VSHL
               : in->op == I_VSHRUV ? I_VSHRU : I_VSHRS;
        commit(s, v, simd_shift_imm(s, v, op, sz, x, (int)(k % (uint64_t)lane_bits)));
        break;
      }
      int n = rd(s, a[1], TMP0);
      if (n != TMP0) x64_mov_rr(B, 0, TMP0, n);
      x64_and_ri(B, 0, TMP0, lane_bits - 1);
      if (in->op == I_VSHRSV && sz == 0) x64_add_ri(B, 0, TMP0, 8);
      x64_movd_gx(B, 0, FTMP, TMP0);
      int res;
      if (sz == 0) {
        if (in->op == I_VSHRSV) {
          /* sign-extending byte shift: widen to words, shift, pack back */
          x64_vpunpckl(B, 0, F1, x, x);
          x64_vpunpckh(B, 0, F2, x, x);
          x64_vpsra_x(B, 1, F1, F1, FTMP);
          x64_vpsra_x(B, 1, F2, F2, FTMP);
          x64_vpacksswb(B, F1, F1, F2);
          res = F1;
        } else {
          /* word shift plus the byte mask the count implies.  The mask is
             built in the low byte of a word and broadcast from there, so
             the right shift has to start from 0x00ff rather than from all
             ones -- shifting all ones right leaves the low byte full. */
          simd_ones(s, F1);
          if (in->op == I_VSHLV) {
            x64_vpsll_x(B, 1, F1, F1, FTMP);
            x64_vpsll_x(B, 1, F2, x, FTMP);
          } else {
            x64_vpsrl_i(B, 1, F1, F1, 8);
            x64_vpsrl_x(B, 1, F1, F1, FTMP);
            x64_vpsrl_x(B, 1, F2, x, FTMP);
          }
          x64_vpbroadcast(B, 0, F1, F1);
          x64_vpand(B, F1, F1, F2);
          res = F1;
        }
      } else if (in->op == I_VSHRSV && sz == 3) {
        /* no arithmetic 64-bit shift: (x ^ bias) >>u n - bias >>u n */
        vsplat_const(s, F2, 3, 0x8000000000000000ull);
        x64_vpxor(B, F1, x, F2);
        x64_vpsrl_x(B, 3, F1, F1, FTMP);
        x64_vpsrl_x(B, 3, F2, F2, FTMP);
        res = wr(s, v, F1);
        x64_vpsub(B, 3, res, F1, F2);
      } else {
        res = wr(s, v, F1);
        if (in->op == I_VSHLV) x64_vpsll_x(B, sz, res, x, FTMP);
        else if (in->op == I_VSHRUV) x64_vpsrl_x(B, sz, res, x, FTMP);
        else x64_vpsra_x(B, sz, res, x, FTMP);
      }
      commit(s, v, res);
      break;
    }

    case I_VDOTI7ADD: {
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
      simd_dot_i7(s, x, y);             /* the i16x8 stage, in FTMP */
      /* the widening add of adjacent lanes is signed and does not saturate */
      vsplat_const(s, F2, 1, 1);
      x64_vpmaddwd(B, FTMP, FTMP, F2);
      /* x and y are dead now, so their scratches are free for the addend */
      int c = rd(s, a[2], F0);
      int res = wr(s, v, F1);
      x64_vpadd(B, 2, res, FTMP, c);
      commit(s, v, res);
      break;
    }

    case I_VFMADD: {
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1), c = rd(s, a[2], F2);
      int fd = (in->imm2 == VLANE_F64);
      simd_mov(s, FTMP, c);
      if (in->imm & 1) x64_vfnmadd231p(B, fd, FTMP, x, y);
      else x64_vfmadd231p(B, fd, FTMP, x, y);
      commit(s, v, FTMP);
      break;
    }

    case I_VSEL: {
      int m = rd(s, a[0], F0), x = rd(s, a[1], F1), y = rd(s, a[2], F2);
      x64_vpand(B, FTMP, x, m);
      int d = wr(s, v, F2);
      x64_vpandn(B, d, m, y);
      x64_vpor(B, d, d, FTMP);
      commit(s, v, d);
      break;
    }

    case I_VSHUFFLE: {
      /* The a64 lowering takes its two index vectors from the IR; vpshufb
         zeroes on the high bit instead of on out-of-range, so build the
         masks here and leave the IR's own operands alone. */
      uint64_t sel[2] = { in->imm, in->imm2 };
      uint64_t mx[2] = { 0, 0 }, my[2] = { 0, 0 };
      bool from_x = false, from_y = false;
      for (int i = 0; i < 16; i++) {
        uint32_t idx = (uint32_t)((sel[i >> 3] >> (8 * (i & 7))) & 0xff);
        int h = i >> 3, sh = 8 * (i & 7);
        if (idx < 16) { from_x = true; mx[h] |= (uint64_t)idx << sh; }
        else mx[h] |= 0x80ull << sh;
        if (idx >= 16 && idx < 32) { from_y = true; my[h] |= (uint64_t)(idx - 16) << sh; }
        else my[h] |= 0x80ull << sh;
      }
      int x = rd(s, a[0], F0), y = rd(s, a[1], F1);
      int res;
      if (!from_y) {
        vconst(s, FTMP, mx[0], mx[1]);
        res = wr(s, v, F2);
        x64_vpshufb(B, res, x, FTMP);
      } else if (!from_x) {
        vconst(s, FTMP, my[0], my[1]);
        res = wr(s, v, F2);
        x64_vpshufb(B, res, y, FTMP);
      } else {
        vconst(s, FTMP, mx[0], mx[1]);
        x64_vpshufb(B, F2, x, FTMP);
        vconst(s, FTMP, my[0], my[1]);
        x64_vpshufb(B, FTMP, y, FTMP);
        x64_vpor(B, F2, F2, FTMP);
        res = F2;
      }
      commit(s, v, res);
      break;
    }

    case I_VANYTRUE: {
      int x = rd(s, a[0], F0);
      int d = wr(s, v, S0);
      x64_vptest(B, x, x);
      x64_setcc(B, XCC_NE, d);
      x64_movzx8(B, 0, d, d);
      commit(s, v, d);
      break;
    }

    case I_VALLTRUE: {
      int x = rd(s, a[0], F0);
      int sz = VLANE_SIZE(in->imm2);
      simd_zero(s, FTMP);
      x64_vpcmpeq(B, sz, FTMP, x, FTMP);   /* set where a lane is zero */
      int d = wr(s, v, S0);
      x64_vptest(B, FTMP, FTMP);
      x64_setcc(B, XCC_E, d);
      x64_movzx8(B, 0, d, d);
      commit(s, v, d);
      break;
    }

    case I_VBITMASK: {
      /* the lane weights the a64 lowering multiplies by are implicit in
         the x86 move-mask instructions, so a[1] goes unread */
      int x = rd(s, a[0], F0);
      int sz = VLANE_SIZE(in->imm2);
      int d = wr(s, v, S0);
      if (sz == 0) {
        x64_vpmovmskb(B, d, x);
      } else if (sz == 1) {
        x64_vpacksswb(B, FTMP, x, x);
        x64_vpmovmskb(B, d, FTMP);
        x64_and_ri(B, 0, d, 0xff);
      } else {
        x64_vmovmskp(B, sz == 3, d, x);
      }
      commit(s, v, d);
      break;
    }

    case I_VREDUCE: {
      int x = rd(s, a[0], F0);
      int sz = VLANE_SIZE(in->imm2);
      uint16_t op = (uint16_t)in->imm;
      simd_mov(s, FTMP, x);
      for (int by = 8; by >= (1 << sz); by >>= 1) {
        x64_vpsrldq(B, F1, FTMP, by);
        switch (op) {
          case I_ADD:    x64_vpadd(B, sz, FTMP, FTMP, F1); break;
          case I_AND:    x64_vpand(B, FTMP, FTMP, F1); break;
          case I_OR:     x64_vpor(B, FTMP, FTMP, F1); break;
          case I_XOR:    x64_vpxor(B, FTMP, FTMP, F1); break;
          case I_VSMIN:  x64_vpmins(B, sz, FTMP, FTMP, F1); break;
          case I_VSMAX:  x64_vpmaxs(B, sz, FTMP, FTMP, F1); break;
          case I_VUMIN:  x64_vpminu(B, sz, FTMP, FTMP, F1); break;
          case I_VUMAX:  x64_vpmaxu(B, sz, FTMP, FTMP, F1); break;
          default:
            warmc_fatal("internal: no vector reduction for %s", ir_op_name(op));
        }
      }
      int d = wr(s, v, S0);
      if (sz >= 2) x64_movd_xg(B, sz == 3, d, FTMP);
      else x64_vpextr(B, sz, d, FTMP, 0);
      commit(s, v, d);
      break;
    }

    case I_VSPLAT: {
      uint64_t lane = in->imm2;
      int d = wr(s, v, F0);
      if (lane == VLANE_F32) {
        int x = rd(s, a[0], F1);
        x64_vbroadcastss(B, d, x);
      } else if (lane == VLANE_F64) {
        int x = rd(s, a[0], F1);
        x64_vpbroadcast(B, 3, d, x);
      } else if (is_fp_value(f, a[0])) {
        /* a narrow vector load feeding the splat keeps the scalar in an
           xmm already */
        int x = rd(s, a[0], F1);
        x64_vpbroadcast(B, VLANE_SIZE(lane), d, x);
      } else {
        int x = rd(s, a[0], S0);
        x64_movd_gx(B, lane == VLANE_I64 ? 1 : 0, d, x);
        x64_vpbroadcast(B, VLANE_SIZE(lane), d, d);
      }
      commit(s, v, d);
      break;
    }

    case I_VEXTRACTS: case I_VEXTRACTU: {
      uint64_t lane = in->imm2;
      int idx = (int)in->imm;
      int x = rd(s, a[0], F0);
      if (lane == VLANE_F32) {
        int d = wr(s, v, F1);
        if (idx == 0) { if (d != x) x64_vmovdqa_rr(B, d, x); }
        else x64_vpshufd(B, d, x, idx);
        commit(s, v, d);
        break;
      }
      if (lane == VLANE_F64) {
        int d = wr(s, v, F1);
        if (idx == 0) { if (d != x) x64_vmovdqa_rr(B, d, x); }
        else x64_vpshufd(B, d, x, 0x0e);
        commit(s, v, d);
        break;
      }
      int d = wr(s, v, S0);
      int sz = VLANE_SIZE(lane);
      x64_vpextr(B, sz, d, x, idx);
      if (in->op == I_VEXTRACTS) {
        if (sz == 0) x64_movsx8(B, 0, d, d);
        else if (sz == 1) x64_movsx16(B, 0, d, d);
      }
      commit(s, v, d);
      break;
    }

    case I_VREPLACE: {
      uint64_t lane = in->imm2;
      int idx = (int)in->imm;
      int sz = VLANE_SIZE(lane);
      int x = rd(s, a[0], F0);
      int d = wr(s, v, F1);
      if (VLANE_IS_FLOAT(lane)) {
        int y = rd(s, a[1], F2);
        x64_movd_xg(B, sz == 3, TMP0, y);
        x64_vpinsr(B, sz, d, x, TMP0, idx);
      } else {
        int y = rd(s, a[1], S0);
        x64_vpinsr(B, sz, d, x, y, idx);
      }
      commit(s, v, d);
      break;
    }

    case I_GLOBAL_GET: {
      uint32_t off;
      int base = vmctx_at(s, global_off((uint32_t)in->imm), 3, &off, S1);
      if (f->m->globals[in->imm].imported) {
        x64_load64(B, S1, base, X64_NOIDX, 0, (int32_t)off);
        base = S1;
        off = 0;
      }
      if (in->type == VT_V128) {
        int d = wr(s, v, F0);
        x64_vmovdqu_load(B, d, base, X64_NOIDX, 0, (int32_t)off);
        commit(s, v, d);
      } else if (vt_is_float((ValType)in->type)) {
        int d = wr(s, v, F0);
        x64_vmovs_load(B, wide, d, base, X64_NOIDX, 0, (int32_t)off);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        if (wide) x64_load64(B, d, base, X64_NOIDX, 0, (int32_t)off);
        else x64_load32(B, d, base, X64_NOIDX, 0, (int32_t)off);
        commit(s, v, d);
      }
      break;
    }
    case I_GLOBAL_SET: {
      ValType t = f->m->globals[in->imm].type;
      uint32_t off;
      int base = vmctx_at(s, global_off((uint32_t)in->imm), 3, &off, S1);
      if (f->m->globals[in->imm].imported) {
        x64_load64(B, S1, base, X64_NOIDX, 0, (int32_t)off);
        base = S1;
        off = 0;
      }
      if (t == VT_V128) {
        int x = rd(s, a[0], F0);
        x64_vmovdqu_store(B, x, base, X64_NOIDX, 0, (int32_t)off);
      } else if (vt_is_float(t)) {
        int x = rd(s, a[0], F0);
        x64_vmovs_store(B, vt_is_64(t), x, base, X64_NOIDX, 0, (int32_t)off);
      } else {
        int x = rd(s, a[0], S0);
        if (vt_is_64(t)) x64_store64(B, x, base, X64_NOIDX, 0, (int32_t)off);
        else x64_store32(B, x, base, X64_NOIDX, 0, (int32_t)off);
      }
      break;
    }

    case I_MEM_SIZE: {
      int d = wr(s, v, S0);
      if (in->imm == 0) {
        if (wide) x64_load64(B, d, REG_VMCTX, X64_NOIDX, 0, VMCTX_MEM_PAGES);
        else x64_load32(B, d, REG_VMCTX, X64_NOIDX, 0, VMCTX_MEM_PAGES);
      } else {
        uint32_t disp;
        int mb = vmctx_at(s, mem_slot_off(f, in->imm) + MEM_SLOT_LEN, 3, &disp, d);
        x64_load64(B, d, mb, X64_NOIDX, 0, (int32_t)disp);
        x64_shr_ri(B, 1, d, 16);
      }
      commit(s, v, d);
      break;
    }
    case I_MEM_GROW:
      call_helper_imm(s, "warmc_memory_grow", a, 1, in->imm, v, true);
      break;
    case I_MEM_COPY:
      call_helper_imm2(s, "warmc_memory_copy", a, 3, in->imm, in->imm2);
      break;
    case I_MEM_FILL:
      call_helper_imm(s, "warmc_memory_fill", a, 3, in->imm, 0, false);
      break;
    case I_MEM_INIT:
      call_helper_imm2(s, "warmc_memory_init", a, 3, in->imm, in->imm2);
      break;
    case I_DATA_DROP:
      call_helper_imm(s, "warmc_data_drop", a, 0, in->imm, 0, false);
      break;

    case I_TABLE_GET: call_helper_imm(s, "warmc_table_get", a, 1, in->imm, v, true); break;
    case I_TABLE_SET: call_helper_imm(s, "warmc_table_set", a, 2, in->imm, 0, false); break;
    case I_TABLE_GROW: call_helper_imm(s, "warmc_table_grow", a, 2, in->imm, v, true); break;
    case I_TABLE_FILL: call_helper_imm(s, "warmc_table_fill", a, 3, in->imm, 0, false); break;
    case I_TABLE_COPY: call_helper_imm2(s, "warmc_table_copy", a, 3, in->imm, in->imm2); break;
    case I_TABLE_INIT: call_helper_imm2(s, "warmc_table_init", a, 3, in->imm, in->imm2); break;
    case I_ELEM_DROP:
      call_helper_imm(s, "warmc_elem_drop", a, 0, in->imm, 0, false);
      break;
    case I_TABLE_SIZE: {
      if (in->imm == 0) {
        int d = wr(s, v, S0);
        x64_load64(B, d, REG_VMCTX, X64_NOIDX, 0, VMCTX_TABLE_LEN);
        commit(s, v, d);
      } else {
        call_helper_imm(s, "warmc_table_size", a, 0, in->imm, v, true);
      }
      break;
    }
    case I_REF_IS_NULL: {
      int x = rd(s, a[0], S0);
      int d = wr(s, v, S0);
      x64_test_rr(B, 1, x, x);
      x64_setcc(B, XCC_E, d);
      x64_movzx8(B, 0, d, d);
      commit(s, v, d);
      break;
    }
    case I_REF_FUNC: {
      int d = wr(s, v, S0);
      uint32_t off = VMCTX_FUNCREFS(f->m->nglobals, f->m->nimported_funcs,
                                    f->m->ntables, f->m->nmemories) +
                     FUNCREF_SIZE * (uint32_t)in->imm;
      if (off <= 0x7fffffffu) {
        x64_lea(B, 1, d, REG_VMCTX, X64_NOIDX, 0, (int32_t)off);
      } else {
        x64_mov_ri(B, 1, TMP0, off);
        x64_lea(B, 1, d, REG_VMCTX, TMP0, 0, 0);
      }
      commit(s, v, d);
      break;
    }

    case I_TRAP_IF: {
      int x = rd(s, a[0], S0);
      trap_if_zero(s, is64(f, a[0]), x, (int)in->imm);
      break;
    }
    case I_TRAP: emit_trap(s, (int)in->imm); break;

    case I_CALL: {
      CompType *sig = call_sig(s, in);
      csa_save(s, v);
      Func *fn = &f->m->funcs[in->imm];
      if (fn->imported) {
        emit_call_args_novmctx(s, a, in->nargs);
        uint32_t off = VMCTX_IMPORTS(f->m->nglobals) + 16u * (uint32_t)in->imm;
        uint32_t disp;
        int base = vmctx_at(s, off, 3, &disp, TMP1);
        x64_load64(B, TMP0, base, X64_NOIDX, 0, (int32_t)disp);
        x64_load64(B, X_RCX, base, X64_NOIDX, 0, (int32_t)(disp + 8));
        emit_ret_area(s, sig);
        x64_call_r(B, TMP0);
      } else {
        emit_call_args(s, a, in->nargs);
        emit_ret_area(s, sig);
        uint32_t sym = obj_intern_sym(s->o, fn->sym, SYM_LOCAL_FUNC, -1, 0);
        x64_call_rel32(B);
        isel_reloc(s, REL_PC32, B->len - 4, sym, -4);
      }
      note_site(s);
      note_safepoint(s, v);
      emit_call_ret(s, v, sig);
      csa_restore(s, v);
      break;
    }

    case I_CALL_INDIRECT: case I_CALL_REF: {
      CompType *sig = call_sig(s, in);
      csa_save(s, v);
      if (in->op == I_CALL_REF) emit_ref_lookup(s, a[in->nargs - 1]);
      else emit_indirect_lookup(s, in, a[in->nargs - 1]);

      /* the funcref cell rides out the marshalling in R11 */
      emit_call_args_novmctx(s, a, in->nargs - 1);
      x64_load64(B, X_RCX, S0, X64_NOIDX, 0, FUNCREF_INSTANCE);
      x64_load64(B, TMP0, S0, X64_NOIDX, 0, FUNCREF_CODE);
      emit_ret_area(s, sig);
      x64_call_r(B, TMP0);
      note_site(s);
      note_safepoint(s, v);
      emit_call_ret(s, v, sig);
      csa_restore(s, v);
      break;
    }

    case I_RETURN_CALL: case I_RETURN_CALL_INDIRECT: case I_RETURN_CALL_REF: {
      bool indirect = in->op == I_RETURN_CALL_INDIRECT || in->op == I_RETURN_CALL_REF;
      Module *m = f->m;
      uint32_t nargs = in->nargs - (indirect ? 1u : 0u);
      int target = -1;

      if (indirect) {
        if (in->op == I_RETURN_CALL_REF) emit_ref_lookup(s, a[in->nargs - 1]);
        else emit_indirect_lookup(s, in, a[in->nargs - 1]);
      }

      /* stack args land in our own incoming argument area */
      emit_call_args_core(s, a, nargs, (int32_t)(s->frame + s->saved_bytes),
                          1, true, false);
      if (f->sig->nresults > 1)
        x64_load64(B, X_R10, X_RSP, X64_NOIDX, 0, (int32_t)s->retptr_slot);

      if (indirect) {
        x64_load64(B, X_RCX, S0, X64_NOIDX, 0, FUNCREF_INSTANCE);
        x64_load64(B, TMP0, S0, X64_NOIDX, 0, FUNCREF_CODE);
        target = TMP0;
      } else if (m->funcs[in->imm].imported) {
        uint32_t off = VMCTX_IMPORTS(m->nglobals) + 16u * (uint32_t)in->imm;
        uint32_t disp;
        int base = vmctx_at(s, off, 3, &disp, TMP1);
        x64_load64(B, TMP0, base, X64_NOIDX, 0, (int32_t)disp);
        x64_load64(B, X_RCX, base, X64_NOIDX, 0, (int32_t)(disp + 8));
        target = TMP0;
      }

      emit_teardown(s);
      if (target >= 0) {
        x64_jmp_r(B, target);
      } else {
        uint32_t sym = obj_intern_sym(s->o, m->funcs[in->imm].sym,
                                      SYM_LOCAL_FUNC, -1, 0);
        x64_jmp_rel32(B);
        isel_reloc(s, REL_PC32, B->len - 4, sym, -4);
      }
      s->tail_done = true;
      break;
    }

    case I_THROW: {
      for (uint32_t i = 0; i < in->nargs; i++) {
        int32_t at = (int32_t)(s->throw_area_off + ARG_STACK_SLOT * i);
        Ref x = a[i];
        if (is_vec_value(f, x))
          x64_vmovdqu_store(B, rd(s, x, FTMP), X_RSP, X64_NOIDX, 0, at);
        else if (is_fp_value(f, x))
          x64_vmovs_store(B, 1, rd(s, x, FTMP), X_RSP, X64_NOIDX, 0, at);
        else
          x64_store64(B, rd(s, x, TMP0), X_RSP, X64_NOIDX, 0, at);
      }
      x64_mov_rr(B, 1, X_RCX, REG_VMCTX);
      x64_mov_ri(B, 0, X_RDX, in->imm);
      x64_lea(B, 1, X_R8, X_RSP, X64_NOIDX, 0, (int32_t)s->throw_area_off);
      x64_mov_ri(B, 0, X_R9, in->nargs);
      emit_call_sym(s, "warmc_throw");
      note_site(s);
      note_safepoint(s, v);
      x64_int3(B);
      s->tail_done = true;
      break;
    }
    case I_THROW_REF: {
      int x = rd(s, a[0], S0);
      if (x != X_RCX) x64_mov_rr(B, 1, X_RCX, x);
      emit_call_sym(s, "warmc_throw_ref");
      note_site(s);
      x64_int3(B);
      s->tail_done = true;
      break;
    }

    case I_EXN_OBJ: {
      int d = wr(s, v, S0);
      x64_load64(B, d, X_RSP, X64_NOIDX, 0, (int32_t)s->exn_slot);
      commit(s, v, d);
      break;
    }
    case I_EXN_PAYLOAD: {
      int x = rd(s, a[0], S0);
      emit_gc_canon(s, S1, x);
      int32_t off = (int32_t)(EXN_VALS_OFF + ARG_STACK_SLOT * (uint32_t)in->imm);
      if ((ValType)in->type == VT_V128) {
        int d = wr(s, v, F0);
        x64_vmovdqu_load(B, d, S1, X64_NOIDX, 0, off);
        commit(s, v, d);
      } else if (vt_is_float((ValType)in->type)) {
        int d = wr(s, v, F0);
        x64_vmovs_load(B, wide, d, S1, X64_NOIDX, 0, off);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        if (wide) x64_load64(B, d, S1, X64_NOIDX, 0, off);
        else x64_load32(B, d, S1, X64_NOIDX, 0, off);
        if (load_needs_barrier(f, in))
          emit_gc_load_barrier(s, d, S1, -1, (uint32_t)off,
                               load_barrier_mask(f, v));
        commit(s, v, d);
      }
      break;
    }

    case I_RESULT: {
      int32_t off = (int32_t)(s->retarea_off + RESULT_SLOT_SIZE * (uint32_t)in->imm);
      if ((ValType)in->type == VT_V128) {
        int d = wr(s, v, F0);
        x64_vmovdqu_load(B, d, X_RSP, X64_NOIDX, 0, off);
        commit(s, v, d);
      } else if (vt_is_float((ValType)in->type)) {
        int d = wr(s, v, F0);
        x64_vmovs_load(B, wide, d, X_RSP, X64_NOIDX, 0, off);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        if (wide) x64_load64(B, d, X_RSP, X64_NOIDX, 0, off);
        else x64_load32(B, d, X_RSP, X64_NOIDX, 0, off);
        commit(s, v, d);
      }
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
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, 1, x, TRAP_NULL_REF);
      emit_gc_canon(s, S1, x);
      int size = (int)GCF_SIZE(in->imm2);
      bool sgn = (in->imm2 & GCF_SIGNED) != 0;
      int32_t off = (int32_t)in->imm;
      ValType t = (ValType)in->type;
      if (t == VT_V128) {
        int d = wr(s, v, F0);
        x64_vmovdqu_load(B, d, S1, X64_NOIDX, 0, off);
        commit(s, v, d);
      } else if (vt_is_float(t)) {
        int d = wr(s, v, F0);
        x64_vmovs_load(B, size == 3, d, S1, X64_NOIDX, 0, off);
        commit(s, v, d);
      } else {
        int d = wr(s, v, S0);
        gload(B, size, sgn, wide, d, S1, X64_NOIDX, 0, off);
        if (load_needs_barrier(f, in))
          emit_gc_load_barrier(s, d, S1, -1, (uint32_t)off,
                               load_barrier_mask(f, v));
        commit(s, v, d);
      }
      break;
    }

    case I_FIELD_SET: {
      int x = rd(s, a[0], S0);
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, 1, x, TRAP_NULL_REF);
      emit_gc_canon(s, TMP1, x);
      int size = (int)GCF_SIZE(in->imm2);
      int32_t off = (int32_t)in->imm;
      ValType t = (ValType)f->insts[a[1]].type;
      if (t == VT_V128) {
        int y = rd(s, a[1], F0);
        x64_vmovdqu_store(B, y, TMP1, X64_NOIDX, 0, off);
      } else if (vt_is_float(t)) {
        int y = rd(s, a[1], F0);
        x64_vmovs_store(B, size == 3, y, TMP1, X64_NOIDX, 0, off);
      } else {
        int y = rd(s, a[1], S1);
        gstore(B, size, y, TMP1, X64_NOIDX, 0, off);
      }
      break;
    }

    case I_ARRAY_NEW: {
      bool fast = false;
      uint64_t total;
      int64_t alen;
      if (gc_fastnew_shape(f, v, &total, &alen))
        fast = emit_gc_fastnew(s, v, gctype_slot(f->m, (uint32_t)in->imm),
                               total, alen, "warmc_array_new", a, 1);
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
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, 1, x, TRAP_NULL_REF);
      emit_gc_canon(s, TMP1, x);
      int size = (int)GCF_SIZE(in->imm2);
      int idx = rd(s, a[1], S1);
      if (!(in->imm2 & GCF_NOBOUND)) {
        x64_load32(B, TMP0, TMP1, X64_NOIDX, 0, 4);
        x64_cmp_rr(B, 0, idx, TMP0);
        trap_if_cc(s, XCC_AE, TRAP_ARRAY_OOB);
      }
      /* A store's value is read before the element's address goes into
         TMP0: reading a float or vector constant may build it there. */
      int y = -1;
      ValType yt = VT_I32;
      if (in->op == I_ARRAY_SET) {
        yt = (ValType)f->insts[a[2]].type;
        y = rd(s, a[2], yt == VT_V128 || vt_is_float(yt) ? F0 : S2);
      }
      int32_t off = (int32_t)f->m->types[in->imm].hdr_size;
      if (size <= 3) {
        x64_lea(B, 1, TMP0, TMP1, idx, size, 0);
      } else {
        x64_mov_rr(B, 0, TMP0, idx);
        x64_shl_ri(B, 1, TMP0, 4);
        x64_add_rr(B, 1, TMP0, TMP1);
      }
      if (in->op == I_ARRAY_GET) {
        bool sgn = (in->imm2 & GCF_SIGNED) != 0;
        ValType t = (ValType)in->type;
        if (t == VT_V128) {
          int d = wr(s, v, F0);
          x64_vmovdqu_load(B, d, TMP0, X64_NOIDX, 0, off);
          commit(s, v, d);
        } else if (vt_is_float(t)) {
          int d = wr(s, v, F0);
          x64_vmovs_load(B, size == 3, d, TMP0, X64_NOIDX, 0, off);
          commit(s, v, d);
        } else {
          int d = wr(s, v, S0);
          gload(B, size, sgn, wide, d, TMP0, X64_NOIDX, 0, off);
          if (load_needs_barrier(f, in))
            emit_gc_load_barrier(s, d, TMP0, -1, (uint32_t)off,
                                 load_barrier_mask(f, v));
          commit(s, v, d);
        }
      } else {
        if (yt == VT_V128) x64_vmovdqu_store(B, y, TMP0, X64_NOIDX, 0, off);
        else if (vt_is_float(yt)) x64_vmovs_store(B, size == 3, y, TMP0, X64_NOIDX, 0, off);
        else gstore(B, size, y, TMP0, X64_NOIDX, 0, off);
      }
      break;
    }

    case I_GC_LAB_OPEN:
      x64_load64(B, TMP0, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_LAB);
      x64_load64(B, s->lab_reg, TMP0, X64_NOIDX, 0, 0);
      break;

    case I_GC_LAB_FLUSH:
      x64_load64(B, TMP0, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_LAB);
      x64_store64(B, s->lab_reg, TMP0, X64_NOIDX, 0, 0);
      break;

    case I_GC_BADMASK: {
      int d = wr(s, v, S0);
      x64_load64(B, d, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_BADMASK);
      commit(s, v, d);
      break;
    }

    case I_ARRAY_LEN: {
      int x = rd(s, a[0], S0);
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, 1, x, TRAP_NULL_REF);
      emit_gc_canon(s, TMP1, x);
      int d = wr(s, v, S0);
      x64_load32(B, d, TMP1, X64_NOIDX, 0, 4);
      commit(s, v, d);
      break;
    }

    case I_REF_TEST: {
      /* Same slot fast path as the a64 backend: this module's objects
         answer inline from their slot (rtest_slots), another module's ask
         warmc_ref_test. */
      size_t to_end[3];
      int nend = 0;
      int d = wr(s, v, S1);
      uint32_t lo, hi, nslots;
      bool exact;
      if ((RTEST_KIND(in->imm) & ~RTK_NULLABLE) == RTK_CONCRETE_ANY &&
          !warmc_getenv("WARMC_NO_RTEST_FAST") &&
          rtest_slots(f->m, in->imm, in->imm2, &lo, &hi, &exact, &nslots)) {
        int x = rd(s, a[0], S0);
        bool nullable = (RTEST_KIND(in->imm) & RTK_NULLABLE) != 0;
        uint32_t tmask = (1u << (30 - GC_TYPE_SHIFT)) - 1u;
        size_t to_slow = 0;

        x64_test_rr(B, 1, x, x);
        x64_jcc_rel32(B, XCC_E);
        size_t to_null = B->len;
        x64_test_ri(B, 1, x, 3);
        x64_jcc_rel32(B, XCC_NE);
        size_t to_i31 = B->len;
        emit_gc_canon(s, TMP0, x);
        x64_load32(B, TMP0, TMP0, X64_NOIDX, 0, GC_HDR_WORD_OFF);
        x64_load32(B, TMP1, REG_VMCTX, X64_NOIDX, 0, VMCTX_GC_HDR);
        x64_shr_ri(B, 0, TMP0, GC_TYPE_SHIFT);
        x64_and_ri(B, 0, TMP0, (int32_t)tmask);
        x64_shr_ri(B, 0, TMP1, GC_TYPE_SHIFT);
        x64_and_ri(B, 0, TMP1, (int32_t)tmask);
        /* the slot, if the object is ours */
        x64_sub_rr(B, 0, TMP0, TMP1);
        if (exact) {
          x64_cmp_ri(B, 0, TMP0, (int32_t)nslots);
          x64_jcc_rel32(B, XCC_AE);
          to_slow = B->len;
        }
        if (lo <= hi) {
          if (lo != hi && lo) x64_sub_ri(B, 0, TMP0, (int32_t)lo);
          x64_cmp_ri(B, 0, TMP0, (int32_t)(lo == hi ? lo : hi - lo));
          if (exact) {
            x64_setcc(B, lo == hi ? XCC_E : XCC_BE, d);
            x64_movzx8(B, 0, d, d);
          } else {
            x64_jcc_rel32(B, lo == hi ? XCC_NE : XCC_A);
            to_slow = B->len;
            x64_mov_ri(B, 0, d, 1);
          }
          x64_jmp_rel32(B);
          to_end[nend++] = B->len;
        }
        br_patch(s, to_i31, B->len);
        x64_mov_ri(B, 0, d, 0);
        x64_jmp_rel32(B);
        to_end[nend++] = B->len;
        br_patch(s, to_null, B->len);
        x64_mov_ri(B, 0, d, nullable ? 1 : 0);
        x64_jmp_rel32(B);
        to_end[nend++] = B->len;
        if (to_slow) br_patch(s, to_slow, B->len);
      }
      csa_save(s, v);
      emit_call_args_core(s, a, 1, 0, 1, true, true);
      helper_imm_at(s, 2, RTEST_KIND(in->imm));
      helper_imm_at(s, 3, in->imm2);
      helper_imm_at(s, 4, RTEST_DEPTH(in->imm));
      emit_call_sym(s, "warmc_ref_test");
      s->mem_valid = false;
      if (d != X_RAX) x64_mov_rr(B, 0, d, X_RAX);
      csa_restore(s, v);
      for (int e = 0; e < nend; e++) br_patch(s, to_end[e], B->len);
      commit(s, v, d);
      break;
    }

    case I_REF_CONVERT: {
      int x = rd(s, a[0], S0);
      int d = wr(s, v, S0);
      if (d != x) x64_mov_rr(B, 1, d, x);
      commit(s, v, d);
      break;
    }

    case I_REF_I31: {
      int y = rd(s, a[0], S0);
      int d = wr(s, v, S1);
      x64_mov_rr(B, 0, d, y);
      x64_and_ri(B, 0, d, 0x7fffffff);
      x64_shl_ri(B, 1, d, 1);
      x64_or_ri(B, 1, d, 1);
      commit(s, v, d);
      break;
    }

    case I_I31_GET: {
      int x = rd(s, a[0], S0);
      if (in->imm2 & GCF_NULLCHK) trap_if_zero(s, 1, x, TRAP_NULL_REF);
      int d = wr(s, v, S1);
      x64_mov_rr(B, 0, d, x);
      if (in->imm) x64_sar_ri(B, 0, d, 1);
      else x64_shr_ri(B, 0, d, 1);
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
    if (vt_is_vec_reg(sig->results[0])) {
      int x = rd(s, r, X_XMM0);
      if (x != X_XMM0) x64_vmovdqa_rr(B, X_XMM0, x);
    } else {
      int x = rd(s, r, TMP0);
      if (x != X_RAX) x64_mov_rr(B, 1, X_RAX, x);
    }
  } else if (sig->nresults > 1) {
    x64_load64(B, TMP1, X_RSP, X64_NOIDX, 0, (int32_t)s->retptr_slot);
    for (uint32_t i = 0; i < sig->nresults; i++) {
      Ref r = b->ret_args[i];
      int32_t at = (int32_t)(i * RESULT_SLOT_SIZE);
      if (sig->results[i] == VT_V128) {
        int x = rd(s, r, F0);
        x64_vmovdqu_store(B, x, TMP1, X64_NOIDX, 0, at);
      } else if (vt_is_float(sig->results[i])) {
        int x = rd(s, r, F0);
        x64_vmovs_store(B, vt_is_64(sig->results[i]), x, TMP1, X64_NOIDX, 0, at);
      } else {
        int x = rd(s, r, S0);
        if (vt_is_64(sig->results[i]))
          x64_store64(B, x, TMP1, X64_NOIDX, 0, at);
        else
          x64_store32(B, x, TMP1, X64_NOIDX, 0, at);
      }
    }
  }
  emit_epilogue(s);
}

static void emit_block(Isel *s, uint32_t bi, uint32_t next_bi) {
  IrFunc *f = s->f;
  Block *b = &f->blocks[bi];

  s->cur_block = bi;
  rc_clear(s);

  bool eh = ir_block_has_eh(f, bi);
  s->cmp_ref = REF_NONE;
  s->fl_kind = 0;
  for (uint32_t i = 0; i < b->ninsts; i++) {
    s->at_eh_site = eh && i + 1 == b->ninsts;
    if (s->at_eh_site) emit_eh_stores(s, bi);
    emit_inst(s, b->insts[i]);
  }
  s->at_eh_site = false;

  if (s->tail_done) { s->tail_done = false; return; }

  switch (b->term) {
    case T_JUMP: {
      emit_edge(s, &b->succs[0]);
      if (b->succs[0].target != next_bi) {
        x64_jmp_rel32(B);
        add_fix(s, B->len, 0, b->succs[0].target);
      }
      break;
    }
    case T_BRIF: {
      Edge *t = &b->succs[0], *e = &b->succs[1];
      Ref c = b->term_arg;

      int kind = 0, cc = XCC_NE, zreg = 0, zw = 0;
      if (s->fused && s->fused[c] && is_condition(s, c)) {
        Inst *ci = &f->insts[c];
        Ref *ca = ir_args(f, c);
        if (ci->op == I_EQZ) {
          kind = 1; zw = is64(f, ca[0]); zreg = rd(s, ca[0], S0);
        } else if ((ci->op == I_EQ || ci->op == I_NE) &&
                   is_imm_at(s, c, 1, ca[1]) && imm_of(s, ca[1]) == 0) {
          kind = (ci->op == I_EQ) ? 1 : 2;
          zw = is64(f, ca[0]); zreg = rd(s, ca[0], S0);
        } else if (is_feqne(ci->op)) {
          emit_compare(s, c);
          emit_setcc(s, c, S0);
          kind = 2; zw = 0; zreg = S0;
        } else {
          emit_compare(s, c);
          cc = cond_of(s, c);
        }
      } else {
        kind = 2; zw = 0; zreg = rd(s, c, S0);
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
      /* Neither side falls through: give the conditional jump, the only one
         that costs a single branch, to the likelier side (see the arm64
         emit_block). */
      else if (!tmoves && !emoves && t->target != next_bi &&
               e->target != next_bi &&
               ir_succ_score(f, b, 1) > ir_succ_score(f, b, 0))
        invert = true;
      if (invert) {
        Edge *tmp = t; t = e; e = tmp;
        if (kind == 0) cc = xcc_invert(cc);
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
          if (kind == 0) cc = xcc_invert(cc); else kind = (kind == 1) ? 2 : 1;
        } else if (e_next && !t_next && !emoves && tmoves && s1 >= s0) {
          cold = t; cmv = tm; cn = tn;
        }
        if (cold) {
          struct EdgeStub *est = xmalloc(sizeof *est);
          est->n = cn;
          est->mv = xmalloc((cn ? (size_t)cn : 1) * sizeof(Move));
          memcpy(est->mv, cmv, (size_t)cn * sizeof(Move));
          est->target = cold->target;
          est->next = s->edge_stubs;
          s->edge_stubs = est;
          est->site = br_cc(s, kind, cc, zw, zreg);
          xfree(tm); xfree(em);
          break;
        }
      }

      if (!tmoves) {
        size_t bsite = br_cc(s, kind, cc, zw, zreg);
        add_fix(s, bsite, 0, t->target);
        parallel_copy(s, em, en);
        if (e->target != next_bi) {
          x64_jmp_rel32(B);
          add_fix(s, B->len, 0, e->target);
        }
      } else if (!emoves) {
        if (kind == 0) cc = xcc_invert(cc);
        else kind = (kind == 1) ? 2 : 1;
        size_t bsite = br_cc(s, kind, cc, zw, zreg);
        add_fix(s, bsite, 0, e->target);
        parallel_copy(s, tm, tn);
        if (t->target != next_bi) {
          x64_jmp_rel32(B);
          add_fix(s, B->len, 0, t->target);
        }
      } else {
        size_t site = br_cc(s, kind, cc, zw, zreg);
        parallel_copy(s, em, en);
        x64_jmp_rel32(B);
        add_fix(s, B->len, 0, e->target);
        br_patch(s, site, B->len);
        parallel_copy(s, tm, tn);
        x64_jmp_rel32(B);
        add_fix(s, B->len, 0, t->target);
      }
      xfree(tm); xfree(em);
      break;
    }
    case T_BRTABLE: {
      uint32_t n = b->nsuccs - 1;
      int idx = rd(s, b->term_arg, S0);
      x64_mov_rr(B, 0, S1, idx);
      x64_cmp_ri(B, 0, S1, (int32_t)n);
      Edge *de = &b->succs[n];
      size_t to_default = br_cc(s, 0, XCC_AE, 0, 0);
      bool default_moves = edge_has_moves(s, de);
      if (!default_moves) add_fix(s, to_default, 0, de->target);
      x64_lea(B, 1, TMP0, X64_RIPREL, X64_NOIDX, 0, 0);
      size_t lea_site = B->len;
      x64_load32sx(B, TMP1, TMP0, S1, 2, 0);
      x64_add_rr(B, 1, TMP0, TMP1);
      x64_jmp_r(B, TMP0);
      buf_align(B, 4);
      size_t table_base = B->len;
      x64_patch_rel32(B, lea_site - 4, table_base);
      for (uint32_t i = 0; i < n; i++) buf_u32(B, 0);
      /* As on arm64: an edge with no moves has its entry name the target. */
      for (uint32_t i = 0; i < n; i++) {
        Edge *e = &b->succs[i];
        if (!edge_has_moves(s, e)) {
          add_table_fix(s, table_base + 4 * i, table_base, e->target);
          continue;
        }
        size_t stub = B->len;
        uint32_t w = (uint32_t)(int32_t)(stub - table_base);
        memcpy(B->data + table_base + 4 * i, &w, 4);
        emit_edge(s, e);
        x64_jmp_rel32(B);
        add_fix(s, B->len, 0, e->target);
      }
      if (default_moves) {
        br_patch(s, to_default, B->len);
        emit_edge(s, de);
        x64_jmp_rel32(B);
        add_fix(s, B->len, 0, de->target);
      }
      break;
    }
    case T_RETURN: emit_return(s, b); break;
    case T_TRAP: emit_trap(s, (int)b->term_imm); break;
    default: warmc_fatal("isel: block b%u has no terminator", bi);
  }
}
