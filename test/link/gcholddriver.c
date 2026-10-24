/* Driver for the held-reference test: a reference the embedder holds with
 * warmc_gc_hold across a call that traps stays held.  The trap unwinds
 * only the frames the call made, so the collector still finds the
 * embedder's root and the read after comes back with what was put in:
 * hold=42. */
#include "../../rt/warm_rt.h"

extern const ModuleDesc gch_module;

/* The CLI harness in the runtime refers to this; a driver replaces the
 * harness but not the reference. */
const ModuleDesc warmc_module = { 0 };


static void out(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(1, s, (usz)(p - s));
}

static void put_u64(u64 v) {
  char buf[24];
  int i = 24;
  if (!v) buf[--i] = '0';
  while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
  warmc_rt_write(1, buf + i, (usz)(24 - i));
}

static u64 call(WarmInstance *inst, const char *name, u64 arg, u32 *trap) {
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("missing export "); out(name); out("\n"); return 0; }
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  iargs[0] = arg;
  FuncRef fr = warmc_export_func(inst, e);
  *trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
  return ret[WARMC_RET_X0];
}

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  (void)argc; (void)argv;
  WarmInstance *t = warmc_instantiate(&gch_module);
  u32 trap = 0;
  static const u32 one = 1;
  u64 slot = call(t, "make", 42, &trap);
  u64 hold[8];
  if (warmc_gc_hold_bytes() > sizeof hold) { out("hold too small\n"); return 1; }
  warmc_gc_hold(&slot, &one, hold);
  call(t, "boom", 0, &trap);
  out(trap ? "trapped\n" : "no trap\n");
  call(t, "churn", 4000, &trap);
  warmc_gc_collect();
  out("hold=");
  put_u64(call(t, "read", slot, &trap));
  out("\n");
  warmc_gc_release(hold);
  return 0;
}
