// Lambdas: capture by value and by reference, default captures, init
// captures (including move-only values moved into the closure), mutable
// closures with their own state, capturing this and *this, generic lambdas
// and lambdas with explicit template parameters, immediately invoked
// lambdas, lambdas returning lambdas (currying and composition), recursion
// through std::function and through a self parameter, closures stored in
// containers and sorted by, and the copy semantics of closures.
//
// A closure is a compiler-generated class; what this checks is that the
// captured state is laid out, copied and updated the way the source says,
// whether the call is inlined or goes through std::function's type-erased
// thunk.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Widget {
  int base = 10;
  std::string name = "widget";
  auto adderByThis() {
    return [this](int x) { return base + x; };
  }
  auto adderByCopy() {
    return [*this](int x) { return base + x; };
  }
  auto renamer() {
    return [this](const std::string &n) {
      name = n;
      return name.size();
    };
  }
};

std::function<int(int)> makeAccumulator(int start) {
  return [sum = start](int x) mutable {
    sum += x;
    return sum;
  };
}

auto compose = [](auto f, auto g) { return [=](auto x) { return f(g(x)); }; };

auto curry3 = [](auto f) {
  return [=](auto a) {
    return [=](auto b) { return [=](auto c) { return f(a, b, c); }; };
  };
};

}  // namespace

