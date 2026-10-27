/* The WASI preview1 host test/llvm_compare.py links wasm2c's C for a kernel
   with, as module m: enough for the benchmark corpora -- stdio, clocks,
   random bytes and sleeps; no file but 0-2, no directory, no socket.
   M_NO_WASI: a module that imports nothing, which wasm2c instantiates
   with no host. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wasm-rt.h"
#include "m.h"

struct w2c_wasi__snapshot__preview1 { wasm_rt_memory_t *mem; };

static uint8_t *M(struct w2c_wasi__snapshot__preview1 *w) { return w->mem->data; }
#define P(a) (M(w) + (uint32_t)(a))

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;

#define F(name) w2c_wasi__snapshot__preview1_##name

u32 F(fd_write)(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 iovs, u32 n, u32 pn) {
  uint32_t tot = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t b, l;
    memcpy(&b, P(iovs + 8 * i), 4); memcpy(&l, P(iovs + 8 * i + 4), 4);
    if (!l) continue;
    ssize_t r = write((int)fd, P(b), l);
    if (r < 0) return tot ? 0 : 29;
    tot += (uint32_t)r;
    if ((uint32_t)r < l) break;
  }
  memcpy(P(pn), &tot, 4);
  return 0;
}
u32 F(fd_read)(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 iovs, u32 n, u32 pn) {
  uint32_t tot = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t b, l;
    memcpy(&b, P(iovs + 8 * i), 4); memcpy(&l, P(iovs + 8 * i + 4), 4);
    if (!l) continue;
    ssize_t r = read((int)fd, P(b), l);
    if (r < 0) return tot ? 0 : 29;
    tot += (uint32_t)r;
    if ((uint32_t)r < l) break;
  }
  memcpy(P(pn), &tot, 4);
  return 0;
}
void F(proc_exit)(struct w2c_wasi__snapshot__preview1 *w, u32 code) { (void)w; fflush(stdout); _exit((int)code); }
u32 F(environ_sizes_get)(struct w2c_wasi__snapshot__preview1 *w, u32 pc, u32 ps) {
  uint32_t z = 0; memcpy(P(pc), &z, 4); memcpy(P(ps), &z, 4); return 0;
}
u32 F(environ_get)(struct w2c_wasi__snapshot__preview1 *w, u32 a, u32 b) { (void)w; (void)a; (void)b; return 0; }
u32 F(args_sizes_get)(struct w2c_wasi__snapshot__preview1 *w, u32 pc, u32 ps) {
  uint32_t one = 1, two = 2; memcpy(P(pc), &one, 4); memcpy(P(ps), &two, 4); return 0;
}
u32 F(args_get)(struct w2c_wasi__snapshot__preview1 *w, u32 argv, u32 buf) {
  memcpy(P(argv), &buf, 4); memcpy(P(buf), "x", 2); return 0;
}
u32 F(random_get)(struct w2c_wasi__snapshot__preview1 *w, u32 buf, u32 n) {
  for (uint32_t i = 0; i < n; i++) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    *P(buf + i) = (uint8_t)g_rng;
  }
  return 0;
}
static const clockid_t CLK[4] = { CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID };
u32 F(clock_time_get)(struct w2c_wasi__snapshot__preview1 *w, u32 id, u64 prec, u32 pt) {
  (void)prec;
  if (id > 3) return 28;
  struct timespec ts; clock_gettime(CLK[id], &ts);
  uint64_t t = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
  memcpy(P(pt), &t, 8); return 0;
}
u32 F(clock_res_get)(struct w2c_wasi__snapshot__preview1 *w, u32 id, u32 pt) {
  (void)id; uint64_t t = 1000; memcpy(P(pt), &t, 8); return 0;
}
u32 F(fd_fdstat_get)(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 buf) {
  if (fd > 2) return 8;
  memset(P(buf), 0, 24); *P(buf) = 2;
  uint64_t rights = ~0ull; memcpy(P(buf + 8), &rights, 8); return 0;
}
u32 F(fd_fdstat_set_flags)(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 f) { (void)w; (void)f; return fd > 2 ? 8 : 0; }
u32 F(fd_filestat_get)(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 buf) {
  if (fd > 2) return 8;
  memset(P(buf), 0, 64); *P(buf + 16) = 2; return 0;
}
u32 F(fd_close)(struct w2c_wasi__snapshot__preview1 *w, u32 fd) { (void)w; (void)fd; return 0; }
u32 F(fd_seek)(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u64 off, u32 wh, u32 pn) { (void)w; (void)fd; (void)off; (void)wh; (void)pn; return 70; }
u32 F(fd_prestat_get)(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 buf) { (void)w; (void)fd; (void)buf; return 8; }
u32 F(fd_prestat_dir_name)(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 p, u32 n) { (void)w; (void)fd; (void)p; (void)n; return 8; }
u32 F(sched_yield)(struct w2c_wasi__snapshot__preview1 *w) { (void)w; return 0; }
u32 F(poll_oneoff)(struct w2c_wasi__snapshot__preview1 *w, u32 in, u32 out, u32 n, u32 pn) {
  /* clocks are slept for, the earliest; descriptors are ready */
  uint64_t best = UINT64_MAX; uint32_t bi = 0, nev = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint8_t *s = P(in + 48 * i);
    if (s[8] == 0) {
      uint64_t t; memcpy(&t, s + 24, 8);
      uint16_t fl; memcpy(&fl, s + 40, 2);
      if (fl & 1) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; t = t > now ? t - now : 0; }
      if (t < best) { best = t; bi = i; }
    } else {
      uint8_t *e = P(out + 32 * nev++); memset(e, 0, 32);
      memcpy(e, s, 8); e[10] = s[8];
    }
  }
  if (!nev && best != UINT64_MAX) {
    struct timespec ts = { (time_t)(best / 1000000000ull), (long)(best % 1000000000ull) };
    nanosleep(&ts, 0);
    uint8_t *e = P(out); memset(e, 0, 32); memcpy(e, P(in + 48 * bi), 8); nev = 1;
  }
  memcpy(P(pn), &nev, 4);
  return 0;
}
#define NOSYS(name, ...) u32 F(name)(struct w2c_wasi__snapshot__preview1 *w, __VA_ARGS__) { (void)w; return 52; }
NOSYS(path_filestat_get, u32 a, u32 b, u32 c, u32 d, u32 e)
NOSYS(fd_filestat_set_size, u32 a, u64 b)
NOSYS(path_open, u32 a, u32 b, u32 c, u32 d, u32 e, u64 f, u64 g, u32 h, u32 i)
NOSYS(path_create_directory, u32 a, u32 b, u32 c)
NOSYS(fd_pread, u32 a, u32 b, u32 c, u64 d, u32 e)
NOSYS(fd_pwrite, u32 a, u32 b, u32 c, u64 d, u32 e)
NOSYS(fd_readdir, u32 a, u32 b, u32 c, u64 d, u32 e)
NOSYS(fd_sync, u32 a)
NOSYS(sock_accept, u32 a, u32 b, u32 c)
NOSYS(sock_recv, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f)
NOSYS(sock_send, u32 a, u32 b, u32 c, u32 d, u32 e)
NOSYS(sock_shutdown, u32 a, u32 b)

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  wasm_rt_init();
  static w2c_m inst;
  static struct w2c_wasi__snapshot__preview1 wasi;
#ifdef M_NO_WASI
  (void)wasi;
  wasm2c_m_instantiate(&inst);
#else
  wasm2c_m_instantiate(&inst, &wasi);
  wasi.mem = w2c_m_memory(&inst);
#endif
  w2c_m_0x5Fstart(&inst);
  fflush(stdout);
  wasm2c_m_free(&inst);
  wasm_rt_free();
  return 0;
}
