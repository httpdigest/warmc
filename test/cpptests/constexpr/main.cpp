// Compile-time evaluation: constexpr functions with loops, recursion and
// local arrays; consteval immediate functions; constinit globals; constexpr
// constructors and member functions on literal classes; tables computed at
// compile time (primes, CRC-32, a sine table by Taylor series, Pascal's
// triangle) and checked with static_assert; std::is_constant_evaluated
// choosing different code paths; constexpr lambdas; constexpr std::array
// and algorithms; and the same functions called at run time with arguments
// the compiler cannot see, so that the run-time result is printed beside the
// compile-time one and the two must agree.
//
// The compile-time results reach the wasm as data segments and immediates;
// the run-time calls are real code.  A disagreement between the two columns
// is a miscompile of the run-time path.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string_view>
#include <type_traits>

namespace {

constexpr uint64_t factorial(unsigned n) { return n <= 1 ? 1 : n * factorial(n - 1); }

constexpr uint64_t fibIter(unsigned n) {
  uint64_t a = 0, b = 1;
  for (unsigned i = 0; i < n; i++) {
    uint64_t t = a + b;
    a = b;
    b = t;
  }
  return a;
}

constexpr bool isPrime(uint32_t n) {
  if (n < 2) return false;
  for (uint32_t d = 2; d * d <= n; d++)
    if (n % d == 0) return false;
  return true;
}

template <std::size_t N>
constexpr std::array<uint32_t, N> firstPrimes() {
  std::array<uint32_t, N> out{};
  std::size_t k = 0;
  for (uint32_t c = 2; k < N; c++)
    if (isPrime(c)) out[k++] = c;
  return out;
}

constexpr std::array<uint32_t, 256> crcTable() {
  std::array<uint32_t, 256> t{};
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    t[i] = c;
  }
  return t;
}
constexpr auto kCrc = crcTable();

