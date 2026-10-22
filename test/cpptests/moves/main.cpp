// Value categories and move semantics, observed through counters: copy and
// move constructors and assignments, guaranteed copy elision of prvalues,
// named return value optimisation, std::move into containers and out of
// them, move-only types, vector growth choosing move or copy by noexcept
// (move_if_noexcept), emplace against push, perfect forwarding through a
// factory and through a variadic wrapper (with the value category of each
// argument printed as the callee sees it), std::exchange and std::swap,
// moved-from state of standard types, and reference collapsing.
//
// The counters are the output: a miscompiled special member, or a call the
// compiler should have elided appearing (or one it should have made going
// missing), shows up as a changed count.
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct Stats {
  int ctor = 0, copy = 0, move = 0, copyAssign = 0, moveAssign = 0, dtor = 0;
  void reset() { *this = Stats{}; }
  void print(const char *label) const {
    std::printf("%-34s ctor=%d copy=%d move=%d cassign=%d massign=%d dtor=%d\n", label, ctor,
                copy, move, copyAssign, moveAssign, dtor);
  }
};
Stats S;

struct Tracked {
  std::string payload;
  Tracked() : payload("default") { ++S.ctor; }
  explicit Tracked(std::string p) : payload(std::move(p)) { ++S.ctor; }
  Tracked(const Tracked &o) : payload(o.payload) { ++S.copy; }
  Tracked(Tracked &&o) noexcept : payload(std::move(o.payload)) { ++S.move; }
  Tracked &operator=(const Tracked &o) {
    payload = o.payload;
    ++S.copyAssign;
    return *this;
  }
  Tracked &operator=(Tracked &&o) noexcept {
    payload = std::move(o.payload);
    ++S.moveAssign;
    return *this;
  }
  ~Tracked() { ++S.dtor; }
};

// Same, but the move constructor may throw, so vector growth copies.
struct ThrowingMove {
  int v;
  explicit ThrowingMove(int v) : v(v) { ++S.ctor; }
  ThrowingMove(const ThrowingMove &o) : v(o.v) { ++S.copy; }
  ThrowingMove(ThrowingMove &&o) : v(o.v) { ++S.move; }
  ~ThrowingMove() { ++S.dtor; }
};

struct MoveOnly {
  std::unique_ptr<int> p;
  explicit MoveOnly(int v) : p(std::make_unique<int>(v)) {}
  MoveOnly(MoveOnly &&) = default;
  MoveOnly &operator=(MoveOnly &&) = default;
};

Tracked makePrvalue() { return Tracked("prvalue"); }

Tracked makeNamed(bool flag) {
  Tracked t("named");
  t.payload += flag ? "+" : "-";
  return t;  // NRVO candidate
}

Tracked makeEitherOr(bool flag) {
  Tracked a("a"), b("b");
  if (flag) return a;  // two candidates: implicit move
  return b;
}

Tracked passThrough(Tracked t) { return t; }  // parameter: implicit move

const char *category(int &) { return "lvalue"; }
const char *category(const int &) { return "const lvalue"; }
const char *category(int &&) { return "rvalue"; }

template <class T>
const char *forwarded(T &&x) {
  return category(std::forward<T>(x));
}
template <class T>
const char *notForwarded(T &&x) {
  return category(x);
}

template <class... Args>
void describeAll(Args &&...args) {
  ((std::printf(" %s", category(std::forward<Args>(args)))), ...);
  std::printf("\n");
}

template <class T, class... Args>
std::unique_ptr<T> factory(Args &&...args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

struct Pair {
  Tracked a, b;
  template <class A, class B>
  Pair(A &&x, B &&y) : a(std::forward<A>(x)), b(std::forward<B>(y)) {}
};

template <class T>
constexpr const char *refKind() {
  if (std::is_lvalue_reference_v<T>) return "T&";
  if (std::is_rvalue_reference_v<T>) return "T&&";
  return "T";
}

}  // namespace

