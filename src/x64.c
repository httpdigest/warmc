#include "x64.h"

#include <stdio.h>
#include <stdlib.h>

/* Encoding internals.  Register numbers carry their REX bit in bit 3;
   (r & 7) is the ModRM/SIB field value.  Every choice below mirrors the
   canonical encoding llvm-mc picks for the same mnemonic, so the
   differential oracle can byte-compare whole streams. */

__attribute__((noreturn))
static void enc_fail(const char *what, long long v) {
  fprintf(stderr, "warmc: internal: cannot encode %s = %lld\n", what, v);
  exit(1);
}

static void u8(Buf *b, uint8_t v) { buf_u8(b, v); }
static void u32(Buf *b, uint32_t v) { buf_u32(b, v); }

static bool fits8(int64_t v) { return v >= -128 && v <= 127; }

/* An 8-bit register operand 4..7 (SPL/BPL/SIL/DIL) needs a REX prefix to
   mean the low byte rather than AH..BH. */
static bool byte_needs_rex(int r) { return r >= 4 && r <= 7; }

static void rex(Buf *b, int w, int reg, int idx, int base, bool force) {
  b->ninstr++;
  int v = 0x40 | (w << 3) | ((reg >> 3) << 2) | ((idx >> 3) << 1) | (base >> 3);
  if (v != 0x40 || force) u8(b, v);
}

/* ModRM + SIB + displacement for a (base, idx, scale, disp) operand.
   reg is the /r field.  RIP-relative ends the instruction with a zeroed
   disp32 the caller patches. */
static void modrm_mem(Buf *b, int reg, int base, int idx, int scale, int32_t disp) {
  reg &= 7;
  if (base == X64_RIPREL) {
    if (idx != X64_NOIDX) enc_fail("rip-relative with index", idx);
    u8(b, (uint8_t)((reg << 3) | 5));
    u32(b, (uint32_t)disp);
    return;
  }
  if (idx == X_RSP) enc_fail("rsp as index register", idx);
  bool sib = idx != X64_NOIDX || (base & 7) == 4;
  int mod;
  if (disp == 0 && (base & 7) != 5) mod = 0;
  else if (fits8(disp)) mod = 1;
  else mod = 2;
  if (!sib) {
    u8(b, (uint8_t)((mod << 6) | (reg << 3) | (base & 7)));
  } else {
    u8(b, (uint8_t)((mod << 6) | (reg << 3) | 4));
    int ix = idx == X64_NOIDX ? 4 : (idx & 7);
    u8(b, (uint8_t)((scale << 6) | (ix << 3) | (base & 7)));
  }
  if (mod == 1) u8(b, (uint8_t)disp);
  else if (mod == 2) u32(b, (uint32_t)disp);
}

static void modrm_rr(Buf *b, int reg, int rm) {
  u8(b, (uint8_t)(0xc0 | ((reg & 7) << 3) | (rm & 7)));
}

/* Legacy-map op with a register-direct rm. */
static void op_rr(Buf *b, int w, uint8_t opc, int reg, int rm) {
  rex(b, w, reg, 0, rm, false);
  u8(b, opc);
  modrm_rr(b, reg, rm);
}

static void op_rr_0f(Buf *b, int w, uint8_t opc, int reg, int rm) {
  rex(b, w, reg, 0, rm, false);
  u8(b, 0x0f); u8(b, opc);
  modrm_rr(b, reg, rm);
}

static void op_mem(Buf *b, int w, uint8_t opc, int reg,
                   int base, int idx, int scale, int32_t disp) {
  rex(b, w, reg, idx == X64_NOIDX || idx == X64_RIPREL ? 0 : idx,
      base == X64_RIPREL ? 0 : base, false);
  u8(b, opc);
  modrm_mem(b, reg, base, idx, scale, disp);
}

static void op_mem_0f(Buf *b, int w, uint8_t opc, int reg,
                      int base, int idx, int scale, int32_t disp) {
  rex(b, w, reg, idx == X64_NOIDX || idx == X64_RIPREL ? 0 : idx,
      base == X64_RIPREL ? 0 : base, false);
  u8(b, 0x0f); u8(b, opc);
  modrm_mem(b, reg, base, idx, scale, disp);
}

/* VEX prefix.  mm: 1 = 0F, 2 = 0F38, 3 = 0F3A; pp: 0/1/2/3 = -/66/F3/F2.
   vvvv is the extra source, 0 when the form has none.  Two-byte C5 when
   X, B, W are clear and mm is 0F -- llvm-mc's preference. */
static void vex(Buf *b, int r, int x, int bb, int mm, int w, int vvvv, int l, int pp) {
  b->ninstr++;
  r >>= 3; x >>= 3; bb >>= 3;
  if (!x && !bb && !w && mm == 1) {
    u8(b, 0xc5);
    u8(b, (uint8_t)(((~r & 1) << 7) | ((~vvvv & 0xf) << 3) | (l << 2) | pp));
  } else {
    u8(b, 0xc4);
    u8(b, (uint8_t)(((~r & 1) << 7) | ((~x & 1) << 6) | ((~bb & 1) << 5) | mm));
    u8(b, (uint8_t)((w << 7) | ((~vvvv & 0xf) << 3) | (l << 2) | pp));
  }
}

