// Floats converted to IEEE half precision and back in software, reading their
// bits through a union and rounding to nearest even by hand: what a program
// that stores half-float textures, audio or model weights does, since core
// wasm has no f16 conversion.
//
// The union becomes i32.reinterpret_f32 and f32.reinterpret_i32, and the
// rounding is integer shifts, masks and compares with a branch per class --
// normal, subnormal, overflow, infinity, NaN -- over inputs spread so that
// every one is taken; widening a subnormal back uses i32.clz.  The fixed-point
// quantisation after it goes float to int with i32.trunc_sat_f32_s and back
// with f32.convert_i32_s.  rsbench/f32ops is single-precision arithmetic; this
// is the bit-level traffic between f32 and i32 that sits around such code.
#include <stdio.h>
#include <stdint.h>

typedef union { float f; uint32_t u; } Bits;

// Round to nearest, ties to even, with subnormals, overflow to infinity and
// NaN payloads kept quiet.
static uint16_t f2h(float f) {
  Bits v = { f };
  uint32_t sign = (v.u >> 16) & 0x8000u, ax = v.u & 0x7fffffffu;
  if (ax >= 0x7f800000u)
    return (uint16_t)(sign | 0x7c00u | (ax > 0x7f800000u ? 0x200u | ((ax >> 13) & 0x3ffu) : 0));
  if (ax >= 0x477ff000u) return (uint16_t)(sign | 0x7c00u);
  if (ax < 0x38800000u) {
    if (ax < 0x33000000u) return (uint16_t)sign;
    uint32_t e = ax >> 23, m = (ax & 0x7fffffu) | 0x800000u, sh = 126 - e;
    uint32_t r = m >> sh, rem = m & ((1u << sh) - 1), half = 1u << (sh - 1);
    if (rem > half || (rem == half && (r & 1))) r++;
    return (uint16_t)(sign | r);
  }
  uint32_t r = ax - 0x38000000u, res = r >> 13, rem = r & 0x1fffu;
  if (rem > 0x1000u || (rem == 0x1000u && (res & 1))) res++;
  return (uint16_t)(sign | res);
}

static float h2f(uint16_t h) {
  uint32_t sign = (uint32_t)(h & 0x8000u) << 16, e = (h >> 10) & 0x1fu, m = h & 0x3ffu;
  Bits v;
  if (e == 0) {
    if (m == 0) {
      v.u = sign;
    } else {
      uint32_t sh = (uint32_t)__builtin_clz(m) - 21;
      v.u = sign | ((113 - sh) << 23) | (((m << sh) & 0x3ffu) << 13);
    }
  } else if (e == 31) {
    v.u = sign | 0x7f800000u | (m << 13);
  } else {
    v.u = sign | ((e + 112) << 23) | (m << 13);
  }
  return v.f;
}

enum { N = 1 << 15, PASSES = 330 };
static float src[N];
static uint16_t packed[N];

int main(void) {
  uint32_t x = 1234567u;
  for (int i = 0; i < N; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    // Exponents from 2^-30 to 2^17, so every rounding path is taken; a few
    // infinities and NaNs among them.
    uint32_t exp = 97 + (x >> 8) % 48;
    Bits v = { .u = (x & 0x807fffffu) | (exp << 23) };
    if ((x & 0x3ff0) == 0) v.u |= 0x7f800000u;
    src[i] = v.f;
  }
  uint64_t bits = 0;
  int64_t quant = 0;
  double err = 0;
  uint32_t inf = 0, sub = 0;
  for (int p = 0; p < PASSES; p++) {
    float scale = 1.0f + (float)p * 0.0371f;
    // A NaN goes in unscaled: arithmetic on one may change its payload.
    for (int i = 0; i < N; i++) {
      Bits b = { src[i] };
      packed[i] = f2h((b.u & 0x7fffffffu) > 0x7f800000u ? src[i] : src[i] * scale);
    }
    for (int i = 0; i < N; i++) {
      uint16_t h = packed[i];
      bits = bits * 31 + h;
      if ((h & 0x7c00u) == 0x7c00u) { inf++; continue; }
      float g = h2f(h), want = src[i] * scale;
      if ((h & 0x7c00u) == 0) sub++;
      err += (double)(g - want);
      // Float to integer and back: a fixed-point quantisation of the value.
      int32_t q = (int32_t)(g * 64.0f);
      quant += q;
      float back = (float)q * (1.0f / 64.0f);
      quant ^= (int32_t)((g - back) * 1048576.0f);
      packed[i] = (uint16_t)(h ^ (uint16_t)q);
    }
    for (int i = 0; i < N; i += 64) bits += packed[i];
  }
  printf("bits = %016llx quant = %lld err = %.9e inf = %u sub = %u\n",
         (unsigned long long)bits, (long long)quant, err, inf, sub);
  return 0;
}
