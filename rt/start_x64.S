/* x64 Windows assembly support.  The PE entry point is warmc_win_entry (plain
   C in rt.c via win.inc), so this file carries only what cannot be C:

   - __chkstk stubs: generated and runtime code never probes, because the PE
     header commits the whole 8 MiB stack up front (reserve == commit).
   - warmc_setjmp/warmc_longjmp over the Win64 callee-saved set.
   - the throw/GC entries, which capture or restore the exact register state
     of compiled wasm code.  The WarmRegs/GcCtx gpr index map is fixed by
     isel_cs_index: 0..7 = RSI,RDI,RBX,RBP,R12,R13,R14,R15 (gpr[0] is vmctx,
     which the unwinder reads); fpr 0..7 = the low halves of XMM8..15.
     Every entry here is reached by a `call` from 16-aligned wasm code, so
     rsp == 8 (mod 16) on entry, the wasm frame's sp is rsp+8, and the
     site pc the unwind tables are keyed by is the pushed return address. */

    .text

    .globl ___chkstk_ms
___chkstk_ms:
    ret

    .globl __chkstk
__chkstk:
    ret

/* jmpbuf layout (fits g_catch_buf's u64[32]):
   0 rbx, 8 rbp, 16 rdi, 24 rsi, 32 r12, 40 r13, 48 r14, 56 r15,
   64 rsp-after-return, 72 return address, 80+16k xmm(6+k) for k = 0..9 */

    .globl warmc_setjmp
warmc_setjmp:
    movq   %rbx,   0(%rcx)
    movq   %rbp,   8(%rcx)
    movq   %rdi,  16(%rcx)
    movq   %rsi,  24(%rcx)
    movq   %r12,  32(%rcx)
    movq   %r13,  40(%rcx)
    movq   %r14,  48(%rcx)
    movq   %r15,  56(%rcx)
    leaq   8(%rsp), %rax
    movq   %rax,  64(%rcx)
    movq   (%rsp), %rax
    movq   %rax,  72(%rcx)
    movups %xmm6,   80(%rcx)
    movups %xmm7,   96(%rcx)
    movups %xmm8,  112(%rcx)
    movups %xmm9,  128(%rcx)
    movups %xmm10, 144(%rcx)
    movups %xmm11, 160(%rcx)
    movups %xmm12, 176(%rcx)
    movups %xmm13, 192(%rcx)
    movups %xmm14, 208(%rcx)
    movups %xmm15, 224(%rcx)
    xorl   %eax, %eax
    ret

    .globl warmc_longjmp
warmc_longjmp:
    movq   0(%rcx),  %rbx
    movq   8(%rcx),  %rbp
    movq   16(%rcx), %rdi
    movq   24(%rcx), %rsi
    movq   32(%rcx), %r12
    movq   40(%rcx), %r13
    movq   48(%rcx), %r14
    movq   56(%rcx), %r15
    movups 80(%rcx),  %xmm6
    movups 96(%rcx),  %xmm7
    movups 112(%rcx), %xmm8
    movups 128(%rcx), %xmm9
    movups 144(%rcx), %xmm10
    movups 160(%rcx), %xmm11
    movups 176(%rcx), %xmm12
    movups 192(%rcx), %xmm13
    movups 208(%rcx), %xmm14
    movups 224(%rcx), %xmm15
    movl   %edx, %eax
    testl  %eax, %eax
    jnz    1f
    movl   $1, %eax
1:  movq   64(%rcx), %rsp
    jmpq   *72(%rcx)

/* void warmc_invoke(void *fn(rcx), WarmInstance *v(rdx), const u64 *iargs(r8),
                      const u64 *fargs(r9), u64 *retbuf(stack)).
   Bridges MS ABI into the internal convention: vmctx moves to RCX's slot
   as arg0, up to 3 int args ride RDX/R8/R9 and 4 fp args XMM0-3 (the same
   per-class ceilings the backend marshals with), the retarea pointer is
   retbuf+32 in R10.  Callees preserve the CS GPRs they use, but only the
   low halves of XMM8-15 and nothing of XMM6/7, so the full XMM6-15 set is
   saved here -- this entry is what makes the internal convention's
   "XMM4-7 volatile" legal.  The 512-byte cushion keeps a callee's reads
   of never-written stack-arg slots memory-safe. */
    .globl warmc_invoke
