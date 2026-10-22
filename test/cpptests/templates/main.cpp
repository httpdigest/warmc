// Templates: function templates with deduction and explicit arguments,
// overloading against non-templates, class templates with full and partial
// specialisation, non-type and template-template parameters, variadic
// templates expanded by recursion and by fold expressions over every
// operator kind, CRTP static polymorphism, SFINAE with enable_if and
// decltype in the return type, tag dispatch, if constexpr, type traits
// computed by hand, and a compile-time typelist.
//
// Everything here is resolved at compile time, so what reaches the wasm is
// many small monomorphised functions; the output records which
// specialisation or overload each call landed in, and the values each
// instantiation computed.
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// ---- function templates and overload resolution
template <class T>
const char *which(T) { return "template<T>"; }
template <class T>
const char *which(T *) { return "template<T*>"; }
const char *which(int) { return "non-template int"; }
template <>
const char *which<double>(double) { return "explicit specialisation double"; }

template <class T, class U>
auto add(T a, U b) -> decltype(a + b) { return a + b; }

template <class T>
T maxOf(T a, T b) { return a < b ? b : a; }

template <class To, class From>
To convert(From f) { return static_cast<To>(f); }

// ---- class templates: primary, full, partial specialisation
template <class T>
struct Describe {
  static std::string name() { return "generic"; }
};
template <>
struct Describe<int> {
  static std::string name() { return "int"; }
};
template <class T>
struct Describe<T *> {
  static std::string name() { return "pointer to " + Describe<T>::name(); }
};
template <class T>
struct Describe<const T> {
  static std::string name() { return "const " + Describe<T>::name(); }
};
template <class T, std::size_t N>
struct Describe<T[N]> {
  static std::string name() {
    return "array[" + std::to_string(N) + "] of " + Describe<T>::name();
  }
};
template <class T>
struct Describe<std::vector<T>> {
  static std::string name() { return "vector of " + Describe<T>::name(); }
};
template <class K, class V>
struct Describe<std::pair<K, V>> {
  static std::string name() {
    return "pair<" + Describe<K>::name() + "," + Describe<V>::name() + ">";
  }
};

// ---- non-type template parameters
template <int N>
struct Fib {
  static constexpr long long value = Fib<N - 1>::value + Fib<N - 2>::value;
};
template <>
struct Fib<0> {
  static constexpr long long value = 0;
};
template <>
struct Fib<1> {
  static constexpr long long value = 1;
};

template <class T, std::size_t N>
struct RingBuffer {
  std::array<T, N> data{};
  std::size_t head = 0, count = 0;
  void push(T v) {
    data[(head + count) % N] = v;
    if (count < N)
      ++count;
    else
      head = (head + 1) % N;
  }
  T sum() const {
    T s{};
    for (std::size_t i = 0; i < count; i++) s += data[(head + i) % N];
    return s;
  }
};

template <auto V>
struct Constant {
  static constexpr auto value = V;
};

// ---- template template parameters
template <class T, template <class...> class Container>
struct Stack {
  Container<T> c;
  void push(T v) { c.push_back(v); }
  T pop() {
    T v = c.back();
    c.pop_back();
    return v;
  }
  std::size_t size() const { return c.size(); }
};

// ---- variadic templates
void printAll() { std::printf("\n"); }
template <class T, class... Rest>
void printAll(T first, Rest... rest) {
  if constexpr (std::is_same_v<T, std::string>)
    std::printf("str(%s)", first.c_str());
  else if constexpr (std::is_floating_point_v<T>)
    std::printf("flt(%.2f)", (double)first);
  else if constexpr (std::is_pointer_v<T>)
    std::printf("cstr(%s)", first);
  else
    std::printf("int(%lld)", (long long)first);
  if constexpr (sizeof...(rest) > 0) std::printf(" ");
  printAll(rest...);
}

template <class... Ts>
constexpr auto sumFold(Ts... xs) { return (xs + ... + 0); }
template <class... Ts>
constexpr auto subLeft(Ts... xs) { return (... - xs); }
template <class... Ts>
constexpr auto subRight(Ts... xs) { return (xs - ...); }
template <class... Ts>
constexpr bool allTrue(Ts... xs) { return (xs && ...); }
template <class... Ts>
constexpr bool anyTrue(Ts... xs) { return (xs || ...); }
template <class... Ts>
constexpr auto bitOr(Ts... xs) { return (xs | ...); }
template <class... Ts>
constexpr auto product(Ts... xs) { return (1 * ... * xs); }

