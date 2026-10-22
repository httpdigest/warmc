// Operator overloading: a rational number class with arithmetic, compound
// assignment, unary minus and comparisons; a defaulted three-way comparison
// (operator<=>) and the relational operators it synthesises, plus a custom
// <=> returning std::weak_ordering and std::partial_ordering; a 2D vector
// with scalar operations on both sides; a matrix with operator() indexing
// and operator*; a big unsigned integer with + * and << to a stream-like
// sink; prefix and postfix ++/--; operator[] on a sparse array that inserts
// on write; operator bool and operator!; bitwise operators on a flags enum;
// user-defined literals; and the comma, arrow and address-of overloads.
//
// Results print after every step, so a wrong carry, a mis-ordered compare or
// a missed normalisation changes a line.
#include <algorithm>
#include <cctype>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace {

class Rational {
  int64_t n_, d_;
  void norm() {
    if (d_ < 0) {
      n_ = -n_;
      d_ = -d_;
    }
    int64_t g = std::gcd(n_, d_);
    if (g > 1) {
      n_ /= g;
      d_ /= g;
    }
  }

 public:
  Rational(int64_t n = 0, int64_t d = 1) : n_(n), d_(d) { norm(); }
  int64_t num() const { return n_; }
  int64_t den() const { return d_; }
  Rational &operator+=(const Rational &o) {
    n_ = n_ * o.d_ + o.n_ * d_;
    d_ *= o.d_;
    norm();
    return *this;
  }
  Rational &operator-=(const Rational &o) { return *this += -o; }
  Rational &operator*=(const Rational &o) {
    n_ *= o.n_;
    d_ *= o.d_;
    norm();
    return *this;
  }
  Rational &operator/=(const Rational &o) {
    n_ *= o.d_;
    d_ *= o.n_;
    norm();
    return *this;
  }
  Rational operator-() const { return Rational(-n_, d_); }
  Rational operator+() const { return *this; }
  friend Rational operator+(Rational a, const Rational &b) { return a += b; }
  friend Rational operator-(Rational a, const Rational &b) { return a -= b; }
  friend Rational operator*(Rational a, const Rational &b) { return a *= b; }
  friend Rational operator/(Rational a, const Rational &b) { return a /= b; }
  friend bool operator==(const Rational &a, const Rational &b) {
    return a.n_ == b.n_ && a.d_ == b.d_;
  }
  friend std::strong_ordering operator<=>(const Rational &a, const Rational &b) {
    return a.n_ * b.d_ <=> b.n_ * a.d_;
  }
  explicit operator double() const { return (double)n_ / (double)d_; }
  std::string str() const {
    return d_ == 1 ? std::to_string(n_) : std::to_string(n_) + "/" + std::to_string(d_);
  }
};

// Defaulted <=> compares members lexicographically.
struct Version {
  int major, minor, patch;
  auto operator<=>(const Version &) const = default;
};

// Case-insensitive string with weak ordering.
struct CiString {
  std::string s;
  friend std::weak_ordering operator<=>(const CiString &a, const CiString &b) {
    std::size_t n = std::min(a.s.size(), b.s.size());
    for (std::size_t i = 0; i < n; i++) {
      char x = (char)std::tolower((unsigned char)a.s[i]);
      char y = (char)std::tolower((unsigned char)b.s[i]);
      if (x != y) return x < y ? std::weak_ordering::less : std::weak_ordering::greater;
    }
    return a.s.size() <=> b.s.size();
  }
  friend bool operator==(const CiString &a, const CiString &b) {
    return (a <=> b) == 0;
  }
};

// Interval comparison: a partial order (overlapping intervals are unordered).
struct Interval {
  double lo, hi;
  friend std::partial_ordering operator<=>(const Interval &a, const Interval &b) {
    if (a.hi < b.lo) return std::partial_ordering::less;
    if (b.hi < a.lo) return std::partial_ordering::greater;
    if (a.lo == b.lo && a.hi == b.hi) return std::partial_ordering::equivalent;
    return std::partial_ordering::unordered;
  }
};

const char *ordName(std::strong_ordering o) {
  return o < 0 ? "less" : o > 0 ? "greater" : "equal";
}
const char *ordName(std::weak_ordering o) {
  return o < 0 ? "less" : o > 0 ? "greater" : "equivalent";
}
const char *ordName(std::partial_ordering o) {
  if (o == std::partial_ordering::unordered) return "unordered";
  return o < 0 ? "less" : o > 0 ? "greater" : "equivalent";
}

