/* rt/linuxposix.c -- the POSIX surface rt/wasi.c, rt/wasifs.c and rt/wasip2.c
   are written against, implemented over raw AArch64 Linux system calls.

   macOS answers those calls from libSystem, which every Mach-O program links,
   and Windows from rt/winposix.c.  A Linux program from warmc is a static ELF
   with no libc in it at all -- the core runtime makes its own system calls --
   so without this file the WASI hosts could only be linked by a compiler
   driver that brought a libc along.  This is the libc they need: the calls
   are the kernel's, one for one, and the rest is what a libc keeps in user
   space -- errno, the directory stream, the allocator.

   wasifs.h maps the names this file defines to warmc_lx_* on Linux, and so
   does every caller.  A program that does link a libc -- an embedder's own
   driver, or the `cc -nostartfiles` recipe this file replaces -- then keeps
   that libc's malloc and stdio whole, instead of having half of its
   allocator interposed by this file's.

   Layouts need no translation: wasifs.h's non-Darwin arm is the AArch64
   Linux one, so struct stat is what newfstatat writes, struct dirent is the
   record getdents64 writes, and errno numbers are the kernel's.

   Single-threaded by design: only the program's thread calls these.  The
   collector's thread lives in rt/rt.c and makes its own system calls. */

#include "wasifs.h"

#if !defined(WARMC_DARWIN) && !defined(WARMC_WINDOWS)

void *malloc(usz n);
void free(void *p);

/* ---- the system call ---- */

#define NR_dup          23
#define NR_fcntl        25
#define NR_ioctl        29
#define NR_mkdirat      34
#define NR_unlinkat     35
#define NR_symlinkat    36
#define NR_linkat       37
#define NR_ftruncate    46
#define NR_openat       56
#define NR_close        57
#define NR_getdents64   61
#define NR_lseek        62
#define NR_pread64      67
#define NR_pwrite64     68
#define NR_ppoll        73
#define NR_readlinkat   78
#define NR_newfstatat   79
#define NR_fstat        80
#define NR_fsync        82
#define NR_fdatasync    83
#define NR_utimensat    88
#define NR_nanosleep    101
#define NR_clock_gettime 113
#define NR_clock_getres 114
#define NR_rt_sigaction 134
#define NR_socket       198
#define NR_bind         200
#define NR_listen       201
#define NR_accept       202
#define NR_sendto       206
#define NR_recvfrom     207
#define NR_setsockopt   208
#define NR_shutdown     210
#define NR_munmap       215
#define NR_mmap         222
#define NR_renameat2    276

static long sc(long n, long a, long b, long c, long d, long e, long f) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  register long x4 __asm__("x4") = e;
  register long x5 __asm__("x5") = f;
  __asm__ volatile("svc #0"
                   : "+r"(x0)
                   : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                   : "memory", "cc");
  return x0;
}

/* ---- errno ---- */

static int g_errno;

int *warmc_lx_errno(void) { return &g_errno; }

/* A system call answers -errno; a libc call answers -1 and sets errno. */
static long ret(long r) {
  if (r < 0 && r > -4096) { g_errno = (int)-r; return -1; }
  return r;
}

#define SC0(n)                ret(sc((n), 0, 0, 0, 0, 0, 0))
#define SC1(n, a)             ret(sc((n), (long)(a), 0, 0, 0, 0, 0))
#define SC2(n, a, b)          ret(sc((n), (long)(a), (long)(b), 0, 0, 0, 0))
#define SC3(n, a, b, c)       ret(sc((n), (long)(a), (long)(b), (long)(c), 0, 0, 0))
#define SC4(n, a, b, c, d)    ret(sc((n), (long)(a), (long)(b), (long)(c), (long)(d), 0, 0))
#define SC5(n, a, b, c, d, e) \
  ret(sc((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), 0))
#define SC6(n, a, b, c, d, e, f) \
  ret(sc((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f)))

/* ---- files ---- */

#define H_O_TMPFILE 0x404000

int openat(int dir, const char *path, int flags, ...) {
  unsigned mode = 0;
  /* the mode is only there when the call creates something */
  if ((flags & H_CREAT) || (flags & H_O_TMPFILE) == H_O_TMPFILE) {
    __builtin_va_list ap;
    __builtin_va_start(ap, flags);
    mode = __builtin_va_arg(ap, unsigned);
    __builtin_va_end(ap);
  }
  return (int)SC4(NR_openat, dir, path, flags, mode);
}

int close(int fd) { return (int)SC1(NR_close, fd); }
int dup(int fd) { return (int)SC1(NR_dup, fd); }

long pread(int fd, void *buf, usz n, long long off) {
  return SC4(NR_pread64, fd, buf, n, off);
}

long pwrite(int fd, const void *buf, usz n, long long off) {
  return SC4(NR_pwrite64, fd, buf, n, off);
}

long long lseek(int fd, long long off, int whence) {
  return SC3(NR_lseek, fd, off, whence);
}

int fstat(int fd, void *st) { return (int)SC2(NR_fstat, fd, st); }

