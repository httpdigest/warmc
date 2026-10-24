#include "wasifs.h"

extern const ModuleDesc warmc_module;
extern const ModuleDesc warmc_wasi_stub_module;

#define WASI_ESUCCESS 0
#define WASI_EBADF    8
#define WASI_EFAULT   21
#define WASI_EINVAL   28
#define WASI_ENOSYS   52
#define WASI_ENOTSUP  58
#define WASI_ESPIPE   70

extern void *malloc(usz);
extern void *realloc(void *, usz);
extern void free(void *);

static int g_argc;
static char **g_argv;

static void err_u64(unsigned long long v) {
  char b[24];
  int i = 24;
  if (!v) { warmc_rt_write(2, "0", 1); return; }
  while (v) { b[--i] = (char)('0' + (v % 10)); v /= 10; }
  warmc_rt_write(2, b + i, (usz)(24 - i));
}

static void err(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(2, s, (usz)(p - s));
}

static int bytes_eq(const char *a, const char *b, usz n) {
  for (usz i = 0; i < n; i++)
    if (a[i] != b[i]) return 0;
  return 1;
}

static usz cstrlen(const char *s) {
  const char *p = s;
  while (*p) p++;
  return (usz)(p - s);
}

static int ok(WarmInstance *vm, u64 off, u64 len) {
  return off + len <= vm->mem_size;
}

static int put32(WarmInstance *vm, u64 addr, u32 v) {
  if (!ok(vm, addr, 4)) return 0;
  __builtin_memcpy(vm->mem_base + addr, &v, 4);
  return 1;
}

static int put64(WarmInstance *vm, u64 addr, u64 v) {
  if (!ok(vm, addr, 8)) return 0;
  __builtin_memcpy(vm->mem_base + addr, &v, 8);
  return 1;
}

static int get32(WarmInstance *vm, u64 addr, u32 *out) {
  if (!ok(vm, addr, 4)) return 0;
  __builtin_memcpy(out, vm->mem_base + addr, 4);
  return 1;
}

static u32 to_wasi_errno(int e);

#if defined(WARMC_WINDOWS)
/* warmc_rt_read/write know only the three standard handles on Windows; every
   other descriptor lives in rt/winposix.c's table.  Both answer -errno, the
   way the raw syscalls behind warmc_rt_read/write do. */
extern long warmc_win_read(int fd, void *buf, usz n);
extern long warmc_win_write(int fd, const void *buf, usz n);
#define host_read  warmc_win_read
#define host_write warmc_win_write
#else
#define host_read  warmc_rt_read
#define host_write warmc_rt_write
#endif

#define IOV_MAX_TOTAL 0xffffffffull

