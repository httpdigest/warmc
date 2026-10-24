/* Driver for the memory-maximum pair: instantiate A, register it, and
 * instantiate B and C against it, saying of each whether it linked. */
#include "../../rt/warm_rt.h"

extern const ModuleDesc mma_module;
extern const ModuleDesc mmb_module;
extern const ModuleDesc mmc_module;

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
  u32 e = 0;
  warmc_register("A", warmc_instantiate(&mma_module));
  out(warmc_instantiate_catching(&mmb_module, &e) ? "b=linked\n" : "b=refused\n");
  out(warmc_instantiate_catching(&mmc_module, &e) ? "c=linked\n" : "c=refused\n");
  return 0;
}