struct Vec2 {
  double x, y;
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(double k) const { return {x * k, y * k}; }
  friend Vec2 operator*(double k, Vec2 v) { return v * k; }
  double operator*(Vec2 o) const { return x * o.x + y * o.y; }  // dot
  double operator^(Vec2 o) const { return x * o.y - y * o.x; }  // cross
  Vec2 &operator+=(Vec2 o) {
    x += o.x;
    y += o.y;
    return *this;
  }
  bool operator==(const Vec2 &) const = default;
};

class Matrix {
  int r_, c_;
  std::vector<long> d_;

 public:
  Matrix(int r, int c) : r_(r), c_(c), d_(r * c) {}
  long &operator()(int i, int j) { return d_[i * c_ + j]; }
  long operator()(int i, int j) const { return d_[i * c_ + j]; }
  int rows() const { return r_; }
  int cols() const { return c_; }
  friend Matrix operator*(const Matrix &a, const Matrix &b) {
    Matrix m(a.r_, b.c_);
    for (int i = 0; i < a.r_; i++)
      for (int j = 0; j < b.c_; j++) {
        long s = 0;
        for (int k = 0; k < a.c_; k++) s += a(i, k) * b(k, j);
        m(i, j) = s;
      }
    return m;
  }
  bool operator==(const Matrix &) const = default;
  void print(const char *name) const {
    std::printf("%s =\n", name);
    for (int i = 0; i < r_; i++) {
      for (int j = 0; j < c_; j++) std::printf(" %6ld", (*this)(i, j));
      std::printf("\n");
    }
  }
};

// Big unsigned integer, base 1e9.
class BigUInt {
  std::vector<uint32_t> limbs_;  // little-endian, base 1e9
  static constexpr uint32_t kBase = 1000000000;

 public:
  BigUInt(uint64_t v = 0) {
    do {
      limbs_.push_back((uint32_t)(v % kBase));
      v /= kBase;
    } while (v);
  }
  BigUInt &operator+=(const BigUInt &o) {
    uint64_t carry = 0;
    for (std::size_t i = 0; i < std::max(limbs_.size(), o.limbs_.size()) || carry; i++) {
      if (i == limbs_.size()) limbs_.push_back(0);
      uint64_t s = carry + limbs_[i] + (i < o.limbs_.size() ? o.limbs_[i] : 0);
      limbs_[i] = (uint32_t)(s % kBase);
      carry = s / kBase;
    }
    return *this;
  }
  friend BigUInt operator+(BigUInt a, const BigUInt &b) { return a += b; }
  friend BigUInt operator*(const BigUInt &a, const BigUInt &b) {
    std::vector<uint64_t> tmp(a.limbs_.size() + b.limbs_.size() + 1, 0);
    for (std::size_t i = 0; i < a.limbs_.size(); i++) {
      uint64_t carry = 0;
      for (std::size_t j = 0; j < b.limbs_.size() || carry; j++) {
        uint64_t cur = tmp[i + j] + carry +
                       (j < b.limbs_.size() ? (uint64_t)a.limbs_[i] * b.limbs_[j] : 0);
        tmp[i + j] = cur % kBase;
        carry = cur / kBase;
      }
    }
    BigUInt r;
    r.limbs_.assign(tmp.begin(), tmp.end());
    while (r.limbs_.size() > 1 && r.limbs_.back() == 0) r.limbs_.pop_back();
    return r;
  }
  BigUInt &operator*=(const BigUInt &o) { return *this = *this * o; }
  std::string str() const {
    std::string s = std::to_string(limbs_.back());
    char buf[16];
    for (std::size_t i = limbs_.size() - 1; i-- > 0;) {
      std::snprintf(buf, sizeof buf, "%09u", limbs_[i]);
      s += buf;
    }
    return s;
  }
  auto operator<=>(const BigUInt &o) const {
    if (limbs_.size() != o.limbs_.size()) return limbs_.size() <=> o.limbs_.size();
    for (std::size_t i = limbs_.size(); i-- > 0;)
      if (limbs_[i] != o.limbs_[i]) return limbs_[i] <=> o.limbs_[i];
    return std::strong_ordering::equal;
  }
  bool operator==(const BigUInt &) const = default;
};

struct Sink {
  std::string buf;
  Sink &operator<<(const char *s) {
    buf += s;
    return *this;
  }
  Sink &operator<<(long v) {
    buf += std::to_string(v);
    return *this;
  }
  Sink &operator<<(const BigUInt &b) {
    buf += b.str();
    return *this;
  }
  Sink &operator<<(const Rational &r) {
    buf += r.str();
    return *this;
  }
};