static u64 iov_walk_fd(WarmInstance *vm, int hostfd, int writing, u64 iovs_,
                       u64 niov_, u64 pn_) {
  u32 iovs = (u32)iovs_, niov = (u32)niov_, pn = (u32)pn_;
  u64 total = 0;
  for (u32 i = 0; i < niov; i++) {
    u64 rec = (u64)iovs + 8ull * i;
    u32 buf, len;
    if (!get32(vm, rec, &buf) || !get32(vm, rec + 4, &len)) return WASI_EFAULT;
    if (!ok(vm, buf, len)) return WASI_EFAULT;
    if (!len) continue;
    /* the count comes back in 32 bits: no more than they can say moves */
    if (len > IOV_MAX_TOTAL - total) len = (u32)(IOV_MAX_TOTAL - total);
    if (!len) break;
    long r = writing ? host_write(hostfd, vm->mem_base + buf, len)
                     : host_read(hostfd, vm->mem_base + buf, len);

    if (r < 0) {

      if (total) break;
      return to_wasi_errno((int)-r);
    }
    total += (u64)r;

    if ((usz)r < len) break;
  }
  if (!put32(vm, pn, (u32)total)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

/* An exit status is what the process exits with.  One the system cannot
   carry -- it keeps 8 bits, so 256 would read as success -- or that a shell
   takes for its own (126 and up) is a trap, as wasmtime makes it. */
__attribute__((noreturn))
static u64 wasi_proc_exit(WarmInstance *vm, u64 code) {
  if ((u32)code >= 126) {
    err("warmc-wasi: proc_exit(");
    err_u64((u32)code);
    err("): an exit status outside [0, 126)\n");
    warmc_trap(TRAP_UNREACHABLE);
  }
  warmc_report_run(vm);
  warmc_rt_exit((int)(u32)code);
}

static u64 g_seed_state;
static int g_seed_fixed;

static u64 splitmix64(u64 *s) {
  *s += 0x9e3779b97f4a7c15ull;
  u64 z = *s;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

static void seed_init(void) {
  char **e = warmc_rt_environ();
  const char *v = 0;
  if (e)
    for (int i = 0; e[i]; i++) {
      const char *p = e[i], *q = "WARMC_RANDOM_SEED";
      while (*q && *p == *q) { p++; q++; }
      if (!*q && *p == '=') { v = p + 1; break; }
    }
  if (!v) return;
  u64 n = 0;
  for (const char *p = v; *p >= '0' && *p <= '9'; p++) n = n * 10 + (u64)(*p - '0');
  g_seed_state = n;
  g_seed_fixed = 1;
  err("warmc-wasi: random_get is a fixed stream (WARMC_RANDOM_SEED)\n");
}

static u64 wasi_random_get(WarmInstance *vm, u64 buf_, u64 len_) {
  u32 buf = (u32)buf_, len = (u32)len_;
  if (!ok(vm, buf, len)) return WASI_EFAULT;
  if (g_seed_fixed) {
    for (u32 i = 0; i < len;) {
      u64 w = splitmix64(&g_seed_state);
      for (u32 k = 0; k < 8 && i < len; k++, i++)
        vm->mem_base[buf + i] = (u8)(w >> (8 * k));
    }
    return WASI_ESUCCESS;
  }
  if (warmc_rt_random(vm->mem_base + buf, len) != 0) return WASI_ENOSYS;
  return WASI_ESUCCESS;
}

static u64 clock_query(int want_res, u32 id, u64 *out);

static u64 wasi_clock_time_get(WarmInstance *vm, u64 id_, u64 prec_, u64 pt_) {
  (void)prec_;
  u64 v;
  u64 rc = clock_query(0, (u32)id_, &v);
  if (rc) return rc;
  if (!put64(vm, (u32)pt_, v)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 wasi_clock_res_get(WarmInstance *vm, u64 id_, u64 pr_) {
  u64 v;
  u64 rc = clock_query(1, (u32)id_, &v);
  if (rc) return rc;

  if (!v) v = 1;
  if (!put64(vm, (u32)pr_, v)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 sizes_get(WarmInstance *vm, char **v, int n, u64 pcount, u64 psize) {
  u64 bytes = 0;
  for (int i = 0; i < n; i++) bytes += cstrlen(v[i]) + 1;
  if (!put32(vm, (u32)pcount, (u32)n)) return WASI_EFAULT;
  if (!put32(vm, (u32)psize, (u32)bytes)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 vec_get(WarmInstance *vm, char **v, int n, u64 ptrs_, u64 blob_) {
  u32 ptrs = (u32)ptrs_, blob = (u32)blob_;
  u64 at = blob;
  for (int i = 0; i < n; i++) {
    usz len = cstrlen(v[i]) + 1;
    if (!ok(vm, at, len)) return WASI_EFAULT;
    if (!put32(vm, (u64)ptrs + 4ull * (u32)i, (u32)at)) return WASI_EFAULT;
    for (usz j = 0; j < len; j++) vm->mem_base[at + j] = (u8)v[i][j];
    at += len;
  }
  return WASI_ESUCCESS;
}

static u64 wasi_args_sizes_get(WarmInstance *vm, u64 pc, u64 ps) {
  return sizes_get(vm, g_argv, g_argc, pc, ps);
}
static u64 wasi_args_get(WarmInstance *vm, u64 p, u64 b) {
  return vec_get(vm, g_argv, g_argc, p, b);
}

static char **g_env;
static int g_nenv, g_capenv;
static int g_env_inherit;

static int add_env(char *kv) {

  if (g_nenv + 1 >= g_capenv) {
    int cap = g_capenv ? g_capenv * 2 : 32;
    char **p = (char **)realloc(g_env, (usz)cap * sizeof *p);
    if (!p) return 0;
    g_env = p;
    g_capenv = cap;
  }
  g_env[g_nenv++] = kv;
  return 1;
}

static char **guest_environ(void) {
  if (g_env_inherit) return warmc_rt_environ();

  if (!g_env) return 0;
  g_env[g_nenv] = 0;
  return g_env;
}

static int environ_count(void) {
  char **e = guest_environ();
  int n = 0;
  if (e) while (e[n]) n++;
  return n;
}

static u64 wasi_environ_sizes_get(WarmInstance *vm, u64 pc, u64 ps) {
  return sizes_get(vm, guest_environ(), environ_count(), pc, ps);
}
static u64 wasi_environ_get(WarmInstance *vm, u64 p, u64 b) {
  return vec_get(vm, guest_environ(), environ_count(), p, b);
}

static u64 wasi_sched_yield(WarmInstance *vm) {
  (void)vm;
  return WASI_ESUCCESS;
}

extern int dup(int);
extern void *fdopendir(int);
extern void *readdir(void *);
extern int closedir(void *);
extern void rewinddir(void *);
extern long telldir(void *);
extern void seekdir(void *, long);

#if defined(WARMC_DARWIN)

static int host_fdatasync(int fd) { return fsync(fd); }
#else
extern int fdatasync(int);
static int host_fdatasync(int fd) { return fdatasync(fd); }
#endif

static u32 to_wasi_errno(int e) { return wasifs_errno(e); }

extern int clock_gettime(int, void *);
extern int clock_getres(int, void *);

#if defined(WARMC_DARWIN)

static const int H_CLOCK[4] = { 0 , 8 , 12 , 16  };
#elif !defined(WARMC_WINDOWS)
static const int H_CLOCK[4] = { 0, 1, 2, 3 };
#endif

#if defined(WARMC_WINDOWS)
/* A timespec is two longs, which are 32 bits here, and the two CPU clocks
   are a different API rather than a different clock id -- so Windows answers
   all four in nanoseconds directly. */
extern int warmc_win_clock(int want_res, u32 id, u64 *out);
#endif

static u64 clock_query(int want_res, u32 id, u64 *out) {

  if (id > 3) return WASI_EINVAL;
#if defined(WARMC_WINDOWS)
  if (warmc_win_clock(want_res, id, out) < 0) return to_wasi_errno(h_errno);
  return WASI_ESUCCESS;
#else
  long ts[2] = { 0, 0 };
  int r = want_res ? clock_getres(H_CLOCK[id], ts)
                   : clock_gettime(H_CLOCK[id], ts);
  if (r < 0) return to_wasi_errno(h_errno);
  *out = (u64)ts[0] * 1000000000ull + (u64)ts[1];
  return WASI_ESUCCESS;
#endif
}

#define WASI_ENOENT      44
#define WASI_ENOTDIR     54
#define WASI_ENOTCAPABLE 76
#define WASI_ELOOP       32
#define WASI_ENAMETOOLONG 37
#define WASI_EPERM       63
#define WASI_EISDIR      31
#define WASI_EEXIST      20
#define WASI_ERANGE      68
#define WASI_EOVERFLOW   61

#define FT_SOCK_DGRAM  5
#define FT_SOCK_STREAM 6

#define FD_INIT  64

#define FD_LIMIT 65536

#define FDFLAG_APPEND   0x01u
#define FDFLAG_DSYNC    0x02u
#define FDFLAG_NONBLOCK 0x04u
#define FDFLAG_RSYNC    0x08u
#define FDFLAG_SYNC     0x10u
#define FDFLAG_ALL      0x1fu

typedef struct {
  int host;
  u8 type;
  u8 preopen;
  u8 keep_host;

  u16 fdflags;
  const char *pre_name;
  u64 rights, rights_inh;

  void *dir;
  u64 dir_cookie;
} Fd;

static Fd *g_fds;
static u32 g_nfds;

static void fd_clear(Fd *f) {
  f->host = -1;
  f->type = 0;
  f->preopen = 0;
  f->keep_host = 0;
  f->fdflags = 0;
  f->pre_name = 0;
  f->rights = 0;
  f->rights_inh = 0;
  f->dir = 0;
  f->dir_cookie = 0;
}

static int fds_reserve(u32 want) {
  if (want < g_nfds) return 1;
  if (want >= FD_LIMIT) return 0;
  u32 n = g_nfds ? g_nfds : FD_INIT;
  while (n <= want) n *= 2;
  if (n > FD_LIMIT) n = FD_LIMIT;
  Fd *p = (Fd *)realloc(g_fds, (usz)n * sizeof *p);
  if (!p) return 0;
  for (u32 i = g_nfds; i < n; i++) fd_clear(&p[i]);
  g_fds = p;
  g_nfds = n;
  return 1;
}

static Fd *fd_get(u32 i) {
  if (i >= g_nfds || g_fds[i].host < 0) return 0;
  return &g_fds[i];
}

static int fd_alloc(void) {

  for (u32 i = 0; i < g_nfds; i++) if (g_fds[i].host < 0) return (int)i;
  u32 at = g_nfds;
  if (!fds_reserve(at)) return -1;
  return (int)at;
}

#define R_FD_DATASYNC        (1ull << 0)
#define R_FD_READ            (1ull << 1)
#define R_FD_SEEK            (1ull << 2)
#define R_FD_FDSTAT_SET_FLAGS (1ull << 3)
#define R_FD_SYNC            (1ull << 4)
#define R_FD_TELL            (1ull << 5)
#define R_FD_WRITE           (1ull << 6)
#define R_FD_ADVISE          (1ull << 7)
#define R_FD_ALLOCATE        (1ull << 8)
#define R_PATH_CREATE_DIRECTORY (1ull << 9)
#define R_PATH_CREATE_FILE   (1ull << 10)
#define R_PATH_LINK_SOURCE   (1ull << 11)
#define R_PATH_LINK_TARGET   (1ull << 12)
#define R_PATH_OPEN          (1ull << 13)
#define R_FD_READDIR         (1ull << 14)
#define R_PATH_READLINK      (1ull << 15)
#define R_PATH_RENAME_SOURCE (1ull << 16)
#define R_PATH_RENAME_TARGET (1ull << 17)
#define R_PATH_FILESTAT_GET  (1ull << 18)
#define R_PATH_FILESTAT_SET_SIZE (1ull << 19)
#define R_PATH_FILESTAT_SET_TIMES (1ull << 20)
#define R_FD_FILESTAT_GET    (1ull << 21)
#define R_FD_FILESTAT_SET_SIZE (1ull << 22)
#define R_FD_FILESTAT_SET_TIMES (1ull << 23)
#define R_PATH_SYMLINK       (1ull << 24)
#define R_PATH_REMOVE_DIRECTORY (1ull << 25)
#define R_PATH_UNLINK_FILE   (1ull << 26)
#define R_POLL_FD_READWRITE  (1ull << 27)
#define R_SOCK_SHUTDOWN      (1ull << 28)
#define R_SOCK_ACCEPT        (1ull << 29)

#define RIGHTS_FILE (R_FD_DATASYNC | R_FD_READ | R_FD_SEEK | \
                     R_FD_FDSTAT_SET_FLAGS | \
                     R_FD_SYNC | R_FD_TELL | R_FD_WRITE | R_FD_ADVISE | \
                     R_FD_ALLOCATE | R_FD_FILESTAT_GET | \
                     R_FD_FILESTAT_SET_SIZE | R_FD_FILESTAT_SET_TIMES | \
                     R_POLL_FD_READWRITE)

#define RIGHTS_DIR (R_FD_DATASYNC | R_FD_FDSTAT_SET_FLAGS | R_FD_SYNC | \
                    R_FD_ADVISE | \
                    R_PATH_CREATE_DIRECTORY | R_PATH_CREATE_FILE | \
                    R_PATH_LINK_SOURCE | R_PATH_LINK_TARGET | R_PATH_OPEN | \
                    R_FD_READDIR | R_PATH_READLINK | R_PATH_RENAME_SOURCE | \
                    R_PATH_RENAME_TARGET | R_PATH_FILESTAT_GET | \
                    R_PATH_FILESTAT_SET_SIZE | R_PATH_FILESTAT_SET_TIMES | \
                    R_FD_FILESTAT_GET | R_FD_FILESTAT_SET_TIMES | \
                    R_PATH_SYMLINK | R_PATH_REMOVE_DIRECTORY | \
                    R_PATH_UNLINK_FILE)

#define RIGHTS_SOCKET (R_FD_READ | R_FD_WRITE | R_FD_FDSTAT_SET_FLAGS | \
                       R_FD_FILESTAT_GET | R_POLL_FD_READWRITE | \
                       R_SOCK_SHUTDOWN)

#define RIGHTS_LISTENER (RIGHTS_SOCKET | R_SOCK_ACCEPT)

#define H_SETTABLE (H_APPEND | H_NONBLOCK | H_SYNC)

static int host_fdflags(u32 fdflags) {
  int hf = 0;
  if (fdflags & FDFLAG_APPEND) hf |= H_APPEND;
  if (fdflags & FDFLAG_NONBLOCK) hf |= H_NONBLOCK;

  if (fdflags & (FDFLAG_DSYNC | FDFLAG_SYNC)) hf |= H_SYNC;
  return hf;
}


static u32 copy_path(WarmInstance *vm, u64 p_, u64 len_, char *out) {
  u32 p = (u32)p_, len = (u32)len_;
  if (!ok(vm, p, len)) return WASI_EFAULT;
  if (len >= PATHMAX) return WASI_ENAMETOOLONG;
  for (u32 i = 0; i < len; i++) {
    char c = (char)vm->mem_base[p + i];

    if (!c) return WASI_EINVAL;
    out[i] = c;
  }
  out[len] = 0;
  return WASI_ESUCCESS;
}

static int str_eq(const char *a, const char *b) { return wasifs_str_eq(a, b); }

static u32 walk(int root, const char *path, int *out_dir, char *leaf) {
  return wasifs_walk(root, path, out_dir, leaf);
}

static void walk_done(int root, int dir) { wasifs_walk_done(root, dir); }

static u64 st_u64(const u8 *st, int off) { return wasifs_st_u64(st, off); }

static u32 st_small(const u8 *st, int off) { return wasifs_st_small(st, off); }

static u64 st_time(const u8 *st, int off) { return wasifs_st_time(st, off); }

static u8 st_filetype(const u8 *st) { return wasifs_filetype(st); }

static u32 put_filestat(WarmInstance *vm, u64 buf_, const u8 *st) {
  u32 buf = (u32)buf_;
  if (!ok(vm, buf, 64)) return WASI_EFAULT;
  put64(vm, buf, 0);
  put64(vm, buf + 8, st_u64(st, ST_OFF_INO));
  vm->mem_base[buf + 16] = st_filetype(st);
  for (int i = 17; i < 24; i++) vm->mem_base[buf + i] = 0;
  put64(vm, buf + 24, st_small(st, ST_OFF_NLINK));
  put64(vm, buf + 32, st_u64(st, ST_OFF_FSIZE));
  put64(vm, buf + 40, st_time(st, ST_OFF_ATIME));
  put64(vm, buf + 48, st_time(st, ST_OFF_MTIME));
  put64(vm, buf + 56, st_time(st, ST_OFF_CTIME));
  return WASI_ESUCCESS;
}

#if defined(WARMC_WINDOWS)
/* x64 spills arguments in eight-byte slots, the same as the Win64 ABI this
   file is compiled for, so path_open's last two parameters need no help. */
typedef u64 wide_arg;
#else
/* AArch64 has eight argument registers and warmc spills the rest in 16-byte
   slots, so the two parameters past the eighth have to be declared as
   something that occupies one.  An __int128 does; a u64 would sit in the
   low half of the slot the caller wrote and read the wrong one. */
typedef unsigned __int128 wide_arg;
#endif

static u32 walk_leaf(int root, char *path, int *out_dir, char *leaf,
                     int follow) {
  return wasifs_walk_leaf(root, path, out_dir, leaf, follow);
}

static u32 wasi_path_open(WarmInstance *vm, u64 dirfd_, u64 dirflags_,
                          u64 path_, u64 plen_, u64 oflags_, u64 rights_,
                          u64 rights_inh_, wide_arg fdflags_w, wide_arg pfd_w) {
  u64 fdflags_ = (u64)fdflags_w, pfd_ = (u64)pfd_w;
  Fd *d = fd_get((u32)dirfd_);
  if (!d) return WASI_EBADF;
  if (d->type != FT_DIR) return WASI_ENOTDIR;
  if (!(d->rights & R_PATH_OPEN)) return WASI_ENOTCAPABLE;
  if (((u32)oflags_ & 1) && !(d->rights & R_PATH_CREATE_FILE))
    return WASI_ENOTCAPABLE;
  if (((u32)oflags_ & 8) && !(d->rights & R_PATH_FILESTAT_SET_SIZE))
    return WASI_ENOTCAPABLE;

  char path[PATHMAX], leaf[PATHMAX];
  u32 rc = copy_path(vm, path_, plen_, path);
  if (rc) return rc;
  /* a file is not made by a name that says it is a directory.  The errno is
     the host's open(2), as wasmtime passes it on: Linux answers EISDIR for any
     O_CREAT path that ends in a slash, macOS EINVAL; Windows takes Linux's,
     as rt/winposix.c does throughout */
  if (((u32)oflags_ & 1) && plen_ && path[(u32)plen_ - 1] == '/')
#if defined(WARMC_DARWIN)
    return WASI_EINVAL;
#else
    return WASI_EISDIR;
#endif

  u32 oflags = (u32)oflags_, fdflags = (u32)fdflags_;
  u64 rights = rights_ & d->rights_inh;
  u64 rights_inh = rights_inh_ & d->rights_inh;

  int hf;
  if ((rights & R_FD_READ) && (rights & R_FD_WRITE)) hf = H_RDWR;
  else if (rights & R_FD_WRITE) hf = H_WRONLY;
  else hf = H_RDONLY;
  if (oflags & 1) hf |= H_CREAT;
  if (oflags & 2) hf |= H_DIRECTORY;
  if (oflags & 4) hf |= H_EXCL;
  if (oflags & 8) hf |= H_TRUNC;
  hf |= host_fdflags(fdflags);

  int follow = ((u32)dirflags_ & 1) != 0;
  int links = 0;
  int hostfd = -1;
  for (;;) {
    int dir;
    rc = walk(d->host, path, &dir, leaf);
    if (rc) return rc;

    const char *name = leaf[0] ? leaf : ".";
    hostfd = openat(dir, name, hf | H_NOFOLLOW, 0666);
    if (hostfd >= 0) { walk_done(d->host, dir); break; }
    int e = h_errno;
    /* a link opened without following it: ELOOP, or on Linux ENOTDIR when
       a directory is asked for, which a file answers too -- so ENOTDIR from
       what is no link stays ENOTDIR */
    if (!follow || (e != H_ELOOP && e != H_ENOTDIR)) { walk_done(d->host, dir); return to_wasi_errno(e); }

    char link[PATHMAX];
    long n = readlinkat(dir, name, link, PATHMAX - 1);
    walk_done(d->host, dir);
    if (n < 0) return to_wasi_errno(e == H_ENOTDIR ? e : h_errno);
    link[n] = 0;
    if (link[0] == '/') return WASI_EPERM;
    if (++links > MAXLINKS) return WASI_ELOOP;
    rc = wasifs_splice_leaf(path, link);
    if (rc) return rc;
  }

  u8 st[ST_SIZE];
  if (fstat(hostfd, st) < 0) { int e = h_errno; close(hostfd); return to_wasi_errno(e); }
  u8 ft = st_filetype(st);

  int slot = fd_alloc();
  if (slot < 0) { close(hostfd); return 33 ; }

  fd_clear(&g_fds[slot]);
  g_fds[slot].host = hostfd;
  g_fds[slot].type = ft;
  g_fds[slot].fdflags = (u16)(fdflags & FDFLAG_ALL);

  g_fds[slot].rights = rights & (ft == FT_DIR ? RIGHTS_DIR : RIGHTS_FILE);
  g_fds[slot].rights_inh = rights_inh;
  if (!put32(vm, (u32)pfd_, (u32)slot)) { close(hostfd); g_fds[slot].host = -1; return WASI_EFAULT; }
  return WASI_ESUCCESS;
}

static u64 wasi_fd_prestat_get2(WarmInstance *vm, u64 fd_, u64 buf_) {
  Fd *f = fd_get((u32)fd_);
  if (!f || !f->preopen) return WASI_EBADF;
  u32 buf = (u32)buf_;
  if (!ok(vm, buf, 8)) return WASI_EFAULT;

  if (!put32(vm, buf, 0)) return WASI_EFAULT;
  if (!put32(vm, buf + 4, (u32)cstrlen(f->pre_name))) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 wasi_fd_prestat_dir_name(WarmInstance *vm, u64 fd_, u64 p_, u64 n_) {
  Fd *f = fd_get((u32)fd_);
  /* an open descriptor that is no preopen has no preopen's name, as
     wasmtime answers it; one not open is none at all */
  if (!f) return WASI_EBADF;
  if (!f->preopen) return WASI_ENOTDIR;
  u32 p = (u32)p_, n = (u32)n_;
  usz len = cstrlen(f->pre_name);
  if (n < len) return 37 /* ENAMETOOLONG */;
  if (!ok(vm, p, len)) return WASI_EFAULT;
  for (usz i = 0; i < len; i++) vm->mem_base[p + i] = (u8)f->pre_name[i];
  return WASI_ESUCCESS;
}

static u64 wasi_fd_close2(WarmInstance *vm, u64 fd_) {
  (void)vm;
  u32 i = (u32)fd_;
  Fd *f = fd_get(i);
  if (!f) return WASI_EBADF;

  if (f->dir) closedir(f->dir);
  if (!f->keep_host) close(f->host);
  fd_clear(f);
  return WASI_ESUCCESS;
}

static u64 wasi_fd_fdstat_get2(WarmInstance *vm, u64 fd_, u64 buf_) {
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  u32 buf = (u32)buf_;
  if (!ok(vm, buf, 24)) return WASI_EFAULT;
  for (int i = 0; i < 24; i++) vm->mem_base[buf + i] = 0;
  vm->mem_base[buf] = f->type;

  __builtin_memcpy(vm->mem_base + buf + 2, &f->fdflags, 2);
  if (!put64(vm, (u64)buf + 8, f->rights)) return WASI_EFAULT;
  if (!put64(vm, (u64)buf + 16, f->rights_inh)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 wasi_fd_fdstat_set_flags(WarmInstance *vm, u64 fd_, u64 flags_) {
  (void)vm;
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & R_FD_FDSTAT_SET_FLAGS)) return WASI_ENOTCAPABLE;
  u16 want = (u16)((u32)flags_ & FDFLAG_ALL);

  int cur = fcntl(f->host, 3 , 0);
  if (cur < 0) return to_wasi_errno(h_errno);
  if (fcntl(f->host, 4 , (cur & ~H_SETTABLE) | host_fdflags(want)) < 0) {

    int keep = H_APPEND | H_NONBLOCK;
    if (fcntl(f->host, 4, (cur & ~keep) | (host_fdflags(want) & keep)) < 0)
      return to_wasi_errno(h_errno);
  }
  f->fdflags = want;
  return WASI_ESUCCESS;
}

static u64 wasi_fd_fdstat_set_rights(WarmInstance *vm, u64 fd_, u64 r_, u64 ri_) {
  (void)vm;
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;

  if ((r_ & ~f->rights) || (ri_ & ~f->rights_inh)) return WASI_ENOTCAPABLE;
  f->rights = r_;
  f->rights_inh = ri_;
  return WASI_ESUCCESS;
}

static u64 wasi_fd_filestat_get(WarmInstance *vm, u64 fd_, u64 buf_) {
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & R_FD_FILESTAT_GET)) return WASI_ENOTCAPABLE;
  u8 st[ST_SIZE];
  if (fstat(f->host, st) < 0) return to_wasi_errno(h_errno);
  return put_filestat(vm, buf_, st);
}

static u64 wasi_path_filestat_get(WarmInstance *vm, u64 fd_, u64 flags_,
                                  u64 p_, u64 n_, u64 buf_) {
  Fd *d = fd_get((u32)fd_);
  if (!d) return WASI_EBADF;
  if (d->type != FT_DIR) return WASI_ENOTDIR;
  if (!(d->rights & R_PATH_FILESTAT_GET)) return WASI_ENOTCAPABLE;
  char path[PATHMAX], leaf[PATHMAX];
  u32 rc = copy_path(vm, p_, n_, path);
  if (rc) return rc;
  int dir;
  rc = walk_leaf(d->host, path, &dir, leaf, ((u32)flags_ & 1) != 0);
  if (rc) return rc;
  u8 st[ST_SIZE];
  int r = fstatat(dir, leaf[0] ? leaf : ".", st, H_AT_NOFOLLOW);
  walk_done(d->host, dir);
  if (r < 0) return to_wasi_errno(h_errno);
  return put_filestat(vm, buf_, st);
}

static u64 fs_io(WarmInstance *vm, u64 fd_, u64 iovs, u64 niov, u64 pn,
                 int writing) {
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & (writing ? R_FD_WRITE : R_FD_READ))) return WASI_ENOTCAPABLE;
  if (f->type == FT_DIR) return WASI_EISDIR;
  if ((f->fdflags & FDFLAG_APPEND) && writing) lseek(f->host, 0, 2 );
  return iov_walk_fd(vm, f->host, writing, iovs, niov, pn);
}

static u64 fs_pio(WarmInstance *vm, u64 fd_, u64 iovs_, u64 niov_, u64 off_,
                  u64 pn_, int writing) {
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & (writing ? R_FD_WRITE : R_FD_READ))) return WASI_ENOTCAPABLE;
  if (!(f->rights & R_FD_SEEK)) return WASI_ENOTCAPABLE;
  if (f->type == FT_DIR) return WASI_EISDIR;
  u32 iovs = (u32)iovs_, niov = (u32)niov_, pn = (u32)pn_;
  long long off = (long long)off_;
  u64 total = 0;
  for (u32 i = 0; i < niov; i++) {
    u64 rec = (u64)iovs + 8ull * i;
    u32 buf, len;
    if (!get32(vm, rec, &buf) || !get32(vm, rec + 4, &len)) return WASI_EFAULT;
    if (!ok(vm, buf, len)) return WASI_EFAULT;
    if (!len) continue;
    /* the count comes back in 32 bits: no more than they can say moves */
    if (len > IOV_MAX_TOTAL - total) len = (u32)(IOV_MAX_TOTAL - total);
    if (!len) break;
    long r = writing ? pwrite(f->host, vm->mem_base + buf, len, off)
                     : pread(f->host, vm->mem_base + buf, len, off);
    if (r < 0) {
      if (total) break;
      return to_wasi_errno(h_errno);
    }
    total += (u64)r;
    off += r;
    if ((usz)r < len) break;
  }
  if (!put32(vm, pn, (u32)total)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 wasi_fd_pread(WarmInstance *vm, u64 a, u64 b, u64 c, u64 d, u64 e) {
  return fs_pio(vm, a, b, c, d, e, 0);
}
static u64 wasi_fd_pwrite(WarmInstance *vm, u64 a, u64 b, u64 c, u64 d, u64 e) {
  return fs_pio(vm, a, b, c, d, e, 1);
}

static u64 wasi_fd_seek2(WarmInstance *vm, u64 fd_, u64 off_, u64 whence_, u64 pn_) {
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;

  static const int W[3] = { 0 , 1 , 2  };
  u32 w = (u32)whence_;
  if (w > 2) return 28 ;
  /* a seek that leaves the offset where it is asks only for fd_tell */
  u64 need = (off_ == 0 && w == 1) ? R_FD_SEEK | R_FD_TELL : R_FD_SEEK;
  if (!(f->rights & need)) return WASI_ENOTCAPABLE;
  long long r = lseek(f->host, (long long)off_, W[w]);
  if (r < 0) return to_wasi_errno(h_errno);
  if (!put64(vm, (u32)pn_, (u64)r)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 wasi_fd_tell(WarmInstance *vm, u64 fd_, u64 pn_) {
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & R_FD_TELL)) return WASI_ENOTCAPABLE;
  long long r = lseek(f->host, 0, 1 );
  if (r < 0) return to_wasi_errno(h_errno);
  if (!put64(vm, (u32)pn_, (u64)r)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

#define WITH_LEAF(right, trim, body)                             \
  Fd *d = fd_get((u32)fd_);                                      \
  if (!d) return WASI_EBADF;                                     \
  if (d->type != FT_DIR) return WASI_ENOTDIR;                    \
  if (!(d->rights & (right))) return WASI_ENOTCAPABLE;           \
  char path[PATHMAX], leaf[PATHMAX];                             \
  u32 rc = copy_path(vm, p_, n_, path);                          \
  if (rc) return rc;                                             \
  if (trim) wasifs_trim_slash(path);                             \
  int dir;                                                       \
  rc = walk(d->host, path, &dir, leaf);                          \
  if (rc) return rc;                                             \
  const char *name = leaf[0] ? leaf : ".";                       \
  int r = (body);                                                \
  walk_done(d->host, dir);                                       \
  return r < 0 ? to_wasi_errno(h_errno) : WASI_ESUCCESS;

static u64 wasi_path_create_directory(WarmInstance *vm, u64 fd_, u64 p_, u64 n_) {
  WITH_LEAF(R_PATH_CREATE_DIRECTORY, 1, mkdirat(dir, name, 0777))
}

static u64 wasi_path_unlink_file(WarmInstance *vm, u64 fd_, u64 p_, u64 n_) {
  WITH_LEAF(R_PATH_UNLINK_FILE, 0, unlinkat(dir, name, 0))
}

static u64 wasi_path_remove_directory(WarmInstance *vm, u64 fd_, u64 p_, u64 n_) {
  WITH_LEAF(R_PATH_REMOVE_DIRECTORY, 0, unlinkat(dir, name, H_AT_REMOVEDIR))
}

#if defined(WARMC_WINDOWS)
/* Whether a link at `path` (under the preopen) to `target` would point out of
   the preopen: an absolute target, or one whose `..`s climb above it from the
   link's own directory.  Read lexically, as both are written. */
static int link_leaves(const char *path, const char *target) {
  if (target[0] == '/' || target[0] == '\\' || (target[0] && target[1] == ':'))
    return 1;
  int depth = 0, last = 0;
  for (const char *p = path; *p;) {
    const char *q = p;
    while (*q && *q != '/' && *q != '\\') q++;
    usz n = (usz)(q - p);
    if (n == 2 && p[0] == '.' && p[1] == '.') depth--;
    else if (n && !(n == 1 && p[0] == '.')) depth++;
    last = n && !(n == 1 && p[0] == '.') && !(n == 2 && p[0] == '.' && p[1] == '.');
    p = *q ? q + 1 : q;
  }
  if (last) depth--;
  for (const char *p = target; *p;) {
    const char *q = p;
    while (*q && *q != '/' && *q != '\\') q++;
    usz n = (usz)(q - p);
    if (n == 2 && p[0] == '.' && p[1] == '.') {
      if (--depth < 0) return 1;
    } else if (n && !(n == 1 && p[0] == '.')) {
      depth++;
    }
    p = *q ? q + 1 : q;
  }
  return 0;
}
#endif

static u64 wasi_path_symlink(WarmInstance *vm, u64 old_, u64 oldn_, u64 fd_,
                             u64 p_, u64 n_) {
  Fd *d = fd_get((u32)fd_);
  if (!d) return WASI_EBADF;
  if (d->type != FT_DIR) return WASI_ENOTDIR;
  if (!(d->rights & R_PATH_SYMLINK)) return WASI_ENOTCAPABLE;
  char target[PATHMAX], path[PATHMAX], leaf[PATHMAX];
  u32 rc = copy_path(vm, old_, oldn_, target);
  if (rc) return rc;
  rc = copy_path(vm, p_, n_, path);
  if (rc) return rc;
#if defined(WARMC_WINDOWS)
  /* Win32 has to be told when a link is made whether its target is a
     directory, and finding out for a target outside the preopen means
     looking outside it.  So such a link is refused here, as wasmtime refuses
     it on Windows; POSIX hosts make it and refuse to follow it instead. */
  if (link_leaves(path, target)) return WASI_EPERM;
#endif
  int dir;
  rc = walk(d->host, path, &dir, leaf);
  if (rc) return rc;

  int r = symlinkat(target, dir, leaf[0] ? leaf : ".");
  walk_done(d->host, dir);
  return r < 0 ? to_wasi_errno(h_errno) : WASI_ESUCCESS;
}

static u64 wasi_path_readlink(WarmInstance *vm, u64 fd_, u64 p_, u64 n_,
                              u64 buf_, u64 buflen_, u64 pn_) {
  Fd *d = fd_get((u32)fd_);
  if (!d) return WASI_EBADF;
  if (d->type != FT_DIR) return WASI_ENOTDIR;
  if (!(d->rights & R_PATH_READLINK)) return WASI_ENOTCAPABLE;
  char path[PATHMAX], leaf[PATHMAX];
  u32 rc = copy_path(vm, p_, n_, path);
  if (rc) return rc;
  int dir;
  rc = walk(d->host, path, &dir, leaf);
  if (rc) return rc;
  char link[PATHMAX];
  long r = readlinkat(dir, leaf[0] ? leaf : ".", link, PATHMAX - 1);
  walk_done(d->host, dir);
  if (r < 0) return to_wasi_errno(h_errno);
  u32 buf = (u32)buf_, buflen = (u32)buflen_;
  u32 take = (u32)r < buflen ? (u32)r : buflen;
  if (!ok(vm, buf, take)) return WASI_EFAULT;
  for (u32 i = 0; i < take; i++) vm->mem_base[buf + i] = (u8)link[i];
  if (!put32(vm, (u32)pn_, take)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 two_place(WarmInstance *vm, u64 sfd_, u64 sp_, u64 sn_, u64 dfd_,
                     u64 dp_, u64 dn_, u64 sright, u64 dright, int hard,
                     int follow) {
  Fd *s = fd_get((u32)sfd_), *t = fd_get((u32)dfd_);
  if (!s || !t) return WASI_EBADF;
  if (s->type != FT_DIR || t->type != FT_DIR) return WASI_ENOTDIR;
  if (!(s->rights & sright) || !(t->rights & dright)) return WASI_ENOTCAPABLE;
  char sp[PATHMAX], sl[PATHMAX], dp[PATHMAX], dl[PATHMAX];
  u32 rc = copy_path(vm, sp_, sn_, sp);
  if (rc) return rc;
  rc = copy_path(vm, dp_, dn_, dp);
  if (rc) return rc;
  /* a rename by a name that ends in a slash moves the entry itself, which
     must be a directory */
  int trailed = 0;
  if (!hard) {
    trailed |= wasifs_trim_slash(sp);
    trailed |= wasifs_trim_slash(dp);
  }
  int sdir, ddir;
  rc = walk(s->host, sp, &sdir, sl);
  if (rc) return rc;
  rc = walk(t->host, dp, &ddir, dl);
  if (rc) { walk_done(s->host, sdir); return rc; }
  if (trailed) {
    u8 st[ST_SIZE];
    rc = 0;
    if (fstatat(sdir, sl[0] ? sl : ".", st, H_AT_NOFOLLOW) < 0) rc = to_wasi_errno(h_errno);
    else if (st_filetype(st) != FT_DIR) rc = WASI_ENOTDIR;
    if (rc) { walk_done(s->host, sdir); walk_done(t->host, ddir); return rc; }
  }
  int r = hard ? linkat(sdir, sl[0] ? sl : ".", ddir, dl[0] ? dl : ".",
                        follow ? H_AT_SYMLINK_FOLLOW : 0)
               : renameat(sdir, sl[0] ? sl : ".", ddir, dl[0] ? dl : ".");
  walk_done(s->host, sdir);
  walk_done(t->host, ddir);
  return r < 0 ? to_wasi_errno(h_errno) : WASI_ESUCCESS;
}

static u64 wasi_path_rename(WarmInstance *vm, u64 a, u64 b, u64 c, u64 d,
                            u64 e, u64 f) {
  return two_place(vm, a, b, c, d, e, f, R_PATH_RENAME_SOURCE,
                   R_PATH_RENAME_TARGET, 0, 0);
}

static u64 wasi_path_link(WarmInstance *vm, u64 sfd, u64 flags, u64 sp, u64 sn,
                          u64 dfd, u64 dp, u64 dn) {
  /* Following a symlink at the source is refused, as wasmtime refuses it:
     two_place resolves every component but the leaf, and linkat with
     AT_SYMLINK_FOLLOW would follow a leaf link the guest made to anywhere
     -- out of the preopen included -- and hard-link what it names. */
  if ((u32)flags & 1) return WASI_EINVAL;
  return two_place(vm, sfd, sp, sn, dfd, dp, dn, R_PATH_LINK_SOURCE,
                   R_PATH_LINK_TARGET, 1, 0);
}

static u32 de_namlen(const u8 *de) { return wasifs_de_namlen(de); }

static u8 de_filetype(u8 dt) { return wasifs_de_filetype(dt); }

static u64 wasi_fd_readdir(WarmInstance *vm, u64 fd_, u64 buf_, u64 buflen_,
                           u64 cookie_, u64 pn_) {
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (f->type != FT_DIR) return WASI_ENOTDIR;
  if (!(f->rights & R_FD_READDIR)) return WASI_ENOTCAPABLE;
  u32 buf = (u32)buf_, buflen = (u32)buflen_;
  if (!ok(vm, buf, buflen)) return WASI_EFAULT;

  if (!f->dir) {

    int hd = dup(f->host);
    if (hd < 0) return to_wasi_errno(h_errno);
    f->dir = fdopendir(hd);
    if (!f->dir) { int e = h_errno; close(hd); return to_wasi_errno(e); }
    rewinddir(f->dir);
    f->dir_cookie = 0;
  }

  if (cookie_ < f->dir_cookie) { rewinddir(f->dir); f->dir_cookie = 0; }

  u64 seen = f->dir_cookie, written = 0;
  while (seen < cookie_) {
    if (!readdir(f->dir)) break;
    seen++;
  }
  f->dir_cookie = seen;

  for (;;) {

    long loc = telldir(f->dir);
    u8 *de = (u8 *)readdir(f->dir);
    if (!de) break;
    u32 namlen = de_namlen(de);

    u8 hdr[24];
    u64 next = seen + 1;
    __builtin_memcpy(hdr, &next, 8);
    u64 ino;
    __builtin_memcpy(&ino, de + DE_INO, 8);
    __builtin_memcpy(hdr + 8, &ino, 8);
    __builtin_memcpy(hdr + 16, &namlen, 4);
    hdr[20] = de_filetype(de[DE_TYPE]);
    hdr[21] = hdr[22] = hdr[23] = 0;

    int cut = (u64)namlen + 24 > (u64)buflen - written;
    for (u32 i = 0; i < 24 && written < buflen; i++)
      vm->mem_base[buf + written++] = hdr[i];
    for (u32 i = 0; i < namlen && written < buflen; i++)
      vm->mem_base[buf + written++] = de[DE_NAME + i];

    if (cut) { seekdir(f->dir, loc); break; }
    seen++;
    f->dir_cookie = seen;
    if (written >= buflen) break;
  }
  if (!put32(vm, (u32)pn_, (u32)written)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 wasi_fd_sync(WarmInstance *vm, u64 fd_) {
  (void)vm;
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & R_FD_SYNC)) return WASI_ENOTCAPABLE;
  return fsync(f->host) < 0 ? to_wasi_errno(h_errno) : WASI_ESUCCESS;
}

static u64 wasi_fd_datasync(WarmInstance *vm, u64 fd_) {
  (void)vm;
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;

  if (!(f->rights & R_FD_DATASYNC)) return WASI_ENOTCAPABLE;
  return host_fdatasync(f->host) < 0 ? to_wasi_errno(h_errno) : WASI_ESUCCESS;
}

static u64 wasi_fd_advise(WarmInstance *vm, u64 fd_, u64 off, u64 len, u64 adv) {
  (void)vm; (void)off; (void)len;
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & R_FD_ADVISE)) return WASI_ENOTCAPABLE;

  if ((u32)adv > 5) return WASI_EINVAL;

  return WASI_ESUCCESS;
}

static u64 wasi_fd_allocate(WarmInstance *vm, u64 fd_, u64 off_, u64 len_) {
  (void)vm;
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & R_FD_ALLOCATE)) return WASI_ENOTCAPABLE;

  u8 st[ST_SIZE];
  if (off_ + len_ < off_) return WASI_EOVERFLOW;
  if (fstat(f->host, st) < 0) return to_wasi_errno(h_errno);
  u64 want = off_ + len_;
  if (want > st_u64(st, ST_OFF_FSIZE))
    if (ftruncate(f->host, (long long)want) < 0) return to_wasi_errno(h_errno);
  return WASI_ESUCCESS;
}

static u64 wasi_fd_filestat_set_size(WarmInstance *vm, u64 fd_, u64 sz_) {
  (void)vm;
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & R_FD_FILESTAT_SET_SIZE)) return WASI_ENOTCAPABLE;
  return ftruncate(f->host, (long long)sz_) < 0 ? to_wasi_errno(h_errno)
                                                : WASI_ESUCCESS;
}

#if defined(WARMC_DARWIN)
#define UTIME_NOW  (-1)
#define UTIME_OMIT (-2)
#else
#define UTIME_NOW  ((1l << 30) - 1l)
#define UTIME_OMIT ((1l << 30) - 2l)
#endif

static void fill_times(long long *ts, u64 at, u64 mt, u32 flags) {
  if (flags & 2)      { ts[0] = 0; ts[1] = UTIME_NOW; }
  else if (flags & 1) { ts[0] = (long long)(at / 1000000000ull);
                        ts[1] = (long long)(at % 1000000000ull); }
  else                { ts[0] = 0; ts[1] = UTIME_OMIT; }
  if (flags & 8)      { ts[2] = 0; ts[3] = UTIME_NOW; }
  else if (flags & 4) { ts[2] = (long long)(mt / 1000000000ull);
                        ts[3] = (long long)(mt % 1000000000ull); }
  else                { ts[2] = 0; ts[3] = UTIME_OMIT; }
}

static u64 wasi_fd_filestat_set_times(WarmInstance *vm, u64 fd_, u64 at, u64 mt,
                                      u64 flags_) {
  (void)vm;
  Fd *f = fd_get((u32)fd_);
  if (!f) return WASI_EBADF;
  if (!(f->rights & R_FD_FILESTAT_SET_TIMES)) return WASI_ENOTCAPABLE;
  if (((u32)flags_ & 3) == 3 || ((u32)flags_ & 12) == 12) return 28 ;
  long long ts[4];
  fill_times(ts, at, mt, (u32)flags_);
  return futimens(f->host, ts) < 0 ? to_wasi_errno(h_errno) : WASI_ESUCCESS;
}

static u64 wasi_path_filestat_set_times(WarmInstance *vm, u64 fd_, u64 lf_,
                                        u64 p_, u64 n_, u64 at, u64 mt,
                                        u64 flags_) {
  Fd *d = fd_get((u32)fd_);
  if (!d) return WASI_EBADF;
  if (d->type != FT_DIR) return WASI_ENOTDIR;
  if (!(d->rights & R_PATH_FILESTAT_SET_TIMES)) return WASI_ENOTCAPABLE;
  if (((u32)flags_ & 3) == 3 || ((u32)flags_ & 12) == 12) return 28 ;
  char path[PATHMAX], leaf[PATHMAX];
  u32 rc = copy_path(vm, p_, n_, path);
  if (rc) return rc;
  int dir;
  rc = walk_leaf(d->host, path, &dir, leaf, ((u32)lf_ & 1) != 0);
  if (rc) return rc;
  long long ts[4];
  fill_times(ts, at, mt, (u32)flags_);
  int r = utimensat(dir, leaf[0] ? leaf : ".", ts, H_AT_NOFOLLOW);
  walk_done(d->host, dir);
  return r < 0 ? to_wasi_errno(h_errno) : WASI_ESUCCESS;
}

static u64 wasi_fd_renumber(WarmInstance *vm, u64 from_, u64 to_) {
  (void)vm;
  u32 a = (u32)from_, b = (u32)to_;
  /* both must be open: renumbering moves one descriptor over another */
  if (!fd_get(a) || !fd_get(b)) return WASI_EBADF;

  if (a == b) return WASI_ESUCCESS;
  if (g_fds[b].dir) closedir(g_fds[b].dir);
  if (g_fds[b].host >= 0 && !g_fds[b].keep_host) close(g_fds[b].host);
  g_fds[b] = g_fds[a];
  fd_clear(&g_fds[a]);
  return WASI_ESUCCESS;
}

extern int poll(void *fds, unsigned nfds, int timeout);

extern int nanosleep(const void *req, void *rem);

#define WASI_MAX_WAIT_NS (0x7fffffffull * 1000000ull)

#define SUB_SIZE 48
#define EV_SIZE  32

#define POLL_SMALL 32

typedef struct { int fd; short ev; short rev; } HPollFd;

typedef struct { u32 idx; u32 clock; u64 deadline; } ClockSub;

static u64 ns_to_ms_up(u64 ns) { return (ns + 999999ull) / 1000000ull; }

static void poll_emit(WarmInstance *vm, u32 out, u32 nout, const u8 *sub,
                      u8 type, u16 err, u16 evflags) {
  u8 *e = vm->mem_base + out + (u64)nout * EV_SIZE;
  for (int j = 0; j < EV_SIZE; j++) e[j] = 0;

  __builtin_memcpy(e, sub, 8);
  __builtin_memcpy(e + 8, &err, 2);
  e[10] = type;

  { u64 nb = 1; __builtin_memcpy(e + 16, &nb, 8); }
  __builtin_memcpy(e + 24, &evflags, 2);
}

static u64 wasi_poll_oneoff(WarmInstance *vm, u64 in_, u64 out_, u64 n_, u64 pn_) {
  u32 in = (u32)in_, out = (u32)out_, n = (u32)n_;
  if (!n) return 28 ;
  if (!ok(vm, in, (u64)n * SUB_SIZE) || !ok(vm, out, (u64)n * EV_SIZE))
    return WASI_EFAULT;

  HPollFd small_p[POLL_SMALL];
  ClockSub small_c[POLL_SMALL];
  u32 small_i[POLL_SMALL];
  HPollFd *pfd = small_p;
  ClockSub *cl = small_c;
  u32 *pi = small_i;
  void *heap = 0;
  if (n > POLL_SMALL) {
    heap = malloc((u64)n * (sizeof(HPollFd) + sizeof(ClockSub) + sizeof(u32)));
    if (!heap) return 48 ;
    pfd = (HPollFd *)heap;
    cl = (ClockSub *)(pfd + n);
    pi = (u32 *)(cl + n);
  }

  u32 npfd = 0, ncl = 0, nout = 0;
  u64 rc = WASI_ESUCCESS;
  u64 min_rel = 0;
  int have_rel = 0;

  for (u32 i = 0; i < n; i++) {
    const u8 *s = vm->mem_base + in + (u64)i * SUB_SIZE;
    u8 tag = s[8];
    if (tag == 0) {
      u32 clock_id;  __builtin_memcpy(&clock_id, s + 16, 4);
      u64 when;      __builtin_memcpy(&when, s + 24, 8);
      unsigned short flags; __builtin_memcpy(&flags, s + 40, 2);
      u64 now;

      if ((rc = clock_query(0, clock_id, &now))) goto done;

      u64 rel = (flags & 1) ? (when > now ? when - now : 0) : when;
      if (rel > WASI_MAX_WAIT_NS) rel = WASI_MAX_WAIT_NS;

      cl[ncl].idx = i;
      cl[ncl].clock = clock_id;
      cl[ncl].deadline = now + rel;
      ncl++;
      if (!have_rel || rel < min_rel) { min_rel = rel; have_rel = 1; }
    } else if (tag == 1 || tag == 2) {
      u32 gfd; __builtin_memcpy(&gfd, s + 16, 4);
      Fd *f = fd_get(gfd);
      if (f && !(f->rights & R_POLL_FD_READWRITE)) f = 0;
      if (!f) {

        poll_emit(vm, out, nout, s, tag, 8 , 0);
        nout++;
        continue;
      }
      pfd[npfd].fd = f->host;
      pfd[npfd].ev = (short)(tag == 1 ? 1  : 4 );
      pfd[npfd].rev = 0;
      pi[npfd] = i;
      npfd++;
    } else {
      rc = 28;
      goto done;
    }
  }

  /* an event already there -- a descriptor that cannot be polled -- ends
     the wait before it starts, as a ready one would */
  if (nout) { have_rel = 1; min_rel = 0; }
  long long timeout = have_rel ? (long long)ns_to_ms_up(min_rel) : -1;

  if (npfd) {
    for (;;) {
      int r = poll(pfd, npfd, (int)timeout);
      if (r >= 0) break;
      if (h_errno != 4 ) { rc = to_wasi_errno(h_errno); goto done; }

      if (!have_rel) continue;
      u64 rem = 0;
      for (u32 k = 0; k < ncl; k++) {
        u64 now = 0;
        clock_query(0, cl[k].clock, &now);
        u64 d = cl[k].deadline > now ? cl[k].deadline - now : 0;
        if (!k || d < rem) rem = d;
      }
      timeout = (long long)ns_to_ms_up(rem);
    }
  } else if (have_rel && min_rel) {
    struct { long tv_sec; long tv_nsec; } ts, rem;
    ts.tv_sec = (long)(min_rel / 1000000000ull);
    ts.tv_nsec = (long)(min_rel % 1000000000ull);
    while (nanosleep(&ts, &rem) < 0 && h_errno == 4 ) ts = rem;
  }

  for (u32 k = 0; k < npfd; k++) {
    if (!pfd[k].rev) continue;
    const u8 *s = vm->mem_base + in + (u64)pi[k] * SUB_SIZE;

    poll_emit(vm, out, nout, s, s[8], 0, (u16)((pfd[k].rev & 0x10 ) ? 1 : 0));
    nout++;
  }

  for (u32 k = 0; k < ncl; k++) {

    u64 now = 0;
    clock_query(0, cl[k].clock, &now);
    if (now < cl[k].deadline) continue;
    poll_emit(vm, out, nout, vm->mem_base + in + (u64)cl[k].idx * SUB_SIZE,
              0, 0, 0);
    nout++;
  }

  if (!nout && ncl) {

    u32 best = 0;
    for (u32 k = 1; k < ncl; k++)
      if (cl[k].deadline < cl[best].deadline) best = k;
    poll_emit(vm, out, nout, vm->mem_base + in + (u64)cl[best].idx * SUB_SIZE,
              0, 0, 0);
    nout++;
  }

  if (!put32(vm, (u32)pn_, nout)) rc = WASI_EFAULT;

done:
  if (heap) free(heap);
  return rc;
}

extern int socket(int, int, int);
extern int setsockopt(int, int, int, const void *, unsigned int);
extern int bind(int, const void *, unsigned int);
extern int listen(int, int);
extern int accept(int, void *, void *);
extern long recv(int, void *, usz, int);
extern long send(int, const void *, usz, int);
extern int shutdown(int, int);

#define H_AF_INET     2
#define H_SOCK_STREAM 1
#define WASI_ENOTSOCK 57

#if defined(WARMC_DARWIN)
#define H_SOL_SOCKET   0xffff
#define H_SO_REUSEADDR 0x0004
#define H_MSG_PEEK     0x0002
#define H_MSG_WAITALL  0x0040
#else
#define H_SOL_SOCKET   1
#define H_SO_REUSEADDR 2
#define H_MSG_PEEK     0x0002
#define H_MSG_WAITALL  0x0100
#endif

static Fd *sock_get(u64 fd_, u64 right, u64 *err) {
  Fd *f = fd_get((u32)fd_);
  if (!f) { *err = WASI_EBADF; return 0; }

  if (f->type != FT_SOCK_STREAM && f->type != FT_SOCK_DGRAM) {
    *err = WASI_ENOTSOCK;
    return 0;
  }
  if (!(f->rights & right)) { *err = WASI_ENOTCAPABLE; return 0; }
  return f;
}

static u64 wasi_sock_accept(WarmInstance *vm, u64 fd_, u64 flags_, u64 pfd_) {
  u64 err;
  Fd *f = sock_get(fd_, R_SOCK_ACCEPT, &err);
  if (!f) return err;

  int hostfd = accept(f->host, 0, 0);
  if (hostfd < 0) return to_wasi_errno(h_errno);

  u16 fdflags = (u16)((u32)flags_ & FDFLAG_ALL);
  if (fdflags & FDFLAG_NONBLOCK) {
    int cur = fcntl(hostfd, 3 , 0);
    if (cur >= 0) fcntl(hostfd, 4 , cur | H_NONBLOCK);
  }

  int slot = fd_alloc();
  if (slot < 0) { close(hostfd); return 33 ; }
  fd_clear(&g_fds[slot]);
  g_fds[slot].host = hostfd;
  g_fds[slot].type = FT_SOCK_STREAM;
  g_fds[slot].fdflags = fdflags;
  g_fds[slot].rights = RIGHTS_SOCKET;
  if (!put32(vm, (u32)pfd_, (u32)slot)) {
    close(hostfd);
    fd_clear(&g_fds[slot]);
    return WASI_EFAULT;
  }
  return WASI_ESUCCESS;
}

static u64 sock_io(WarmInstance *vm, Fd *f, u64 iovs_, u64 niov_, int mflags,
                   u64 pn_, int sending) {
  u32 iovs = (u32)iovs_, niov = (u32)niov_;
  u64 total = 0;
  for (u32 i = 0; i < niov; i++) {
    u64 rec = (u64)iovs + 8ull * i;
    u32 buf, len;
    if (!get32(vm, rec, &buf) || !get32(vm, rec + 4, &len)) return WASI_EFAULT;
    if (!ok(vm, buf, len)) return WASI_EFAULT;
    if (!len) continue;
    /* the count comes back in 32 bits: no more than they can say moves */
    if (len > IOV_MAX_TOTAL - total) len = (u32)(IOV_MAX_TOTAL - total);
    if (!len) break;
    long r = sending ? send(f->host, vm->mem_base + buf, len, mflags)
                     : recv(f->host, vm->mem_base + buf, len, mflags);

    if (r < 0) {
      if (total) break;
      return to_wasi_errno(h_errno);
    }
    total += (u64)r;

    if ((usz)r < len) break;
  }
  if (!put32(vm, (u32)pn_, (u32)total)) return WASI_EFAULT;
  return WASI_ESUCCESS;
}

static u64 wasi_sock_recv(WarmInstance *vm, u64 fd_, u64 iovs_, u64 niov_,
                          u64 riflags_, u64 pn_, u64 pflags_) {
  u64 err;
  Fd *f = sock_get(fd_, R_FD_READ, &err);
  if (!f) return err;

  u32 ri = (u32)riflags_;
  if (ri & ~3u) return WASI_EINVAL;
  int mf = 0;
  if (ri & 1) mf |= H_MSG_PEEK;
  if (ri & 2) mf |= H_MSG_WAITALL;

  u64 rc = sock_io(vm, f, iovs_, niov_, mf, pn_, 0);
  if (rc) return rc;

  if (!ok(vm, (u32)pflags_, 2)) return WASI_EFAULT;
  u16 ro = 0;
  __builtin_memcpy(vm->mem_base + (u32)pflags_, &ro, 2);
  return WASI_ESUCCESS;
}

static u64 wasi_sock_send(WarmInstance *vm, u64 fd_, u64 iovs_, u64 niov_,
                          u64 siflags_, u64 pn_) {
  u64 err;
  Fd *f = sock_get(fd_, R_FD_WRITE, &err);
  if (!f) return err;

  if ((u32)siflags_) return WASI_EINVAL;
  return sock_io(vm, f, iovs_, niov_, 0, pn_, 1);
}

static u64 wasi_sock_shutdown(WarmInstance *vm, u64 fd_, u64 how_) {
  (void)vm;
  u64 err;
  Fd *f = sock_get(fd_, R_SOCK_SHUTDOWN, &err);
  if (!f) return err;

  u32 how = (u32)how_;
  if (!how || (how & ~3u)) return WASI_EINVAL;
  int h = how == 1 ? 0  : how == 2 ? 1  : 2 ;
  return shutdown(f->host, h) < 0 ? to_wasi_errno(h_errno) : WASI_ESUCCESS;
}
static u64 wasi_proc_raise(WarmInstance *vm, u64 sig) {
  (void)vm; (void)sig;

  return 58 ;
}

static u64 wasi_fd_write(WarmInstance *vm, u64 fd, u64 iovs, u64 niov, u64 pn) {
  return fs_io(vm, fd, iovs, niov, pn, 1);
}

static u64 wasi_fd_read(WarmInstance *vm, u64 fd, u64 iovs, u64 niov, u64 pn) {
  return fs_io(vm, fd, iovs, niov, pn, 0);
}

static int add_preopen(const char *spec) {

  usz n = 0;
  while (spec[n] && !(spec[n] == ':' && spec[n + 1] == ':')) n++;

  if (!n || n >= PATHMAX) return 0;
  const char *guest = spec[n] ? spec + n + 2 : 0;

  char *host = (char *)malloc(n + 1);
  if (!host) return 0;
  for (usz i = 0; i < n; i++) host[i] = spec[i];
  host[n] = 0;

  int fd = openat(H_AT_FDCWD, host, H_RDONLY | H_DIRECTORY);
  if (fd < 0) { free(host); return 0; }
  int slot = fd_alloc();
  if (slot < 0) { close(fd); free(host); return 0; }

  fd_clear(&g_fds[slot]);
  g_fds[slot].host = fd;
  g_fds[slot].type = FT_DIR;
  g_fds[slot].preopen = 1;

  g_fds[slot].pre_name = guest && *guest ? guest : host;
  g_fds[slot].rights = RIGHTS_DIR;
  g_fds[slot].rights_inh = RIGHTS_DIR | RIGHTS_FILE;
  return 1;
}

static int add_listener(const char *spec) {

  u32 a[4] = { 127, 0, 0, 1 }, port = 0;
  const char *colon = 0;
  for (const char *q = spec; *q; q++) if (*q == ':') colon = q;

  if (colon && colon != spec) {
    u32 v = 0, part = 0;
    int any = 0;
    for (const char *q = spec; q < colon; q++) {
      if (*q >= '0' && *q <= '9') { v = v * 10 + (u32)(*q - '0'); any = 1; }
      else if (*q == '.') {
        if (part >= 3 || !any) return 0;
        a[part++] = v; v = 0; any = 0;
      } else return 0;
    }
    if (part != 3 || !any) return 0;
    a[3] = v;
  }
  for (int i = 0; i < 4; i++) if (a[i] > 255) return 0;

  const char *p = colon ? colon + 1 : spec;
  if (!*p) return 0;
  for (; *p; p++) {
    if (*p < '0' || *p > '9') return 0;
    port = port * 10 + (u32)(*p - '0');
    if (port > 65535) return 0;
  }

  int s = socket(H_AF_INET, H_SOCK_STREAM, 0);
  if (s < 0) return 0;
  int one = 1;
  setsockopt(s, H_SOL_SOCKET, H_SO_REUSEADDR, &one, sizeof one);

  u8 sa[16];
  for (int i = 0; i < 16; i++) sa[i] = 0;
#if defined(WARMC_DARWIN)

  sa[0] = 16;
  sa[1] = H_AF_INET;
#else
  sa[0] = H_AF_INET;
  sa[1] = 0;
#endif

  sa[2] = (u8)(port >> 8);
  sa[3] = (u8)port;
  for (int i = 0; i < 4; i++) sa[4 + i] = (u8)a[i];

  if (bind(s, sa, sizeof sa) < 0) { close(s); return 0; }
  if (listen(s, 128) < 0) { close(s); return 0; }

  int slot = fd_alloc();
  if (slot < 0) { close(s); return 0; }
  fd_clear(&g_fds[slot]);
  g_fds[slot].host = s;
  g_fds[slot].type = FT_SOCK_STREAM;
  g_fds[slot].rights = RIGHTS_LISTENER;
  return 1;
}

extern int isatty(int);

static int fds_init(void) {
  if (!fds_reserve(2)) return 0;
  for (u32 i = 0; i < 3; i++) {
    fd_clear(&g_fds[i]);
    g_fds[i].host = (int)i;

    g_fds[i].type = isatty((int)i) ? FT_CHAR : FT_UNKNOWN;
    g_fds[i].keep_host = 1;

    g_fds[i].rights = (i == 0 ? R_FD_READ : R_FD_WRITE) |
                      R_FD_FDSTAT_SET_FLAGS | R_POLL_FD_READWRITE |
                      R_FD_FILESTAT_GET;
  }
  return 1;
}

static const struct { const char *name; void *fn; } BINDINGS[] = {
  { "args_get",           (void *)wasi_args_get },
  { "args_sizes_get",     (void *)wasi_args_sizes_get },
  { "environ_get",        (void *)wasi_environ_get },
  { "environ_sizes_get",  (void *)wasi_environ_sizes_get },
  { "clock_res_get",      (void *)wasi_clock_res_get },
  { "clock_time_get",     (void *)wasi_clock_time_get },
  { "fd_close",           (void *)wasi_fd_close2 },
  { "fd_read",            (void *)wasi_fd_read },
  { "fd_seek",            (void *)wasi_fd_seek2 },
  { "fd_write",           (void *)wasi_fd_write },
  { "proc_exit",          (void *)wasi_proc_exit },
  { "random_get",         (void *)wasi_random_get },
  { "sched_yield",        (void *)wasi_sched_yield },
  { "fd_fdstat_get",      (void *)wasi_fd_fdstat_get2 },
  { "fd_fdstat_set_flags",(void *)wasi_fd_fdstat_set_flags },
  { "fd_fdstat_set_rights",(void *)wasi_fd_fdstat_set_rights },
  { "fd_prestat_get",     (void *)wasi_fd_prestat_get2 },
  { "fd_prestat_dir_name",(void *)wasi_fd_prestat_dir_name },
  { "fd_filestat_get",    (void *)wasi_fd_filestat_get },
  { "fd_filestat_set_size",(void *)wasi_fd_filestat_set_size },
  { "fd_filestat_set_times",(void *)wasi_fd_filestat_set_times },
  { "fd_pread",           (void *)wasi_fd_pread },
  { "fd_pwrite",          (void *)wasi_fd_pwrite },
  { "fd_readdir",         (void *)wasi_fd_readdir },
  { "fd_renumber",        (void *)wasi_fd_renumber },
  { "fd_sync",            (void *)wasi_fd_sync },
  { "fd_datasync",        (void *)wasi_fd_datasync },
  { "fd_advise",          (void *)wasi_fd_advise },
  { "fd_allocate",        (void *)wasi_fd_allocate },
  { "fd_tell",            (void *)wasi_fd_tell },
  { "path_open",          (void *)wasi_path_open },
  { "path_create_directory", (void *)wasi_path_create_directory },
  { "path_remove_directory", (void *)wasi_path_remove_directory },
  { "path_unlink_file",   (void *)wasi_path_unlink_file },
  { "path_rename",        (void *)wasi_path_rename },
  { "path_link",          (void *)wasi_path_link },
  { "path_symlink",       (void *)wasi_path_symlink },
  { "path_readlink",      (void *)wasi_path_readlink },
  { "path_filestat_get",  (void *)wasi_path_filestat_get },
  { "path_filestat_set_times", (void *)wasi_path_filestat_set_times },
  { "poll_oneoff",        (void *)wasi_poll_oneoff },
  { "proc_raise",         (void *)wasi_proc_raise },
  { "sock_accept",        (void *)wasi_sock_accept },
  { "sock_recv",          (void *)wasi_sock_recv },
  { "sock_send",          (void *)wasi_sock_send },
  { "sock_shutdown",      (void *)wasi_sock_shutdown },
};
#define NBINDINGS ((u32)(sizeof BINDINGS / sizeof BINDINGS[0]))

extern void (*signal(int, void (*)(int)))(int);

static int drive(int argc, char **argv) {
  if (!fds_init()) { err("warmc-wasi: out of memory\n"); return 74; }

  signal(13 , (void (*)(int))1 );

  int keep = 1;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (a[0] == '-' && a[1] == '-' && a[2] == 'd' && a[3] == 'i' &&
        a[4] == 'r' && a[5] == '=') {
      if (!add_preopen(a + 6)) { err("warmc-wasi: cannot preopen "); err(a + 6); err("\n"); return 74; }
      continue;
    }
    if (str_eq(a, "--dir") && i + 1 < argc) {
      if (!add_preopen(argv[++i])) { err("warmc-wasi: cannot preopen "); err(argv[i]); err("\n"); return 74; }
      continue;
    }
    if (a[0] == '-' && a[1] == '-' && a[2] == 'e' && a[3] == 'n' &&
        a[4] == 'v' && a[5] == '=') {
      if (!add_env((char *)a + 6)) { err("warmc-wasi: out of memory\n"); return 74; }
      continue;
    }
    if (str_eq(a, "--env") && i + 1 < argc) {
      if (!add_env(argv[++i])) { err("warmc-wasi: out of memory\n"); return 74; }
      continue;
    }
    if (a[0] == '-' && a[1] == '-' && a[2] == 't' && a[3] == 'c' &&
        a[4] == 'p' && a[5] == 'l' && a[6] == 'i' && a[7] == 's' &&
        a[8] == 't' && a[9] == 'e' && a[10] == 'n' && a[11] == '=') {
      if (!add_listener(a + 12)) {
        err("warmc-wasi: cannot listen on "); err(a + 12); err("\n");
        return 74;
      }
      continue;
    }
    if (str_eq(a, "--tcplisten") && i + 1 < argc) {
      if (!add_listener(argv[++i])) {
        err("warmc-wasi: cannot listen on "); err(argv[i]); err("\n");
        return 74;
      }
      continue;
    }
    if (str_eq(a, "--inherit-env")) { g_env_inherit = 1; continue; }
    if (str_eq(a, "--")) {
      for (int j = i + 1; j < argc; j++) argv[keep++] = argv[j];
      break;
    }
    argv[keep++] = argv[i];
  }
  argc = keep;

  g_argc = argc;
  g_argv = argv;
  seed_init();

  u32 e = 0;
  WarmInstance *stub = warmc_instantiate_catching(&warmc_wasi_stub_module, &e);
  if (!stub) {
    err("warmc-wasi: the import stub failed to instantiate: ");
    err(warmc_trap_name(e));
    err("\n");
    return 70;
  }
  warmc_register("wasi_snapshot_preview1", stub);

  /* WASI's functions bound as the module is made, so that its start
     function can call them; one this host does not have is the stub's,
     which traps */
  const ModuleDesc *md = &warmc_module;
  WarmBinding *binds = md->nimports ? realloc(0, sizeof(WarmBinding) * md->nimports) : 0;
  for (u32 i = 0; i < md->nimports; i++) {
    const ImportDesc *im = &md->imports[i];
    binds[i] = (WarmBinding){ 0, 0, 0, 0 };
    if (im->kind != 0 || im->module_len != 22 ||
        !bytes_eq(im->module, "wasi_snapshot_preview1", 22))
      continue;
    for (u32 k = 0; k < NBINDINGS; k++)
      if (im->name_len == cstrlen(BINDINGS[k].name) &&
          bytes_eq(im->name, BINDINGS[k].name, im->name_len)) {
        binds[i].code = BINDINGS[k].fn;
        binds[i].self = 1;
        break;
      }
  }
  WarmInstance *app = warmc_instantiate_bound_catching(md, binds, &e);
  free(binds);
  if (!app) {

    err("warmc-wasi: the module failed to instantiate or link: ");
    err(warmc_trap_name(e));
    err(" ("); err_u64(e); err(")\n");
    return 71;
  }

  const ExportDesc *ex = warmc_find_export(app, "_start");
  if (!ex) {
    err("warmc-wasi: the module exports no _start; it is not a WASI command\n");
    return 72;
  }
  FuncRef fr = warmc_export_func(app, ex);
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  u32 trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance,
                                   iargs, fargs, ret);
  if (trap) {

    err("warmc-wasi: trap: ");
    err(warmc_trap_name(trap));
    err("\n");
    return 73;
  }

  warmc_report_run(app);
  return 0;
}

int (*warmc_driver_hook)(int argc, char **argv) = drive;
