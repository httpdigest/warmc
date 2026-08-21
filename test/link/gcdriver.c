/* Driver for the two-module *collection* test: instantiate A, register it
 * under the name B imports from, and call across the boundary in a way that
 * collects with frames from both modules on the stack.
 *
 * Separate from driver.c because these two modules are assembled with
 * wasm-tools rather than wat2wasm -- this wabt build has no GC in it -- and
 * because the program is run twice, once under a stress collection.
 */
#include "../../rt/warm_rt.h"

extern const ModuleDesc gca_module;
extern const ModuleDesc gcb_module;

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

/* A round handler that fails leaves the round unanswered unless the trap
 * path answers it, and the collector then waits for ever with a cycle still
 * marked running -- which is also the one thing gc_assist waits on.  So the
 * finding is not the failure, it is the *next* call: run with
 * WARMC_GC_DIE_ROUND set, the first invocation below dies inside a round and
 * the second has to complete rather than hang. */
static u32 call_catching(WarmInstance *inst, const char *name, u64 arg) {
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("missing export "); out(name); out("\n"); return 0; }
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  iargs[0] = arg;
  FuncRef fr = warmc_export_func(inst, e);
  return warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance,
                               iargs, fargs, ret);
}

static u64 call(WarmInstance *inst, const char *name, u64 arg) {
  const ExportDesc *e = warmc_find_export(inst, name);
  if (!e) { out("missing export "); out(name); out("\n"); return 0; }
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  iargs[0] = arg;
  FuncRef fr = warmc_export_func(inst, e);
  warmc_invoke(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
  return ret[WARMC_RET_X0];
}

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  WarmInstance *a = warmc_instantiate(&gca_module);
  warmc_register("A", a);
  WarmInstance *b = warmc_instantiate(&gcb_module);

  /* B holds three references -- two of its own and one of A's -- across a
   * call into A, which allocates.  Under WARMC_GC_STRESS every one of those
   * allocations is a whole collection, so the walk runs with B's frame and
   * A's frame both live and has to read each one's roots out of its own
   * module's pool.  4242 + 11 + 70 + 7.
   *
   * Twice, with a count that forces a collection on its own the second time
   * for the configuration that is not running under stress. */
  out("gclink_small=");  put_i32((int)(u32)call(b, "run", 4));      out("\n");
  out("gclink_large=");  put_i32((int)(u32)call(b, "run", 200000)); out("\n");

  /* A's object cast in B, against B's own structurally identical type.
   * 100 for the ref.test plus the 11 the ref.cast reaches. */
  out("gclink_cast=");   put_i32((int)(u32)call(b, "cast_borrowed", 0)); out("\n");

  /* Only when asked, because it costs two more collections. */
  if (argc >= 2 && argv[1][0] == 'r') {
    out("gclink_caught=");
    put_i32(call_catching(b, "run", 200000) != 0);
    out("\n");
    out("gclink_after=");
    put_i32((int)(u32)call(b, "run", 200000));
    out("\n");
  }
  return 0;
}
