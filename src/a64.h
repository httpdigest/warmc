#ifndef WARMC_A64_H
#define WARMC_A64_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "buf.h"

enum {
  R0, R1, R2, R3, R4, R5, R6, R7,
  R8, R9, R10, R11, R12, R13, R14, R15,
  R16, R17, R18, R19, R20, R21, R22, R23,
  R24, R25, R26, R27, R28, R29, R30, R31
};
#define RZR 31
#define RSP 31
#define RFP R29
#define RLR R30

enum {
  CC_EQ = 0x0, CC_NE = 0x1, CC_CS = 0x2, CC_CC = 0x3,
  CC_MI = 0x4, CC_PL = 0x5, CC_VS = 0x6, CC_VC = 0x7,
  CC_HI = 0x8, CC_LS = 0x9, CC_GE = 0xa, CC_LT = 0xb,
  CC_GT = 0xc, CC_LE = 0xd, CC_AL = 0xe
};

#define CC_HS CC_CS
#define CC_LO CC_CC
static inline int cc_invert(int c) { return c ^ 1; }

#define SF32 0
#define SF64 1

#define FT_S 0
#define FT_D 1

void a64_word(Buf *b, uint32_t insn);

void a64_movz(Buf *b, int sf, int rd, uint16_t imm, int shift);
void a64_movn(Buf *b, int sf, int rd, uint16_t imm, int shift);
void a64_movk(Buf *b, int sf, int rd, uint16_t imm, int shift);
void a64_mov_reg(Buf *b, int sf, int rd, int rm);
void a64_mov_sp(Buf *b, int sf, int rd, int rn);

void a64_load_imm(Buf *b, int sf, int rd, uint64_t value);

void a64_add_imm(Buf *b, int sf, int rd, int rn, uint32_t imm12, int lsl12);
void a64_sub_imm(Buf *b, int sf, int rd, int rn, uint32_t imm12, int lsl12);
void a64_adds_imm(Buf *b, int sf, int rd, int rn, uint32_t imm12, int lsl12);
void a64_subs_imm(Buf *b, int sf, int rd, int rn, uint32_t imm12, int lsl12);
void a64_add_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_sub_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_adds_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_subs_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_add_reg_lsl(Buf *b, int sf, int rd, int rn, int rm, int shift);
void a64_sub_reg_lsl(Buf *b, int sf, int rd, int rn, int rm, int shift);
/* kind SH_LSL, SH_LSR or SH_ASR */
void a64_add_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int shift);
void a64_sub_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int shift);

void a64_add_uxtw(Buf *b, int rd, int rn, int rm, int shift);

void a64_addsub_extw(Buf *b, int sub, int rd, int rn, int rm, int sxtw, int shift);
void a64_add_extx(Buf *b, int rd, int rn, int rm, int shift);
void a64_cmp_uxtw(Buf *b, int rn, int rm, int shift);
void a64_cmp_sp(Buf *b, int rm);
void a64_neg(Buf *b, int sf, int rd, int rm);
void a64_cmp_reg(Buf *b, int sf, int rn, int rm);
void a64_ccmp_imm(Buf *b, int sf, int rn, uint32_t imm5, int nzcv, int cond);
void a64_cmp_imm(Buf *b, int sf, int rn, uint32_t imm12);
void a64_mul(Buf *b, int sf, int rd, int rn, int rm);
void a64_madd(Buf *b, int sf, int rd, int rn, int rm, int ra);
void a64_msub(Buf *b, int sf, int rd, int rn, int rm, int ra);
void a64_umulh(Buf *b, int rd, int rn, int rm);
void a64_smulh(Buf *b, int rd, int rn, int rm);
void a64_umull(Buf *b, int rd, int rn, int rm);
void a64_smull(Buf *b, int rd, int rn, int rm);
void a64_sdiv(Buf *b, int sf, int rd, int rn, int rm);
void a64_udiv(Buf *b, int sf, int rd, int rn, int rm);

void a64_and_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_orr_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_eor_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_ands_reg(Buf *b, int sf, int rd, int rn, int rm);

