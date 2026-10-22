// Exceptions thrown from inside library code and the state left behind:
// a comparator that throws in the middle of std::sort and std::stable_sort
// (the range must still be a permutation of its input), a copy constructor
// that throws during vector growth and insert (the strong guarantee leaves
// the vector unchanged), during map and unordered_map insertion (no node
// leaks, size unchanged), during std::variant assignment (valueless_by_
// exception), from a callback inside std::function, std::visit and
// std::for_each, from an iterator's operator* consumed by std::copy, and from
// a coroutine-free state machine driven by std::accumulate.  Also exception
// types with multiple and virtual inheritance caught through each of their
// bases, an exception thrown from a destructor that is noexcept(false)
// while no other exception is active, and an object count that must be
// back to zero at the end.
//
// Every one of these is an unwind out of a libc++ template instantiated
// into this module, through frames whose cleanups restore container
// invariants, so the landing pads that matter are the library's own.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <iterator>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {

int g_live = 0;
int g_copiesUntilThrow = -1;  // -1: never throw

struct Bomb {
  int v;
  explicit Bomb(int x) : v(x) { ++g_live; }
  Bomb(const Bomb &o) : v(o.v) {
    if (g_copiesUntilThrow == 0) throw std::runtime_error("copy bomb " + std::to_string(v));
    if (g_copiesUntilThrow > 0) --g_copiesUntilThrow;
    ++g_live;
  }
  Bomb &operator=(const Bomb &o) {
    if (g_copiesUntilThrow == 0) throw std::runtime_error("assign bomb");
    if (g_copiesUntilThrow > 0) --g_copiesUntilThrow;
    v = o.v;
    return *this;
  }
  ~Bomb() { --g_live; }
  bool operator<(const Bomb &o) const { return v < o.v; }
  bool operator==(const Bomb &o) const { return v == o.v; }
};

struct BombHash {
  std::size_t operator()(const Bomb &b) const { return (std::size_t)b.v * 2654435761u; }
};

std::string dumpV(const std::vector<int> &v) {
  std::string s;
  for (int x : v) s += std::to_string(x) + " ";
  return s;
}
std::string dumpB(const std::vector<Bomb> &v) {
  std::string s;
  for (auto &b : v) s += std::to_string(b.v) + " ";
  return s;
}

// Exception hierarchy with multiple and virtual inheritance.
struct ErrBase {
  virtual ~ErrBase() = default;
  virtual const char *who() const { return "ErrBase"; }
};
struct NetErr : virtual ErrBase {
  const char *who() const override { return "NetErr"; }
};
struct IoErr : virtual ErrBase {
  const char *who() const override { return "IoErr"; }
};
struct SocketErr : NetErr, IoErr {
  int fd;
  explicit SocketErr(int f) : fd(f) {}
  const char *who() const override { return "SocketErr"; }
};
struct Tagged {
  int tag = 99;
};
struct Mixed : std::runtime_error, Tagged {
  Mixed() : std::runtime_error("mixed") {}
};

struct ThrowingDtor {
  bool armed;
  ~ThrowingDtor() noexcept(false) {
    if (armed) throw std::logic_error("thrown from destructor");
  }
};

struct ThrowingIter {
  using iterator_category = std::input_iterator_tag;
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  using pointer = const int *;
  using reference = int;
  int i;
  int failAt;
  int operator*() const {
    if (i == failAt) throw std::out_of_range("iterator at " + std::to_string(i));
    return i * i;
  }
  ThrowingIter &operator++() {
    ++i;
    return *this;
  }
  ThrowingIter operator++(int) {
    auto t = *this;
    ++i;
    return t;
  }
  bool operator==(const ThrowingIter &o) const { return i == o.i; }
};

}  // namespace