constexpr uint32_t crc32(std::string_view s) {
  uint32_t c = 0xFFFFFFFFu;
  for (char ch : s) c = kCrc[(c ^ (uint8_t)ch) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

constexpr double sinTaylor(double x) {
  // reduce to [-pi, pi]
  constexpr double pi = 3.14159265358979323846;
  while (x > pi) x -= 2 * pi;
  while (x < -pi) x += 2 * pi;
  double term = x, sum = x;
  for (int n = 1; n < 20; n++) {
    term *= -x * x / ((2 * n) * (2 * n + 1));
    sum += term;
  }
  return sum;
}

template <std::size_t N>
constexpr std::array<double, N> sinTable() {
  std::array<double, N> t{};
  for (std::size_t i = 0; i < N; i++) t[i] = sinTaylor(2 * 3.14159265358979323846 * i / N);
  return t;
}
constexpr auto kSin = sinTable<16>();

template <std::size_t R>
constexpr std::array<std::array<uint64_t, R>, R> pascal() {
  std::array<std::array<uint64_t, R>, R> p{};
  for (std::size_t i = 0; i < R; i++) {
    p[i][0] = 1;
    for (std::size_t j = 1; j <= i; j++) p[i][j] = p[i - 1][j - 1] + p[i - 1][j];
  }
  return p;
}
constexpr auto kPascal = pascal<16>();

consteval uint32_t fnv1a(std::string_view s) {
  uint32_t h = 2166136261u;
  for (char c : s) {
    h ^= (uint8_t)c;
    h *= 16777619u;
  }
  return h;
}

uint32_t fnv1aRuntime(std::string_view s) {
  uint32_t h = 2166136261u;
  for (char c : s) {
    h ^= (uint8_t)c;
    h *= 16777619u;
  }
  return h;
}

constexpr int whereAmI() {
  if (std::is_constant_evaluated()) return 1;
  return 2;
}

// A literal class with constexpr members.
struct Frac {
  int64_t n, d;
  constexpr Frac(int64_t n_, int64_t d_ = 1) : n(n_), d(d_) { normalise(); }
  constexpr void normalise() {
    if (d < 0) {
      n = -n;
      d = -d;
    }
    int64_t a = n < 0 ? -n : n, b = d;
    while (b) {
      int64_t t = a % b;
      a = b;
      b = t;
    }
    if (a > 1) {
      n /= a;
      d /= a;
    }
  }
  constexpr Frac operator+(Frac o) const { return Frac(n * o.d + o.n * d, d * o.d); }
  constexpr Frac operator*(Frac o) const { return Frac(n * o.n, d * o.d); }
  constexpr Frac operator-(Frac o) const { return Frac(n * o.d - o.n * d, d * o.d); }
  constexpr bool operator==(const Frac &) const = default;
};

constexpr Frac harmonic(int n) {
  Frac s(0);
  for (int i = 1; i <= n; i++) s = s + Frac(1, i);
  return s;
}

constexpr Frac bernoulliLike(int n) {
  // sum_{k=1..n} (-1)^k / (k*(k+1))
  Frac s(0);
  for (int k = 1; k <= n; k++) s = s + Frac(k % 2 ? -1 : 1, k * (k + 1));
  return s;
}

// constexpr sort of a small array, and a constexpr string reverse.
template <std::size_t N>
constexpr std::array<int, N> sorted(std::array<int, N> a) {
  std::sort(a.begin(), a.end());
  return a;
}

template <std::size_t N>
struct FixedString {
  char buf[N]{};
  constexpr FixedString(const char (&s)[N]) {
    for (std::size_t i = 0; i < N; i++) buf[i] = s[i];
  }
  constexpr std::size_t size() const { return N - 1; }
  constexpr FixedString reversed() const {
    FixedString r = *this;
    for (std::size_t i = 0; i < size(); i++) r.buf[i] = buf[size() - 1 - i];
    return r;
  }
};

template <FixedString S>
constexpr std::size_t vowelCount() {
  std::size_t c = 0;
  for (std::size_t i = 0; i < S.size(); i++)
    for (char v : std::string_view("aeiouAEIOU"))
      if (S.buf[i] == v) ++c;
  return c;
}

constinit int g_counter = factorial(5);
constinit const char *g_name = "constinit";

constexpr auto kSquare = [](int x) { return x * x; };
constexpr int kSquares = kSquare(3) + kSquare(4);

constexpr int collatzSteps(uint64_t n) {
  int steps = 0;
  while (n != 1) {
    n = (n & 1) ? 3 * n + 1 : n / 2;
    ++steps;
  }
  return steps;
}

template <int... Ns>
constexpr int maxCollatz() {
  return std::max({collatzSteps(Ns)...});
}

// Static assertions: these must hold or the file does not compile.
static_assert(factorial(10) == 3628800);
static_assert(fibIter(50) == 12586269025ull);
static_assert(firstPrimes<10>()[9] == 29);
static_assert(crc32("123456789") == 0xCBF43926u);
static_assert(fnv1a("") == 2166136261u);
static_assert(kPascal[10][5] == 252);
static_assert(harmonic(4) == Frac(25, 12));
static_assert(sorted(std::array<int, 5>{5, 3, 9, 1, 7})[2] == 5);
static_assert(vowelCount<"Compile Time">() == 5);
static_assert(kSquares == 25);
static_assert(collatzSteps(27) == 111);

volatile unsigned g_opaque = 0;  // makes run-time arguments opaque

}  // namespace

int main() {
  unsigned z = g_opaque;  // 0, but the compiler cannot know

  std::printf("== factorial and fibonacci: compile vs run time\n");
  constexpr uint64_t facCT[] = {factorial(0), factorial(5), factorial(12), factorial(20)};
  unsigned facArgs[] = {0, 5, 12, 20};
  for (int i = 0; i < 4; i++)
    std::printf("fact(%u) ct=%llu rt=%llu\n", facArgs[i], (unsigned long long)facCT[i],
                (unsigned long long)factorial(facArgs[i] + z));
  for (unsigned n = 0; n <= 90; n += 10)
    std::printf("fib(%u)=%llu\n", n, (unsigned long long)fibIter(n + z));

  std::printf("== primes\n");
  constexpr auto primes = firstPrimes<40>();
  for (std::size_t i = 0; i < primes.size(); i++)
    std::printf("%u%s", primes[i], (i + 1) % 20 ? " " : "\n");
  int rtCount = 0;
  for (uint32_t c = z; c < 1000 + z; c++) rtCount += isPrime(c);
  std::printf("primes below 1000 at run time: %d\n", rtCount);

  std::printf("== crc32\n");
  const char *words[] = {"", "a", "abc", "message digest", "123456789",
                         "The quick brown fox jumps over the lazy dog"};
  constexpr uint32_t ctCrc[] = {crc32(""), crc32("a"), crc32("abc"), crc32("message digest"),
                                crc32("123456789"),
                                crc32("The quick brown fox jumps over the lazy dog")};
  for (int i = 0; i < 6; i++)
    std::printf("crc32(\"%s\") ct=%08x rt=%08x\n", words[i], ctCrc[i],
                crc32(std::string_view(words[i] + z)));
  for (int i = 0; i < 256; i += 32)
    std::printf("kCrc[%3d]=%08x\n", i, kCrc[i + z]);

  std::printf("== sine table\n");
  for (std::size_t i = 0; i < kSin.size(); i++)
    std::printf("sin(2pi*%2zu/16) ct=% .12f rt=% .12f\n", i, kSin[i],
                sinTaylor(2 * 3.14159265358979323846 * (i + z) / 16));

  std::printf("== pascal\n");
  for (int r = 0; r < 16; r += 3) {
    std::printf("row %2d:", r);
    for (int c = 0; c <= r; c++) std::printf(" %llu", (unsigned long long)kPascal[r][c]);
    std::printf("\n");
  }

  std::printf("== consteval hashing\n");
  constexpr uint32_t h1 = fnv1a("alpha"), h2 = fnv1a("beta"), h3 = fnv1a("gamma");
  std::printf("alpha ct=%08x rt=%08x\n", h1, fnv1aRuntime("alpha"));
  std::printf("beta  ct=%08x rt=%08x\n", h2, fnv1aRuntime("beta"));
  std::printf("gamma ct=%08x rt=%08x\n", h3, fnv1aRuntime("gamma"));
  const char *keys[] = {"beta", "gamma", "delta", "alpha"};
  for (const char *k : keys) {
    switch (fnv1aRuntime(k)) {
      case fnv1a("alpha"): std::printf("%s -> case alpha\n", k); break;
      case fnv1a("beta"): std::printf("%s -> case beta\n", k); break;
      case fnv1a("gamma"): std::printf("%s -> case gamma\n", k); break;
      default: std::printf("%s -> default\n", k);
    }
  }

  std::printf("== is_constant_evaluated\n");
  constexpr int ct = whereAmI();
  int rt = whereAmI() + (int)z;
  std::printf("compile-time=%d run-time=%d\n", ct, rt);

  std::printf("== fractions\n");
  for (int n = 1; n <= 20; n += 1) {
    Frac h = harmonic(n + (int)z);
    Frac b = bernoulliLike(n + (int)z);
    std::printf("H(%2d)=%lld/%lld  B(%2d)=%lld/%lld\n", n, (long long)h.n, (long long)h.d,
                n, (long long)b.n, (long long)b.d);
  }
  constexpr Frac h20 = harmonic(20);
  std::printf("ct H(20)=%lld/%lld equal=%d\n", (long long)h20.n, (long long)h20.d,
              h20 == harmonic(20 + (int)z));
  constexpr Frac prod = Frac(3, 4) * Frac(-8, 9) - Frac(1, 6);
  std::printf("3/4 * -8/9 - 1/6 = %lld/%lld\n", (long long)prod.n, (long long)prod.d);

  std::printf("== constexpr algorithms\n");
  constexpr auto srt = sorted(std::array<int, 10>{9, -3, 7, 0, 22, 5, 5, -10, 13, 1});
  for (int v : srt) std::printf("%d ", v);
  std::printf("\n");
  constexpr int acc = std::accumulate(srt.begin(), srt.end(), 0);
  std::printf("sum=%d\n", acc);
  constexpr FixedString fs("constexpr strings");
  constexpr auto rev = fs.reversed();
  std::printf("reversed=%s vowels=%zu\n", rev.buf, vowelCount<"constexpr strings">());

  std::printf("== constinit and lambdas\n");
  std::printf("g_counter=%d name=%s\n", g_counter, g_name);
  g_counter += 10;
  std::printf("g_counter=%d\n", g_counter);
  std::printf("kSquares=%d kSquare(rt 12)=%d\n", kSquares, kSquare(12 + (int)z));

  std::printf("== collatz\n");
  constexpr int mc = maxCollatz<7, 27, 97, 871, 6171>();
  std::printf("max ct=%d\n", mc);
  for (uint64_t n : {1ull, 7ull, 27ull, 97ull, 871ull, 6171ull, 77031ull, 837799ull})
    std::printf("collatz(%llu)=%d\n", (unsigned long long)n, collatzSteps(n + z));
  return 0;
}
