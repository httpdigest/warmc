// Register pressure: functions that keep many values live at once -- 24
// int32s in one, 16 doubles, 12 floats and three int64s in another -- across
// calls to noinline helpers, so the native code has to spill some and reload them
// after each call; functions with many parameters (more than fit in argument
// registers on either host ABI, mixing integer and floating ones so that
// both register files overflow into stack-passed arguments); deep expression
// trees whose subexpressions are all needed at the end; and a loop whose body
// keeps a dozen accumulators of different types.
//
// wasm locals have no limit, so the pressure lands on the AOT compiler's
// register allocator and calling-convention code: a value reloaded from the
// wrong spill slot, a callee-saved register not preserved, or a stack
// argument read from the wrong offset changes the printed sums.
#include <stdint.h>
#include <stdio.h>

static volatile int vseed = 3;

__attribute__((noinline)) static int32_t touch_i(int32_t x) { return x * 3 + vseed; }
__attribute__((noinline)) static double touch_d(double x) { return x * 0.5 + vseed; }
__attribute__((noinline)) static int64_t touch_l(int64_t x) { return x ^ (int64_t)vseed << 40; }

__attribute__((noinline)) static uint64_t many_ints(int32_t s) {
  int32_t a0 = s + 1, a1 = s * 2, a2 = s - 3, a3 = s ^ 0x55, a4 = s * s, a5 = s + 100, a6 = s * 7 - 1,
          a7 = s >> 1, a8 = s | 0x100, a9 = s & 0xff, a10 = -s, a11 = s * 11, a12 = s + 12, a13 = s * 13,
          a14 = s - 14, a15 = s * 15, a16 = s ^ 16, a17 = s + 17, a18 = s * 18, a19 = s - 19, a20 = s * 20,
          a21 = s ^ 21, a22 = s + 22, a23 = s * 23;
  for (int r = 0; r < 4; r++) {
    a0 = touch_i(a0 + a23);
    a1 += touch_i(a2) & 0xffff;
    a3 ^= touch_i(a4);
    a5 -= touch_i(a6) >> 3;
    a7 = touch_i(a7) + a8;
    a9 += a10 * 3;
    a11 ^= touch_i(a12 + a13);
    a14 = a14 * 5 + touch_i(a15) % 1000;
    a16 += a17 - a18 % 77;
    a19 = touch_i(a19 % 10000);
    a20 ^= (int32_t)((uint32_t)a21 << 2);  // a21 may be negative
    a22 += touch_i(a23 & 0xfff);
    a23 = a23 % 100000 + a0 % 1000;
  }
  uint64_t sum = 0;
  int32_t all[24] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19,
                     a20, a21, a22, a23};
  for (int i = 0; i < 24; i++) {
    printf("%d%c", all[i], i % 12 == 11 ? '\n' : ' ');
    sum = sum * 7 + (uint32_t)all[i];
  }
  return sum;
}

__attribute__((noinline)) static double many_floats(double s) {
  double d0 = s, d1 = s * 1.5, d2 = s - 0.25, d3 = s * s, d4 = 1 / (s + 1), d5 = s + 5, d6 = s * 6, d7 = s - 7,
         d8 = s * 0.125, d9 = s + 9.5, d10 = -s, d11 = s * 11, d12 = s / 12, d13 = s + 13, d14 = s * 1.4,
         d15 = s - 15;
  float f0 = (float)s, f1 = (float)(s * 2), f2 = (float)(s + 0.1), f3 = 3.5f, f4 = (float)(s / 4), f5 = 5.25f,
        f6 = (float)(s * 6), f7 = -7.0f, f8 = (float)(s - 8), f9 = 9.75f, f10 = (float)(s * 0.1), f11 = 11.0f;
  int64_t l0 = (int64_t)(s * 1e6), l1 = 1ll << 40, l2 = -3;
  for (int r = 0; r < 3; r++) {
    d0 = touch_d(d0 + d15);
    d1 += touch_d(d2) * 0.5;
    d3 -= touch_d(d4);
    d5 = d5 * 0.9 + touch_d(d6);
    d7 += d8 * d9;
    d10 = touch_d(d10 - d11);
    d12 += d13 / (d14 + 100);
    d15 = touch_d(d15) - d1;
    f0 += f1 * 0.5f;
    f2 = f2 * f3 - f4;
    f5 += (float)touch_d(f6);
    f7 -= f8 * f9;
    f10 = f10 * 1.5f + f11;
    l0 = touch_l(l0 + l1);
    l1 += l2 * r;
    l2 = touch_l(l2) % 1000003;
  }
  double fs = (double)f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8 + f9 + f10 + f11;
  double ds = d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8 + d9 + d10 + d11 + d12 + d13 + d14 + d15;
  printf("d: %.17g %.17g %.17g %.17g %.17g %.17g\n", d0, d1, d3, d5, d10, d15);
  printf("f: %.9g %.9g %.9g %.9g %.9g\n", (double)f0, (double)f2, (double)f5, (double)f7, (double)f10);
  printf("l: %lld %lld %lld\n", (long long)l0, (long long)l1, (long long)l2);
  return ds + fs + (double)(l0 % 1000) + (double)l2;
}

