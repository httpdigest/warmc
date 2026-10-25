/* The C side of test/native/natives.wat: functions a module calls as
   --native-module imports, each answering with something that shows where
   every argument arrived. */
#include <stdint.h>
#include "warm_rt.h"

/* registers alone */
int32_t nat_i32(int32_t a, int32_t b) { return a * 10 + b; }
int64_t nat_i64(int64_t a, int32_t b, int64_t c) { return a - (int64_t)b * c; }
float nat_f32(float a, float b) { return a / b; }
double nat_f64(double a, float b, double c) { return a * b + c; }
void nat_void(void) {}

/* nine of each kind and more: the rest on the stack, packed by size on
   Apple's convention, eight bytes each on the others */
int64_t nat_ints(int32_t a, int64_t b, int32_t c, int32_t d, int64_t e,
                 int32_t f, int32_t g, int64_t h, int32_t i, int64_t j,
                 int32_t k, int32_t l, int64_t m) {
  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i +
         10 * j + 11 * k + 12 * l + 13 * m;
}

double nat_floats(float a, double b, float c, double d, float e, double f,
                  float g, double h, float i, double j, float k, double l) {
  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i +
         10 * j + 11 * k + 12 * l;
}

double nat_mixed(int32_t a, float b, int64_t c, double d, int32_t e, float f,
                 int32_t g, int32_t h, int32_t i, int32_t j, int32_t k,
                 int64_t l, float m, double n, float o, double p, float q,
                 double r, float s, int32_t t) {
  return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q +
         r + s + t * 1000.0;
}

/* writes every caller-saved register a C function may, so that a value the
   module keeps across the call has to have been saved */
int32_t nat_clobber(int32_t x) {
  volatile double d[24];
  volatile int64_t v[18];
  for (int k = 0; k < 24; k++) d[k] = x + k;
  for (int k = 0; k < 18; k++) v[k] = x * k;
  double s = 0;
  for (int k = 0; k < 24; k++) s += d[k];
  for (int k = 0; k < 18; k++) s += (double)v[k];
  return (int32_t)s;
}

/* back into the module: its export "back" for each of n, which may call
   nat_i32 itself */
int32_t nat_call_back(int32_t n) {
  WarmInstance *app = warmc_wasi_instance;
  if (!app) return -1;
  const ExportDesc *ex = warmc_find_export(app, "back");
  if (!ex) return -2;
  FuncRef fr = warmc_export_func(app, ex);
  int32_t sum = 0;
  for (int32_t i = 0; i < n; i++) {
    u64 iargs[8] = { (u64)(u32)i }, fargs[WARMC_FARGS_WORDS] = { 0 };
    u64 ret[WARMC_RETBUF_WORDS] = { 0 };
    warmc_invoke(fr.code, (WarmInstance *)fr.instance, iargs, fargs, ret);
    sum += (int32_t)ret[WARMC_RET_X0];
  }
  return sum;
}

/* the addresses warmc_call imports call through */
static int32_t bumps;
void nat_bump(void) { bumps++; }
int32_t nat_bumps(void) { return bumps; }
int64_t nat_addr(int32_t which) {
  switch (which) {
    case 0: return (int64_t)(intptr_t)nat_i32;
    case 1: return (int64_t)(intptr_t)nat_f64;
    case 2: return (int64_t)(intptr_t)nat_mixed;
    case 3: return (int64_t)(intptr_t)nat_bump;
    case 4: return (int64_t)(intptr_t)nat_f32;
    case 5: return (int64_t)(intptr_t)nat_i64;
    default: return 0;
  }
}

/* linear memory, through the native address warmc_memory_base gives it */
int32_t nat_read(const int32_t *p) { return *p; }
void nat_write(int32_t *p, int32_t v) { *p = v; }
