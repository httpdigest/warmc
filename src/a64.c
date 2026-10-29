#include "a64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void a64_word(Buf *b, uint32_t insn) { buf_u32(b, insn); }

__attribute__((noreturn))
static void enc_fail(const char *what, long long v) {
  fprintf(stderr, "warmc: internal: cannot encode %s = %lld\n", what, v);
  exit(1);
}
#define FITU(what, v, max) \
  ((uint64_t)(v) <= (uint64_t)(max) ? (uint32_t)(v) \
                                    : (enc_fail((what), (long long)(v)), 0u))
#define FITS(what, v, lo, hi) \
  ((int64_t)(v) >= (int64_t)(lo) && (int64_t)(v) <= (int64_t)(hi) \
     ? (uint32_t)(int32_t)(v) : (enc_fail((what), (long long)(v)), 0u))

#define W(x) a64_word(b, (uint32_t)(x))
#define M5(x)  FITU("register", (x), 31)
#define M12(x) FITU("12-bit immediate", (x), 0xfff)
#define M16(x) ((uint32_t)((x) & 0xffff))

static void mov_wide(Buf *b, int sf, int opc, int rd, uint16_t imm, int shift) {
  W(((uint32_t)sf << 31) | ((uint32_t)opc << 29) | (0x25u << 23) |
    ((uint32_t)(shift / 16) << 21) | ((uint32_t)M16(imm) << 5) | M5(rd));
}
void a64_movn(Buf *b, int sf, int rd, uint16_t imm, int shift) { mov_wide(b, sf, 0, rd, imm, shift); }
void a64_movz(Buf *b, int sf, int rd, uint16_t imm, int shift) { mov_wide(b, sf, 2, rd, imm, shift); }
void a64_movk(Buf *b, int sf, int rd, uint16_t imm, int shift) { mov_wide(b, sf, 3, rd, imm, shift); }

void a64_mov_reg(Buf *b, int sf, int rd, int rm) { a64_orr_reg(b, sf, rd, RZR, rm); }
void a64_mov_sp(Buf *b, int sf, int rd, int rn) { a64_add_imm(b, sf, rd, rn, 0, 0); }

void a64_load_imm(Buf *b, int sf, int rd, uint64_t value) {
  int nwords = sf ? 4 : 2;
  if (!sf) value &= 0xffffffffu;

  int zeros = 0, ones = 0;
  for (int i = 0; i < nwords; i++) {
    uint16_t w = (uint16_t)(value >> (16 * i));
    if (w != 0) zeros++;
    if (w != 0xffff) ones++;
  }
  if (zeros == 0) { a64_movz(b, sf, rd, 0, 0); return; }

  if (zeros <= ones) {
    int first = 1;
    for (int i = 0; i < nwords; i++) {
      uint16_t w = (uint16_t)(value >> (16 * i));
      if (w == 0) continue;
      if (first) { a64_movz(b, sf, rd, w, 16 * i); first = 0; }
      else a64_movk(b, sf, rd, w, 16 * i);
    }
  } else {
    int first = 1;
    for (int i = 0; i < nwords; i++) {
      uint16_t w = (uint16_t)(value >> (16 * i));
      if (first) {
        if (w == 0xffff) continue;
        a64_movn(b, sf, rd, (uint16_t)~w, 16 * i);
        first = 0;
      } else if (w != 0xffff) {
        a64_movk(b, sf, rd, w, 16 * i);
      }
    }
    if (first) a64_movn(b, sf, rd, 0, 0);
  }
}

static void addsub_imm(Buf *b, int sf, int op, int S, int rd, int rn,
                       uint32_t imm12, int lsl12) {
  if (S) b->flags_epoch++;
  W(((uint32_t)sf << 31) | ((uint32_t)op << 30) | ((uint32_t)S << 29) |
    (0x22u << 23) | ((uint32_t)(lsl12 ? 1 : 0) << 22) | (M12(imm12) << 10) |
    (M5(rn) << 5) | M5(rd));
}
void a64_add_imm(Buf *b, int sf, int rd, int rn, uint32_t i, int l)  { addsub_imm(b, sf, 0, 0, rd, rn, i, l); }
void a64_sub_imm(Buf *b, int sf, int rd, int rn, uint32_t i, int l)  { addsub_imm(b, sf, 1, 0, rd, rn, i, l); }
void a64_adds_imm(Buf *b, int sf, int rd, int rn, uint32_t i, int l) { addsub_imm(b, sf, 0, 1, rd, rn, i, l); }
void a64_subs_imm(Buf *b, int sf, int rd, int rn, uint32_t i, int l) { addsub_imm(b, sf, 1, 1, rd, rn, i, l); }