int main() {
  std::printf("== captures\n");
  {
    int a = 1, b = 2;
    auto byVal = [a, b] { return a * 10 + b; };
    auto byRef = [&a, &b] { return a * 10 + b; };
    auto mixed = [a, &b] { return a * 10 + b; };
    auto allVal = [=] { return a + b; };
    auto allRef = [&] {
      a++;
      b++;
      return a + b;
    };
    std::printf("before: val=%d ref=%d mixed=%d allVal=%d\n", byVal(), byRef(),
                mixed(), allVal());
    std::printf("allRef=%d a=%d b=%d\n", allRef(), a, b);
    a = 7;
    b = 8;
    std::printf("after: val=%d ref=%d mixed=%d allVal=%d\n", byVal(), byRef(), mixed(),
                allVal());
  }

  std::printf("== init captures and move-only\n");
  {
    auto p = std::make_unique<std::vector<int>>(std::vector<int>{1, 2, 3, 4});
    auto owner = [v = std::move(p), scale = 3]() {
      int s = 0;
      for (int x : *v) s += x * scale;
      return s;
    };
    std::printf("p is %s, owner()=%d\n", p ? "set" : "null", owner());
    std::string text = "hello";
    auto upper = [s = text + " world"]() mutable {
      for (auto &c : s) c = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
      return s;
    };
    std::printf("%s / %s\n", upper().c_str(), text.c_str());
    auto counter = [n = 0]() mutable { return ++n; };
    auto copy = counter;
    int c1 = counter(), c2 = counter(), c3 = counter();
    int d1 = copy(), d2 = copy();
    std::printf("counter: %d %d %d, copy: %d %d\n", c1, c2, c3, d1, d2);
    auto copy2 = counter;  // copies the advanced state
    int e1 = copy2(), e2 = counter();
    std::printf("copy2: %d counter: %d\n", e1, e2);
  }

  std::printf("== mutable state\n");
  {
    auto fib = [a = 0ll, b = 1ll]() mutable {
      auto r = a;
      a = b;
      b = r + b;
      return r;
    };
    for (int i = 0; i < 25; i++) std::printf("%lld ", fib());
    std::printf("\n");
    auto acc = makeAccumulator(100);
    for (int i = 1; i <= 5; i++) std::printf("acc(%d)=%d\n", i, acc(i));
    auto acc2 = acc;  // std::function copies the closure
    int r2 = acc2(1000);
    int r1 = acc(0);
    std::printf("acc2(1000)=%d acc(0)=%d\n", r2, r1);
  }

  std::printf("== this and *this\n");
  {
    Widget w;
    auto byThis = w.adderByThis();
    auto byCopy = w.adderByCopy();
    auto ren = w.renamer();
    std::printf("byThis(5)=%d byCopy(5)=%d\n", byThis(5), byCopy(5));
    w.base = 1000;
    std::printf("after base=1000: byThis(5)=%d byCopy(5)=%d\n", byThis(5), byCopy(5));
    std::size_t len = ren("gizmo-9000");
    std::printf("rename -> %zu name=%s\n", len, w.name.c_str());
  }

  std::printf("== generic lambdas\n");
  {
    auto plus = [](auto x, auto y) { return x + y; };
    std::printf("int=%d double=%.2f string=%s\n", plus(2, 3), plus(1.5, 2.25),
                plus(std::string("ab"), std::string("cd")).c_str());
    auto sizeOf = []<class T>(const std::vector<T> &v) { return v.size() * sizeof(T); };
    std::printf("bytes: ints=%zu doubles=%zu chars=%zu\n", sizeOf(std::vector<int>(5)),
                sizeOf(std::vector<double>(5)), sizeOf(std::vector<char>(5)));
    auto variadic = [](auto... xs) { return (0 + ... + xs) * (int)sizeof...(xs); };
    std::printf("variadic()=%d variadic(1,2,3)=%d\n", variadic(), variadic(1, 2, 3));
    auto first = [](auto head, auto...) { return head; };
    std::printf("first=%s\n", first(std::string("one"), 2, 3.0).c_str());
  }

  std::printf("== immediately invoked\n");
  {
    const int table = [] {
      int s = 0;
      for (int i = 1; i <= 100; i++) s += i % 7 == 0 ? i : 0;
      return s;
    }();
    const std::vector<int> squares = [](int n) {
      std::vector<int> v(n);
      for (int i = 0; i < n; i++) v[i] = i * i;
      return v;
    }(12);
    std::printf("table=%d squares.back=%d size=%zu\n", table, squares.back(),
                squares.size());
  }

  std::printf("== higher order\n");
  {
    auto inc = [](int x) { return x + 1; };
    auto dbl = [](int x) { return x * 2; };
    auto sq = [](int x) { return x * x; };
    auto incThenDbl = compose(dbl, inc);
    auto dblThenInc = compose(inc, dbl);
    auto all = compose(sq, compose(dbl, inc));
    for (int x = -2; x <= 3; x++)
      std::printf("x=%2d incThenDbl=%3d dblThenInc=%3d all=%4d\n", x, incThenDbl(x),
                  dblThenInc(x), all(x));
    auto vol = curry3([](double a, double b, double c) { return a * b * c; });
    auto wa = vol(2.0);
    auto wab = wa(3.5);
    std::printf("vol=%.2f %.2f %.2f\n", wab(4.0), wab(0.5), wa(1.0)(1.0));
    auto adders = std::vector<std::function<int(int)>>{};
    for (int k = 0; k < 6; k++) adders.push_back([k](int x) { return x * 10 + k; });
    int chain = 0;
    for (auto &f : adders) chain = f(chain);
    std::printf("chain=%d\n", chain);
  }

  std::printf("== recursion\n");
  {
    std::function<long long(int)> fact = [&fact](int n) -> long long {
      return n <= 1 ? 1 : n * fact(n - 1);
    };
    for (int n = 0; n <= 20; n += 4) std::printf("fact(%d)=%lld\n", n, fact(n));
    auto ack = [](auto self, long m, long n) -> long {
      if (m == 0) return n + 1;
      if (n == 0) return self(self, m - 1, 1);
      return self(self, m - 1, self(self, m, n - 1));
    };
    for (long m = 0; m <= 2; m++)
      for (long n = 0; n <= 3; n++) std::printf("ack(%ld,%ld)=%ld\n", m, n, ack(ack, m, n));
    std::function<void(int, std::string &)> hanoiNames;
    int moves = 0;
    std::string trace;
    std::function<void(int, char, char, char)> hanoi = [&](int n, char from, char to,
                                                           char via) {
      if (n == 0) return;
      hanoi(n - 1, from, via, to);
      ++moves;
      if (moves <= 10) {
        trace += from;
        trace += to;
        trace += ' ';
      }
      hanoi(n - 1, via, to, from);
    };
    hanoi(10, 'A', 'C', 'B');
    std::printf("hanoi moves=%d first=%s\n", moves, trace.c_str());
  }

  std::printf("== sorting with closures\n");
  {
    std::vector<std::string> words = {"pear", "fig", "banana", "kiwi", "apple",
                                      "date", "cherry", "plum", "grape", "lime"};
    int comparisons = 0;
    auto byLenThenAlpha = [&comparisons](const std::string &a, const std::string &b) {
      ++comparisons;
      return a.size() != b.size() ? a.size() < b.size() : a < b;
    };
    std::sort(words.begin(), words.end(), byLenThenAlpha);
    for (auto &w : words) std::printf("%s ", w.c_str());
    std::printf("\ncomparisons>0: %d\n", comparisons > 0);
    std::map<char, int, std::function<bool(char, char)>> rev(
        [](char a, char b) { return a > b; });
    for (auto &w : words) rev[w[0]]++;
    for (auto &[c, n] : rev) std::printf("%c:%d ", c, n);
    std::printf("\n");
    std::vector<int> nums(20);
    std::iota(nums.begin(), nums.end(), -10);
    std::stable_sort(nums.begin(), nums.end(),
                     [](int a, int b) { return std::abs(a % 5) < std::abs(b % 5); });
    for (int n : nums) std::printf("%d ", n);
    std::printf("\n");
    int threshold = 3;
    auto cnt = std::count_if(nums.begin(), nums.end(),
                             [threshold](int n) { return n > threshold; });
    std::printf("count > %d = %ld\n", threshold, (long)cnt);
  }

  std::printf("== captured structured bindings and static/global use\n");
  {
    static int calls = 0;
    auto [x, y] = std::pair<int, double>(4, 0.5);
    auto f = [x, y] {
      ++calls;  // statics are not captured
      return x * y;
    };
    double f1 = f();
    double f2 = f();
    std::printf("f()=%.2f f()=%.2f calls=%d\n", f1, f2, calls);
    std::printf("closure sizes: empty=%zu int=%zu ref=%zu\n", sizeof([] {}),
                sizeof([x] { return x; }), sizeof([&x] { return x; }));
  }
  return 0;
}
