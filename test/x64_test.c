/* x64 encoder oracle test.
 *
 * Same contract as a64_test.c: every case emits our encoding AND the
 * equivalent Intel-syntax text; the harness assembles the text with
 * llvm-mc (-triple=x86_64-pc-windows-msvc -mcpu=x86-64-v3) and byte-
 * compares the streams.  x64 instructions are variable length, so a
 * third output file records each case's start offset -- the runner maps
 * a divergent byte back to its case through it.
 *
 * Out-of-range operands cannot be asserted here: enc_fail aborts the
 * process by design, the same property a64_test.c documents.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/x64.h"

static Buf ours;
static FILE *asmf;
static FILE *offf;
static int ncases;
static int nmiscount;
static size_t cur_start;

/* Every case also weighs the encoder's own instruction tally, which is
   what --profile patches into each block's counter bump.  One case is one
   instruction unless it says otherwise, and the runner already fails when
   an emitter is never exercised -- so the whole emitter inventory is
   covered by the same sweep that byte-compares it against llvm-mc. */
#define CASEN(n, text, ...)                                                    \
  do {                                                                         \
    cur_start = ours.len;                                                      \
    uint32_t ni_ = ours.ninstr;                                                \
    __VA_ARGS__;                                                               \
    if (ours.ninstr - ni_ != (uint32_t)(n)) {                                  \
      fprintf(stderr, "case \"%s\": counted %u instructions, "                 \
              "expected %d\n", text, ours.ninstr - ni_, (int)(n));             \
      nmiscount++;                                                             \
    }                                                                          \
    fprintf(asmf, "\t%s\n", text);                                             \
    fprintf(offf, "%zu\t%s\n", cur_start, text);                               \
    ncases++;                                                                  \
  } while (0)

#define CASE(text, ...) CASEN(1, text, __VA_ARGS__)

/* printf-composed text for generated sweeps */
#define CASEF(code, ...)                                                       \
  do {                                                                         \
    char text_[128];                                                           \
    snprintf(text_, sizeof text_, __VA_ARGS__);                                \
    CASE(text_, code);                                                         \
  } while (0)

