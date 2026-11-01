/* What a JIT's runtime (built with WARMC_JIT, linked into libwarmc) finds
   in place of the program warmc links: no modules of its own, no component
   and no driver.  Its modules come from src/jit.c, compiled in the
   process, and its host is the program that loaded the library, which owns
   the entry point (rt.c has none with WARMC_JIT). */

#include "warm_rt.h"

const ModuleDesc *const *warmc_modules = 0;
const char *const *warmc_module_names = 0;
const unsigned warmc_nmodules = 0;
const ComponentDesc *const warmc_component = 0;
int (*warmc_driver_hook)(int argc, char **argv) = 0;
