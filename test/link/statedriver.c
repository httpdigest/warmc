/* Driver for test/run_trapstate.py: calls the exports its arguments name,
 * EXPORT or EXPORT:A,B,C with up to three integer arguments, one after the
 * other on one instance, catching each trap, and prints a line per call --
 * the i32 result, "none" for one without, or "trap" -- so that what a trap
 * left in memory and globals can be read by the calls after it. */
#include "../../rt/warm_rt.h"

extern const ModuleDesc ts_module;

/* The CLI harness in the runtime refers to this; a driver replaces the
 * harness but not the reference. */
const ModuleDesc warmc_module = { 0 };


static void out(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(1, s, (usz)(p - s));
}

static void put_i32(int v) {
  char buf[16];
  int i = 16;
  unsigned u = v < 0 ? (unsigned)(-(v + 1)) + 1u : (unsigned)v;
  if (!u) buf[--i] = '0';
  while (u) { buf[--i] = (char)('0' + (u % 10)); u /= 10; }
  if (v < 0) buf[--i] = '-';
  warmc_rt_write(1, buf + i, (usz)(16 - i));
}

static u64 parse(const char **s) {
  const char *p = *s;
  u64 v = 0;
  int neg = *p == '-';
  if (neg) p++;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    for (p += 2; ; p++) {
      char c = *p;
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0) break;
      v = v * 16 + (u64)d;
    }
  } else {
    for (; *p >= '0' && *p <= '9'; p++) v = v * 10 + (u64)(*p - '0');
  }
  *s = p;
  return neg ? (u64)0 - v : v;
}

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  WarmInstance *t = warmc_instantiate(&ts_module);
  for (int a = 1; a < argc; a++) {
    char name[64];
    const char *p = argv[a];
    u32 n = 0;
    while (*p && *p != ':' && n < sizeof name - 1) name[n++] = *p++;
    name[n] = 0;
    u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
    u64 ret[WARMC_RETBUF_WORDS] = { 0 };
    for (u32 k = 0; *p == ':' || *p == ','; k++) {
      p++;
      if (k < 3) iargs[k] = parse(&p);
    }
    const ExportDesc *e = warmc_find_export(t, name);
    if (!e) { out("missing export "); out(name); out("\n"); return 1; }
    FuncRef fr = warmc_export_func(t, e);
    u32 trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
    if (trap) out("trap");
    else if (!e->nresults) out("none");
    else put_i32((int)(u32)ret[WARMC_RET_X0]);
    out("\n");
  }
  return 0;
}
