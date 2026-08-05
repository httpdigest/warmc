/* Encoder oracle test.
 *
 * For each case we emit our own encoding AND the equivalent assembly text.
 * The harness assembles the text with the GNU assembler and compares the two
 * byte streams.  The assembler is the oracle, so a misreading of the Arm ARM
 * shows up as a hard mismatch rather than a plausible-looking wrong encoding.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/a64.h"

static Buf ours;
static FILE *asmf;
static int ncases;
static const char *cur_text;
static size_t cur_start;

#define CASE(text, ...)                                                        \
  do {                                                                         \
    cur_text = (text);                                                         \
    cur_start = ours.len;                                                      \
    __VA_ARGS__;                                                               \
    fprintf(asmf, "\t%s\n", text);                                             \
    ncases++;                                                                  \
  } while (0)

/* An FMOV immediate is two calls: decide the value is representable, then
 * encode it.  A pattern the helper rejects is a failure in its own right --
 * the assembler is about to prove the value does have an encoding. */
#define FIMM(text, bits, ft, rd)                                               \
  do {                                                                         \
    uint32_t imm8_;                                                            \
    if (!a64_fmov_imm((bits), (ft), &imm8_)) {                                 \
      fprintf(stderr, "a64: %s not recognised as an FMOV immediate\n", text);  \
      return 1;                                                                \
    }                                                                          \
    CASE(text, a64_fmov_imm_emit(b, (ft), (rd), imm8_));                       \
  } while (0)

/* A literal load, emitted with a zero displacement and then patched, which is
 * how the constant pool builds one: the site is where the word landed and the
 * target is that far away. */
#define LITPAT(text, ft, rt, delta)                                            \
  do {                                                                         \
    size_t site_ = ours.len;                                                   \
    CASE(text, a64_ldr_lit_fp(b, (ft), (rt));                                  \
         if (!a64_patch_ldr_lit(b, site_,                                      \
                                (size_t)((ptrdiff_t)site_ + (delta)))) {       \
           fprintf(stderr, "a64: %s rejected as out of range\n", text);        \
           return 1;                                                           \
         });                                                                   \
  } while (0)

/* ------------------------------------------------- logical immediates
 *
 * The bitmask immediate is the one encoding in the set that is a *search*
 * rather than a layout: `a64_logical_imm` has to find the element size, the
 * rotation and the run length that reproduce a value, and answer no when
 * none do.  Ten hand-written values reached the oracle above, out of six and
 * a half thousand encodings -- and the ones a hand picks are the ones a hand
 * would get right.
 *
 * So this sweeps the whole space from the other end.  `DecodeBitMasks` from
 * the Arm ARM is written out below, independently of the encoder, and every
 * (esize, S, R) triple is turned into the value it denotes: the encoder must
 * accept that value and its encoding must decode back to it.  Then four
 * million arbitrary words check the other direction -- nothing may be
 * accepted whose encoding means something else.  A value the encoder wrongly
 * accepts is a silently wrong `and`/`orr`/`eor`/`tst`, which no assembler
 * oracle can see, because the wrong encoding is a perfectly legal
 * instruction. */
static int decode_bitmask(int sf, uint32_t n, uint32_t immr, uint32_t imms,
                          uint64_t *out) {
  unsigned regsize = sf ? 64 : 32;
  if (!sf && n) return 0;
  uint32_t x = (n << 6) | ((~imms) & 0x3f);
  int len = -1;
  for (int i = 6; i >= 0; i--) if (x & (1u << i)) { len = i; break; }
  if (len < 1) return 0;
  unsigned esize = 1u << len;
  if (esize > regsize) return 0;
  uint32_t levels = esize - 1;
  if ((imms & levels) == levels) return 0;          /* reserved */
  unsigned S = imms & levels, R = immr & levels;
  uint64_t emask = (esize == 64) ? ~0ull : ((1ull << esize) - 1);
  uint64_t welem = ((S + 1 == 64) ? ~0ull : ((1ull << (S + 1)) - 1)) & emask;
  uint64_t rot = R ? (((welem >> R) | (welem << (esize - R))) & emask) : welem;
  uint64_t v = 0;
  for (unsigned i = 0; i < regsize; i += esize) v |= rot << i;
  *out = sf ? v : (v & 0xffffffffu);
  return 1;
}

