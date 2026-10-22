// Unions, bitfields and explicit layout: reading a union member other than the
// one last written (defined in C as reinterpreting the bytes), unions of
// floats and integers used to take a number apart, tagged unions dispatched on
// their tag, signed and unsigned bitfields of every width from 1 to 32 with
// read-modify-write of neighbours that share a word, bitfields that straddle
// byte boundaries, zero-width fields forcing alignment, and packed and
// over-aligned structs with their sizes and offsets.
//
// A bitfield store is a load of the containing word, an and-not, an or of the
// shifted field and a store; a signed field read is a shift left and an
// arithmetic shift right.  packed members are accessed with unaligned loads.
// Each step prints the whole containing word as well as the fields, so a
// field written over its neighbour shows up immediately.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef union {
  float f;
  uint32_t u;
  int32_t i;
  uint8_t b[4];
  uint16_t h[2];
} U32;

typedef union {
  double d;
  uint64_t u;
  uint32_t w[2];
  struct { uint64_t mant : 52; uint64_t exp : 11; uint64_t sign : 1; } parts;
} U64;

typedef struct {
  unsigned a : 1;
  unsigned b : 3;
  signed c : 4;
  unsigned d : 7;
  signed e : 9;
  unsigned f : 8;
} Bits;

typedef struct {
  uint64_t lo : 20;
  int64_t mid : 30;
  uint64_t hi : 14;
} Bits64;

typedef struct {
  unsigned char x : 3;
  unsigned char : 0;  // start a new unit
  unsigned char y : 5;
  unsigned short z : 12;
  signed int w : 1;
} Mixed;

typedef struct __attribute__((packed)) {
  uint8_t tag;
  uint32_t value;
  uint16_t extra;
  uint64_t big;
} Packed;

typedef struct {
  uint8_t tag;
  _Alignas(16) uint32_t value;
  uint8_t after;
} Aligned;

typedef struct __attribute__((aligned(32))) { int x; } Big;

typedef enum { T_INT, T_DBL, T_STR, T_PAIR } Tag;
typedef struct {
  Tag tag;
  union {
    long long i;
    double d;
    const char *s;
    struct { int a, b; } pair;
  } as;
} Value;

static void punning(void) {
  printf("-- union reinterpretation\n");
  float fs[] = {0.0f, -0.0f, 1.0f, -2.5f, 0.1f, 3.4e38f, 1e-40f, 65504.0f};
  for (unsigned i = 0; i < sizeof fs / sizeof fs[0]; i++) {
    U32 u;
    u.f = fs[i];
    printf("f=%-12g u=%08x i=%d bytes=%02x %02x %02x %02x halves=%04x %04x\n",
           (double)u.f, u.u, u.i, u.b[0], u.b[1], u.b[2], u.b[3], u.h[0],
           u.h[1]);
    u.b[3] ^= 0x80;  // flip the sign through the byte view
    printf("   negated via byte: %g\n", (double)u.f);
  }
  double ds[] = {1.0, -1.5, 1e-310, 6.02214076e23, 0.0};
  for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
    U64 u;
    u.d = ds[i];
    printf("d=%-14g u=%016llx w=%08x,%08x sign=%u exp=%u mant=%013llx\n", u.d,
           (unsigned long long)u.u, u.w[0], u.w[1], (unsigned)u.parts.sign,
           (unsigned)u.parts.exp, (unsigned long long)u.parts.mant);
    u.parts.exp = 1023 + 4;
    printf("   with exp 2^4: %g\n", u.d);
  }
}

static void print_bits(const char *what, const Bits *b) {
  uint32_t w;
  memcpy(&w, b, 4);
  printf("%-8s a=%u b=%u c=%d d=%u e=%d f=%u word=%08x\n", what, b->a, b->b,
         b->c, b->d, b->e, b->f, w);
}

