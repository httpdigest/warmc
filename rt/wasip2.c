#include "wasifs.h"

extern void *malloc(usz);
extern void free(void *);
extern void (*signal(int, void (*)(int)))(int);
extern int isatty(int);

#define RT_ERROR         0
#define RT_POLLABLE      1
#define RT_INPUT_STREAM  2
#define RT_OUTPUT_STREAM 3
#define RT_DESCRIPTOR    4
#define RT_DIRSTREAM     5
#define RT_TERM_INPUT    6
#define RT_TERM_OUTPUT   7

#define EC_ACCESS        0
#define EC_BADF          3
#define EC_EXIST         7
#define EC_INVALID      12
#define EC_IO           13
#define EC_ISDIR        14
#define EC_LOOP         15
#define EC_NAMETOOLONG  18
#define EC_NOENT        20
#define EC_NOMEM        22
#define EC_NOSPC        23
#define EC_NOTDIR       24
#define EC_NOTEMPTY     25
#define EC_UNSUPPORTED  27
#define EC_OVERFLOW     30
#define EC_PERM         31
#define EC_PIPE         32
#define EC_ROFS         33
#define EC_SPIPE        34
#define EC_XDEV         36

/* A stream is either one of the three standard descriptors, where reading and
   writing are the syscalls of those names and there is no position to keep,
   or a view onto a file taken with read/write/append-via-stream, where there
   is: preview2 has no seek, so the offset a stream was opened at and the
   bytes it has moved since are the whole of where it is.  Rust's std reaches
   files this way and never calls descriptor.read or descriptor.write, which
   is how tinygo reaches them. */
typedef struct {
  int fd;
  u8 input;
  u8 no_close;
  u8 seekable;
  u8 append;
  u64 pos;
} Stream;

static int g_argc;
static char **g_argv;
static int g_env_inherit;
static char **g_env;
static u32 g_nenv, g_env_cap;

static void err(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(2, s, (usz)(p - s));
}