static void addsub_reg(Buf *b, int sf, int op, int S, int rd, int rn, int rm,
                       int shift_kind, int amount) {
  if (S) b->flags_epoch++;
  W(((uint32_t)sf << 31) | ((uint32_t)op << 30) | ((uint32_t)S << 29) |
    (0x0Bu << 24) | ((uint32_t)shift_kind << 22) | (M5(rm) << 16) |
    ((uint32_t)(amount & 0x3f) << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_add_reg(Buf *b, int sf, int rd, int rn, int rm)  { addsub_reg(b, sf, 0, 0, rd, rn, rm, 0, 0); }
void a64_sub_reg(Buf *b, int sf, int rd, int rn, int rm)  { addsub_reg(b, sf, 1, 0, rd, rn, rm, 0, 0); }
void a64_adds_reg(Buf *b, int sf, int rd, int rn, int rm) { addsub_reg(b, sf, 0, 1, rd, rn, rm, 0, 0); }
void a64_subs_reg(Buf *b, int sf, int rd, int rn, int rm) { addsub_reg(b, sf, 1, 1, rd, rn, rm, 0, 0); }

/* adcs and sbcs: rn + rm + C, rn - rm - !C, setting the flags */
static void addsub_carry(Buf *b, int sf, int op, int rd, int rn, int rm) {
  b->flags_epoch++;
  W(((uint32_t)sf << 31) | ((uint32_t)op << 30) | (1u << 29) | (0xD0u << 21) |
    (M5(rm) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_adcs(Buf *b, int sf, int rd, int rn, int rm) { addsub_carry(b, sf, 0, rd, rn, rm); }
void a64_sbcs(Buf *b, int sf, int rd, int rn, int rm) { addsub_carry(b, sf, 1, rd, rn, rm); }
void a64_add_reg_lsl(Buf *b, int sf, int rd, int rn, int rm, int sh) {
  addsub_reg(b, sf, 0, 0, rd, rn, rm, 0, sh);
}
void a64_sub_reg_lsl(Buf *b, int sf, int rd, int rn, int rm, int sh) {
  addsub_reg(b, sf, 1, 0, rd, rn, rm, 0, sh);
}
void a64_add_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int sh) {
  addsub_reg(b, sf, 0, 0, rd, rn, rm, kind, sh);
}
void a64_sub_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int sh) {
  addsub_reg(b, sf, 1, 0, rd, rn, rm, kind, sh);
}
void a64_add_uxtw(Buf *b, int rd, int rn, int rm, int shift) {

  W((1u << 31) | (0x0Bu << 24) | (1u << 21) | (M5(rm) << 16) |
    (2u << 13) | ((uint32_t)(shift & 7) << 10) | (M5(rn) << 5) | M5(rd));
}

/* 64-bit ADD/SUB (extended register) of a W register: rd = rn +/- (uxtw or
   sxtw of wm) << shift.  Register 31 is sp, not xzr, in rd and rn here. */
void a64_addsub_extw(Buf *b, int sub, int rd, int rn, int rm, int sxtw, int shift) {
  W((1u << 31) | ((uint32_t)(sub != 0) << 30) | (0x0Bu << 24) | (1u << 21) |
    (M5(rm) << 16) | ((sxtw ? 6u : 2u) << 13) | ((uint32_t)(shift & 7) << 10) |
    (M5(rn) << 5) | M5(rd));
}

void a64_add_extx(Buf *b, int rd, int rn, int rm, int shift) {
  W((1u << 31) | (0x0Bu << 24) | (1u << 21) | (M5(rm) << 16) |
    (3u << 13) | ((uint32_t)(shift & 7) << 10) | (M5(rn) << 5) | M5(rd));
}

void a64_cmp_uxtw(Buf *b, int rn, int rm, int shift) {
  b->flags_epoch++;
  W((1u << 31) | (1u << 30) | (1u << 29) | (0x0Bu << 24) | (1u << 21) |
    (M5(rm) << 16) | (2u << 13) | ((uint32_t)(shift & 7) << 10) |
    (M5(rn) << 5) | M5(RZR));
}

/* cmp sp, xm: the extended-register form, the one whose rn may be sp. */
void a64_cmp_sp(Buf *b, int rm) {
  b->flags_epoch++;
  W((1u << 31) | (1u << 30) | (1u << 29) | (0x0Bu << 24) | (1u << 21) |
    (M5(rm) << 16) | (3u << 13) | (31u << 5) | M5(RZR));
}
void a64_neg(Buf *b, int sf, int rd, int rm) { a64_sub_reg(b, sf, rd, RZR, rm); }
void a64_cmp_reg(Buf *b, int sf, int rn, int rm) { a64_subs_reg(b, sf, RZR, rn, rm); }
void a64_cmp_imm(Buf *b, int sf, int rn, uint32_t imm12) { a64_subs_imm(b, sf, RZR, rn, imm12, 0); }

/* ccmp and ccmn: where cond holds, the flags of rn - x (rn + x), x the
   5-bit immediate or rm; elsewhere nzcv. */
static void condcmp(Buf *b, int sf, int op, int rn, uint32_t x, int imm, int nzcv, int cond) {
  b->flags_epoch++;
  W(((uint32_t)sf << 31) | ((uint32_t)op << 30) | (1u << 29) | (0xD2u << 21) |
    ((x & 0x1fu) << 16) | ((uint32_t)(cond & 0xf) << 12) | ((uint32_t)imm << 11) |
    (M5(rn) << 5) | (uint32_t)(nzcv & 0xf));
}
void a64_ccmp_imm(Buf *b, int sf, int rn, uint32_t imm5, int nzcv, int cond) {
  condcmp(b, sf, 1, rn, imm5, 1, nzcv, cond);
}
void a64_ccmn_imm(Buf *b, int sf, int rn, uint32_t imm5, int nzcv, int cond) {
  condcmp(b, sf, 0, rn, imm5, 1, nzcv, cond);
}
void a64_ccmp_reg(Buf *b, int sf, int rn, int rm, int nzcv, int cond) {
  condcmp(b, sf, 1, rn, (uint32_t)rm, 0, nzcv, cond);
}

static void dp3(Buf *b, int sf, int op31, int rd, int rn, int rm, int o0, int ra) {
  W(((uint32_t)sf << 31) | (0x1Bu << 24) | ((uint32_t)op31 << 21) |
    (M5(rm) << 16) | ((uint32_t)o0 << 15) | (M5(ra) << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_madd(Buf *b, int sf, int rd, int rn, int rm, int ra) { dp3(b, sf, 0, rd, rn, rm, 0, ra); }
void a64_msub(Buf *b, int sf, int rd, int rn, int rm, int ra) { dp3(b, sf, 0, rd, rn, rm, 1, ra); }
void a64_mul(Buf *b, int sf, int rd, int rn, int rm) { a64_madd(b, sf, rd, rn, rm, RZR); }

void a64_umulh(Buf *b, int rd, int rn, int rm) { dp3(b, 1, 6, rd, rn, rm, 0, RZR); }
void a64_smulh(Buf *b, int rd, int rn, int rm) { dp3(b, 1, 2, rd, rn, rm, 0, RZR); }
void a64_umull(Buf *b, int rd, int rn, int rm) { dp3(b, 1, 5, rd, rn, rm, 0, RZR); }
void a64_smull(Buf *b, int rd, int rn, int rm) { dp3(b, 1, 1, rd, rn, rm, 0, RZR); }

static void dp2(Buf *b, int sf, int opcode, int rd, int rn, int rm) {
  W(((uint32_t)sf << 31) | (0xD6u << 21) | (M5(rm) << 16) |
    ((uint32_t)opcode << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_udiv(Buf *b, int sf, int rd, int rn, int rm) { dp2(b, sf, 0x02, rd, rn, rm); }
void a64_sdiv(Buf *b, int sf, int rd, int rn, int rm) { dp2(b, sf, 0x03, rd, rn, rm); }
void a64_lslv(Buf *b, int sf, int rd, int rn, int rm) { dp2(b, sf, 0x08, rd, rn, rm); }
void a64_lsrv(Buf *b, int sf, int rd, int rn, int rm) { dp2(b, sf, 0x09, rd, rn, rm); }
void a64_asrv(Buf *b, int sf, int rd, int rn, int rm) { dp2(b, sf, 0x0a, rd, rn, rm); }
void a64_rorv(Buf *b, int sf, int rd, int rn, int rm) { dp2(b, sf, 0x0b, rd, rn, rm); }

static void dp1(Buf *b, int sf, int opcode, int rd, int rn) {
  W(((uint32_t)sf << 31) | (1u << 30) | (0xD6u << 21) |
    ((uint32_t)opcode << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_rbit(Buf *b, int sf, int rd, int rn) { dp1(b, sf, 0x00, rd, rn); }
void a64_clz(Buf *b, int sf, int rd, int rn)  { dp1(b, sf, 0x04, rd, rn); }

void a64_rev(Buf *b, int sf, int rd, int rn)  { dp1(b, sf, sf ? 0x03 : 0x02, rd, rn); }

static void log_reg(Buf *b, int sf, int opc, int N, int rd, int rn, int rm,
                    int shift_kind, int amount) {
  if (opc == 3) b->flags_epoch++;
  W(((uint32_t)sf << 31) | ((uint32_t)opc << 29) | (0x0Au << 24) |
    ((uint32_t)shift_kind << 22) | ((uint32_t)N << 21) | (M5(rm) << 16) |
    ((uint32_t)(amount & 0x3f) << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_and_reg(Buf *b, int sf, int rd, int rn, int rm)  { log_reg(b, sf, 0, 0, rd, rn, rm, 0, 0); }
void a64_orr_reg(Buf *b, int sf, int rd, int rn, int rm)  { log_reg(b, sf, 1, 0, rd, rn, rm, 0, 0); }
void a64_eor_reg(Buf *b, int sf, int rd, int rn, int rm)  { log_reg(b, sf, 2, 0, rd, rn, rm, 0, 0); }
void a64_ands_reg(Buf *b, int sf, int rd, int rn, int rm) { log_reg(b, sf, 3, 0, rd, rn, rm, 0, 0); }
void a64_mvn(Buf *b, int sf, int rd, int rm) { log_reg(b, sf, 1, 1, rd, RZR, rm, 0, 0); }

void a64_bic_reg(Buf *b, int sf, int rd, int rn, int rm) { log_reg(b, sf, 0, 1, rd, rn, rm, 0, 0); }
void a64_orn_reg(Buf *b, int sf, int rd, int rn, int rm) { log_reg(b, sf, 1, 1, rd, rn, rm, 0, 0); }
void a64_eon_reg(Buf *b, int sf, int rd, int rn, int rm) { log_reg(b, sf, 2, 1, rd, rn, rm, 0, 0); }
void a64_tst_reg(Buf *b, int sf, int rn, int rm) { a64_ands_reg(b, sf, RZR, rn, rm); }
void a64_and_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int amount) {
  log_reg(b, sf, 0, 0, rd, rn, rm, kind, amount);
}
void a64_orr_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int amount) {
  log_reg(b, sf, 1, 0, rd, rn, rm, kind, amount);
}
void a64_eor_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int amount) {
  log_reg(b, sf, 2, 0, rd, rn, rm, kind, amount);
}

static int ctz64(uint64_t v) { int n = 0; while (!(v & 1)) { v >>= 1; n++; } return n; }
static int cto64(uint64_t v) { int n = 0; while (v & 1) { v >>= 1; n++; } return n; }
static int clo64(uint64_t v) { int n = 0; while (v >> 63) { v <<= 1; n++; } return n; }
static bool is_shifted_mask(uint64_t v) { return v && (((v - 1) | v) == (((v - 1) | v) & -(int64_t)1)) && ((((v - 1) | v) + 1) & ((v - 1) | v)) == 0; }

bool a64_logical_imm(uint64_t imm, int sf, uint32_t *encoding) {
  unsigned regsize = sf ? 64 : 32;
  if (!sf) imm &= 0xffffffffu;
  if (imm == 0 || imm == (regsize == 64 ? ~0ull : 0xffffffffu)) return false;
  if (!sf && (imm >> 32) != 0) return false;

  unsigned size = regsize;
  for (;;) {
    size /= 2;
    uint64_t mask = (size == 64) ? ~0ull : ((1ull << size) - 1);
    if ((imm & mask) != ((imm >> size) & mask)) { size *= 2; break; }
    if (size <= 2) break;
  }
  uint64_t mask = (size == 64) ? ~0ull : (~0ull >> (64 - size));
  uint64_t v = imm & mask;
  unsigned rotate, cto;
  if (is_shifted_mask(v)) {
    unsigned i = (unsigned)ctz64(v);
    cto = (unsigned)cto64(v >> i);
    rotate = i;
  } else {
    v |= ~mask;
    if (!is_shifted_mask(~v)) return false;
    unsigned clo = (unsigned)clo64(v);
    rotate = 64 - clo;
    cto = clo + (unsigned)cto64(v) - (64 - size);
  }
  unsigned immr = (size - rotate) & (size - 1);
  unsigned nimms = (~(size - 1)) << 1;
  nimms |= (cto - 1);
  unsigned n = ((nimms >> 6) & 1) ^ 1;
  *encoding = ((uint32_t)n << 12) | ((uint32_t)immr << 6) | (nimms & 0x3f);
  return true;
}

static void log_imm(Buf *b, int sf, int opc, int rd, int rn, uint32_t enc) {
  if (opc == 3) b->flags_epoch++;
  uint32_t n = (enc >> 12) & 1, immr = (enc >> 6) & 0x3f, imms = enc & 0x3f;
  W(((uint32_t)sf << 31) | ((uint32_t)opc << 29) | (0x24u << 23) | (n << 22) |
    (immr << 16) | (imms << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_and_imm(Buf *b, int sf, int rd, int rn, uint32_t e) { log_imm(b, sf, 0, rd, rn, e); }
void a64_orr_imm(Buf *b, int sf, int rd, int rn, uint32_t e) { log_imm(b, sf, 1, rd, rn, e); }
void a64_eor_imm(Buf *b, int sf, int rd, int rn, uint32_t e) { log_imm(b, sf, 2, rd, rn, e); }
void a64_ands_imm(Buf *b, int sf, int rd, int rn, uint32_t e) { log_imm(b, sf, 3, rd, rn, e); }
void a64_tst_imm(Buf *b, int sf, int rn, uint32_t e) { a64_ands_imm(b, sf, RZR, rn, e); }

/* EXTR: rd = the low half of the double-width rn:rm shifted right by lsb,
   (rm >> lsb) | (rn << (width - lsb)).  ROR by an immediate is rn == rm. */
void a64_extr(Buf *b, int sf, int rd, int rn, int rm, int lsb) {
  W(((uint32_t)sf << 31) | (0x27u << 23) | ((uint32_t)sf << 22) |
    (M5(rm) << 16) | ((uint32_t)(lsb & 0x3f) << 10) | (M5(rn) << 5) | M5(rd));
}

void a64_ror_imm(Buf *b, int sf, int rd, int rn, int shift) {

  W(((uint32_t)sf << 31) | (0x27u << 23) | ((uint32_t)sf << 22) |
    (M5(rn) << 16) | ((uint32_t)(shift & 0x3f) << 10) | (M5(rn) << 5) | M5(rd));
}

static void bfm(Buf *b, int sf, int opc, int rd, int rn, int immr, int imms) {
  W(((uint32_t)sf << 31) | ((uint32_t)opc << 29) | (0x26u << 23) |
    ((uint32_t)sf << 22) | ((uint32_t)(immr & 0x3f) << 16) |
    ((uint32_t)(imms & 0x3f) << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_sbfm(Buf *b, int sf, int rd, int rn, int immr, int imms) { bfm(b, sf, 0, rd, rn, immr, imms); }
void a64_ubfm(Buf *b, int sf, int rd, int rn, int immr, int imms) { bfm(b, sf, 2, rd, rn, immr, imms); }
void a64_bfm(Buf *b, int sf, int rd, int rn, int immr, int imms) { bfm(b, sf, 1, rd, rn, immr, imms); }

void a64_lsl_imm(Buf *b, int sf, int rd, int rn, int sh) {
  int width = sf ? 64 : 32;
  if (sh == 0) { a64_mov_reg(b, sf, rd, rn); return; }
  a64_ubfm(b, sf, rd, rn, (width - sh) % width, width - 1 - sh);
}
void a64_lsr_imm(Buf *b, int sf, int rd, int rn, int sh) {
  int width = sf ? 64 : 32;
  if (sh == 0) { a64_mov_reg(b, sf, rd, rn); return; }
  a64_ubfm(b, sf, rd, rn, sh, width - 1);
}
void a64_asr_imm(Buf *b, int sf, int rd, int rn, int sh) {
  int width = sf ? 64 : 32;
  if (sh == 0) { a64_mov_reg(b, sf, rd, rn); return; }
  a64_sbfm(b, sf, rd, rn, sh, width - 1);
}
void a64_sxtb(Buf *b, int sf, int rd, int rn) { a64_sbfm(b, sf, rd, rn, 0, 7); }
void a64_sxth(Buf *b, int sf, int rd, int rn) { a64_sbfm(b, sf, rd, rn, 0, 15); }
void a64_sxtw(Buf *b, int rd, int rn) { a64_sbfm(b, SF64, rd, rn, 0, 31); }
void a64_uxtb(Buf *b, int rd, int rn) { a64_ubfm(b, SF32, rd, rn, 0, 7); }
void a64_uxth(Buf *b, int rd, int rn) { a64_ubfm(b, SF32, rd, rn, 0, 15); }
void a64_uxtw(Buf *b, int rd, int rn) { a64_mov_reg(b, SF32, rd, rn); }

static void condsel(Buf *b, int sf, int op, int op2, int rd, int rn, int rm, int cond) {
  W(((uint32_t)sf << 31) | ((uint32_t)op << 30) | (0xD4u << 21) |
    (M5(rm) << 16) | ((uint32_t)(cond & 0xf) << 12) | ((uint32_t)op2 << 10) |
    (M5(rn) << 5) | M5(rd));
}
void a64_csel(Buf *b, int sf, int rd, int rn, int rm, int cond)  { condsel(b, sf, 0, 0, rd, rn, rm, cond); }
void a64_csinc(Buf *b, int sf, int rd, int rn, int rm, int cond) { condsel(b, sf, 0, 1, rd, rn, rm, cond); }
void a64_csinv(Buf *b, int sf, int rd, int rn, int rm, int cond) { condsel(b, sf, 1, 0, rd, rn, rm, cond); }
void a64_csneg(Buf *b, int sf, int rd, int rn, int rm, int cond) { condsel(b, sf, 1, 1, rd, rn, rm, cond); }
void a64_cset(Buf *b, int sf, int rd, int cond) { a64_csinc(b, sf, rd, RZR, RZR, cc_invert(cond)); }
void a64_csetm(Buf *b, int sf, int rd, int cond) { a64_csinv(b, sf, rd, RZR, RZR, cc_invert(cond)); }

static void ldst_uimm(Buf *b, int size, int V, int opc, int rt, int rn, uint32_t off) {

  /* An offset the scaled form cannot carry, not a multiple of the size, is
     the unscaled form's (ldur and its kin) when it is below 256. */
  if (off & ((1u << size) - 1u)) {
    uint32_t imm9 = FITU("unscaled load/store offset", off, 0xff);
    W(((uint32_t)size << 30) | (0x7u << 27) | ((uint32_t)V << 26) |
      ((uint32_t)opc << 22) | (imm9 << 12) | (M5(rn) << 5) | M5(rt));
    return;
  }
  uint32_t scaled = FITU("scaled load/store offset", off >> size, 0xfff);
  W(((uint32_t)size << 30) | (0x7u << 27) | ((uint32_t)V << 26) | (1u << 24) |
    ((uint32_t)opc << 22) | (scaled << 10) | (M5(rn) << 5) | M5(rt));
}
void a64_str_imm(Buf *b, int size, int rt, int rn, uint32_t off) { ldst_uimm(b, size, 0, 0, rt, rn, off); }
void a64_ldr_imm(Buf *b, int size, int rt, int rn, uint32_t off) { ldst_uimm(b, size, 0, 1, rt, rn, off); }
void a64_ldrs_imm(Buf *b, int size, int dst64, int rt, int rn, uint32_t off) {
  ldst_uimm(b, size, 0, dst64 ? 2 : 3, rt, rn, off);
}

void a64_fstr_imm(Buf *b, int size, int rt, int rn, uint32_t off) {
  if (size == 4) { a64_str_q(b, rt, rn, off); return; }
  ldst_uimm(b, size, 1, 0, rt, rn, off);
}
void a64_fldr_imm(Buf *b, int size, int rt, int rn, uint32_t off) {
  if (size == 4) { a64_ldr_q(b, rt, rn, off); return; }
  ldst_uimm(b, size, 1, 1, rt, rn, off);
}

static void ldst_reg(Buf *b, int size, int V, int opc, int rt, int rn, int rm,
                     int option, int s) {
  W(((uint32_t)size << 30) | (0x7u << 27) | ((uint32_t)V << 26) |
    ((uint32_t)opc << 22) | (1u << 21) | (M5(rm) << 16) |
    ((uint32_t)option << 13) | ((uint32_t)s << 12) | (2u << 10) |
    (M5(rn) << 5) | M5(rt));
}
void a64_str_reg(Buf *b, int size, int rt, int rn, int rm, int o, int s) { ldst_reg(b, size, 0, 0, rt, rn, rm, o, s); }
void a64_ldr_reg(Buf *b, int size, int rt, int rn, int rm, int o, int s) { ldst_reg(b, size, 0, 1, rt, rn, rm, o, s); }
void a64_ldrs_reg(Buf *b, int size, int dst64, int rt, int rn, int rm, int o, int s) {
  ldst_reg(b, size, 0, dst64 ? 2 : 3, rt, rn, rm, o, s);
}
void a64_fstr_reg(Buf *b, int size, int rt, int rn, int rm, int o, int s) {
  if (size == 4) { a64_str_q_reg(b, rt, rn, rm, o, s); return; }
  ldst_reg(b, size, 1, 0, rt, rn, rm, o, s);
}
void a64_fldr_reg(Buf *b, int size, int rt, int rn, int rm, int o, int s) {
  if (size == 4) { a64_ldr_q_reg(b, rt, rn, rm, o, s); return; }
  ldst_reg(b, size, 1, 1, rt, rn, rm, o, s);
}

static void ldst_pair(Buf *b, int sf, int L, int rt, int rt2, int rn, int off, int mode) {
  int scale = sf ? 8 : 4;
  if (off % scale) enc_fail("unaligned pair offset", off);
  uint32_t imm7 = FITS("pair offset", off / scale, -64, 63) & 0x7f;
  W(((uint32_t)(sf ? 2 : 0) << 30) | (0x5u << 27) | ((uint32_t)mode << 23) |
    ((uint32_t)L << 22) | (imm7 << 15) | (M5(rt2) << 10) |
    (M5(rn) << 5) | M5(rt));
}
void a64_stp(Buf *b, int sf, int rt, int rt2, int rn, int off, int mode) { ldst_pair(b, sf, 0, rt, rt2, rn, off, mode); }
void a64_ldp(Buf *b, int sf, int rt, int rt2, int rn, int off, int mode) { ldst_pair(b, sf, 1, rt, rt2, rn, off, mode); }

static void ldst_pair_v(Buf *b, int ft, int L, int rt, int rt2, int rn, int off,
                        int mode) {
  int scale = ft == FT_S ? 4 : 8;
  if (off % scale) enc_fail("unaligned pair offset", off);
  uint32_t imm7 = FITS("pair offset", off / scale, -64, 63) & 0x7f;
  W(((uint32_t)(ft == FT_S ? 0 : 1) << 30) | (0x5u << 27) | (1u << 26) |
    ((uint32_t)mode << 23) | ((uint32_t)L << 22) | (imm7 << 15) |
    (M5(rt2) << 10) | (M5(rn) << 5) | M5(rt));
}
void a64_fstp(Buf *b, int ft, int rt, int rt2, int rn, int off, int mode) { ldst_pair_v(b, ft, 0, rt, rt2, rn, off, mode); }
void a64_fldp(Buf *b, int ft, int rt, int rt2, int rn, int off, int mode) { ldst_pair_v(b, ft, 1, rt, rt2, rn, off, mode); }

static void ldst_unscaled(Buf *b, int size, int opc, int rt, int rn, int off) {
  uint32_t imm9 = FITS("unscaled offset", off, -256, 255) & 0x1ff;
  W(((uint32_t)size << 30) | (0x7u << 27) | ((uint32_t)opc << 22) |
    (imm9 << 12) | (M5(rn) << 5) | M5(rt));
}
void a64_stur(Buf *b, int size, int rt, int rn, int off) { ldst_unscaled(b, size, 0, rt, rn, off); }
void a64_ldur(Buf *b, int size, int rt, int rn, int off) { ldst_unscaled(b, size, 1, rt, rn, off); }

#define B26_LO (-(1 << 25))
#define B26_HI ((1 << 25) - 1)
#define B19_LO (-(1 << 18))
#define B19_HI ((1 << 18) - 1)

#define B14_LO (-(1 << 13))
#define B14_HI ((1 << 13) - 1)

void a64_b(Buf *b, int32_t imm26) {
  W((0x05u << 26) | (FITS("branch target", imm26, B26_LO, B26_HI) & 0x3ffffff));
}
void a64_bl(Buf *b, int32_t imm26) {
  b->flags_epoch++;
  W((0x25u << 26) | (FITS("call target", imm26, B26_LO, B26_HI) & 0x3ffffff));
}
void a64_bcond(Buf *b, int cond, int32_t imm19) {
  W((0x54u << 24) |
    ((FITS("conditional branch target", imm19, B19_LO, B19_HI) & 0x7ffff) << 5) |
    (uint32_t)(cond & 0xf));
}
static void cmpbr(Buf *b, int sf, int op, int rt, int32_t imm19) {
  W(((uint32_t)sf << 31) | (0x1Au << 25) | ((uint32_t)op << 24) |
    ((FITS("compare-and-branch target", imm19, B19_LO, B19_HI) & 0x7ffff) << 5) |
    M5(rt));
}
void a64_cbz(Buf *b, int sf, int rt, int32_t imm19)  { cmpbr(b, sf, 0, rt, imm19); }
void a64_cbnz(Buf *b, int sf, int rt, int32_t imm19) { cmpbr(b, sf, 1, rt, imm19); }

static void tstbr(Buf *b, int op, int rt, int bit, int32_t imm14) {
  W(((uint32_t)(bit >> 5) << 31) | (0x1Bu << 25) | ((uint32_t)op << 24) |
    ((uint32_t)(bit & 31) << 19) |
    ((FITS("test-and-branch target", imm14, B14_LO, B14_HI) & 0x3fff) << 5) |
    M5(rt));
}
void a64_tbz(Buf *b, int rt, int bit, int32_t imm14)  { tstbr(b, 0, rt, bit, imm14); }
void a64_tbnz(Buf *b, int rt, int bit, int32_t imm14) { tstbr(b, 1, rt, bit, imm14); }
void a64_br(Buf *b, int rn)  { W(0xD61F0000u | (M5(rn) << 5)); }
void a64_blr(Buf *b, int rn) { b->flags_epoch++; W(0xD63F0000u | (M5(rn) << 5)); }
void a64_ret(Buf *b)         { W(0xD65F03C0u); }
void a64_brk(Buf *b, uint16_t imm) { W(0xD4200000u | ((uint32_t)imm << 5)); }
void a64_nop(Buf *b)         { W(0xD503201Fu); }
void a64_adr(Buf *b, int rd, int32_t imm21) {
  W(((uint32_t)(imm21 & 3) << 29) | (0x10u << 24) |
    (((uint32_t)(imm21 >> 2) & 0x7ffff) << 5) | M5(rd));
}
void a64_adrp(Buf *b, int rd, int32_t imm21) {
  W((1u << 31) | ((uint32_t)(imm21 & 3) << 29) | (0x10u << 24) |
    (((uint32_t)(imm21 >> 2) & 0x7ffff) << 5) | M5(rd));
}

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
void a64_patch_b26(Buf *b, size_t site, size_t target) {
  int64_t delta = ((int64_t)target - (int64_t)site) >> 2;
  uint32_t f = FITS("branch displacement", delta, B26_LO, B26_HI);
  uint32_t w = rd32(b->data + site);
  wr32(b->data + site, (w & ~0x3ffffffu) | (f & 0x3ffffffu));
}
void a64_patch_b19(Buf *b, size_t site, size_t target) {
  int64_t delta = ((int64_t)target - (int64_t)site) >> 2;
  uint32_t f = FITS("conditional branch displacement", delta, B19_LO, B19_HI);
  uint32_t w = rd32(b->data + site);
  wr32(b->data + site, (w & ~(0x7ffffu << 5)) | ((f & 0x7ffffu) << 5));
}

bool a64_patch_b19_ok(Buf *b, size_t site, size_t target) {
  int64_t delta = ((int64_t)target - (int64_t)site) >> 2;
  if (delta < B19_LO || delta > B19_HI) return false;
  uint32_t w = rd32(b->data + site);
  wr32(b->data + site,
       (w & ~(0x7ffffu << 5)) | (((uint32_t)(int32_t)delta & 0x7ffffu) << 5));
  return true;
}

bool a64_patch_b26_ok(Buf *b, size_t site, size_t target) {
  int64_t delta = ((int64_t)target - (int64_t)site) >> 2;
  if (delta < B26_LO || delta > B26_HI) return false;
  uint32_t w = rd32(b->data + site);
  wr32(b->data + site, (w & ~0x3ffffffu) | ((uint32_t)(int32_t)delta & 0x3ffffffu));
  return true;
}

void a64_ldr_lit_fp(Buf *b, int ft, int rt) {
  W((ft == FT_S ? 0x1c000000u : 0x5c000000u) | M5(rt));
}
bool a64_patch_ldr_lit(Buf *b, size_t site, size_t target) {
  int64_t delta = ((int64_t)target - (int64_t)site) >> 2;
  if (delta < B19_LO || delta > B19_HI) return false;
  uint32_t w = rd32(b->data + site);
  wr32(b->data + site,
       (w & ~(0x7ffffu << 5)) | (((uint32_t)(int32_t)delta & 0x7ffffu) << 5));
  return true;
}
void a64_patch_b14(Buf *b, size_t site, size_t target) {
  int64_t delta = ((int64_t)target - (int64_t)site) >> 2;
  uint32_t f = FITS("test-and-branch displacement", delta, B14_LO, B14_HI);
  uint32_t w = rd32(b->data + site);
  wr32(b->data + site, (w & ~(0x3fffu << 5)) | ((f & 0x3fffu) << 5));
}
void a64_patch_adr(Buf *b, size_t site, size_t target) {
  int64_t delta = (int64_t)target - (int64_t)site;
  FITS("adr displacement", delta, -(1 << 20), (1 << 20) - 1);
  uint32_t w = rd32(b->data + site);
  w &= ~((3u << 29) | (0x7ffffu << 5));
  w |= ((uint32_t)delta & 3) << 29;
  w |= (((uint32_t)(delta >> 2)) & 0x7ffffu) << 5;
  wr32(b->data + site, w);
}

static void fp2(Buf *b, int ft, int opcode, int rd, int rn, int rm) {
  W((0x1Eu << 24) | ((uint32_t)ft << 22) | (1u << 21) | (M5(rm) << 16) |
    ((uint32_t)opcode << 12) | (2u << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_fmul(Buf *b, int ft, int rd, int rn, int rm) { fp2(b, ft, 0x0, rd, rn, rm); }
void a64_fdiv(Buf *b, int ft, int rd, int rn, int rm) { fp2(b, ft, 0x1, rd, rn, rm); }
void a64_fadd(Buf *b, int ft, int rd, int rn, int rm) { fp2(b, ft, 0x2, rd, rn, rm); }
/* rd = ra + rn * rm rounded once (form 0, FMADD); ra - rn * rm (1, FMSUB);
   rn * rm - ra (2, FNMSUB) */
void a64_fmadd(Buf *b, int ft, int form, int rd, int rn, int rm, int ra) {
  uint32_t o1 = form == 2, o0 = form != 0;
  W((0x1Fu << 24) | ((uint32_t)ft << 22) | (o1 << 21) | (M5(rm) << 16) |
    (o0 << 15) | (M5(ra) << 10) | (M5(rn) << 5) | M5(rd));
}
void a64_fsub(Buf *b, int ft, int rd, int rn, int rm) { fp2(b, ft, 0x3, rd, rn, rm); }
void a64_fmax(Buf *b, int ft, int rd, int rn, int rm) { fp2(b, ft, 0x4, rd, rn, rm); }
void a64_fmin(Buf *b, int ft, int rd, int rn, int rm) { fp2(b, ft, 0x5, rd, rn, rm); }

static void fp1(Buf *b, int ft, int opcode, int rd, int rn) {
  W((0x1Eu << 24) | ((uint32_t)ft << 22) | (1u << 21) |
    ((uint32_t)opcode << 15) | (0x10u << 10) | (M5(rn) << 5) | M5(rd));
}

bool a64_fmov_imm(uint64_t bits, int ft, uint32_t *imm8) {
  uint64_t sign, exp, frac;
  int ebias_lo, ebias_hi;
  if (ft == FT_D) {
    if (bits & 0x0000ffffffffffffull) return false;
    sign = (bits >> 63) & 1; exp = (bits >> 52) & 0x7ff; frac = (bits >> 48) & 0xf;
    ebias_lo = 0x3fc; ebias_hi = 0x403;
  } else {
    bits &= 0xffffffffu;
    if (bits & 0x0007ffffu) return false;
    sign = (bits >> 31) & 1; exp = (bits >> 23) & 0xff; frac = (bits >> 19) & 0xf;
    ebias_lo = 0x7c; ebias_hi = 0x83;
  }
  if (exp < (uint64_t)ebias_lo || exp > (uint64_t)ebias_hi) return false;

  uint32_t b_bit = (exp <= (uint64_t)ebias_lo + 3) ? 1u : 0u;
  uint32_t cd = (uint32_t)(exp & 3u);
  *imm8 = (uint32_t)(sign << 7) | (b_bit << 6) | (cd << 4) | (uint32_t)frac;
  return true;
}

void a64_fmov_imm_emit(Buf *b, int ft, int rd, uint32_t imm8) {
  W((0x1Eu << 24) | ((uint32_t)ft << 22) | (1u << 21) |
    ((imm8 & 0xffu) << 13) | (4u << 10) | M5(rd));
}

void a64_fmov_reg(Buf *b, int ft, int rd, int rn) { fp1(b, ft, 0x00, rd, rn); }
void a64_fabs(Buf *b, int ft, int rd, int rn)     { fp1(b, ft, 0x01, rd, rn); }
void a64_fneg(Buf *b, int ft, int rd, int rn)     { fp1(b, ft, 0x02, rd, rn); }
void a64_fsqrt(Buf *b, int ft, int rd, int rn)    { fp1(b, ft, 0x03, rd, rn); }
void a64_fcvt(Buf *b, int dst_ft, int src_ft, int rd, int rn) {

  int dst_code = (dst_ft == FT_D) ? 1 : 0;
  fp1(b, src_ft, 0x04 | dst_code, rd, rn);
}
void a64_frint(Buf *b, int ft, int rd, int rn, int mode) {
  static const int op[4] = { 0x08, 0x09, 0x0a, 0x0b };
  fp1(b, ft, op[mode], rd, rn);
}
void a64_fcmp(Buf *b, int ft, int rn, int rm) {
  b->flags_epoch++;
  W((0x1Eu << 24) | ((uint32_t)ft << 22) | (1u << 21) | (M5(rm) << 16) |
    (0x8u << 10) | (M5(rn) << 5) | 0);
}
void a64_fcsel(Buf *b, int ft, int rd, int rn, int rm, int cond) {
  W((0x1Eu << 24) | ((uint32_t)ft << 22) | (1u << 21) | (M5(rm) << 16) |
    ((uint32_t)(cond & 0xf) << 12) | (3u << 10) | (M5(rn) << 5) | M5(rd));
}

static void fpint(Buf *b, int sf, int ft, int rmode, int opcode, int rd, int rn) {
  W(((uint32_t)sf << 31) | (0x1Eu << 24) | ((uint32_t)ft << 22) | (1u << 21) |
    ((uint32_t)rmode << 19) | ((uint32_t)opcode << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_fcvtzs(Buf *b, int sf, int ft, int rd, int rn) { fpint(b, sf, ft, 3, 0, rd, rn); }
void a64_fcvtzu(Buf *b, int sf, int ft, int rd, int rn) { fpint(b, sf, ft, 3, 1, rd, rn); }
void a64_scvtf(Buf *b, int sf, int ft, int rd, int rn)  { fpint(b, sf, ft, 0, 2, rd, rn); }
void a64_ucvtf(Buf *b, int sf, int ft, int rd, int rn)  { fpint(b, sf, ft, 0, 3, rd, rn); }
void a64_fmov_to_gpr(Buf *b, int sf, int ft, int rd, int rn)   { fpint(b, sf, ft, 0, 6, rd, rn); }
void a64_fmov_from_gpr(Buf *b, int sf, int ft, int rd, int rn) { fpint(b, sf, ft, 0, 7, rd, rn); }

void a64_cnt_8b(Buf *b, int rd, int rn)  { W(0x0E205800u | (M5(rn) << 5) | M5(rd)); }
void a64_addv_8b(Buf *b, int rd, int rn) { W(0x0E31B800u | (M5(rn) << 5) | M5(rd)); }

#define VSZ(x) ((uint32_t)((x) & 3) << 22)
#define V3(op, rd, rn, rm) W((op) | (M5(rm) << 16) | (M5(rn) << 5) | M5(rd))
#define V2(op, rd, rn)     W((op) | (M5(rn) << 5) | M5(rd))

void a64_ldr_q(Buf *b, int rt, int rn, uint32_t off) {
  if (off & 15u) { a64_ldur_q(b, rt, rn, (int)FITU("unscaled load/store offset", off, 0xff)); return; }
  W(0x3DC00000u | (FITU("scaled load/store offset", off >> 4, 0xfff) << 10) |
    (M5(rn) << 5) | M5(rt));
}
void a64_str_q(Buf *b, int rt, int rn, uint32_t off) {
  if (off & 15u) { a64_stur_q(b, rt, rn, (int)FITU("unscaled load/store offset", off, 0xff)); return; }
  W(0x3D800000u | (FITU("scaled load/store offset", off >> 4, 0xfff) << 10) |
    (M5(rn) << 5) | M5(rt));
}
void a64_ldr_q_reg(Buf *b, int rt, int rn, int rm, int option, int s) {
  W(0x3CE00800u | (M5(rm) << 16) | ((uint32_t)(option & 7) << 13) |
    ((uint32_t)(s & 1) << 12) | (M5(rn) << 5) | M5(rt));
}
static void ldst_pair_q(Buf *b, uint32_t op, int rt, int rt2, int rn, int off) {
  if (off & 15) enc_fail("unaligned pair offset", off);
  uint32_t imm7 = FITS("pair offset", off / 16, -64, 63) & 0x7f;
  W(op | (imm7 << 15) | (M5(rt2) << 10) | (M5(rn) << 5) | M5(rt));
}
void a64_ldp_q(Buf *b, int rt, int rt2, int rn, int off) { ldst_pair_q(b, 0xAD400000u, rt, rt2, rn, off); }
void a64_stp_q(Buf *b, int rt, int rt2, int rn, int off) { ldst_pair_q(b, 0xAD000000u, rt, rt2, rn, off); }
void a64_ldur_q(Buf *b, int rt, int rn, int off) {
  W(0x3CC00000u | ((FITS("unscaled offset", off, -256, 255) & 0x1ffu) << 12) |
    (M5(rn) << 5) | M5(rt));
}
void a64_stur_q(Buf *b, int rt, int rn, int off) {
  W(0x3C800000u | ((FITS("unscaled offset", off, -256, 255) & 0x1ffu) << 12) |
    (M5(rn) << 5) | M5(rt));
}
void a64_str_q_reg(Buf *b, int rt, int rn, int rm, int option, int s) {
  W(0x3CA00800u | (M5(rm) << 16) | ((uint32_t)(option & 7) << 13) |
    ((uint32_t)(s & 1) << 12) | (M5(rn) << 5) | M5(rt));
}

void a64_vmov(Buf *b, int rd, int rn)               { V3(0x4EA01C00u, rd, rn, rn); }
void a64_vand(Buf *b, int rd, int rn, int rm)       { V3(0x4E201C00u, rd, rn, rm); }
void a64_vorr(Buf *b, int rd, int rn, int rm)       { V3(0x4EA01C00u, rd, rn, rm); }
void a64_veor(Buf *b, int rd, int rn, int rm)       { V3(0x6E201C00u, rd, rn, rm); }
void a64_vbic(Buf *b, int rd, int rn, int rm)       { V3(0x4E601C00u, rd, rn, rm); }
void a64_vorn(Buf *b, int rd, int rn, int rm)       { V3(0x4EE01C00u, rd, rn, rm); }
void a64_vnot(Buf *b, int rd, int rn)               { V2(0x6E205800u, rd, rn); }

void a64_vadd(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E208400u | VSZ(size), rd, rn, rm); }
void a64_vsub(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E208400u | VSZ(size), rd, rn, rm); }

void a64_vmul(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E209C00u | VSZ(size), rd, rn, rm); }
/* rd += rn * rm, rd -= rn * rm, lane by lane: bytes, halfwords or words */
void a64_vmla(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E209400u | VSZ(size), rd, rn, rm); }
void a64_vmls(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E209400u | VSZ(size), rd, rn, rm); }
void a64_vneg(Buf *b, int size, int rd, int rn)         { V2(0x6E20B800u | VSZ(size), rd, rn); }

void a64_vsmax(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E206400u | VSZ(size), rd, rn, rm); }
void a64_vsmin(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E206C00u | VSZ(size), rd, rn, rm); }
void a64_vumax(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E206400u | VSZ(size), rd, rn, rm); }
void a64_vumin(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E206C00u | VSZ(size), rd, rn, rm); }

void a64_vuzp1(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E001800u | VSZ(size), rd, rn, rm); }
void a64_vuzp2(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E005800u | VSZ(size), rd, rn, rm); }
void a64_vzip1(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E003800u | VSZ(size), rd, rn, rm); }
void a64_vzip2(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E007800u | VSZ(size), rd, rn, rm); }
void a64_vtrn1(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E002800u | VSZ(size), rd, rn, rm); }
void a64_vtrn2(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E006800u | VSZ(size), rd, rn, rm); }

void a64_vcmgt(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E203400u | VSZ(size), rd, rn, rm); }
void a64_vcmge(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E203C00u | VSZ(size), rd, rn, rm); }
void a64_vcmeq(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E208C00u | VSZ(size), rd, rn, rm); }
void a64_vcmhi(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E203400u | VSZ(size), rd, rn, rm); }
void a64_vcmhs(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E203C00u | VSZ(size), rd, rn, rm); }
void a64_vbsl(Buf *b, int rd, int rn, int rm)            { V3(0x6E601C00u, rd, rn, rm); }

void a64_vshl_imm(Buf *b, int size, int rd, int rn, int shift) {
  uint32_t imm = (uint32_t)(8 << size) + (uint32_t)shift;
  W(0x4F005400u | ((imm >> 3) << 19) | ((imm & 7u) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vushr_imm(Buf *b, int size, int rd, int rn, int shift) {
  uint32_t imm = (uint32_t)(16 << size) - (uint32_t)shift;
  W(0x6F000400u | ((imm >> 3) << 19) | ((imm & 7u) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vsli_imm(Buf *b, int size, int rd, int rn, int shift) {
  uint32_t imm = (uint32_t)(8 << size) + (uint32_t)shift;
  W(0x6F005400u | ((imm >> 3) << 19) | ((imm & 7u) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vsshr_imm(Buf *b, int size, int rd, int rn, int shift) {
  uint32_t imm = (uint32_t)(16 << size) - (uint32_t)shift;
  W(0x4F000400u | ((imm >> 3) << 19) | ((imm & 7u) << 16) | (M5(rn) << 5) | M5(rd));
}

void a64_vushl_reg(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E204400u | VSZ(size), rd, rn, rm); }
void a64_vsshl_reg(Buf *b, int size, int rd, int rn, int rm) { V3(0x4E204400u | VSZ(size), rd, rn, rm); }

static uint32_t lane_imm5(int size, int index) {
  return (uint32_t)(((index) << (size + 1)) | (1 << size)) & 0x1fu;
}

void a64_vdup_lane(Buf *b, int size, int rd, int rn, int index) {
  W(0x4E000400u | (lane_imm5(size, index) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vdup_lane0(Buf *b, int size, int rd, int rn) { a64_vdup_lane(b, size, rd, rn, 0); }

void a64_vumov(Buf *b, int size, int rd, int rn, int index) {
  W(0x0E003C00u | ((uint32_t)(size == 3) << 30) |
    (lane_imm5(size, index) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vsmov(Buf *b, int sf, int size, int rd, int rn, int index) {
  W(0x0E002C00u | ((uint32_t)(sf & 1) << 30) |
    (lane_imm5(size, index) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vins_gpr(Buf *b, int size, int rd, int index, int rn) {
  W(0x4E001C00u | (lane_imm5(size, index) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vins_elem(Buf *b, int size, int rd, int didx, int rn, int sidx) {
  W(0x6E000400u | (lane_imm5(size, didx) << 16) |
    ((uint32_t)((sidx << size) & 0xf) << 11) | (M5(rn) << 5) | M5(rd));
}

void a64_vext(Buf *b, int rd, int rn, int rm, int imm4) {
  W(0x6E000000u | (M5(rm) << 16) | ((uint32_t)(imm4 & 0xf) << 11) |
    (M5(rn) << 5) | M5(rd));
}

void a64_vtbl1(Buf *b, int rd, int rn, int rm) {
  W(0x4E000000u | (M5(rm) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vtbl2(Buf *b, int rd, int rn, int rm) {
  W(0x4E002000u | (M5(rm) << 16) | (M5(rn) << 5) | M5(rd));
}

void a64_vsqadd(Buf *b, int size, int rd, int rn, int rm)  { V3(0x4E200C00u | VSZ(size), rd, rn, rm); }
void a64_vuqadd(Buf *b, int size, int rd, int rn, int rm)  { V3(0x6E200C00u | VSZ(size), rd, rn, rm); }
void a64_vsqsub(Buf *b, int size, int rd, int rn, int rm)  { V3(0x4E202C00u | VSZ(size), rd, rn, rm); }
void a64_vuqsub(Buf *b, int size, int rd, int rn, int rm)  { V3(0x6E202C00u | VSZ(size), rd, rn, rm); }
void a64_vurhadd(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E201400u | VSZ(size), rd, rn, rm); }

void a64_vsqrdmulh(Buf *b, int size, int rd, int rn, int rm) { V3(0x6E20B400u | VSZ(size), rd, rn, rm); }
void a64_vaddp(Buf *b, int size, int rd, int rn, int rm)   { V3(0x4E20BC00u | VSZ(size), rd, rn, rm); }
void a64_vumaxp(Buf *b, int size, int rd, int rn, int rm)  { V3(0x6E20A400u | VSZ(size), rd, rn, rm); }

void a64_vabs(Buf *b, int size, int rd, int rn) { V2(0x4E20B800u | VSZ(size), rd, rn); }
void a64_vcnt(Buf *b, int rd, int rn)           { V2(0x4E205800u, rd, rn); }
void a64_vcmlt_zero(Buf *b, int size, int rd, int rn) { V2(0x4E20A800u | VSZ(size), rd, rn); }
void a64_vcmeq_zero(Buf *b, int size, int rd, int rn) { V2(0x4E209800u | VSZ(size), rd, rn); }

void a64_vaddv(Buf *b, int q, int size, int rd, int rn) {
  W(0x0E31B800u | ((uint32_t)(q & 1) << 30) | VSZ(size) | (M5(rn) << 5) | M5(rd));
}
void a64_vuminv(Buf *b, int size, int rd, int rn) {
  W(0x6E31A800u | VSZ(size) | (M5(rn) << 5) | M5(rd));
}

void a64_vumaxv(Buf *b, int size, int rd, int rn) {
  W(0x6E30A800u | VSZ(size) | (M5(rn) << 5) | M5(rd));
}
void a64_vsminv(Buf *b, int size, int rd, int rn) {
  W(0x4E31A800u | VSZ(size) | (M5(rn) << 5) | M5(rd));
}
void a64_vsmaxv(Buf *b, int size, int rd, int rn) {
  W(0x4E30A800u | VSZ(size) | (M5(rn) << 5) | M5(rd));
}

static void vshll(Buf *b, uint32_t base, int q, int size, int rd, int rn, int shift) {
  uint32_t imm = (uint32_t)(8 << size) + (uint32_t)shift;
  W(base | ((uint32_t)(q & 1) << 30) | ((imm >> 3) << 19) | ((imm & 7u) << 16) |
    (M5(rn) << 5) | M5(rd));
}
void a64_vsshll(Buf *b, int q, int size, int rd, int rn, int shift) {
  vshll(b, 0x0F00A400u, q, size, rd, rn, shift);
}
void a64_vushll(Buf *b, int q, int size, int rd, int rn, int shift) {
  vshll(b, 0x2F00A400u, q, size, rd, rn, shift);
}

void a64_vsqxtn(Buf *b, int q, int size, int rd, int rn) {
  W(0x0E214800u | ((uint32_t)(q & 1) << 30) | VSZ(size) | (M5(rn) << 5) | M5(rd));
}
void a64_vuqxtn(Buf *b, int q, int size, int rd, int rn) {
  W(0x2E214800u | ((uint32_t)(q & 1) << 30) | VSZ(size) | (M5(rn) << 5) | M5(rd));
}

void a64_vsqxtun(Buf *b, int q, int size, int rd, int rn) {
  W(0x2E212800u | ((uint32_t)(q & 1) << 30) | VSZ(size) | (M5(rn) << 5) | M5(rd));
}

void a64_vsaddlp(Buf *b, int size, int rd, int rn) { V2(0x4E202800u | VSZ(size), rd, rn); }
void a64_vuaddlp(Buf *b, int size, int rd, int rn) { V2(0x6E202800u | VSZ(size), rd, rn); }
void a64_vsadalp(Buf *b, int size, int rd, int rn) { V2(0x4E206800u | VSZ(size), rd, rn); }
void a64_vuadalp(Buf *b, int size, int rd, int rn) { V2(0x6E206800u | VSZ(size), rd, rn); }

void a64_vsmull(Buf *b, int q, int size, int rd, int rn, int rm) {
  W(0x0E20C000u | ((uint32_t)(q & 1) << 30) | VSZ(size) |
    (M5(rm) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vumull(Buf *b, int q, int size, int rd, int rn, int rm) {
  W(0x2E20C000u | ((uint32_t)(q & 1) << 30) | VSZ(size) |
    (M5(rm) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vumlal(Buf *b, int q, int size, int rd, int rn, int rm) {
  W(0x2E208000u | ((uint32_t)(q & 1) << 30) | VSZ(size) |
    (M5(rm) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vsmlal(Buf *b, int q, int size, int rd, int rn, int rm) {
  W(0x0E208000u | ((uint32_t)(q & 1) << 30) | VSZ(size) |
    (M5(rm) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vaddhn(Buf *b, int q, int size, int rd, int rn, int rm) {
  W(0x0E204000u | ((uint32_t)(q & 1) << 30) | VSZ(size) |
    (M5(rm) << 16) | (M5(rn) << 5) | M5(rd));
}
void a64_vldn(Buf *b, int n, int size, int rt, int rn) {
  uint32_t opc = n == 2 ? 0x8u : n == 3 ? 0x4u : 0x0u;
  W(0x4C400000u | (opc << 12) | ((uint32_t)(size & 3) << 10) | (M5(rn) << 5) | M5(rt));
}

void a64_vrev64(Buf *b, int size, int rd, int rn) { V2(0x4E200800u | VSZ(size), rd, rn); }
void a64_vrev32(Buf *b, int size, int rd, int rn) { V2(0x6E200800u | VSZ(size), rd, rn); }
void a64_vrev16(Buf *b, int rd, int rn) { V2(0x4E201800u, rd, rn); }
void a64_vxtn(Buf *b, int q, int size, int rd, int rn) {
  W(0x0E212800u | ((uint32_t)(q & 1) << 30) | VSZ(size) | (M5(rn) << 5) | M5(rd));
}
void a64_vdup_gpr(Buf *b, int size, int rd, int rn) {
  W(0x4E000C00u | ((uint32_t)(1 << size) << 16) | (M5(rn) << 5) | M5(rd));
}

void a64_vmovi_b(Buf *b, int rd, uint32_t imm8) {
  W(0x4F00E400u | ((imm8 >> 5) << 16) | ((imm8 & 0x1fu) << 5) | M5(rd));
}

void a64_vfadd(Buf *b, int sz, int rd, int rn, int rm) { V3(0x4E20D400u | VSZ(sz), rd, rn, rm); }
void a64_vfsub(Buf *b, int sz, int rd, int rn, int rm) { V3(0x4EA0D400u | VSZ(sz), rd, rn, rm); }
void a64_vfmul(Buf *b, int sz, int rd, int rn, int rm) { V3(0x6E20DC00u | VSZ(sz), rd, rn, rm); }
void a64_vfdiv(Buf *b, int sz, int rd, int rn, int rm) { V3(0x6E20FC00u | VSZ(sz), rd, rn, rm); }
void a64_vfmax(Buf *b, int sz, int rd, int rn, int rm) { V3(0x4E20F400u | VSZ(sz), rd, rn, rm); }

/* fmul by element: rm's lane idx in every lane, .d[H] or .s[H:L] */
void a64_vfmul_elem(Buf *b, int sz, int rd, int rn, int rm, int idx) {
  uint32_t hl = sz ? (uint32_t)(idx & 1) << 11
                   : ((uint32_t)(idx & 2) << 10) | ((uint32_t)(idx & 1) << 21);
  V3(0x4F809000u | ((uint32_t)sz << 22) | hl, rd, rn, rm);
}

void a64_vfmla(Buf *b, int sz, int rd, int rn, int rm) { V3(0x4E20CC00u | VSZ(sz), rd, rn, rm); }
void a64_vfmls(Buf *b, int sz, int rd, int rn, int rm) { V3(0x4EA0CC00u | VSZ(sz), rd, rn, rm); }
void a64_vfmin(Buf *b, int sz, int rd, int rn, int rm) { V3(0x4EA0F400u | VSZ(sz), rd, rn, rm); }
void a64_vfneg(Buf *b, int sz, int rd, int rn)  { V2(0x6EA0F800u | VSZ(sz), rd, rn); }
void a64_vfabs(Buf *b, int sz, int rd, int rn)  { V2(0x4EA0F800u | VSZ(sz), rd, rn); }
void a64_vfsqrt(Buf *b, int sz, int rd, int rn) { V2(0x6EA1F800u | VSZ(sz), rd, rn); }

void a64_vfcmeq(Buf *b, int sz, int rd, int rn, int rm) { V3(0x4E20E400u | VSZ(sz), rd, rn, rm); }
void a64_vfcmge(Buf *b, int sz, int rd, int rn, int rm) { V3(0x6E20E400u | VSZ(sz), rd, rn, rm); }
void a64_vfcmgt(Buf *b, int sz, int rd, int rn, int rm) { V3(0x6EA0E400u | VSZ(sz), rd, rn, rm); }

void a64_vfrint(Buf *b, int sz, int rd, int rn, int mode) {
  uint32_t hi = (uint32_t)(mode & 1) << 23;
  uint32_t op = (uint32_t)((mode >> 1) & 1) << 12;
  V2(0x4E218800u | hi | op | VSZ(sz), rd, rn);
}

void a64_vfcvtzs(Buf *b, int sz, int rd, int rn) { V2(0x4EA1B800u | VSZ(sz), rd, rn); }
void a64_vfcvtzu(Buf *b, int sz, int rd, int rn) { V2(0x6EA1B800u | VSZ(sz), rd, rn); }
void a64_vscvtf(Buf *b, int sz, int rd, int rn)  { V2(0x4E21D800u | VSZ(sz), rd, rn); }
void a64_vucvtf(Buf *b, int sz, int rd, int rn)  { V2(0x6E21D800u | VSZ(sz), rd, rn); }

void a64_vfcvtn(Buf *b, int rd, int rn) { V2(0x0E616800u, rd, rn); }
void a64_vfcvtl(Buf *b, int rd, int rn) { V2(0x0E617800u, rd, rn); }

void a64_addv_4s(Buf *b, int rd, int rn) { V2(0x4EB1B800u, rd, rn); }
void a64_addp_2d(Buf *b, int rd, int rn) { V2(0x5EF1B800u, rd, rn); }

void a64_umov_d1(Buf *b, int rd, int rn) { V2(0x4E183C00u, rd, rn); }

void a64_ins_d1_gpr(Buf *b, int rd, int rn) { V2(0x4E181C00u, rd, rn); }