int main() {
  std::printf("== basics\n");
  {
    S.reset();
    Tracked a("x");
    Tracked b = a;
    Tracked c = std::move(a);
    b = c;
    c = std::move(b);
    S.print("copy, move, assign:");
    std::printf("a.payload=\"%s\" b.payload=\"%s\" c.payload=\"%s\"\n", a.payload.c_str(),
                b.payload.c_str(), c.payload.c_str());
  }
  S.print("after scope:");

  std::printf("== elision\n");
  {
    S.reset();
    Tracked p = makePrvalue();
    S.print("prvalue return:");
    S.reset();
    Tracked n = makeNamed(true);
    S.print("named return:");
    S.reset();
    Tracked e = makeEitherOr(false);
    S.print("either-or return:");
    S.reset();
    Tracked t = passThrough(Tracked("param"));
    S.print("param pass-through:");
    S.reset();
    Tracked chained = Tracked(Tracked(Tracked("nest")));
    S.print("nested prvalues:");
    std::printf("payloads: %s %s %s %s %s\n", p.payload.c_str(), n.payload.c_str(),
                e.payload.c_str(), t.payload.c_str(), chained.payload.c_str());
    S.reset();
  }
  S.print("destroying five:");

  std::printf("== containers\n");
  {
    S.reset();
    std::vector<Tracked> v;
    v.reserve(8);
    Tracked local("local");
    v.push_back(local);
    v.push_back(std::move(local));
    v.push_back(Tracked("temp"));
    v.emplace_back("emplaced");
    v.emplace_back();
    S.print("push/emplace with reserve:");
    S.reset();
    v.insert(v.begin(), Tracked("front"));
    S.print("insert at front:");
    S.reset();
    v.erase(v.begin() + 1);
    S.print("erase second:");
    S.reset();
    Tracked out = std::move(v.back());
    v.pop_back();
    S.print("move out of back:");
    for (auto &x : v) std::printf(" [%s]", x.payload.c_str());
    std::printf(" out=%s\n", out.payload.c_str());
    S.reset();
    std::vector<Tracked> w = std::move(v);
    S.print("move whole vector:");
    std::printf("v.size=%zu w.size=%zu\n", v.size(), w.size());
    S.reset();
    std::vector<Tracked> copyOfW = w;
    S.print("copy whole vector:");
  }

  std::printf("== growth: noexcept move vs throwing move\n");
  {
    S.reset();
    std::vector<Tracked> v;
    for (int i = 0; i < 9; i++) v.emplace_back("g");
    S.print("noexcept move, 9 growths:");
    std::printf("capacity=%zu\n", v.capacity());
    S.reset();
    std::vector<ThrowingMove> u;
    for (int i = 0; i < 9; i++) u.emplace_back(i);
    S.print("throwing move, 9 growths:");
    std::printf("is_nothrow_move: Tracked=%d ThrowingMove=%d\n",
                (int)std::is_nothrow_move_constructible_v<Tracked>,
                (int)std::is_nothrow_move_constructible_v<ThrowingMove>);
  }

  std::printf("== move-only\n");
  {
    std::vector<MoveOnly> mv;
    for (int i = 0; i < 6; i++) mv.emplace_back(i * i);
    MoveOnly taken = std::move(mv[2]);
    std::printf("taken=%d mv[2].p null=%d\n", *taken.p, mv[2].p == nullptr);
    mv[2] = std::move(taken);
    std::map<int, MoveOnly> m;
    for (auto &x : mv) m.emplace(*x.p, std::move(x));
    for (auto &[k, x] : m) std::printf(" %d:%d", k, *x.p);
    std::printf("\nall moved out: %d\n",
                (int)std::all_of(mv.begin(), mv.end(), [](auto &x) { return !x.p; }));
    std::printf("copyable=%d movable=%d\n", (int)std::is_copy_constructible_v<MoveOnly>,
                (int)std::is_move_constructible_v<MoveOnly>);
  }

  std::printf("== perfect forwarding\n");
  {
    int x = 1;
    const int cx = 2;
    std::printf("forwarded: %s %s %s %s\n", forwarded(x), forwarded(cx), forwarded(3),
                forwarded(std::move(x)));
    std::printf("not forwarded: %s %s %s\n", notForwarded(x), notForwarded(cx),
                notForwarded(3));
    std::printf("describeAll:");
    describeAll(x, cx, 4, std::move(x), static_cast<const int &>(x));
    S.reset();
    Tracked lv("lv");
    Pair p1(lv, Tracked("rv"));
    S.print("Pair(lvalue, rvalue):");
    S.reset();
    Pair p2(std::string("s1"), "s2");
    S.print("Pair(string, const char*):");
    S.reset();
    auto up = factory<Tracked>(std::string("made"));
    auto up2 = factory<Tracked>(lv);
    auto up3 = factory<Tracked>(std::move(lv));
    S.print("factory x3:");
    std::printf("p1=%s,%s p2=%s,%s up=%s up2=%s up3=%s lv=\"%s\"\n", p1.a.payload.c_str(),
                p1.b.payload.c_str(), p2.a.payload.c_str(), p2.b.payload.c_str(),
                up->payload.c_str(), up2->payload.c_str(), up3->payload.c_str(),
                lv.payload.c_str());
  }

  std::printf("== exchange and swap\n");
  {
    S.reset();
    Tracked a("A"), b("B");
    std::swap(a, b);
    S.print("std::swap:");
    S.reset();
    Tracked old = std::exchange(a, Tracked("C"));
    S.print("std::exchange:");
    std::printf("a=%s b=%s old=%s\n", a.payload.c_str(), b.payload.c_str(),
                old.payload.c_str());
    int counter = 5;
    int prev = std::exchange(counter, counter * 10);
    std::printf("prev=%d counter=%d\n", prev, counter);
  }

  std::printf("== moved-from standard types\n");
  {
    std::string s(100, 'z');
    std::string t = std::move(s);
    std::printf("string: moved-from empty=%d target size=%zu\n", s.empty(), t.size());
    std::vector<int> v(50, 1);
    std::vector<int> w = std::move(v);
    std::printf("vector: moved-from size=%zu target size=%zu\n", v.size(), w.size());
    auto u = std::make_unique<int>(9);
    auto u2 = std::move(u);
    std::printf("unique_ptr: moved-from null=%d target=%d\n", u == nullptr, *u2);
    std::optional<std::string> o = std::string("opt");
    std::optional<std::string> o2 = std::move(o);
    std::printf("optional: moved-from engaged=%d target=%s\n", o.has_value(), o2->c_str());
    s = "reused";
    std::printf("reassigned moved-from string: %s\n", s.c_str());
  }

  std::printf("== reference collapsing\n");
  {
    using L = int &;
    using R = int &&;
    std::printf("L&=%s L&&=%s R&=%s R&&=%s\n", refKind<L &>(), refKind<L &&>(),
                refKind<R &>(), refKind<R &&>());
    int i = 0;
    auto &&a = i;
    auto &&b = 5;
    std::printf("auto&& from lvalue=%s from prvalue=%s\n", refKind<decltype(a)>(),
                refKind<decltype(b)>());
    std::printf("decltype(i)=%s decltype((i))=%s decltype(std::move(i))=%s\n",
                refKind<decltype(i)>(), refKind<decltype((i))>(),
                refKind<decltype(std::move(i))>());
  }
  return 0;
}