static void vex_rr(Buf *b, int mm, int w, int pp, int l, uint8_t opc,
                   int reg, int vvvv, int rm) {
  vex(b, reg, 0, rm, mm, w, vvvv, l, pp);
  u8(b, opc);
  modrm_rr(b, reg, rm);
}

static void vex_mem(Buf *b, int mm, int w, int pp, int l, uint8_t opc,
                    int reg, int vvvv, int base, int idx, int scale, int32_t disp) {
  vex(b, reg, idx == X64_NOIDX || idx == X64_RIPREL ? 0 : idx,
      base == X64_RIPREL ? 0 : base, mm, w, vvvv, l, pp);
  u8(b, opc);
  modrm_mem(b, reg, base, idx, scale, disp);
}

void x64_patch_rel32(Buf *b, size_t field, size_t target) {
  int64_t rel = (int64_t)target - ((int64_t)field + 4);
  if (rel < INT32_MIN || rel > INT32_MAX) enc_fail("rel32", rel);
  for (int i = 0; i < 4; i++)
    b->data[field + i] = (uint8_t)((uint64_t)rel >> (8 * i));
}

/* ---- mov / constants ------------------------------------------------ */

void x64_mov_rr(Buf *b, int w, int dst, int src) { op_rr(b, w, 0x89, src, dst); }

void x64_mov_ri(Buf *b, int w, int dst, uint64_t imm) {
  if (!w || imm <= 0xffffffffu) {
    /* B8+r id zero-extends to 64 bits, the shortest correct form. */
    rex(b, 0, 0, 0, dst, false);
    u8(b, (uint8_t)(0xb8 | (dst & 7)));
    u32(b, (uint32_t)imm);
  } else if ((int64_t)imm == (int64_t)(int32_t)imm) {
    rex(b, 1, 0, 0, dst, false);
    u8(b, 0xc7);
    modrm_rr(b, 0, dst);
    u32(b, (uint32_t)imm);
  } else {
    rex(b, 1, 0, 0, dst, false);
    u8(b, (uint8_t)(0xb8 | (dst & 7)));
    buf_u64(b, imm);
  }
}

void x64_movzx8(Buf *b, int w, int dst, int src) {
  rex(b, w, dst, 0, src, byte_needs_rex(src));
  u8(b, 0x0f); u8(b, 0xb6);
  modrm_rr(b, dst, src);
}
void x64_movzx16(Buf *b, int w, int dst, int src) { op_rr_0f(b, w, 0xb7, dst, src); }
void x64_movsx8(Buf *b, int w, int dst, int src) {
  rex(b, w, dst, 0, src, byte_needs_rex(src));
  u8(b, 0x0f); u8(b, 0xbe);
  modrm_rr(b, dst, src);
}
void x64_movsx16(Buf *b, int w, int dst, int src) { op_rr_0f(b, w, 0xbf, dst, src); }
void x64_movsxd(Buf *b, int dst, int src) { op_rr(b, 1, 0x63, dst, src); }

void x64_lea(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp) {
  op_mem(b, w, 0x8d, dst, base, idx, scale, disp);
}

/* ---- ALU ------------------------------------------------------------ */

/* opbase is the x86 group base (add 00, or 08, and 20, sub 28, xor 30,
   cmp 38); the MR reg,reg form is opbase+1, llvm's canonical pick. */
static void alu_rr(Buf *b, int w, uint8_t opbase, int rm, int reg) {
  op_rr(b, w, (uint8_t)(opbase + 1), reg, rm);
  b->flags_epoch++;
}

/* digit is the group's /r digit for the 81/83 immediate forms; the AX
   short form opbase+5 wins only where it is the shortest encoding. */
static void alu_ri(Buf *b, int w, uint8_t opbase, int digit, int dst, int32_t imm) {
  b->flags_epoch++;
  if (fits8(imm)) {
    rex(b, w, 0, 0, dst, false);
    u8(b, 0x83);
    modrm_rr(b, digit, dst);
    u8(b, (uint8_t)imm);
  } else if (dst == X_RAX) {
    rex(b, w, 0, 0, 0, false);
    u8(b, (uint8_t)(opbase + 5));
    u32(b, (uint32_t)imm);
  } else {
    rex(b, w, 0, 0, dst, false);
    u8(b, 0x81);
    modrm_rr(b, digit, dst);
    u32(b, (uint32_t)imm);
  }
}

void x64_add_rr(Buf *b, int w, int dst, int src) { alu_rr(b, w, 0x00, dst, src); }
void x64_sub_rr(Buf *b, int w, int dst, int src) { alu_rr(b, w, 0x28, dst, src); }
void x64_and_rr(Buf *b, int w, int dst, int src) { alu_rr(b, w, 0x20, dst, src); }
void x64_or_rr(Buf *b, int w, int dst, int src) { alu_rr(b, w, 0x08, dst, src); }
void x64_xor_rr(Buf *b, int w, int dst, int src) { alu_rr(b, w, 0x30, dst, src); }
void x64_cmp_rr(Buf *b, int w, int a, int reg) { alu_rr(b, w, 0x38, a, reg); }
void x64_test_rr(Buf *b, int w, int a, int reg) {
  op_rr(b, w, 0x85, reg, a);
  b->flags_epoch++;
}

