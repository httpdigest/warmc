// C++20 concepts: standard concepts from <concepts>, user concepts built
// from requires-expressions (simple, type, compound and nested
// requirements), overloads chosen by constraint subsumption, requires
// clauses on member functions of class templates, abbreviated function
// templates (auto parameters with a constraint), constrained lambdas and
// concept-checked class templates.
//
// The program prints which overload each call resolved to, and runs a small
// generic numeric library and a generic container printer whose dispatch is
// entirely by concept, so the same source text becomes many differently
// shaped instantiations.
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace {

template <class T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <class T>
concept Hashable = requires(T a) {
  { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
};

template <class T>
concept Container = requires(T c) {
  typename T::value_type;
  typename T::iterator;
  { c.begin() } -> std::same_as<typename T::iterator>;
  { c.end() } -> std::same_as<typename T::iterator>;
  { c.size() } -> std::convertible_to<std::size_t>;
};

template <class T>
concept RandomAccess = Container<T> && requires(T c, std::size_t i) {
  { c[i] } -> std::convertible_to<typename T::value_type>;
};

template <class T>
concept Associative = Container<T> && requires {
  typename T::key_type;
  typename T::mapped_type;
};

template <class T>
concept Printable = requires(const T &t) {
  { t.print() } -> std::same_as<std::string>;
};

template <class T>
concept SmallTrivial = std::is_trivially_copyable_v<T> && requires {
  requires sizeof(T) <= 8;
};

// ---- overloads ordered by subsumption
template <class T>
const char *kind(const T &) { return "anything"; }
template <Container T>
const char *kind(const T &) { return "container"; }
template <RandomAccess T>
const char *kind(const T &) { return "random-access container"; }
template <Associative T>
const char *kind(const T &) { return "associative container"; }
template <Numeric T>
const char *kind(const T &) { return "number"; }
template <std::integral T>
const char *kind(const T &) { return "integral number"; }
template <std::signed_integral T>
const char *kind(const T &) { return "signed integral number"; }

// ---- printing driven by concepts
void show(Numeric auto x) {
  if constexpr (std::floating_point<decltype(x)>)
    std::printf("%.3f", (double)x);
  else if constexpr (std::same_as<decltype(x), bool>)
    std::printf("%s", x ? "true" : "false");
  else if constexpr (std::same_as<decltype(x), char>)
    std::printf("'%c'", x);
  else if constexpr (std::signed_integral<decltype(x)>)
    std::printf("%lld", (long long)x);
  else
    std::printf("%lluu", (unsigned long long)x);
}
void show(const std::string &s) { std::printf("\"%s\"", s.c_str()); }
void show(const Printable auto &p) { std::printf("%s", p.print().c_str()); }
template <class A, class B>
void show(const std::pair<A, B> &p);
template <Container C>
  requires(!std::same_as<C, std::string>)
void show(const C &c) {
  std::printf("[");
  bool first = true;
  for (const auto &e : c) {
    if (!first) std::printf(", ");
    first = false;
    show(e);
  }
  std::printf("]");
}
template <class A, class B>
void show(const std::pair<A, B> &p) {
  show(p.first);
  std::printf(": ");
  show(p.second);
}

struct Money {
  int64_t cents;
  std::string print() const {
    return "$" + std::to_string(cents / 100) + "." +
           (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100);
  }
};

// ---- a generic numeric library
template <Numeric T>
T gcdOf(T a, T b) requires std::integral<T> {
  while (b != 0) {
    T t = a % b;
    a = b;
    b = t;
  }
  return a < 0 ? -a : a;
}

template <Numeric T>
constexpr T absOf(T x) { return x < 0 ? -x : x; }

template <class T>
  requires std::floating_point<T>
T sqrtNewton(T x) {
  if (x <= 0) return 0;
  T g = x > 1 ? x : T(1);
  for (int i = 0; i < 60; i++) g = (g + x / g) / 2;
  return g;
}

template <RandomAccess C>
  requires Numeric<typename C::value_type>
auto mean(const C &c) {
  double s = 0;
  for (std::size_t i = 0; i < c.size(); i++) s += c[i];
  return c.size() ? s / c.size() : 0.0;
}

template <class T>
concept Addable = requires(T a, T b) {
  { a + b } -> std::convertible_to<T>;
};

template <Addable T>
T sumAll(const std::vector<T> &v, T init) {
  for (const auto &x : v) init = init + x;
  return init;
}

// ---- class template with constrained members
template <class T>
class Box {
  T v_;

 public:
  explicit Box(T v) : v_(std::move(v)) {}
  const T &get() const { return v_; }
  T doubled() const
    requires Addable<T>
  {
    return v_ + v_;
  }
  std::size_t hash() const
    requires Hashable<T>
  {
    return std::hash<T>{}(v_) % 1000003;
  }
  bool isNegative() const
    requires std::signed_integral<T> || std::floating_point<T>
  {
    return v_ < 0;
  }
};

template <class T>
constexpr bool canDouble = requires(const Box<T> &b) { b.doubled(); };
template <class T>
constexpr bool canNeg = requires(const Box<T> &b) { b.isNegative(); };

struct NoAdd {
  int x;
};

// ---- a concept-checked queue
template <SmallTrivial T, std::size_t N>
  requires(N > 0 && (N & (N - 1)) == 0)
class FixedQueue {
  T items_[N];
  std::size_t head_ = 0, tail_ = 0;

 public:
  bool push(T v) {
    if (tail_ - head_ == N) return false;
    items_[tail_++ & (N - 1)] = v;
    return true;
  }
  bool pop(T &out) {
    if (head_ == tail_) return false;
    out = items_[head_++ & (N - 1)];
    return true;
  }
  std::size_t size() const { return tail_ - head_; }
};

template <class F, class... Args>
  requires std::invocable<F, Args...>
auto callTwice(F f, Args... args) {
  f(args...);
  return f(args...);
}

}  // namespace

int main() {
  std::printf("== subsumption\n");
  std::vector<int> v{3, 1, 4};
  std::list<double> l{1.5, 2.5};
  std::map<std::string, int> m{{"b", 2}, {"a", 1}};
  std::printf("int: %s\n", kind(5));
  std::printf("unsigned: %s\n", kind(5u));
  std::printf("double: %s\n", kind(5.0));
  std::printf("vector: %s\n", kind(v));
  std::printf("list: %s\n", kind(l));
  std::printf("map: %s\n", kind(m));
  std::printf("Money: %s\n", kind(Money{1}));
  std::printf("string: %s\n", kind(std::string("s")));

  std::printf("== concept-driven printing\n");
  show(42); std::printf("\n");
  show(42u); std::printf("\n");
  show(-3.25); std::printf("\n");
  show('q'); std::printf("\n");
  show(true); std::printf("\n");
  show(std::string("text")); std::printf("\n");
  show(Money{12345}); std::printf("\n");
  show(Money{705}); std::printf("\n");
  show(v); std::printf("\n");
  show(l); std::printf("\n");
  show(m); std::printf("\n");
  std::vector<std::vector<int>> vv{{1, 2}, {}, {3}};
  show(vv); std::printf("\n");
  std::map<int, std::vector<Money>> wallet{{1, {{100}, {250}}}, {2, {{99999}}}};
  show(wallet); std::printf("\n");
  std::vector<std::pair<std::string, double>> pairs{{"pi", 3.14159}, {"e", 2.71828}};
  show(pairs); std::printf("\n");

  std::printf("== numeric library\n");
  for (int a : {12, -18, 35, 0, 1071})
    for (int b : {8, 27, -49, 462})
      std::printf("gcd(%d,%d)=%d ", a, b, gcdOf(a, b));
  std::printf("\n");
  std::printf("gcd64=%lld\n", (long long)gcdOf<int64_t>(1LL << 40, 6LL << 20));
  std::printf("abs: %d %.2f %lld\n", absOf(-5), absOf(-2.5), (long long)absOf(-9000000000LL));
  for (double x : {0.0, 0.25, 2.0, 10.0, 12345.678})
    std::printf("sqrtNewton(%.3f)=%.9f\n", x, sqrtNewton(x));
  std::printf("sqrtNewton(2.0f)=%.5f\n", (double)sqrtNewton(2.0f));
  std::vector<double> dv{1.5, 2.5, 9.0};
  std::printf("mean ints=%.4f mean doubles=%.4f\n", mean(v), mean(dv));
  std::printf("sumAll ints=%d strings=%s\n", sumAll(v, 100),
              sumAll(std::vector<std::string>{"a", "b", "c"}, std::string(">")).c_str());

  std::printf("== constrained members\n");
  Box<int> bi(-21);
  Box<std::string> bs("hi");
  Box<double> bd(1.25);
  Box<unsigned> bu(7);
  std::printf("bi doubled=%d neg=%d hash=%zu\n", bi.doubled(), bi.isNegative(), bi.hash());
  std::printf("bs doubled=%s hash-ok=%d\n", bs.doubled().c_str(), bs.hash() < 1000003);
  std::printf("bd doubled=%.2f neg=%d\n", bd.doubled(), bd.isNegative());
  std::printf("bu doubled=%u\n", bu.doubled());
  std::printf("canDouble: int=%d string=%d NoAdd=%d\n", (int)canDouble<int>,
              (int)canDouble<std::string>, (int)canDouble<NoAdd>);
  std::printf("canNeg: int=%d unsigned=%d double=%d string=%d\n", (int)canNeg<int>,
              (int)canNeg<unsigned>, (int)canNeg<double>, (int)canNeg<std::string>);

  std::printf("== standard concepts\n");
  std::printf("integral<char>=%d floating_point<float>=%d same_as<int,int32_t>=%d\n",
              (int)std::integral<char>, (int)std::floating_point<float>,
              (int)std::same_as<int, int32_t>);
  std::printf("derived_from<string,string>=%d convertible_to<int*,void*>=%d\n",
              (int)std::derived_from<std::string, std::string>,
              (int)std::convertible_to<int *, void *>);
  std::printf("totally_ordered<double>=%d copyable<int[2]>=%d movable<string>=%d\n",
              (int)std::totally_ordered<double>, (int)std::copyable<int[2]>,
              (int)std::movable<std::string>);
  std::printf("regular<int>=%d default_initializable<Money>=%d equality_comparable<NoAdd>=%d\n",
              (int)std::regular<int>, (int)std::default_initializable<Money>,
              (int)std::equality_comparable<NoAdd>);
  std::printf("Hashable<int>=%d Hashable<NoAdd>=%d Container<string>=%d "
              "RandomAccess<list>=%d SmallTrivial<double>=%d SmallTrivial<string>=%d\n",
              (int)Hashable<int>, (int)Hashable<NoAdd>, (int)Container<std::string>,
              (int)RandomAccess<std::list<int>>, (int)SmallTrivial<double>,
              (int)SmallTrivial<std::string>);

  std::printf("== fixed queue\n");
  FixedQueue<int, 8> q;
  int pushed = 0, popped = 0;
  for (int round = 0; round < 6; round++) {
    int okp = 0;
    for (int k = 0; k < 5; k++) okp += q.push(pushed++ * 3);
    int out, okq = 0, sum = 0;
    for (int k = 0; k < 3 && q.pop(out); k++) {
      okq++;
      sum += out;
      popped++;
    }
    std::printf("round %d pushed-ok=%d popped=%d sum=%d size=%zu\n", round, okp, okq,
                sum, q.size());
  }
  FixedQueue<double, 2> qd;
  std::printf("qd push %d %d %d\n", qd.push(1.0), qd.push(2.0), qd.push(3.0));

  std::printf("== constrained lambdas\n");
  auto twice = []<std::integral T>(T x) { return x * 2; };
  auto describe = [](const auto &x) {
    if constexpr (requires { x.size(); })
      return (int)x.size();
    else
      return -1;
  };
  std::printf("twice(21)=%d twice('a')=%d\n", twice(21), twice('a'));
  std::printf("describe(v)=%d describe(5)=%d describe(str)=%d\n", describe(v),
              describe(5), describe(std::string("four")));
  int counter = 0;
  auto r = callTwice([&counter](int k) { return counter += k; }, 5);
  std::printf("callTwice -> %d counter=%d\n", r, counter);
  return 0;
}
