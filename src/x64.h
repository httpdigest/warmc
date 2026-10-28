#ifndef WARMC_X64_H
#define WARMC_X64_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "buf.h"

/* The x64 encoder.  One exported function per mnemonic-form, like a64.h,
   so the differential oracle (test/x64_test.c vs llvm-mc) stays a flat
   list.  Encoding choices match llvm-mc's canonical picks byte for byte:
   imm8 sign-extended forms when the immediate fits, the AX short forms
   for RAX destinations, 2-byte VEX whenever legal.

   Width argument w: 0 = 32-bit operand, 1 = 64-bit (the a64 SF32/SF64
   convention).  8/16-bit accesses go through dedicated widening/narrow
   forms; there is no general 8/16-bit ALU surface. */

enum {
  X_RAX, X_RCX, X_RDX, X_RBX, X_RSP, X_RBP, X_RSI, X_RDI,
  X_R8, X_R9, X_R10, X_R11, X_R12, X_R13, X_R14, X_R15
};

enum {
  X_XMM0, X_XMM1, X_XMM2, X_XMM3, X_XMM4, X_XMM5, X_XMM6, X_XMM7,
  X_XMM8, X_XMM9, X_XMM10, X_XMM11, X_XMM12, X_XMM13, X_XMM14, X_XMM15
};

/* Condition codes in the hardware tttn encoding (0F 8x / 0F 9x / 0F 4x).
   xcc_invert flips the sense, same trick as a64's cc_invert. */
enum {
  XCC_O = 0x0, XCC_NO = 0x1, XCC_B = 0x2, XCC_AE = 0x3,
  XCC_E = 0x4, XCC_NE = 0x5, XCC_BE = 0x6, XCC_A = 0x7,
  XCC_S = 0x8, XCC_NS = 0x9, XCC_P = 0xa, XCC_NP = 0xb,
  XCC_L = 0xc, XCC_GE = 0xd, XCC_LE = 0xe, XCC_G = 0xf
};
static inline int xcc_invert(int c) { return c ^ 1; }

/* Memory operands are (base, idx, scale_log2, disp).  idx = X64_NOIDX for
   no index (idx may never be X_RSP); base = X64_RIPREL for RIP-relative,
   whose disp32 field always ends the instruction and is patched with
   x64_patch_rel32. */
#define X64_NOIDX  (-1)
#define X64_RIPREL (-2)

/* Write rel32 at `field` (the offset of the 4-byte field itself) so it
   reaches `target`, both offsets into the same buffer. */
void x64_patch_rel32(Buf *b, size_t field, size_t target);

/* ---- mov / constants ------------------------------------------------ */
void x64_mov_rr(Buf *b, int w, int dst, int src);
void x64_mov_ri(Buf *b, int w, int dst, uint64_t imm);   /* canonical pick */
void x64_movzx8(Buf *b, int w, int dst, int src);
void x64_movzx16(Buf *b, int w, int dst, int src);
void x64_movsx8(Buf *b, int w, int dst, int src);
void x64_movsx16(Buf *b, int w, int dst, int src);
void x64_movsxd(Buf *b, int dst, int src);               /* r64 <- sx(r32) */
void x64_lea(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp);

/* ---- ALU (two-address; dst := dst op src) --------------------------- */
void x64_add_rr(Buf *b, int w, int dst, int src);
void x64_sub_rr(Buf *b, int w, int dst, int src);
void x64_and_rr(Buf *b, int w, int dst, int src);
void x64_or_rr(Buf *b, int w, int dst, int src);
void x64_xor_rr(Buf *b, int w, int dst, int src);
void x64_cmp_rr(Buf *b, int w, int a, int reg);
void x64_test_rr(Buf *b, int w, int a, int reg);
void x64_add_ri(Buf *b, int w, int dst, int32_t imm);
void x64_sub_ri(Buf *b, int w, int dst, int32_t imm);
void x64_and_ri(Buf *b, int w, int dst, int32_t imm);
void x64_or_ri(Buf *b, int w, int dst, int32_t imm);
void x64_xor_ri(Buf *b, int w, int dst, int32_t imm);
void x64_cmp_ri(Buf *b, int w, int a, int32_t imm);

/* add r/m, imm32 -- always the 32-bit immediate form, never the shorter
   imm8 one, so the immediate stays patchable in place afterwards. */
void x64_add_mi32(Buf *b, int w, int base, int idx, int scale, int32_t disp,
                  int32_t imm);