int fstatat(int dir, const char *path, void *st, int flags) {
  return (int)SC4(NR_newfstatat, dir, path, st, flags);
}

int mkdirat(int dir, const char *path, unsigned int mode) {
  return (int)SC3(NR_mkdirat, dir, path, mode);
}

int unlinkat(int dir, const char *path, int flags) {
  return (int)SC3(NR_unlinkat, dir, path, flags);
}

/* AArch64 has no renameat, only renameat2 with its flags. */
int renameat(int da, const char *a, int db, const char *b) {
  return (int)SC5(NR_renameat2, da, a, db, b, 0);
}

int symlinkat(const char *target, int dir, const char *path) {
  return (int)SC3(NR_symlinkat, target, dir, path);
}

long readlinkat(int dir, const char *path, char *buf, usz n) {
  return SC4(NR_readlinkat, dir, path, buf, n);
}

int linkat(int da, const char *a, int db, const char *b, int flags) {
  return (int)SC5(NR_linkat, da, a, db, b, flags);
}

int fsync(int fd) { return (int)SC1(NR_fsync, fd); }
int fdatasync(int fd) { return (int)SC1(NR_fdatasync, fd); }
int ftruncate(int fd, long long len) { return (int)SC2(NR_ftruncate, fd, len); }

#define H_F_GETFD 1
#define H_F_GETFL 3

int fcntl(int fd, int cmd, ...) {
  long arg = 0;
  /* the two queries the callers make take no argument; the rest take one */
  if (cmd != H_F_GETFD && cmd != H_F_GETFL) {
    __builtin_va_list ap;
    __builtin_va_start(ap, cmd);
    arg = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
  }
  return (int)SC3(NR_fcntl, fd, cmd, arg);
}

int utimensat(int dir, const char *path, const void *ts, int flags) {
  return (int)SC4(NR_utimensat, dir, path, ts, flags);
}

/* futimens is utimensat on the descriptor itself, which a null path asks for */
int futimens(int fd, const void *ts) {
  return (int)SC4(NR_utimensat, fd, 0, ts, 0);
}

#define H_TCGETS 0x5401
#define H_ENOTTY 25

int isatty(int fd) {
  u8 termios[64];
  if (ret(sc(NR_ioctl, fd, H_TCGETS, (long)termios, 0, 0, 0)) < 0) {
    g_errno = H_ENOTTY;
    return 0;
  }
  return 1;
}

/* ---- directory streams ---- */

/* A stream is the descriptor plus one getdents64 buffer of records, which are
   struct dirent exactly; readdir hands back a pointer into the buffer.  A
   record's d_off is the position of the one after it, which is what telldir
   answers and seekdir gives back to lseek -- the reading glibc has too. */
typedef struct {
  int fd;
  u32 len, at;
  long long pos;
  u8 buf[4096] __attribute__((aligned(8)));
} LxDir;

#define DE_OFF    8
#define DE_RECLEN 16

void *fdopendir(int fd) {
  u8 st[ST_SIZE];
  if (fstat(fd, st) < 0) return 0;
  if (wasifs_filetype(st) != FT_DIR) { g_errno = H_ENOTDIR; return 0; }
  LxDir *d = malloc(sizeof *d);
  if (!d) return 0;
  d->fd = fd;
  d->len = d->at = 0;
  d->pos = 0;
  return d;
}

void *readdir(void *dp) {
  LxDir *d = dp;
  if (d->at >= d->len) {
    long n = SC3(NR_getdents64, d->fd, d->buf, sizeof d->buf);
    if (n <= 0) return 0;   /* the end, or an error with errno set */
    d->len = (u32)n;
    d->at = 0;
  }
  u8 *de = d->buf + d->at;
  u16 reclen;
  __builtin_memcpy(&reclen, de + DE_RECLEN, 2);
  __builtin_memcpy(&d->pos, de + DE_OFF, 8);
  d->at += reclen;
  return de;
}

long telldir(void *dp) { return (long)((LxDir *)dp)->pos; }

void seekdir(void *dp, long loc) {
  LxDir *d = dp;
  lseek(d->fd, loc, 0);
  d->len = d->at = 0;
  d->pos = loc;
}

void rewinddir(void *dp) { seekdir(dp, 0); }

int closedir(void *dp) {
  LxDir *d = dp;
  int r = close(d->fd);
  free(d);
  return r;
}

/* ---- time ---- */

int clock_gettime(int id, void *ts) { return (int)SC2(NR_clock_gettime, id, ts); }
int clock_getres(int id, void *ts) { return (int)SC2(NR_clock_getres, id, ts); }

int nanosleep(const void *req, void *rem) {
  return (int)SC2(NR_nanosleep, req, rem);
}

/* AArch64 has ppoll and no poll: the timeout becomes a timespec, and none at
   all is how ppoll is told to wait for ever. */
int poll(void *fds, unsigned nfds, int timeout) {
  long ts[2];
  long *t = 0;
  if (timeout >= 0) {
    ts[0] = timeout / 1000;
    ts[1] = (long)(timeout % 1000) * 1000000;
    t = ts;
  }
  return (int)SC5(NR_ppoll, fds, nfds, t, 0, 8);
}