void x64_add_ri(Buf *b, int w, int dst, int32_t imm) { alu_ri(b, w, 0x00, 0, dst, imm); }
void x64_sub_ri(Buf *b, int w, int dst, int32_t imm) { alu_ri(b, w, 0x28, 5, dst, imm); }
void x64_and_ri(Buf *b, int w, int dst, int32_t imm) { alu_ri(b, w, 0x20, 4, dst, imm); }
void x64_or_ri(Buf *b, int w, int dst, int32_t imm) { alu_ri(b, w, 0x08, 1, dst, imm); }
void x64_xor_ri(Buf *b, int w, int dst, int32_t imm) { alu_ri(b, w, 0x30, 6, dst, imm); }
void x64_cmp_ri(Buf *b, int w, int a, int32_t imm) { alu_ri(b, w, 0x38, 7, a, imm); }

/* One read-modify-write instruction, which is what --profile's per-block
   counter bump wants: nothing to spill around it, and the immediate sits
   in the last four bytes for the back-patch.  Never the imm8 form, since
   the count is not known until the block has been emitted. */
void x64_add_mi32(Buf *b, int w, int base, int idx, int scale, int32_t disp,
                  int32_t imm) {
  b->flags_epoch++;
  rex(b, w, 0, idx == X64_NOIDX || idx == X64_RIPREL ? 0 : idx,
      base == X64_RIPREL ? 0 : base, false);
  u8(b, 0x81);
  modrm_mem(b, 0, base, idx, scale, disp);
  u32(b, (uint32_t)imm);
}

void x64_test_ri(Buf *b, int w, int a, int32_t imm) {
  b->flags_epoch++;
  if (a == X_RAX) {
    rex(b, w, 0, 0, 0, false);
    u8(b, 0xa9);
  } else {
    rex(b, w, 0, 0, a, false);
    u8(b, 0xf7);
    modrm_rr(b, 0, a);
  }
  u32(b, (uint32_t)imm);
}

void x64_neg(Buf *b, int w, int dst) {
  op_rr(b, w, 0xf7, 3, dst);
  b->flags_epoch++;
}
void x64_not(Buf *b, int w, int dst) { op_rr(b, w, 0xf7, 2, dst); }

/* ---- shifts / bits -------------------------------------------------- */

static void shift_ri(Buf *b, int w, int digit, int dst, int imm) {
  b->flags_epoch++;
  rex(b, w, 0, 0, dst, false);
  if (imm == 1) {
    u8(b, 0xd1);
    modrm_rr(b, digit, dst);
  } else {
    u8(b, 0xc1);
    modrm_rr(b, digit, dst);
    u8(b, (uint8_t)imm);
  }
}

void x64_shl_ri(Buf *b, int w, int dst, int imm) { shift_ri(b, w, 4, dst, imm); }
void x64_shr_ri(Buf *b, int w, int dst, int imm) { shift_ri(b, w, 5, dst, imm); }
void x64_sar_ri(Buf *b, int w, int dst, int imm) { shift_ri(b, w, 7, dst, imm); }
void x64_rol_ri(Buf *b, int w, int dst, int imm) { shift_ri(b, w, 0, dst, imm); }
void x64_ror_ri(Buf *b, int w, int dst, int imm) { shift_ri(b, w, 1, dst, imm); }

void x64_shlx(Buf *b, int w, int dst, int src, int cnt) {
  vex_rr(b, 2, w, 1, 0, 0xf7, dst, cnt, src);
}
void x64_sarx(Buf *b, int w, int dst, int src, int cnt) {
  vex_rr(b, 2, w, 2, 0, 0xf7, dst, cnt, src);
}
void x64_shrx(Buf *b, int w, int dst, int src, int cnt) {
  vex_rr(b, 2, w, 3, 0, 0xf7, dst, cnt, src);
}
void x64_rorx(Buf *b, int w, int dst, int src, int imm) {
  vex_rr(b, 3, w, 3, 0, 0xf0, dst, 0, src);
  u8(b, (uint8_t)imm);
}

/* lzcnt/tzcnt/popcnt all write flags */
static void f30f_rr(Buf *b, int w, uint8_t opc, int reg, int rm) {
  b->flags_epoch++;
  u8(b, 0xf3);
  rex(b, w, reg, 0, rm, false);
  u8(b, 0x0f); u8(b, opc);
  modrm_rr(b, reg, rm);
}

void x64_lzcnt(Buf *b, int w, int dst, int src) { f30f_rr(b, w, 0xbd, dst, src); }
void x64_tzcnt(Buf *b, int w, int dst, int src) { f30f_rr(b, w, 0xbc, dst, src); }
void x64_popcnt(Buf *b, int w, int dst, int src) { f30f_rr(b, w, 0xb8, dst, src); }

void x64_bswap(Buf *b, int w, int dst) {
  rex(b, w, 0, 0, dst, false);
  u8(b, 0x0f);
  u8(b, (uint8_t)(0xc8 | (dst & 7)));
}

/* ---- mul / div ------------------------------------------------------ */

void x64_imul_rr(Buf *b, int w, int dst, int src) {
  op_rr_0f(b, w, 0xaf, dst, src);
  b->flags_epoch++;
}

void x64_imul_rri(Buf *b, int w, int dst, int src, int32_t imm) {
  b->flags_epoch++;
  rex(b, w, dst, 0, src, false);
  if (fits8(imm)) {
    u8(b, 0x6b);
    modrm_rr(b, dst, src);
    u8(b, (uint8_t)imm);
  } else {
    u8(b, 0x69);
    modrm_rr(b, dst, src);
    u32(b, (uint32_t)imm);
  }
}

