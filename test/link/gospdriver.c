/* Driver for the Go-shaped pair: instantiate A, register it under the name
 * B imports from, instantiate B, whose start function runs its Go function,
 * and read the stack pointer the function A called back saw. */
#include "../../rt/warm_rt.h"

extern const ModuleDesc gsa_module;
extern const ModuleDesc gsb_module;

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

static u64 call(WarmInstance *inst, const char *name) {
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("missing export "); out(name); out("\n"); return 0; }
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  FuncRef fr = warmc_export_func(inst, e);
  warmc_invoke(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
  return ret[WARMC_RET_X0];
}

static void show(const char *label, u64 v) {
  out(label);
  out("=");
  put_i32((int)(u32)v);
  out("\n");
}

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  (void)argc; (void)argv;
  WarmInstance *a = warmc_instantiate(&gsa_module);
  warmc_register("A", a);
  WarmInstance *b = warmc_instantiate(&gsb_module);
  (void)a;
  show("peek_saw", call(b, "seen"));
  return 0;
}
