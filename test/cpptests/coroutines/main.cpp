// C++20 coroutines built by hand, with no library support beyond
// <coroutine>: a Generator<T> with an iterator interface, used for ranges,
// Fibonacci, a recursive tree walk (generators yielding from generators)
// and a pipeline of three generators; a lazy Task<T> that is awaited with
// symmetric transfer, chains of tasks awaiting tasks, co_return of values
// and of void; custom awaiters whose await_ready/await_suspend/
// await_resume are logged; exceptions thrown inside a coroutine body
// captured by unhandled_exception and rethrown to the awaiter; a coroutine
// destroyed while suspended, with the destructors of its locals observed;
// and a tiny round-robin scheduler resuming several coroutines in turn.
//
// clang splits every coroutine into a heap-allocated frame with resume and
// destroy functions it reaches through pointers stored in the frame, and a
// switch on the suspend index at the top of each; so each resumption is an
// indirect call into a state machine that the source never spells out.
#include <coroutine>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

template <class T>
class Generator {
 public:
  struct promise_type {
    const T *current = nullptr;
    std::exception_ptr error;
    Generator get_return_object() {
      return Generator(std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(const T &v) noexcept {
      current = std::addressof(v);
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() { error = std::current_exception(); }
  };
  struct sentinel {};
  struct iterator {
    std::coroutine_handle<promise_type> h;
    iterator &operator++() {
      h.resume();
      if (h.done() && h.promise().error) std::rethrow_exception(h.promise().error);
      return *this;
    }
    const T &operator*() const { return *h.promise().current; }
    bool operator==(sentinel) const { return !h || h.done(); }
  };
  iterator begin() {
    if (h_) {
      h_.resume();
      if (h_.done() && h_.promise().error) std::rethrow_exception(h_.promise().error);
    }
    return iterator{h_};
  }
  sentinel end() { return {}; }

  explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}
  Generator(Generator &&o) noexcept : h_(std::exchange(o.h_, {})) {}
  Generator(const Generator &) = delete;
  ~Generator() {
    if (h_) h_.destroy();
  }

 private:
  std::coroutine_handle<promise_type> h_;
};

Generator<int> range(int lo, int hi, int step = 1) {
  for (int i = lo; i < hi; i += step) co_yield i;
}

Generator<uint64_t> fibonacci() {
  uint64_t a = 0, b = 1;
  while (true) {
    co_yield a;
    a = std::exchange(b, a + b);
  }
}

struct Tree {
  int value;
  std::unique_ptr<Tree> left, right;
};

std::unique_ptr<Tree> build(int lo, int hi) {
  if (lo > hi) return nullptr;
  int mid = (lo + hi) / 2;
  auto t = std::make_unique<Tree>();
  t->value = mid;
  t->left = build(lo, mid - 1);
  t->right = build(mid + 1, hi);
  return t;
}

Generator<int> inorder(const Tree *t) {
  if (!t) co_return;
  for (int v : inorder(t->left.get())) co_yield v;
  co_yield t->value;
  for (int v : inorder(t->right.get())) co_yield v;
}

Generator<int> evensOf(Generator<int> in) {
  for (int v : in)
    if (v % 2 == 0) co_yield v;
}

Generator<std::string> labelled(Generator<int> in) {
  int idx = 0;
  for (int v : in) co_yield "#" + std::to_string(idx++) + "=" + std::to_string(v);
}

Generator<int> failing(int n) {
  for (int i = 0; i < n; i++) {
    if (i == 3) throw std::runtime_error("generator failed at 3");
    co_yield i;
  }
}

struct Noisy {
  const char *name;
  explicit Noisy(const char *n) : name(n) { std::printf("  + %s\n", name); }
  ~Noisy() { std::printf("  - %s\n", name); }
};

Generator<int> withLocals() {
  Noisy a("outer local");
  co_yield 1;
  {
    Noisy b("inner local");
    co_yield 2;
    co_yield 3;
  }
  co_yield 4;
}

// ---- Task with symmetric transfer
template <class T>
class Task {
 public:
  struct promise_type;
  using handle = std::coroutine_handle<promise_type>;
  struct final_awaiter {
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<> await_suspend(handle h) noexcept {
      auto c = h.promise().continuation;
      return c ? c : std::noop_coroutine();
    }
    void await_resume() noexcept {}
  };
  struct promise_type {
    std::optional<T> value;
    std::exception_ptr error;
    std::coroutine_handle<> continuation;
    Task get_return_object() { return Task(handle::from_promise(*this)); }
    std::suspend_always initial_suspend() noexcept { return {}; }
    final_awaiter final_suspend() noexcept { return {}; }
    void return_value(T v) { value = std::move(v); }
    void unhandled_exception() { error = std::current_exception(); }
  };
  explicit Task(handle h) : h_(h) {}
  Task(Task &&o) noexcept : h_(std::exchange(o.h_, {})) {}
  ~Task() {
    if (h_) h_.destroy();
  }
  bool await_ready() const noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
    h_.promise().continuation = awaiting;
    return h_;
  }
  T await_resume() {
    if (h_.promise().error) std::rethrow_exception(h_.promise().error);
    return std::move(*h_.promise().value);
  }
  T run() {
    h_.resume();
    return await_resume();
  }

