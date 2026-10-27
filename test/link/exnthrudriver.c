/* Driver for the exception-through-a-module trio: A throws, B calls it and
 * has nothing to throw or collect, C calls B and catches; C's catch(20) is
 * 41, A's throw of B's twice 20 plus one. */
#include "../../rt/warm_rt.h"

extern const ModuleDesc exa_module;
extern const ModuleDesc exb_module;
extern const ModuleDesc exc_module;

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
  warmc_register("A", warmc_instantiate(&exa_module));
  warmc_register("B", warmc_instantiate(&exb_module));
  WarmInstance *c = warmc_instantiate(&exc_module);
  const ExportDesc *ex = warmc_find_export(c, "catch");
  if (!ex) { out("no catch export\n"); return 1; }
  FuncRef fr = warmc_export_func(c, ex);
  u64 iargs[8] = { 20 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  u32 trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
  if (trap) { out("trap: "); out(warmc_trap_name(trap)); out("\n"); return 1; }
  out((u32)ret[0] == 41 ? "41\n" : "wrong\n");
  return 0;
}
