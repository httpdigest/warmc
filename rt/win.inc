/* The Windows arm of the runtime's OS layer, #included from rt.c when
   WARMC_WINDOWS is defined.  Win32 APIs are hand-declared (x64 has a single
   calling convention) and resolve through warmc's own PE import table --
   plain names become FF25 thunks, so no SDK import libraries are needed.

   Memory model: sys_mmap(PROT_NONE) reserves, sys_mprotect(READ|WRITE)
   commits (idempotent on already-committed pages), sys_mprotect(PROT_NONE)
   decommits, sys_munmap releases a whole reservation.  rt.c never unmaps a
   sub-range, so MEM_RELEASE with size 0 is always correct. */

/* ---- Win32 declarations ---- */

extern void *VirtualAlloc(void *addr, usz size, u32 type, u32 prot);
extern int VirtualFree(void *addr, usz size, u32 type);
extern __attribute__((noreturn)) void ExitProcess(u32 code);
extern void *GetStdHandle(u32 which);
extern int WriteFile(void *h, const void *buf, u32 n, u32 *written, void *ov);
extern int ReadFile(void *h, void *buf, u32 n, u32 *got, void *ov);
extern int QueryPerformanceCounter(u64 *count);
extern int QueryPerformanceFrequency(u64 *freq);
extern void GetSystemTimePreciseAsFileTime(u64 *ft);
extern int ProcessPrng(u8 *buf, usz n);
extern void *AddVectoredExceptionHandler(u32 first, long (*handler)(void *));
extern u32 GetCurrentThreadId(void);
/* the calling thread's reserved stack, for a DLL's stack limit (lib.c) */
extern void GetCurrentThreadStackLimits(uptr *low, uptr *high);
extern void *CreateThread(void *attr, usz stack, u32 (*fn)(void *), void *arg,
                          u32 flags, u32 *tid);
extern int CloseHandle(void *h);
extern int WaitOnAddress(volatile void *addr, void *compare, usz size, u32 ms);
extern void WakeByAddressSingle(void *addr);
extern u16 *GetCommandLineW(void);
extern u16 *GetEnvironmentStringsW(void);

/* defined later in rt.c */
static void eputs(const char *s);
static int addr_in_a_guard(const u8 *addr);
static int gcth_on_collector(void);

/* ---- memory ---- */

#define MAP_ANONYMOUS 0

#define WIN_MEM_COMMIT   0x1000u
#define WIN_MEM_RESERVE  0x2000u
#define WIN_MEM_DECOMMIT 0x4000u
#define WIN_MEM_RELEASE  0x8000u
#define WIN_MEM_RESET    0x80000u
#define WIN_PAGE_NOACCESS  0x01u
#define WIN_PAGE_READWRITE 0x04u

static void *sys_mmap(void *addr, usz len, int prot, int flags) {
  (void)flags;
  if (prot == PROT_NONE)
    return VirtualAlloc(addr, len, WIN_MEM_RESERVE, WIN_PAGE_NOACCESS);
  return VirtualAlloc(addr, len, WIN_MEM_RESERVE | WIN_MEM_COMMIT,
                      WIN_PAGE_READWRITE);
}
static int sys_mprotect(void *addr, usz len, int prot) {
  if (prot == PROT_NONE)
    return VirtualFree(addr, len, WIN_MEM_DECOMMIT) ? 0 : -1;
  return VirtualAlloc(addr, len, WIN_MEM_COMMIT, WIN_PAGE_READWRITE) ? 0 : -1;
}
static void sys_munmap(void *addr, usz len) {
  (void)len;
  VirtualFree(addr, 0, WIN_MEM_RELEASE);
}

#define WARMC_MADV_FREE   1
/* MEM_RESET does not guarantee zeroes on reuse */
#define WARMC_MADV_ZEROES 0
static int sys_madvise(void *addr, usz len, int advice) {
  (void)advice;
  return VirtualAlloc(addr, len, WIN_MEM_RESET, WIN_PAGE_READWRITE) ? 0 : -1;
}

/* ---- I/O and exit ---- */

__attribute__((noreturn)) static void sys_exit(int code) {
  ExitProcess((u32)code);
}

static void *win_std_handle(int fd) {
  /* -10 stdin, -11 stdout, -12 stderr */
  return GetStdHandle((u32)(-10 - fd));
}

static long sys_write(int fd, const void *buf, usz n) {
  if (fd < 0 || fd > 2) return -9;
  u32 chunk = n > (1u << 30) ? (1u << 30) : (u32)n;
  u32 written = 0;
  if (!WriteFile(win_std_handle(fd), buf, chunk, &written, 0)) return -5;
  return (long)written;
}

