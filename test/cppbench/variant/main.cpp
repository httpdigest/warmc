// std::variant and std::visit over a stream of tagged values: a little stack
// machine whose operands are integers, doubles or 2-vectors and whose
// operations dispatch on the pair of alternatives they find.
//
// A closed sum type, so the alternatives are known to the compiler, but
// libc++'s visit still goes through a table of function pointers indexed by
// the stored index -- a two-argument visit is a table of tables.  The row is
// that dispatch plus the copying of variants in and out of a vector.
#include <cstdint>
#include <cstdio>
#include <variant>
#include <vector>

namespace {

struct Vec2 {
  double x, y;
};

using Value = std::variant<int64_t, double, Vec2>;

template <class... Fs>
struct Overload : Fs... {
  using Fs::operator()...;
};
template <class... Fs>
Overload(Fs...) -> Overload<Fs...>;

Value add(const Value &a, const Value &b) {
  return std::visit(
      Overload{
          [](int64_t p, int64_t q) -> Value { return p + q; },
          [](int64_t p, double q) -> Value { return static_cast<double>(p) + q; },
          [](double p, int64_t q) -> Value { return p + static_cast<double>(q); },
          [](double p, double q) -> Value { return p + q; },
          [](const Vec2 &p, const Vec2 &q) -> Value { return Vec2{p.x + q.x, p.y + q.y}; },
          [](const Vec2 &p, int64_t q) -> Value { return Vec2{p.x * q, p.y * q}; },
          [](const Vec2 &p, double q) -> Value { return Vec2{p.x * q, p.y * q}; },
          [](int64_t p, const Vec2 &q) -> Value { return q.x * p + q.y; },
          [](double p, const Vec2 &q) -> Value { return q.x * p - q.y; },
      },
      a, b);
}

double weight(const Value &v) {
  return std::visit(Overload{
                        [](int64_t p) { return static_cast<double>(p % 1000); },
                        [](double p) { return p; },
                        [](const Vec2 &p) { return p.x * p.x + p.y * p.y; },
                    },
                    v);
}

}  // namespace

int main() {
  std::vector<Value> stack;
  uint32_t x = 7654321u;
  double total = 0;
  int64_t ints = 0;
  for (int i = 0; i < 5000000; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    switch (x % 5) {
      case 0: stack.emplace_back(static_cast<int64_t>(x % 100)); break;
      case 1: stack.emplace_back((x % 1000) * 0.001); break;
      case 2: stack.emplace_back(Vec2{(x & 255) * 0.01, ((x >> 8) & 255) * 0.01}); break;
      default:
        if (stack.size() >= 2) {
          Value b = stack.back();
          stack.pop_back();
          Value a = stack.back();
          stack.pop_back();
          Value r = add(a, b);
          // Keep magnitudes bounded so the checksum stays exact across engines.
          if (weight(r) > 1e6) r = int64_t{1};
          stack.push_back(r);
        }
        break;
    }
    if (stack.size() > 64) {
      for (const Value &v : stack) total += weight(v);
      ints += std::holds_alternative<int64_t>(stack.front());
      stack.clear();
    }
  }
  std::printf("total = %.9e ints = %lld\n", total, static_cast<long long>(ints));
  return 0;
}
