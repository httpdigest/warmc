// <format> and <charconv>: std::format with positional and automatic
// arguments, fill/align/sign/#/0/width/precision for integers in every base,
// floating point in the default (shortest round-trip), fixed, scientific,
// general and hex presentations, strings, chars, bools and pointers to
// nothing (nullptr), dynamic width and precision taken from arguments,
// escaped braces, format_to into a back_inserter, formatted_size,
// vformat with make_format_args, and a user-defined formatter; then
// std::to_chars and std::from_chars for integers in bases 2 to 36 and for
// floating point in shortest, fixed, scientific and general form at many
// precisions, with round trips checked bit for bit.
//
// libc++ implements shortest floating-point output with Ryu, which leans
// on 64x64->128-bit multiplies and large precomputed tables, and
// fixed-precision output with Ryu printf's tables; integer conversions use
// digit-pair tables and countl_zero.  All of it is integer arithmetic that
// has to be exactly right for every digit.
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

struct Point {
  int x, y;
};

}  // namespace

template <>
struct std::formatter<Point> {
  bool polar = false;
  constexpr auto parse(std::format_parse_context &ctx) {
    auto it = ctx.begin();
    if (it != ctx.end() && *it == 'p') {
      polar = true;
      ++it;
    }
    return it;
  }
  auto format(const Point &p, std::format_context &ctx) const {
    if (polar) {
      double r = std::sqrt((double)p.x * p.x + (double)p.y * p.y);
      return std::format_to(ctx.out(), "<r={:.3f}>", r);
    }
    return std::format_to(ctx.out(), "({}, {})", p.x, p.y);
  }
};

namespace {

volatile double vdoubles[] = {0.0, -0.0, 1.0, 0.1, 0.2, 0.30000000000000004, 1.0 / 3.0, 2.0 / 3.0, 100.0,
                              123456.789, 1e21, 1e22, 1e23, 5e-324, 2.2250738585072014e-308,
                              1.7976931348623157e308, 9007199254740993.0, 0.000001, 1e-7, 3.141592653589793,
                              -2.718281828459045, 4.35, 0.5, 1.5, 2.5};
volatile float vfloats[] = {0.0f, 1.0f, 0.1f, 1.0f / 3.0f, 16777216.0f, 3.4028235e38f, 1e-45f, 1.17549435e-38f,
                            -7.25f, 0.3f};

std::string chars(double d, std::chars_format f, int prec = -1) {
  char buf[400];
  auto r = prec < 0 ? std::to_chars(buf, buf + sizeof buf, d, f) : std::to_chars(buf, buf + sizeof buf, d, f, prec);
  if (r.ec != std::errc()) return "<error>";
  return std::string(buf, r.ptr);
}

}  // namespace

