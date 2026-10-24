/* The face a module compiled with `warmc --shared` shows the program that
   loads it.

   Each exported function becomes a C function of the export's name, which
   warmc emits as two instructions: the export's index into a register, and
   a jump to warmc_lib_thunk at the foot of this file.  The thunk stores the
   C argument registers and calls warmc_lib_call, which reads the arguments
   the way the platform's C convention passed them, calls the export, and
   leaves the result where the thunk returns it from.  Both live here, not
   in start.S, so that only a library pulls them in.

   The library is a guest in someone else's process.  Its one instance is
   made on the first call, with the imports the host bound by then; calls
   from several threads take turns, and a host import may call back in on
   its own thread.  A trap ends the call, not the process: the call returns
   zero and <lib>_last_trap() says which trap, per thread. */

#include "warm_rt.h"

extern const ModuleDesc warmc_module;

#if defined(__x86_64__)
/* Wasm calls an import with the internal convention, which assigns the
   argument registers class by class where Win64 assigns them by position;
   warmc emits an adapter per import (shlib.c) and binds that, with the
   host's function and ctx as the pair it is handed in RCX. */
extern void *const warmc_lib_adapters[];
#endif

static WarmInstance *lib_vm;
static u32 lib_failed;
static WarmBinding *lib_binds;

/* ---- the lock --------------------------------------------------------
   0 free, 1 held, 2 held with a thread waiting.  The holder may take it
   again: a host import that calls back into the library does. */
static u32 lib_word;
static u64 lib_owner;
static u32 lib_depth;

static void lib_lock(void) {
  u64 me = warmc_rt_thread_id();
  if (__atomic_load_n(&lib_owner, __ATOMIC_RELAXED) == me) { lib_depth++; return; }
  u32 c = 0;
  if (!__atomic_compare_exchange_n(&lib_word, &c, 1, 0, __ATOMIC_ACQUIRE,
                                   __ATOMIC_RELAXED)) {
    if (c != 2) c = __atomic_exchange_n(&lib_word, 2, __ATOMIC_ACQUIRE);
    while (c) {
      warmc_rt_wait(&lib_word, 2);
      c = __atomic_exchange_n(&lib_word, 2, __ATOMIC_ACQUIRE);
    }
  }
  __atomic_store_n(&lib_owner, me, __ATOMIC_RELAXED);
  lib_depth = 1;
}

static void lib_unlock(void) {
  if (--lib_depth) return;
  __atomic_store_n(&lib_owner, 0, __ATOMIC_RELAXED);
  if (__atomic_exchange_n(&lib_word, 0, __ATOMIC_RELEASE) == 2)
    warmc_rt_wake(&lib_word);
}

/* ---- what is kept per thread ------------------------------------------
   Its last trap, and the bounds of its stack, which Linux has to read from
   /proc.  Kept by thread id under the lock rather than in thread-local
   storage, which a Linux library without libc has no say over.  A thread
   past the table's reach evicts the oldest entry near its own. */
#define LIB_TRAPS 1024
#define LIB_PROBE 16
static struct { u64 tid, stamp, lo, hi; u32 trap; } lib_traps[LIB_TRAPS];
static u64 lib_stamp;

static u32 trap_slot(u64 tid) {
  u32 h = (u32)((tid * 0x9e3779b97f4a7c15ull) >> 54);
  u32 best = h & (LIB_TRAPS - 1);
  for (u32 i = 0; i < LIB_PROBE; i++) {
    u32 s = (h + i) & (LIB_TRAPS - 1);
    if (lib_traps[s].tid == tid || !lib_traps[s].tid) return s;
    if (lib_traps[s].stamp < lib_traps[best].stamp) best = s;
  }
  return best;
}

static u32 my_slot(void) {
  u64 me = warmc_rt_thread_id();
  u32 s = trap_slot(me);
  if (lib_traps[s].tid != me) {
    lib_traps[s].tid = me;
    lib_traps[s].trap = 0;
    lib_traps[s].lo = lib_traps[s].hi = 0;
  }
  lib_traps[s].stamp = ++lib_stamp;
  return s;
}

static void set_last_trap(u32 trap) { lib_traps[my_slot()].trap = trap; }

