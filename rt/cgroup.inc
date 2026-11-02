/* The CPU and memory limits a Linux cgroup v2 hierarchy sets on the
   process, as its files show them: the process's cgroup is the "0::" line
   of /proc/self/cgroup, under the cgroup2 mount /proc/self/mountinfo
   names, and every cgroup from it up to the mount limits it -- the least
   cpu.max quota over its period, the least memory.max and memory.high.

   Included by rt/rt.c (the GC heap a program reserves) and src/limits.c
   (the threads a compile and the JIT's collectors take), each of which
   gives it a way to read a file and a buffer to read into.  Free of any C
   library, as the runtime is.  `root`, normally "", is put in front of
   every path: WARMC_CGROUP_ROOT, a tree that tests lay out by hand. */

typedef struct {
  /* bytes: the least memory.max or memory.high on the way up; 0 for none */
  unsigned long long mem;
  /* thousandths of a CPU: the least cpu.max quota over its period; 0 for
     none */
  unsigned long long cpu_milli;
} CgLimits;

/* path read whole into buf, cap bytes with its NUL: its length, or -1 */
typedef long (*CgRead)(const char *path, char *buf, long cap);

#define CG_PATH 4096

static long cg_len(const char *s) {
  long n = 0;
  while (s[n]) n++;
  return n;
}

/* d made a then b, cut at CG_PATH: 0 when it did not fit */
static int cg_join(char *d, const char *a, const char *b, long bn) {
  long an = cg_len(a);
  if (bn < 0) bn = cg_len(b);
  if (an + bn + 1 > CG_PATH) return 0;
  for (long i = 0; i < an; i++) d[i] = a[i];
  for (long i = 0; i < bn; i++) d[an + i] = b[i];
  d[an + bn] = 0;
  return 1;
}

/* the decimal number at s, or ~0 for "max"; ~0 too for anything else */
static unsigned long long cg_num(const char *s, const char **end) {
  unsigned long long v = 0;
  const char *p = s;
  if (p[0] == 'm' && p[1] == 'a' && p[2] == 'x') {
    if (end) *end = p + 3;
    return ~0ull;
  }
  if (*p < '0' || *p > '9') {
    if (end) *end = p;
    return ~0ull;
  }
  while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned long long)(*p++ - '0');
  if (end) *end = p;
  return v;
}

/* a field of mountinfo, its octal escapes (\040 a space) undone, to d */
static const char *cg_field(const char *p, char *d, long cap) {
  long n = 0;
  while (*p && *p != ' ' && *p != '\n') {
    char c = *p++;
    if (c == '\\' && p[0] >= '0' && p[0] <= '7' && p[1] >= '0' && p[1] <= '7' &&
        p[2] >= '0' && p[2] <= '7') {
      c = (char)((p[0] - '0') * 64 + (p[1] - '0') * 8 + (p[2] - '0'));
      p += 3;
    }
    if (n + 1 < cap) d[n++] = c;
  }
  d[n] = 0;
  return *p == ' ' ? p + 1 : p;
}

/* The limits on the process into *out, zero where there are none:
   no cgroup v2, a file missing or unreadable, a limit of "max". */
static void cg_limits(CgRead rd, const char *root, char *buf, long cap, CgLimits *out) {
  out->mem = 0;
  out->cpu_milli = 0;
  char path[CG_PATH], cg[CG_PATH], mroot[CG_PATH], mpoint[CG_PATH], dir[CG_PATH];

  /* the process's cgroup: the "0::" line */
  if (!cg_join(path, root, "/proc/self/cgroup", -1) || rd(path, buf, cap) < 0) return;
  const char *p = buf;
  cg[0] = 0;
  while (*p) {
    if (p[0] == '0' && p[1] == ':' && p[2] == ':') {
      long n = 0;
      p += 3;
      while (p[n] && p[n] != '\n') n++;
      if (!cg_join(cg, "", p, n)) return;
      break;
    }
    while (*p && *p != '\n') p++;
    if (*p) p++;
  }
  if (!cg[0]) return;

  /* where the cgroup2 hierarchy is mounted, and which of its cgroups the
     mount shows at its top */
  if (!cg_join(path, root, "/proc/self/mountinfo", -1) || rd(path, buf, cap) < 0) return;
  mpoint[0] = 0;
  for (p = buf; *p && !mpoint[0];) {
    const char *line = p, *dash = 0;
    while (*p && *p != '\n') {
      if (p[0] == ' ' && p[1] == '-' && p[2] == ' ' && !dash) dash = p + 3;
      p++;
    }
    if (*p) p++;
    if (!dash || dash[0] != 'c' || dash[1] != 'g' || dash[2] != 'r' || dash[3] != 'o' ||
        dash[4] != 'u' || dash[5] != 'p' || dash[6] != '2' || dash[7] != ' ')
      continue;
    /* id parent major:minor root mountpoint ... */
    const char *f = line;
    char skip[64];
    for (int k = 0; k < 3; k++) f = cg_field(f, skip, sizeof skip);
    f = cg_field(f, mroot, CG_PATH);
    cg_field(f, mpoint, CG_PATH);
  }
  if (!mpoint[0]) return;

  /* the process's cgroup's directory: its path below the mount's root;
     one the mount does not show is the mount's top */
  long rn = cg_len(mroot);
  const char *below = cg;
  if (!(rn == 1 && mroot[0] == '/')) {
    long k = 0;
    while (k < rn && cg[k] == mroot[k]) k++;
    below = k == rn && (cg[k] == '/' || !cg[k]) ? cg + k : "";
  }
  if (below[0] == '/' && !below[1]) below = "";
  char top[CG_PATH];
  if (!cg_join(top, root, mpoint, -1) || !cg_join(dir, top, below, -1)) return;

  /* up from it to the mount, each cgroup's limits */
  long topn = cg_len(top);
  for (;;) {
    if (cg_join(path, dir, "/cpu.max", -1) && rd(path, buf, cap) > 0) {
      const char *e;
      unsigned long long q = cg_num(buf, &e), per = 0;
      if (*e == ' ') per = cg_num(e + 1, 0);
      if (q != ~0ull && per && per != ~0ull) {
        unsigned long long m = q * 1000 / per;
        if (!m) m = 1;
        if (!out->cpu_milli || m < out->cpu_milli) out->cpu_milli = m;
      }
    }
    static const char *const memf[2] = { "/memory.max", "/memory.high" };
    for (int k = 0; k < 2; k++)
      if (cg_join(path, dir, memf[k], -1) && rd(path, buf, cap) > 0) {
        unsigned long long v = cg_num(buf, 0);
        if (v != ~0ull && v && (!out->mem || v < out->mem)) out->mem = v;
      }
    long n = cg_len(dir);
    if (n <= topn) break;
    while (n > topn && dir[n - 1] != '/') n--;
    if (n > topn) n--;
    dir[n] = 0;
  }
}
