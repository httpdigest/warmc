// C++20 coroutines: a hand-written generator type, and a pipeline of three
// generators -- numbers, a filter on them, a running pairing of the filtered
// ones -- each pulling from the one below.
//
// clang splits every coroutine into a heap-allocated frame and resume and
// destroy functions it reaches through pointers stored in that frame, so each
// value pulled through the pipeline is a chain of indirect calls into
// functions that switch on a suspend-point index before they do any work: a
// state machine the source never spells out.
#include <coroutine>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>

namespace {

template <class T>
class Generator {
 public:
  struct promise_type {
    T value{};
    Generator get_return_object() {
      return Generator(std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T v) noexcept {
      value = v;
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }
  };

  explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}
  Generator(Generator &&o) noexcept : h_(std::exchange(o.h_, {})) {}
  Generator(const Generator &) = delete;
  ~Generator() {
    if (h_) h_.destroy();
  }

  bool next() {
    h_.resume();
    return !h_.done();
  }
  T value() const { return h_.promise().value; }

 private:
  std::coroutine_handle<promise_type> h_;
};

Generator<uint32_t> numbers(uint32_t seed, uint32_t n) {
  uint32_t x = seed;
  for (uint32_t i = 0; i < n; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    co_yield x;
  }
}

Generator<uint32_t> filtered(Generator<uint32_t> in, uint32_t mask) {
  while (in.next())
    if ((in.value() & mask) != 0) co_yield in.value() & 0xffff;
}

Generator<uint64_t> paired(Generator<uint32_t> in) {
  uint64_t prev = 0;
  while (in.next()) {
    uint64_t v = in.value();
    co_yield prev * 65536 + v;
    prev = v;
  }
}

}  // namespace

int main() {
  uint64_t sum = 0, count = 0;
  for (uint32_t round = 0; round < 40; round++) {
    auto g = paired(filtered(numbers(round * 7919 + 1, 200000), 0x30));
    while (g.next()) {
      sum += g.value() % 1000003;
      count++;
    }
  }
  std::printf("sum = %llu count = %llu\n", static_cast<unsigned long long>(sum),
              static_cast<unsigned long long>(count));
  return 0;
}