u32 warmc_lib_last_trap(void) {
  lib_lock();
  u64 me = warmc_rt_thread_id();
  u32 s = trap_slot(me);
  u32 t = lib_traps[s].tid == me ? lib_traps[s].trap : 0;
  lib_unlock();
  return t;
}

/* ---- the calling thread's stack ---------------------------------------
   Wasm code checks each frame against the instance's stack limit, which a
   program warmc links sets from its own stack.  A library takes it from
   the stack of the thread that calls, leaving room below for the runtime
   and for a signal frame; where the platform cannot say, it allows a fixed
   budget below where the call came in. */
#define LIB_STACK_BUDGET (1ull << 20)

static u64 thread_stack_limit(void) {
  u64 sp = (u64)(uptr)__builtin_frame_address(0);
  u32 s = my_slot();
  /* A thread id outlives its thread, so the bounds kept for it hold only
     while the stack pointer is inside them. */
  if (sp <= lib_traps[s].lo || sp > lib_traps[s].hi)
    warmc_rt_stack_bounds(&lib_traps[s].lo, &lib_traps[s].hi);
  u64 lo = lib_traps[s].lo, hi = lib_traps[s].hi;
  if (!lo || sp <= lo || sp > hi) return sp > LIB_STACK_BUDGET ? sp - LIB_STACK_BUDGET : 0;
  u64 margin = (hi - lo) / 8;
  if (margin < (64u << 10)) margin = 64u << 10;
  if (margin > (256u << 10)) margin = 256u << 10;
  return lo + margin;
}

/* ---- the instance ----------------------------------------------------- */

static void say(const char *s) {
  usz n = 0;
  while (s[n]) n++;
  warmc_rt_write(2, s, n);
}

static u32 lib_init(void) {
  if (lib_vm) return 0;
  if (lib_failed) return lib_failed;
  if (!warmc_rt_library_mode()) {
    lib_failed = TRAP_LINK;
    return lib_failed;
  }
  const ModuleDesc *md = &warmc_module;
  for (u32 i = 0; i < md->nimports; i++) {
    if (lib_binds && lib_binds[i].code) continue;
    say("warmc: import \"");
    say(md->imports[i].module);
    say("\".\"");
    say(md->imports[i].name);
    say("\" was not bound before the library's first call\n");
    lib_failed = TRAP_LINK;
    return lib_failed;
  }
  warmc_rt_set_stack_limit(thread_stack_limit());
  u32 err = 0;
  WarmInstance *v = warmc_instantiate_bound_catching(md, lib_binds, &err);
  if (!v) { lib_failed = err ? err : TRAP_LINK; return lib_failed; }
  lib_vm = v;
  return 0;
}

int warmc_lib_bind_import(const char *module, const char *name, void *fn,
                          void *ctx) {
  const ModuleDesc *md = &warmc_module;
  int rc = 1;
  lib_lock();
  if (lib_vm || lib_failed) {
    rc = 2;
  } else {
    for (u32 i = 0; i < md->nimports; i++) {
      const ImportDesc *im = &md->imports[i];
      const char *a = im->module, *b = im->name;
      u32 k = 0;
      while (a[k] && a[k] == module[k]) k++;
      if (a[k] || module[k]) continue;
      k = 0;
      while (b[k] && b[k] == name[k]) k++;
      if (b[k] || name[k]) continue;
      if (!lib_binds) {
        lib_binds = warmc_rt_zalloc(sizeof(WarmBinding) * md->nimports);
        for (u32 j = 0; j < md->nimports; j++)
          lib_binds[j].name = md->imports[j].name;
      }
#if defined(__x86_64__)
      void **pair = warmc_rt_zalloc(2 * sizeof(void *));
      pair[0] = fn;
      pair[1] = ctx;
      lib_binds[i].inst = (WarmInstance *)pair;
      lib_binds[i].code = warmc_lib_adapters[i];
#else
      lib_binds[i].inst = (WarmInstance *)ctx;
      lib_binds[i].code = fn;
#endif
      rc = 0;
      break;
    }
  }
  lib_unlock();
  return rc;
}

usz warmc_lib_custom_section(const char *name, usz index, const u8 **data,
                             usz *size) {
  return warmc_custom_section(&warmc_module, name, index, data, size);
}