int main() {
  std::printf("== format basics\n");
  std::printf("%s\n", std::format("{} {} {}", 1, "two", 3.0).c_str());
  std::printf("%s\n", std::format("{2} {0} {1} {0}", "a", "b", "c").c_str());
  std::printf("%s\n", std::format("{{literal}} {}%", 50).c_str());
  std::printf("%s\n", std::format("bool {} {} char {} {:d} str {:>8} sv {:.3}", true, false, 'x', 'x', "right",
                                  std::string_view("truncate")).c_str());
  std::printf("%s\n", std::format("nullptr {}", nullptr).c_str());

  std::printf("== integers\n");
  for (long long v : {0LL, 1LL, -1LL, 42LL, -42LL, 255LL, 65535LL, -2147483648LL, 9223372036854775807LL}) {
    std::printf("%s\n", std::format("[{:d}] [{:+d}] [{: d}] [{:x}] [{:#X}] [{:o}] [{:#b}] [{:>12}] [{:<12}] [{:^12}] [{:*^12}] [{:012}]",
                                    v, v, v, v, v, v, v & 0xFFFF, v, v, v, v, v).c_str());
  }
  std::printf("%s\n", std::format("unsigned max {} {:#x}", std::numeric_limits<uint64_t>::max(),
                                  std::numeric_limits<uint64_t>::max()).c_str());
  std::printf("%s\n", std::format("char as int {:d} {:#x}, int as char {:c}", 'A', 'z', 66).c_str());

  std::printf("== floating point\n");
  for (auto &vd : vdoubles) {
    double d = vd;
    std::printf("%s\n", std::format("{} | {:.3f} | {:e} | {:.2e} | {:g} | {:.10g} | {:a} | {:+.1f} | {:>14.4}", d,
                                    d, d, d, d, d, d, d, d).c_str());
  }
  for (auto &vf : vfloats) {
    float f = vf;
    std::printf("%s\n", std::format("float {} | {:.9g} | {:e} | {:a}", f, f, f, f).c_str());
  }
  double inf = std::numeric_limits<double>::infinity();
  std::printf("%s\n", std::format("inf {} {:+} {:e} {:>6} -inf {}", inf, inf, inf, inf, -inf).c_str());

  std::printf("== dynamic width and precision\n");
  for (int w = 4; w <= 16; w += 4)
    for (int p = 0; p <= 6; p += 3)
      std::printf("%s\n", std::format("w={:2} p={} [{:{}.{}f}] [{:>{}}]", w, p, 3.14159265, w, p, "ab", w).c_str());

  std::printf("== format_to, formatted_size, vformat, custom\n");
  {
    std::string out;
    for (int i = 0; i < 5; i++) std::format_to(std::back_inserter(out), "{}:{:02x} ", i, i * 37);
    std::printf("%s\n", out.c_str());
    std::printf("formatted_size=%zu\n", std::formatted_size("{:>20}|{:.5f}", "x", 2.0));
    char fixed[16];
    auto res = std::format_to_n(fixed, sizeof fixed - 1, "{}-{}-{}", 123456, 789012, 345678);
    *res.out = '\0';
    std::printf("format_to_n: '%s' would have been %ld\n", fixed, (long)res.size);
    int a = 7;
    double b = 0.125;
    std::string_view fmtStr = "vformat {} and {:.4f}";
    std::printf("%s\n", std::vformat(fmtStr, std::make_format_args(a, b)).c_str());
    Point p{3, 4};
    std::printf("%s\n", std::format("point {0} polar {1:p} twice {0}", p, p).c_str());
    std::vector<Point> pts{{1, 2}, {-5, 12}, {0, 0}};
    std::string all;
    for (auto &pt : pts) all += std::format("{}{:p} ", pt, pt);
    std::printf("%s\n", all.c_str());
    try {
      std::string bad = "{:d}";
      std::string s = "text";
      std::printf("%s\n", std::vformat(bad, std::make_format_args(s)).c_str());
    } catch (const std::format_error &e) {
      std::printf("format_error caught\n");
    }
  }

  std::printf("== to_chars / from_chars integers\n");
  for (long long v : {0LL, 7LL, -7LL, 255LL, 1000000LL, -123456789LL, 9223372036854775807LL,
                      (long long)0x8000000000000000ULL}) {
    std::printf("%20lld:", v);
    for (int base : {2, 8, 10, 16, 36}) {
      char buf[80];
      auto r = std::to_chars(buf, buf + sizeof buf, v, base);
      std::string s(buf, r.ptr);
      long long back = 0;
      auto fr = std::from_chars(s.data(), s.data() + s.size(), back, base);
      std::printf(" b%d=%s%s", base, s.c_str(), (fr.ec == std::errc() && back == v) ? "" : "(!)");
    }
    std::printf("\n");
  }
  {
    const char *inputs[] = {"12345", "-0", "99999999999", "7fffffff", "zz", "  12", "12abc", "", "-"};
    for (const char *in : inputs) {
      int v = -1;
      auto r = std::from_chars(in, in + std::strlen(in), v);
      int hv = -1;
      auto rh = std::from_chars(in, in + std::strlen(in), hv, 16);
      std::printf("from_chars('%s'): dec v=%d ec=%d used=%ld | hex v=%d ec=%d used=%ld\n", in, v, (int)r.ec,
                  (long)(r.ptr - in), hv, (int)rh.ec, (long)(rh.ptr - in));
    }
    char small[3];
    auto r = std::to_chars(small, small + sizeof small, 12345);
    std::printf("to_chars into 3 bytes: ec=value_too_large %d\n", r.ec == std::errc::value_too_large);
  }

  std::printf("== to_chars floating point\n");
  int roundTrips = 0, total = 0;
  for (auto &vd : vdoubles) {
    double d = vd;
    std::string shortest = chars(d, std::chars_format::general);
    double back = 0;
    auto r = std::from_chars(shortest.data(), shortest.data() + shortest.size(), back);
    bool same = r.ec == std::errc() && std::memcmp(&back, &d, sizeof d) == 0;
    roundTrips += same;
    ++total;
    std::printf("%-24s fixed=%s sci=%s hex=%s fixed6=%s sci3=%s gen12=%s\n", shortest.c_str(),
                chars(d, std::chars_format::fixed).substr(0, 40).c_str(),
                chars(d, std::chars_format::scientific).c_str(), chars(d, std::chars_format::hex).c_str(),
                chars(d, std::chars_format::fixed, 6).substr(0, 40).c_str(),
                chars(d, std::chars_format::scientific, 3).c_str(), chars(d, std::chars_format::general, 12).c_str());
  }
  std::printf("shortest round trips exact: %d of %d\n", roundTrips, total);
  {
    double x = 1.0;
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 400; i++) {
      x = x * 1.0137 + 0.000371;
      if (x > 1e200) x = x * 1e-250;
      char buf[64];
      auto r = std::to_chars(buf, buf + sizeof buf, x);
      for (char *c = buf; c < r.ptr; c++) h = (h ^ (unsigned char)*c) * 1099511628211ull;
      double back;
      std::from_chars(buf, r.ptr, back);
      if (back != x) std::printf("round trip failed at %d\n", i);
    }
    std::printf("hash of 400 shortest representations=%016llx\n", (unsigned long long)h);
  }
  {
    const char *fin[] = {"3.14159", "1e300", "1e-400", "-0.0", "0x1.8p1", "inf", "nan", "1.7976931348623159e308",
                         "4.9406564584124654e-324", ".5", "5.", "12e", "abc"};
    for (const char *in : fin) {
      double v = 0;
      auto r = std::from_chars(in, in + std::strlen(in), v);
      std::printf("from_chars('%s') -> %s ec=%d used=%ld\n", in,
                  std::isnan(v) ? "nan" : std::format("{}", v).c_str(), (int)r.ec, (long)(r.ptr - in));
    }
    double hx = 0;
    const char hexIn[] = "1.8p1";
    std::from_chars(hexIn, hexIn + 5, hx, std::chars_format::hex);
    std::printf("hex from_chars('1.8p1')=%g\n", hx);
  }
  return 0;
}