static int logical_imm_sweep(void) {
  int bad = 0, ok = 0;
  for (int sf = 0; sf <= 1; sf++) {
    unsigned regsize = sf ? 64u : 32u;
    for (unsigned esize = 2; esize <= regsize; esize *= 2)
      for (unsigned S = 0; S + 1 < esize; S++)
        for (unsigned R = 0; R < esize; R++) {
          uint64_t emask = (esize == 64) ? ~0ull : ((1ull << esize) - 1);
          uint64_t welem = ((S + 1 == 64) ? ~0ull : ((1ull << (S + 1)) - 1)) & emask;
          uint64_t rot = R ? (((welem >> R) | (welem << (esize - R))) & emask) : welem;
          uint64_t v = 0;
          for (unsigned i = 0; i < regsize; i += esize) v |= rot << i;
          if (!sf) v &= 0xffffffffu;
          uint32_t enc;
          uint64_t back;
          if (!a64_logical_imm(v, sf, &enc)) {
            if (bad++ < 8)
              fprintf(stderr, "a64: refused representable sf=%d 0x%llx "
                      "(esize=%u S=%u R=%u)\n", sf, (unsigned long long)v,
                      esize, S, R);
            continue;
          }
          if (!decode_bitmask(sf, (enc >> 12) & 1, (enc >> 6) & 0x3f, enc & 0x3f,
                              &back) || back != v) {
            if (bad++ < 8)
              fprintf(stderr, "a64: sf=%d 0x%llx encoded 0x%x, decodes 0x%llx\n",
                      sf, (unsigned long long)v, enc, (unsigned long long)back);
            continue;
          }
          ok++;
        }
  }
  /* And the refusals: an arbitrary word is almost never a bitmask, so what
   * this measures is that nothing is accepted whose encoding means something
   * else. */
  uint64_t seed = 0x123456789abcdefull;
  for (long i = 0; i < 4000000; i++) {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    for (int sf = 0; sf <= 1; sf++) {
      uint64_t v = sf ? (seed >> 3) : ((seed >> 3) & 0xffffffffu);
      uint32_t enc;
      uint64_t back;
      if (!a64_logical_imm(v, sf, &enc)) continue;
      if (!decode_bitmask(sf, (enc >> 12) & 1, (enc >> 6) & 0x3f, enc & 0x3f,
                          &back) || back != v) {
        if (bad++ < 8)
          fprintf(stderr, "a64: accepted sf=%d 0x%llx as 0x%x, which is 0x%llx\n",
                  sf, (unsigned long long)v, enc, (unsigned long long)back);
      }
    }
  }
  fprintf(stderr, "logical immediates: %d encodings round-trip, %d bad\n", ok, bad);
  return bad;
}

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: a64_test ours.bin expected.s\n"); return 2; }
  asmf = fopen(argv[2], "w");
  if (!asmf) { perror("open"); return 2; }
  fprintf(asmf, "\t.text\n");

  Buf *b = &ours;

  /* moves */
  CASE("movz x0, #0x1234", a64_movz(b, SF64, R0, 0x1234, 0));
  CASE("movz x1, #0xabcd, lsl #16", a64_movz(b, SF64, R1, 0xabcd, 16));
  CASE("movz x2, #0x1, lsl #32", a64_movz(b, SF64, R2, 1, 32));
  CASE("movz w3, #0xffff", a64_movz(b, SF32, R3, 0xffff, 0));
  CASE("movk x4, #0x9999, lsl #48", a64_movk(b, SF64, R4, 0x9999, 48));
  CASE("movn x5, #0x7", a64_movn(b, SF64, R5, 7, 0));
  CASE("mov x6, x7", a64_mov_reg(b, SF64, R6, R7));
  CASE("mov w8, w9", a64_mov_reg(b, SF32, R8, R9));
  CASE("mov x10, sp", a64_mov_sp(b, SF64, R10, RSP));

  /* add/sub immediate and register */
  CASE("add x0, x1, #0x10", a64_add_imm(b, SF64, R0, R1, 0x10, 0));
  CASE("add x0, x1, #0x10, lsl #12", a64_add_imm(b, SF64, R0, R1, 0x10, 1));
  CASE("sub w2, w3, #0xfff", a64_sub_imm(b, SF32, R2, R3, 0xfff, 0));
  CASE("adds x4, x5, #1", a64_adds_imm(b, SF64, R4, R5, 1, 0));
  CASE("subs w6, w7, #2", a64_subs_imm(b, SF32, R6, R7, 2, 0));
  CASE("add x8, x9, x10", a64_add_reg(b, SF64, R8, R9, R10));
  CASE("sub w11, w12, w13", a64_sub_reg(b, SF32, R11, R12, R13));
  CASE("adds x14, x15, x16", a64_adds_reg(b, SF64, R14, R15, R16));
  CASE("subs w17, w18, w19", a64_subs_reg(b, SF32, R17, R18, R19));
  CASE("add x20, x21, x22, lsl #3", a64_add_reg_lsl(b, SF64, R20, R21, R22, 3));
  CASE("sub w0, w1, w2, lsl #5", a64_sub_reg_lsl(b, SF32, R0, R1, R2, 5));
  CASE("add x3, x4, w5, uxtw", a64_add_uxtw(b, R3, R4, R5, 0));
  CASE("add x6, x7, w8, uxtw #3", a64_add_uxtw(b, R6, R7, R8, 3));
  /* The base is the stack pointer, which the shifted-register form cannot
   * name: there Rn == 31 reads as XZR and the address is simply lost. */
  CASE("add x8, sp, x16", a64_add_extx(b, R8, RSP, R16, 0));
  CASE("add x2, x3, x4, uxtx #2", a64_add_extx(b, R2, R3, R4, 2));
  CASE("neg w23, w24", a64_neg(b, SF32, R23, R24));
  CASE("cmp x25, x26", a64_cmp_reg(b, SF64, R25, R26));
  CASE("cmp w27, #5", a64_cmp_imm(b, SF32, R27, 5));

  /* mul / div */
  CASE("mul x0, x1, x2", a64_mul(b, SF64, R0, R1, R2));
  CASE("madd w3, w4, w5, w6", a64_madd(b, SF32, R3, R4, R5, R6));
  /* The high half of a product, both signednesses and both routes to it:
   * `umulh`/`smulh` at 64 bits, and the widening `umull`/`smull` at 32, which
   * is how a 32-bit multiply-high is reached on a machine that has no such
   * instruction. */
  CASE("umulh x3, x4, x5", a64_umulh(b, R3, R4, R5));
  CASE("smulh x3, x4, x5", a64_smulh(b, R3, R4, R5));
  CASE("umull x3, w4, w5", a64_umull(b, R3, R4, R5));
  CASE("smull x3, w4, w5", a64_smull(b, R3, R4, R5));
  CASE("umulh x0, x0, x0", a64_umulh(b, R0, R0, R0));
  CASE("umull x28, w28, w28", a64_umull(b, R28, R28, R28));
  CASE("msub x7, x8, x9, x10", a64_msub(b, SF64, R7, R8, R9, R10));
  CASE("sdiv w11, w12, w13", a64_sdiv(b, SF32, R11, R12, R13));
  CASE("udiv x14, x15, x16", a64_udiv(b, SF64, R14, R15, R16));

  /* logical / shift */
  CASE("and x0, x1, x2", a64_and_reg(b, SF64, R0, R1, R2));
  CASE("orr w3, w4, w5", a64_orr_reg(b, SF32, R3, R4, R5));
  CASE("eor x6, x7, x8", a64_eor_reg(b, SF64, R6, R7, R8));
  CASE("ands w9, w10, w11", a64_ands_reg(b, SF32, R9, R10, R11));
  CASE("mvn x12, x13", a64_mvn(b, SF64, R12, R13));
  CASE("bic w9, w10, w11", a64_bic_reg(b, SF32, R9, R10, R11));
  CASE("bic x9, x10, x11", a64_bic_reg(b, SF64, R9, R10, R11));
  CASE("orn w1, w2, w3", a64_orn_reg(b, SF32, R1, R2, R3));
  CASE("eon x4, x5, x6", a64_eon_reg(b, SF64, R4, R5, R6));
  CASE("and w12, w13, w14, lsl #3", a64_and_reg_sh(b, SF32, R12, R13, R14, SH_LSL, 3));
  CASE("orr x15, x16, x17, lsr #33", a64_orr_reg_sh(b, SF64, R15, R16, R17, SH_LSR, 33));
  CASE("eor x18, x19, x20, asr #7", a64_eor_reg_sh(b, SF64, R18, R19, R20, SH_ASR, 7));
  CASE("eor w21, w22, w23, ror #17", a64_eor_reg_sh(b, SF32, R21, R22, R23, SH_ROR, 17));
  {
    uint32_t e;
    a64_logical_imm(0xff, SF32, &e);
    CASE("and w0, w1, #0xff", a64_and_imm(b, SF32, R0, R1, e));
    a64_logical_imm(0x7fffffff, SF32, &e);
    CASE("and w2, w3, #0x7fffffff", a64_and_imm(b, SF32, R2, R3, e));
    a64_logical_imm(0xf0f0f0f0, SF32, &e);
    CASE("eor w4, w5, #0xf0f0f0f0", a64_eor_imm(b, SF32, R4, R5, e));
    a64_logical_imm(0x1, SF32, &e);
    CASE("orr w6, w7, #0x1", a64_orr_imm(b, SF32, R6, R7, e));
    a64_logical_imm(0xffffffffffff0000ull, SF64, &e);
    CASE("and x8, x9, #0xffffffffffff0000", a64_and_imm(b, SF64, R8, R9, e));
    a64_logical_imm(0x5555555555555555ull, SF64, &e);
    CASE("orr x10, x11, #0x5555555555555555", a64_orr_imm(b, SF64, R10, R11, e));
    a64_logical_imm(0x3fffff, SF64, &e);
    CASE("and x12, x13, #0x3fffff", a64_and_imm(b, SF64, R12, R13, e));
  }
  CASE("ror w14, w15, #7", a64_ror_imm(b, SF32, R14, R15, 7));
  CASE("ror x16, x17, #33", a64_ror_imm(b, SF64, R16, R17, 33));
  CASE("tst w14, w15", a64_tst_reg(b, SF32, R14, R15));
  CASE("lsl w16, w17, w18", a64_lslv(b, SF32, R16, R17, R18));
  CASE("lsr x19, x20, x21", a64_lsrv(b, SF64, R19, R20, R21));
  CASE("asr w22, w23, w24", a64_asrv(b, SF32, R22, R23, R24));
  CASE("ror x25, x26, x27", a64_rorv(b, SF64, R25, R26, R27));
  CASE("lsl w0, w1, #5", a64_lsl_imm(b, SF32, R0, R1, 5));
  CASE("lsl x2, x3, #17", a64_lsl_imm(b, SF64, R2, R3, 17));
  CASE("lsr w4, w5, #31", a64_lsr_imm(b, SF32, R4, R5, 31));
  CASE("asr x6, x7, #63", a64_asr_imm(b, SF64, R6, R7, 63));
  CASE("clz w8, w9", a64_clz(b, SF32, R8, R9));
  CASE("rbit x10, x11", a64_rbit(b, SF64, R10, R11));
  CASE("rev w12, w13", a64_rev(b, SF32, R12, R13));
  CASE("rev x14, x15", a64_rev(b, SF64, R14, R15));
  CASE("sxtb w12, w13", a64_sxtb(b, SF32, R12, R13));
  CASE("sxth x14, w15", a64_sxth(b, SF64, R14, R15));
  CASE("sxtw x16, w17", a64_sxtw(b, R16, R17));
  CASE("uxtb w18, w19", a64_uxtb(b, R18, R19));
  CASE("uxth w20, w21", a64_uxth(b, R20, R21));

  /* conditional */
  CASE("csel x0, x1, x2, eq", a64_csel(b, SF64, R0, R1, R2, CC_EQ));
  CASE("csinc w3, w4, w5, lt", a64_csinc(b, SF32, R3, R4, R5, CC_LT));
  CASE("cset w6, ne", a64_cset(b, SF32, R6, CC_NE));
  CASE("cset x7, ge", a64_cset(b, SF64, R7, CC_GE));
  CASE("csinv x8, x9, x10, hi", a64_csinv(b, SF64, R8, R9, R10, CC_HI));
  CASE("csetm w11, lo", a64_csetm(b, SF32, R11, CC_LO));
  CASE("csetm x12, eq", a64_csetm(b, SF64, R12, CC_EQ));

  /* memory */
  CASE("strb w0, [x1]", a64_str_imm(b, 0, R0, R1, 0));
  CASE("strh w2, [x3, #6]", a64_str_imm(b, 1, R2, R3, 6));
  CASE("str w4, [x5, #16]", a64_str_imm(b, 2, R4, R5, 16));
  CASE("str x6, [x7, #4088]", a64_str_imm(b, 3, R6, R7, 4088));
  CASE("ldrb w8, [x9, #3]", a64_ldr_imm(b, 0, R8, R9, 3));
  CASE("ldrh w10, [x11, #8]", a64_ldr_imm(b, 1, R10, R11, 8));
  CASE("ldr w12, [x13, #40]", a64_ldr_imm(b, 2, R12, R13, 40));
  CASE("ldr x14, [x15, #64]", a64_ldr_imm(b, 3, R14, R15, 64));
  CASE("ldrsb w16, [x17, #1]", a64_ldrs_imm(b, 0, 0, R16, R17, 1));
  CASE("ldrsb x18, [x19, #1]", a64_ldrs_imm(b, 0, 1, R18, R19, 1));
  CASE("ldrsh w20, [x21, #2]", a64_ldrs_imm(b, 1, 0, R20, R21, 2));
  CASE("ldrsw x22, [x23, #4]", a64_ldrs_imm(b, 2, 1, R22, R23, 4));
  CASE("str w0, [x1, x2]", a64_str_reg(b, 2, R0, R1, R2, 3, 0));
  CASE("ldr x3, [x4, x5]", a64_ldr_reg(b, 3, R3, R4, R5, 3, 0));
  CASE("ldrb w6, [x7, x8]", a64_ldr_reg(b, 0, R6, R7, R8, 3, 0));
  CASE("ldrsw x9, [x10, x11]", a64_ldrs_reg(b, 2, 1, R9, R10, R11, 3, 0));
  CASE("ldr w0, [x1, w2, uxtw]", a64_ldr_reg(b, 2, R0, R1, R2, 2, 0));
  CASE("str x3, [x4, w5, uxtw]", a64_str_reg(b, 3, R3, R4, R5, 2, 0));
  CASE("ldrb w6, [x7, w8, uxtw]", a64_ldr_reg(b, 0, R6, R7, R8, 2, 0));
  CASE("ldr d9, [x10, w11, uxtw]", a64_fldr_reg(b, 3, 9, R10, R11, 2, 0));
  CASE("str s0, [x1, #12]", a64_fstr_imm(b, 2, 0, R1, 12));
  CASE("ldr d2, [x3, #24]", a64_fldr_imm(b, 3, 2, R3, 24));
  CASE("str d4, [x5, x6]", a64_fstr_reg(b, 3, 4, R5, R6, 3, 0));
  CASE("ldr s7, [x8, x9]", a64_fldr_reg(b, 2, 7, R8, R9, 3, 0));
  CASE("stp x29, x30, [sp, #-16]!", a64_stp(b, SF64, R29, R30, RSP, -16, LSP_PRE));
  CASE("ldp x29, x30, [sp], #16", a64_ldp(b, SF64, R29, R30, RSP, 16, LSP_POST));
  CASE("stp x19, x20, [sp, #32]", a64_stp(b, SF64, R19, R20, RSP, 32, LSP_OFF));
  CASE("ldp w21, w22, [x23, #8]", a64_ldp(b, SF32, R21, R22, R23, 8, LSP_OFF));
  CASE("stur w0, [x1, #-4]", a64_stur(b, 2, R0, R1, -4));
  CASE("ldur x2, [x3, #-8]", a64_ldur(b, 3, R2, R3, -8));

  /* The literal load and its patch.
   *
   * Two things to prove and the assembler proves both: that the unpatched word
   * is the right instruction with the right register, and that the patch puts
   * the displacement where the field is -- which a wrong shift would leave
   * looking like a perfectly legal load of the wrong eight bytes, so an oracle
   * is the only witness.  Both directions, because the field is signed. */
  CASE("ldr d0, .", a64_ldr_lit_fp(b, FT_D, 0));
  CASE("ldr s3, .", a64_ldr_lit_fp(b, FT_S, 3));
  CASE("ldr d31, .", a64_ldr_lit_fp(b, FT_D, 31));
  LITPAT("ldr d1, . + 8", FT_D, 1, 8);
  LITPAT("ldr s2, . + 0x400", FT_S, 2, 0x400);
  LITPAT("ldr d4, . - 8", FT_D, 4, -8);

  /* branches (self-relative so the text is unambiguous) */
  CASE("b .", a64_b(b, 0));
  CASE("bl .", a64_bl(b, 0));
  CASE("b.eq .", a64_bcond(b, CC_EQ, 0));
  CASE("b.hs .", a64_bcond(b, CC_CS, 0));
  CASE("cbz w0, .", a64_cbz(b, SF32, R0, 0));
  CASE("cbnz x1, .", a64_cbnz(b, SF64, R1, 0));

  /* The skeleton the long-branch form emits: an inverted conditional over an
   * unconditional branch.  The inversion is the easy thing to get wrong --
   * b.cond flips on bit 0 of the condition, cbz/cbnz on bit 24 of the word --
   * so each pair is written out and checked against the assembler. */
  CASE("b.ne . + 8", a64_bcond(b, cc_invert(CC_EQ), 2));
  CASE("b.eq . + 8", a64_bcond(b, cc_invert(CC_NE), 2));
  CASE("b.ls . + 8", a64_bcond(b, cc_invert(CC_HI), 2));
  CASE("b.lo . + 8", a64_bcond(b, cc_invert(CC_HS), 2));
  CASE("cbnz w0, . + 8", a64_cbnz(b, SF32, R0, 2));
  CASE("cbz x1, . + 8", a64_cbz(b, SF64, R1, 2));
  /* Both sides of the bit-number split, and both sides of the width it
   * implies: bit 5 is inside the low field, bit 31 is its top, and bit 40
   * is only reachable at all because the register is read as an X. */
  CASE("tbz w6, #5, .", a64_tbz(b, R6, 5, 0));
  CASE("tbz w7, #31, .", a64_tbz(b, R7, 31, 0));
  CASE("tbnz x8, #30, .", a64_tbnz(b, R8, 30, 0));
  CASE("tbnz x9, #40, .", a64_tbnz(b, R9, 40, 0));
  CASE("br x2", a64_br(b, R2));
  CASE("blr x3", a64_blr(b, R3));
  CASE("ret", a64_ret(b));
  CASE("brk #0x1", a64_brk(b, 1));
  CASE("nop", a64_nop(b));
  CASE("adr x4, .", a64_adr(b, R4, 0));
  CASE("adrp x5, .", a64_adrp(b, R5, 0));

  /* floating point */
  CASE("fadd s0, s1, s2", a64_fadd(b, FT_S, 0, 1, 2));
  CASE("fadd d3, d4, d5", a64_fadd(b, FT_D, 3, 4, 5));
  CASE("fsub s6, s7, s8", a64_fsub(b, FT_S, 6, 7, 8));
  CASE("fmul d9, d10, d11", a64_fmul(b, FT_D, 9, 10, 11));
  CASE("fdiv s12, s13, s14", a64_fdiv(b, FT_S, 12, 13, 14));
  CASE("fmax d15, d16, d17", a64_fmax(b, FT_D, 15, 16, 17));
  CASE("fmin s18, s19, s20", a64_fmin(b, FT_S, 18, 19, 20));
  CASE("fabs d21, d22", a64_fabs(b, FT_D, 21, 22));
  CASE("fneg s23, s24", a64_fneg(b, FT_S, 23, 24));
  CASE("fsqrt d25, d26", a64_fsqrt(b, FT_D, 25, 26));
  CASE("fmov s27, s28", a64_fmov_reg(b, FT_S, 27, 28));
  CASE("frintn d0, d1", a64_frint(b, FT_D, 0, 1, 0));
  CASE("frintp s2, s3", a64_frint(b, FT_S, 2, 3, 1));
  CASE("frintm d4, d5", a64_frint(b, FT_D, 4, 5, 2));
  CASE("frintz s6, s7", a64_frint(b, FT_S, 6, 7, 3));
  CASE("fcmp s8, s9", a64_fcmp(b, FT_S, 8, 9));
  CASE("fcmp d10, d11", a64_fcmp(b, FT_D, 10, 11));
  CASE("fcsel s0, s1, s2, ne", a64_fcsel(b, FT_S, 0, 1, 2, CC_NE));
  CASE("fcsel d3, d4, d5, eq", a64_fcsel(b, FT_D, 3, 4, 5, CC_EQ));
  CASE("fcvt d12, s13", a64_fcvt(b, FT_D, FT_S, 12, 13));
  CASE("fcvt s14, d15", a64_fcvt(b, FT_S, FT_D, 14, 15));
  CASE("fcvtzs w0, s1", a64_fcvtzs(b, SF32, FT_S, R0, 1));
  CASE("fcvtzs x2, d3", a64_fcvtzs(b, SF64, FT_D, R2, 3));
  CASE("fcvtzu w4, d5", a64_fcvtzu(b, SF32, FT_D, R4, 5));
  CASE("scvtf s6, w7", a64_scvtf(b, SF32, FT_S, 6, R7));
  CASE("scvtf d8, x9", a64_scvtf(b, SF64, FT_D, 8, R9));
  CASE("ucvtf d10, w11", a64_ucvtf(b, SF32, FT_D, 10, R11));
  CASE("fmov w12, s13", a64_fmov_to_gpr(b, SF32, FT_S, R12, 13));
  CASE("fmov x14, d15", a64_fmov_to_gpr(b, SF64, FT_D, R14, 15));
  CASE("fmov s16, w17", a64_fmov_from_gpr(b, SF32, FT_S, 16, R17));
  CASE("fmov d18, x19", a64_fmov_from_gpr(b, SF64, FT_D, 18, R19));
  /* The 8-bit immediate form, at both ends of its range and both signs. */
  FIMM("fmov d20, #1.0", 0x3ff0000000000000ull, FT_D, 20);
  FIMM("fmov d21, #1.5", 0x3ff8000000000000ull, FT_D, 21);
  FIMM("fmov d22, #-8.0", 0xc020000000000000ull, FT_D, 22);
  FIMM("fmov d23, #0.125", 0x3fc0000000000000ull, FT_D, 23);
  FIMM("fmov d24, #31.0", 0x403f000000000000ull, FT_D, 24);
  FIMM("fmov s25, #0.5", 0x3f000000ull, FT_S, 25);
  FIMM("fmov s26, #-1.0625", 0xbf880000ull, FT_S, 26);

  /* simd (popcount idiom) */
  { uint32_t e;
    a64_logical_imm(0xff, SF32, &e);
    CASE("tst w20, #0xff", a64_tst_imm(b, SF32, R20, e));
    a64_logical_imm(0x00ff00ff00ff00ffull, SF64, &e);
    CASE("ands x21, x22, #0xff00ff00ff00ff", a64_ands_imm(b, SF64, R21, R22, e)); }
  CASE("tst x23, x24", a64_tst_reg(b, SF64, R23, R24));

  CASE("cnt v0.8b, v1.8b", a64_cnt_8b(b, 0, 1));
  CASE("addv b2, v3.8b", a64_addv_8b(b, 2, 3));

  /* 128-bit SIMD.  Every lane shape of every form is listed rather than one
   * representative, because `size` reaches the encoding through three
   * different fields depending on the form -- a plain two-bit selector on the
   * arithmetic ops, one bit on the floating-point ops, and folded into
   * `immh` on the shifts, where an off-by-one is a different lane width
   * rather than an invalid instruction. */
  CASE("ldr q0, [x1]", a64_ldr_q(b, 0, R1, 0));
  CASE("ldr q31, [x2, #4080]", a64_ldr_q(b, 31, R2, 4080));
  CASE("str q3, [x4, #16]", a64_str_q(b, 3, R4, 16));
  CASE("str q17, [sp, #65520]", a64_str_q(b, 17, RSP, 65520));
  /* The top of the scaled range on both sides.  Past this the offset used to
   * wrap into the register field instead of failing; it now aborts, which is
   * not something this harness can assert, since enc_fail exits. */
  CASE("ldr q30, [sp, #65520]", a64_ldr_q(b, 30, RSP, 65520));
  CASE("ldr q5, [x6, x7, lsl #4]", a64_ldr_q_reg(b, 5, R6, R7, 3, 1));
  CASE("ldr q8, [x9, w10, uxtw #0]", a64_ldr_q_reg(b, 8, R9, R10, 2, 0));
  CASE("str q11, [x12, x13, lsl #4]", a64_str_q_reg(b, 11, R12, R13, 3, 1));
  CASE("str q14, [x15, w16, uxtw #0]", a64_str_q_reg(b, 14, R15, R16, 2, 0));

  CASE("mov v0.16b, v1.16b", a64_vmov(b, 0, 1));
  CASE("and v2.16b, v3.16b, v4.16b", a64_vand(b, 2, 3, 4));
  CASE("orr v5.16b, v6.16b, v7.16b", a64_vorr(b, 5, 6, 7));
  CASE("eor v8.16b, v9.16b, v10.16b", a64_veor(b, 8, 9, 10));
  CASE("bic v11.16b, v12.16b, v13.16b", a64_vbic(b, 11, 12, 13));
  CASE("orn v14.16b, v15.16b, v16.16b", a64_vorn(b, 14, 15, 16));
  CASE("not v17.16b, v18.16b", a64_vnot(b, 17, 18));

  CASE("add v0.16b, v1.16b, v2.16b", a64_vadd(b, 0, 0, 1, 2));
  CASE("add v3.8h, v4.8h, v5.8h", a64_vadd(b, 1, 3, 4, 5));
  CASE("add v6.4s, v7.4s, v8.4s", a64_vadd(b, 2, 6, 7, 8));
  CASE("add v9.2d, v10.2d, v11.2d", a64_vadd(b, 3, 9, 10, 11));
  CASE("sub v12.16b, v13.16b, v14.16b", a64_vsub(b, 0, 12, 13, 14));
  CASE("sub v15.8h, v16.8h, v17.8h", a64_vsub(b, 1, 15, 16, 17));
  CASE("sub v18.4s, v19.4s, v20.4s", a64_vsub(b, 2, 18, 19, 20));
  CASE("sub v21.2d, v22.2d, v23.2d", a64_vsub(b, 3, 21, 22, 23));
  CASE("mul v24.16b, v25.16b, v26.16b", a64_vmul(b, 0, 24, 25, 26));
  CASE("mul v27.8h, v28.8h, v29.8h", a64_vmul(b, 1, 27, 28, 29));
  CASE("mul v30.4s, v31.4s, v0.4s", a64_vmul(b, 2, 30, 31, 0));
  CASE("neg v1.16b, v2.16b", a64_vneg(b, 0, 1, 2));
  CASE("neg v3.8h, v4.8h", a64_vneg(b, 1, 3, 4));
  CASE("neg v5.4s, v6.4s", a64_vneg(b, 2, 5, 6));
  CASE("neg v7.2d, v8.2d", a64_vneg(b, 3, 7, 8));

  CASE("cmgt v0.16b, v1.16b, v2.16b", a64_vcmgt(b, 0, 0, 1, 2));
  CASE("cmgt v5.8h, v6.8h, v7.8h", a64_vcmgt(b, 1, 5, 6, 7));
  CASE("cmgt v10.4s, v11.4s, v12.4s", a64_vcmgt(b, 2, 10, 11, 12));
  CASE("cmgt v15.2d, v16.2d, v17.2d", a64_vcmgt(b, 3, 15, 16, 17));
  CASE("cmge v0.16b, v1.16b, v2.16b", a64_vcmge(b, 0, 0, 1, 2));
  CASE("cmge v5.8h, v6.8h, v7.8h", a64_vcmge(b, 1, 5, 6, 7));
  CASE("cmge v10.4s, v11.4s, v12.4s", a64_vcmge(b, 2, 10, 11, 12));
  CASE("cmge v15.2d, v16.2d, v17.2d", a64_vcmge(b, 3, 15, 16, 17));
  CASE("cmeq v0.16b, v1.16b, v2.16b", a64_vcmeq(b, 0, 0, 1, 2));
  CASE("cmeq v5.8h, v6.8h, v7.8h", a64_vcmeq(b, 1, 5, 6, 7));
  CASE("cmeq v10.4s, v11.4s, v12.4s", a64_vcmeq(b, 2, 10, 11, 12));
  CASE("cmeq v15.2d, v16.2d, v17.2d", a64_vcmeq(b, 3, 15, 16, 17));
  CASE("cmhi v0.16b, v1.16b, v2.16b", a64_vcmhi(b, 0, 0, 1, 2));
  CASE("cmhi v5.8h, v6.8h, v7.8h", a64_vcmhi(b, 1, 5, 6, 7));
  CASE("cmhi v10.4s, v11.4s, v12.4s", a64_vcmhi(b, 2, 10, 11, 12));
  CASE("cmhi v15.2d, v16.2d, v17.2d", a64_vcmhi(b, 3, 15, 16, 17));
  CASE("cmhs v0.16b, v1.16b, v2.16b", a64_vcmhs(b, 0, 0, 1, 2));
  CASE("cmhs v5.8h, v6.8h, v7.8h", a64_vcmhs(b, 1, 5, 6, 7));
  CASE("cmhs v10.4s, v11.4s, v12.4s", a64_vcmhs(b, 2, 10, 11, 12));
  CASE("cmhs v15.2d, v16.2d, v17.2d", a64_vcmhs(b, 3, 15, 16, 17));
  CASE("bsl v7.16b, v8.16b, v9.16b", a64_vbsl(b, 7, 8, 9));
  CASE("uzp1 v0.16b, v1.16b, v2.16b", a64_vuzp1(b, 0, 0, 1, 2));
  CASE("uzp1 v3.8h, v4.8h, v5.8h", a64_vuzp1(b, 1, 3, 4, 5));
  CASE("uzp1 v6.4s, v7.4s, v8.4s", a64_vuzp1(b, 2, 6, 7, 8));
  CASE("uzp1 v9.2d, v10.2d, v11.2d", a64_vuzp1(b, 3, 9, 10, 11));
  CASE("uzp2 v0.16b, v1.16b, v2.16b", a64_vuzp2(b, 0, 0, 1, 2));
  CASE("uzp2 v3.8h, v4.8h, v5.8h", a64_vuzp2(b, 1, 3, 4, 5));
  CASE("uzp2 v6.4s, v7.4s, v8.4s", a64_vuzp2(b, 2, 6, 7, 8));
  CASE("uzp2 v9.2d, v10.2d, v11.2d", a64_vuzp2(b, 3, 9, 10, 11));
  CASE("zip1 v0.16b, v1.16b, v2.16b", a64_vzip1(b, 0, 0, 1, 2));
  CASE("zip1 v3.8h, v4.8h, v5.8h", a64_vzip1(b, 1, 3, 4, 5));
  CASE("zip1 v6.4s, v7.4s, v8.4s", a64_vzip1(b, 2, 6, 7, 8));
  CASE("zip1 v9.2d, v10.2d, v11.2d", a64_vzip1(b, 3, 9, 10, 11));
  CASE("zip2 v0.16b, v1.16b, v2.16b", a64_vzip2(b, 0, 0, 1, 2));
  CASE("zip2 v3.8h, v4.8h, v5.8h", a64_vzip2(b, 1, 3, 4, 5));
  CASE("zip2 v6.4s, v7.4s, v8.4s", a64_vzip2(b, 2, 6, 7, 8));
  CASE("zip2 v9.2d, v10.2d, v11.2d", a64_vzip2(b, 3, 9, 10, 11));
  CASE("smax v0.16b, v1.16b, v2.16b", a64_vsmax(b, 0, 0, 1, 2));
  CASE("smax v3.8h, v4.8h, v5.8h", a64_vsmax(b, 1, 3, 4, 5));
  CASE("smax v6.4s, v7.4s, v8.4s", a64_vsmax(b, 2, 6, 7, 8));
  CASE("smin v9.16b, v10.16b, v11.16b", a64_vsmin(b, 0, 9, 10, 11));
  CASE("smin v12.8h, v13.8h, v14.8h", a64_vsmin(b, 1, 12, 13, 14));
  CASE("smin v15.4s, v16.4s, v17.4s", a64_vsmin(b, 2, 15, 16, 17));
  CASE("umax v18.16b, v19.16b, v20.16b", a64_vumax(b, 0, 18, 19, 20));
  CASE("umax v21.8h, v22.8h, v23.8h", a64_vumax(b, 1, 21, 22, 23));
  CASE("umax v24.4s, v25.4s, v26.4s", a64_vumax(b, 2, 24, 25, 26));
  CASE("umin v27.16b, v28.16b, v29.16b", a64_vumin(b, 0, 27, 28, 29));
  CASE("umin v30.8h, v31.8h, v0.8h", a64_vumin(b, 1, 30, 31, 0));
  CASE("umin v1.4s, v2.4s, v3.4s", a64_vumin(b, 2, 1, 2, 3));
  CASE("shl v0.16b, v1.16b, #7", a64_vshl_imm(b, 0, 0, 1, 7));
  CASE("shl v2.8h, v3.8h, #1", a64_vshl_imm(b, 1, 2, 3, 1));
  CASE("shl v4.4s, v5.4s, #31", a64_vshl_imm(b, 2, 4, 5, 31));
  CASE("shl v6.2d, v7.2d, #63", a64_vshl_imm(b, 3, 6, 7, 63));
  CASE("shl v8.4s, v9.4s, #0", a64_vshl_imm(b, 2, 8, 9, 0));
  CASE("ushr v10.16b, v11.16b, #8", a64_vushr_imm(b, 0, 10, 11, 8));
  CASE("ushr v12.8h, v13.8h, #1", a64_vushr_imm(b, 1, 12, 13, 1));
  CASE("ushr v14.4s, v15.4s, #32", a64_vushr_imm(b, 2, 14, 15, 32));
  CASE("ushr v16.2d, v17.2d, #17", a64_vushr_imm(b, 3, 16, 17, 17));
  CASE("sshr v18.16b, v19.16b, #3", a64_vsshr_imm(b, 0, 18, 19, 3));
  CASE("sshr v20.8h, v21.8h, #16", a64_vsshr_imm(b, 1, 20, 21, 16));
  CASE("sshr v22.4s, v23.4s, #1", a64_vsshr_imm(b, 2, 22, 23, 1));
  CASE("sshr v24.2d, v25.2d, #64", a64_vsshr_imm(b, 3, 24, 25, 64));

  CASE("dup v0.16b, w1", a64_vdup_gpr(b, 0, 0, R1));
  CASE("dup v2.8h, w3", a64_vdup_gpr(b, 1, 2, R3));
  CASE("dup v4.4s, w5", a64_vdup_gpr(b, 2, 4, R5));
  CASE("dup v6.2d, x7", a64_vdup_gpr(b, 3, 6, R7));
  CASE("dup v11.4s, v12.s[0]", a64_vdup_lane0(b, 2, 11, 12));
  CASE("dup v13.2d, v14.d[0]", a64_vdup_lane0(b, 3, 13, 14));
  CASE("movi v8.16b, #0", a64_vmovi_b(b, 8, 0));
  CASE("movi v9.16b, #255", a64_vmovi_b(b, 9, 255));
  CASE("movi v10.16b, #170", a64_vmovi_b(b, 10, 170));

  CASE("fadd v0.4s, v1.4s, v2.4s", a64_vfadd(b, 0, 0, 1, 2));
  CASE("fadd v3.2d, v4.2d, v5.2d", a64_vfadd(b, 1, 3, 4, 5));
  CASE("fsub v6.4s, v7.4s, v8.4s", a64_vfsub(b, 0, 6, 7, 8));
  CASE("fsub v9.2d, v10.2d, v11.2d", a64_vfsub(b, 1, 9, 10, 11));
  CASE("fmul v12.4s, v13.4s, v14.4s", a64_vfmul(b, 0, 12, 13, 14));
  CASE("fmul v15.2d, v16.2d, v17.2d", a64_vfmul(b, 1, 15, 16, 17));
  CASE("fdiv v18.4s, v19.4s, v20.4s", a64_vfdiv(b, 0, 18, 19, 20));
  CASE("fdiv v21.2d, v22.2d, v23.2d", a64_vfdiv(b, 1, 21, 22, 23));
  CASE("fmax v24.4s, v25.4s, v26.4s", a64_vfmax(b, 0, 24, 25, 26));
  CASE("fmax v27.2d, v28.2d, v29.2d", a64_vfmax(b, 1, 27, 28, 29));
  CASE("fmin v30.4s, v31.4s, v0.4s", a64_vfmin(b, 0, 30, 31, 0));
  CASE("fmin v1.2d, v2.2d, v3.2d", a64_vfmin(b, 1, 1, 2, 3));
  /* The fused forms, which relaxed_madd and relaxed_nmadd are.  Note that
   * these read their destination as well as writing it. */
  CASE("fmla v4.4s, v5.4s, v6.4s", a64_vfmla(b, 0, 4, 5, 6));
  CASE("fmla v7.2d, v8.2d, v9.2d", a64_vfmla(b, 1, 7, 8, 9));
  CASE("fmls v10.4s, v11.4s, v12.4s", a64_vfmls(b, 0, 10, 11, 12));
  CASE("fmls v13.2d, v14.2d, v15.2d", a64_vfmls(b, 1, 13, 14, 15));
  CASE("fneg v4.4s, v5.4s", a64_vfneg(b, 0, 4, 5));
  CASE("fneg v6.2d, v7.2d", a64_vfneg(b, 1, 6, 7));
  CASE("fabs v8.4s, v9.4s", a64_vfabs(b, 0, 8, 9));
  CASE("fabs v10.2d, v11.2d", a64_vfabs(b, 1, 10, 11));
  CASE("fsqrt v12.4s, v13.4s", a64_vfsqrt(b, 0, 12, 13));
  CASE("fsqrt v14.2d, v15.2d", a64_vfsqrt(b, 1, 14, 15));

  CASE("addv s16, v17.4s", a64_addv_4s(b, 16, 17));
  CASE("addp d18, v19.2d", a64_addp_2d(b, 18, 19));
  CASE("umov x20, v21.d[1]", a64_umov_d1(b, R20, 21));
  CASE("mov v22.d[1], x23", a64_ins_d1_gpr(b, 22, R23));
  CASE("mov v0.d[1], x1", a64_ins_d1_gpr(b, 0, R1));

  /* ---- the explicit wasm vector instruction set ---- */

  CASE("ushl v0.16b, v1.16b, v2.16b", a64_vushl_reg(b, 0, 0, 1, 2));
  CASE("ushl v3.8h, v4.8h, v5.8h", a64_vushl_reg(b, 1, 3, 4, 5));
  CASE("ushl v6.4s, v7.4s, v8.4s", a64_vushl_reg(b, 2, 6, 7, 8));
  CASE("ushl v9.2d, v10.2d, v11.2d", a64_vushl_reg(b, 3, 9, 10, 11));
  CASE("sshl v12.16b, v13.16b, v14.16b", a64_vsshl_reg(b, 0, 12, 13, 14));
  CASE("sshl v15.8h, v16.8h, v17.8h", a64_vsshl_reg(b, 1, 15, 16, 17));
  CASE("sshl v18.4s, v19.4s, v20.4s", a64_vsshl_reg(b, 2, 18, 19, 20));
  CASE("sshl v21.2d, v22.2d, v23.2d", a64_vsshl_reg(b, 3, 21, 22, 23));

  CASE("dup v0.16b, v1.b[15]", a64_vdup_lane(b, 0, 0, 1, 15));
  CASE("dup v2.8h, v3.h[7]", a64_vdup_lane(b, 1, 2, 3, 7));
  CASE("dup v4.4s, v5.s[3]", a64_vdup_lane(b, 2, 4, 5, 3));
  CASE("dup v6.2d, v7.d[1]", a64_vdup_lane(b, 3, 6, 7, 1));

  CASE("umov w0, v1.b[13]", a64_vumov(b, 0, R0, 1, 13));
  CASE("umov w2, v3.h[6]", a64_vumov(b, 1, R2, 3, 6));
  CASE("umov w4, v5.s[2]", a64_vumov(b, 2, R4, 5, 2));
  CASE("umov x6, v7.d[1]", a64_vumov(b, 3, R6, 7, 1));
  CASE("smov w8, v9.b[11]", a64_vsmov(b, SF32, 0, R8, 9, 11));
  CASE("smov w10, v11.h[5]", a64_vsmov(b, SF32, 1, R10, 11, 5));
  CASE("smov x12, v13.b[3]", a64_vsmov(b, SF64, 0, R12, 13, 3));
  CASE("mov v14.b[9], w15", a64_vins_gpr(b, 0, 14, 9, R15));
  CASE("mov v16.h[4], w17", a64_vins_gpr(b, 1, 16, 4, R17));
  CASE("mov v18.s[1], w19", a64_vins_gpr(b, 2, 18, 1, R19));
  CASE("mov v20.d[0], x21", a64_vins_gpr(b, 3, 20, 0, R21));
  CASE("mov v22.b[7], v23.b[2]", a64_vins_elem(b, 0, 22, 7, 23, 2));
  CASE("mov v24.h[3], v25.h[0]", a64_vins_elem(b, 1, 24, 3, 25, 0));
  CASE("mov v26.s[2], v27.s[3]", a64_vins_elem(b, 2, 26, 2, 27, 3));
  CASE("mov v28.d[1], v29.d[0]", a64_vins_elem(b, 3, 28, 1, 29, 0));

  CASE("ext v0.16b, v1.16b, v2.16b, #8", a64_vext(b, 0, 1, 2, 8));
  CASE("ext v3.16b, v4.16b, v5.16b, #1", a64_vext(b, 3, 4, 5, 1));
  CASE("tbl v6.16b, {v7.16b}, v8.16b", a64_vtbl1(b, 6, 7, 8));
  CASE("tbl v9.16b, {v10.16b, v11.16b}, v12.16b", a64_vtbl2(b, 9, 10, 12));

  CASE("sqadd v0.16b, v1.16b, v2.16b", a64_vsqadd(b, 0, 0, 1, 2));
  CASE("sqadd v3.8h, v4.8h, v5.8h", a64_vsqadd(b, 1, 3, 4, 5));
  CASE("uqadd v6.16b, v7.16b, v8.16b", a64_vuqadd(b, 0, 6, 7, 8));
  CASE("uqadd v9.8h, v10.8h, v11.8h", a64_vuqadd(b, 1, 9, 10, 11));
  CASE("sqsub v12.16b, v13.16b, v14.16b", a64_vsqsub(b, 0, 12, 13, 14));
  CASE("sqsub v15.8h, v16.8h, v17.8h", a64_vsqsub(b, 1, 15, 16, 17));
  CASE("uqsub v18.16b, v19.16b, v20.16b", a64_vuqsub(b, 0, 18, 19, 20));
  CASE("uqsub v21.8h, v22.8h, v23.8h", a64_vuqsub(b, 1, 21, 22, 23));
  CASE("urhadd v24.16b, v25.16b, v26.16b", a64_vurhadd(b, 0, 24, 25, 26));
  CASE("urhadd v27.8h, v28.8h, v29.8h", a64_vurhadd(b, 1, 27, 28, 29));
  CASE("sqrdmulh v30.8h, v31.8h, v0.8h", a64_vsqrdmulh(b, 1, 30, 31, 0));
  CASE("addp v1.16b, v2.16b, v3.16b", a64_vaddp(b, 0, 1, 2, 3));
  CASE("addp v4.8h, v5.8h, v6.8h", a64_vaddp(b, 1, 4, 5, 6));
  CASE("addp v7.4s, v8.4s, v9.4s", a64_vaddp(b, 2, 7, 8, 9));
  CASE("addp v10.2d, v11.2d, v12.2d", a64_vaddp(b, 3, 10, 11, 12));
  CASE("umaxp v13.4s, v14.4s, v15.4s", a64_vumaxp(b, 2, 13, 14, 15));

  CASE("abs v0.16b, v1.16b", a64_vabs(b, 0, 0, 1));
  CASE("abs v2.8h, v3.8h", a64_vabs(b, 1, 2, 3));
  CASE("abs v4.4s, v5.4s", a64_vabs(b, 2, 4, 5));
  CASE("abs v6.2d, v7.2d", a64_vabs(b, 3, 6, 7));
  CASE("cnt v8.16b, v9.16b", a64_vcnt(b, 8, 9));
  CASE("cmlt v10.16b, v11.16b, #0", a64_vcmlt_zero(b, 0, 10, 11));
  CASE("cmlt v12.8h, v13.8h, #0", a64_vcmlt_zero(b, 1, 12, 13));
  CASE("cmlt v14.4s, v15.4s, #0", a64_vcmlt_zero(b, 2, 14, 15));
  CASE("cmlt v16.2d, v17.2d, #0", a64_vcmlt_zero(b, 3, 16, 17));
  CASE("cmeq v18.16b, v19.16b, #0", a64_vcmeq_zero(b, 0, 18, 19));
  CASE("cmeq v20.2d, v21.2d, #0", a64_vcmeq_zero(b, 3, 20, 21));

  CASE("addv b0, v1.8b", a64_vaddv(b, 0, 0, 0, 1));
  CASE("addv b2, v3.16b", a64_vaddv(b, 1, 0, 2, 3));
  CASE("addv h4, v5.8h", a64_vaddv(b, 1, 1, 4, 5));
  CASE("addv s6, v7.4s", a64_vaddv(b, 1, 2, 6, 7));
  CASE("uminv b8, v9.16b", a64_vuminv(b, 0, 8, 9));
  CASE("uminv h10, v11.8h", a64_vuminv(b, 1, 10, 11));
  CASE("uminv s12, v13.4s", a64_vuminv(b, 2, 12, 13));

  CASE("sshll v0.8h, v1.8b, #0", a64_vsshll(b, 0, 0, 0, 1, 0));
  CASE("sshll2 v2.8h, v3.16b, #0", a64_vsshll(b, 1, 0, 2, 3, 0));
  CASE("sshll v4.4s, v5.4h, #0", a64_vsshll(b, 0, 1, 4, 5, 0));
  CASE("sshll2 v6.4s, v7.8h, #0", a64_vsshll(b, 1, 1, 6, 7, 0));
  CASE("sshll v8.2d, v9.2s, #0", a64_vsshll(b, 0, 2, 8, 9, 0));
  CASE("sshll2 v10.2d, v11.4s, #0", a64_vsshll(b, 1, 2, 10, 11, 0));
  CASE("ushll v12.8h, v13.8b, #0", a64_vushll(b, 0, 0, 12, 13, 0));
  CASE("ushll2 v14.8h, v15.16b, #0", a64_vushll(b, 1, 0, 14, 15, 0));
  CASE("ushll v16.2d, v17.2s, #0", a64_vushll(b, 0, 2, 16, 17, 0));
  CASE("ushll2 v18.2d, v19.4s, #0", a64_vushll(b, 1, 2, 18, 19, 0));

  CASE("sqxtn v0.8b, v1.8h", a64_vsqxtn(b, 0, 0, 0, 1));
  CASE("sqxtn2 v2.16b, v3.8h", a64_vsqxtn(b, 1, 0, 2, 3));
  CASE("sqxtn v4.4h, v5.4s", a64_vsqxtn(b, 0, 1, 4, 5));
  CASE("sqxtn2 v6.8h, v7.4s", a64_vsqxtn(b, 1, 1, 6, 7));
  CASE("sqxtn v8.2s, v9.2d", a64_vsqxtn(b, 0, 2, 8, 9));
  CASE("uqxtn v10.2s, v11.2d", a64_vuqxtn(b, 0, 2, 10, 11));
  CASE("sqxtun v12.8b, v13.8h", a64_vsqxtun(b, 0, 0, 12, 13));
  CASE("sqxtun2 v14.16b, v15.8h", a64_vsqxtun(b, 1, 0, 14, 15));
  CASE("sqxtun v16.4h, v17.4s", a64_vsqxtun(b, 0, 1, 16, 17));
  CASE("sqxtun2 v18.8h, v19.4s", a64_vsqxtun(b, 1, 1, 18, 19));

  CASE("saddlp v0.8h, v1.16b", a64_vsaddlp(b, 0, 0, 1));
  CASE("saddlp v2.4s, v3.8h", a64_vsaddlp(b, 1, 2, 3));
  CASE("uaddlp v4.8h, v5.16b", a64_vuaddlp(b, 0, 4, 5));
  CASE("uaddlp v6.4s, v7.8h", a64_vuaddlp(b, 1, 6, 7));

  CASE("smull v0.8h, v1.8b, v2.8b", a64_vsmull(b, 0, 0, 0, 1, 2));
  CASE("smull2 v3.8h, v4.16b, v5.16b", a64_vsmull(b, 1, 0, 3, 4, 5));
  CASE("smull v6.4s, v7.4h, v8.4h", a64_vsmull(b, 0, 1, 6, 7, 8));
  CASE("smull2 v9.4s, v10.8h, v11.8h", a64_vsmull(b, 1, 1, 9, 10, 11));
  CASE("smull v12.2d, v13.2s, v14.2s", a64_vsmull(b, 0, 2, 12, 13, 14));
  CASE("smull2 v15.2d, v16.4s, v17.4s", a64_vsmull(b, 1, 2, 15, 16, 17));
  CASE("umull v18.8h, v19.8b, v20.8b", a64_vumull(b, 0, 0, 18, 19, 20));
  CASE("umull2 v21.2d, v22.4s, v23.4s", a64_vumull(b, 1, 2, 21, 22, 23));

  CASE("fcmeq v0.4s, v1.4s, v2.4s", a64_vfcmeq(b, 0, 0, 1, 2));
  CASE("fcmeq v3.2d, v4.2d, v5.2d", a64_vfcmeq(b, 1, 3, 4, 5));
  CASE("fcmgt v6.4s, v7.4s, v8.4s", a64_vfcmgt(b, 0, 6, 7, 8));
  CASE("fcmgt v9.2d, v10.2d, v11.2d", a64_vfcmgt(b, 1, 9, 10, 11));
  CASE("fcmge v12.4s, v13.4s, v14.4s", a64_vfcmge(b, 0, 12, 13, 14));
  CASE("fcmge v15.2d, v16.2d, v17.2d", a64_vfcmge(b, 1, 15, 16, 17));

  CASE("frintn v0.4s, v1.4s", a64_vfrint(b, 0, 0, 1, 0));
  CASE("frintp v2.4s, v3.4s", a64_vfrint(b, 0, 2, 3, 1));
  CASE("frintm v4.4s, v5.4s", a64_vfrint(b, 0, 4, 5, 2));
  CASE("frintz v6.4s, v7.4s", a64_vfrint(b, 0, 6, 7, 3));
  CASE("frintn v8.2d, v9.2d", a64_vfrint(b, 1, 8, 9, 0));
  CASE("frintp v10.2d, v11.2d", a64_vfrint(b, 1, 10, 11, 1));
  CASE("frintm v12.2d, v13.2d", a64_vfrint(b, 1, 12, 13, 2));
  CASE("frintz v14.2d, v15.2d", a64_vfrint(b, 1, 14, 15, 3));

  CASE("fcvtzs v0.4s, v1.4s", a64_vfcvtzs(b, 0, 0, 1));
  CASE("fcvtzs v2.2d, v3.2d", a64_vfcvtzs(b, 1, 2, 3));
  CASE("fcvtzu v4.4s, v5.4s", a64_vfcvtzu(b, 0, 4, 5));
  CASE("fcvtzu v6.2d, v7.2d", a64_vfcvtzu(b, 1, 6, 7));
  CASE("scvtf v8.4s, v9.4s", a64_vscvtf(b, 0, 8, 9));
  CASE("scvtf v10.2d, v11.2d", a64_vscvtf(b, 1, 10, 11));
  CASE("ucvtf v12.4s, v13.4s", a64_vucvtf(b, 0, 12, 13));
  CASE("ucvtf v14.2d, v15.2d", a64_vucvtf(b, 1, 14, 15));
  CASE("fcvtn v16.2s, v17.2d", a64_vfcvtn(b, 16, 17));
  CASE("fcvtl v18.2d, v19.2s", a64_vfcvtl(b, 18, 19));

  CASE("umlal v0.2d, v1.2s, v2.2s", a64_vumlal(b, 0, 2, 0, 1, 2));
  CASE("umlal2 v3.2d, v4.4s, v5.4s", a64_vumlal(b, 1, 2, 3, 4, 5));
  CASE("rev64 v6.4s, v7.4s", a64_vrev64(b, 2, 6, 7));
  CASE("xtn v8.2s, v9.2d", a64_vxtn(b, 0, 2, 8, 9));
  CASE("xtn2 v10.4s, v11.2d", a64_vxtn(b, 1, 2, 10, 11));

  CASE("ldr q0, [x1, #16]", a64_fldr_imm(b, 4, 0, R1, 16));
  CASE("str q2, [x3, #65520]", a64_fstr_imm(b, 4, 2, R3, 65520));
  CASE("ldr q4, [x5, w6, uxtw]", a64_fldr_reg(b, 4, 4, R5, R6, 2, 0));
  CASE("str q7, [x8, x9]", a64_fstr_reg(b, 4, 7, R8, R9, 3, 0));

  fclose(asmf);
  FILE *bin = fopen(argv[1], "wb");
  if (!bin) { perror("open"); return 2; }
  fwrite(ours.data, 1, ours.len, bin);
  fclose(bin);
  fprintf(stderr, "emitted %d cases, %zu bytes\n", ncases, ours.len);
  if (logical_imm_sweep() != 0) return 1;
  return 0;
}