template <class F, class... Ts>
void forEach(F f, Ts... xs) { (f(xs), ...); }

template <class... Ts>
constexpr std::size_t countArgs(Ts...) { return sizeof...(Ts); }

template <class T, class... Ts>
constexpr bool contains = (std::is_same_v<T, Ts> || ...);

template <std::size_t... Is>
constexpr std::array<int, sizeof...(Is)> squares(std::index_sequence<Is...>) {
  return {{(int)(Is * Is)...}};
}

// ---- CRTP
template <class Derived>
struct Shape {
  double area() const { return static_cast<const Derived *>(this)->areaImpl(); }
  std::string label() const {
    return std::string(Derived::kName) + "(" + std::to_string((int)(area() * 100)) +
           ")";
  }
};
struct Sq : Shape<Sq> {
  static constexpr const char *kName = "sq";
  double s;
  explicit Sq(double s) : s(s) {}
  double areaImpl() const { return s * s; }
};
struct Tri : Shape<Tri> {
  static constexpr const char *kName = "tri";
  double b, h;
  Tri(double b, double h) : b(b), h(h) {}
  double areaImpl() const { return b * h / 2; }
};

template <class Derived>
struct Countable {
  static inline int created = 0;
  Countable() { ++created; }
};
struct Apple : Countable<Apple> {};
struct Pear : Countable<Pear> {};

template <class D>
struct Comparable {
  friend bool operator!=(const D &a, const D &b) { return !(a == b); }
  friend bool operator>(const D &a, const D &b) { return b < a; }
  friend bool operator<=(const D &a, const D &b) { return !(b < a); }
  friend bool operator>=(const D &a, const D &b) { return !(a < b); }
};
struct Ver : Comparable<Ver> {
  int major, minor;
  Ver(int a, int b) : major(a), minor(b) {}
  friend bool operator==(const Ver &a, const Ver &b) {
    return a.major == b.major && a.minor == b.minor;
  }
  friend bool operator<(const Ver &a, const Ver &b) {
    return a.major != b.major ? a.major < b.major : a.minor < b.minor;
  }
};

// ---- SFINAE
template <class T>
auto lengthOf(const T &t, int) -> decltype(t.size(), std::size_t()) {
  return t.size();
}
template <class T>
std::size_t lengthOf(const T &, long) { return 1; }

template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
const char *category(T) { return "integral"; }
template <class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
const char *category(T) { return "floating"; }
template <class T,
          std::enable_if_t<!std::is_arithmetic_v<T>, int> = 0>
const char *category(const T &) { return "other"; }

template <class T, class = void>
struct HasValueType : std::false_type {};
template <class T>
struct HasValueType<T, std::void_t<typename T::value_type>> : std::true_type {};

// ---- tag dispatch
struct ForwardTag {};
struct RandomTag : ForwardTag {};
template <class T>
struct IterTag {
  using type = ForwardTag;
};
template <class T>
struct IterTag<T *> {
  using type = RandomTag;
};
const char *advanceImpl(ForwardTag) { return "step by step"; }
const char *advanceImpl(RandomTag) { return "jump"; }
template <class It>
const char *advanceHow(It) { return advanceImpl(typename IterTag<It>::type{}); }

// ---- hand-written traits
template <class T>
struct RemoveAllPtr {
  using type = T;
};
template <class T>
struct RemoveAllPtr<T *> : RemoveAllPtr<T> {};
template <class T>
struct PtrDepth : std::integral_constant<int, 0> {};
template <class T>
struct PtrDepth<T *> : std::integral_constant<int, 1 + PtrDepth<T>::value> {};

// ---- typelists
template <class... Ts>
struct TypeList {};
template <class L>
struct Length;
template <class... Ts>
struct Length<TypeList<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};
template <class L, class T>
struct PushFront;
template <class... Ts, class T>
struct PushFront<TypeList<Ts...>, T> {
  using type = TypeList<T, Ts...>;
};
template <class L>
struct SizeSum;
template <class... Ts>
struct SizeSum<TypeList<Ts...>> : std::integral_constant<std::size_t, (sizeof(Ts) + ... + 0)> {};
template <template <class> class Pred, class L>
struct CountIf;
template <template <class> class Pred, class... Ts>
struct CountIf<Pred, TypeList<Ts...>>
    : std::integral_constant<int, ((Pred<Ts>::value ? 1 : 0) + ... + 0)> {};