struct Counter {
  int v = 0;
  int log = 0;
  Counter &operator++() {
    ++v;
    log = log * 10 + 1;
    return *this;
  }
  Counter operator++(int) {
    Counter old = *this;
    ++v;
    log = log * 10 + 2;
    return old;
  }
  Counter &operator--() {
    --v;
    log = log * 10 + 3;
    return *this;
  }
  Counter operator--(int) {
    Counter old = *this;
    --v;
    log = log * 10 + 4;
    return old;
  }
};

class Sparse {
  std::map<long, double> cells_;

 public:
  double &operator[](long i) { return cells_[i]; }
  double operator[](long i) const {
    auto it = cells_.find(i);
    return it == cells_.end() ? 0.0 : it->second;
  }
  std::size_t stored() const { return cells_.size(); }
  explicit operator bool() const { return !cells_.empty(); }
  bool operator!() const { return cells_.empty(); }
};

enum class Perm : unsigned { None = 0, Read = 1, Write = 2, Exec = 4, All = 7 };
constexpr Perm operator|(Perm a, Perm b) { return Perm((unsigned)a | (unsigned)b); }
constexpr Perm operator&(Perm a, Perm b) { return Perm((unsigned)a & (unsigned)b); }
constexpr Perm operator^(Perm a, Perm b) { return Perm((unsigned)a ^ (unsigned)b); }
constexpr Perm operator~(Perm a) { return Perm(~(unsigned)a & 7u); }
Perm &operator|=(Perm &a, Perm b) { return a = a | b; }
Perm &operator&=(Perm &a, Perm b) { return a = a & b; }
std::string permStr(Perm p) {
  std::string s;
  s += (unsigned)(p & Perm::Read) ? 'r' : '-';
  s += (unsigned)(p & Perm::Write) ? 'w' : '-';
  s += (unsigned)(p & Perm::Exec) ? 'x' : '-';
  return s;
}

// User-defined literals.
constexpr long double operator""_km(long double v) { return v * 1000; }
constexpr unsigned long long operator""_KiB(unsigned long long v) { return v * 1024; }
Rational operator""_r(unsigned long long v) { return Rational((int64_t)v); }
std::string operator""_rep(const char *s, std::size_t n) {
  return std::string(s, n) + std::string(s, n);
}

// Arrow and comma.
struct Inner {
  int value = 5;
};
struct Handle {
  Inner *p;
  int *hits;
  Inner *operator->() {
    ++*hits;
    return p;
  }
};
struct Seq {
  std::vector<int> v;
};
Seq &operator,(Seq &s, int x) {
  s.v.push_back(x);
  return s;
}

}  // namespace