void x64_imul_r(Buf *b, int w, int src) { op_rr(b, w, 0xf7, 5, src); b->flags_epoch++; }
void x64_mul_r(Buf *b, int w, int src) { op_rr(b, w, 0xf7, 4, src); b->flags_epoch++; }
void x64_div_r(Buf *b, int w, int src) { op_rr(b, w, 0xf7, 6, src); b->flags_epoch++; }
void x64_idiv_r(Buf *b, int w, int src) { op_rr(b, w, 0xf7, 7, src); b->flags_epoch++; }

void x64_cdq_cqo(Buf *b, int w) {
  rex(b, w, 0, 0, 0, false);
  u8(b, 0x99);
}

void x64_mulx(Buf *b, int w, int hi, int lo, int src) {
  vex_rr(b, 2, w, 3, 0, 0xf6, hi, lo, src);
}

/* ---- conditionals --------------------------------------------------- */

void x64_setcc(Buf *b, int cc, int dst) {
  rex(b, 0, 0, 0, dst, byte_needs_rex(dst));
  u8(b, 0x0f); u8(b, (uint8_t)(0x90 | cc));
  modrm_rr(b, 0, dst);
}

void x64_cmovcc(Buf *b, int w, int cc, int dst, int src) {
  op_rr_0f(b, w, (uint8_t)(0x40 | cc), dst, src);
}

/* ---- loads / stores ------------------------------------------------- */

void x64_load8zx(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp) {
  op_mem_0f(b, w, 0xb6, dst, base, idx, scale, disp);
}
void x64_load16zx(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp) {
  op_mem_0f(b, w, 0xb7, dst, base, idx, scale, disp);
}
void x64_load8sx(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp) {
  op_mem_0f(b, w, 0xbe, dst, base, idx, scale, disp);
}
void x64_load16sx(Buf *b, int w, int dst, int base, int idx, int scale, int32_t disp) {
  op_mem_0f(b, w, 0xbf, dst, base, idx, scale, disp);
}
void x64_load32(Buf *b, int dst, int base, int idx, int scale, int32_t disp) {
  op_mem(b, 0, 0x8b, dst, base, idx, scale, disp);
}
void x64_load32sx(Buf *b, int dst, int base, int idx, int scale, int32_t disp) {
  op_mem(b, 1, 0x63, dst, base, idx, scale, disp);
}
void x64_load64(Buf *b, int dst, int base, int idx, int scale, int32_t disp) {
  op_mem(b, 1, 0x8b, dst, base, idx, scale, disp);
}

void x64_store8(Buf *b, int src, int base, int idx, int scale, int32_t disp) {
  rex(b, 0, src, idx == X64_NOIDX || idx == X64_RIPREL ? 0 : idx,
      base == X64_RIPREL ? 0 : base, byte_needs_rex(src));
  u8(b, 0x88);
  modrm_mem(b, src, base, idx, scale, disp);
}
void x64_store16(Buf *b, int src, int base, int idx, int scale, int32_t disp) {
  u8(b, 0x66);
  op_mem(b, 0, 0x89, src, base, idx, scale, disp);
}
void x64_store32(Buf *b, int src, int base, int idx, int scale, int32_t disp) {
  op_mem(b, 0, 0x89, src, base, idx, scale, disp);
}
void x64_store64(Buf *b, int src, int base, int idx, int scale, int32_t disp) {
  op_mem(b, 1, 0x89, src, base, idx, scale, disp);
}

/* ---- flow / stack --------------------------------------------------- */

void x64_push(Buf *b, int r) {
  rex(b, 0, 0, 0, r, false);
  u8(b, (uint8_t)(0x50 | (r & 7)));
}
void x64_pop(Buf *b, int r) {
  rex(b, 0, 0, 0, r, false);
  u8(b, (uint8_t)(0x58 | (r & 7)));
}

void x64_call_rel32(Buf *b) {
  b->ninstr++; u8(b, 0xe8); u32(b, 0); b->flags_epoch++;
}
void x64_call_r(Buf *b, int r) {
  rex(b, 0, 0, 0, r, false);
  u8(b, 0xff);
  modrm_rr(b, 2, r);
  b->flags_epoch++;
}
void x64_jmp_rel32(Buf *b) { b->ninstr++; u8(b, 0xe9); u32(b, 0); }
void x64_jmp_r(Buf *b, int r) {
  rex(b, 0, 0, 0, r, false);
  u8(b, 0xff);
  modrm_rr(b, 4, r);
}
void x64_jcc_rel32(Buf *b, int cc) {
  b->ninstr++;
  u8(b, 0x0f);
  u8(b, (uint8_t)(0x80 | cc));
  u32(b, 0);
}
void x64_ret(Buf *b) { b->ninstr++; u8(b, 0xc3); }
void x64_ud2(Buf *b) { b->ninstr++; u8(b, 0x0f); u8(b, 0x0b); }
void x64_int3(Buf *b) { b->ninstr++; u8(b, 0xcc); }
void x64_nop(Buf *b) { b->ninstr++; u8(b, 0x90); }

