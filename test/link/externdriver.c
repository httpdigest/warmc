/* Driver for the host-reference test: a pointer the host makes, null, and a
 * struct the module made, each through test/link/extern.wat's t and rt. */
#include "../../rt/warm_rt.h"

extern const ModuleDesc ext_module;

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
  while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
  warmc_rt_write(1, buf + i, (usz)(16 - i));
}

static u64 call(WarmInstance *inst, const char *name, u64 arg) {
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("missing export "); out(name); out("\n"); return 0; }
  u64 iargs[8] = { arg }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  FuncRef fr = warmc_export_func(inst, e);
  if (warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret)) {
    out("trap\n");
    return 0;
  }
  return ret[WARMC_RET_X0];
}

static u64 obj[4] __attribute__((aligned(16))) = { 0x1234, 0, 0, 0 };

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  (void)argc; (void)argv;
  WarmInstance *t = warmc_instantiate(&ext_module);
  u64 host = (u64)(uptr)obj;
  out("host=");     put_u32((u32)call(t, "t", host));  out("\n");
  out("host_back="); out(call(t, "rt", host) == host ? "same" : "changed"); out("\n");
  out("null_back="); out(call(t, "rt", 0) == 0 ? "null" : "not null"); out("\n");
  u64 s = call(t, "mk", 0);
  out("struct=");   put_u32((u32)call(t, "t", s));  out("\n");
  return 0;
}