// 20 parameters: on both x86-64 SysV/Win64 and AArch64 some go on the stack.
__attribute__((noinline)) static double many_params(int a, double b, int64_t c, float d, int e, double f, int64_t g,
                                                    float h, int i, double j, int64_t k, float l, int m, double n,
                                                    int64_t o, float p, uint8_t q, int16_t r, double s, int64_t t) {
  printf("params: %d %g %lld %g %d %g %lld %g %d %g %lld %g %d %g %lld %g %u %d %g %lld\n", a, b, (long long)c,
         (double)d, e, f, (long long)g, (double)h, i, j, (long long)k, (double)l, m, n, (long long)o, (double)p, q,
         r, s, (long long)t);
  return a + b + (double)c + d + e + f + (double)g + h + i + j + (double)k + l + m + n + (double)o + p + q + r + s +
         (double)t;
}

__attribute__((noinline)) static int64_t many_params_int(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                                                         int64_t f, int64_t g, int64_t h, int64_t i, int64_t j,
                                                         int32_t k, int32_t l, int32_t m, int32_t n) {
  return a - b + c * 2 - d + e * 3 - f + g * 4 - h + i * 5 - j + k * 6 - l + m * 7 - n;
}

// A deep expression tree: every leaf is computed from the inputs and all are
// combined only at the root.
__attribute__((noinline)) static uint64_t tree_expr(uint64_t x, uint64_t y) {
  uint64_t a = x * 3 + y, b = x ^ (y << 7), c = (x >> 3) * (y | 1), d = x + y * y, e = (x * 0x9e37) ^ y,
           f = y - x, g = x * x - y, h = (x | y) * 5, i = (x & y) + 77, j = x << (y & 15), k = y >> (x & 15),
           l = x * 31 + y * 17;
  return ((a * b + c) ^ (d * e - f)) + ((g ^ h) * (i + j)) - ((k * l) ^ (a + l)) + (b * g) - (c ^ k) + (e * i) +
         (f * h * j);
}

static void accumulators(void) {
  int8_t c8 = 0;
  uint8_t u8 = 1;
  int16_t c16 = 0;
  uint16_t u16 = 1;
  int32_t c32 = 0;
  uint32_t u32 = 1;
  int64_t c64 = 0;
  uint64_t u64 = 1;
  float fl = 0;
  double db = 0;
  double prod = 1;
  int flips = 0;
  for (int i = 1; i <= 500; i++) {
    c8 = (int8_t)(c8 + i);
    u8 = (uint8_t)(u8 * 3 + i);
    c16 = (int16_t)(c16 - i * 7);
    u16 = (uint16_t)(u16 * 31 + (unsigned)i);
    c32 += i * (i & 1 ? 1 : -1);
    u32 = u32 * 2654435761u + (unsigned)i;
    c64 += (int64_t)i * i * i;
    u64 = u64 * 6364136223846793005ull + (uint64_t)i;
    fl += 1.0f / (float)i;
    db += 1.0 / ((double)i * i);
    prod *= 1.0 + 1.0 / (i * 100.0);
    flips += (u32 >> 31) != (u64 >> 63);
  }
  printf("acc: %d %u %d %u %d %u %lld %llu %.9g %.17g %.17g %d\n", c8, u8, c16, u16, c32, u32, (long long)c64,
         (unsigned long long)u64, (double)fl, db, prod, flips);
}

int main(void) {
  printf("-- many live ints\n");
  for (int s = -2; s <= 4; s += 3) printf("sum=%llu\n", (unsigned long long)many_ints(s * 1000 + vseed));
  printf("-- many live floats\n");
  for (int s = 1; s <= 3; s++) printf("total=%.17g\n", many_floats(s * 1.75 + vseed));
  printf("-- many parameters\n");
  for (int s = 0; s < 3; s++) {
    double r = many_params(s + vseed, 1.5 * s, (int64_t)s << 33, 2.25f * (float)s, -s, 3.125, -((int64_t)s << 35),
                           0.5f, s * 100, -2.5 * s, 77ll * s, -1.25f, 1000 + s, 1e-3 * s, (int64_t)s * 1000000007,
                           8.0f, (uint8_t)(250 + s), (int16_t)(-30000 - s), 6.0 * vseed, -(int64_t)s);
    printf("sum=%.17g\n", r);
  }
  for (int s = 0; s < 4; s++)
    printf("int params=%lld\n", (long long)many_params_int(s, s * 2, s * 3, (int64_t)s << 40, -s, 5, 6, 7, 8, 9,
                                                          s + vseed, -100, 1 << 20, s * s));
  printf("-- expression tree\n");
  for (uint64_t x = 1; x < 1000000; x = x * 37 + 11)
    printf("%016llx\n", (unsigned long long)tree_expr(x, x * 0x12345 + (uint64_t)vseed));
  printf("-- accumulators\n");
  accumulators();
  return 0;
}