static const char *r64[16] = {
  "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};
static const char *r32[16] = {
  "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
  "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
};
static const char *r16[16] = {
  "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
  "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"
};
static const char *r8[16] = {
  "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
  "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"
};
static const char *xmm[16] = {
  "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
  "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
};
static const char *rw(int w, int r) { return w ? r64[r] : r32[r]; }

/* condition-code suffixes in tttn order */
static const char *ccs[16] = {
  "o", "no", "b", "ae", "e", "ne", "be", "a",
  "s", "ns", "p", "np", "l", "ge", "le", "g"
};

/* the memory-operand text for (base, idx, scale, disp) */
static void memtext(char *out, size_t n, int base, int idx, int scale, int32_t disp) {
  char *p = out;
  p += snprintf(p, n, "[%s", r64[base]);
  if (idx != X64_NOIDX)
    p += snprintf(p, n - (size_t)(p - out), " + %s*%d", r64[idx], 1 << scale);
  if (disp > 0) snprintf(p, n - (size_t)(p - out), " + %d]", disp);
  else if (disp < 0) snprintf(p, n - (size_t)(p - out), " - %lld]", -(long long)disp);
  else snprintf(p, n - (size_t)(p - out), "]");
}

/* Every (base, idx, scale, disp-class) shape over lea + a load + a byte
 * store: the disp8/disp32 selection and the rbp/r13/rsp/r12 special rows
 * live here, not in hand-picked lines. */
static void addressing_sweep(Buf *b) {
  static const int32_t disps[8] = { 0, 1, 127, 128, -128, -129, 2147483647,
                                    (int32_t)-2147483647 - 1 };
  char m[96];
  for (int base = 0; base < 16; base++) {
    for (int ii = -1; ii < 16; ii++) {
      if (ii == X_RSP) continue;
      int nscale = ii < 0 ? 1 : 4;
      for (int scale = 0; scale < nscale; scale++) {
        for (int di = 0; di < 8; di++) {
          int idx = ii < 0 ? X64_NOIDX : ii;
          int32_t disp = disps[di];
          memtext(m, sizeof m, base, idx, scale, disp);
          CASEF(x64_lea(b, 1, X_RAX, base, idx, scale, disp), "lea rax, %s", m);
          CASEF(x64_load64(b, X_RCX, base, idx, scale, disp),
                "mov rcx, qword ptr %s", m);
          CASEF(x64_store8(b, X_RSI, base, idx, scale, disp),
                "mov byte ptr %s, sil", m);
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: x64_test OURS.bin EXPECTED.s OFFSETS.txt\n");
    return 2;
  }
  asmf = fopen(argv[2], "wb");
  offf = fopen(argv[3], "wb");
  if (!asmf || !offf) { perror("open"); return 2; }
  fprintf(asmf, ".intel_syntax noprefix\n\t.text\n");
  Buf *b = &ours;

  /* ---- mov ---- */
  CASE("mov eax, ecx", x64_mov_rr(b, 0, X_RAX, X_RCX));
  CASE("mov r15, rdx", x64_mov_rr(b, 1, X_R15, X_RDX));
  CASE("mov rbp, r9", x64_mov_rr(b, 1, X_RBP, X_R9));
  CASE("mov esi, r12d", x64_mov_rr(b, 0, X_RSI, X_R12));

  CASE("mov eax, 0", x64_mov_ri(b, 0, X_RAX, 0));
  CASE("mov r11d, 305419896", x64_mov_ri(b, 0, X_R11, 0x12345678));
  CASE("mov ecx, 4294967295", x64_mov_ri(b, 1, X_RCX, 0xffffffffu));
  CASE("mov rdx, -1", x64_mov_ri(b, 1, X_RDX, ~0ull));
  CASE("mov r13, -2147483648", x64_mov_ri(b, 1, X_R13, (uint64_t)(int64_t)INT32_MIN));
  CASE("movabs rax, 4886718345", x64_mov_ri(b, 1, X_RAX, 0x123456789ull));
  CASE("movabs r8, -4886718345", x64_mov_ri(b, 1, X_R8, (uint64_t)-0x123456789ll));

  /* ---- widen/narrow ---- */
  CASE("movzx eax, cl", x64_movzx8(b, 0, X_RAX, X_RCX));
  CASE("movzx r10, sil", x64_movzx8(b, 1, X_R10, X_RSI));
  CASE("movzx edx, dil", x64_movzx8(b, 0, X_RDX, X_RDI));
  CASE("movzx ecx, bpl", x64_movzx8(b, 0, X_RCX, X_RBP));
  CASE("movzx eax, spl", x64_movzx8(b, 0, X_RAX, X_RSP));
  CASE("movzx eax, r15b", x64_movzx8(b, 0, X_RAX, X_R15));
  CASE("movzx eax, cx", x64_movzx16(b, 0, X_RAX, X_RCX));
  CASE("movzx r9, r10w", x64_movzx16(b, 1, X_R9, X_R10));
  CASE("movsx eax, cl", x64_movsx8(b, 0, X_RAX, X_RCX));
  CASE("movsx r11, dil", x64_movsx8(b, 1, X_R11, X_RDI));
  CASE("movsx edx, bx", x64_movsx16(b, 0, X_RDX, X_RBX));
  CASE("movsx r14, r15w", x64_movsx16(b, 1, X_R14, X_R15));
  CASE("movsxd rax, ecx", x64_movsxd(b, X_RAX, X_RCX));
  CASE("movsxd r12, r13d", x64_movsxd(b, X_R12, X_R13));

  /* ---- ALU rr ---- */
  CASE("add eax, ecx", x64_add_rr(b, 0, X_RAX, X_RCX));
  CASE("add r15, r8", x64_add_rr(b, 1, X_R15, X_R8));
  CASE("sub rbx, rbp", x64_sub_rr(b, 1, X_RBX, X_RBP));
  CASE("sub r9d, esi", x64_sub_rr(b, 0, X_R9, X_RSI));
  CASE("and rax, r12", x64_and_rr(b, 1, X_RAX, X_R12));
  CASE("or edi, r13d", x64_or_rr(b, 0, X_RDI, X_R13));
  CASE("xor r14, rcx", x64_xor_rr(b, 1, X_R14, X_RCX));
  CASE("cmp rdx, r10", x64_cmp_rr(b, 1, X_RDX, X_R10));
  CASE("cmp esi, eax", x64_cmp_rr(b, 0, X_RSI, X_RAX));
  CASE("test rcx, rbx", x64_test_rr(b, 1, X_RCX, X_RBX));
  CASE("test r8d, r9d", x64_test_rr(b, 0, X_R8, X_R9));

  /* ---- ALU ri: the 83/81/AX-form boundaries ---- */
  {
    static const int32_t imms[7] = { -129, -128, -1, 0, 1, 127, 128 };
    static const int regs[3] = { X_RAX, X_RCX, X_R13 };
    for (int ri = 0; ri < 3; ri++)
      for (int ii = 0; ii < 7; ii++)
        for (int w = 0; w < 2; w++) {
          int r = regs[ri];
          int32_t imm = imms[ii];
          CASEF(x64_add_ri(b, w, r, imm), "add %s, %d", rw(w, r), imm);
          CASEF(x64_sub_ri(b, w, r, imm), "sub %s, %d", rw(w, r), imm);
          CASEF(x64_and_ri(b, w, r, imm), "and %s, %d", rw(w, r), imm);
          CASEF(x64_or_ri(b, w, r, imm), "or %s, %d", rw(w, r), imm);
          CASEF(x64_xor_ri(b, w, r, imm), "xor %s, %d", rw(w, r), imm);
          CASEF(x64_cmp_ri(b, w, r, imm), "cmp %s, %d", rw(w, r), imm);
          CASEF(x64_test_ri(b, w, r, imm), "test %s, %d", rw(w, r), imm);
        }
  }
  CASE("add rax, 100000", x64_add_ri(b, 1, X_RAX, 100000));
  CASE("cmp r13, -100000", x64_cmp_ri(b, 1, X_R13, -100000));

  CASE("neg eax", x64_neg(b, 0, X_RAX));
  CASE("neg r13", x64_neg(b, 1, X_R13));
  CASE("not rcx", x64_not(b, 1, X_RCX));
  CASE("not r8d", x64_not(b, 0, X_R8));

  /* ---- shifts / bits ---- */
  CASE("shl eax, 1", x64_shl_ri(b, 0, X_RAX, 1));
  CASE("shl r12, 5", x64_shl_ri(b, 1, X_R12, 5));
  CASE("shr rcx, 63", x64_shr_ri(b, 1, X_RCX, 63));
  CASE("shr ebx, 1", x64_shr_ri(b, 0, X_RBX, 1));
  CASE("sar r9d, 31", x64_sar_ri(b, 0, X_R9, 31));
  CASE("sar rdx, 1", x64_sar_ri(b, 1, X_RDX, 1));
  CASE("rol eax, 3", x64_rol_ri(b, 0, X_RAX, 3));
  CASE("ror r15, 17", x64_ror_ri(b, 1, X_R15, 17));

  CASE("shlx rax, rcx, rdx", x64_shlx(b, 1, X_RAX, X_RCX, X_RDX));
  CASE("shlx r8d, r9d, r10d", x64_shlx(b, 0, X_R8, X_R9, X_R10));
  CASE("shrx rbx, r12, rsi", x64_shrx(b, 1, X_RBX, X_R12, X_RSI));
  CASE("sarx edi, r13d, ecx", x64_sarx(b, 0, X_RDI, X_R13, X_RCX));
  CASE("rorx rax, r9, 13", x64_rorx(b, 1, X_RAX, X_R9, 13));
  CASE("rorx r14d, ebx, 7", x64_rorx(b, 0, X_R14, X_RBX, 7));

  CASE("lzcnt eax, ecx", x64_lzcnt(b, 0, X_RAX, X_RCX));
  CASE("lzcnt r12, r13", x64_lzcnt(b, 1, X_R12, X_R13));
  CASE("tzcnt rbx, r8", x64_tzcnt(b, 1, X_RBX, X_R8));
  CASE("tzcnt r9d, edx", x64_tzcnt(b, 0, X_R9, X_RDX));
  CASE("popcnt rsi, rdi", x64_popcnt(b, 1, X_RSI, X_RDI));
  CASE("popcnt r10d, r11d", x64_popcnt(b, 0, X_R10, X_R11));
  CASE("bswap eax", x64_bswap(b, 0, X_RAX));
  CASE("bswap r13", x64_bswap(b, 1, X_R13));
  CASE("bswap rcx", x64_bswap(b, 1, X_RCX));

  /* ---- mul / div ---- */
  CASE("imul rax, r12", x64_imul_rr(b, 1, X_RAX, X_R12));
  CASE("imul r9d, esi", x64_imul_rr(b, 0, X_R9, X_RSI));
  CASE("imul rax, rcx, 100", x64_imul_rri(b, 1, X_RAX, X_RCX, 100));
  CASE("imul r8, r9, 1000", x64_imul_rri(b, 1, X_R8, X_R9, 1000));
  CASE("imul edx, ebx, -7", x64_imul_rri(b, 0, X_RDX, X_RBX, -7));
  CASE("imul r12", x64_imul_r(b, 1, X_R12));
  CASE("imul ecx", x64_imul_r(b, 0, X_RCX));
  CASE("mul rbx", x64_mul_r(b, 1, X_RBX));
  CASE("mul r15d", x64_mul_r(b, 0, X_R15));
  CASE("div rsi", x64_div_r(b, 1, X_RSI));
  CASE("div r8d", x64_div_r(b, 0, X_R8));
  CASE("idiv rdi", x64_idiv_r(b, 1, X_RDI));
  CASE("idiv r13d", x64_idiv_r(b, 0, X_R13));
  CASE("cdq", x64_cdq_cqo(b, 0));
  CASE("cqo", x64_cdq_cqo(b, 1));
  CASE("mulx rax, rcx, r9", x64_mulx(b, 1, X_RAX, X_RCX, X_R9));
  CASE("mulx r10d, r11d, ebx", x64_mulx(b, 0, X_R10, X_R11, X_RBX));

  /* ---- setcc / cmovcc, all 16 ---- */
  for (int cc = 0; cc < 16; cc++) {
    CASEF(x64_setcc(b, cc, X_RAX), "set%s al", ccs[cc]);
    CASEF(x64_setcc(b, cc, X_RSI), "set%s sil", ccs[cc]);
    CASEF(x64_setcc(b, cc, X_R15), "set%s r15b", ccs[cc]);
    CASEF(x64_cmovcc(b, 1, cc, X_RAX, X_R12), "cmov%s rax, r12", ccs[cc]);
    CASEF(x64_cmovcc(b, 0, cc, X_R9, X_RBX), "cmov%s r9d, ebx", ccs[cc]);
  }

  /* ---- loads / stores, one shape each (the sweep does the rest) ---- */
  CASE("movzx eax, byte ptr [rbx + 4]", x64_load8zx(b, 0, X_RAX, X_RBX, X64_NOIDX, 0, 4));
  CASE("movzx r10, byte ptr [r13]", x64_load8zx(b, 1, X_R10, X_R13, X64_NOIDX, 0, 0));
  CASE("movzx ecx, word ptr [rsi + rdi*2]", x64_load16zx(b, 0, X_RCX, X_RSI, X_RDI, 1, 0));
  CASE("movzx r9, word ptr [rsp + 8]", x64_load16zx(b, 1, X_R9, X_RSP, X64_NOIDX, 0, 8));
  CASE("movsx eax, byte ptr [rbp - 1]", x64_load8sx(b, 0, X_RAX, X_RBP, X64_NOIDX, 0, -1));
  CASE("movsx rdx, word ptr [r12 + 300]", x64_load16sx(b, 1, X_RDX, X_R12, X64_NOIDX, 0, 300));
  CASE("mov eax, dword ptr [rcx + r8*4 + 16]", x64_load32(b, X_RAX, X_RCX, X_R8, 2, 16));
  CASE("movsxd r11, dword ptr [rbx - 8]", x64_load32sx(b, X_R11, X_RBX, X64_NOIDX, 0, -8));
  CASE("mov r14, qword ptr [rdi + rsi*8]", x64_load64(b, X_R14, X_RDI, X_RSI, 3, 0));

  CASE("mov byte ptr [rax], cl", x64_store8(b, X_RCX, X_RAX, X64_NOIDX, 0, 0));
  CASE("mov byte ptr [rbx + 1], dil", x64_store8(b, X_RDI, X_RBX, X64_NOIDX, 0, 1));
  CASE("mov byte ptr [r8], r15b", x64_store8(b, X_R15, X_R8, X64_NOIDX, 0, 0));
  CASE("mov word ptr [rcx + 2], dx", x64_store16(b, X_RDX, X_RCX, X64_NOIDX, 0, 2));
  CASE("mov word ptr [r12], r13w", x64_store16(b, X_R13, X_R12, X64_NOIDX, 0, 0));
  CASE("mov dword ptr [rsi + rdi*4], eax", x64_store32(b, X_RAX, X_RSI, X_RDI, 2, 0));
  CASE("mov dword ptr [rbp + 8], r9d", x64_store32(b, X_R9, X_RBP, X64_NOIDX, 0, 8));
  CASE("mov qword ptr [rsp + 16], rbx", x64_store64(b, X_RBX, X_RSP, X64_NOIDX, 0, 16));
  CASE("mov qword ptr [r13 - 4], r14", x64_store64(b, X_R14, X_R13, X64_NOIDX, 0, -4));

  /* RIP-relative, the constant-pool shape: emit with disp 0, patch to a
   * forward target so the text and the patched bytes agree. */
  {
    size_t at = ours.len;
    CASE("lea rax, [rip + 32]",
         x64_lea(b, 1, X_RAX, X64_RIPREL, X64_NOIDX, 0, 0);
         x64_patch_rel32(b, ours.len - 4, at + 7 + 32));
    at = ours.len;
    CASE("mov rcx, qword ptr [rip + 64]",
         x64_load64(b, X_RCX, X64_RIPREL, X64_NOIDX, 0, 0);
         x64_patch_rel32(b, ours.len - 4, at + 7 + 64));
    at = ours.len;
    CASE("vmovsd xmm3, qword ptr [rip + 16]",
         x64_vmovs_load(b, 1, X_XMM3, X64_RIPREL, X64_NOIDX, 0, 0);
         x64_patch_rel32(b, ours.len - 4, at + 8 + 16));
  }

  /* ---- flow / stack ---- */
  CASE("push rbp", x64_push(b, X_RBP));
  CASE("push r15", x64_push(b, X_R15));
  CASE("pop rbx", x64_pop(b, X_RBX));
  CASE("pop r12", x64_pop(b, X_R12));
  CASE("call 1f; 1:", x64_call_rel32(b));
  CASE("call rax", x64_call_r(b, X_RAX));
  CASE("call r11", x64_call_r(b, X_R11));
  /* the label sits past rel8 range so llvm-mc cannot relax to the short
   * form; patching to it is the branch-fixup path isel uses */
  CASE("jmp 1f; .space 130; 1:",
       x64_jmp_rel32(b);
       x64_patch_rel32(b, ours.len - 4, ours.len + 130);
       buf_zero(b, 130));
  CASE("jmp r10", x64_jmp_r(b, X_R10));
  for (int cc = 0; cc < 16; cc++)
    CASEF(x64_jcc_rel32(b, cc);
          x64_patch_rel32(b, ours.len - 4, ours.len + 130);
          buf_zero(b, 130),
          "j%s 1f; .space 130; 1:", ccs[cc]);
  CASE("ret", x64_ret(b));
  CASE("ud2", x64_ud2(b));
  CASE("int3", x64_int3(b));
  CASE("nop", x64_nop(b));

  /* alignment pads have no single mnemonic; pin the table byte for byte */
  CASE(".byte 0x90", x64_nop_n(b, 1));
  CASE(".byte 0x66, 0x90", x64_nop_n(b, 2));
  CASE(".byte 0x0f, 0x1f, 0x00", x64_nop_n(b, 3));
  CASE(".byte 0x0f, 0x1f, 0x40, 0x00", x64_nop_n(b, 4));
  CASE(".byte 0x0f, 0x1f, 0x44, 0x00, 0x00", x64_nop_n(b, 5));
  CASE(".byte 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00", x64_nop_n(b, 6));
  CASE(".byte 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00", x64_nop_n(b, 7));
  CASE(".byte 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00", x64_nop_n(b, 8));
  CASE(".byte 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00", x64_nop_n(b, 9));
  CASEN(2, ".byte 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, "
        "0x0f, 0x1f, 0x44, 0x00, 0x00", x64_nop_n(b, 14));

  /* ---- scalar FP ---- */
  for (int d = 0; d < 2; d++) {
    const char *sx = d ? "sd" : "ss";
    CASEF(x64_vadds(b, d, X_XMM1, X_XMM2, X_XMM3), "vadd%s xmm1, xmm2, xmm3", sx);
    CASEF(x64_vadds(b, d, X_XMM8, X_XMM9, X_XMM10), "vadd%s xmm8, xmm9, xmm10", sx);
    CASEF(x64_vsubs(b, d, X_XMM4, X_XMM5, X_XMM12), "vsub%s xmm4, xmm5, xmm12", sx);
    CASEF(x64_vmuls(b, d, X_XMM0, X_XMM15, X_XMM7), "vmul%s xmm0, xmm15, xmm7", sx);
    CASEF(x64_vdivs(b, d, X_XMM13, X_XMM6, X_XMM2), "vdiv%s xmm13, xmm6, xmm2", sx);
    CASEF(x64_vmins(b, d, X_XMM3, X_XMM11, X_XMM1), "vmin%s xmm3, xmm11, xmm1", sx);
    CASEF(x64_vmaxs(b, d, X_XMM9, X_XMM0, X_XMM14), "vmax%s xmm9, xmm0, xmm14", sx);
    CASEF(x64_vsqrts(b, d, X_XMM5, X_XMM5, X_XMM8), "vsqrt%s xmm5, xmm5, xmm8", sx);
    CASEF(x64_vucomis(b, d, X_XMM1, X_XMM2), "vucomi%s xmm1, xmm2", sx);
    CASEF(x64_vucomis(b, d, X_XMM10, X_XMM12), "vucomi%s xmm10, xmm12", sx);
    for (int mode = 0; mode < 4; mode++)
      CASEF(x64_vrounds(b, d, X_XMM1, X_XMM2, X_XMM3, mode),
            "vround%s xmm1, xmm2, xmm3, %d", sx, mode);
    CASEF(x64_vmovs_load(b, d, X_XMM6, X_RAX, X64_NOIDX, 0, 8),
          "vmov%s xmm6, %s ptr [rax + 8]", sx, d ? "qword" : "dword");
    CASEF(x64_vmovs_load(b, d, X_XMM12, X_R13, X64_NOIDX, 0, 0),
          "vmov%s xmm12, %s ptr [r13]", sx, d ? "qword" : "dword");
    CASEF(x64_vmovs_store(b, d, X_XMM2, X_RSP, X64_NOIDX, 0, 16),
          "vmov%s %s ptr [rsp + 16], xmm2", sx, d ? "qword" : "dword");
    CASEF(x64_vmovs_store(b, d, X_XMM9, X_RBX, X_RCX, 3, 0),
          "vmov%s %s ptr [rbx + rcx*8], xmm9", sx, d ? "qword" : "dword");
    CASEF(x64_vmovs_rr(b, d, X_XMM1, X_XMM2, X_XMM3), "vmov%s xmm1, xmm2, xmm3", sx);
    CASEF(x64_vmovs_rr(b, d, X_XMM11, X_XMM4, X_XMM13), "vmov%s xmm11, xmm4, xmm13", sx);
    for (int w = 0; w < 2; w++) {
      CASEF(x64_vcvtsi2s(b, d, w, X_XMM1, X_XMM2, X_RCX),
            "vcvtsi2%s xmm1, xmm2, %s", sx, rw(w, X_RCX));
      CASEF(x64_vcvtsi2s(b, d, w, X_XMM10, X_XMM3, X_R9),
            "vcvtsi2%s xmm10, xmm3, %s", sx, rw(w, X_R9));
      CASEF(x64_vcvtts2si(b, d, w, X_RAX, X_XMM5),
            "vcvtt%s2si %s, xmm5", sx, rw(w, X_RAX));
      CASEF(x64_vcvtts2si(b, d, w, X_R14, X_XMM11),
            "vcvtt%s2si %s, xmm11", sx, rw(w, X_R14));
    }
  }
  CASE("vcvtss2sd xmm1, xmm2, xmm3", x64_vcvtss2sd(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vcvtss2sd xmm9, xmm10, xmm11", x64_vcvtss2sd(b, X_XMM9, X_XMM10, X_XMM11));
  CASE("vcvtsd2ss xmm4, xmm5, xmm6", x64_vcvtsd2ss(b, X_XMM4, X_XMM5, X_XMM6));
  CASE("vcvtsd2ss xmm12, xmm13, xmm14", x64_vcvtsd2ss(b, X_XMM12, X_XMM13, X_XMM14));
  CASE("vmovd xmm1, eax", x64_movd_gx(b, 0, X_XMM1, X_RAX));
  CASE("vmovq xmm9, r13", x64_movd_gx(b, 1, X_XMM9, X_R13));
  CASE("vmovd r10d, xmm2", x64_movd_xg(b, 0, X_R10, X_XMM2));
  CASE("vmovq rbx, xmm15", x64_movd_xg(b, 1, X_RBX, X_XMM15));

  /* ---- 128-bit moves / bitwise ---- */
  CASE("vmovdqu xmm1, xmmword ptr [rax]", x64_vmovdqu_load(b, X_XMM1, X_RAX, X64_NOIDX, 0, 0));
  CASE("vmovdqu xmm10, xmmword ptr [r13 + rcx*4 + 32]",
       x64_vmovdqu_load(b, X_XMM10, X_R13, X_RCX, 2, 32));
  CASE("vmovdqu xmmword ptr [rsp + 8], xmm3", x64_vmovdqu_store(b, X_XMM3, X_RSP, X64_NOIDX, 0, 8));
  CASE("vmovdqu xmmword ptr [rbx], xmm12", x64_vmovdqu_store(b, X_XMM12, X_RBX, X64_NOIDX, 0, 0));
  CASE("vmovdqa xmm2, xmm9", x64_vmovdqa_rr(b, X_XMM2, X_XMM9));
  CASE("vmovdqa xmm14, xmm5", x64_vmovdqa_rr(b, X_XMM14, X_XMM5));
  CASE("vandps xmm1, xmm2, xmm3", x64_vandps(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vandps xmm8, xmm14, xmm2", x64_vandps(b, X_XMM8, X_XMM14, X_XMM2));
  CASE("vandnps xmm4, xmm5, xmm6", x64_vandnps(b, X_XMM4, X_XMM5, X_XMM6));
  CASE("vorps xmm7, xmm8, xmm9", x64_vorps(b, X_XMM7, X_XMM8, X_XMM9));
  CASE("vxorps xmm10, xmm11, xmm12", x64_vxorps(b, X_XMM10, X_XMM11, X_XMM12));
  CASE("vxorps xmm0, xmm0, xmm0", x64_vxorps(b, X_XMM0, X_XMM0, X_XMM0));

  /* ---- SIMD lanes / broadcasts ---- */
  {
    static const char *bc[4] = { "vpbroadcastb", "vpbroadcastw",
                                 "vpbroadcastd", "vpbroadcastq" };
    for (int sz = 0; sz < 4; sz++) {
      CASEF(x64_vpbroadcast(b, sz, X_XMM1, X_XMM2), "%s xmm1, xmm2", bc[sz]);
      CASEF(x64_vpbroadcast(b, sz, X_XMM9, X_XMM12), "%s xmm9, xmm12", bc[sz]);
    }
  }
  CASE("vbroadcastss xmm3, xmm4", x64_vbroadcastss(b, X_XMM3, X_XMM4));
  CASE("vbroadcastss xmm13, xmm2", x64_vbroadcastss(b, X_XMM13, X_XMM2));
  CASE("vpshufd xmm1, xmm2, 78", x64_vpshufd(b, X_XMM1, X_XMM2, 0x4e));
  CASE("vpshufd xmm11, xmm14, 14", x64_vpshufd(b, X_XMM11, X_XMM14, 0x0e));
  CASE("vpunpcklqdq xmm1, xmm2, xmm3", x64_vpunpcklqdq(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpunpcklqdq xmm10, xmm12, xmm14", x64_vpunpcklqdq(b, X_XMM10, X_XMM12, X_XMM14));
  {
    for (int i = 0; i < 16; i++)
      CASEF(x64_vpextr(b, 0, X_RAX, X_XMM3, i), "vpextrb eax, xmm3, %d", i);
    CASE("vpextrb r10d, xmm11, 5", x64_vpextr(b, 0, X_R10, X_XMM11, 5));
    for (int i = 0; i < 8; i++)
      CASEF(x64_vpextr(b, 1, X_RDX, X_XMM5, i), "vpextrw edx, xmm5, %d", i);
    CASE("vpextrw r11d, xmm13, 3", x64_vpextr(b, 1, X_R11, X_XMM13, 3));
    for (int i = 0; i < 4; i++)
      CASEF(x64_vpextr(b, 2, X_RCX, X_XMM7, i), "vpextrd ecx, xmm7, %d", i);
    CASE("vpextrd r9d, xmm9, 2", x64_vpextr(b, 2, X_R9, X_XMM9, 2));
    for (int i = 0; i < 2; i++)
      CASEF(x64_vpextr(b, 3, X_R8, X_XMM2, i), "vpextrq r8, xmm2, %d", i);
    CASE("vpextrq rax, xmm15, 1", x64_vpextr(b, 3, X_RAX, X_XMM15, 1));
    for (int i = 0; i < 16; i++)
      CASEF(x64_vpinsr(b, 0, X_XMM1, X_XMM2, X_RAX, i),
            "vpinsrb xmm1, xmm2, eax, %d", i);
    CASE("vpinsrb xmm9, xmm10, r11d, 7", x64_vpinsr(b, 0, X_XMM9, X_XMM10, X_R11, 7));
    for (int i = 0; i < 8; i++)
      CASEF(x64_vpinsr(b, 1, X_XMM3, X_XMM4, X_RDX, i),
            "vpinsrw xmm3, xmm4, edx, %d", i);
    CASE("vpinsrw xmm12, xmm13, r8d, 2", x64_vpinsr(b, 1, X_XMM12, X_XMM13, X_R8, 2));
    for (int i = 0; i < 4; i++)
      CASEF(x64_vpinsr(b, 2, X_XMM5, X_XMM6, X_RCX, i),
            "vpinsrd xmm5, xmm6, ecx, %d", i);
    CASE("vpinsrd xmm14, xmm15, r9d, 3", x64_vpinsr(b, 2, X_XMM14, X_XMM15, X_R9, 3));
    for (int i = 0; i < 2; i++)
      CASEF(x64_vpinsr(b, 3, X_XMM7, X_XMM8, X_RBX, i),
            "vpinsrq xmm7, xmm8, rbx, %d", i);
    CASE("vpinsrq xmm2, xmm3, r14, 1", x64_vpinsr(b, 3, X_XMM2, X_XMM3, X_R14, 1));
  }

  /* ---- SIMD integer (AVX2) ----
   * Each family sweeps every lane width it has, and repeats one width with
   * xmm8+ operands in all three slots so the VEX R/B/vvvv bits are covered.
   */
  {
    static const char *sfx = "bwdq";
    struct { void (*fn)(Buf *, int, int, int, int); const char *name; int lanes; }
    lane3[] = {
      { x64_vpadd, "vpadd", 0xf }, { x64_vpsub, "vpsub", 0xf },
      { x64_vpadds, "vpadds", 0x3 }, { x64_vpaddus, "vpaddus", 0x3 },
      { x64_vpsubs, "vpsubs", 0x3 }, { x64_vpsubus, "vpsubus", 0x3 },
      { x64_vpavg, "vpavg", 0x3 },
      { x64_vpmins, "vpmins", 0x7 }, { x64_vpmaxs, "vpmaxs", 0x7 },
      { x64_vpminu, "vpminu", 0x7 }, { x64_vpmaxu, "vpmaxu", 0x7 },
      { x64_vpcmpeq, "vpcmpeq", 0xf }, { x64_vpcmpgt, "vpcmpgt", 0xf },
      { x64_vpunpckl, "vpunpckl", 0xf }, { x64_vpunpckh, "vpunpckh", 0xf },
    };
    /* the unpacks name both lane widths: vpunpcklbw, ..., vpunpcklqdq */
    static const char *unp[4] = { "bw", "wd", "dq", "qdq" };
    for (size_t i = 0; i < sizeof lane3 / sizeof lane3[0]; i++) {
      bool isunp = strncmp(lane3[i].name, "vpunpck", 7) == 0;
      for (int sz = 0; sz < 4; sz++) {
        if (!(lane3[i].lanes & (1 << sz))) continue;
        char mn[24], one[2] = { sfx[sz], 0 };
        snprintf(mn, sizeof mn, "%s%s", lane3[i].name, isunp ? unp[sz] : one);
        CASEF(lane3[i].fn(b, sz, X_XMM1, X_XMM2, X_XMM3),
              "%s xmm1, xmm2, xmm3", mn);
        CASEF(lane3[i].fn(b, sz, X_XMM9, X_XMM12, X_XMM14),
              "%s xmm9, xmm12, xmm14", mn);
      }
    }
    for (int sz = 0; sz < 3; sz++) {
      CASEF(x64_vpabs(b, sz, X_XMM4, X_XMM5), "vpabs%c xmm4, xmm5", sfx[sz]);
      CASEF(x64_vpabs(b, sz, X_XMM11, X_XMM13), "vpabs%c xmm11, xmm13", sfx[sz]);
    }
  }

  CASE("vpmullw xmm1, xmm2, xmm3", x64_vpmullw(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmullw xmm10, xmm11, xmm12", x64_vpmullw(b, X_XMM10, X_XMM11, X_XMM12));
  CASE("vpmulhw xmm1, xmm2, xmm3", x64_vpmulhw(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmulhuw xmm1, xmm2, xmm3", x64_vpmulhuw(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmulhrsw xmm1, xmm2, xmm3", x64_vpmulhrsw(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmulhrsw xmm9, xmm10, xmm11", x64_vpmulhrsw(b, X_XMM9, X_XMM10, X_XMM11));
  CASE("vpmulld xmm1, xmm2, xmm3", x64_vpmulld(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmulld xmm13, xmm14, xmm15", x64_vpmulld(b, X_XMM13, X_XMM14, X_XMM15));
  CASE("vpmuludq xmm1, xmm2, xmm3", x64_vpmuludq(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmuldq xmm1, xmm2, xmm3", x64_vpmuldq(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmuldq xmm8, xmm9, xmm10", x64_vpmuldq(b, X_XMM8, X_XMM9, X_XMM10));
  CASE("vpmaddwd xmm1, xmm2, xmm3", x64_vpmaddwd(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmaddubsw xmm1, xmm2, xmm3", x64_vpmaddubsw(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpmaddubsw xmm12, xmm13, xmm14",
       x64_vpmaddubsw(b, X_XMM12, X_XMM13, X_XMM14));
  CASE("vpand xmm1, xmm2, xmm3", x64_vpand(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpandn xmm1, xmm2, xmm3", x64_vpandn(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpor xmm1, xmm2, xmm3", x64_vpor(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpxor xmm1, xmm2, xmm3", x64_vpxor(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpxor xmm14, xmm15, xmm8", x64_vpxor(b, X_XMM14, X_XMM15, X_XMM8));
  CASE("vpshufb xmm1, xmm2, xmm3", x64_vpshufb(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpshufb xmm9, xmm10, xmm11", x64_vpshufb(b, X_XMM9, X_XMM10, X_XMM11));
  CASE("vpblendw xmm1, xmm2, xmm3, 85", x64_vpblendw(b, X_XMM1, X_XMM2, X_XMM3, 0x55));
  CASE("vpblendw xmm9, xmm10, xmm11, 170",
       x64_vpblendw(b, X_XMM9, X_XMM10, X_XMM11, 0xaa));
  CASE("vpblendvb xmm1, xmm2, xmm3, xmm4",
       x64_vpblendvb(b, X_XMM1, X_XMM2, X_XMM3, X_XMM4));
  CASE("vpblendvb xmm9, xmm10, xmm11, xmm12",
       x64_vpblendvb(b, X_XMM9, X_XMM10, X_XMM11, X_XMM12));
  CASE("vpacksswb xmm1, xmm2, xmm3", x64_vpacksswb(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpackssdw xmm1, xmm2, xmm3", x64_vpackssdw(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpackuswb xmm1, xmm2, xmm3", x64_vpackuswb(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpackusdw xmm1, xmm2, xmm3", x64_vpackusdw(b, X_XMM1, X_XMM2, X_XMM3));
  CASE("vpackusdw xmm11, xmm12, xmm13",
       x64_vpackusdw(b, X_XMM11, X_XMM12, X_XMM13));
  {
    static const char *ext[3] = { "bw", "wd", "dq" };
    for (int sz = 0; sz < 3; sz++) {
      CASEF(x64_vpmovsx(b, sz, X_XMM1, X_XMM2), "vpmovsx%s xmm1, xmm2", ext[sz]);
      CASEF(x64_vpmovzx(b, sz, X_XMM1, X_XMM2), "vpmovzx%s xmm1, xmm2", ext[sz]);
      CASEF(x64_vpmovsx(b, sz, X_XMM10, X_XMM12), "vpmovsx%s xmm10, xmm12", ext[sz]);
      CASEF(x64_vpmovzx(b, sz, X_XMM10, X_XMM12), "vpmovzx%s xmm10, xmm12", ext[sz]);
    }
  }
  {
    static const char *sfx = "bwdq";
    for (int sz = 1; sz < 4; sz++) {
      for (int k = 0; k < 3; k++) {
        static const int imms[3] = { 1, 7, 31 };
        CASEF(x64_vpsll_i(b, sz, X_XMM1, X_XMM2, imms[k]),
              "vpsll%c xmm1, xmm2, %d", sfx[sz], imms[k]);
        CASEF(x64_vpsrl_i(b, sz, X_XMM3, X_XMM4, imms[k]),
              "vpsrl%c xmm3, xmm4, %d", sfx[sz], imms[k]);
      }
      CASEF(x64_vpsll_i(b, sz, X_XMM9, X_XMM11, 5),
            "vpsll%c xmm9, xmm11, 5", sfx[sz]);
      CASEF(x64_vpsrl_i(b, sz, X_XMM10, X_XMM13, 5),
            "vpsrl%c xmm10, xmm13, 5", sfx[sz]);
      CASEF(x64_vpsll_x(b, sz, X_XMM1, X_XMM2, X_XMM3),
            "vpsll%c xmm1, xmm2, xmm3", sfx[sz]);
      CASEF(x64_vpsrl_x(b, sz, X_XMM9, X_XMM10, X_XMM11),
            "vpsrl%c xmm9, xmm10, xmm11", sfx[sz]);
    }
    for (int sz = 1; sz < 3; sz++) {
      CASEF(x64_vpsra_i(b, sz, X_XMM5, X_XMM6, 3), "vpsra%c xmm5, xmm6, 3", sfx[sz]);
      CASEF(x64_vpsra_i(b, sz, X_XMM12, X_XMM14, 9),
            "vpsra%c xmm12, xmm14, 9", sfx[sz]);
      CASEF(x64_vpsra_x(b, sz, X_XMM1, X_XMM2, X_XMM3),
            "vpsra%c xmm1, xmm2, xmm3", sfx[sz]);
    }
  }
  CASE("vpslldq xmm1, xmm2, 8", x64_vpslldq(b, X_XMM1, X_XMM2, 8));
  CASE("vpsrldq xmm1, xmm2, 8", x64_vpsrldq(b, X_XMM1, X_XMM2, 8));
  CASE("vpsrldq xmm11, xmm13, 4", x64_vpsrldq(b, X_XMM11, X_XMM13, 4));
  CASE("vpmovmskb eax, xmm3", x64_vpmovmskb(b, X_RAX, X_XMM3));
  CASE("vpmovmskb r11d, xmm12", x64_vpmovmskb(b, X_R11, X_XMM12));
  CASE("vmovmskps ecx, xmm5", x64_vmovmskp(b, 0, X_RCX, X_XMM5));
  CASE("vmovmskpd edx, xmm6", x64_vmovmskp(b, 1, X_RDX, X_XMM6));
  CASE("vmovmskps r8d, xmm14", x64_vmovmskp(b, 0, X_R8, X_XMM14));
  CASE("vptest xmm1, xmm2", x64_vptest(b, X_XMM1, X_XMM2));
  CASE("vptest xmm10, xmm13", x64_vptest(b, X_XMM10, X_XMM13));

  /* ---- SIMD float ---- */
  {
    struct { void (*fn)(Buf *, int, int, int, int); const char *name; } fp3[] = {
      { x64_vaddp, "vadd" }, { x64_vsubp, "vsub" }, { x64_vmulp, "vmul" },
      { x64_vdivp, "vdiv" }, { x64_vminp, "vmin" }, { x64_vmaxp, "vmax" },
    };
    for (size_t i = 0; i < sizeof fp3 / sizeof fp3[0]; i++)
      for (int d = 0; d < 2; d++) {
        CASEF(fp3[i].fn(b, d, X_XMM1, X_XMM2, X_XMM3), "%sp%c xmm1, xmm2, xmm3",
              fp3[i].name, d ? 'd' : 's');
        CASEF(fp3[i].fn(b, d, X_XMM9, X_XMM11, X_XMM13),
              "%sp%c xmm9, xmm11, xmm13", fp3[i].name, d ? 'd' : 's');
      }
    for (int d = 0; d < 2; d++) {
      CASEF(x64_vsqrtp(b, d, X_XMM1, X_XMM2), "vsqrtp%c xmm1, xmm2", d ? 'd' : 's');
      CASEF(x64_vsqrtp(b, d, X_XMM10, X_XMM12), "vsqrtp%c xmm10, xmm12",
            d ? 'd' : 's');
      static const int preds[7] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x0d, 0x0e };
      for (int k = 0; k < 7; k++)
        CASEF(x64_vcmpp(b, d, X_XMM1, X_XMM2, X_XMM3, preds[k]),
              "vcmpp%c xmm1, xmm2, xmm3, %d", d ? 'd' : 's', preds[k]);
      CASEF(x64_vcmpp(b, d, X_XMM12, X_XMM13, X_XMM14, 0),
            "vcmpp%c xmm12, xmm13, xmm14, 0", d ? 'd' : 's');
      for (int m = 8; m < 12; m++)
        CASEF(x64_vroundp(b, d, X_XMM1, X_XMM2, m), "vroundp%c xmm1, xmm2, %d",
              d ? 'd' : 's', m);
      CASEF(x64_vroundp(b, d, X_XMM9, X_XMM14, 11), "vroundp%c xmm9, xmm14, 11",
            d ? 'd' : 's');
      CASEF(x64_vfmadd231p(b, d, X_XMM1, X_XMM2, X_XMM3),
            "vfmadd231p%c xmm1, xmm2, xmm3", d ? 'd' : 's');
      CASEF(x64_vfmadd231p(b, d, X_XMM10, X_XMM11, X_XMM12),
            "vfmadd231p%c xmm10, xmm11, xmm12", d ? 'd' : 's');
      CASEF(x64_vfnmadd231p(b, d, X_XMM1, X_XMM2, X_XMM3),
            "vfnmadd231p%c xmm1, xmm2, xmm3", d ? 'd' : 's');
      CASEF(x64_vfnmadd231p(b, d, X_XMM13, X_XMM14, X_XMM15),
            "vfnmadd231p%c xmm13, xmm14, xmm15", d ? 'd' : 's');
    }
  }
  CASE("vshufps xmm1, xmm2, xmm3, 136", x64_vshufps(b, X_XMM1, X_XMM2, X_XMM3, 0x88));
  CASE("vshufps xmm9, xmm10, xmm11, 221",
       x64_vshufps(b, X_XMM9, X_XMM10, X_XMM11, 0xdd));
  CASE("vcvtdq2ps xmm1, xmm2", x64_vcvtdq2ps(b, X_XMM1, X_XMM2));
  CASE("vcvtdq2ps xmm11, xmm13", x64_vcvtdq2ps(b, X_XMM11, X_XMM13));
  CASE("vcvttps2dq xmm1, xmm2", x64_vcvttps2dq(b, X_XMM1, X_XMM2));
  CASE("vcvttps2dq xmm11, xmm13", x64_vcvttps2dq(b, X_XMM11, X_XMM13));
  CASE("vcvtps2pd xmm1, xmm2", x64_vcvtps2pd(b, X_XMM1, X_XMM2));
  CASE("vcvtpd2ps xmm1, xmm2", x64_vcvtpd2ps(b, X_XMM1, X_XMM2));
  CASE("vcvtdq2pd xmm1, xmm2", x64_vcvtdq2pd(b, X_XMM1, X_XMM2));
  CASE("vcvttpd2dq xmm1, xmm2", x64_vcvttpd2dq(b, X_XMM1, X_XMM2));
  CASE("vcvtps2pd xmm10, xmm12", x64_vcvtps2pd(b, X_XMM10, X_XMM12));
  CASE("vcvtpd2ps xmm10, xmm12", x64_vcvtpd2ps(b, X_XMM10, X_XMM12));
  CASE("vcvtdq2pd xmm10, xmm12", x64_vcvtdq2pd(b, X_XMM10, X_XMM12));
  CASE("vcvttpd2dq xmm10, xmm12", x64_vcvttpd2dq(b, X_XMM10, X_XMM12));

  /* --profile's counter bump: a read-modify-write add whose immediate is
   * patched once the block it counts has been emitted.  Every immediate
   * here is past imm8 range because llvm-mc would otherwise answer with
   * the shorter 0x83 form, which this emitter deliberately never picks. */
  CASE("add qword ptr [rsi + 56], 4660",
       x64_add_mi32(b, 1, X_RSI, X64_NOIDX, 0, 56, 4660));
  CASE("add dword ptr [r12 + rax*8 + 128], -1000",
       x64_add_mi32(b, 0, X_R12, X_RAX, 3, 128, -1000));
  CASE("add qword ptr [r13 + 100000], 2147483647",
       x64_add_mi32(b, 1, X_R13, X64_NOIDX, 0, 100000, 2147483647));

  /* the generated addressing sweep last, so hand-case mismatches surface
   * with small offsets */
  addressing_sweep(b);

  fclose(asmf);
  fclose(offf);
  FILE *bin = fopen(argv[1], "wb");
  if (!bin) { perror("open"); return 2; }
  fwrite(ours.data, 1, ours.len, bin);
  fclose(bin);
  fprintf(stderr, "emitted %d cases, %zu bytes, %u instructions\n",
          ncases, ours.len, ours.ninstr);
  if (nmiscount) {
    fprintf(stderr, "%d case(s) miscounted instructions\n", nmiscount);
    return 3;
  }
  return 0;
}