static long sys_read(int fd, void *buf, usz n) {
  if (fd < 0 || fd > 2) return -9;
  u32 chunk = n > (1u << 30) ? (1u << 30) : (u32)n;
  u32 got = 0;
  if (!ReadFile(win_std_handle(fd), buf, chunk, &got, 0)) return 0;
  return (long)got;
}

/* ---- time and randomness ---- */

static u64 win_qpc_freq;

static u64 sys_nanos(void) {
  u64 f = win_qpc_freq, c;
  if (!f) {
    QueryPerformanceFrequency(&f);
    win_qpc_freq = f;
  }
  QueryPerformanceCounter(&c);
  return (c / f) * 1000000000ull + (c % f) * 1000000000ull / f;
}

static u64 sys_realtime_nanos(void) {
  u64 ft;
  GetSystemTimePreciseAsFileTime(&ft);
  /* FILETIME epoch 1601-01-01, 100ns units */
  return (ft - 116444736000000000ull) * 100ull;
}

static int sys_random(void *buf, usz n) {
  return ProcessPrng((u8 *)buf, n) ? 0 : -1;
}

static void sys_prefer_fast_core(void) { }

/* ---- fault handling (vectored exception handler) ---- */

/* The VEH may not longjmp (ntdll holds the handler-list lock while
   dispatching), so a guard fault is redirected: rewrite Rip to this thunk
   with the trap code in Rcx and resume the thread. */
__attribute__((noreturn)) static void warmc_win_trap_thunk(u64 code) {
  warmc_trap((u32)code);
  __builtin_unreachable();
}

#define WIN_EXC_ACCESS_VIOLATION 0xC0000005u
/* EXCEPTION_RECORD: Code +0x00, ExceptionInformation[] +0x20
   CONTEXT: Rcx +0x80, Rsp +0x98, Rip +0xF8 */
static long warmc_win_veh(void *info) {
  u8 *rec = ((u8 **)info)[0];
  u8 *ctx = ((u8 **)info)[1];
  if (*(u32 *)rec != WIN_EXC_ACCESS_VIOLATION) return 0;
  u8 *addr = (u8 *)*(uptr *)(rec + 0x28);
  if (gcth_on_collector()) {
    eputs("warmc-rt: fault on the collector's thread\n");
    sys_exit(139);
  }
  if (!addr_in_a_guard(addr)) return 0;
  *(u64 *)(ctx + 0x80) = TRAP_MEM_OOB;
  u64 rsp = *(u64 *)(ctx + 0x98);
  rsp = ((rsp - 32) & ~15ull) - 8;   /* entry alignment: rsp % 16 == 8 */
  *(u64 *)(ctx + 0x98) = rsp;
  *(u64 *)(ctx + 0xf8) = (u64)(uptr)warmc_win_trap_thunk;
  return -1;                          /* EXCEPTION_CONTINUE_EXECUTION */
}

static void install_fault_handler(void) {
  AddVectoredExceptionHandler(1, warmc_win_veh);
}

/* ---- entry: ISA check, command line, environment ---- */

static void win_cpuid(u32 leaf, u32 sub, u32 *o) {
  __asm__ volatile("cpuid"
                   : "=a"(o[0]), "=b"(o[1]), "=c"(o[2]), "=d"(o[3])
                   : "a"(leaf), "c"(sub));
}

static int warmc_win_isa_ok(void) {
  u32 r[4];
  win_cpuid(1, 0, r);
  u32 need1 = (1u << 27) | (1u << 28) | (1u << 12);   /* OSXSAVE, AVX, FMA */
  int ok = (r[2] & need1) == need1;
  if (ok) {
    win_cpuid(7, 0, r);
    u32 need7 = (1u << 5) | (1u << 3) | (1u << 8);    /* AVX2, BMI1, BMI2 */
    ok = (r[1] & need7) == need7;
  }
  if (ok) {
    u32 lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    ok = (lo & 6) == 6;                               /* XMM and YMM state */
  }
  return ok;
}

static void warmc_win_check_isa(void) {
  if (!warmc_win_isa_ok()) {
    eputs("warmc-rt: this executable needs an x86-64-v3 CPU "
          "(AVX2, BMI2, FMA)\n");
    sys_exit(1);
  }
}