// ---- generic algorithms over any container
template <class C, class F>
auto mapTo(const C &c, F f) {
  std::vector<decltype(f(*c.begin()))> out;
  for (auto &x : c) out.push_back(f(x));
  return out;
}

template <class T>
T power(T base, unsigned e) {
  T r = T(1);
  while (e) {
    if (e & 1) r *= base;
    base *= base;
    e >>= 1;
  }
  return r;
}

// Class template argument deduction with a deduction guide.
template <class T>
struct Wrapper {
  T v;
  Wrapper(T x) : v(x) {}
};
Wrapper(const char *) -> Wrapper<std::string>;

}  // namespace

int main() {
  std::printf("== overloads\n");
  int i = 3;
  std::printf("which(3)=%s\n", which(3));
  std::printf("which(3L)=%s\n", which(3L));
  std::printf("which(2.0)=%s\n", which(2.0));
  std::printf("which(2.0f)=%s\n", which(2.0f));
  std::printf("which(&i)=%s\n", which(&i));
  std::printf("which<int>(3)=%s\n", which<int>(3));
  std::printf("add(1,2.5)=%.2f add(1u,2ll)=%lld maxOf('a','z')=%c\n", add(1, 2.5),
              (long long)add(1u, 2ll), maxOf('a', 'z'));
  std::printf("maxOf<double>(3,4.5)=%.1f convert<int>(9.99)=%d\n",
              maxOf<double>(3, 4.5), convert<int>(9.99));

  std::printf("== specialisations\n");
  std::printf("%s\n", Describe<double>::name().c_str());
  std::printf("%s\n", Describe<int>::name().c_str());
  std::printf("%s\n", Describe<int **>::name().c_str());
  std::printf("%s\n", Describe<const int *>::name().c_str());
  std::printf("%s\n", Describe<int *const>::name().c_str());
  std::printf("%s\n", Describe<int[4]>::name().c_str());
  std::printf("%s\n", Describe<std::vector<std::vector<int *>>>::name().c_str());
  std::printf("%s\n", Describe<std::pair<int, std::pair<char, int[2]>>>::name().c_str());

  std::printf("== non-type parameters\n");
  std::printf("fib10=%lld fib50=%lld fib90=%lld\n", Fib<10>::value, Fib<50>::value,
              Fib<90>::value);
  RingBuffer<int, 4> rb;
  for (int k = 1; k <= 10; k++) {
    rb.push(k * k);
    std::printf("push %3d count=%zu sum=%d\n", k * k, rb.count, rb.sum());
  }
  RingBuffer<double, 3> rd;
  for (int k = 0; k < 5; k++) rd.push(k / 4.0);
  std::printf("ring<double> sum=%.3f\n", rd.sum());
  std::printf("Constant<'x'>=%c Constant<42u>=%u Constant<true>=%d\n",
              Constant<'x'>::value, Constant<42u>::value, (int)Constant<true>::value);

  std::printf("== template template\n");
  Stack<int, std::vector> st;
  for (int k = 0; k < 6; k++) st.push(k * 7);
  std::printf("size=%zu pops:", st.size());
  while (st.size()) std::printf(" %d", st.pop());
  std::printf("\n");

  std::printf("== variadic\n");
  printAll(1, 2.5, std::string("three"), "four", 5ll, 'A', 0.125f);
  printAll(std::string("solo"));
  std::printf("sum=%d sumMixed=%.2f\n", sumFold(1, 2, 3, 4, 5), sumFold(1, 2.5, 3u));
  std::printf("subLeft(10,3,2)=%d subRight(10,3,2)=%d\n", subLeft(10, 3, 2),
              subRight(10, 3, 2));
  std::printf("allTrue=%d,%d anyTrue=%d,%d\n", allTrue(true, 1, 'c'),
              allTrue(true, 0, 'c'), anyTrue(0, false, 0), anyTrue(0, 0, 3));
  std::printf("bitOr=0x%x product=%lld product()=%d\n", bitOr(1u, 4u, 16u, 256u),
              (long long)product(2ll, 3, 5, 7, 11), product());
  forEach([](auto x) { std::printf("[%d]", (int)(x * 2)); }, 1, 2.6, 'a', true);
  std::printf("\ncountArgs=%zu,%zu contains=%d,%d\n", countArgs(), countArgs(1, "a", 2.0),
              (int)contains<int, char, long, int>, (int)contains<float, char, double>);
  constexpr auto sq = squares(std::make_index_sequence<8>{});
  for (int v : sq) std::printf("%d ", v);
  std::printf("\n");

  std::printf("== CRTP\n");
  Sq a(3);
  Tri b(4, 5);
  std::printf("%s %s\n", a.label().c_str(), b.label().c_str());
  Apple ap1, ap2, ap3;
  Pear pe1;
  (void)ap1, (void)ap2, (void)ap3, (void)pe1;
  std::printf("apples=%d pears=%d\n", Apple::created, Pear::created);
  Ver vs[] = {{1, 2}, {1, 10}, {2, 0}, {1, 2}};
  for (auto &x : vs)
    for (auto &y : vs)
      std::printf("%d.%d vs %d.%d: != %d > %d <= %d >= %d\n", x.major, x.minor,
                  y.major, y.minor, x != y, x > y, x <= y, x >= y);

  std::printf("== SFINAE\n");
  std::vector<int> v5(5);
  std::string s7 = "seven!!";
  std::printf("lengthOf vector=%zu string=%zu int=%zu\n", lengthOf(v5, 0),
              lengthOf(s7, 0), lengthOf(42, 0));
  std::printf("category: %s %s %s %s %s\n", category(1), category('c'),
              category(1.5), category(2.5f), category(s7));
  std::printf("HasValueType vector=%d int=%d string=%d\n",
              (int)HasValueType<std::vector<int>>::value, (int)HasValueType<int>::value,
              (int)HasValueType<std::string>::value);
  std::vector<int>::iterator vit = v5.begin();
  std::printf("advance: ptr=%s iterator-object=%s\n", advanceHow(&i),
              advanceHow(vit));

  std::printf("== traits\n");
  std::printf("PtrDepth<int***>=%d same=%d\n", PtrDepth<int ***>::value,
              (int)std::is_same_v<RemoveAllPtr<int ***>::type, int>);
  using L0 = TypeList<char, int, double>;
  using L1 = PushFront<L0, short>::type;
  std::printf("len L0=%zu L1=%zu sizesum L1=%zu integral in L1=%d\n",
              Length<L0>::value, Length<L1>::value, SizeSum<L1>::value,
              CountIf<std::is_integral, L1>::value);
  std::printf("is_same<int,signed>=%d is_convertible<int,double>=%d "
              "is_base_of<Shape<Sq>,Sq>=%d decay=%d\n",
              (int)std::is_same_v<int, signed>,
              (int)std::is_convertible_v<int, double>,
              (int)std::is_base_of_v<Shape<Sq>, Sq>,
              (int)std::is_same_v<std::decay_t<const int (&)[3]>, const int *>);
  std::printf("conditional size=%zu common_type double=%d\n",
              sizeof(std::conditional_t<(sizeof(long) > 4), long, long long>),
              (int)std::is_same_v<std::common_type_t<int, float, double>, double>);

  std::printf("== generic algorithms\n");
  std::vector<int> base = {1, 2, 3, 4, 5, 6};
  auto halves = mapTo(base, [](int x) { return x / 2.0; });
  auto strs = mapTo(base, [](int x) { return std::string(x, '*'); });
  for (std::size_t k = 0; k < base.size(); k++)
    std::printf("%d -> %.1f %s\n", base[k], halves[k], strs[k].c_str());
  for (unsigned e = 0; e < 12; e += 3)
    std::printf("power: 3^%u=%lld 1.5^%u=%.4f\n", e, (long long)power<long long>(3, e),
                e, power(1.5, e));
  Wrapper w1(5);
  Wrapper w2("deduced");
  Wrapper w3(2.5);
  std::printf("CTAD: %d %s(len %zu) %.1f\n", w1.v, w2.v.c_str(), w2.v.size(), w3.v);
  std::pair pr(1, 2.5);
  std::vector vv{1, 2, 3};
  std::printf("CTAD std: pair second=%.1f vector size=%zu\n", pr.second, vv.size());
  return 0;
}
