// std::function and the other callable wrappers: std::function holding free
// functions, function pointers, lambdas with small and large captures (the
// small-buffer case and the heap case), functors with state, member
// function pointers and member data pointers through std::invoke and
// std::mem_fn; std::bind with placeholders, reordered and nested; std::ref
// and std::cref; empty functions and std::bad_function_call; copying,
// moving, swapping and reassigning functions; target() and target_type();
// std::not_fn; and a table-driven dispatcher built from a map of functions.
//
// Every call through std::function is an indirect call through libc++'s
// type-erased vtable; the copy and destroy operations on the wrapper are
// too, and the logs printed by the stateful functors record that each of
// them happened the right number of times.
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

int g_copies = 0, g_dtors = 0;

int square(int x) { return x * x; }
int negate(int x) { return -x; }
long add3(int a, long b, short c) { return a + b + c; }
double divide(double a, double b) { return a / b; }
std::string join(const std::string &a, const std::string &b, char sep) {
  return a + sep + b;
}

struct Multiplier {
  int k;
  int calls = 0;
  explicit Multiplier(int k) : k(k) {}
  Multiplier(const Multiplier &o) : k(o.k), calls(o.calls) { ++g_copies; }
  ~Multiplier() { ++g_dtors; }
  int operator()(int x) {
    ++calls;
    return x * k;
  }
};

struct Big {
  long a[16];
  int operator()(int x) const {
    long s = x;
    for (long v : a) s += v;
    return (int)s;
  }
};

struct Account {
  std::string owner;
  long balance;
  long deposit(long amt) { return balance += amt; }
  long get() const { return balance; }
  bool richerThan(long amt) const { return balance > amt; }
};

void increment(int &x) { ++x; }

using Op = std::function<double(double, double)>;

}  // namespace