void x64_test_ri(Buf *b, int w, int a, int32_t imm);
void x64_neg(Buf *b, int w, int dst);
void x64_not(Buf *b, int w, int dst);

/* ---- shifts / bits -------------------------------------------------- */
void x64_shl_ri(Buf *b, int w, int dst, int imm);
void x64_shr_ri(Buf *b, int w, int dst, int imm);
void x64_sar_ri(Buf *b, int w, int dst, int imm);
void x64_rol_ri(Buf *b, int w, int dst, int imm);
void x64_ror_ri(Buf *b, int w, int dst, int imm);
void x64_shlx(Buf *b, int w, int dst, int src, int cnt);  /* BMI2 */
void x64_shrx(Buf *b, int w, int dst, int src, int cnt);
void x64_sarx(Buf *b, int w, int dst, int src, int cnt);
void x64_rorx(Buf *b, int w, int dst, int src, int imm);
void x64_lzcnt(Buf *b, int w, int dst, int src);
void x64_tzcnt(Buf *b, int w, int dst, int src);
void x64_popcnt(Buf *b, int w, int dst, int src);
void x64_bswap(Buf *b, int w, int dst);

/* ---- mul / div ------------------------------------------------------ */
void x64_imul_rr(Buf *b, int w, int dst, int src);
void x64_imul_rri(Buf *b, int w, int dst, int src, int32_t imm);
void x64_imul_r(Buf *b, int w, int src);     /* RDX:RAX = RAX * src */
void x64_mul_r(Buf *b, int w, int src);
void x64_div_r(Buf *b, int w, int src);
void x64_idiv_r(Buf *b, int w, int src);
void x64_cdq_cqo(Buf *b, int w);
void x64_mulx(Buf *b, int w, int hi, int lo, int src);   /* BMI2: uses RDX */

/* ---- conditionals --------------------------------------------------- */
void x64_setcc(Buf *b, int cc, int dst);                 /* byte dst */
void x64_cmovcc(Buf *b, int w, int cc, int dst, int src);

/* ---- loads / stores ------------------------------------------------- */
/* sz: 0/1 widen through zero- or sign-extension, 2 = dword, 3 = qword.
   The zero/sign variants take the full-width destination. */
void x64_load8zx(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp);
void x64_load16zx(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp);
void x64_load8sx(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp);
void x64_load16sx(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp);
void x64_load32(Buf *b, int dst, int base, int idx, int scale, int32_t disp);
void x64_load32sx(Buf *b, int dst, int base, int idx, int scale, int32_t disp);
void x64_load64(Buf *b, int dst, int base, int idx, int scale, int32_t disp);
void x64_store8(Buf *b, int src, int base, int idx, int scale, int32_t disp);
void x64_store16(Buf *b, int src, int base, int idx, int scale, int32_t disp);
void x64_store32(Buf *b, int src, int base, int idx, int scale, int32_t disp);
void x64_store64(Buf *b, int src, int base, int idx, int scale, int32_t disp);

/* ---- flow / stack --------------------------------------------------- */
void x64_push(Buf *b, int r);
void x64_pop(Buf *b, int r);
void x64_call_rel32(Buf *b);       /* disp 0; field at len-4 */
void x64_call_r(Buf *b, int r);
void x64_jmp_rel32(Buf *b);        /* disp 0; field at len-4 */
void x64_jmp_r(Buf *b, int r);
void x64_jcc_rel32(Buf *b, int cc);/* disp 0; field at len-4 */
void x64_ret(Buf *b);
void x64_ud2(Buf *b);
void x64_int3(Buf *b);
void x64_nop(Buf *b);
void x64_nop_n(Buf *b, int n);     /* one canonical multi-byte nop, 1..15 */