warmc_invoke:
    pushq  %rbx
    subq   $672, %rsp
    movups %xmm6,  512(%rsp)
    movups %xmm7,  528(%rsp)
    movups %xmm8,  544(%rsp)
    movups %xmm9,  560(%rsp)
    movups %xmm10, 576(%rsp)
    movups %xmm11, 592(%rsp)
    movups %xmm12, 608(%rsp)
    movups %xmm13, 624(%rsp)
    movups %xmm14, 640(%rsp)
    movups %xmm15, 656(%rsp)
    movq   %rcx, %r11
    movq   %rdx, %rcx
    movq   720(%rsp), %rbx
    movups   (%r9), %xmm0
    movups 16(%r9), %xmm1
    movups 32(%r9), %xmm2
    movups 48(%r9), %xmm3
    movq     (%r8), %rdx
    movq   16(%r8), %r9
    movq    8(%r8), %r8
    leaq   32(%rbx), %r10
    incq   warmc_gc_invoke_depth(%rip)
    callq  *%r11
    decq   warmc_gc_invoke_depth(%rip)
    movq   %rax,  (%rbx)
    movups %xmm0, 16(%rbx)
    movups 512(%rsp), %xmm6
    movups 528(%rsp), %xmm7
    movups 544(%rsp), %xmm8
    movups 560(%rsp), %xmm9
    movups 576(%rsp), %xmm10
    movups 592(%rsp), %xmm11
    movups 608(%rsp), %xmm12
    movups 624(%rsp), %xmm13
    movups 640(%rsp), %xmm14
    movups 656(%rsp), %xmm15
    addq   $672, %rsp
    popq   %rbx
    ret

/* WARM_SNAPSHOT at offset \at: a WarmRegs (u64 gpr[10]; u64 fpr[8], 144
   bytes) filled with the throw site's callee-saved state.  gpr[8..9] have no
   x64 register behind them; they are zeroed so the WarmRegs the unwinder
   copies around is deterministic. */
    .macro WARM_SNAPSHOT at
    movq   %rsi, \at+0(%rsp)
    movq   %rdi, \at+8(%rsp)
    movq   %rbx, \at+16(%rsp)
    movq   %rbp, \at+24(%rsp)
    movq   %r12, \at+32(%rsp)
    movq   %r13, \at+40(%rsp)
    movq   %r14, \at+48(%rsp)
    movq   %r15, \at+56(%rsp)
    movq   $0,   \at+64(%rsp)
    movq   $0,   \at+72(%rsp)
    movq   %xmm8,  \at+80(%rsp)
    movq   %xmm9,  \at+88(%rsp)
    movq   %xmm10, \at+96(%rsp)
    movq   %xmm11, \at+104(%rsp)
    movq   %xmm12, \at+112(%rsp)
    movq   %xmm13, \at+120(%rsp)
    movq   %xmm14, \at+128(%rsp)
    movq   %xmm15, \at+136(%rsp)
    .endm

/* warmc_throw(vm(rcx), tag(rdx), payload(r8), nvals(r9)): append the
   snapshot, the throw site's sp and its pc, and tail into the C unwinder
   warmc_throw_c(vm, tag, payload, nvals, snap, sp, pc) -- args 5..7 go on
   the stack per MS ABI.  Frame: 32 shadow + 24 stack args + WarmRegs 144
   = 200, which also restores 16-alignment for the call. */
/* warmc_invoke_s: warmc_invoke plus stack arguments.  The internal
   convention carries only 3 ints and 4 floats in registers; a callee with
   more reads the rest from its incoming stack slots at +32 above the call.
   The caller builds an image of exactly those slots -- eight bytes per
   spilled scalar in parameter order, sixteen for a v128 -- and it is copied
   into the cushion here, eight bytes at a time because the image's length is
   only a multiple of eight.
   Args: fn(rcx), vm(rdx), iargs(r8), fargs(r9), then on the stack:
   retbuf, stk, stk_bytes.  stk_bytes caps at 480 (the cushion). */
    .globl warmc_invoke_s