int main() {
  std::printf("== kinds of target\n");
  {
    std::function<int(int)> f = square;
    std::printf("square(7)=%d\n", f(7));
    f = &negate;
    std::printf("negate(7)=%d\n", f(7));
    f = [](int x) { return x + 100; };
    std::printf("lambda(7)=%d\n", f(7));
    int offset = 1000;
    f = [offset](int x) { return x + offset; };
    std::printf("capture(7)=%d\n", f(7));
    Big big{};
    for (int i = 0; i < 16; i++) big.a[i] = i * i;
    f = big;
    std::printf("big(7)=%d\n", f(7));
    std::vector<long> captured(50, 3);
    f = [captured](int x) { return x + (int)captured.size() * (int)captured[0]; };
    std::printf("vector capture(7)=%d\n", f(7));
    std::function<long(int, long, short)> g = add3;
    std::printf("add3=%ld\n", g(1, 2000000000L, 3));
  }

  std::printf("== stateful functors and copies\n");
  {
    g_copies = g_dtors = 0;
    Multiplier m(3);
    std::function<int(int)> f = m;  // copies m
    std::printf("f(1)=%d f(2)=%d f(3)=%d m.calls=%d\n", f(1), f(2), f(3), m.calls);
    auto *inner = f.target<Multiplier>();
    std::printf("inner calls=%d k=%d\n", inner ? inner->calls : -1, inner ? inner->k : -1);
    std::function<int(int)> g = f;  // copies the inner functor with its state
    g(10);
    std::printf("after g: f.calls=%d g.calls=%d\n", f.target<Multiplier>()->calls,
                g.target<Multiplier>()->calls);
    std::function<int(int)> h = std::move(g);
    std::printf("moved: h.calls=%d g empty=%d\n", h.target<Multiplier>()->calls, !g);
    std::printf("copies=%d dtors=%d\n", g_copies, g_dtors);
    std::function<int(int)> byRef = std::ref(m);
    byRef(5);
    byRef(6);
    std::printf("through std::ref m.calls=%d\n", m.calls);
  }
  std::printf("after scope copies=%d dtors=%d\n", g_copies, g_dtors);

  std::printf("== empty and bad_function_call\n");
  {
    std::function<void()> empty;
    std::printf("empty bool=%d ==nullptr=%d\n", (bool)empty, empty == nullptr);
    try {
      empty();
    } catch (const std::bad_function_call &e) {
      std::printf("caught bad_function_call\n");
    }
    empty = [] { std::printf("now callable\n"); };
    empty();
    empty = nullptr;
    std::printf("reset bool=%d\n", (bool)empty);
    std::function<int(int)> fromNullPtr = static_cast<int (*)(int)>(nullptr);
    std::printf("from null fn ptr bool=%d\n", (bool)fromNullPtr);
  }

  std::printf("== swap and target_type\n");
  {
    std::function<int(int)> a = square, b = [](int x) { return x * 3; };
    std::printf("a(4)=%d b(4)=%d\n", a(4), b(4));
    a.swap(b);
    std::printf("swapped a(4)=%d b(4)=%d\n", a(4), b(4));
    std::swap(a, b);
    std::printf("swapped back a(4)=%d b(4)=%d\n", a(4), b(4));
    std::printf("a holds fn ptr=%d b holds fn ptr=%d\n",
                a.target_type() == typeid(int (*)(int)),
                b.target_type() == typeid(int (*)(int)));
    std::printf("*a.target=%d\n", (*a.target<int (*)(int)>())(9));
  }

  std::printf("== invoke and mem_fn\n");
  {
    Account acc{"ann", 100};
    long (Account::*dep)(long) = &Account::deposit;
    std::printf("invoke deposit=%ld\n", std::invoke(dep, acc, 50));
    std::printf("invoke via ptr=%ld\n", std::invoke(dep, &acc, 25));
    std::printf("invoke data member=%ld owner=%s\n", std::invoke(&Account::balance, acc),
                std::invoke(&Account::owner, acc).c_str());
    std::printf("invoke free=%d lambda=%d\n", std::invoke(square, 12),
                std::invoke([](int a, int b) { return a - b; }, 10, 3));
    auto getter = std::mem_fn(&Account::get);
    auto rich = std::mem_fn(&Account::richerThan);
    std::vector<Account> accounts = {{"a", 10}, {"b", 500}, {"c", 175}, {"d", 9000}};
    for (auto &x : accounts)
      std::printf("%s get=%ld rich(150)=%d\n", x.owner.c_str(), getter(x), rich(x, 150));
    std::function<long(Account &, long)> fdep = &Account::deposit;
    fdep(accounts[0], 5);
    std::function<long(const Account &)> fbal = &Account::balance;
    std::printf("member through function: %ld\n", fbal(accounts[0]));
  }

  std::printf("== bind\n");
  {
    using namespace std::placeholders;
    auto half = std::bind(divide, _1, 2.0);
    auto recip = std::bind(divide, 1.0, _1);
    auto flipped = std::bind(divide, _2, _1);
    std::printf("half(9)=%.3f recip(8)=%.4f flipped(2,10)=%.2f\n", half(9), recip(8),
                flipped(2, 10));
    auto dashJoin = std::bind(join, _1, _2, '-');
    auto rev = std::bind(join, _2, _1, '+');
    std::printf("%s %s\n", dashJoin("left", "right").c_str(), rev("left", "right").c_str());
    auto nested = std::bind(divide, std::bind(square, _1), _2);
    std::printf("nested(6,4)=%.2f\n", nested(6, 4));
    int counter = 0;
    auto incRef = std::bind(increment, std::ref(counter));
    auto incCopy = std::bind(increment, counter);
    incRef();
    incRef();
    incCopy();
    std::printf("counter=%d\n", counter);
    Account acc{"zed", 1};
    auto depositTen = std::bind(&Account::deposit, &acc, 10);
    depositTen();
    depositTen();
    std::printf("zed balance=%ld\n", acc.balance);
    auto notRich = std::not_fn(std::bind(&Account::richerThan, _1, 15));
    std::printf("notRich(zed)=%d\n", notRich(acc));
  }

  std::printf("== dispatcher\n");
  {
    std::map<std::string, Op> ops;
    ops["+"] = [](double a, double b) { return a + b; };
    ops["-"] = [](double a, double b) { return a - b; };
    ops["*"] = std::multiplies<double>();
    ops["/"] = divide;
    ops["max"] = [](double a, double b) { return a > b ? a : b; };
    ops["pow"] = [](double a, double b) {
      double r = 1;
      for (int i = 0; i < (int)b; i++) r *= a;
      return r;
    };
    struct Step {
      const char *op;
      double arg;
    };
    Step prog[] = {{"+", 5},  {"*", 3},   {"-", 2},     {"/", 4},  {"pow", 3},
                   {"max", 100}, {"-", 0.5}, {"bogus", 1}, {"*", -1}};
    double acc = 1;
    for (auto &s : prog) {
      auto it = ops.find(s.op);
      if (it == ops.end()) {
        std::printf("%-5s %7.3f -> unknown op\n", s.op, s.arg);
        continue;
      }
      acc = it->second(acc, s.arg);
      std::printf("%-5s %7.3f -> %.6f\n", s.op, s.arg, acc);
    }
  }

  std::printf("== std function objects\n");
  {
    std::plus<int> p;
    std::minus<> m;
    std::modulus<int> mod;
    std::greater<std::string> gt;
    std::logical_and<> land;
    std::bit_xor<unsigned> bx;
    std::negate<double> neg;
    std::printf("plus=%d minus=%.1f mod=%d gt=%d land=%d bx=%u neg=%.1f\n", p(2, 3),
                m(5.5, 2), mod(17, 5), gt("b", "a"), land(true, false), bx(0xF0u, 0x3Cu),
                neg(2.5));
    std::vector<std::function<int(int)>> pipeline = {square, negate,
                                                     [](int x) { return x + 7; },
                                                     Multiplier(2)};
    for (int x = -3; x <= 3; x++) {
      int v = x;
      for (auto &f : pipeline) v = f(v);
      std::printf("pipeline(%d)=%d\n", x, v);
    }
  }
  return 0;
}