 private:
  handle h_;
};

Task<int> leafTask(int x) {
  if (x < 0) throw std::invalid_argument("negative input " + std::to_string(x));
  co_return x * x;
}

Task<int> sumOfSquares(int n) {
  int s = 0;
  for (int i = 1; i <= n; i++) s += co_await leafTask(i);
  co_return s;
}

Task<int> deepChain(int depth) {
  if (depth == 0) co_return 1;
  int below = co_await deepChain(depth - 1);
  co_return (below * 2 + depth) % 1000003;
}

Task<std::string> describeTask(int n) {
  int s = co_await sumOfSquares(n);
  int d = co_await deepChain(n);
  co_return "n=" + std::to_string(n) + " squares=" + std::to_string(s) + " chain=" + std::to_string(d);
}

Task<int> catchesInside(int x) {
  try {
    co_return co_await leafTask(x);
  } catch (const std::invalid_argument &e) {
    std::printf("  caught inside coroutine: %s\n", e.what());
  }
  co_return -1;
}

// ---- a logged custom awaiter
struct LoggedAwaiter {
  int value;
  bool ready;
  bool await_ready() const {
    std::printf("  await_ready(%d) -> %d\n", value, ready);
    return ready;
  }
  bool await_suspend(std::coroutine_handle<>) const {
    std::printf("  await_suspend(%d) -> resume immediately\n", value);
    return false;  // do not actually suspend
  }
  int await_resume() const {
    std::printf("  await_resume(%d)\n", value);
    return value * 100;
  }
};

Task<int> usesAwaiters() {
  int a = co_await LoggedAwaiter{1, true};
  int b = co_await LoggedAwaiter{2, false};
  co_return a + b;
}

// ---- round-robin scheduler
struct Scheduler {
  std::deque<std::coroutine_handle<>> ready;
  auto yield() {
    struct Awaiter {
      Scheduler *s;
      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> h) { s->ready.push_back(h); }
      void await_resume() const noexcept {}
    };
    return Awaiter{this};
  }
  void run() {
    while (!ready.empty()) {
      auto h = ready.front();
      ready.pop_front();
      h.resume();
    }
  }
};

struct Fire {
  struct promise_type {
    Fire get_return_object() { return {}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};

Fire worker(Scheduler &s, const char *name, int steps, std::string &log) {
  for (int i = 0; i < steps; i++) {
    log += name;
    log += std::to_string(i);
    log += ' ';
    co_await s.yield();
  }
  log += name;
  log += "! ";
}

}  // namespace

int main() {
  std::printf("== generators\n");
  std::printf("range(0,10):");
  for (int v : range(0, 10)) std::printf(" %d", v);
  std::printf("\nrange(5,40,7):");
  for (int v : range(5, 40, 7)) std::printf(" %d", v);
  std::printf("\nrange(3,3): empty=%d\n", [] {
    int n = 0;
    for (int v : range(3, 3)) n += v + 1;
    return n == 0;
  }());
  std::printf("fibonacci:");
  int count = 0;
  for (uint64_t f : fibonacci()) {
    if (count++ == 50) break;
    if (count % 5 == 0) std::printf(" %llu", (unsigned long long)f);
  }
  std::printf("\n");

  std::printf("== recursive generators\n");
  auto tree = build(1, 20);
  std::printf("inorder:");
  for (int v : inorder(tree.get())) std::printf(" %d", v);
  std::printf("\npipeline:");
  for (auto &s : labelled(evensOf(inorder(tree.get())))) std::printf(" %s", s.c_str());
  std::printf("\n");

  std::printf("== exceptions from generators\n");
  try {
    for (int v : failing(10)) std::printf("  got %d\n", v);
  } catch (const std::exception &e) {
    std::printf("  caught: %s\n", e.what());
  }

  std::printf("== destroying a suspended generator\n");
  {
    auto g = withLocals();
    for (int v : g) {
      std::printf("  value %d\n", v);
      if (v == 2) break;
    }
    std::printf("  breaking out; destroying frame\n");
  }
  std::printf("  full run:\n");
  for (int v : withLocals()) std::printf("  value %d\n", v);

  std::printf("== tasks\n");
  for (int n : {0, 1, 5, 10})
    std::printf("sumOfSquares(%d)=%d deepChain(%d)=%d\n", n, sumOfSquares(n).run(), n, deepChain(n).run());
  std::printf("%s\n", describeTask(6).run().c_str());
  std::printf("deepChain(200)=%d\n", deepChain(200).run());
  try {
    leafTask(-3).run();
  } catch (const std::exception &e) {
    std::printf("task exception: %s\n", e.what());
  }
  std::printf("catchesInside(4)=%d\n", catchesInside(4).run());
  std::printf("catchesInside(-4)=%d\n", catchesInside(-4).run());

  std::printf("== custom awaiters\n");
  std::printf("result=%d\n", usesAwaiters().run());

  std::printf("== scheduler\n");
  {
    Scheduler s;
    std::string log;
    worker(s, "A", 3, log);
    worker(s, "B", 5, log);
    worker(s, "C", 1, log);
    std::printf("after start: %s\n", log.c_str());
    s.run();
    std::printf("after run:   %s\n", log.c_str());
  }
  return 0;
}
