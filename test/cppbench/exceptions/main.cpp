// C++ exceptions thrown through a few frames and caught, some of them
// rethrown, with destructors to run on the way out of every frame.
//
// Built with -fwasm-exceptions, so a throw is libc++abi allocating the
// exception and calling _Unwind_RaiseException, which executes wasm's own
// throw with the C++ tag; every frame with a destructor or a handler is a
// try_table the unwinder lands in, runs its cleanup and rethrows from.
// ktbench/exn is the same idea on the wasm GC side with no runtime library
// in the way; here most of the cost is that library.
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

uint64_t g_cleanups;

struct Guard {
  uint32_t v;
  explicit Guard(uint32_t v) : v(v) {}
  ~Guard() { g_cleanups += v & 7; }
};

struct ParseError : std::runtime_error {
  uint32_t code;
  ParseError(const char *what, uint32_t code) : std::runtime_error(what), code(code) {}
};

__attribute__((noinline)) uint32_t leaf(uint32_t x) {
  Guard g(x);
  if (x % 7 == 0) throw ParseError("divisible by seven", x);
  if (x % 13 == 0) throw std::out_of_range("thirteen");
  return x * 3 + 1;
}

__attribute__((noinline)) uint32_t middle(uint32_t x) {
  Guard g(x >> 3);
  uint32_t r = leaf(x) + leaf(x + 1);
  return r ^ (x << 1);
}

__attribute__((noinline)) uint32_t outer(uint32_t x) {
  Guard g(x >> 5);
  try {
    return middle(x);
  } catch (const ParseError &e) {
    if (e.code % 3 == 0) throw;
    return e.code;
  }
}

}  // namespace

int main() {
  uint64_t sum = 0, parse = 0, range = 0;
  for (uint32_t i = 1; i < 600000; i++) {
    try {
      sum += outer(i);
    } catch (const ParseError &e) {
      parse += e.code;
    } catch (const std::exception &e) {
      range += std::string(e.what()).size() + i;
    }
  }
  std::printf("sum = %llu parse = %llu range = %llu cleanups = %llu\n",
              static_cast<unsigned long long>(sum), static_cast<unsigned long long>(parse),
              static_cast<unsigned long long>(range),
              static_cast<unsigned long long>(g_cleanups));
  return 0;
}