/* ---- scalar FP (VEX; d: 0 = ss/f32, 1 = sd/f64) --------------------- */
void x64_vmovs_load(Buf *b, int d, int dst, int base, int idx, int scale, int32_t disp);
void x64_vmovs_store(Buf *b, int d, int src, int base, int idx, int scale, int32_t disp);
void x64_vmovs_rr(Buf *b, int d, int dst, int src1, int src2);
void x64_vadds(Buf *b, int d, int dst, int a, int c);
void x64_vsubs(Buf *b, int d, int dst, int a, int c);
void x64_vmuls(Buf *b, int d, int dst, int a, int c);
void x64_vdivs(Buf *b, int d, int dst, int a, int c);
void x64_vmins(Buf *b, int d, int dst, int a, int c);
void x64_vmaxs(Buf *b, int d, int dst, int a, int c);
void x64_vsqrts(Buf *b, int d, int dst, int a, int c);
void x64_vucomis(Buf *b, int d, int a, int c);
void x64_vrounds(Buf *b, int d, int dst, int a, int c, int mode);
void x64_vcvtsi2s(Buf *b, int d, int w, int dst, int a, int src);
void x64_vcvtts2si(Buf *b, int d, int w, int dst, int src);
void x64_vcvtss2sd(Buf *b, int dst, int a, int src);
void x64_vcvtsd2ss(Buf *b, int dst, int a, int src);
void x64_movd_gx(Buf *b, int w, int dst_xmm, int src_gpr);
void x64_movd_xg(Buf *b, int w, int dst_gpr, int src_xmm);

/* ---- 128-bit moves and bitwise (VEX.128) ---------------------------- */
void x64_vmovdqu_load(Buf *b, int dst, int base, int idx, int scale, int32_t disp);
void x64_vmovdqu_store(Buf *b, int src, int base, int idx, int scale, int32_t disp);
void x64_vmovdqa_rr(Buf *b, int dst, int src);
void x64_vandps(Buf *b, int dst, int a, int c);
void x64_vandnps(Buf *b, int dst, int a, int c);
void x64_vorps(Buf *b, int dst, int a, int c);
void x64_vxorps(Buf *b, int dst, int a, int c);

/* ---- SIMD lanes / broadcasts (sz = lane width log2: b/w/d/q) -------- */
void x64_vpbroadcast(Buf *b, int sz, int dst, int src);   /* AVX2, xmm<-xmm */
void x64_vbroadcastss(Buf *b, int dst, int src);          /* AVX2, xmm<-xmm */
void x64_vpshufd(Buf *b, int dst, int src, int imm);
void x64_vpshuflw(Buf *b, int dst, int src, int imm);
void x64_vpshufhw(Buf *b, int dst, int src, int imm);
void x64_vpalignr(Buf *b, int dst, int a, int c, int imm);
void x64_vpblendd(Buf *b, int dst, int a, int c, int imm);
void x64_vinsertps(Buf *b, int dst, int a, int c, int imm);
void x64_vpunpcklqdq(Buf *b, int dst, int a, int c);
void x64_vpextr(Buf *b, int sz, int dst_gpr, int src_xmm, int imm);
void x64_vpinsr(Buf *b, int sz, int dst, int a, int src_gpr, int imm);

/* ---- SIMD integer (VEX.128, AVX2) ----------------------------------- */
/* sz is the lane width log2 (0 = b, 1 = w, 2 = d, 3 = q); an emitter
   whose table has no entry for a width fails to encode rather than
   silently picking another. */