u8 *warmc_lib_memory(usz *size) {
  u8 *base = 0;
  usz n = 0;
  lib_lock();
  if (!lib_init() && warmc_module.nmemories) {
    base = lib_vm->mem_base;
    n = (usz)lib_vm->mem_size;
  }
  lib_unlock();
  if (size) *size = n;
  return base;
}

/* ---- arguments, as the C convention passed them -----------------------
   The thunk stores the eight integer and eight vector argument registers
   (Win64: the four of each) and passes where the caller's stack arguments
   begin. */
typedef struct {
  const u64 *gpr, *fpr;
  const u8 *stk;
  u32 ngr, nfr, npos;
  usz off;
} CArgs;

static u64 load_le(const u8 *p, u32 n) {
  u64 v = 0;
  for (u32 i = 0; i < n; i++) v |= (u64)p[i] << (8 * i);
  return v;
}

#if defined(__x86_64__)
/* Win64: the first four arguments by position, in RCX/RDX/R8/R9 or
   XMM0-3; the rest in eight-byte slots above the 32-byte shadow space. */
static u64 c_int(CArgs *a, u32 size) {
  u32 k = a->npos++;
  if (k < 4) return size == 4 ? (u32)a->gpr[k] : a->gpr[k];
  return load_le(a->stk + 32 + 8 * (usz)(k - 4), size);
}
static u64 c_float(CArgs *a, u32 size) {
  u32 k = a->npos++;
  if (k < 4) return size == 4 ? (u32)a->fpr[k] : a->fpr[k];
  return load_le(a->stk + 32 + 8 * (usz)(k - 4), size);
}
#else
static u64 stack_arg(CArgs *a, u32 size) {
#if defined(WARMC_DARWIN)
  /* Apple's arm64 convention packs stack arguments at their own size. */
  a->off = (a->off + size - 1) & ~(usz)(size - 1);
  u64 v = load_le(a->stk + a->off, size);
  a->off += size;
#else
  u64 v = load_le(a->stk + a->off, size);
  a->off += 8;
#endif
  return v;
}

/* AAPCS64: integers in x0-x7 and floats in v0-v7, each class on its own,
   then the stack. */
static u64 c_int(CArgs *a, u32 size) {
  if (a->ngr < 8) { u64 v = a->gpr[a->ngr++]; return size == 4 ? (u32)v : v; }
  return stack_arg(a, size);
}
static u64 c_float(CArgs *a, u32 size) {
  if (a->nfr < 8) { u64 v = a->fpr[a->nfr++]; return size == 4 ? (u32)v : v; }
  return stack_arg(a, size);
}
#endif

static u32 width(u8 t) { return t == VT_I32 || t == VT_F32 ? 4 : 8; }
static int is_float(u8 t) { return t == VT_F32 || t == VT_F64; }

/* The C entry of export idx.  out[0] is returned in x0/rax, out[1] in
   d0/xmm0.  An export with several results takes a pointer per result
   after its parameters and gets them written there. */