bool a64_logical_imm(uint64_t value, int sf, uint32_t *encoding);
void a64_and_imm(Buf *b, int sf, int rd, int rn, uint32_t encoding);
void a64_orr_imm(Buf *b, int sf, int rd, int rn, uint32_t encoding);
void a64_eor_imm(Buf *b, int sf, int rd, int rn, uint32_t encoding);
void a64_ror_imm(Buf *b, int sf, int rd, int rn, int shift);
void a64_extr(Buf *b, int sf, int rd, int rn, int rm, int lsb);
void a64_mvn(Buf *b, int sf, int rd, int rm);
void a64_bic_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_orn_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_eon_reg(Buf *b, int sf, int rd, int rn, int rm);
void a64_tst_reg(Buf *b, int sf, int rn, int rm);
void a64_ands_imm(Buf *b, int sf, int rd, int rn, uint32_t encoding);
void a64_tst_imm(Buf *b, int sf, int rn, uint32_t encoding);

#define SH_LSL 0
#define SH_LSR 1
#define SH_ASR 2
#define SH_ROR 3
void a64_and_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int amount);
void a64_orr_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int amount);
void a64_eor_reg_sh(Buf *b, int sf, int rd, int rn, int rm, int kind, int amount);
void a64_lslv(Buf *b, int sf, int rd, int rn, int rm);
void a64_lsrv(Buf *b, int sf, int rd, int rn, int rm);
void a64_asrv(Buf *b, int sf, int rd, int rn, int rm);
void a64_rorv(Buf *b, int sf, int rd, int rn, int rm);
void a64_lsl_imm(Buf *b, int sf, int rd, int rn, int shift);
void a64_lsr_imm(Buf *b, int sf, int rd, int rn, int shift);
void a64_asr_imm(Buf *b, int sf, int rd, int rn, int shift);
void a64_ubfm(Buf *b, int sf, int rd, int rn, int immr, int imms);
void a64_sbfm(Buf *b, int sf, int rd, int rn, int immr, int imms);
void a64_bfm(Buf *b, int sf, int rd, int rn, int immr, int imms);
void a64_clz(Buf *b, int sf, int rd, int rn);
void a64_rbit(Buf *b, int sf, int rd, int rn);
void a64_rev(Buf *b, int sf, int rd, int rn);
void a64_sxtb(Buf *b, int sf, int rd, int rn);
void a64_sxth(Buf *b, int sf, int rd, int rn);
void a64_sxtw(Buf *b, int rd, int rn);
void a64_uxtb(Buf *b, int rd, int rn);
void a64_uxth(Buf *b, int rd, int rn);
void a64_uxtw(Buf *b, int rd, int rn);

void a64_csel(Buf *b, int sf, int rd, int rn, int rm, int cond);
void a64_csinc(Buf *b, int sf, int rd, int rn, int rm, int cond);
void a64_csinv(Buf *b, int sf, int rd, int rn, int rm, int cond);
void a64_csneg(Buf *b, int sf, int rd, int rn, int rm, int cond);
void a64_cset(Buf *b, int sf, int rd, int cond);
void a64_csetm(Buf *b, int sf, int rd, int cond);

void a64_str_imm(Buf *b, int size, int rt, int rn, uint32_t off);
void a64_ldr_imm(Buf *b, int size, int rt, int rn, uint32_t off);
void a64_ldrs_imm(Buf *b, int size, int dst64, int rt, int rn, uint32_t off);
void a64_str_reg(Buf *b, int size, int rt, int rn, int rm, int option, int s);
void a64_ldr_reg(Buf *b, int size, int rt, int rn, int rm, int option, int s);
void a64_ldrs_reg(Buf *b, int size, int dst64, int rt, int rn, int rm, int option, int s);

void a64_fstr_imm(Buf *b, int size, int rt, int rn, uint32_t off);
void a64_fldr_imm(Buf *b, int size, int rt, int rn, uint32_t off);
void a64_fstr_reg(Buf *b, int size, int rt, int rn, int rm, int option, int s);
void a64_fldr_reg(Buf *b, int size, int rt, int rn, int rm, int option, int s);