int main() {
  std::printf("== throwing comparators\n");
  for (int limit : {5, 50, 200, 100000}) {
    std::vector<int> v(64);
    for (int i = 0; i < 64; i++) v[i] = (i * 37 + 11) % 64;
    std::vector<int> orig = v;
    int calls = 0;
    try {
      std::sort(v.begin(), v.end(), [&](int a, int b) {
        if (++calls == limit) throw std::runtime_error("comparator gave up");
        return a < b;
      });
      std::printf("sort limit=%d completed sorted=%d\n", limit, std::is_sorted(v.begin(), v.end()));
    } catch (const std::exception &e) {
      std::printf("sort limit=%d threw '%s' after %d calls, permutation=%d\n", limit, e.what(), calls,
                  std::is_permutation(v.begin(), v.end(), orig.begin()));
    }
    std::vector<int> s = orig;
    calls = 0;
    try {
      std::stable_sort(s.begin(), s.end(), [&](int a, int b) {
        if (++calls == limit) throw std::runtime_error("stable comparator gave up");
        return a < b;
      });
      std::printf("stable_sort limit=%d completed sorted=%d\n", limit, std::is_sorted(s.begin(), s.end()));
    } catch (const std::exception &e) {
      std::printf("stable_sort limit=%d threw after %d calls, permutation=%d\n", limit, calls,
                  std::is_permutation(s.begin(), s.end(), orig.begin()));
    }
  }

  std::printf("== vector strong guarantee\n");
  {
    std::vector<Bomb> v;
    v.reserve(4);
    for (int i = 0; i < 4; i++) v.emplace_back(i);
    std::printf("before: %s cap=%zu live=%d\n", dumpB(v).c_str(), v.capacity(), g_live);
    for (int k : {0, 2, 3}) {
      g_copiesUntilThrow = k;  // Bomb has a throwing copy and no move: growth copies
      try {
        v.push_back(Bomb(100 + k));
        std::printf("push_back k=%d ok\n", k);
      } catch (const std::exception &e) {
        std::printf("push_back k=%d threw '%s': %s size=%zu cap=%zu live=%d\n", k, e.what(), dumpB(v).c_str(),
                    v.size(), v.capacity(), g_live);
      }
      g_copiesUntilThrow = -1;
    }
    g_copiesUntilThrow = 1;
    try {
      v.insert(v.begin() + 1, Bomb(500));
    } catch (const std::exception &e) {
      std::printf("insert threw '%s': size=%zu live=%d\n", e.what(), v.size(), g_live);
    }
    g_copiesUntilThrow = -1;
    std::printf("after: %s live=%d\n", dumpB(v).c_str(), g_live);
    g_copiesUntilThrow = 2;
    try {
      std::vector<Bomb> copy = v;
      std::printf("vector copy ok (unexpected)\n");
    } catch (const std::exception &e) {
      std::printf("vector copy threw, live=%d (partial copy destroyed)\n", g_live);
    }
    g_copiesUntilThrow = -1;
  }
  std::printf("live after vector scope=%d\n", g_live);

  std::printf("== node containers\n");
  {
    std::map<int, Bomb> m;
    for (int i = 0; i < 5; i++) m.emplace(i, Bomb(i * 10));
    Bomb extra(77);
    g_copiesUntilThrow = 0;
    try {
      m.emplace(9, extra);
    } catch (const std::exception &e) {
      std::printf("map insert threw: size=%zu contains(9)=%d live=%d\n", m.size(), m.count(9) > 0, g_live);
    }
    g_copiesUntilThrow = -1;
    std::unordered_map<Bomb, int, BombHash> um;
    for (int i = 0; i < 20; i++) um.emplace(Bomb(i), i);
    g_copiesUntilThrow = 0;
    try {
      um.emplace(extra, 1);
    } catch (const std::exception &e) {
      std::printf("unordered_map emplace threw: size=%zu live=%d\n", um.size(), g_live);
    }
    g_copiesUntilThrow = -1;
    long sum = 0;
    for (auto &[k, v] : um) sum += k.v * v;
    std::printf("unordered_map intact sum=%ld\n", sum);
  }
  std::printf("live after node scope=%d\n", g_live);

  std::printf("== variant valueless\n");
  {
    struct Thrower {
      Thrower() = default;
      Thrower(const Thrower &) { throw std::runtime_error("variant alt copy"); }
    };
    std::variant<int, std::string, Thrower> var = std::string("held");
    Thrower t;
    try {
      var = t;
    } catch (const std::exception &e) {
      std::printf("assignment threw '%s', valueless=%d index_npos=%d\n", e.what(), var.valueless_by_exception(),
                  var.index() == std::variant_npos);
    }
    try {
      std::visit([](auto &&) { std::printf("not reached\n"); }, var);
    } catch (const std::bad_variant_access &) {
      std::printf("visit on valueless throws bad_variant_access\n");
    }
    var = 42;
    std::printf("reassigned index=%zu value=%d\n", var.index(), std::get<int>(var));
  }

  std::printf("== callbacks\n");
  {
    std::function<int(int)> f = [](int x) {
      if (x > 3) throw std::invalid_argument("x too big: " + std::to_string(x));
      return x * 2;
    };
    std::vector<int> in{1, 2, 3, 4, 5}, out;
    try {
      std::transform(in.begin(), in.end(), std::back_inserter(out), f);
    } catch (const std::exception &e) {
      std::printf("transform stopped: %s, out=%s\n", e.what(), dumpV(out).c_str());
    }
    int seen = 0;
    try {
      std::for_each(in.begin(), in.end(), [&](int x) {
        ++seen;
        if (x == 3) throw x;
      });
    } catch (int x) {
      std::printf("for_each threw int %d after %d calls\n", x, seen);
    }
    std::variant<int, double> vv = 2.5;
    try {
      std::visit([](auto x) {
        if constexpr (std::is_same_v<decltype(x), double>) throw std::domain_error("double not allowed");
      }, vv);
    } catch (const std::domain_error &e) {
      std::printf("visit threw: %s\n", e.what());
    }
    try {
      int total = std::accumulate(in.begin(), in.end(), 0, [](int acc, int x) {
        if (acc > 5) throw std::overflow_error("accumulator overflow at " + std::to_string(x));
        return acc + x;
      });
      std::printf("accumulate=%d\n", total);
    } catch (const std::overflow_error &e) {
      std::printf("accumulate threw: %s\n", e.what());
    }
    std::vector<int> copied;
    try {
      std::copy(ThrowingIter{0, 6}, ThrowingIter{10, 6}, std::back_inserter(copied));
    } catch (const std::out_of_range &e) {
      std::printf("copy from throwing iterator: %s, copied=%s\n", e.what(), dumpV(copied).c_str());
    }
  }

  std::printf("== exception types with complex bases\n");
  for (int k = 0; k < 4; k++) {
    try {
      switch (k) {
        case 0: throw SocketErr(7);
        case 1: throw NetErr();
        case 2: throw Mixed();
        case 3: throw IoErr();
      }
    } catch (const NetErr &e) {
      std::printf("k=%d caught as NetErr, who=%s\n", k, e.who());
    } catch (const ErrBase &e) {
      std::printf("k=%d caught as ErrBase, who=%s\n", k, e.who());
    } catch (const Tagged &t) {
      std::printf("k=%d caught as Tagged, tag=%d\n", k, t.tag);
    }
  }
  try {
    throw SocketErr(11);
  } catch (const IoErr &e) {
    const SocketErr *s = dynamic_cast<const SocketErr *>(&e);
    std::printf("SocketErr via IoErr: who=%s fd=%d\n", e.who(), s ? s->fd : -1);
  }
  try {
    throw Mixed();
  } catch (const std::exception &e) {
    std::printf("Mixed via std::exception: %s\n", e.what());
  }

  std::printf("== throwing destructor\n");
  try {
    ThrowingDtor d{true};
    std::printf("leaving scope normally\n");
  } catch (const std::logic_error &e) {
    std::printf("caught: %s\n", e.what());
  }
  std::printf("final live=%d\n", g_live);
  return 0;
}
