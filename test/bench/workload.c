/* A self-contained C workload compiled to wasm32 and then to arm64 by warmc.
 * No libc: everything it touches is linear memory and arithmetic, so it
 * exercises real compiler output rather than hand-written wat. */

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef int i32;
typedef long long i64;

#define EXPORT __attribute__((visibility("default"), used))

static u8 heap[1 << 16];

/* ---------------- SHA-256, a dense mix of rotates, shifts and adds ------- */

static const u32 K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
  0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
  0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
  0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

static u32 ror(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(u32 *h, const u8 *p) {
  u32 w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((u32)p[4 * i] << 24) | ((u32)p[4 * i + 1] << 16) |
           ((u32)p[4 * i + 2] << 8) | (u32)p[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    u32 s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
    u32 s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  u32 a = h[0], b = h[1], c = h[2], d = h[3];
  u32 e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    u32 S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
    u32 ch = (e & f) ^ (~e & g);
    u32 t1 = hh + S1 + ch + K[i] + w[i];
    u32 S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
    u32 maj = (a & b) ^ (a & c) ^ (b & c);
    u32 t2 = S0 + maj;
    hh = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d;
  h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

EXPORT u32 sha_rounds(u32 blocks) {
  u32 h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
  for (u32 i = 0; i < (1 << 16); i++) heap[i] = (u8)(i * 31 + 7);
  for (u32 b = 0; b < blocks; b++) sha256_block(h, heap + ((b * 64) & 0xFFC0));
  return h[0] ^ h[1] ^ h[2] ^ h[3] ^ h[4] ^ h[5] ^ h[6] ^ h[7];
}

/* ---------------- sorting: branches, swaps, recursion ------------------- */

static void isort(i32 *a, int lo, int hi) {
  for (int i = lo + 1; i <= hi; i++) {
    i32 v = a[i];
    int j = i - 1;
    while (j >= lo && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
}

static void qsort_(i32 *a, int lo, int hi) {
  while (lo < hi) {
    if (hi - lo < 12) { isort(a, lo, hi); return; }
    i32 pivot = a[(lo + hi) / 2];
    int i = lo, j = hi;
    while (i <= j) {
      while (a[i] < pivot) i++;
      while (a[j] > pivot) j--;
      if (i <= j) { i32 t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
    }
    if (j - lo < hi - i) { qsort_(a, lo, j); lo = i; }
    else { qsort_(a, i, hi); hi = j; }
  }
}

EXPORT u32 sort_check(u32 n) {
  if (n > 8192) n = 8192;
  i32 *a = (i32 *)heap;
  u32 x = 123456789;
  for (u32 i = 0; i < n; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    a[i] = (i32)x;
  }
  qsort_(a, 0, (int)n - 1);
  u32 sum = 0;
  int ordered = 1;
  for (u32 i = 0; i < n; i++) {
    sum = sum * 31 + (u32)a[i];
    if (i && a[i - 1] > a[i]) ordered = 0;
  }
  return ordered ? sum : 0;
}

/* ---------------- floating point: matrix multiply ----------------------- */

EXPORT i64 matmul(u32 n) {
  if (n > 48) n = 48;
  double *A = (double *)heap;
  double *B = A + 48 * 48;
  double *C = B + 48 * 48;
  for (u32 i = 0; i < n * n; i++) {
    A[i] = (double)(i % 17) * 0.5 - 3.0;
    B[i] = (double)(i % 23) * 0.25 + 1.0;
  }
  for (u32 i = 0; i < n; i++)
    for (u32 j = 0; j < n; j++) {
      double s = 0.0;
      for (u32 k = 0; k < n; k++) s += A[i * n + k] * B[k * n + j];
      C[i * n + j] = s;
    }
  double acc = 0.0;
  for (u32 i = 0; i < n * n; i++) acc += C[i] * (double)((i & 7) + 1);
  i64 bits;
  __builtin_memcpy(&bits, &acc, 8);
  return bits;
}

/* ---------------- 64-bit integer mixing --------------------------------- */

EXPORT i64 mix64(u32 rounds) {
  u64 s = 0x243F6A8885A308D3ull;
  for (u32 i = 0; i < rounds; i++) {
    s ^= s >> 33;
    s *= 0xff51afd7ed558ccdull;
    s ^= s << 17;
    s = (s << 41) | (s >> 23);
    s += (u64)i * 0x9E3779B97F4A7C15ull;
    if ((s & 0xFF) == 0) s = ~s;
    s /= ((s & 0x7) + 1);
  }
  return (i64)s;
}

/* ---------------- indirect dispatch ------------------------------------- */

static i32 op_add(i32 a, i32 b) { return a + b; }
static i32 op_sub(i32 a, i32 b) { return a - b; }
static i32 op_mul(i32 a, i32 b) { return a * b; }
static i32 op_xor(i32 a, i32 b) { return a ^ b; }

typedef i32 (*binop)(i32, i32);
static binop ops[4] = { op_add, op_sub, op_mul, op_xor };

EXPORT i32 dispatch(u32 n) {
  i32 acc = 1;
  for (u32 i = 0; i < n; i++) acc = ops[i & 3](acc, (i32)i + 1);
  return acc;
}

/* ---------------- n-body: float-heavy nested loops ---------------------- */

#define NBODY 12
EXPORT i64 nbody(u32 steps) {
  double *x = (double *)heap, *y = x + NBODY, *z = y + NBODY;
  double *vx = z + NBODY, *vy = vx + NBODY, *vz = vy + NBODY;
  double *mass = vz + NBODY;
  for (int i = 0; i < NBODY; i++) {
    x[i] = (double)i * 1.5 - 8.0;
    y[i] = (double)(i * 7 % 11) - 5.0;
    z[i] = (double)(i * 3 % 5) - 2.0;
    vx[i] = vy[i] = vz[i] = 0.0;
    mass[i] = 1.0 + (double)(i % 3) * 0.5;
  }
  const double dt = 0.001;
  for (u32 s = 0; s < steps; s++) {
    for (int i = 0; i < NBODY; i++) {
      double ax = 0.0, ay = 0.0, az = 0.0;
      for (int j = 0; j < NBODY; j++) {
        if (i == j) continue;
        double dx = x[j] - x[i], dy = y[j] - y[i], dz = z[j] - z[i];
        double d2 = dx * dx + dy * dy + dz * dz + 0.01;
        double inv = mass[j] / (d2 * __builtin_sqrt(d2));
        ax += dx * inv; ay += dy * inv; az += dz * inv;
      }
      vx[i] += ax * dt; vy[i] += ay * dt; vz[i] += az * dt;
    }
    for (int i = 0; i < NBODY; i++) {
      x[i] += vx[i] * dt; y[i] += vy[i] * dt; z[i] += vz[i] * dt;
    }
  }
  double acc = 0.0;
  for (int i = 0; i < NBODY; i++) acc += x[i] + y[i] * 2.0 + z[i] * 3.0;
  i64 bits;
  __builtin_memcpy(&bits, &acc, 8);
  return bits;
}

/* ---------------- byte-level work: loads, stores, bounds checks --------- */

EXPORT u32 strings(u32 rounds) {
  u8 *buf = heap;
  u32 h = 2166136261u;
  for (u32 i = 0; i < 4096; i++) buf[i] = (u8)(i * 131 + 17);
  for (u32 r = 0; r < rounds; r++) {
    u32 base = (r * 37) & 0xFFF;
    for (u32 i = 0; i < 64; i++) {
      u8 c = buf[(base + i) & 0xFFF];
      h ^= c;
      h *= 16777619u;
      buf[(base + i + 1024) & 0xFFF] = (u8)(h >> 13);
    }
  }
  return h;
}

/* ---------------- integer bit manipulation ------------------------------ */

EXPORT u32 bitops(u32 n) {
  u32 acc = 0x12345678u;
  for (u32 i = 0; i < n; i++) {
    acc = (acc << 7) | (acc >> 25);
    acc ^= i * 2654435761u;
    acc += __builtin_popcount(acc) * 3u;
    acc ^= __builtin_clz(acc | 1u);
    acc = acc * 5u + 1u;
    acc &= 0x7FFFFFFFu;
    acc |= (acc >> 16) & 0xFFu;
  }
  return acc;
}

/* A do-nothing export so the harness can subtract process start-up cost. */
EXPORT u32 startup(u32 x) { return x; }
