// Control flow as the core language writes it: switch statements that
// become jump tables (dense cases), binary searches (sparse cases) and
// ranges with fallthrough, over int, char and enum class scrutinees,
// including negative and huge case values and switches with init
// statements; if with initialisers and if constexpr; nested loops left by
// break, continue and goto; do/while; short-circuit && and || whose right
// sides have side effects; the conditional operator as an lvalue; the
// comma operator; mutual recursion; deep recursion; tail-recursive
// functions; and a computed state machine driven by a table of function
// pointers.
//
// clang lowers dense switches to wasm br_table and the rest to compare
// trees, and loops with multiple exits to nested blocks with br_if; those
// shapes are what the back end has to map to native branches, so each
// path is driven with every input from just below the smallest case to
// just above the largest.
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

volatile int g_zero = 0;

const char *dense(int x) {
  switch (x) {
    case 0: return "zero";
    case 1: return "one";
    case 2: return "two";
    case 3: return "three";
    case 4: return "four";
    case 5: return "five";
    case 6: return "six";
    case 7: return "seven";
    case 8: return "eight";
    case 9: return "nine";
    default: return "other";
  }
}

int sparse(int x) {
  switch (x) {
    case -1000000: return 1;
    case -7: return 2;
    case 0: return 3;
    case 13: return 4;
    case 99: return 5;
    case 1024: return 6;
    case 65536: return 7;
    case 2000000000: return 8;
    case INT32_MIN: return 9;
    default: return 0;
  }
}

int fallthrough(int x) {
  int acc = 0;
  switch (x) {
    case 0: acc += 1; [[fallthrough]];
    case 1: acc += 10; [[fallthrough]];
    case 2: acc += 100; break;
    case 3:
    case 4:
    case 5: acc += 1000; [[fallthrough]];
    case 6: acc += 10000; break;
    default: acc = -1;
  }
  return acc;
}

enum class Color : uint8_t { Red, Green, Blue, Cyan = 10, Magenta, Yellow, Black = 255 };

const char *colorName(Color c) {
  switch (c) {
    case Color::Red: return "red";
    case Color::Green: return "green";
    case Color::Blue: return "blue";
    case Color::Cyan: return "cyan";
    case Color::Magenta: return "magenta";
    case Color::Yellow: return "yellow";
    case Color::Black: return "black";
  }
  return "unnamed";
}

int charClass(char c) {
  switch (c) {
    case 'a': case 'e': case 'i': case 'o': case 'u': return 1;
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': return 2;
    case ' ': case '\t': case '\n': return 3;
    case '+': case '-': case '*': case '/': return 4;
    default: return (c >= 'a' && c <= 'z') ? 5 : 0;
  }
}

// Duff's device: a switch into the middle of a loop.
int duffCopy(int *to, const int *from, int count) {
  int n = (count + 7) / 8, copied = 0;
  if (count <= 0) return 0;
  switch (count % 8) {
    case 0: do { *to++ = *from++; ++copied; [[fallthrough]];
    case 7: *to++ = *from++; ++copied; [[fallthrough]];
    case 6: *to++ = *from++; ++copied; [[fallthrough]];
    case 5: *to++ = *from++; ++copied; [[fallthrough]];
    case 4: *to++ = *from++; ++copied; [[fallthrough]];
    case 3: *to++ = *from++; ++copied; [[fallthrough]];
    case 2: *to++ = *from++; ++copied; [[fallthrough]];
    case 1: *to++ = *from++; ++copied;
            } while (--n > 0);
  }
  return copied;
}

bool isEven(unsigned n);
bool isOdd(unsigned n) { return n == 0 ? false : isEven(n - 1); }
bool isEven(unsigned n) { return n == 0 ? true : isOdd(n - 1); }

long long sumTail(long long n, long long acc) { return n == 0 ? acc : sumTail(n - 1, acc + n); }

int depthProbe(int n) {
  volatile int pad[4] = {n, n, n, n};
  if (n == 0) return pad[0];
  return depthProbe(n - 1) + (pad[3] & 1);
}

long ackermann(long m, long n) {
  if (m == 0) return n + 1;
  if (n == 0) return ackermann(m - 1, 1);
  return ackermann(m - 1, ackermann(m, n - 1));
}

// A table-driven state machine recognising signed decimal numbers with an
// optional fraction and exponent.
enum State { Start, Sign, Int, Dot, Frac, Exp, ExpSign, ExpInt, Fail, NumStates };
using Step = State (*)(char);
State fStart(char c) { return c == '+' || c == '-' ? Sign : (c >= '0' && c <= '9') ? Int : c == '.' ? Dot : Fail; }
State fSign(char c) { return (c >= '0' && c <= '9') ? Int : c == '.' ? Dot : Fail; }
State fInt(char c) { return (c >= '0' && c <= '9') ? Int : c == '.' ? Frac : (c == 'e' || c == 'E') ? Exp : Fail; }
State fDot(char c) { return (c >= '0' && c <= '9') ? Frac : Fail; }
State fFrac(char c) { return (c >= '0' && c <= '9') ? Frac : (c == 'e' || c == 'E') ? Exp : Fail; }
State fExp(char c) { return c == '+' || c == '-' ? ExpSign : (c >= '0' && c <= '9') ? ExpInt : Fail; }
State fExpSign(char c) { return (c >= '0' && c <= '9') ? ExpInt : Fail; }
State fExpInt(char c) { return (c >= '0' && c <= '9') ? ExpInt : Fail; }
State fFail(char) { return Fail; }
const Step kSteps[NumStates] = {fStart, fSign, fInt, fDot, fFrac, fExp, fExpSign, fExpInt, fFail};