int main() {
  std::printf("== rationals\n");
  Rational a(1, 2), b(-3, 4), c(5, 6);
  Rational s = a + b * c;
  std::printf("a=%s b=%s c=%s a+b*c=%s\n", a.str().c_str(), b.str().c_str(), c.str().c_str(),
              s.str().c_str());
  std::printf("a-b=%s a/b=%s -c=%s +c=%s\n", (a - b).str().c_str(), (a / b).str().c_str(),
              (-c).str().c_str(), (+c).str().c_str());
  Rational h;
  for (int i = 1; i <= 12; i++) {
    h += Rational(1, i * i);
    std::printf("sum 1/k^2 to %2d = %s ~ %.10f\n", i, h.str().c_str(), (double)h);
  }
  Rational vals[] = {a, b, c, Rational(2, 4), Rational(-6, 8), 3_r};
  for (auto &x : vals) {
    std::printf("%5s:", x.str().c_str());
    for (auto &y : vals) std::printf(" %s", ordName(x <=> y));
    std::printf(" | < %d <= %d == %d != %d\n", x < a, x <= a, x == a, x != a);
  }

  std::printf("== defaulted <=>\n");
  Version vs[] = {{1, 2, 3}, {1, 10, 0}, {1, 2, 3}, {0, 99, 99}, {2, 0, 0}};
  for (auto &x : vs) {
    for (auto &y : vs) std::printf(" %c", (x < y) ? '<' : (x > y) ? '>' : '=');
    std::printf("   %d.%d.%d\n", x.major, x.minor, x.patch);
  }
  std::printf("== weak and partial orderings\n");
  CiString ci[] = {{"Apple"}, {"apple"}, {"APPLES"}, {"banana"}, {"Ban"}};
  for (auto &x : ci)
    for (auto &y : ci)
      std::printf("%s vs %s: %s eq=%d\n", x.s.c_str(), y.s.c_str(), ordName(x <=> y), x == y);
  Interval iv[] = {{0, 1}, {2, 3}, {0.5, 2.5}, {0, 1}};
  for (auto &x : iv)
    for (auto &y : iv)
      std::printf("[%.1f,%.1f] vs [%.1f,%.1f]: %s\n", x.lo, x.hi, y.lo, y.hi,
                  ordName(x <=> y));

  std::printf("== vectors\n");
  Vec2 p{1, 2}, q{3, -1};
  Vec2 r = 2.0 * p + q * 0.5 - Vec2{1, 1};
  std::printf("r=(%.2f,%.2f) dot=%.2f cross=%.2f eq=%d\n", r.x, r.y, p * q, p ^ q,
              (p + q) == Vec2{4, 1});
  Vec2 acc{0, 0};
  for (int i = 0; i < 8; i++) {
    acc += Vec2{(double)i, (double)(i * i)} * 0.25;
    std::printf("acc=(%.2f,%.2f)\n", acc.x, acc.y);
  }

  std::printf("== matrices\n");
  Matrix m(3, 3), id(3, 3);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      m(i, j) = i * 3 + j + 1;
      id(i, j) = i == j;
    }
  m.print("m");
  Matrix m2 = m * m;
  m2.print("m*m");
  (m2 * m).print("m^3");
  std::printf("m*id==m: %d m*m==m: %d\n", m * id == m, m2 == m);
  Matrix rect(2, 3);
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 3; j++) rect(i, j) = (i + 1) * (j - 1);
  (rect * m).print("rect*m");

  std::printf("== big integers\n");
  BigUInt f(1);
  Sink sink;
  for (int i = 1; i <= 30; i++) {
    f *= BigUInt(i);
    if (i % 5 == 0) {
      sink.buf.clear();
      sink << (long)i << "! = " << f;
      std::printf("%s\n", sink.buf.c_str());
    }
  }
  BigUInt fa(0), fb(1);
  for (int i = 0; i < 150; i++) {
    BigUInt t = fa + fb;
    fa = fb;
    fb = t;
  }
  std::printf("fib(150)=%s\n", fa.str().c_str());
  BigUInt two(1);
  for (int i = 0; i < 128; i++) two += two;
  std::printf("2^128=%s\n", two.str().c_str());
  std::printf("cmp: %s %s %s\n", ordName(two <=> f), ordName(f <=> two),
              ordName(fa <=> fa));
  sink.buf.clear();
  sink << "ratio " << Rational(10, 4) << " and " << -7L;
  std::printf("%s\n", sink.buf.c_str());

  std::printf("== increments\n");
  Counter ct;
  Counter old1 = ct++;
  Counter &ref = ++ct;
  Counter old2 = ct--;
  --ct;
  ++ ++ct;
  std::printf("v=%d log=%d old1.v=%d old2.v=%d ref.v=%d\n", ct.v, ct.log, old1.v, old2.v,
              ref.v);

  std::printf("== sparse\n");
  Sparse sp;
  const Sparse &csp = sp;
  std::printf("empty: bool=%d not=%d\n", (bool)sp, !sp);
  for (long i = 0; i < 1000000; i += 99991) sp[i] = i * 0.5;
  sp[7] += 3;
  sp[7] *= 2;
  std::printf("stored=%zu sp[7]=%.1f csp[8]=%.1f stored=%zu sp[199982]=%.1f\n", sp.stored(),
              sp[7], csp[8], sp.stored(), csp[199982]);
  std::printf("nonempty: bool=%d not=%d\n", (bool)sp, !sp);

  std::printf("== flags\n");
  Perm pm = Perm::Read;
  pm |= Perm::Exec;
  std::printf("pm=%s ~pm=%s pm^All=%s\n", permStr(pm).c_str(), permStr(~pm).c_str(),
              permStr(pm ^ Perm::All).c_str());
  pm &= ~Perm::Read;
  std::printf("pm=%s\n", permStr(pm).c_str());
  for (unsigned i = 0; i < 8; i++) std::printf("%u:%s ", i, permStr(Perm(i)).c_str());
  std::printf("\n");

  std::printf("== literals\n");
  std::printf("1.5_km=%.1f 4_KiB=%llu 7_r+1/2=%s \"ab\"_rep=%s\n", (double)1.5_km, 4_KiB,
              (7_r + Rational(1, 2)).str().c_str(), ("ab"_rep).c_str());

  std::printf("== arrow and comma\n");
  Inner in;
  int hits = 0;
  Handle hd{&in, &hits};
  hd->value += 10;
  int seen = hd->value;
  std::printf("value=%d seen=%d hits=%d\n", in.value, seen, hits);
  Seq sq;
  (sq, 1, 2, 3, 5, 8);
  std::printf("seq:");
  for (int x : sq.v) std::printf(" %d", x);
  std::printf("\n");
  return 0;
}