void x64_vpadd(Buf *b, int sz, int dst, int a, int c);
void x64_vpsub(Buf *b, int sz, int dst, int a, int c);
void x64_vpadds(Buf *b, int sz, int dst, int a, int c);    /* signed sat */
void x64_vpaddus(Buf *b, int sz, int dst, int a, int c);
void x64_vpsubs(Buf *b, int sz, int dst, int a, int c);
void x64_vpsubus(Buf *b, int sz, int dst, int a, int c);
void x64_vpavg(Buf *b, int sz, int dst, int a, int c);
void x64_vpmins(Buf *b, int sz, int dst, int a, int c);
void x64_vpmaxs(Buf *b, int sz, int dst, int a, int c);
void x64_vpminu(Buf *b, int sz, int dst, int a, int c);
void x64_vpmaxu(Buf *b, int sz, int dst, int a, int c);
void x64_vpcmpeq(Buf *b, int sz, int dst, int a, int c);
void x64_vpcmpgt(Buf *b, int sz, int dst, int a, int c);
void x64_vpabs(Buf *b, int sz, int dst, int src);
void x64_vpunpckl(Buf *b, int sz, int dst, int a, int c);
void x64_vpunpckh(Buf *b, int sz, int dst, int a, int c);
void x64_vpmullw(Buf *b, int dst, int a, int c);
void x64_vpmulhw(Buf *b, int dst, int a, int c);
void x64_vpmulhuw(Buf *b, int dst, int a, int c);
void x64_vpmulhrsw(Buf *b, int dst, int a, int c);
void x64_vpmulld(Buf *b, int dst, int a, int c);
void x64_vpmuludq(Buf *b, int dst, int a, int c);
void x64_vpmuldq(Buf *b, int dst, int a, int c);
void x64_vpmaddwd(Buf *b, int dst, int a, int c);
void x64_vpmaddubsw(Buf *b, int dst, int a, int c);
void x64_vpand(Buf *b, int dst, int a, int c);
void x64_vpandn(Buf *b, int dst, int a, int c);            /* (~a) & c */
void x64_vpor(Buf *b, int dst, int a, int c);
void x64_vpxor(Buf *b, int dst, int a, int c);
void x64_vpshufb(Buf *b, int dst, int a, int c);
void x64_vpblendw(Buf *b, int dst, int a, int c, int imm);
void x64_vpblendvb(Buf *b, int dst, int a, int c, int mask);
void x64_vpacksswb(Buf *b, int dst, int a, int c);
void x64_vpackssdw(Buf *b, int dst, int a, int c);
void x64_vpackuswb(Buf *b, int dst, int a, int c);
void x64_vpackusdw(Buf *b, int dst, int a, int c);
/* sz names the SOURCE lane width: 0 = bw, 1 = wd, 2 = dq */
void x64_vpmovsx(Buf *b, int sz, int dst, int src);
void x64_vpmovzx(Buf *b, int sz, int dst, int src);
void x64_vpsll_i(Buf *b, int sz, int dst, int src, int imm);
void x64_vpsrl_i(Buf *b, int sz, int dst, int src, int imm);
void x64_vpsra_i(Buf *b, int sz, int dst, int src, int imm);
void x64_vpsll_x(Buf *b, int sz, int dst, int a, int cnt);  /* count in xmm */
void x64_vpsrl_x(Buf *b, int sz, int dst, int a, int cnt);
void x64_vpsra_x(Buf *b, int sz, int dst, int a, int cnt);
void x64_vpslldq(Buf *b, int dst, int src, int imm);        /* whole-reg */
void x64_vpsrldq(Buf *b, int dst, int src, int imm);
void x64_vpmovmskb(Buf *b, int dst_gpr, int src);
void x64_vmovmskp(Buf *b, int d, int dst_gpr, int src);     /* ps / pd */
void x64_vptest(Buf *b, int a, int c);

/* ---- SIMD float (VEX.128; d: 0 = ps/f32x4, 1 = pd/f64x2) ------------ */
void x64_vaddp(Buf *b, int d, int dst, int a, int c);
void x64_vsubp(Buf *b, int d, int dst, int a, int c);
void x64_vmulp(Buf *b, int d, int dst, int a, int c);
void x64_vdivp(Buf *b, int d, int dst, int a, int c);
void x64_vminp(Buf *b, int d, int dst, int a, int c);
void x64_vmaxp(Buf *b, int d, int dst, int a, int c);
void x64_vsqrtp(Buf *b, int d, int dst, int src);
void x64_vcmpp(Buf *b, int d, int dst, int a, int c, int pred);
void x64_vroundp(Buf *b, int d, int dst, int src, int mode);
void x64_vshufps(Buf *b, int dst, int a, int c, int imm);
void x64_vcvtdq2ps(Buf *b, int dst, int src);
void x64_vcvttps2dq(Buf *b, int dst, int src);
void x64_vcvtps2pd(Buf *b, int dst, int src);
void x64_vcvtpd2ps(Buf *b, int dst, int src);
void x64_vcvtdq2pd(Buf *b, int dst, int src);
void x64_vcvttpd2dq(Buf *b, int dst, int src);
void x64_vfmadd231p(Buf *b, int d, int dst, int a, int c);
void x64_vfnmadd231p(Buf *b, int d, int dst, int a, int c);
void x64_vfmadd231s(Buf *b, int d, int dst, int a, int c);
void x64_vfnmadd231s(Buf *b, int d, int dst, int a, int c);
void x64_vfmsub231s(Buf *b, int d, int dst, int a, int c);

/* VEX compare predicates (the ordered/unordered pairs we lower to). */
enum {
  XCMP_EQ_OQ = 0x00, XCMP_LT_OS = 0x01, XCMP_LE_OS = 0x02,
  XCMP_UNORD_Q = 0x03, XCMP_NEQ_UQ = 0x04, XCMP_GE_OS = 0x0d,
  XCMP_GT_OS = 0x0e
};

/* vroundp / vrounds modes (bit 3 suppresses the precision exception). */
enum {
  XRND_NEAREST = 0x08, XRND_FLOOR = 0x09, XRND_CEIL = 0x0a, XRND_TRUNC = 0x0b
};

#endif