static int str_eq(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

static usz cstrlen(const char *s) {
  const char *p = s;
  while (*p) p++;
  return (usz)(p - s);
}

static int ok(WarmInstance *vm, u64 off, u64 len) {
  return vm && off + len >= off && off + len <= vm->mem_size;
}

static int put8(WarmInstance *vm, u64 a, u8 v) {
  if (!ok(vm, a, 1)) return 0;
  vm->mem_base[a] = (char)v;
  return 1;
}

static int put32(WarmInstance *vm, u64 a, u32 v) {
  if (!ok(vm, a, 4)) return 0;
  __builtin_memcpy(vm->mem_base + a, &v, 4);
  return 1;
}

static int put64(WarmInstance *vm, u64 a, u64 v) {
  if (!ok(vm, a, 8)) return 0;
  __builtin_memcpy(vm->mem_base + a, &v, 8);
  return 1;
}

static WarmInstance *g_alloc_vm;
static FuncRef g_alloc_fn;

static u64 guest_alloc(WarmInstance *vm, u32 align, u32 size) {
  if (!size) return 0;
  if (vm != g_alloc_vm) {
    const ExportDesc *e = warmc_find_export(vm, "cabi_realloc");
    if (!e) {
      err("warmc-wasip2: the component exports no cabi_realloc, so a list "
          "cannot be returned to it\n");
      warmc_trap(TRAP_UNREACHABLE);
    }
    g_alloc_fn = warmc_export_func(vm, e);
    g_alloc_vm = vm;
  }
  u64 iargs[8] = { 0, 0, align, size };
  u64 fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  u32 trap = warmc_invoke_catching(g_alloc_fn.code,
                                   (WarmInstance *)g_alloc_fn.instance,
                                   iargs, fargs, ret);
  if (trap) warmc_trap(trap);
  u64 p = ret[0] & 0xffffffffu;
  if (!ok(vm, p, size)) warmc_trap(TRAP_MEM_OOB);
  return p;
}

static u64 put_bytes(WarmInstance *vm, const void *src, u64 n) {
  u64 p = guest_alloc(vm, 1, (u32)n);
  /* A loop, not __builtin_memcpy: a length known only at run time lowers
     to a call to memcpy, and a Linux link of this host has no libc. */
  const u8 *s = src;
  for (u64 i = 0; i < n; i++) vm->mem_base[p + i] = (char)s[i];
  return p;
}

static void put_string(WarmInstance *vm, u64 at, const char *s, u64 n) {
  put32(vm, at, (u32)put_bytes(vm, s, n));
  put32(vm, at + 4, (u32)n);
}

static Stream *stream_of(u32 rt, u64 h) {
  Stream *s = (Stream *)(uptr)warmc_res_lift_borrow(rt, (u32)h);
  warmc_res_release(rt, (u32)h);
  return s;
}

static Stream *stream_make(int fd, u8 input, u8 no_close, u8 seekable,
                           u8 append, u64 pos) {
  Stream *s = malloc(sizeof *s);
  if (!s) warmc_trap(TRAP_HEAP_OOM);
  s->fd = fd;
  s->input = input;
  s->no_close = no_close;
  s->seekable = seekable;
  s->append = append;
  s->pos = pos;
  return s;
}

static u64 stdio_handle(int fd, u8 input) {
  return warmc_res_lower_own(input ? RT_INPUT_STREAM : RT_OUTPUT_STREAM,
                             (u64)(uptr)stream_make(fd, input, 1, 0, 0, 0));
}

static void stream_ok(WarmInstance *vm, u64 ret) { put8(vm, ret, 0); }

static void stream_closed(WarmInstance *vm, u64 ret) {
  put8(vm, ret, 1);
  put8(vm, ret + 4, 1);
}

static void stream_failed(WarmInstance *vm, u64 ret, int e) {
  put8(vm, ret, 1);
  put8(vm, ret + 4, 0);
  put32(vm, ret + 8, warmc_res_lower_own(RT_ERROR, (u64)(unsigned)e));
}

static u64 p2_exit(WarmInstance *vm, u64 status) {
  warmc_rt_exit((status & 1) ? 1 : 0);
  return 0;
}

/* exit-with-code carries the whole u8 status; rustc's std calls it for any
   std::process::exit code other than 0 and 1. */
static u64 p2_exit_with_code(WarmInstance *vm, u64 code) {
  warmc_rt_exit((int)(code & 0xff));
  return 0;
}

static u64 p2_get_stdin(WarmInstance *vm) { return stdio_handle(0, 1); }
static u64 p2_get_stdout(WarmInstance *vm) { return stdio_handle(1, 0); }
static u64 p2_get_stderr(WarmInstance *vm) { return stdio_handle(2, 0); }

static u64 p2_stream_drop(WarmInstance *vm, u64 h, u32 rt) {
  u32 owned = 0;
  Stream *s = (Stream *)(uptr)warmc_res_drop(rt, (u32)h, &owned);
  if (s && !s->no_close) close(s->fd);
  free(s);
  return 0;
}

static u64 p2_drop_input(WarmInstance *vm, u64 h) {
  return p2_stream_drop(vm, h, RT_INPUT_STREAM);
}

static u64 p2_drop_output(WarmInstance *vm, u64 h) {
  return p2_stream_drop(vm, h, RT_OUTPUT_STREAM);
}

static u64 p2_drop_error(WarmInstance *vm, u64 h) {
  u32 owned = 0;
  warmc_res_drop(RT_ERROR, (u32)h, &owned);
  return 0;
}

static u64 p2_write_and_flush(WarmInstance *vm, u64 self, u64 ptr, u64 len,
                              u64 ret) {
  Stream *s = stream_of(RT_OUTPUT_STREAM, self);
  ptr &= 0xffffffffu;
  len &= 0xffffffffu;
  if (!ok(vm, ptr, len)) { stream_failed(vm, ret, EC_INVALID); return 0; }

  u64 done = 0;
  while (done < len) {
    long r;
    if (s->seekable && !s->append)
      r = pwrite(s->fd, vm->mem_base + ptr + done, (usz)(len - done),
                 (long long)(s->pos + done));
    else
      r = warmc_rt_write(s->fd, vm->mem_base + ptr + done,
                         (usz)(len - done));
    if (r < 0) {
      if (-r == 32) stream_closed(vm, ret);
      else stream_failed(vm, ret, (int)-r);
      return 0;
    }
    if (!r) break;
    done += (u64)r;
  }
  s->pos += done;
  stream_ok(vm, ret);
  return 0;
}

/* The non-blocking half of the same path.  A guest that writes through
   check-write/write/flush rather than blocking-write-and-flush -- which is
   what Rust's std does -- never touches the call above.  The permit is a
   bound the guest must not exceed, not a promise about what one write will
   take, so answering a fixed 4096 is honest for a host whose writes go
   straight to a descriptor. */
static u64 p2_stream_write(WarmInstance *vm, u64 self, u64 ptr, u64 len,
                           u64 ret) {
  return p2_write_and_flush(vm, self, ptr, len, ret);
}

static u64 p2_check_write(WarmInstance *vm, u64 self, u64 ret) {
  stream_of(RT_OUTPUT_STREAM, self);
  put8(vm, ret, 0);
  put64(vm, ret + 8, 4096);
  return 0;
}

static u64 p2_get_terminal(WarmInstance *vm, u64 ret, int fd, u32 rt) {
  if (!isatty(fd)) { put8(vm, ret, 0); return 0; }
  put8(vm, ret, 1);
  put32(vm, ret + 4, warmc_res_lower_own(rt, (u64)(unsigned)fd));
  return 0;
}

static u64 p2_terminal_stdin(WarmInstance *vm, u64 ret) {
  return p2_get_terminal(vm, ret, 0, RT_TERM_INPUT);
}
static u64 p2_terminal_stdout(WarmInstance *vm, u64 ret) {
  return p2_get_terminal(vm, ret, 1, RT_TERM_OUTPUT);
}
static u64 p2_terminal_stderr(WarmInstance *vm, u64 ret) {
  return p2_get_terminal(vm, ret, 2, RT_TERM_OUTPUT);
}

static u64 p2_drop_term_in(WarmInstance *vm, u64 h) {
  u32 owned = 0;
  warmc_res_drop(RT_TERM_INPUT, (u32)h, &owned);
  return 0;
}

static u64 p2_drop_term_out(WarmInstance *vm, u64 h) {
  u32 owned = 0;
  warmc_res_drop(RT_TERM_OUTPUT, (u32)h, &owned);
  return 0;
}

static u64 p2_flush(WarmInstance *vm, u64 self, u64 ret) {
  stream_of(RT_OUTPUT_STREAM, self);
  stream_ok(vm, ret);
  return 0;
}

static u64 p2_blocking_read(WarmInstance *vm, u64 self, u64 len, u64 ret) {
  Stream *s = stream_of(RT_INPUT_STREAM, self);
  if (len > (1u << 20)) len = 1u << 20;

  char *buf = len ? malloc((usz)len) : 0;
  if (len && !buf) { stream_failed(vm, ret, EC_NOMEM); return 0; }
  long r = 0;
  if (len) {
    if (s->seekable)
      r = pread(s->fd, buf, (usz)len, (long long)s->pos);
    else
      r = warmc_rt_read(s->fd, buf, (usz)len);
  }
  if (r < 0) { free(buf); stream_failed(vm, ret, (int)-r); return 0; }
  if (!r && len) { free(buf); stream_closed(vm, ret); return 0; }
  s->pos += (u64)r;

  put8(vm, ret, 0);
  u64 p = put_bytes(vm, buf, (u64)r);
  free(buf);
  put32(vm, ret + 4, (u32)p);
  put32(vm, ret + 8, (u32)r);
  return 0;
}

static u64 p2_random_u64(WarmInstance *vm) {
  u64 v = 0;
  warmc_rt_random(&v, sizeof v);
  return v;
}

static u64 p2_random_bytes(WarmInstance *vm, u64 len, u64 ret) {
  if (len > (1u << 20)) len = 1u << 20;
  u64 p = guest_alloc(vm, 1, (u32)len);
  if (len) warmc_rt_random(vm->mem_base + p, (usz)len);
  put32(vm, ret, (u32)p);
  put32(vm, ret + 4, (u32)len);
  return 0;
}

extern int poll(void *fds, unsigned nfds, int timeout);
extern int nanosleep(const void *req, void *rem);

#define POLL_TIMER 0
#define POLL_READ  1
#define POLL_WRITE 2

typedef struct {
  u8 kind;
  int fd;
  u64 deadline;
} Pollable;

typedef struct { int fd; short ev; short rev; } HPollFd;

static u64 pollable_new(u8 kind, int fd, u64 deadline) {
  Pollable *p = malloc(sizeof *p);
  if (!p) warmc_trap(TRAP_HEAP_OOM);
  p->kind = kind;
  p->fd = fd;
  p->deadline = deadline;
  return warmc_res_lower_own(RT_POLLABLE, (u64)(uptr)p);
}

static int pollable_ready(const Pollable *p) {
  if (p->kind == POLL_TIMER) return warmc_rt_nanos(0) >= p->deadline;
  HPollFd f;
  f.fd = p->fd;
  f.ev = p->kind == POLL_READ ? 1 : 4;
  f.rev = 0;
  return poll(&f, 1, 0) > 0;
}

static void sleep_ns(u64 ns) {
  long ts[2];
  ts[0] = (long)(ns / 1000000000ull);
  ts[1] = (long)(ns % 1000000000ull);
  long rem[2];
  while (nanosleep(ts, rem) < 0 && h_errno == 4) { ts[0] = rem[0]; ts[1] = rem[1]; }
}

static void pollable_block(const Pollable *p) {
  if (p->kind == POLL_TIMER) {
    u64 now = warmc_rt_nanos(0);
    if (now < p->deadline) sleep_ns(p->deadline - now);
    return;
  }
  HPollFd f;
  f.fd = p->fd;
  f.ev = p->kind == POLL_READ ? 1 : 4;
  f.rev = 0;
  while (poll(&f, 1, -1) < 0 && h_errno == 4) f.rev = 0;
}

static u64 p2_subscribe_duration(WarmInstance *vm, u64 ns) {
  return pollable_new(POLL_TIMER, -1, warmc_rt_nanos(0) + ns);
}

static u64 p2_subscribe_instant(WarmInstance *vm, u64 when) {
  return pollable_new(POLL_TIMER, -1, when);
}

static u64 p2_stream_subscribe(WarmInstance *vm, u64 self, u32 rt) {
  Stream *s = stream_of(rt, self);
  return pollable_new(rt == RT_INPUT_STREAM ? POLL_READ : POLL_WRITE, s->fd, 0);
}

static u64 p2_input_subscribe(WarmInstance *vm, u64 self) {
  return p2_stream_subscribe(vm, self, RT_INPUT_STREAM);
}

static u64 p2_output_subscribe(WarmInstance *vm, u64 self) {
  return p2_stream_subscribe(vm, self, RT_OUTPUT_STREAM);
}

static u64 p2_pollable_ready(WarmInstance *vm, u64 self) {
  Pollable *p =
      (Pollable *)(uptr)warmc_res_lift_borrow(RT_POLLABLE, (u32)self);
  warmc_res_release(RT_POLLABLE, (u32)self);
  return (u64)(pollable_ready(p) != 0);
}

static u64 p2_pollable_block(WarmInstance *vm, u64 self) {
  Pollable *p =
      (Pollable *)(uptr)warmc_res_lift_borrow(RT_POLLABLE, (u32)self);
  warmc_res_release(RT_POLLABLE, (u32)self);
  pollable_block(p);
  return 0;
}

static u64 p2_drop_pollable(WarmInstance *vm, u64 h) {
  u32 owned = 0;
  free((void *)(uptr)warmc_res_drop(RT_POLLABLE, (u32)h, &owned));
  return 0;
}

static u64 p2_poll(WarmInstance *vm, u64 ptr, u64 len, u64 ret) {
  ptr &= 0xffffffffu;
  len &= 0xffffffffu;
  if (!len || !ok(vm, ptr, len * 4)) {
    put32(vm, ret, 0);
    put32(vm, ret + 4, 0);
    return 0;
  }

  Pollable **ps = malloc(sizeof(Pollable *) * (usz)len);
  u32 *hit = malloc(sizeof(u32) * (usz)len);
  if (!ps || !hit) { free(ps); free(hit); warmc_trap(TRAP_HEAP_OOM); }
  for (u64 i = 0; i < len; i++) {
    u32 h = 0;
    __builtin_memcpy(&h, vm->mem_base + ptr + i * 4, 4);
    ps[i] = (Pollable *)(uptr)warmc_res_lift_borrow(RT_POLLABLE, h);
    warmc_res_release(RT_POLLABLE, h);
  }

  u32 n = 0;
  for (;;) {
    u64 now = warmc_rt_nanos(0);
    u64 soonest = 0;
    int have_timer = 0;
    HPollFd fds[64];
    u32 nfd = 0, idx[64];
    for (u64 i = 0; i < len; i++) {
      if (ps[i]->kind == POLL_TIMER) {
        if (now >= ps[i]->deadline) hit[n++] = (u32)i;
        else if (!have_timer || ps[i]->deadline < soonest) {
          soonest = ps[i]->deadline;
          have_timer = 1;
        }
      } else if (nfd < 64) {
        fds[nfd].fd = ps[i]->fd;
        fds[nfd].ev = ps[i]->kind == POLL_READ ? 1 : 4;
        fds[nfd].rev = 0;
        idx[nfd] = (u32)i;
        nfd++;
      }
    }
    if (nfd) {
      int ms = n ? 0 : (have_timer ? (int)((soonest - now + 999999ull) / 1000000ull)
                                   : -1);
      int r = poll(fds, nfd, ms);
      if (r < 0 && h_errno == 4) continue;
      for (u32 k = 0; k < nfd && r > 0; k++)
        if (fds[k].rev) hit[n++] = idx[k];
    }
    if (n) break;
    if (!have_timer) break;
    sleep_ns(soonest - now);
  }

  u64 out = guest_alloc(vm, 4, n * 4);
  for (u32 i = 0; i < n; i++) put32(vm, out + (u64)i * 4, hit[i]);
  put32(vm, ret, (u32)out);
  put32(vm, ret + 4, n);
  free(ps);
  free(hit);
  return 0;
}

static u64 p2_monotonic_now(WarmInstance *vm) { return warmc_rt_nanos(0); }

static u64 p2_wall_now(WarmInstance *vm, u64 ret) {
  u64 ns = warmc_rt_nanos(1);
  put64(vm, ret, ns / 1000000000ull);
  put32(vm, ret + 8, (u32)(ns % 1000000000ull));
  return 0;
}

static u64 p2_get_arguments(WarmInstance *vm, u64 ret) {
  u32 n = (u32)g_argc;
  u64 arr = guest_alloc(vm, 4, n * 8);
  for (u32 i = 0; i < n; i++)
    put_string(vm, arr + (u64)i * 8, g_argv[i], cstrlen(g_argv[i]));
  put32(vm, ret, (u32)arr);
  put32(vm, ret + 4, n);
  return 0;
}

static u64 p2_get_environment(WarmInstance *vm, u64 ret) {
  char **src = g_env_inherit ? warmc_rt_environ() : g_env;
  u32 n = 0;
  if (g_env_inherit) { while (src && src[n]) n++; }
  else n = g_nenv;

  u64 arr = guest_alloc(vm, 4, n * 16);
  for (u32 i = 0; i < n; i++) {
    const char *e = src[i];
    usz k = 0;
    while (e[k] && e[k] != '=') k++;
    put_string(vm, arr + (u64)i * 16, e, k);
    const char *v = e[k] ? e + k + 1 : e + k;
    put_string(vm, arr + (u64)i * 16 + 8, v, cstrlen(v));
  }
  put32(vm, ret, (u32)arr);
  put32(vm, ret + 4, n);
  return 0;
}

static u64 p2_initial_cwd(WarmInstance *vm, u64 ret) {
  put8(vm, ret, 0);
  return 0;
}

typedef struct {
  int fd;
  u8 preopen;
  const char *guest;
} Desc;

typedef struct {
  void *dir;
} DirStream;

static u32 g_pre[64];
static u32 g_npre;

static u32 ec_of_p1(u32 e) {
  switch (e) {
    case 2:  return 0;
    case 6:  return 1;
    case 7:  return 2;
    case 8:  return EC_BADF;
    case 10: return 4;
    case 16: return 5;
    case 19: return 6;
    case 20: return EC_EXIST;
    case 22: return 8;
    case 25: return 9;
    case 26: return 10;
    case 27: return 11;
    case 21: case 28: return EC_INVALID;
    case 29: return EC_IO;
    case 31: return EC_ISDIR;
    case 32: return EC_LOOP;
    case 34: return 16;
    case 35: return 17;
    case 37: return EC_NAMETOOLONG;
    case 43: return 19;
    case 44: return EC_NOENT;
    case 46: return 21;
    case 33: case 41: case 48: return EC_NOMEM;
    case 51: return EC_NOSPC;
    case 54: return EC_NOTDIR;
    case 55: return EC_NOTEMPTY;
    case 56: return 26;
    case 58: return EC_UNSUPPORTED;
    case 59: return 28;
    case 60: return 29;
    case 61: return EC_OVERFLOW;
    case 63: return EC_PERM;
    case 64: return EC_PIPE;
    case 69: return EC_ROFS;
    case 70: return EC_SPIPE;
    case 74: return 35;
    case 75: return EC_XDEV;
    default: return EC_IO;
  }
}

static u32 ec_of_host(int e) { return ec_of_p1(wasifs_errno(e)); }

static u8 p2_filetype(u8 ft) {
  switch (ft) {
    case FT_BLOCK:   return 1;
    case FT_CHAR:    return 2;
    case FT_DIR:     return 3;
    case FT_REGULAR: return 6;
    case FT_SYMLINK: return 5;
    default:         return 0;
  }
}

static Desc *desc_of(u64 h) {
  Desc *d = (Desc *)(uptr)warmc_res_lift_borrow(RT_DESCRIPTOR, (u32)h);
  warmc_res_release(RT_DESCRIPTOR, (u32)h);
  return d;
}

static void res_err(WarmInstance *vm, u64 ret, u32 payload_off, u32 ec) {
  put8(vm, ret, 1);
  put8(vm, ret + payload_off, (u8)ec);
}

static u32 read_path(WarmInstance *vm, u64 p, u64 n, char *out) {
  p &= 0xffffffffu;
  n &= 0xffffffffu;
  if (!ok(vm, p, n)) return EC_INVALID;
  if (n >= PATHMAX) return EC_NAMETOOLONG;
  for (u64 i = 0; i < n; i++) {
    char c = vm->mem_base[p + i];
    if (!c) return EC_INVALID;
    out[i] = c;
  }
  out[n] = 0;
  return 0;
}

static u32 at_leaf(Desc *d, WarmInstance *vm, u64 p, u64 n, int follow,
                   int *dir, char *leaf) {
  char path[PATHMAX];
  u32 ec = read_path(vm, p, n, path);
  if (ec) return ec;
  u32 rc = wasifs_walk_leaf(d->fd, path, dir, leaf, follow);
  if (rc) return ec_of_p1(rc);
  if (!leaf[0]) leaf[0] = '.', leaf[1] = 0;
  return 0;
}

static u64 p2_get_directories(WarmInstance *vm, u64 ret) {
  u64 arr = guest_alloc(vm, 4, g_npre * 12);
  for (u32 i = 0; i < g_npre; i++) {
    Desc *d = (Desc *)(uptr)warmc_res_rep(RT_DESCRIPTOR, g_pre[i]);
    put32(vm, arr + (u64)i * 12, warmc_res_lower_own(RT_DESCRIPTOR,
                                                     (u64)(uptr)d));
    put_string(vm, arr + (u64)i * 12 + 4, d->guest, cstrlen(d->guest));
  }
  put32(vm, ret, (u32)arr);
  put32(vm, ret + 4, g_npre);
  return 0;
}

static void put_stat(WarmInstance *vm, u64 at, const u8 *st) {
  put8(vm, at, p2_filetype(wasifs_filetype(st)));
  put64(vm, at + 8, wasifs_st_small(st, ST_OFF_NLINK));
  put64(vm, at + 16, wasifs_st_u64(st, ST_OFF_FSIZE));
  static const int off[3] = { ST_OFF_ATIME, ST_OFF_MTIME, ST_OFF_CTIME };
  for (int i = 0; i < 3; i++) {
    u64 slot = at + 24 + (u64)i * 24;
    u64 ns = wasifs_st_time(st, off[i]);
    put8(vm, slot, 1);
    put64(vm, slot + 8, ns / 1000000000ull);
    put32(vm, slot + 16, (u32)(ns % 1000000000ull));
  }
}

static u64 p2_stat(WarmInstance *vm, u64 self, u64 ret) {
  Desc *d = desc_of(self);
  u8 st[ST_SIZE];
  if (fstat(d->fd, st) < 0) { res_err(vm, ret, 8, ec_of_host(h_errno)); return 0; }
  put8(vm, ret, 0);
  put_stat(vm, ret + 8, st);
  return 0;
}

static u64 p2_stat_at(WarmInstance *vm, u64 self, u64 flags, u64 p, u64 n,
                      u64 ret) {
  Desc *d = desc_of(self);
  int dir = -1;
  char leaf[PATHMAX];
  u32 ec = at_leaf(d, vm, p, n, (int)(flags & 1), &dir, leaf);
  if (ec) { res_err(vm, ret, 8, ec); return 0; }

  u8 st[ST_SIZE];
  int r = fstatat(dir, leaf, st, (flags & 1) ? 0 : H_AT_NOFOLLOW);
  int e = h_errno;
  wasifs_walk_done(d->fd, dir);
  if (r < 0) { res_err(vm, ret, 8, ec_of_host(e)); return 0; }
  put8(vm, ret, 0);
  put_stat(vm, ret + 8, st);
  return 0;
}

static u64 p2_open_at(WarmInstance *vm, u64 self, u64 pflags, u64 p, u64 n,
                      u64 oflags, u64 dflags, u64 ret) {
  Desc *d = desc_of(self);
  int dir = -1;
  char leaf[PATHMAX];
  u32 ec = at_leaf(d, vm, p, n, (int)(pflags & 1), &dir, leaf);
  if (ec) { res_err(vm, ret, 4, ec); return 0; }

  int hf = 0;
  if ((dflags & 3) == 3) hf |= H_RDWR;
  else if (dflags & 2) hf |= H_WRONLY;
  else hf |= H_RDONLY;
  if (dflags & (4 | 8 | 16)) hf |= H_SYNC;
  if (oflags & 1) hf |= H_CREAT;
  if (oflags & 2) hf |= H_DIRECTORY;
  if (oflags & 4) hf |= H_EXCL;
  if (oflags & 8) hf |= H_TRUNC;
  if (!(pflags & 1)) hf |= H_NOFOLLOW;

  int fd = openat(dir, leaf, hf, 0666);
  int e = h_errno;
  wasifs_walk_done(d->fd, dir);
  if (fd < 0) { res_err(vm, ret, 4, ec_of_host(e)); return 0; }

  Desc *nd = malloc(sizeof *nd);
  if (!nd) { close(fd); res_err(vm, ret, 4, EC_NOMEM); return 0; }
  nd->fd = fd;
  nd->preopen = 0;
  nd->guest = 0;
  put8(vm, ret, 0);
  put32(vm, ret + 4, warmc_res_lower_own(RT_DESCRIPTOR, (u64)(uptr)nd));
  return 0;
}

static u64 p2_read(WarmInstance *vm, u64 self, u64 len, u64 off, u64 ret) {
  Desc *d = desc_of(self);
  if (len > (1u << 24)) len = 1u << 24;
  char *buf = len ? malloc((usz)len) : 0;
  if (len && !buf) { res_err(vm, ret, 4, EC_NOMEM); return 0; }
  long r = len ? pread(d->fd, buf, (usz)len, (long long)off) : 0;
  if (r < 0) {
    u32 ec = ec_of_host(h_errno);
    free(buf);
    res_err(vm, ret, 4, ec == EC_ISDIR ? EC_BADF : ec);
    return 0;
  }

  put8(vm, ret, 0);
  u64 q = put_bytes(vm, buf, (u64)r);
  free(buf);
  put32(vm, ret + 4, (u32)q);
  put32(vm, ret + 8, (u32)r);
  put8(vm, ret + 12, (u8)(r == 0 && len != 0));
  return 0;
}

static u64 p2_write(WarmInstance *vm, u64 self, u64 p, u64 n, u64 off,
                    u64 ret) {
  Desc *d = desc_of(self);
  p &= 0xffffffffu;
  n &= 0xffffffffu;
  if (!ok(vm, p, n)) { res_err(vm, ret, 8, EC_INVALID); return 0; }
  u64 done = 0;
  while (done < n) {
    long r = pwrite(d->fd, vm->mem_base + p + done, (usz)(n - done),
                    (long long)(off + done));
    if (r < 0) {
      u32 ec = ec_of_host(h_errno);
      res_err(vm, ret, 8, ec == EC_ISDIR ? EC_BADF : ec);
      return 0;
    }
    if (!r) break;
    done += (u64)r;
  }
  put8(vm, ret, 0);
  put64(vm, ret + 8, done);
  return 0;
}

/* Rust's std reaches a file through a stream rather than through
   descriptor.read and descriptor.write, so a host that has only the latter
   pair answers tinygo and traps on rustc.  The descriptor is dup'd rather
   than shared so that dropping either end leaves the other working, which is
   what a guest that closes a File while a reader is alive expects. */
static u64 via_stream(WarmInstance *vm, u64 self, u64 offset, u64 ret,
                      u8 input, u8 append) {
  Desc *d = desc_of(self);
  /* A directory has no stream, and wasmtime says so here rather than at the
     first read: the difference is visible to a guest, which gets a stream it
     can only fail on instead of an error it can branch on.  The code is
     is-directory, what the kernel says for a read of one and what wasmtime
     answers since 49 (before that it said bad-descriptor); rustc's std turns
     it into ErrorKind::IsADirectory.  descriptor.read keeps bad-descriptor,
     which is still wasmtime's answer there. */
  {
    u8 st[ST_SIZE];
    if (fstat(d->fd, st) < 0) {
      res_err(vm, ret, 4, ec_of_host(h_errno));
      return 0;
    }
    if (wasifs_filetype(st) == FT_DIR) {
      res_err(vm, ret, 4, EC_ISDIR);
      return 0;
    }
  }
  int fd = dup(d->fd);
  if (fd < 0) { res_err(vm, ret, 4, ec_of_host(h_errno)); return 0; }
  /* An appending stream starts at the end rather than at nought.  A dup
     shares its file offset with the descriptor it came from, so writing
     through one without saying where would land wherever the other had left
     it -- which for a freshly opened file is the beginning, on top of what is
     already there. */
  if (append) {
    u8 st[ST_SIZE];
    if (fstat(fd, st) < 0) {
      int e = h_errno;
      close(fd);
      res_err(vm, ret, 4, ec_of_host(e));
      return 0;
    }
    offset = wasifs_st_u64(st, ST_OFF_FSIZE);
  }
  Stream *s = stream_make(fd, input, 0, 1, 0, offset);
  put8(vm, ret, 0);
  put32(vm, ret + 4,
        warmc_res_lower_own(input ? RT_INPUT_STREAM : RT_OUTPUT_STREAM,
                            (u64)(uptr)s));
  return 0;
}

static u64 p2_read_via_stream(WarmInstance *vm, u64 self, u64 off, u64 ret) {
  return via_stream(vm, self, off, ret, 1, 0);
}

static u64 p2_write_via_stream(WarmInstance *vm, u64 self, u64 off, u64 ret) {
  return via_stream(vm, self, off, ret, 0, 0);
}

static u64 p2_append_via_stream(WarmInstance *vm, u64 self, u64 ret) {
  return via_stream(vm, self, 0, ret, 0, 1);
}

static u64 p2_get_flags(WarmInstance *vm, u64 self, u64 ret) {
  Desc *d = desc_of(self);
  int fl = fcntl(d->fd, 3 );
  if (fl < 0) { res_err(vm, ret, 1, ec_of_host(h_errno)); return 0; }
  u8 f = 0;
  if ((fl & 3) == H_RDWR) f = 1 | 2;
  else if ((fl & 3) == H_WRONLY) f = 2;
  else f = 1;
  if (fl & H_SYNC) f |= 4 | 8;
  put8(vm, ret, 0);
  put8(vm, ret + 1, f);
  return 0;
}

/* Two files are the same object when their hashes match, which is all a guest
   does with this, so (inode, modification time ^ size) says the two things it
   has to: which file, and whether it has changed since. */
static void put_hash(WarmInstance *vm, u64 at, const u8 *st) {
  put64(vm, at, wasifs_st_u64(st, ST_OFF_INO));
  put64(vm, at + 8,
        wasifs_st_time(st, ST_OFF_MTIME) ^ wasifs_st_u64(st, ST_OFF_FSIZE));
}

static u64 p2_metadata_hash(WarmInstance *vm, u64 self, u64 ret) {
  Desc *d = desc_of(self);
  u8 st[ST_SIZE];
  if (fstat(d->fd, st) < 0) { res_err(vm, ret, 8, ec_of_host(h_errno)); return 0; }
  put8(vm, ret, 0);
  put_hash(vm, ret + 8, st);
  return 0;
}

static u64 p2_metadata_hash_at(WarmInstance *vm, u64 self, u64 flags, u64 p,
                               u64 n, u64 ret) {
  Desc *d = desc_of(self);
  int dir = -1;
  char leaf[PATHMAX];
  u32 ec = at_leaf(d, vm, p, n, (int)(flags & 1), &dir, leaf);
  if (ec) { res_err(vm, ret, 8, ec); return 0; }
  u8 st[ST_SIZE];
  int r = fstatat(dir, leaf, st, (flags & 1) ? 0 : H_AT_NOFOLLOW);
  int e = h_errno;
  wasifs_walk_done(d->fd, dir);
  if (r < 0) { res_err(vm, ret, 8, ec_of_host(e)); return 0; }
  put8(vm, ret, 0);
  put_hash(vm, ret + 8, st);
  return 0;
}

static u64 p2_read_directory(WarmInstance *vm, u64 self, u64 ret) {
  Desc *d = desc_of(self);
  int fd = dup(d->fd);
  if (fd < 0) { res_err(vm, ret, 4, ec_of_host(h_errno)); return 0; }
  void *dir = fdopendir(fd);
  if (!dir) { int e = h_errno; close(fd); res_err(vm, ret, 4, ec_of_host(e)); return 0; }
  rewinddir(dir);

  DirStream *ds = malloc(sizeof *ds);
  if (!ds) { closedir(dir); res_err(vm, ret, 4, EC_NOMEM); return 0; }
  ds->dir = dir;
  put8(vm, ret, 0);
  put32(vm, ret + 4, warmc_res_lower_own(RT_DIRSTREAM, (u64)(uptr)ds));
  return 0;
}

static u64 p2_read_directory_entry(WarmInstance *vm, u64 self, u64 ret) {
  DirStream *ds =
      (DirStream *)(uptr)warmc_res_lift_borrow(RT_DIRSTREAM, (u32)self);
  warmc_res_release(RT_DIRSTREAM, (u32)self);

  for (;;) {
    u8 *e = readdir(ds->dir);
    if (!e) { put8(vm, ret, 0); put8(vm, ret + 4, 0); return 0; }
    const char *name = (const char *)(e + DE_NAME);
    if (wasifs_str_eq(name, ".") || wasifs_str_eq(name, "..")) continue;
    put8(vm, ret, 0);
    put8(vm, ret + 4, 1);
    put8(vm, ret + 8, p2_filetype(wasifs_de_filetype(e[DE_TYPE])));
    put_string(vm, ret + 12, name, cstrlen(name));
    return 0;
  }
}

static u64 path_op(WarmInstance *vm, u64 self, u64 p, u64 n, u64 ret, int op) {
  Desc *d = desc_of(self);
  int dir = -1;
  char leaf[PATHMAX];
  u32 ec = at_leaf(d, vm, p, n, 0, &dir, leaf);
  if (ec) { res_err(vm, ret, 1, ec); return 0; }

  int r;
  if (op == 0) r = mkdirat(dir, leaf, 0777);
  else if (op == 1) r = unlinkat(dir, leaf, 0);
  else r = unlinkat(dir, leaf, H_AT_REMOVEDIR);
  int e = h_errno;
  wasifs_walk_done(d->fd, dir);
  if (r < 0) { res_err(vm, ret, 1, ec_of_host(e)); return 0; }
  put8(vm, ret, 0);
  return 0;
}

static u64 p2_create_directory_at(WarmInstance *vm, u64 s, u64 p, u64 n, u64 r) {
  return path_op(vm, s, p, n, r, 0);
}
static u64 p2_unlink_file_at(WarmInstance *vm, u64 s, u64 p, u64 n, u64 r) {
  return path_op(vm, s, p, n, r, 1);
}
static u64 p2_remove_directory_at(WarmInstance *vm, u64 s, u64 p, u64 n, u64 r) {
  return path_op(vm, s, p, n, r, 2);
}

static u64 p2_readlink_at(WarmInstance *vm, u64 self, u64 p, u64 n, u64 ret) {
  Desc *d = desc_of(self);
  int dir = -1;
  char leaf[PATHMAX];
  u32 ec = at_leaf(d, vm, p, n, 0, &dir, leaf);
  if (ec) { res_err(vm, ret, 4, ec); return 0; }

  char link[PATHMAX];
  long r = readlinkat(dir, leaf, link, PATHMAX - 1);
  int e = h_errno;
  wasifs_walk_done(d->fd, dir);
  if (r < 0) { res_err(vm, ret, 4, ec_of_host(e)); return 0; }
  put8(vm, ret, 0);
  put_string(vm, ret + 4, link, (u64)r);
  return 0;
}

static u64 p2_symlink_at(WarmInstance *vm, u64 self, u64 op, u64 on, u64 np,
                         u64 nn, u64 ret) {
  Desc *d = desc_of(self);
  char old[PATHMAX];
  u32 ec = read_path(vm, op, on, old);
  if (ec) { res_err(vm, ret, 1, ec); return 0; }
  int dir = -1;
  char leaf[PATHMAX];
  ec = at_leaf(d, vm, np, nn, 0, &dir, leaf);
  if (ec) { res_err(vm, ret, 1, ec); return 0; }

  int r = symlinkat(old, dir, leaf);
  int e = h_errno;
  wasifs_walk_done(d->fd, dir);
  if (r < 0) { res_err(vm, ret, 1, ec_of_host(e)); return 0; }
  put8(vm, ret, 0);
  return 0;
}

static u64 two_place(WarmInstance *vm, u64 self, u64 p1, u64 n1, u64 other,
                     u64 p2, u64 n2, u64 ret, int link, u64 flags) {
  Desc *a = desc_of(self);
  Desc *b = desc_of(other);
  int da = -1, db = -1;
  char la[PATHMAX], lb[PATHMAX];
  u32 ec = at_leaf(a, vm, p1, n1, link ? (int)(flags & 1) : 0, &da, la);
  if (ec) { res_err(vm, ret, 1, ec); return 0; }
  ec = at_leaf(b, vm, p2, n2, 0, &db, lb);
  if (ec) { wasifs_walk_done(a->fd, da); res_err(vm, ret, 1, ec); return 0; }

  int r = link ? linkat(da, la, db, lb, (flags & 1) ? H_AT_SYMLINK_FOLLOW : 0)
               : renameat(da, la, db, lb);
  int e = h_errno;
  wasifs_walk_done(a->fd, da);
  wasifs_walk_done(b->fd, db);
  if (r < 0) { res_err(vm, ret, 1, ec_of_host(e)); return 0; }
  put8(vm, ret, 0);
  return 0;
}

static u64 p2_rename_at(WarmInstance *vm, u64 s, u64 p1, u64 n1, u64 o, u64 p2,
                        u64 n2, u64 ret) {
  return two_place(vm, s, p1, n1, o, p2, n2, ret, 0, 0);
}

static u64 p2_link_at(WarmInstance *vm, u64 s, u64 fl, u64 p1, u64 n1, u64 o,
                      u64 p2, u64 n2, u64 ret) {
  return two_place(vm, s, p1, n1, o, p2, n2, ret, 1, fl);
}

static u64 p2_sync_data(WarmInstance *vm, u64 self, u64 ret) {
  Desc *d = desc_of(self);
  if (fsync(d->fd) < 0) { res_err(vm, ret, 1, ec_of_host(h_errno)); return 0; }
  put8(vm, ret, 0);
  return 0;
}

static u64 p2_drop_descriptor(WarmInstance *vm, u64 h) {
  u32 owned = 0;
  Desc *d = (Desc *)(uptr)warmc_res_drop(RT_DESCRIPTOR, (u32)h, &owned);
  if (d && !d->preopen) { close(d->fd); free(d); }
  return 0;
}

static u64 p2_drop_dirstream(WarmInstance *vm, u64 h) {
  u32 owned = 0;
  DirStream *ds =
      (DirStream *)(uptr)warmc_res_drop(RT_DIRSTREAM, (u32)h, &owned);
  if (ds) { closedir(ds->dir); free(ds); }
  return 0;
}

typedef struct { const char *name; void *fn; } Binding;

static const Binding BINDINGS[] = {
  { "wasi:cli/environment#get-arguments", (void *)p2_get_arguments },
  { "wasi:cli/environment#get-environment", (void *)p2_get_environment },
  { "wasi:cli/environment#initial-cwd", (void *)p2_initial_cwd },
  { "wasi:cli/exit#exit", (void *)p2_exit },
  { "wasi:cli/exit#exit-with-code", (void *)p2_exit_with_code },
  { "wasi:cli/stdin#get-stdin", (void *)p2_get_stdin },
  { "wasi:cli/stdout#get-stdout", (void *)p2_get_stdout },
  { "wasi:cli/stderr#get-stderr", (void *)p2_get_stderr },
  { "wasi:clocks/monotonic-clock#now", (void *)p2_monotonic_now },
  { "wasi:clocks/monotonic-clock#subscribe-duration",
    (void *)p2_subscribe_duration },
  { "wasi:clocks/monotonic-clock#subscribe-instant",
    (void *)p2_subscribe_instant },
  { "wasi:clocks/wall-clock#now", (void *)p2_wall_now },
  { "wasi:io/poll#poll", (void *)p2_poll },
  { "wasi:io/poll#[method]pollable.block", (void *)p2_pollable_block },
  { "wasi:io/poll#[method]pollable.ready", (void *)p2_pollable_ready },
  { "wasi:io/poll#[resource-drop]pollable", (void *)p2_drop_pollable },
  { "wasi:io/streams#[method]input-stream.subscribe",
    (void *)p2_input_subscribe },
  { "wasi:io/streams#[method]output-stream.subscribe",
    (void *)p2_output_subscribe },
  { "wasi:io/error#[resource-drop]error", (void *)p2_drop_error },
  { "wasi:io/streams#[method]input-stream.blocking-read",
    (void *)p2_blocking_read },
  { "wasi:io/streams#[method]input-stream.read", (void *)p2_blocking_read },
  { "wasi:filesystem/types#[method]descriptor.read-via-stream",
    (void *)p2_read_via_stream },
  { "wasi:filesystem/types#[method]descriptor.write-via-stream",
    (void *)p2_write_via_stream },
  { "wasi:filesystem/types#[method]descriptor.append-via-stream",
    (void *)p2_append_via_stream },
  { "wasi:filesystem/types#[method]descriptor.get-flags", (void *)p2_get_flags },
  { "wasi:filesystem/types#[method]descriptor.metadata-hash",
    (void *)p2_metadata_hash },
  { "wasi:filesystem/types#[method]descriptor.metadata-hash-at",
    (void *)p2_metadata_hash_at },
  { "wasi:io/streams#[method]output-stream.blocking-flush",
    (void *)p2_flush },
  { "wasi:io/streams#[method]output-stream.blocking-write-and-flush",
    (void *)p2_write_and_flush },
  { "wasi:io/streams#[resource-drop]input-stream", (void *)p2_drop_input },
  { "wasi:io/streams#[resource-drop]output-stream", (void *)p2_drop_output },
  { "wasi:io/streams#[method]output-stream.write", (void *)p2_stream_write },
  { "wasi:io/streams#[method]output-stream.check-write", (void *)p2_check_write },
  { "wasi:cli/terminal-stdin#get-terminal-stdin", (void *)p2_terminal_stdin },
  { "wasi:cli/terminal-stdout#get-terminal-stdout", (void *)p2_terminal_stdout },
  { "wasi:cli/terminal-stderr#get-terminal-stderr", (void *)p2_terminal_stderr },
  { "wasi:cli/terminal-input#[resource-drop]terminal-input",
    (void *)p2_drop_term_in },
  { "wasi:cli/terminal-output#[resource-drop]terminal-output",
    (void *)p2_drop_term_out },
  { "wasi:random/random#get-random-bytes", (void *)p2_random_bytes },
  { "wasi:random/random#get-random-u64", (void *)p2_random_u64 },
  { "wasi:filesystem/preopens#get-directories",
    (void *)p2_get_directories },
  { "wasi:filesystem/types#[method]descriptor.stat", (void *)p2_stat },
  { "wasi:filesystem/types#[method]descriptor.stat-at",
    (void *)p2_stat_at },
  { "wasi:filesystem/types#[method]descriptor.open-at",
    (void *)p2_open_at },
  { "wasi:filesystem/types#[method]descriptor.read", (void *)p2_read },
  { "wasi:filesystem/types#[method]descriptor.write", (void *)p2_write },
  { "wasi:filesystem/types#[method]descriptor.read-directory",
    (void *)p2_read_directory },
  { "wasi:filesystem/types#[method]directory-entry-stream.read-directory-entry",
    (void *)p2_read_directory_entry },
  { "wasi:filesystem/types#[method]descriptor.create-directory-at",
    (void *)p2_create_directory_at },
  { "wasi:filesystem/types#[method]descriptor.unlink-file-at",
    (void *)p2_unlink_file_at },
  { "wasi:filesystem/types#[method]descriptor.remove-directory-at",
    (void *)p2_remove_directory_at },
  { "wasi:filesystem/types#[method]descriptor.readlink-at",
    (void *)p2_readlink_at },
  { "wasi:filesystem/types#[method]descriptor.symlink-at",
    (void *)p2_symlink_at },
  { "wasi:filesystem/types#[method]descriptor.rename-at",
    (void *)p2_rename_at },
  { "wasi:filesystem/types#[method]descriptor.link-at",
    (void *)p2_link_at },
  { "wasi:filesystem/types#[method]descriptor.sync-data",
    (void *)p2_sync_data },
  { "wasi:filesystem/types#[resource-drop]descriptor",
    (void *)p2_drop_descriptor },
  { "wasi:filesystem/types#[resource-drop]directory-entry-stream",
    (void *)p2_drop_dirstream },
};

#define NBINDINGS (sizeof BINDINGS / sizeof BINDINGS[0])

/* An interface name carries a version -- wasi:io/streams@0.2.0 from tinygo,
   @0.2.12 from rustc -- and the 0.2 series is one compatible interface with
   things added to it, which is why clink accepts any @0.2.x.  The table is
   keyed without a version and the version is skipped here, so a guest built
   against a later patch of the same interface is not a guest this host
   silently fails to answer. */
static int name_eq_unversioned(const char *key, const char *name) {
  while (*key && *name) {
    if (*name == '@') {
      while (*name && *name != '#') name++;
      continue;
    }
    if (*key != *name) return 0;
    key++; name++;
  }
  if (*name == '@') while (*name && *name != '#') name++;
  return *key == *name;
}

static void *p2_lookup(const char *name) {
  for (unsigned i = 0; i < NBINDINGS; i++)
    if (name_eq_unversioned(BINDINGS[i].name, name)) return BINDINGS[i].fn;
  return 0;
}

static int add_env(char *kv) {
  if (g_nenv == g_env_cap) {
    u32 cap = g_env_cap ? g_env_cap * 2 : 16;
    char **v = malloc(sizeof(char *) * cap);
    if (!v) return 0;
    for (u32 i = 0; i < g_nenv; i++) v[i] = g_env[i];
    free(g_env);
    g_env = v;
    g_env_cap = cap;
  }
  g_env[g_nenv++] = kv;
  return 1;
}

static int add_preopen(const char *spec) {
  char host[PATHMAX];
  const char *guest = spec;
  usz i = 0;
  for (; spec[i] && !(spec[i] == ':' && spec[i + 1] == ':'); i++) {
    if (i >= PATHMAX - 1) return 0;
    host[i] = spec[i];
  }
  host[i] = 0;
  guest = spec[i] ? spec + i + 2 : host;
  if (!guest[0]) guest = host;

  int fd = openat(H_AT_FDCWD, host, H_RDONLY | H_DIRECTORY);
  if (fd < 0) return 0;

  Desc *d = malloc(sizeof *d);
  if (!d) { close(fd); return 0; }
  d->fd = fd;
  d->preopen = 1;
  d->guest = guest;
  if (g_npre == sizeof g_pre / sizeof g_pre[0]) { close(fd); free(d); return 0; }
  g_pre[g_npre++] = warmc_res_new(RT_DESCRIPTOR, (u64)(uptr)d);
  return 1;
}

static int drive(int argc, char **argv) {
  signal(13 , (void (*)(int))1 );

  int keep = 1;
  for (int i = 1; i < argc; i++) {
    char *a = argv[i];
    if (a[0] == '-' && a[1] == '-' && a[2] == 'd' && a[3] == 'i' &&
        a[4] == 'r' && a[5] == '=') {
      if (!add_preopen(a + 6)) {
        err("warmc-wasip2: cannot preopen "); err(a + 6); err("\n");
        return 74;
      }
      continue;
    }
    if (str_eq(a, "--dir") && i + 1 < argc) {
      if (!add_preopen(argv[++i])) {
        err("warmc-wasip2: cannot preopen "); err(argv[i]); err("\n");
        return 74;
      }
      continue;
    }
    if (a[0] == '-' && a[1] == '-' && a[2] == 'e' && a[3] == 'n' &&
        a[4] == 'v' && a[5] == '=') {
      if (!add_env(a + 6)) { err("warmc-wasip2: out of memory\n"); return 74; }
      continue;
    }
    if (str_eq(a, "--env") && i + 1 < argc) {
      if (!add_env(argv[++i])) { err("warmc-wasip2: out of memory\n"); return 74; }
      continue;
    }
    if (str_eq(a, "--inherit-env")) { g_env_inherit = 1; continue; }
    if (str_eq(a, "--")) {
      for (int j = i + 1; j < argc; j++) argv[keep++] = argv[j];
      break;
    }
    argv[keep++] = argv[i];
  }
  g_argc = keep;
  g_argv = argv;

  warmc_host_lookup = p2_lookup;
  warmc_component_instantiate();

  const ExportDesc *ex = 0;
  WarmInstance *v = 0;
  for (u32 i = 0; i < warmc_component->nexports && !v; i++)
    if (name_eq_unversioned("wasi:cli/run#run",
                            warmc_component->exports[i].name))
      v = warmc_component_export(warmc_component->exports[i].name, &ex);
  if (!v) {
    err("warmc-wasip2: the component does not export wasi:cli/run@0.2.0;"
        " it is not a command\n");
    return 72;
  }

  FuncRef fr = warmc_export_func(v, ex);
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  u32 trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance,
                                   iargs, fargs, ret);
  if (trap) {
    err("warmc-wasip2: trap: ");
    err(warmc_trap_name(trap));
    err("\n  (an unimplemented wasi:* call traps like this;\n"
        "   rt/wasip2.c lists what this host implements)\n");
    return 73;
  }

  warmc_report_run(v);
  return (ret[0] & 1) ? 1 : 0;
}

int (*warmc_driver_hook)(int argc, char **argv) = drive;
