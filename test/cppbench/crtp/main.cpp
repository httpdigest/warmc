// Expression templates over a CRTP base: vector arithmetic written as whole
// expressions -- a scaled sum, a three-point stencil, a dot product -- that
// the templates fuse into one loop per statement instead of one temporary
// vector per operator.
//
// Static polymorphism in its textbook form.  Expr<E> knows its derived type
// at compile time, so indexing a sum of products is a tree of operator[]
// calls through static_cast that clang inlines down to one loop of f64 loads,
// multiplies and adds per assignment, with the clamp at a stencil's edge as a
// select: no call, no call_indirect, no allocation inside the time step, no
// SIMD since the corpus is built without simd128, and the shape of the
// expression surviving only as the order of the arithmetic.  The row
// measures how well a template library's promise of "as fast as the
// hand-written loop" holds once the loop reaches warmc.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

template <class E>
struct Expr {
  const E &self() const { return static_cast<const E &>(*this); }
};

class Vec : public Expr<Vec> {
  std::vector<double> d_;

 public:
  explicit Vec(size_t n, double v = 0) : d_(n, v) {}
  size_t size() const { return d_.size(); }
  double operator[](size_t i) const { return d_[i]; }
  double &operator[](size_t i) { return d_[i]; }

  template <class E>
  Vec &operator=(const Expr<E> &e) {
    const E &x = e.self();
    for (size_t i = 0; i < d_.size(); i++) d_[i] = x[i];
    return *this;
  }
};

// A leaf that reads a vector at a fixed offset, clamped at the ends: the
// neighbour terms of a stencil.
template <long Off>
struct Shift : Expr<Shift<Off>> {
  const Vec &v;
  explicit Shift(const Vec &v) : v(v) {}
  double operator[](size_t i) const {
    long j = static_cast<long>(i) + Off;
    long n = static_cast<long>(v.size());
    j = j < 0 ? 0 : (j >= n ? n - 1 : j);
    return v[static_cast<size_t>(j)];
  }
};

struct Scalar : Expr<Scalar> {
  double k;
  explicit Scalar(double k) : k(k) {}
  double operator[](size_t) const { return k; }
};

struct Add {
  static double apply(double a, double b) { return a + b; }
};
struct Sub {
  static double apply(double a, double b) { return a - b; }
};
struct Mul {
  static double apply(double a, double b) { return a * b; }
};

template <class L, class R, class Op>
struct Bin : Expr<Bin<L, R, Op>> {
  L l;
  R r;
  Bin(const L &l, const R &r) : l(l), r(r) {}
  double operator[](size_t i) const { return Op::apply(l[i], r[i]); }
};

// Leaves are held by reference (a Vec) or by value (everything else), so a
// temporary node built inside an expression outlives the statement's loop.
template <class E>
struct Hold {
  using type = E;
};
template <>
struct Hold<Vec> {
  struct type : Expr<type> {
    const Vec &v;
    type(const Vec &v) : v(v) {}
    double operator[](size_t i) const { return v[i]; }
  };
};

template <class L, class R>
Bin<typename Hold<L>::type, typename Hold<R>::type, Add> operator+(const Expr<L> &a,
                                                                     const Expr<R> &b) {
  return {a.self(), b.self()};
}
template <class L, class R>
Bin<typename Hold<L>::type, typename Hold<R>::type, Sub> operator-(const Expr<L> &a,
                                                                     const Expr<R> &b) {
  return {a.self(), b.self()};
}
template <class L, class R>
Bin<typename Hold<L>::type, typename Hold<R>::type, Mul> operator*(const Expr<L> &a,
                                                                     const Expr<R> &b) {
  return {a.self(), b.self()};
}
template <class R>
Bin<Scalar, typename Hold<R>::type, Mul> operator*(double k, const Expr<R> &b) {
  return {Scalar(k), b.self()};
}

template <class L, class R>
double dot(const Expr<L> &a, const Expr<R> &b, size_t n) {
  const L &x = a.self();
  const R &y = b.self();
  double s = 0;
  for (size_t i = 0; i < n; i++) s += x[i] * y[i];
  return s;
}

}  // namespace

int main() {
  constexpr size_t N = 4096;
  Vec x(N), y(N), z(N), w(N);
  uint32_t r = 977u;
  for (size_t i = 0; i < N; i++) {
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    x[i] = (r % 2000) * 0.0005 - 0.5;
    y[i] = ((r >> 11) % 2000) * 0.0005;
  }

  double check = 0;
  for (int step = 0; step < 9000; step++) {
    double a = 0.25 + (step % 7) * 0.01;
    // A diffusion step on y, a damped update of x from it, and a residual.
    z = a * Shift<-1>(y) + a * Shift<1>(y) + (1.0 - 2 * a) * y;
    w = z - y;
    x = 0.999 * x + 0.01 * (w * z) - 0.001 * Shift<2>(x);
    y = z + 0.0001 * x;
    check += dot(w, w, N) + dot(x, Shift<-3>(y), N) * 1e-3;
  }
  std::printf("check = %.12e x0 = %.12e\n", check, x[0] + x[N / 2] + x[N - 1]);
  return 0;
}