enum { LSP_POST = 1, LSP_OFF = 2, LSP_PRE = 3 };
void a64_stp(Buf *b, int sf, int rt, int rt2, int rn, int off, int mode);
void a64_ldp(Buf *b, int sf, int rt, int rt2, int rn, int off, int mode);
void a64_fstp(Buf *b, int ft, int rt, int rt2, int rn, int off, int mode);
void a64_fldp(Buf *b, int ft, int rt, int rt2, int rn, int off, int mode);

void a64_stur(Buf *b, int size, int rt, int rn, int off);
void a64_ldur(Buf *b, int size, int rt, int rn, int off);

void a64_b(Buf *b, int32_t imm26);
void a64_bl(Buf *b, int32_t imm26);
void a64_bcond(Buf *b, int cond, int32_t imm19);
void a64_cbz(Buf *b, int sf, int rt, int32_t imm19);
void a64_cbnz(Buf *b, int sf, int rt, int32_t imm19);

void a64_tbz(Buf *b, int rt, int bit, int32_t imm14);
void a64_tbnz(Buf *b, int rt, int bit, int32_t imm14);
void a64_br(Buf *b, int rn);
void a64_blr(Buf *b, int rn);
void a64_ret(Buf *b);
void a64_brk(Buf *b, uint16_t imm);
void a64_nop(Buf *b);
void a64_adr(Buf *b, int rd, int32_t imm21);
void a64_adrp(Buf *b, int rd, int32_t imm21);

void a64_patch_b26(Buf *b, size_t site, size_t target);
void a64_patch_b19(Buf *b, size_t site, size_t target);
void a64_ldr_lit_fp(Buf *b, int ft, int rt);
bool a64_patch_ldr_lit(Buf *b, size_t site, size_t target);
bool a64_patch_b19_ok(Buf *b, size_t site, size_t target);
bool a64_patch_b26_ok(Buf *b, size_t site, size_t target);
void a64_patch_b14(Buf *b, size_t site, size_t target);
void a64_patch_adr(Buf *b, size_t site, size_t target);

void a64_fadd(Buf *b, int ft, int rd, int rn, int rm);
void a64_fsub(Buf *b, int ft, int rd, int rn, int rm);
void a64_fmul(Buf *b, int ft, int rd, int rn, int rm);
void a64_fdiv(Buf *b, int ft, int rd, int rn, int rm);
void a64_fmax(Buf *b, int ft, int rd, int rn, int rm);
void a64_fmin(Buf *b, int ft, int rd, int rn, int rm);
void a64_fabs(Buf *b, int ft, int rd, int rn);
void a64_fneg(Buf *b, int ft, int rd, int rn);
void a64_fsqrt(Buf *b, int ft, int rd, int rn);
void a64_fmov_reg(Buf *b, int ft, int rd, int rn);

bool a64_fmov_imm(uint64_t bits, int ft, uint32_t *imm8);
void a64_fmov_imm_emit(Buf *b, int ft, int rd, uint32_t imm8);
void a64_frint(Buf *b, int ft, int rd, int rn, int mode);
void a64_fcmp(Buf *b, int ft, int rn, int rm);
void a64_fcsel(Buf *b, int ft, int rd, int rn, int rm, int cond);
void a64_fcvt(Buf *b, int dst_ft, int src_ft, int rd, int rn);
void a64_fcvtzs(Buf *b, int sf, int ft, int rd, int rn);
void a64_fcvtzu(Buf *b, int sf, int ft, int rd, int rn);
void a64_scvtf(Buf *b, int sf, int ft, int rd, int rn);
void a64_ucvtf(Buf *b, int sf, int ft, int rd, int rn);
void a64_fmov_to_gpr(Buf *b, int sf, int ft, int rd, int rn);
void a64_fmov_from_gpr(Buf *b, int sf, int ft, int rd, int rn);

void a64_cnt_8b(Buf *b, int rd, int rn);
void a64_addv_8b(Buf *b, int rd, int rn);

