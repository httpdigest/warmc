/* Driver for the link test of sealing (test/gen_link_seal.py): P, then m1 to
 * m11, each registered under its name for the modules after it, then R; and
 * each way a reference can cross a module's boundary run once, printing
 * what the module's casts came to, or the trap that stopped them.
 */
#include "../../rt/warm_rt.h"

extern const ModuleDesc sP_module, sm1_module, sm2_module, sm3_module,
    sm4_module, sm5_module, sm6_module, sm7_module, sm8_module, sm9_module,
    sm10_module, sm11_module, sR_module;

/* The CLI harness in the runtime refers to this; a driver replaces the
 * harness but not the reference. */
const ModuleDesc warmc_module = { 0 };

static void out(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(1, s, (usz)(p - s));
}

static void put_u32(u32 v) {
  char buf[16];
  int i = 16;
  if (!v) buf[--i] = '0';
  while (v) { buf[--i] = (char)('0' + v % 10); v /= 10; }
  warmc_rt_write(1, buf + i, (usz)(16 - i));
}

/* a function with no result prints ok */
static void run(const char *label, WarmInstance *inst, const char *name, int result) {
  out(label);
  out("=");
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("no export "); out(name); out("\n"); return; }
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  FuncRef fr = warmc_export_func(inst, e);
  u32 trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
  if (trap) {
    out("trap: ");
    out(warmc_trap_name(trap));
  } else if (result) {
    put_u32((u32)ret[WARMC_RET_X0]);
  } else {
    out("ok");
  }
  out("\n");
}

static WarmInstance *inst(const ModuleDesc *md, const char *name) {
  WarmInstance *i = warmc_instantiate(md);
  if (name) warmc_register(name, i);
  return i;
}

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  (void)argc; (void)argv;
  inst(&sP_module, "P");
  WarmInstance *m1 = inst(&sm1_module, 0), *m2 = inst(&sm2_module, 0),
               *m3 = inst(&sm3_module, 0), *m4 = inst(&sm4_module, 0),
               *m5 = inst(&sm5_module, 0);
  inst(&sm6_module, "m6");
  inst(&sm7_module, "m7");
  inst(&sm8_module, "m8");
  inst(&sm9_module, "m9");
  inst(&sm10_module, "m10");
  WarmInstance *m11 = inst(&sm11_module, "m11");
  WarmInstance *r = inst(&sR_module, 0);

  run("import_result", m1, "run", 1);
  run("import_argument", m2, "run", 1);
  run("import_global", m3, "run", 1);
  run("import_table", m4, "run", 1);
  run("import_tag", m5, "run", 1);
  run("export_argument", r, "run6", 1);
  run("export_result", r, "run7", 1);
  run("export_global_mut", r, "run8", 1);
  run("export_global", r, "run9", 1);
  run("export_table", r, "run10", 1);
  run("arm", r, "arm11", 0);
  run("export_tag", m11, "run", 1);
  return 0;
}