static usz win_utf8_of(const u16 *w, usz wn, u8 *dst) {
  usz o = 0;
  for (usz i = 0; i < wn; i++) {
    u32 c = w[i];
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < wn &&
        w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
      c = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00);
      i++;
    } else if (c >= 0xD800 && c < 0xE000) {
      c = 0xFFFD;
    }
    if (c < 0x80) {
      dst[o++] = (u8)c;
    } else if (c < 0x800) {
      dst[o++] = (u8)(0xC0 | (c >> 6));
      dst[o++] = (u8)(0x80 | (c & 63));
    } else if (c < 0x10000) {
      dst[o++] = (u8)(0xE0 | (c >> 12));
      dst[o++] = (u8)(0x80 | ((c >> 6) & 63));
      dst[o++] = (u8)(0x80 | (c & 63));
    } else {
      dst[o++] = (u8)(0xF0 | (c >> 18));
      dst[o++] = (u8)(0x80 | ((c >> 12) & 63));
      dst[o++] = (u8)(0x80 | ((c >> 6) & 63));
      dst[o++] = (u8)(0x80 | (c & 63));
    }
  }
  return o;
}

/* CommandLineToArgvW rules (post-2008 msvcrt): 2n backslashes before a
   quote emit n and toggle, 2n+1 emit n and a literal quote; "" inside
   quotes is a literal quote. */
static void warmc_win_cmdline(int *argc_out, char ***argv_out) {
  const u16 *wcl = GetCommandLineW();
  usz wn = 0;
  while (wcl[wn]) wn++;
  usz cap = 3 * wn + 4;
  usz argv_bytes = ((usz)wn / 2 + 2) * sizeof(char *);
  u8 *blk = (u8 *)sys_mmap(0, argv_bytes + 2 * cap, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS);
  if (!blk) {
    eputs("warmc-rt: cannot allocate the command line\n");
    sys_exit(1);
  }
  char **argv = (char **)blk;
  u8 *src = blk + argv_bytes;
  usz n = win_utf8_of(wcl, wn, src);
  src[n] = 0;
  char *o = (char *)(src + cap);

  int argc = 0;
  const char *p = (const char *)src;
  while (*p) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    argv[argc++] = o;
    int inq = 0;
    while (*p && (inq || (*p != ' ' && *p != '\t'))) {
      usz bs = 0;
      while (*p == '\\') { bs++; p++; }
      if (*p == '"') {
        for (usz i = 0; i < bs / 2; i++) *o++ = '\\';
        if (bs & 1) {
          *o++ = '"';
          p++;
        } else {
          p++;
          if (inq && *p == '"') { *o++ = '"'; p++; }
          else inq = !inq;
        }
      } else {
        for (usz i = 0; i < bs; i++) *o++ = '\\';
        if (*p && (inq || (*p != ' ' && *p != '\t'))) *o++ = *p++;
      }
    }
    *o++ = 0;
  }
  argv[argc] = 0;
  *argc_out = argc;
  *argv_out = argv;
}

static char **warmc_win_environ(void) {
  const u16 *blk = GetEnvironmentStringsW();
  usz wn = 0;
  u32 nent = 0;
  for (const u16 *p = blk; *p; ) {
    usz l = 0;
    while (p[l]) l++;
    nent++;
    wn += l + 1;
    p += l + 1;
  }
  u8 *mem = (u8 *)sys_mmap(0, ((usz)nent + 1) * sizeof(char *) + 3 * wn + 16,
                           PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS);
  if (!mem) {
    eputs("warmc-rt: cannot allocate the environment\n");
    sys_exit(1);
  }
  char **envp = (char **)mem;
  u8 *o = mem + ((usz)nent + 1) * sizeof(char *);
  u32 k = 0;
  for (const u16 *p = blk; *p; ) {
    usz l = 0;
    while (p[l]) l++;
    if (p[0] != '=') {                /* skip the "=C:=..." drive entries */
      envp[k++] = (char *)o;
      usz n = win_utf8_of(p, l, o);
      o[n] = 0;
      o += n + 1;
    }
    p += l + 1;
  }
  envp[k] = 0;
  return envp;
}

/* ---- freestanding memory intrinsics ----
   clang lowers aggregate copies and large zero-inits to these even under
   -ffreestanding; -fno-builtin keeps the loops from folding back into
   recursive libcalls. */

void *memcpy(void *d, const void *s, usz n) {
  u8 *dp = (u8 *)d;
  const u8 *sp = (const u8 *)s;
  while (n--) *dp++ = *sp++;
  return d;
}

void *memmove(void *d, const void *s, usz n) {
  u8 *dp = (u8 *)d;
  const u8 *sp = (const u8 *)s;
  if (dp < sp) {
    while (n--) *dp++ = *sp++;
  } else if (dp > sp) {
    while (n--) dp[n] = sp[n];
  }
  return d;
}

void *memset(void *d, int c, usz n) {
  u8 *dp = (u8 *)d;
  while (n--) *dp++ = (u8)c;
  return d;
}

int memcmp(const void *a, const void *b, usz n) {
  const u8 *pa = (const u8 *)a, *pb = (const u8 *)b;
  for (usz i = 0; i < n; i++)
    if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
  return 0;
}