static void bitfields(void) {
  printf("-- bitfields\n");
  printf("sizeof Bits=%zu Bits64=%zu Mixed=%zu\n", sizeof(Bits), sizeof(Bits64),
         sizeof(Mixed));
  Bits b;
  memset(&b, 0, sizeof b);
  print_bits("zero", &b);
  b.a = 1;
  print_bits("a=1", &b);
  b.b = 5;
  print_bits("b=5", &b);
  b.c = -3;
  print_bits("c=-3", &b);
  b.d = 100;
  print_bits("d=100", &b);
  b.e = -200;
  print_bits("e=-200", &b);
  b.f = 255;
  print_bits("f=255", &b);
  for (int i = 0; i < 20; i++) {
    b.a ^= 1;
    b.b += 3;      // wraps modulo 8
    b.c = (signed)((b.c + 5 + 8) % 16) - 8;  // stays in [-8, 7]
    b.d = (b.d * 5 + 1) & 127;
    b.e = b.e > 200 ? -255 : b.e + 37;
    b.f -= 17;
    print_bits("step", &b);
  }
  // fill every field to all-ones then clear one at a time
  memset(&b, 0xff, sizeof b);
  print_bits("ones", &b);
  b.d = 0;
  print_bits("d=0", &b);
  b.c = 0;
  print_bits("c=0", &b);
  b.f = 0;
  print_bits("f=0", &b);

  Bits64 q = {0, 0, 0};
  for (int i = 0; i < 12; i++) {
    q.lo = (q.lo + 0x3ffff) & 0xfffff;
    int64_t t = q.mid + (int64_t)(i - 6) * 98765431;
    while (t > 400000000 || t < -400000000) t /= 3;  // keep within 30 bits
    q.mid = t;
    q.hi = (q.hi * 3 + 1) & 0x3fff;
    uint64_t w;
    memcpy(&w, &q, 8);
    printf("b64 lo=%llu mid=%lld hi=%llu word=%016llx\n",
           (unsigned long long)q.lo, (long long)q.mid,
           (unsigned long long)q.hi, (unsigned long long)w);
  }
  Mixed m;
  memset(&m, 0, sizeof m);
  m.x = 7;
  m.y = 31;
  m.z = 4095;
  m.w = -1;
  unsigned char raw[sizeof(Mixed)];
  memcpy(raw, &m, sizeof m);
  printf("mixed x=%u y=%u z=%u w=%d raw=", m.x, m.y, m.z, m.w);
  for (size_t i = 0; i < sizeof m; i++) printf("%02x", raw[i]);
  printf("\n");
  m.z -= 4000;
  m.x += 2;
  memcpy(raw, &m, sizeof m);
  printf("mixed x=%u y=%u z=%u w=%d raw=", m.x, m.y, m.z, m.w);
  for (size_t i = 0; i < sizeof m; i++) printf("%02x", raw[i]);
  printf("\n");
}

static void layout(void) {
  printf("-- packed and aligned\n");
  printf("Packed size=%zu value@%zu extra@%zu big@%zu\n", sizeof(Packed),
         offsetof(Packed, value), offsetof(Packed, extra), offsetof(Packed, big));
  printf("Aligned size=%zu align=%zu value@%zu after@%zu\n", sizeof(Aligned),
         _Alignof(Aligned), offsetof(Aligned, value), offsetof(Aligned, after));
  printf("Big size=%zu align=%zu\n", sizeof(Big), _Alignof(Big));
  Packed arr[5];
  for (int i = 0; i < 5; i++) {
    arr[i].tag = (uint8_t)(0xa0 + i);
    arr[i].value = 0x11111111u * (uint32_t)(i + 1);
    arr[i].extra = (uint16_t)(0xbeef + i);
    arr[i].big = 0x0102030405060708ull * (uint64_t)(i + 1);
  }
  unsigned char raw[sizeof arr];
  memcpy(raw, arr, sizeof arr);
  for (size_t i = 0; i < sizeof arr; i++) {
    printf("%02x", raw[i]);
    if (i % 15 == 14) printf("\n");
  }
  uint64_t sum = 0;
  for (int i = 0; i < 5; i++) sum += arr[i].big ^ arr[i].value ^ arr[i].extra;
  printf("sum=%016llx\n", (unsigned long long)sum);
  Big bigs[3];
  printf("Big array stride=%zu\n", (size_t)((char *)&bigs[1] - (char *)&bigs[0]));
}

static void show(const Value *v) {
  switch (v->tag) {
    case T_INT: printf("int %lld\n", v->as.i); break;
    case T_DBL: printf("dbl %g\n", v->as.d); break;
    case T_STR: printf("str %s\n", v->as.s); break;
    case T_PAIR: printf("pair %d,%d\n", v->as.pair.a, v->as.pair.b); break;
  }
}
static Value add(Value a, Value b) {
  Value r;
  if (a.tag == T_INT && b.tag == T_INT) {
    r.tag = T_INT;
    r.as.i = a.as.i + b.as.i;
  } else if (a.tag == T_PAIR && b.tag == T_PAIR) {
    r.tag = T_PAIR;
    r.as.pair.a = a.as.pair.a + b.as.pair.a;
    r.as.pair.b = a.as.pair.b * b.as.pair.b;
  } else {
    double x = a.tag == T_INT ? (double)a.as.i : a.tag == T_DBL ? a.as.d : 0;
    double y = b.tag == T_INT ? (double)b.as.i : b.tag == T_DBL ? b.as.d : 0;
    r.tag = T_DBL;
    r.as.d = x + y;
  }
  return r;
}

static void tagged(void) {
  printf("-- tagged unions\n");
  Value vs[] = {
    {T_INT, {.i = 40}}, {T_DBL, {.d = 2.5}}, {T_STR, {.s = "hello"}},
    {T_PAIR, {.pair = {3, 4}}}, {T_INT, {.i = -1000000000000ll}},
    {T_PAIR, {.pair = {-1, 7}}},
  };
  int n = sizeof vs / sizeof vs[0];
  for (int i = 0; i < n; i++) show(&vs[i]);
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) {
      Value r = add(vs[i], vs[j]);
      printf("%d+%d: ", i, j);
      show(&r);
    }
  printf("sizeof Value=%zu\n", sizeof(Value));
}

int main(void) {
  punning();
  bitfields();
  layout();
  tagged();
  return 0;
}
