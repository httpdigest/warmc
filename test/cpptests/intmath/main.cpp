// Integer arithmetic at the edges, with every operand read through a
// volatile so that nothing is folded at compile time: signed and unsigned
// division and remainder for all sign combinations and the extreme values
// (never INT_MIN / -1 or a zero divisor), division by constants (which the
// compiler turns into multiply-high and shift sequences, a different magic
// number per divisor) checked against division by the same value at run
// time; shifts by every count, arithmetic right shifts of negatives and
// left shifts that cross the sign bit (both defined in C++20); wrapping
// unsigned add/sub/mul; narrowing and widening conversions with sign and
// zero extension; integer promotion of 8- and 16-bit types; the
// __builtin_*_overflow checks; 128-bit arithmetic (multiply, divide,
// remainder and shifts, which go through compiler-rt's helpers on wasm32);
// the high half of a 64x64 multiply; and conversions between integers and
// floating point for values near the precision limits.
//
// The output is a table of results per operation; a wrong lowering of any
// one of these to a native instruction sequence is a single changed number.
#include <climits>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

namespace {

volatile int32_t vi32[] = {0, 1, -1, 2, -2, 3, -3, 7, -7, 10, -10, 100, -100, 12345, -12345,
                           INT32_MAX, INT32_MIN, INT32_MAX - 1, INT32_MIN + 1, 0x40000000, -0x40000000};
volatile int64_t vi64[] = {0, 1, -1, 3, -3, 1000000007LL, -1000000007LL, INT64_MAX, INT64_MIN,
                           INT64_MAX - 1, INT64_MIN + 1, 0x100000000LL, -0x100000000LL, 0x7FFFFFFFLL,
                           -0x80000000LL, 6000000000LL};
volatile uint32_t vu32[] = {0u, 1u, 2u, 3u, 7u, 10u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu, 123456789u};
volatile uint64_t vu64[] = {0ull, 1ull, 3ull, 0xFFFFFFFFull, 0x100000000ull, 0x8000000000000000ull,
                            0xFFFFFFFFFFFFFFFFull, 12345678901234567890ull};

uint64_t g_hash = 1469598103934665603ull;
void mix(uint64_t v) { g_hash = (g_hash ^ v) * 1099511628211ull; }

template <int D>
int32_t sdivConst(int32_t x) { return x / D; }
template <int D>
int32_t sremConst(int32_t x) { return x % D; }
template <unsigned D>
uint32_t udivConst(uint32_t x) { return x / D; }
template <unsigned D>
uint32_t uremConst(uint32_t x) { return x % D; }
template <long long D>
int64_t sdiv64Const(int64_t x) { return x / D; }
template <unsigned long long D>
uint64_t udiv64Const(uint64_t x) { return x / D; }

template <int D>
void checkConst32() {
  volatile int32_t dv = D;
  int bad = 0;
  int32_t sample = 0;
  for (auto &x : vi32) {
    int32_t v = x;
    if (D == -1 && v == INT32_MIN) continue;
    int32_t q = sdivConst<D>(v), r = sremConst<D>(v);
    if (q != v / dv || r != v % dv) ++bad;
    sample ^= (int32_t)((uint32_t)q * 31u + (uint32_t)r);
    mix((uint32_t)q);
    mix((uint32_t)r);
  }
  for (auto &x : vu32) {
    uint32_t v = x;
    if (D > 0) {
      uint32_t q = udivConst<(unsigned)D>(v), r = uremConst<(unsigned)D>(v);
      if (q != v / (uint32_t)dv || r != v % (uint32_t)dv) ++bad;
      mix(q);
      mix(r);
    }
  }
  std::printf("div by const %6d: mismatches=%d sample=%d\n", D, bad, sample);
}

template <long long D>
void checkConst64() {
  volatile int64_t dv = D;
  int bad = 0;
  for (auto &x : vi64) {
    int64_t v = x;
    if (D == -1 && v == INT64_MIN) continue;
    int64_t q = sdiv64Const<D>(v);
    if (q != v / dv) ++bad;
    mix((uint64_t)q);
  }
  for (auto &x : vu64) {
    uint64_t v = x;
    if (D > 0) {
      uint64_t q = udiv64Const<(unsigned long long)D>(v);
      if (q != v / (uint64_t)dv) ++bad;
      mix(q);
    }
  }
  std::printf("div64 by const %12lld: mismatches=%d\n", D, bad);
}

std::string_view yesno(bool b) { return b ? "Y" : "n"; }

}  // namespace