void a64_ldr_q(Buf *b, int rt, int rn, uint32_t off);
void a64_str_q(Buf *b, int rt, int rn, uint32_t off);
void a64_ldr_q_reg(Buf *b, int rt, int rn, int rm, int option, int s);
void a64_ldp_q(Buf *b, int rt, int rt2, int rn, int off);
void a64_stp_q(Buf *b, int rt, int rt2, int rn, int off);
void a64_ldur_q(Buf *b, int rt, int rn, int off);
void a64_stur_q(Buf *b, int rt, int rn, int off);
void a64_str_q_reg(Buf *b, int rt, int rn, int rm, int option, int s);

void a64_vmov(Buf *b, int rd, int rn);
void a64_vand(Buf *b, int rd, int rn, int rm);
void a64_vorr(Buf *b, int rd, int rn, int rm);
void a64_veor(Buf *b, int rd, int rn, int rm);
void a64_vbic(Buf *b, int rd, int rn, int rm);
void a64_vorn(Buf *b, int rd, int rn, int rm);
void a64_vnot(Buf *b, int rd, int rn);

void a64_vadd(Buf *b, int size, int rd, int rn, int rm);
void a64_vsub(Buf *b, int size, int rd, int rn, int rm);
void a64_vmul(Buf *b, int size, int rd, int rn, int rm);
void a64_vmla(Buf *b, int size, int rd, int rn, int rm);
void a64_vmls(Buf *b, int size, int rd, int rn, int rm);
void a64_vneg(Buf *b, int size, int rd, int rn);
void a64_vsmax(Buf *b, int size, int rd, int rn, int rm);
void a64_vsmin(Buf *b, int size, int rd, int rn, int rm);
void a64_vumax(Buf *b, int size, int rd, int rn, int rm);
void a64_vumin(Buf *b, int size, int rd, int rn, int rm);
void a64_vuzp1(Buf *b, int size, int rd, int rn, int rm);
void a64_vuzp2(Buf *b, int size, int rd, int rn, int rm);
void a64_vzip1(Buf *b, int size, int rd, int rn, int rm);
void a64_vzip2(Buf *b, int size, int rd, int rn, int rm);
void a64_vcmgt(Buf *b, int size, int rd, int rn, int rm);
void a64_vcmge(Buf *b, int size, int rd, int rn, int rm);
void a64_vcmeq(Buf *b, int size, int rd, int rn, int rm);
void a64_vcmhi(Buf *b, int size, int rd, int rn, int rm);
void a64_vcmhs(Buf *b, int size, int rd, int rn, int rm);
void a64_vbsl(Buf *b, int rd, int rn, int rm);
void a64_vshl_imm(Buf *b, int size, int rd, int rn, int shift);
void a64_vushr_imm(Buf *b, int size, int rd, int rn, int shift);
void a64_vsli_imm(Buf *b, int size, int rd, int rn, int shift);
void a64_vsshr_imm(Buf *b, int size, int rd, int rn, int shift);

void a64_vushl_reg(Buf *b, int size, int rd, int rn, int rm);
void a64_vsshl_reg(Buf *b, int size, int rd, int rn, int rm);
void a64_vdup_gpr(Buf *b, int size, int rd, int rn);
void a64_vdup_lane(Buf *b, int size, int rd, int rn, int index);
void a64_vdup_lane0(Buf *b, int size, int rd, int rn);
void a64_vmovi_b(Buf *b, int rd, uint32_t imm8);

void a64_vumov(Buf *b, int size, int rd, int rn, int index);
void a64_vsmov(Buf *b, int sf, int size, int rd, int rn, int index);
void a64_vins_gpr(Buf *b, int size, int rd, int index, int rn);
void a64_vins_elem(Buf *b, int size, int rd, int didx, int rn, int sidx);
void a64_vext(Buf *b, int rd, int rn, int rm, int imm4);
void a64_vtbl1(Buf *b, int rd, int rn, int rm);
void a64_vtbl2(Buf *b, int rd, int rn, int rm);

