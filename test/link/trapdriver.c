/* Driver for the trap-then-read test: call an export that traps partway
 * through a loop, catch the trap, and read the memory the loop was writing.
 *
 * Separate from driver.c because it is the *embedder's* view that this needs
 * -- the CLI harness runs one export per process and a trap ends it, so an
 * instruction moved to the wrong side of a trap is invisible from inside
 * wasm.  warmc_invoke_catching is the entry point that survives one.
 */
#include "../../rt/warm_rt.h"

extern const ModuleDesc t_module;

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

static u32 call_catching(WarmInstance *inst, const char *name, u64 a0, u64 a1) {
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("missing export "); out(name); out("\n"); return 0; }
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  iargs[0] = a0;
  iargs[1] = a1;
  FuncRef fr = warmc_export_func(inst, e);
  return warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance,
                               iargs, fargs, ret);
}

static u64 call(WarmInstance *inst, const char *name, u64 a0) {
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("missing export "); out(name); out("\n"); return 0; }
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  iargs[0] = a0;
  FuncRef fr = warmc_export_func(inst, e);
  warmc_invoke(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
  return ret[WARMC_RET_X0];
}

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  (void)argc; (void)argv;

  WarmInstance *t = warmc_instantiate(&t_module);

  /* Four iterations asked for, a zero divisor, so the first divide traps and
   * the store under it never runs.  The cell must still hold what the data
   * segment put there. */
  out("trapped=");
  put_i32(call_catching(t, "hoist", 4, 0) != 0);
  out("\n");
  out("cell_after_trap=");
  put_i32((int)call(t, "peek", 64));
  out("\n");

  /* And the same loop with a divisor that cannot trap does write. */
  call(t, "ok", 4);
  out("cell_after_ok=");
  put_i32((int)call(t, "peek", 68));
  out("\n");
  return 0;
}