int main() {
  std::printf("== 32-bit signed division table\n");
  const int n32 = sizeof(vi32) / sizeof(vi32[0]);
  for (int i = 0; i < n32; i++) {
    int32_t a = vi32[i];
    std::printf("%11d:", a);
    for (int32_t d : {1, -1, 2, -2, 3, -3, 7, -10, 1000, INT32_MAX, INT32_MIN}) {
      volatile int32_t vd = d;
      int32_t dd = vd;
      if (dd == -1 && a == INT32_MIN) {
        std::printf(" [skip]");
        continue;
      }
      std::printf(" %d,%d", a / dd, a % dd);
    }
    std::printf("\n");
  }

  std::printf("== 64-bit signed and unsigned division\n");
  for (auto &x : vi64) {
    int64_t a = x;
    std::printf("%20lld:", (long long)a);
    for (int64_t d : {3LL, -7LL, 1000000007LL, -0x100000000LL, INT64_MAX}) {
      volatile int64_t vd = d;
      std::printf(" %lld,%lld", (long long)(a / vd), (long long)(a % vd));
    }
    std::printf("\n");
  }
  for (auto &x : vu64) {
    uint64_t a = x;
    std::printf("%20llu:", (unsigned long long)a);
    for (uint64_t d : {3ull, 10ull, 0xFFFFFFFFull, 0x100000001ull, 0x8000000000000000ull}) {
      volatile uint64_t vd = d;
      std::printf(" %llu,%llu", (unsigned long long)(a / vd), (unsigned long long)(a % vd));
    }
    std::printf("\n");
  }

  std::printf("== division by constants vs run-time divisors\n");
  checkConst32<1>();
  checkConst32<-1>();
  checkConst32<2>();
  checkConst32<3>();
  checkConst32<-3>();
  checkConst32<5>();
  checkConst32<6>();
  checkConst32<7>();
  checkConst32<-7>();
  checkConst32<9>();
  checkConst32<10>();
  checkConst32<11>();
  checkConst32<12>();
  checkConst32<13>();
  checkConst32<16>();
  checkConst32<-16>();
  checkConst32<25>();
  checkConst32<60>();
  checkConst32<100>();
  checkConst32<641>();
  checkConst32<1000>();
  checkConst32<4096>();
  checkConst32<65537>();
  checkConst32<1000000>();
  checkConst32<0x40000000>();
  checkConst32<INT32_MAX>();
  checkConst64<3>();
  checkConst64<7>();
  checkConst64<-7>();
  checkConst64<10>();
  checkConst64<1000>();
  checkConst64<1000000007>();
  checkConst64<0x100000000LL>();
  checkConst64<1LL << 40>();
  checkConst64<INT64_MAX>();
  std::printf("division hash=%016llx\n", (unsigned long long)g_hash);

  std::printf("== shifts\n");
  volatile int32_t vneg = -123456789, vpos = 0x12345678;
  volatile uint32_t vu = 0x87654321u;
  volatile int64_t vneg64 = -1234567890123LL;
  volatile uint64_t vu64s = 0xFEDCBA9876543210ull;
  for (int s = 0; s < 32; s += 1) {
    volatile int sh = s;
    int32_t n = vneg, p = vpos;
    uint32_t u = vu;
    std::printf("s=%2d neg>>=%11d neg<<=%11d pos<<=%11d u>>=%08x u<<=%08x\n", s, n >> sh,
                (int32_t)((uint32_t)n << sh), p << sh, u >> sh, u << sh);
  }
  for (int s = 0; s < 64; s += 5) {
    volatile int sh = s;
    int64_t n = vneg64;
    uint64_t u = vu64s;
    std::printf("s=%2d neg64>>=%20lld neg64<<=%20lld u64>>=%016llx u64<<=%016llx\n", s,
                (long long)(n >> sh), (long long)(n << sh), (unsigned long long)(u >> sh),
                (unsigned long long)(u << sh));
  }

  std::printf("== wrapping unsigned and two's complement\n");
  for (auto &x : vu32) {
    uint32_t a = x;
    std::printf("%10u: +0x80000001=%10u -5=%10u *3=%10u *0xFFFFFFFF=%10u neg=%10u ~=%10u\n", a,
                a + 0x80000001u, a - 5u, a * 3u, a * 0xFFFFFFFFu, -a, ~a);
  }
  for (auto &x : vu64) {
    uint64_t a = x;
    std::printf("%20llu: *0x9E3779B97F4A7C15=%20llu -a=%20llu\n", (unsigned long long)a,
                (unsigned long long)(a * 0x9E3779B97F4A7C15ull), (unsigned long long)(0 - a));
  }

  std::printf("== conversions\n");
  for (auto &x : vi64) {
    int64_t a = x;
    std::printf("%20lld -> i32=%11d u32=%10u i16=%6d u16=%5u i8=%4d u8=%3u dbl=%.17g flt=%.9g\n",
                (long long)a, (int32_t)a, (uint32_t)a, (int16_t)a, (uint16_t)a, (int8_t)a, (uint8_t)a,
                (double)a, (double)(float)a);
  }
  for (auto &x : vu64) {
    uint64_t a = x;
    std::printf("%20llu -> i64=%20lld dbl=%.17g flt=%.9g\n", (unsigned long long)a, (long long)(int64_t)a,
                (double)a, (double)(float)a);
  }
  volatile int8_t c8 = -128;
  volatile uint8_t u8v = 200;
  volatile int16_t c16 = -30000;
  volatile uint16_t u16v = 60000;
  std::printf("sext8=%d zext8=%u sext16=%d zext16=%u\n", (int)c8, (unsigned)u8v, (int)c16, (unsigned)u16v);
  uint8_t pa = u8v, pb = u8v;
  uint16_t qa = u16v, qb = u16v;
  std::printf("promotion: u8*u8=%d u8+u8=%d (uint8_t)(u8+u8)=%u u16*u16 as unsigned=%u\n", pa * pb, pa + pb,
              (unsigned)(uint8_t)(pa + pb), (unsigned)qa * qb);
  int8_t ca = c8;
  std::printf("-(int8)-128=%d (int8_t)(-c8)=%d c8>>1=%d c8/2=%d\n", -ca, (int)(int8_t)(-ca), ca >> 1, ca / 2);
  volatile double dvals[] = {0.0, -0.0, 0.5, -0.5, 0.9999999, -0.9999999, 1.5, -1.5, 2147483647.0,
                             -2147483648.0, 4294967295.0, 9007199254740993.0, 1e18, -1e18};
  for (auto &dv : dvals) {
    double d = dv;
    std::printf("double %22.1f -> i32=%s u32=%s i64=%lld\n", d,
                (d > -2147483649.0 && d < 2147483648.0) ? std::to_string((int32_t)d).c_str() : "(n/a)",
                (d > -1.0 && d < 4294967296.0) ? std::to_string((uint32_t)d).c_str() : "(n/a)",
                (long long)(int64_t)d);
  }
  volatile int64_t bigs[] = {(1LL << 53) - 1, (1LL << 53), (1LL << 53) + 1, (1LL << 53) + 3,
                             (1LL << 24) + 1, (1LL << 62) + 1, -(1LL << 53) - 1};
  for (auto &b : bigs) {
    int64_t v = b;
    std::printf("i64 %20lld -> double %.1f -> back %lld; -> float %.1f\n", (long long)v, (double)v,
                (long long)(int64_t)(double)v, (double)(float)v);
  }

  std::printf("== overflow builtins\n");
  for (auto &x : vi32) {
    int32_t a = x, r1, r2, r3;
    bool o1 = __builtin_add_overflow(a, 0x7FFFFF00, &r1);
    bool o2 = __builtin_sub_overflow(a, -0x7FFFFF00, &r2);
    bool o3 = __builtin_mul_overflow(a, 70000, &r3);
    std::printf("%11d: add %s %11d sub %s %11d mul %s %11d\n", a, yesno(o1).data(), r1, yesno(o2).data(), r2,
                yesno(o3).data(), r3);
  }
  for (auto &x : vu64) {
    uint64_t a = x, r;
    bool o = __builtin_mul_overflow(a, 0x100000001ull, &r);
    uint64_t s;
    bool os = __builtin_add_overflow(a, 0x8000000000000000ull, &s);
    std::printf("%20llu: mul %s %20llu add %s %20llu\n", (unsigned long long)a, yesno(o).data(),
                (unsigned long long)r, yesno(os).data(), (unsigned long long)s);
  }

  std::printf("== 128-bit\n");
  for (auto &x : vu64) {
    unsigned __int128 a = x;
    unsigned __int128 sq = a * a;
    unsigned __int128 big = (sq << 7) | 0x55;
    unsigned __int128 q = big / 1000000007ull, rem = big % 1000000007ull;
    std::printf("%20llu: sq.hi=%016llx sq.lo=%016llx q.hi=%016llx q.lo=%016llx rem=%llu\n",
                (unsigned long long)a, (unsigned long long)(sq >> 64), (unsigned long long)sq,
                (unsigned long long)(q >> 64), (unsigned long long)q, (unsigned long long)rem);
  }
  for (auto &x : vi64) {
    __int128 a = x;
    __int128 p = a * (__int128)-987654321987LL;
    __int128 d = p / (__int128)12345678901LL;
    __int128 m = p % (__int128)12345678901LL;
    std::printf("%20lld: p.hi=%20lld p.lo=%016llx d.lo=%20lld m=%lld sar100=%lld\n", (long long)a,
                (long long)(p >> 64), (unsigned long long)p, (long long)d, (long long)m, (long long)(p >> 100));
  }
  unsigned __int128 fact = 1;
  for (int i = 1; i <= 34; i++) fact *= (unsigned)i;
  char buf[64];
  int pos = 63;
  buf[pos] = 0;
  unsigned __int128 t = fact;
  do {
    buf[--pos] = (char)('0' + (int)(t % 10));
    t /= 10;
  } while (t);
  std::printf("34! = %s\n", buf + pos);

  std::printf("== mulhi and misc\n");
  for (auto &x : vu64) {
    uint64_t a = x;
    uint64_t hi = (uint64_t)(((unsigned __int128)a * 0xD1B54A32D192ED03ull) >> 64);
    int64_t sa = (int64_t)a;
    int64_t shi = (int64_t)(((__int128)sa * (__int128)-0x2545F4914F6CDD1DLL) >> 64);
    std::printf("%20llu: umulhi=%016llx smulhi=%20lld\n", (unsigned long long)a, (unsigned long long)hi,
                (long long)shi);
  }
  for (auto &x : vi32) {
    int32_t a = x;
    int32_t absv = a == INT32_MIN ? INT32_MIN : (a < 0 ? -a : a);
    int32_t sgn = (a > 0) - (a < 0);
    int32_t avg = (int32_t)(((int64_t)a + 12345) >> 1);
    uint32_t ua = (uint32_t)a;
    std::printf("%11d: abs=%11d sgn=%2d avg=%11d lt_u=%d lt_s=%d rotl13=%08x\n", a, absv, sgn, avg,
                ua < 100u, a < 100, (ua << 13) | (ua >> 19));
  }
  std::printf("limits: %d %d %u %lld %llu min8=%d max16=%d\n", std::numeric_limits<int>::min(),
              std::numeric_limits<int>::max(), std::numeric_limits<unsigned>::max(),
              (long long)std::numeric_limits<int64_t>::min(), (unsigned long long)std::numeric_limits<uint64_t>::max(),
              (int)std::numeric_limits<int8_t>::min(), (int)std::numeric_limits<int16_t>::max());
  return 0;
}
