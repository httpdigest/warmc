/* Driver for the pinned-register pair (pin_a.wat, pin_b.wat): instantiate A,
 * register it under the name B imports from, instantiate B, and call each of
 * B's exports, every one of which crosses into A and back -- and A's own
 * export, entered from here, whose million tail calls go through its entry
 * check without a frame each. */
#include "../../rt/warm_rt.h"

extern const ModuleDesc pa_module;
extern const ModuleDesc pb_module;

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

static u64 call(WarmInstance *inst, const char *name, u64 arg, int nargs) {
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("missing export "); out(name); out("\n"); return 0; }
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  if (nargs) iargs[0] = arg;
  FuncRef fr = warmc_export_func(inst, e);
  warmc_invoke(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
  return e->nresults ? ret[WARMC_RET_X0] : 0;
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
  WarmInstance *a = warmc_instantiate(&pa_module);
  warmc_register("A", a);
  WarmInstance *b = warmc_instantiate(&pb_module);
  show("call_wide", call(b, "call_wide", 0, 0));
  show("ind_wide", call(b, "ind_wide", 0, 0));
  show("ref_wide", call(b, "ref_wide", 0, 0));
  show("tail_wide", call(b, "tail_wide", 0, 0));
  show("tail_ind_wide", call(b, "tail_ind_wide", 0, 0));
  show("tail_ref_wide", call(b, "tail_ref_wide", 0, 0));
  show("call_fwide", call(b, "call_fwide", 0, 0));
  show("call_multi", call(b, "call_multi", 0, 0));
  show("catch_mem", call(b, "catch_mem", 5, 1));
  show("nested", call(b, "nested", 50, 1));
  show("nested_throw", call(b, "nested_throw", 7, 1));
  show("nested_mid", call(b, "nested_mid", 5, 1));
  show("by_global", call(b, "by_global", 0, 0));
  show("by_table", call(b, "by_table", 0, 0));
  show("given", call(b, "given", 0, 0));
  show("call_tail_more", call(b, "call_tail_more", 0, 0));
  show("call_deep", call(b, "call_deep", 0, 0));
  show("tail_deep", call(b, "tail_deep", 0, 0));
  show("tail_bounce", call(b, "tail_bounce", 0, 0));
  show("deep", call(a, "deep", 1000000, 1));
  return 0;
}
