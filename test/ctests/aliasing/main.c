// Memory accesses that overlap in every way: stores and loads of 1, 2, 4 and
// 8 bytes at every relative offset from -8 to +8 through two pointers into
// the same buffer that the compiler cannot prove distinct, so each load must
// see exactly the bytes the most recent stores left there.  Then store-to-
// load forwarding across a call, a dead-looking store that a later load
// through another pointer needs, read-modify-write through aliased struct
// record and array pointers, and memcpy/memmove of a region onto itself.
//
// clang lowers each small memcpy to a single unaligned load or store of that
// width, and it cannot remove any of them here because the pointers come in
// as noinline arguments; what is left is for the wasm consumer's own memory
// optimisation to get right.  A forwarded value taken from a store that was
// partly overwritten, or a store eliminated as dead when an overlapping load
// still needed it, prints a wrong hex word.  Strict aliasing is respected:
// every access goes through memcpy, a character type, or int32_t on int32_t.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char buf[64];

static inline uint16_t ld16(const unsigned char *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline uint32_t ld32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t ld64(const unsigned char *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline void st8(unsigned char *p, uint8_t v) { *p = v; }
static inline void st16(unsigned char *p, uint16_t v) { memcpy(p, &v, 2); }
static inline void st32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void st64(unsigned char *p, uint64_t v) { memcpy(p, &v, 8); }

// store wide through p, store narrow through q, load wide through p
__attribute__((noinline)) static uint64_t s64_s8_l64(unsigned char *p, unsigned char *q) {
  st64(p, 0x0102030405060708ull);
  st8(q, 0xaa);
  return ld64(p);
}
__attribute__((noinline)) static uint64_t s64_s16_l64(unsigned char *p, unsigned char *q) {
  st64(p, 0x1112131415161718ull);
  st16(q, 0xbbcc);
  return ld64(p);
}
__attribute__((noinline)) static uint64_t s64_s32_l64(unsigned char *p, unsigned char *q) {
  st64(p, 0x2122232425262728ull);
  st32(q, 0xddeeff00u);
  return ld64(p);
}
__attribute__((noinline)) static uint64_t s64_s64_l64(unsigned char *p, unsigned char *q) {
  st64(p, 0x3132333435363738ull);
  st64(q, 0x4142434445464748ull);
  return ld64(p);
}
// store through p, store through q, load narrow through p at an offset
__attribute__((noinline)) static uint32_t s32_s16_l32(unsigned char *p, unsigned char *q) {
  st32(p, 0x51525354u);
  st16(q, 0x6162);
  return ld32(p);
}
__attribute__((noinline)) static uint32_t s32_s32_l16(unsigned char *p, unsigned char *q) {
  st32(p, 0x71727374u);
  st32(q, 0x81828384u);
  return (uint32_t)ld16(p + 1) << 16 | ld16(p + 2);
}
__attribute__((noinline)) static uint32_t s16_s8_l32(unsigned char *p, unsigned char *q) {
  st16(p, 0x9192);
  st8(q, 0xa1);
  st16(p + 2, 0xb1b2);
  return ld32(p);
}
// load first, store through the other pointer, load again: the second load
// must not be replaced by the first
__attribute__((noinline)) static uint64_t l64_s32_l64(unsigned char *p, unsigned char *q) {
  uint64_t a = ld64(p);
  st32(q, ~(uint32_t)a);
  uint64_t b = ld64(p);
  return a ^ (b * 3);
}
// a store that looks dead (overwritten through p) but is read through q first
__attribute__((noinline)) static uint64_t dead_store(unsigned char *p, unsigned char *q) {
  st64(p, 0xc1c2c3c4c5c6c7c8ull);
  uint32_t seen = ld32(q);
  st64(p, 0xd1d2d3d4d5d6d7d8ull);
  return (uint64_t)seen << 32 | ld32(q);
}
// sign/zero extension of narrow loads after a wider store
__attribute__((noinline)) static int64_t ext_loads(unsigned char *p, unsigned char *q) {
  st64(p, 0x80ff7f0180fe7e02ull);
  int8_t a;
  int16_t b;
  int32_t c;
  memcpy(&a, q, 1);
  memcpy(&b, q, 2);
  memcpy(&c, q, 4);
  return (int64_t)a * 1000000 + (int64_t)b * 10 + c;
}

typedef uint64_t (*Pair64)(unsigned char *, unsigned char *);
typedef uint32_t (*Pair32)(unsigned char *, unsigned char *);

static void offsets(void) {
  printf("-- overlapping stores at relative offsets -8..+8\n");
  Pair64 f64[] = {s64_s8_l64, s64_s16_l64, s64_s32_l64, s64_s64_l64, l64_s32_l64, dead_store};
  const char *n64[] = {"s64 s8 l64", "s64 s16 l64", "s64 s32 l64", "s64 s64 l64", "l64 s32 l64", "dead store"};
  for (int f = 0; f < 6; f++) {
    printf("%s:\n", n64[f]);
    for (int d = -8; d <= 8; d++) {
      for (int i = 0; i < 64; i++) buf[i] = (unsigned char)(i * 13 + 1);
      unsigned char *p = buf + 24, *q = buf + 24 + d;
      uint64_t r = f64[f](p, q);
      uint32_t h = 0;
      for (int i = 0; i < 64; i++) h = h * 31 + buf[i];
      printf("  d=%+d -> %016llx mem %08x\n", d, (unsigned long long)r, h);
    }
  }
  Pair32 f32[] = {s32_s16_l32, s32_s32_l16, s16_s8_l32};
  const char *n32[] = {"s32 s16 l32", "s32 s32 l16x2", "s16 s8 s16 l32"};
  for (int f = 0; f < 3; f++) {
    printf("%s:", n32[f]);
    for (int d = -5; d <= 5; d++) {
      memset(buf, 0x5a, sizeof buf);
      uint32_t r = f32[f](buf + 16, buf + 16 + d);
      printf(" %08x", r);
    }
    printf("\n");
  }
  printf("ext loads:");
  for (int d = 0; d <= 7; d++) {
    memset(buf, 0, sizeof buf);
    printf(" %lld", (long long)ext_loads(buf + 8, buf + 8 + d));
  }
  printf("\n");
}

__attribute__((noinline)) static void clobber(unsigned char *x, int k) { x[k] ^= 0xff; }

static void across_calls(void) {
  printf("-- forwarding across calls\n");
  for (int k = 0; k < 10; k++) {
    for (int i = 0; i < 64; i++) buf[i] = (unsigned char)i;
    st64(buf + 8, 0x0123456789abcdefull);
    clobber(buf, k + 4);  // may or may not hit the stored bytes
    printf("k=%d %016llx %08x\n", k, (unsigned long long)ld64(buf + 8), ld32(buf + 4));
  }
}

// Fields of a 4-int record addressed by index rather than through a struct
// type: two struct lvalues that partly overlap would be undefined, and clang's
// struct-path TBAA would be entitled to assume they do not.
enum { A, B, C, D };
__attribute__((noinline)) static int32_t record_rmw(int32_t *x, int32_t *y) {
  x[A] += 1;
  y[B] += 10;  // y may be x, or x shifted by an int or two
  x[B] += 100;
  y[A] *= 2;
  return x[A] * 7 + x[B] * 3 + x[C] + y[D];
}

__attribute__((noinline)) static int64_t array_rmw(int32_t *a, int32_t *b, int n) {
  for (int i = 0; i < n; i++) a[i] += b[i];  // b may overlap a at an offset
  int64_t s = 0;
  for (int i = 0; i < n + 4; i++) s = s * 3 + a[i];
  return s;
}

static void read_modify_write(void) {
  printf("-- read-modify-write through aliases\n");
  static int32_t mem[32];
  for (int shift = -2; shift <= 2; shift++) {
    for (int i = 0; i < 32; i++) mem[i] = i * 5 - 20;
    int32_t r = record_rmw(&mem[8], &mem[8 + shift]);
    printf("shift %+d: r=%d mem[6..13]=", shift, r);
    for (int i = 6; i < 14; i++) printf("%d ", mem[i]);
    printf("\n");
  }
  for (int shift = -3; shift <= 3; shift++) {
    for (int i = 0; i < 32; i++) mem[i] = i * i - 50;
    int64_t s = array_rmw(&mem[10], &mem[10 + shift], 8);
    printf("array shift %+d: %lld\n", shift, (long long)s);
  }
}

static void self_copy(void) {
  printf("-- copies onto themselves\n");
  for (int i = 0; i < 64; i++) buf[i] = (unsigned char)(i + 100);
  memmove(buf + 3, buf + 3, 20);
  memcpy(buf + 40, buf + 40, 0);
  uint32_t h = 0;
  for (int i = 0; i < 64; i++) h = h * 31 + buf[i];
  printf("unchanged hash %08x\n", h);
  for (int step = 1; step <= 7; step += 2) {
    for (int i = 0; i < 64; i++) buf[i] = (unsigned char)(i + 100);
    for (int i = 0; i + step + 8 <= 64; i += 8) st64(buf + i + step, ld64(buf + i));  // overlapping chain
    h = 0;
    for (int i = 0; i < 64; i++) h = h * 31 + buf[i];
    printf("chain step %d: %08x first %016llx\n", step, h, (unsigned long long)ld64(buf));
  }
}

int main(void) {
  offsets();
  across_calls();
  read_modify_write();
  self_copy();
  return 0;
}