warmc_invoke_s:
    pushq  %rbx
    subq   $672, %rsp
    movups %xmm6,  512(%rsp)
    movups %xmm7,  528(%rsp)
    movups %xmm8,  544(%rsp)
    movups %xmm9,  560(%rsp)
    movups %xmm10, 576(%rsp)
    movups %xmm11, 592(%rsp)
    movups %xmm12, 608(%rsp)
    movups %xmm13, 624(%rsp)
    movups %xmm14, 640(%rsp)
    movups %xmm15, 656(%rsp)
    movq   728(%rsp), %rax
    movq   736(%rsp), %r10
    leaq   32(%rsp), %r11
    testq  %r10, %r10
    jz     2f
1:  movq   (%rax), %rbx
    movq   %rbx, (%r11)
    addq   $8, %rax
    addq   $8, %r11
    subq   $8, %r10
    jnz    1b
2:  movq   %rcx, %r11
    movq   %rdx, %rcx
    movq   720(%rsp), %rbx
    movups   (%r9), %xmm0
    movups 16(%r9), %xmm1
    movups 32(%r9), %xmm2
    movups 48(%r9), %xmm3
    movq     (%r8), %rdx
    movq   16(%r8), %r9
    movq    8(%r8), %r8
    leaq   32(%rbx), %r10
    incq   warmc_gc_invoke_depth(%rip)
    callq  *%r11
    decq   warmc_gc_invoke_depth(%rip)
    movq   %rax,  (%rbx)
    movups %xmm0, 16(%rbx)
    movups 512(%rsp), %xmm6
    movups 528(%rsp), %xmm7
    movups 544(%rsp), %xmm8
    movups 560(%rsp), %xmm9
    movups 576(%rsp), %xmm10
    movups 592(%rsp), %xmm11
    movups 608(%rsp), %xmm12
    movups 624(%rsp), %xmm13
    movups 640(%rsp), %xmm14
    movups 656(%rsp), %xmm15
    addq   $672, %rsp
    popq   %rbx
    ret

    .globl warmc_throw
warmc_throw:
    subq   $200, %rsp
    WARM_SNAPSHOT 56
    leaq   56(%rsp), %rax
    movq   %rax, 32(%rsp)
    leaq   208(%rsp), %rax
    movq   %rax, 40(%rsp)
    movq   200(%rsp), %rax
    movq   %rax, 48(%rsp)
    callq  warmc_throw_c
    int3

/* warmc_throw_ref(exn(rcx)) -> warmc_throw_ref_c(exn, snap, sp, pc): all
   four args ride registers, so the frame is just shadow + WarmRegs + pad. */
    .globl warmc_throw_ref
warmc_throw_ref:
    subq   $184, %rsp
    WARM_SNAPSHOT 32
    leaq   32(%rsp), %rdx
    leaq   192(%rsp), %r8
    movq   184(%rsp), %r9
    callq  warmc_throw_ref_c
    int3

/* GcCtx is { u64 gpr[10]; u64 sp; u64 pc }.  The allocation entries park
   the caller's callee-saved state there so a collection triggered inside
   the worker can walk and *rewrite* the wasm frames' roots, then reload
   the (possibly relocated) values on the way out.  MS-ABI args pass
   through untouched; the workers take up to five, so one caller stack-arg
   slot is forwarded (a second for headroom -- copying an unused slot of
   the caller's out-area is harmless). */
    .macro GC_TRAMPOLINE name
    .globl \name