void a64_vsqadd(Buf *b, int size, int rd, int rn, int rm);
void a64_vuqadd(Buf *b, int size, int rd, int rn, int rm);
void a64_vsqsub(Buf *b, int size, int rd, int rn, int rm);
void a64_vuqsub(Buf *b, int size, int rd, int rn, int rm);
void a64_vurhadd(Buf *b, int size, int rd, int rn, int rm);
void a64_vsqrdmulh(Buf *b, int size, int rd, int rn, int rm);
void a64_vaddp(Buf *b, int size, int rd, int rn, int rm);
void a64_vumaxp(Buf *b, int size, int rd, int rn, int rm);
void a64_vabs(Buf *b, int size, int rd, int rn);
void a64_vcnt(Buf *b, int rd, int rn);
void a64_vcmlt_zero(Buf *b, int size, int rd, int rn);
void a64_vcmeq_zero(Buf *b, int size, int rd, int rn);

void a64_vaddv(Buf *b, int q, int size, int rd, int rn);
void a64_vuminv(Buf *b, int size, int rd, int rn);
void a64_vumaxv(Buf *b, int size, int rd, int rn);
void a64_vsminv(Buf *b, int size, int rd, int rn);
void a64_vsmaxv(Buf *b, int size, int rd, int rn);

void a64_vsshll(Buf *b, int q, int size, int rd, int rn, int shift);
void a64_vushll(Buf *b, int q, int size, int rd, int rn, int shift);
void a64_vsqxtn(Buf *b, int q, int size, int rd, int rn);
void a64_vuqxtn(Buf *b, int q, int size, int rd, int rn);
void a64_vsqxtun(Buf *b, int q, int size, int rd, int rn);
void a64_vsaddlp(Buf *b, int size, int rd, int rn);
void a64_vuaddlp(Buf *b, int size, int rd, int rn);
/* add-accumulate long pairwise: rd += pairs of rn, size the source lanes' */
void a64_vsadalp(Buf *b, int size, int rd, int rn);
void a64_vuadalp(Buf *b, int size, int rd, int rn);
void a64_vsmull(Buf *b, int q, int size, int rd, int rn, int rm);
void a64_vumull(Buf *b, int q, int size, int rd, int rn, int rm);
void a64_vumlal(Buf *b, int q, int size, int rd, int rn, int rm);
void a64_vsmlal(Buf *b, int q, int size, int rd, int rn, int rm);
/* add returning high narrow: rd's lanes of size (its high half for q) are
   the high halves of rn + rm's, twice as wide */
void a64_vaddhn(Buf *b, int q, int size, int rd, int rn, int rm);
/* ld2, ld3 or ld4 (n) of 16-byte registers rt, rt + 1 ... from [rn],
   deinterleaving lanes of size */
void a64_vldn(Buf *b, int n, int size, int rt, int rn);
void a64_vrev64(Buf *b, int size, int rd, int rn);
void a64_vrev32(Buf *b, int size, int rd, int rn);
void a64_vrev16(Buf *b, int rd, int rn);
void a64_vxtn(Buf *b, int q, int size, int rd, int rn);

void a64_vfadd(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfsub(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfmul(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfdiv(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfmax(Buf *b, int sz, int rd, int rn, int rm);

void a64_vfmla(Buf *b, int sz, int rd, int rn, int rm);
void a64_fmadd(Buf *b, int ft, int form, int rd, int rn, int rm, int ra);
void a64_vfmls(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfmin(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfneg(Buf *b, int sz, int rd, int rn);
void a64_vfabs(Buf *b, int sz, int rd, int rn);
void a64_vfsqrt(Buf *b, int sz, int rd, int rn);
void a64_vfcmeq(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfcmgt(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfcmge(Buf *b, int sz, int rd, int rn, int rm);
void a64_vfrint(Buf *b, int sz, int rd, int rn, int mode);
void a64_vfcvtzs(Buf *b, int sz, int rd, int rn);
void a64_vfcvtzu(Buf *b, int sz, int rd, int rn);
void a64_vscvtf(Buf *b, int sz, int rd, int rn);
void a64_vucvtf(Buf *b, int sz, int rd, int rn);
void a64_vfcvtn(Buf *b, int rd, int rn);
void a64_vfcvtl(Buf *b, int rd, int rn);

void a64_addv_4s(Buf *b, int rd, int rn);
void a64_addp_2d(Buf *b, int rd, int rn);
void a64_umov_d1(Buf *b, int rd, int rn);
void a64_ins_d1_gpr(Buf *b, int rd, int rn);

#endif
