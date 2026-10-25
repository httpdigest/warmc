/* Driver for test/link/once_x.wat: stores the function the module's `bump`
 * global holds into its exported global `x`, which the module's code calls
 * through as TeaVM calls a class's initializer, and prints what run
 * counts: three calls of bump.
 */
#include "../../rt/warm_rt.h"

extern const ModuleDesc ox_module;

/* The CLI harness in the runtime refers to this; a driver replaces the
 * harness but not the reference. */
const ModuleDesc warmc_module = { 0 };

static void out(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(1, s, (usz)(p - s));
}

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  (void)argc; (void)argv;
  WarmInstance *in = warmc_instantiate(&ox_module);
  const ExportDesc *x = warmc_find_export(in, "x"), *b = warmc_find_export(in, "bump");
  const ExportDesc *run = warmc_find_export(in, "run");
  if (!x || !b || !run) { out("missing export\n"); return 1; }
  *warmc_global_cell(in, x->index) = *warmc_global_cell(in, b->index);
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 }, ret[WARMC_RETBUF_WORDS] = { 0 };
  FuncRef fr = warmc_export_func(in, run);
  warmc_invoke(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
  char buf[] = "once_x=0\n";
  buf[7] = (char)('0' + (u32)ret[WARMC_RET_X0] % 10);
  out(buf);
  return 0;
}