void x64_nop_n(Buf *b, int n) {
  static const uint8_t pads[9][9] = {
    { 0x90 },
    { 0x66, 0x90 },
    { 0x0f, 0x1f, 0x00 },
    { 0x0f, 0x1f, 0x40, 0x00 },
    { 0x0f, 0x1f, 0x44, 0x00, 0x00 },
    { 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00 },
    { 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00 },
    { 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
  };
  while (n > 9) { b->ninstr++; buf_bytes(b, pads[8], 9); n -= 9; }
  if (n > 0) { b->ninstr++; buf_bytes(b, pads[n - 1], (size_t)n); }
}

/* ---- scalar FP (VEX) ------------------------------------------------ */

/* pp for the ss/sd pair: F3 for f32, F2 for f64. */
static int sd_pp(int d) { return d ? 3 : 2; }

void x64_vmovs_load(Buf *b, int d, int dst, int base, int idx, int scale, int32_t disp) {
  vex_mem(b, 1, 0, sd_pp(d), 0, 0x10, dst, 0, base, idx, scale, disp);
}
void x64_vmovs_store(Buf *b, int d, int src, int base, int idx, int scale, int32_t disp) {
  vex_mem(b, 1, 0, sd_pp(d), 0, 0x11, src, 0, base, idx, scale, disp);
}
void x64_vmovs_rr(Buf *b, int d, int dst, int src1, int src2) {
  vex_rr(b, 1, 0, sd_pp(d), 0, 0x10, dst, src1, src2);
}

static void fp3(Buf *b, int d, uint8_t opc, int dst, int a, int c) {
  vex_rr(b, 1, 0, sd_pp(d), 0, opc, dst, a, c);
}

void x64_vadds(Buf *b, int d, int dst, int a, int c) { fp3(b, d, 0x58, dst, a, c); }
void x64_vsubs(Buf *b, int d, int dst, int a, int c) { fp3(b, d, 0x5c, dst, a, c); }
void x64_vmuls(Buf *b, int d, int dst, int a, int c) { fp3(b, d, 0x59, dst, a, c); }
void x64_vdivs(Buf *b, int d, int dst, int a, int c) { fp3(b, d, 0x5e, dst, a, c); }
void x64_vmins(Buf *b, int d, int dst, int a, int c) { fp3(b, d, 0x5d, dst, a, c); }
void x64_vmaxs(Buf *b, int d, int dst, int a, int c) { fp3(b, d, 0x5f, dst, a, c); }
void x64_vsqrts(Buf *b, int d, int dst, int a, int c) { fp3(b, d, 0x51, dst, a, c); }

void x64_vucomis(Buf *b, int d, int a, int c) {
  vex_rr(b, 1, 0, d ? 1 : 0, 0, 0x2e, a, 0, c);
  b->flags_epoch++;
}

void x64_vrounds(Buf *b, int d, int dst, int a, int c, int mode) {
  vex_rr(b, 3, 0, 1, 0, (uint8_t)(0x0a + d), dst, a, c);
  u8(b, (uint8_t)mode);
}

void x64_vcvtsi2s(Buf *b, int d, int w, int dst, int a, int src) {
  vex_rr(b, 1, w, sd_pp(d), 0, 0x2a, dst, a, src);
}
void x64_vcvtts2si(Buf *b, int d, int w, int dst, int src) {
  vex_rr(b, 1, w, sd_pp(d), 0, 0x2c, dst, 0, src);
}
void x64_vcvtss2sd(Buf *b, int dst, int a, int src) {
  vex_rr(b, 1, 0, 2, 0, 0x5a, dst, a, src);
}
void x64_vcvtsd2ss(Buf *b, int dst, int a, int src) {
  vex_rr(b, 1, 0, 3, 0, 0x5a, dst, a, src);
}

void x64_movd_gx(Buf *b, int w, int dst_xmm, int src_gpr) {
  vex_rr(b, 1, w, 1, 0, 0x6e, dst_xmm, 0, src_gpr);
}
void x64_movd_xg(Buf *b, int w, int dst_gpr, int src_xmm) {
  vex_rr(b, 1, w, 1, 0, 0x7e, src_xmm, 0, dst_gpr);
}

/* ---- 128-bit moves and bitwise -------------------------------------- */

void x64_vmovdqu_load(Buf *b, int dst, int base, int idx, int scale, int32_t disp) {
  vex_mem(b, 1, 0, 2, 0, 0x6f, dst, 0, base, idx, scale, disp);
}
void x64_vmovdqu_store(Buf *b, int src, int base, int idx, int scale, int32_t disp) {
  vex_mem(b, 1, 0, 2, 0, 0x7f, src, 0, base, idx, scale, disp);
}
void x64_vmovdqa_rr(Buf *b, int dst, int src) {
  /* the MR form keeps the VEX prefix two bytes when only src is high --
     llvm-mc's pick, so the oracle sees identical streams */
  if (src >= 8 && dst < 8) vex_rr(b, 1, 0, 1, 0, 0x7f, src, 0, dst);
  else vex_rr(b, 1, 0, 1, 0, 0x6f, dst, 0, src);
}

static void bit128(Buf *b, uint8_t opc, int dst, int a, int c) {
  vex_rr(b, 1, 0, 0, 0, opc, dst, a, c);
}
void x64_vandps(Buf *b, int dst, int a, int c) { bit128(b, 0x54, dst, a, c); }
void x64_vandnps(Buf *b, int dst, int a, int c) { bit128(b, 0x55, dst, a, c); }
void x64_vorps(Buf *b, int dst, int a, int c) { bit128(b, 0x56, dst, a, c); }
void x64_vxorps(Buf *b, int dst, int a, int c) { bit128(b, 0x57, dst, a, c); }

/* ---- SIMD lanes / broadcasts (the M7 first group) ------------------- */

/* sz is the lane width log2: 0 = b, 1 = w, 2 = d, 3 = q. */
void x64_vpbroadcast(Buf *b, int sz, int dst, int src) {
  static const uint8_t opc[4] = { 0x78, 0x79, 0x58, 0x59 };
  vex_rr(b, 2, 0, 1, 0, opc[sz], dst, 0, src);
}
void x64_vbroadcastss(Buf *b, int dst, int src) {
  vex_rr(b, 2, 0, 1, 0, 0x18, dst, 0, src);
}
void x64_vpshufd(Buf *b, int dst, int src, int imm) {
  vex_rr(b, 1, 0, 1, 0, 0x70, dst, 0, src);
  u8(b, (uint8_t)imm);
}
void x64_vpshuflw(Buf *b, int dst, int src, int imm) {
  vex_rr(b, 1, 0, 3, 0, 0x70, dst, 0, src);
  u8(b, (uint8_t)imm);
}
void x64_vpshufhw(Buf *b, int dst, int src, int imm) {
  vex_rr(b, 1, 0, 2, 0, 0x70, dst, 0, src);
  u8(b, (uint8_t)imm);
}
/* (a:c) >> 8 * imm: c's bytes from imm up, then a's */
void x64_vpalignr(Buf *b, int dst, int a, int c, int imm) {
  vex_rr(b, 3, 0, 1, 0, 0x0f, dst, a, c);
  u8(b, (uint8_t)imm);
}
void x64_vpunpcklqdq(Buf *b, int dst, int a, int c) {
  vex_rr(b, 1, 0, 1, 0, 0x6c, dst, a, c);
}
void x64_vpextr(Buf *b, int sz, int dst_gpr, int src_xmm, int imm) {
  if (sz == 1) vex_rr(b, 1, 0, 1, 0, 0xc5, dst_gpr, 0, src_xmm);
  else if (sz == 0) vex_rr(b, 3, 0, 1, 0, 0x14, src_xmm, 0, dst_gpr);
  else vex_rr(b, 3, sz == 3, 1, 0, 0x16, src_xmm, 0, dst_gpr);
  u8(b, (uint8_t)imm);
}
void x64_vpinsr(Buf *b, int sz, int dst, int a, int src_gpr, int imm) {
  if (sz == 1) vex_rr(b, 1, 0, 1, 0, 0xc4, dst, a, src_gpr);
  else if (sz == 0) vex_rr(b, 3, 0, 1, 0, 0x20, dst, a, src_gpr);
  else vex_rr(b, 3, sz == 3, 1, 0, 0x22, dst, a, src_gpr);
  u8(b, (uint8_t)imm);
}

/* ---- SIMD integer (AVX2) -------------------------------------------- */

/* Lane-width tables hold the opcode plus its map: 0x100 marks 0F38, and a
   zero entry means the hardware has no instruction for that width. */
#define M38 0x100

static void pv(Buf *b, uint16_t e, int dst, int a, int c) {
  vex_rr(b, (e & M38) ? 2 : 1, 0, 1, 0, (uint8_t)e, dst, a, c);
}

static uint16_t lane(const uint16_t *t, int n, int sz) {
  if (sz < 0 || sz >= n || !t[sz]) enc_fail("simd lane width", sz);
  return t[sz];
}

#define LANE_OP(name, ...)                                                     \
  void name(Buf *b, int sz, int dst, int a, int c) {                           \
    static const uint16_t o[4] = { __VA_ARGS__ };                              \
    pv(b, lane(o, 4, sz), dst, a, c);                                          \
  }

LANE_OP(x64_vpadd,    0xfc, 0xfd, 0xfe, 0xd4)
LANE_OP(x64_vpsub,    0xf8, 0xf9, 0xfa, 0xfb)
LANE_OP(x64_vpadds,   0xec, 0xed, 0, 0)
LANE_OP(x64_vpaddus,  0xdc, 0xdd, 0, 0)
LANE_OP(x64_vpsubs,   0xe8, 0xe9, 0, 0)
LANE_OP(x64_vpsubus,  0xd8, 0xd9, 0, 0)
LANE_OP(x64_vpavg,    0xe0, 0xe3, 0, 0)
LANE_OP(x64_vpmins,   M38 | 0x38, 0xea, M38 | 0x39, 0)
LANE_OP(x64_vpmaxs,   M38 | 0x3c, 0xee, M38 | 0x3d, 0)
LANE_OP(x64_vpminu,   0xda, M38 | 0x3a, M38 | 0x3b, 0)
LANE_OP(x64_vpmaxu,   0xde, M38 | 0x3e, M38 | 0x3f, 0)
LANE_OP(x64_vpcmpeq,  0x74, 0x75, 0x76, M38 | 0x29)
LANE_OP(x64_vpcmpgt,  0x64, 0x65, 0x66, M38 | 0x37)
LANE_OP(x64_vpunpckl, 0x60, 0x61, 0x62, 0x6c)
LANE_OP(x64_vpunpckh, 0x68, 0x69, 0x6a, 0x6d)

void x64_vpabs(Buf *b, int sz, int dst, int src) {
  static const uint16_t o[4] = { M38 | 0x1c, M38 | 0x1d, M38 | 0x1e, 0 };
  pv(b, lane(o, 4, sz), dst, 0, src);
}

void x64_vpmullw(Buf *b, int dst, int a, int c) { pv(b, 0xd5, dst, a, c); }
void x64_vpmulhw(Buf *b, int dst, int a, int c) { pv(b, 0xe5, dst, a, c); }
void x64_vpmulhuw(Buf *b, int dst, int a, int c) { pv(b, 0xe4, dst, a, c); }
void x64_vpmulhrsw(Buf *b, int dst, int a, int c) { pv(b, M38 | 0x0b, dst, a, c); }
void x64_vpmulld(Buf *b, int dst, int a, int c) { pv(b, M38 | 0x40, dst, a, c); }
void x64_vpmuludq(Buf *b, int dst, int a, int c) { pv(b, 0xf4, dst, a, c); }
void x64_vpmuldq(Buf *b, int dst, int a, int c) { pv(b, M38 | 0x28, dst, a, c); }
void x64_vpmaddwd(Buf *b, int dst, int a, int c) { pv(b, 0xf5, dst, a, c); }
void x64_vpmaddubsw(Buf *b, int dst, int a, int c) { pv(b, M38 | 0x04, dst, a, c); }
void x64_vpand(Buf *b, int dst, int a, int c) { pv(b, 0xdb, dst, a, c); }
void x64_vpandn(Buf *b, int dst, int a, int c) { pv(b, 0xdf, dst, a, c); }
void x64_vpor(Buf *b, int dst, int a, int c) { pv(b, 0xeb, dst, a, c); }
void x64_vpxor(Buf *b, int dst, int a, int c) { pv(b, 0xef, dst, a, c); }
void x64_vpshufb(Buf *b, int dst, int a, int c) { pv(b, M38 | 0x00, dst, a, c); }
void x64_vpacksswb(Buf *b, int dst, int a, int c) { pv(b, 0x63, dst, a, c); }
void x64_vpackssdw(Buf *b, int dst, int a, int c) { pv(b, 0x6b, dst, a, c); }
void x64_vpackuswb(Buf *b, int dst, int a, int c) { pv(b, 0x67, dst, a, c); }
void x64_vpackusdw(Buf *b, int dst, int a, int c) { pv(b, M38 | 0x2b, dst, a, c); }

void x64_vpblendw(Buf *b, int dst, int a, int c, int imm) {
  vex_rr(b, 3, 0, 1, 0, 0x0e, dst, a, c);
  u8(b, (uint8_t)imm);
}
/* a with its dword imm[5:4] c's dword imm[7:6], the dwords in imm[3:0]
   zeroed */
void x64_vinsertps(Buf *b, int dst, int a, int c, int imm) {
  vex_rr(b, 3, 0, 1, 0, 0x21, dst, a, c);
  u8(b, (uint8_t)imm);
}
void x64_vpblendd(Buf *b, int dst, int a, int c, int imm) {
  vex_rr(b, 3, 0, 1, 0, 0x02, dst, a, c);
  u8(b, (uint8_t)imm);
}

/* The mask register rides in imm8[7:4] (the "is4" byte). */
void x64_vpblendvb(Buf *b, int dst, int a, int c, int mask) {
  vex_rr(b, 3, 0, 1, 0, 0x4c, dst, a, c);
  u8(b, (uint8_t)(mask << 4));
}

void x64_vpmovsx(Buf *b, int sz, int dst, int src) {
  static const uint16_t o[3] = { M38 | 0x20, M38 | 0x23, M38 | 0x25 };
  pv(b, lane(o, 3, sz), dst, 0, src);
}
void x64_vpmovzx(Buf *b, int sz, int dst, int src) {
  static const uint16_t o[3] = { M38 | 0x30, M38 | 0x33, M38 | 0x35 };
  pv(b, lane(o, 3, sz), dst, 0, src);
}

/* Immediate shifts put the destination in vvvv and the opcode's extension
   digit in the /r field. */
static void shift_i(Buf *b, int sz, int digit, int dst, int src, int imm) {
  static const uint16_t o[4] = { 0, 0x71, 0x72, 0x73 };
  vex_rr(b, 1, 0, 1, 0, (uint8_t)lane(o, 4, sz), digit, dst, src);
  u8(b, (uint8_t)imm);
}

void x64_vpsll_i(Buf *b, int sz, int dst, int src, int imm) {
  shift_i(b, sz, 6, dst, src, imm);
}
void x64_vpsrl_i(Buf *b, int sz, int dst, int src, int imm) {
  shift_i(b, sz, 2, dst, src, imm);
}
void x64_vpsra_i(Buf *b, int sz, int dst, int src, int imm) {
  if (sz == 3) enc_fail("vpsraq (no AVX2 form)", sz);
  shift_i(b, sz, 4, dst, src, imm);
}
void x64_vpslldq(Buf *b, int dst, int src, int imm) {
  shift_i(b, 3, 7, dst, src, imm);
}
void x64_vpsrldq(Buf *b, int dst, int src, int imm) {
  shift_i(b, 3, 3, dst, src, imm);
}

void x64_vpsll_x(Buf *b, int sz, int dst, int a, int cnt) {
  static const uint16_t o[4] = { 0, 0xf1, 0xf2, 0xf3 };
  pv(b, lane(o, 4, sz), dst, a, cnt);
}
void x64_vpsrl_x(Buf *b, int sz, int dst, int a, int cnt) {
  static const uint16_t o[4] = { 0, 0xd1, 0xd2, 0xd3 };
  pv(b, lane(o, 4, sz), dst, a, cnt);
}
void x64_vpsra_x(Buf *b, int sz, int dst, int a, int cnt) {
  static const uint16_t o[4] = { 0, 0xe1, 0xe2, 0 };
  pv(b, lane(o, 4, sz), dst, a, cnt);
}

void x64_vpmovmskb(Buf *b, int dst_gpr, int src) {
  vex_rr(b, 1, 0, 1, 0, 0xd7, dst_gpr, 0, src);
}
void x64_vmovmskp(Buf *b, int d, int dst_gpr, int src) {
  vex_rr(b, 1, 0, d ? 1 : 0, 0, 0x50, dst_gpr, 0, src);
}
void x64_vptest(Buf *b, int a, int c) {
  vex_rr(b, 2, 0, 1, 0, 0x17, a, 0, c);
  b->flags_epoch++;
}

/* ---- SIMD float ------------------------------------------------------ */

/* pp for the ps/pd pair: none for f32, 66 for f64. */
static void fp3p(Buf *b, int d, uint8_t opc, int dst, int a, int c) {
  vex_rr(b, 1, 0, d ? 1 : 0, 0, opc, dst, a, c);
}

void x64_vaddp(Buf *b, int d, int dst, int a, int c) { fp3p(b, d, 0x58, dst, a, c); }
void x64_vsubp(Buf *b, int d, int dst, int a, int c) { fp3p(b, d, 0x5c, dst, a, c); }
void x64_vmulp(Buf *b, int d, int dst, int a, int c) { fp3p(b, d, 0x59, dst, a, c); }
void x64_vdivp(Buf *b, int d, int dst, int a, int c) { fp3p(b, d, 0x5e, dst, a, c); }
void x64_vminp(Buf *b, int d, int dst, int a, int c) { fp3p(b, d, 0x5d, dst, a, c); }
void x64_vmaxp(Buf *b, int d, int dst, int a, int c) { fp3p(b, d, 0x5f, dst, a, c); }
void x64_vsqrtp(Buf *b, int d, int dst, int src) { fp3p(b, d, 0x51, dst, 0, src); }

void x64_vcmpp(Buf *b, int d, int dst, int a, int c, int pred) {
  fp3p(b, d, 0xc2, dst, a, c);
  u8(b, (uint8_t)pred);
}
void x64_vroundp(Buf *b, int d, int dst, int src, int mode) {
  vex_rr(b, 3, 0, 1, 0, (uint8_t)(0x08 + d), dst, 0, src);
  u8(b, (uint8_t)mode);
}
void x64_vshufps(Buf *b, int dst, int a, int c, int imm) {
  fp3p(b, 0, 0xc6, dst, a, c);
  u8(b, (uint8_t)imm);
}

void x64_vcvtdq2ps(Buf *b, int dst, int src) { vex_rr(b, 1, 0, 0, 0, 0x5b, dst, 0, src); }
void x64_vcvttps2dq(Buf *b, int dst, int src) { vex_rr(b, 1, 0, 2, 0, 0x5b, dst, 0, src); }
void x64_vcvtps2pd(Buf *b, int dst, int src) { vex_rr(b, 1, 0, 0, 0, 0x5a, dst, 0, src); }
void x64_vcvtpd2ps(Buf *b, int dst, int src) { vex_rr(b, 1, 0, 1, 0, 0x5a, dst, 0, src); }
void x64_vcvtdq2pd(Buf *b, int dst, int src) { vex_rr(b, 1, 0, 2, 0, 0xe6, dst, 0, src); }
void x64_vcvttpd2dq(Buf *b, int dst, int src) { vex_rr(b, 1, 0, 1, 0, 0xe6, dst, 0, src); }

/* FMA's ps/pd choice rides the VEX.W bit rather than the prefix. */
void x64_vfmadd231p(Buf *b, int d, int dst, int a, int c) {
  vex_rr(b, 2, d, 1, 0, 0xb8, dst, a, c);
}
void x64_vfnmadd231p(Buf *b, int d, int dst, int a, int c) {
  vex_rr(b, 2, d, 1, 0, 0xbc, dst, a, c);
}
/* the scalar forms, ss or sd by VEX.W too: dst = a * c + dst, -(a * c) + dst
   and a * c - dst */
void x64_vfmadd231s(Buf *b, int d, int dst, int a, int c) {
  vex_rr(b, 2, d, 1, 0, 0xb9, dst, a, c);
}
void x64_vfnmadd231s(Buf *b, int d, int dst, int a, int c) {
  vex_rr(b, 2, d, 1, 0, 0xbd, dst, a, c);
}
void x64_vfmsub231s(Buf *b, int d, int dst, int a, int c) {
  vex_rr(b, 2, d, 1, 0, 0xbb, dst, a, c);
}
