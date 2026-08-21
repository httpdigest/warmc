/* Driver for the TinyGo end-to-end test: enough of wasi_snapshot_preview1 to
 * run a real Go program and let it print.
 *
 * The shape: the stub module (all `unreachable`) is registered under the
 * WASI name so instantiation can link and type-check the imports, and the
 * three calls the program actually makes are then rebound to the C
 * functions below.  Each one receives the *guest* instance in x0 -- that is
 * what `warmc_bind_import` was told to pass -- because a WASI pointer is an
 * offset into the guest's memory and `vm->mem_base` is where that memory
 * is.  Anything the program imports but the driver never bound still traps
 * in the stub, which is the right noise for an unimplemented syscall.
 */
#include "../../rt/warm_rt.h"

extern const ModuleDesc app_module;
extern const ModuleDesc wasistub_module;

/* The CLI harness in the runtime refers to this; a driver replaces the
 * harness but not the reference. */
const ModuleDesc warmc_module = { 0 };

static void out(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(2, s, (usz)(p - s));
}

/* ---- the syscalls ----------------------------------------------------- */

/* Every parameter is a u64 holding what the caller left in the register;
 * an i32 argument only promises its low half, so everything is masked
 * before use.  Returns are WASI errno values: 0 for success, 21 (EFAULT)
 * for a pointer that leaves the guest's memory. */

static u64 wasi_fd_write(WarmInstance *vm, u64 fd_, u64 iovs_, u64 niov_,
                         u64 pnwritten_) {
  u8 *m = vm->mem_base;
  u64 size = vm->mem_size;
  u32 fd = (u32)fd_, iovs = (u32)iovs_, niov = (u32)niov_;
  u32 pnwritten = (u32)pnwritten_;
  u64 total = 0;
  for (u32 i = 0; i < niov; i++) {
    u64 rec = (u64)iovs + 8ull * i;
    if (rec + 8 > size) return 21;
    u32 buf, len;
    __builtin_memcpy(&buf, m + rec, 4);
    __builtin_memcpy(&len, m + rec + 4, 4);
    if ((u64)buf + len > size) return 21;
    if (fd == 1 || fd == 2) warmc_rt_write((int)fd, m + buf, len);
    total += len;
  }
  if ((u64)pnwritten + 4 > size) return 21;
  u32 t32 = (u32)total;
  __builtin_memcpy(m + pnwritten, &t32, 4);
  return 0;
}

static u64 wasi_proc_exit(WarmInstance *vm, u64 code) {
  (void)vm;
  warmc_rt_exit((int)(u32)code);
}

static u64 wasi_random_get(WarmInstance *vm, u64 buf_, u64 len_) {
  u8 *m = vm->mem_base;
  u32 buf = (u32)buf_, len = (u32)len_;
  if ((u64)buf + len > vm->mem_size) return 21;
  /* Deterministic on purpose: the golden output must not depend on what a
   * map picked for its seed, and the program prints nothing that does. */
  for (u32 i = 0; i < len; i++) m[buf + i] = (u8)(0xa5 ^ (i * 7));
  return 0;
}

/* ---- the run ---------------------------------------------------------- */

static int drive(int argc, char **argv) {
  (void)argc; (void)argv;
  u32 err = 0;
  WarmInstance *stub = warmc_instantiate_catching(&wasistub_module, &err);
  if (!stub) { out("tinygo driver: wasi stub failed to instantiate\n"); return 100; }
  warmc_register("wasi_snapshot_preview1", stub);

  WarmInstance *app = warmc_instantiate_catching(&app_module, &err);
  if (!app) { out("tinygo driver: app failed to instantiate or link\n"); return 101; }

  if (!warmc_bind_import(app, "wasi_snapshot_preview1", "fd_write",
                         (void *)wasi_fd_write, app) ||
      !warmc_bind_import(app, "wasi_snapshot_preview1", "proc_exit",
                         (void *)wasi_proc_exit, app) ||
      !warmc_bind_import(app, "wasi_snapshot_preview1", "random_get",
                         (void *)wasi_random_get, app)) {
    out("tinygo driver: an expected import is missing\n");
    return 102;
  }

  const ExportDesc *e = warmc_find_export(app, "_start");
  if (!e) { out("tinygo driver: no _start export\n"); return 103; }
  FuncRef fr = warmc_export_func(app, e);
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  u32 trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance,
                                   iargs, fargs, ret);
  if (trap) {
    out("tinygo driver: trap: ");
    out(warmc_trap_name(trap));
    out("\n");
    return 104;
  }
  return 0;
}

int (*warmc_driver_hook)(int argc, char **argv) = drive;