/* ---- signals ---- */

/* The one use is ignoring SIGPIPE, so a handler is only ever SIG_DFL or
   SIG_IGN and never runs: no restorer is needed. */
typedef void (*SigFn)(int);

SigFn signal(int sig, SigFn fn) {
  struct { void *handler; usz flags; void *restorer; usz mask; } act, old;
  act.handler = (void *)fn;
  act.flags = 0;
  act.restorer = 0;
  act.mask = 0;
  if (SC4(NR_rt_sigaction, sig, &act, &old, 8) < 0) return (SigFn)(uptr)-1;
  return (SigFn)old.handler;
}

/* ---- sockets ---- */

int socket(int dom, int type, int proto) {
  return (int)SC3(NR_socket, dom, type, proto);
}

int bind(int fd, const void *sa, unsigned int n) { return (int)SC3(NR_bind, fd, sa, n); }
int listen(int fd, int backlog) { return (int)SC2(NR_listen, fd, backlog); }
int accept(int fd, void *sa, void *n) { return (int)SC3(NR_accept, fd, sa, n); }

int setsockopt(int fd, int level, int name, const void *v, unsigned int n) {
  return (int)SC5(NR_setsockopt, fd, level, name, v, n);
}

int shutdown(int fd, int how) { return (int)SC2(NR_shutdown, fd, how); }

long send(int fd, const void *buf, usz n, int flags) {
  return SC6(NR_sendto, fd, buf, n, flags, 0, 0);
}

long recv(int fd, void *buf, usz n, int flags) {
  return SC6(NR_recvfrom, fd, buf, n, flags, 0, 0);
}

/* ---- memory ---- */

/* Size classes of 16 bytes to 64 KiB in powers of two, carved from 1 MiB
   arenas and recycled through a free list per class; anything bigger is a
   mapping of its own, returned to the kernel on free.  The callers allocate
   descriptor tables, path buffers and the odd read buffer, so this is about
   being correct and small rather than fast.  A block's 16-byte header says
   which it is, and keeps the payload 16-aligned. */
#define LX_MIN_SHIFT 4
#define LX_MAX_SHIFT 16
#define LX_NCLASS    (LX_MAX_SHIFT - LX_MIN_SHIFT + 1)
#define LX_ARENA     (1u << 20)
#define LX_BIG       0xffu

typedef struct { usz size; usz cls; } LxHdr;

static void *g_free_list[LX_NCLASS];
static u8 *g_arena, *g_arena_end;

static void *lx_map(usz n) {
  long r = sc(NR_mmap, 0, (long)n, 3 /* PROT_READ|WRITE */,
              0x22 /* MAP_PRIVATE|ANONYMOUS */, -1, 0);
  if (r < 0 && r > -4096) { g_errno = (int)-r; return 0; }
  return (void *)r;
}

void *malloc(usz n) {
  if (!n) n = 1;
  if (n > ((usz)1 << LX_MAX_SHIFT)) {
    if (n > ((usz)1 << 40)) { g_errno = 12 /* ENOMEM */; return 0; }
    usz size = (n + sizeof(LxHdr) + 4095) & ~(usz)4095;
    LxHdr *h = lx_map(size);
    if (!h) return 0;
    h->size = size;
    h->cls = LX_BIG;
    return h + 1;
  }
  usz cls = 0;
  while (((usz)1 << (cls + LX_MIN_SHIFT)) < n) cls++;
  LxHdr *h = g_free_list[cls];
  if (h) {
    g_free_list[cls] = *(void **)(h + 1);
  } else {
    usz need = sizeof(LxHdr) + ((usz)1 << (cls + LX_MIN_SHIFT));
    if (!g_arena || (usz)(g_arena_end - g_arena) < need) {
      g_arena = lx_map(LX_ARENA);
      if (!g_arena) return 0;
      g_arena_end = g_arena + LX_ARENA;
    }
    h = (LxHdr *)g_arena;
    g_arena += need;
  }
  h->size = (usz)1 << (cls + LX_MIN_SHIFT);
  h->cls = cls;
  return h + 1;
}

void free(void *p) {
  if (!p) return;
  LxHdr *h = (LxHdr *)p - 1;
  if (h->cls == LX_BIG) { sc(NR_munmap, (long)h, (long)h->size, 0, 0, 0, 0); return; }
  *(void **)p = g_free_list[h->cls];
  g_free_list[h->cls] = h;
}

void *realloc(void *p, usz n) {
  if (!p) return malloc(n);
  LxHdr *h = (LxHdr *)p - 1;
  usz have = h->cls == LX_BIG ? h->size - sizeof(LxHdr) : h->size;
  if (n && n <= have) return p;
  void *q = malloc(n);
  if (!q) return 0;
  u8 *d = q;
  const u8 *s = p;
  for (usz i = 0; i < have && i < n; i++) d[i] = s[i];
  free(p);
  return q;
}

#endif