\name:
    leaq   warmc_gc_ctx(%rip), %rax
    movq   %rsi,  0(%rax)
    movq   %rdi,  8(%rax)
    movq   %rbx, 16(%rax)
    movq   %rbp, 24(%rax)
    movq   %r12, 32(%rax)
    movq   %r13, 40(%rax)
    movq   %r14, 48(%rax)
    movq   %r15, 56(%rax)
    leaq   8(%rsp), %r10
    movq   %r10, 80(%rax)
    movq   (%rsp), %r10
    movq   %r10, 88(%rax)
    subq   $56, %rsp
    movq   96(%rsp), %r10
    movq   %r10, 32(%rsp)
    movq   104(%rsp), %r10
    movq   %r10, 40(%rsp)
    callq  \name\()_w
    addq   $56, %rsp
    leaq   warmc_gc_ctx(%rip), %r10
    movq    0(%r10), %rsi
    movq    8(%r10), %rdi
    movq   16(%r10), %rbx
    movq   24(%r10), %rbp
    movq   32(%r10), %r12
    movq   40(%r10), %r13
    movq   48(%r10), %r14
    movq   56(%r10), %r15
    ret
    .endm

    GC_TRAMPOLINE warmc_struct_new
    GC_TRAMPOLINE warmc_array_new
    GC_TRAMPOLINE warmc_array_new_fill
    GC_TRAMPOLINE warmc_array_new_data
    GC_TRAMPOLINE warmc_array_new_elem

/* Load-barrier stub.  Contract with isel_x64: slot address in R10, value in
   R11, result back in R11; everything else the generated code could hold
   live is preserved (flags excepted -- the isel's test/jcc consumed them).
   MS-callee-saved GPRs and XMM6-15 survive the C call on their own, so only
   the volatile half is spilled here. */
    .globl warmc_gc_load_stub
warmc_gc_load_stub:
    subq   $184, %rsp
    movq   %rax, 32(%rsp)
    movq   %rcx, 40(%rsp)
    movq   %rdx, 48(%rsp)
    movq   %r8,  56(%rsp)
    movq   %r9,  64(%rsp)
    movq   %r10, 72(%rsp)
    movups %xmm0,  80(%rsp)
    movups %xmm1,  96(%rsp)
    movups %xmm2, 112(%rsp)
    movups %xmm3, 128(%rsp)
    movups %xmm4, 144(%rsp)
    movups %xmm5, 160(%rsp)
    movq   %r11, %rcx
    movq   %r10, %rdx
    callq  warmc_gc_load_barrier
    movq   %rax, %r11
    movups  80(%rsp), %xmm0
    movups  96(%rsp), %xmm1
    movups 112(%rsp), %xmm2
    movups 128(%rsp), %xmm3
    movups 144(%rsp), %xmm4
    movups 160(%rsp), %xmm5
    movq   32(%rsp), %rax
    movq   40(%rsp), %rcx
    movq   48(%rsp), %rdx
    movq   56(%rsp), %r8
    movq   64(%rsp), %r9
    movq   72(%rsp), %r10
    addq   $184, %rsp
    ret

/* warmc_resume(sp(rcx), pc(rdx), exn(r8), slot(r9), gpr(stack), fpr(stack)):
   land on a catch pad with the walked-frame register state, the target
   frame's sp, and the exception object parked at sp+slot where the pad's
   I_EXN_OBJ load expects it.  Never returns. */
    .globl warmc_resume
warmc_resume:
    movq   0x28(%rsp), %r10
    movq   0x30(%rsp), %r11
    movq    0(%r11), %xmm8
    movq    8(%r11), %xmm9
    movq   16(%r11), %xmm10
    movq   24(%r11), %xmm11
    movq   32(%r11), %xmm12
    movq   40(%r11), %xmm13
    movq   48(%r11), %xmm14
    movq   56(%r11), %xmm15
    movq    0(%r10), %rsi
    movq    8(%r10), %rdi
    movq   16(%r10), %rbx
    movq   24(%r10), %rbp
    movq   32(%r10), %r12
    movq   40(%r10), %r13
    movq   48(%r10), %r14
    movq   56(%r10), %r15
    movq   %rcx, %rsp
    movq   %r8, (%rsp,%r9)
    jmpq   *%rdx
