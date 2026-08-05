#if defined(WARMC_DARWIN)
#define SYM(x) _##x
#define FUNC_DIRECTIVES(x)
#define SIZE_DIRECTIVE(x)
#else
#define SYM(x) x
#define FUNC_DIRECTIVES(x) .section .text.x,"ax",%progbits ; .type x, %function
#define SIZE_DIRECTIVE(x) .size x, . - x
#endif

    .macro LOADADDR reg, sym
#if defined(WARMC_DARWIN)
    adrp    \reg, \sym@PAGE
    add     \reg, \reg, \sym@PAGEOFF
#else
    adrp    \reg, \sym
    add     \reg, \reg, :lo12:\sym
#endif
    .endm

    .text

#if !defined(WARMC_DARWIN)
    .globl _start
    FUNC_DIRECTIVES(_start)
_start:
    mov     x0, sp
    mov     x9, sp
    and     x9, x9, #-16
    mov     sp, x9
    mov     x29, #0
    mov     x30, #0
    bl      warmc_start_c

    brk     #0
    .size _start, . - _start
#endif

    .globl SYM(warmc_invoke)
    FUNC_DIRECTIVES(SYM(warmc_invoke))
SYM(warmc_invoke):
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    stp     x19, x20, [sp, #-16]!

    sub     sp, sp, #512

    mov     x19, x4
    mov     x16, x0
    mov     x17, x1

    ldp     q0, q1, [x3, #0]
    ldp     q2, q3, [x3, #32]
    ldp     q4, q5, [x3, #64]
    ldp     q6, q7, [x3, #96]

    ldp     x9,  x10, [x2, #0]
    ldp     x11, x12, [x2, #16]
    ldp     x13, x14, [x2, #32]
    ldr     x15,      [x2, #48]

    mov     x0, x17
    mov     x1, x9
    mov     x2, x10
    mov     x3, x11
    mov     x4, x12
    mov     x5, x13
    mov     x6, x14
    mov     x7, x15
    add     x8, x19, #32

    LOADADDR x20, SYM(warmc_gc_invoke_depth)
    ldr     x9, [x20]
    add     x9, x9, #1
    str     x9, [x20]

    blr     x16

    ldr     x9, [x20]
    sub     x9, x9, #1
    str     x9, [x20]

    str     x0, [x19, #0]
    str     q0, [x19, #16]

    add     sp, sp, #512
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret
    SIZE_DIRECTIVE(SYM(warmc_invoke))

    .globl SYM(warmc_setjmp)
    FUNC_DIRECTIVES(SYM(warmc_setjmp))
SYM(warmc_setjmp):
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x9, sp
    str     x9, [x0, #96]
    stp     d8,  d9,  [x0, #104]
    stp     d10, d11, [x0, #120]
    stp     d12, d13, [x0, #136]
    stp     d14, d15, [x0, #152]
    mov     x0, #0
    ret
    SIZE_DIRECTIVE(SYM(warmc_setjmp))

    .globl SYM(warmc_longjmp)
    FUNC_DIRECTIVES(SYM(warmc_longjmp))
SYM(warmc_longjmp):
    ldp     x19, x20, [x0, #0]
    ldp     x21, x22, [x0, #16]
    ldp     x23, x24, [x0, #32]
    ldp     x25, x26, [x0, #48]
    ldp     x27, x28, [x0, #64]
    ldp     x29, x30, [x0, #80]
    ldr     x9, [x0, #96]
    mov     sp, x9
    ldp     d8,  d9,  [x0, #104]
    ldp     d10, d11, [x0, #120]
    ldp     d12, d13, [x0, #136]
    ldp     d14, d15, [x0, #152]
    mov     x0, x1
    cmp     x0, #0
    csinc   x0, x0, xzr, ne
    ret
    SIZE_DIRECTIVE(SYM(warmc_longjmp))

#define SNAP_BYTES 160

    .macro WARM_SNAPSHOT
    sub     sp, sp, #SNAP_BYTES
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     d8,  d9,  [sp, #80]
    stp     d10, d11, [sp, #96]
    stp     d12, d13, [sp, #112]
    stp     d14, d15, [sp, #128]
    .endm

    .globl SYM(warmc_throw)
    FUNC_DIRECTIVES(SYM(warmc_throw))
SYM(warmc_throw):
    mov     x9, x30
    mov     x10, sp
    WARM_SNAPSHOT
    mov     x4, sp
    mov     x5, x10
    mov     x6, x9
    bl      SYM(warmc_throw_c)
    brk     #0
    SIZE_DIRECTIVE(SYM(warmc_throw))

    .globl SYM(warmc_throw_ref)
    FUNC_DIRECTIVES(SYM(warmc_throw_ref))
SYM(warmc_throw_ref):
    mov     x9, x30
    mov     x10, sp
    WARM_SNAPSHOT
    mov     x1, sp
    mov     x2, x10
    mov     x3, x9
    bl      SYM(warmc_throw_ref_c)
    brk     #0
    SIZE_DIRECTIVE(SYM(warmc_throw_ref))

    .macro GC_ENTER
    LOADADDR x9, SYM(warmc_gc_ctx)
    stp     x19, x20, [x9, #0]
    stp     x21, x22, [x9, #16]
    stp     x23, x24, [x9, #32]
    stp     x25, x26, [x9, #48]
    stp     x27, x28, [x9, #64]
    mov     x10, sp
    stp     x10, x30, [x9, #80]
    .endm

    .macro GC_LEAVE
    LOADADDR x9, SYM(warmc_gc_ctx)
    ldp     x19, x20, [x9, #0]
    ldp     x21, x22, [x9, #16]
    ldp     x23, x24, [x9, #32]
    ldp     x25, x26, [x9, #48]
    ldp     x27, x28, [x9, #64]
    .endm

    .macro GC_TRAMPOLINE name
    .globl SYM(\name)
    FUNC_DIRECTIVES(SYM(\name))
SYM(\name):

    GC_ENTER
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    bl      SYM(\name\()_w)
    GC_LEAVE
    ldp     x29, x30, [sp], #16
    ret
    SIZE_DIRECTIVE(SYM(\name))
    .endm

    GC_TRAMPOLINE warmc_struct_new
    GC_TRAMPOLINE warmc_array_new
    GC_TRAMPOLINE warmc_array_new_fill
    GC_TRAMPOLINE warmc_array_new_data
    GC_TRAMPOLINE warmc_array_new_elem

    .globl SYM(warmc_gc_load_stub)
    FUNC_DIRECTIVES(SYM(warmc_gc_load_stub))
SYM(warmc_gc_load_stub):
    sub     sp, sp, #672
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    str     x30, [sp, #144]
    stp     q0, q1, [sp, #160]
    stp     q2, q3, [sp, #192]
    stp     q4, q5, [sp, #224]
    stp     q6, q7, [sp, #256]
    stp     q8, q9, [sp, #288]
    stp     q10, q11, [sp, #320]
    stp     q12, q13, [sp, #352]
    stp     q14, q15, [sp, #384]
    stp     q16, q17, [sp, #416]
    stp     q18, q19, [sp, #448]
    stp     q20, q21, [sp, #480]
    stp     q22, q23, [sp, #512]
    stp     q24, q25, [sp, #544]
    stp     q26, q27, [sp, #576]
    stp     q28, q29, [sp, #608]
    stp     q30, q31, [sp, #640]
    mov     x0, x14
    mov     x1, x15
    bl      SYM(warmc_gc_load_barrier)

    str     x0, [sp, #112]
    ldp     q0, q1, [sp, #160]
    ldp     q2, q3, [sp, #192]
    ldp     q4, q5, [sp, #224]
    ldp     q6, q7, [sp, #256]
    ldp     q8, q9, [sp, #288]
    ldp     q10, q11, [sp, #320]
    ldp     q12, q13, [sp, #352]
    ldp     q14, q15, [sp, #384]
    ldp     q16, q17, [sp, #416]
    ldp     q18, q19, [sp, #448]
    ldp     q20, q21, [sp, #480]
    ldp     q22, q23, [sp, #512]
    ldp     q24, q25, [sp, #544]
    ldp     q26, q27, [sp, #576]
    ldp     q28, q29, [sp, #608]
    ldp     q30, q31, [sp, #640]
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldr     x30, [sp, #144]
    add     sp, sp, #672
    ret
    SIZE_DIRECTIVE(SYM(warmc_gc_load_stub))

    .globl SYM(warmc_resume)
    FUNC_DIRECTIVES(SYM(warmc_resume))
SYM(warmc_resume):
    mov     x9,  x0
    mov     x10, x1
    mov     x11, x2
    add     x12, x0, x3
    ldp     d8,  d9,  [x5, #0]
    ldp     d10, d11, [x5, #16]
    ldp     d12, d13, [x5, #32]
    ldp     d14, d15, [x5, #48]
    ldp     x19, x20, [x4, #0]
    ldp     x21, x22, [x4, #16]
    ldp     x23, x24, [x4, #32]
    ldp     x25, x26, [x4, #48]
    ldp     x27, x28, [x4, #64]
    mov     sp, x9
    str     x11, [x12]
    br      x10
    SIZE_DIRECTIVE(SYM(warmc_resume))

#if !defined(WARMC_DARWIN)

    .globl warmc_gcth_clone
    FUNC_DIRECTIVES(warmc_gcth_clone)
warmc_gcth_clone:
    mov     x9, x2

    mov     x2, #0
    mov     x3, #0
    mov     x4, #0
    mov     x8, #220
    svc     #0
    cbz     x0, 1f
    ret
1:  mov     x29, #0
    mov     x30, #0
    blr     x9

    mov     x0, #0
    mov     x8, #93
    svc     #0
    brk     #0
    .size warmc_gcth_clone, . - warmc_gcth_clone

    .section .note.GNU-stack,"",%progbits
#else
    .subsections_via_symbols
#endif