bool isNumber(const char *s) {
  State st = Start;
  for (; *s; ++s) st = kSteps[st](*s);
  return st == Int || st == Frac || st == ExpInt;
}

int sideEffects = 0;
bool yes() {
  ++sideEffects;
  return true;
}
bool no() {
  sideEffects += 10;
  return false;
}

}  // namespace

int main() {
  std::printf("== dense switch\n");
  for (int x = -2; x <= 11; x++) std::printf("%d:%s ", x, dense(x + g_zero));
  std::printf("\n== sparse switch\n");
  for (int x : {INT32_MIN, -1000001, -1000000, -999999, -8, -7, -6, -1, 0, 1, 12, 13, 14, 98, 99, 100, 1023,
                1024, 1025, 65535, 65536, 65537, 1999999999, 2000000000, 2000000001, INT32_MAX})
    std::printf("%d->%d ", x, sparse(x + g_zero));
  std::printf("\n== fallthrough\n");
  for (int x = -1; x <= 8; x++) std::printf("%d->%d ", x, fallthrough(x + g_zero));
  std::printf("\n== enum class switch\n");
  for (int v : {0, 1, 2, 3, 9, 10, 11, 12, 13, 254, 255}) {
    Color c = static_cast<Color>(v + g_zero);
    std::printf("%d:%s ", v, colorName(c));
  }
  std::printf("\n== char classes\n");
  std::string text = "x = 3*(a+b) - 42/u\tok\n";
  for (char c : text) std::printf("%d", charClass(c));
  std::printf("\n");

  std::printf("== switch with init and if with init\n");
  for (int i = 0; i < 6; i++) {
    switch (int k = (i * 7 + g_zero) % 4; k) {
      case 0: std::printf("i=%d k=%d zero\n", i, k); break;
      case 1: std::printf("i=%d k=%d one\n", i, k); break;
      default: std::printf("i=%d k=%d many\n", i, k);
    }
    if (int sq = i * i; sq > 10)
      std::printf("  sq=%d big\n", sq);
    else if (sq > 3)
      std::printf("  sq=%d medium\n", sq);
    else
      std::printf("  sq=%d small\n", sq);
  }

  std::printf("== loops with break, continue, goto\n");
  {
    int found = -1, visited = 0;
    for (int i = 0; i < 10 && found < 0; i++) {
      for (int j = 0; j < 10; j++) {
        ++visited;
        if ((i + j) % 2 != 0) continue;
        if (i * j == 24) {
          found = i * 10 + j;
          break;
        }
      }
    }
    std::printf("found=%d visited=%d\n", found, visited);
    int gi = 0, gj = 0, gsum = 0;
    for (gi = 0; gi < 20; gi++)
      for (gj = 0; gj < 20; gj++) {
        gsum += gi ^ gj;
        if (gi * gj > 150) goto done;
      }
  done:
    std::printf("goto exit at i=%d j=%d sum=%d\n", gi, gj, gsum);
    int n = 0, dw = 0;
    do {
      dw += n;
      n += 3;
    } while (n < 50);
    std::printf("do-while n=%d sum=%d\n", n, dw);
    int w = 1000, iters = 0;
    while (true) {
      ++iters;
      if (w % 2 == 0) w /= 2;
      else if (w == 1) break;
      else w = w * 3 + 1;
    }
    std::printf("while-true iterations=%d\n", iters);
  }

  std::printf("== Duff's device\n");
  for (int count : {0, 1, 7, 8, 9, 15, 16, 17, 23}) {
    int src[32], dst[32] = {0};
    for (int i = 0; i < 32; i++) src[i] = i * i + g_zero;
    int copied = duffCopy(dst, src, count);
    int sum = 0;
    for (int i = 0; i < 32; i++) sum += dst[i];
    std::printf("count=%2d copied=%2d sum=%5d\n", count, copied, sum);
  }

  std::printf("== short circuit and conditional\n");
  {
    sideEffects = 0;
    bool r1 = yes() || no();
    bool r2 = no() && yes();
    bool r3 = yes() && no();
    bool r4 = no() || yes();
    bool r5 = (no() || no()) && yes();
    std::printf("r=%d%d%d%d%d sideEffects=%d\n", r1, r2, r3, r4, r5, sideEffects);
    int a = 1, b = 2;
    for (int i = 0; i < 5; i++) ((i + g_zero) % 2 ? a : b) += i;
    std::printf("conditional lvalue: a=%d b=%d\n", a, b);
    int c = (a++, b++, a + b);
    std::printf("comma: a=%d b=%d c=%d\n", a, b, c);
    int x = 0;
    int y = (x = 5 + g_zero);
    y += (x == 5 ? 1 : 0);
    std::printf("assignment value y=%d\n", y);
  }

  std::printf("== recursion\n");
  for (unsigned n : {0u, 1u, 10u, 77u, 1000u}) std::printf("isEven(%u)=%d isOdd(%u)=%d\n", n, isEven(n + g_zero), n, isOdd(n));
  std::printf("sumTail(100000)=%lld\n", sumTail(100000 + g_zero, 0));
  std::printf("depthProbe(500)=%d\n", depthProbe(500 + g_zero));
  for (long m = 0; m <= 3; m++) {
    std::printf("ack(%ld, n):", m);
    for (long n = 0; n <= 4; n++) std::printf(" %ld", ackermann(m, n + g_zero));
    std::printf("\n");
  }

  std::printf("== table-driven state machine\n");
  for (const char *s : {"0", "-12", "+3.25", ".5", "5.", "1e10", "2.5E-3", "e5", "-", "1e", "1.2.3", "--1",
                        "12a", "007", "+.e1", "6.02e+23"})
    std::printf("%-9s %s\n", s, isNumber(s) ? "number" : "not a number");
  return 0;
}