void warmc_lib_call(u32 idx, const u64 *gpr, const u64 *fpr, const u8 *stk,
                    u64 *out) {
  out[0] = out[1] = 0;
  lib_lock();
  u32 trap = lib_init();
  if (!trap) {
    const ExportDesc *e = &warmc_module.exports[idx];
    u64 iargs[8] = { 0 };
    u64 fargs[WARMC_FARGS_WORDS] = { 0 };
    u64 ret[WARMC_RETBUF_WORDS] = { 0 };
    CArgs a = { gpr, fpr, stk, 0, 0, 0, 0 };
    u32 ni = 0, nf = 0;
    for (u32 i = 0; i < e->nparams; i++) {
      u8 t = e->ptypes[i];
      if (is_float(t)) fargs[2 * nf++] = c_float(&a, width(t));
      else iargs[ni++] = c_int(&a, width(t));
    }
    u8 *outs[WARMC_RET_MAX_RESULTS];
    if (e->nresults > 1)
      for (u32 i = 0; i < e->nresults; i++) outs[i] = (u8 *)(uptr)c_int(&a, 8);

    u64 limit = lib_vm->stack_limit;
    if (lib_depth == 1) lib_vm->stack_limit = thread_stack_limit();
    FuncRef fr = warmc_export_func(lib_vm, e);
#if defined(__x86_64__)
    /* Parameters past the three integer and four float registers ride the
       callee's stack slots, eight bytes each in parameter order. */
    u64 stkimg[24];
    u32 w = 0, i2 = 0, f2 = 0;
    for (u32 i = 0; i < e->nparams; i++) {
      if (is_float(e->ptypes[i])) { if (f2 >= 4) stkimg[w++] = fargs[2 * f2]; f2++; }
      else { if (i2 >= 3) stkimg[w++] = iargs[i2]; i2++; }
    }
    trap = warmc_invoke_catching_s(fr.code, (WarmInstance *)fr.instance, iargs,
                                   fargs, ret, stkimg, 8ull * w);
#else
    trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance, iargs,
                                 fargs, ret);
#endif
    lib_vm->stack_limit = limit;

    if (!trap) {
      if (e->nresults == 1) {
        if (is_float(e->rtypes[0])) out[1] = ret[WARMC_RET_V0];
        else out[0] = ret[WARMC_RET_X0];
      } else {
        for (u32 i = 0; i < e->nresults; i++) {
          u64 v = ret[WARMC_RET_MULTI(i)];
          u32 n = width(e->rtypes[i]);
          for (u32 b = 0; b < n; b++) outs[i][b] = (u8)(v >> (8 * b));
        }
      }
    }
  }
  set_last_trap(trap);
  lib_unlock();
}

/* ---- the thunk ---------------------------------------------------------
   Entered from an export's own symbol with its index in w9 (R10D on x64)
   and the C arguments untouched. */
#if defined(WARMC_DARWIN)
#define CSYM(x) "_" #x
#else
#define CSYM(x) #x
#endif

#if defined(__x86_64__)
/* RCX/RDX/R8/R9 go to the caller's shadow space, which the stack arguments
   follow; XMM0-3 to this frame.  warmc_lib_call's fifth argument, the
   result words, rides the stack above its own shadow space. */
__attribute__((naked)) void warmc_lib_thunk(void) {
  __asm__ volatile(
    "movq   %rcx, 8(%rsp)\n"
    "movq   %rdx, 16(%rsp)\n"
    "movq   %r8, 24(%rsp)\n"
    "movq   %r9, 32(%rsp)\n"
    "subq   $88, %rsp\n"
    "movsd  %xmm0, 40(%rsp)\n"
    "movsd  %xmm1, 48(%rsp)\n"
    "movsd  %xmm2, 56(%rsp)\n"
    "movsd  %xmm3, 64(%rsp)\n"
    "leaq   72(%rsp), %rax\n"
    "movq   %rax, 32(%rsp)\n"
    "movl   %r10d, %ecx\n"
    "leaq   96(%rsp), %rdx\n"
    "leaq   40(%rsp), %r8\n"
    "leaq   96(%rsp), %r9\n"
    "call   " CSYM(warmc_lib_call) "\n"
    "movq   72(%rsp), %rax\n"
    "movsd  80(%rsp), %xmm0\n"
    "addq   $88, %rsp\n"
    "ret\n");
}
#else
/* x0-x7 and d0-d7 to this frame, and where the caller's stack arguments
   begin; the result words come back as x0 and d0. */
__attribute__((naked)) void warmc_lib_thunk(void) {
  __asm__ volatile(
    "stp    x29, x30, [sp, #-160]!\n"
    "mov    x29, sp\n"
    "stp    x0, x1, [sp, #16]\n"
    "stp    x2, x3, [sp, #32]\n"
    "stp    x4, x5, [sp, #48]\n"
    "stp    x6, x7, [sp, #64]\n"
    "stp    d0, d1, [sp, #80]\n"
    "stp    d2, d3, [sp, #96]\n"
    "stp    d4, d5, [sp, #112]\n"
    "stp    d6, d7, [sp, #128]\n"
    "mov    w0, w9\n"
    "add    x1, sp, #16\n"
    "add    x2, sp, #80\n"
    "add    x3, sp, #160\n"
    "add    x4, sp, #144\n"
    "bl     " CSYM(warmc_lib_call) "\n"
    "ldr    x0, [sp, #144]\n"
    "ldr    d0, [sp, #152]\n"
    "ldp    x29, x30, [sp], #160\n"
    "ret\n");
}
#endif
