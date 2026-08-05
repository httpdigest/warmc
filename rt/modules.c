#include "warm_rt.h"

static const ModuleDesc *const one[] = { &warmc_module };
static const char *const one_name[] = { "" };

const ModuleDesc *const *warmc_modules = one;
const char *const *warmc_module_names = one_name;
const unsigned warmc_nmodules = 1;
