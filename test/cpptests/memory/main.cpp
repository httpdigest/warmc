// Memory management: replaced global operator new and delete that count
// every allocation made by the program (including the ones the standard
// library makes inside containers and strings), class-specific operator new
// and delete with sizes logged, over-aligned types through the
// align_val_t overloads, placement new, std::construct_at/destroy_at and the
// uninitialized_* algorithms, a counting allocator used by vector, list,
// map and basic_string through allocator_traits, polymorphic memory
// resources (monotonic_buffer_resource over a stack buffer,
// unsynchronized_pool_resource, a custom memory_resource that logs), and the
// C allocation functions: calloc zeroing, realloc growing and shrinking with
// contents preserved, memmove with overlapping ranges, memset and memcmp.
//
// The allocation counts are a precise record of what libc++ does for each
// operation -- how many nodes, how many buffer growths -- so a miscompiled
// size computation or a skipped deallocation in its inlined code changes
// a count.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <string>
#include <vector>

namespace {

// volatile: calls to the replaceable allocation functions are builtins the
// optimiser may assume leave other globals alone, so plain counters read on
// both sides of a new-expression can be forwarded across it.
volatile std::size_t g_news = 0, g_deletes = 0, g_bytes = 0, g_alignedNews = 0;
void *volatile g_sink;  // makes allocations escape so they are not elided

struct Scope {
  const char *name;
  std::size_t news, deletes, bytes;
  explicit Scope(const char *n) : name(n), news(g_news), deletes(g_deletes), bytes(g_bytes) {}
  ~Scope() {
    std::printf("  [%s] allocations=%zu frees=%zu bytes=%zu\n", name, g_news - news, g_deletes - deletes,
                g_bytes - bytes);
  }
};

}  // namespace

void *operator new(std::size_t n) {
  g_news = g_news + 1;
  g_bytes = g_bytes + n;
  void *p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void *p) noexcept {
  if (p) g_deletes = g_deletes + 1;
  std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
  if (p) g_deletes = g_deletes + 1;
  std::free(p);
}
void *operator new(std::size_t n, std::align_val_t al) {
  g_news = g_news + 1;
  g_alignedNews = g_alignedNews + 1;
  g_bytes = g_bytes + n;
  std::size_t a = static_cast<std::size_t>(al);
  void *p = std::aligned_alloc(a, (n + a - 1) / a * a);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void *p, std::align_val_t) noexcept {
  if (p) g_deletes = g_deletes + 1;
  std::free(p);
}
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
  if (p) g_deletes = g_deletes + 1;
  std::free(p);
}

namespace {

struct Pooled {
  int a, b;
  static int liveFromPool;
  static void *operator new(std::size_t n) {
    ++liveFromPool;
    std::printf("  Pooled::operator new(%zu)\n", n);
    return ::operator new(n);
  }
  static void operator delete(void *p, std::size_t n) {
    --liveFromPool;
    std::printf("  Pooled::operator delete(%zu)\n", n);
    ::operator delete(p);
  }
};
int Pooled::liveFromPool = 0;

struct alignas(64) CacheLine {
  double v[8];
};

struct Tracked {
  static int live;
  int v;
  explicit Tracked(int x) : v(x) { ++live; }
  Tracked(const Tracked &o) : v(o.v) { ++live; }
  ~Tracked() { --live; }
};
int Tracked::live = 0;

template <class T>
struct CountingAlloc {
  using value_type = T;
  int *counter;
  explicit CountingAlloc(int *c) : counter(c) {}
  template <class U>
  CountingAlloc(const CountingAlloc<U> &o) : counter(o.counter) {}
  T *allocate(std::size_t n) {
    *counter += (int)n;
    return static_cast<T *>(::operator new(n * sizeof(T)));
  }
  void deallocate(T *p, std::size_t n) {
    *counter -= (int)n;
    ::operator delete(p);
  }
  template <class U>
  bool operator==(const CountingAlloc<U> &o) const { return counter == o.counter; }
};

class LoggingResource : public std::pmr::memory_resource {
  std::pmr::memory_resource *up_;

 public:
  int allocs = 0, deallocs = 0;
  std::size_t bytes = 0;
  explicit LoggingResource(std::pmr::memory_resource *up) : up_(up) {}

 private:
  void *do_allocate(std::size_t n, std::size_t align) override {
    ++allocs;
    bytes += n;
    return up_->allocate(n, align);
  }
  void do_deallocate(void *p, std::size_t n, std::size_t align) override {
    ++deallocs;
    up_->deallocate(p, n, align);
  }
  bool do_is_equal(const std::pmr::memory_resource &o) const noexcept override { return this == &o; }
};

}  // namespace

int main() {
  std::printf("== counted standard containers\n");
  {
    Scope s("vector<int> 100 push_backs");
    std::vector<int> v;
    for (int i = 0; i < 100; i++) v.push_back(i);
  }
  {
    Scope s("vector<int> reserve then 100 push_backs");
    std::vector<int> v;
    v.reserve(100);
    for (int i = 0; i < 100; i++) v.push_back(i);
  }
  {
    Scope s("list<int> 50 nodes");
    std::list<int> l;
    for (int i = 0; i < 50; i++) l.push_back(i);
  }
  {
    Scope s("map<int,int> 40 entries");
    std::map<int, int> m;
    for (int i = 0; i < 40; i++) m[i * 3 % 40] = i;
  }
  {
    Scope s("strings at the SSO boundary");
    std::string a(10, 'a');  // inline on wasm32
    std::string b(11, 'b');  // heap
    std::string c = a + a;   // heap
    std::string d = std::move(b);
  }
  {
    Scope s("shared_ptr: make_shared vs new");
    auto p = std::make_shared<int>(5);
    std::shared_ptr<int> q(new int(6));
    g_sink = p.get();
    g_sink = q.get();
  }
  {
    Scope s("make_unique<int[]>(10) and new[]");
    auto arr = std::make_unique<int[]>(10);
    g_sink = arr.get();
    long *raw = new long[25];
    g_sink = raw;
    delete[] raw;
  }

  std::printf("== class-specific and aligned new\n");
  {
    Pooled *p = new Pooled{1, 2};
    g_sink = p;
    Pooled *q = new Pooled{3, 4};
    g_sink = q;
    std::printf("  live from pool=%d sum=%d\n", Pooled::liveFromPool, p->a + p->b + q->a + q->b);
    delete p;
    delete q;
    std::printf("  live from pool=%d\n", Pooled::liveFromPool);
    std::size_t before = g_alignedNews;
    auto *cl = new CacheLine{};
    g_sink = cl;
    auto *cls = new CacheLine[3];
    g_sink = cls;
    std::printf("  aligned allocations=%zu aligned to 64: %d %d\n", g_alignedNews - before,
                reinterpret_cast<std::uintptr_t>(cl) % 64 == 0, reinterpret_cast<std::uintptr_t>(cls) % 64 == 0);
    std::vector<CacheLine> vcl(5);
    std::printf("  vector<CacheLine> data aligned: %d alignof=%zu\n",
                reinterpret_cast<std::uintptr_t>(vcl.data()) % 64 == 0, alignof(CacheLine));
    delete cl;
    delete[] cls;
  }

  std::printf("== placement and uninitialized algorithms\n");
  {
    alignas(Tracked) unsigned char buf[sizeof(Tracked) * 8];
    Tracked *t = reinterpret_cast<Tracked *>(buf);
    for (int i = 0; i < 4; i++) new (t + i) Tracked(i * 11);
    std::printf("  live=%d values=%d %d %d %d\n", Tracked::live, t[0].v, t[1].v, t[2].v, t[3].v);
    std::uninitialized_copy(t, t + 4, t + 4);
    std::printf("  after uninitialized_copy live=%d t[6]=%d\n", Tracked::live, t[6].v);
    std::destroy(t, t + 8);
    std::printf("  after destroy live=%d\n", Tracked::live);
    std::uninitialized_fill_n(t, 3, Tracked(7));
    std::printf("  after fill live=%d t[2]=%d\n", Tracked::live, t[2].v);
    std::destroy_n(t, 3);
    Tracked *one = std::construct_at(t + 5, 55);
    std::printf("  construct_at -> %d live=%d\n", one->v, Tracked::live);
    std::destroy_at(one);
    int raw[6];
    std::uninitialized_value_construct_n(raw, 6);
    std::printf("  value-constructed ints: %d %d %d\n", raw[0], raw[3], raw[5]);
    std::printf("  final live=%d\n", Tracked::live);
  }

  std::printf("== counting allocator\n");
  {
    int count = 0;
    {
      std::vector<int, CountingAlloc<int>> v{CountingAlloc<int>(&count)};
      for (int i = 0; i < 20; i++) {
        v.push_back(i);
        if ((i & (i + 1)) == 0) std::printf("  vector size=%zu elements allocated=%d\n", v.size(), count);
      }
      std::list<int, CountingAlloc<int>> l{CountingAlloc<int>(&count)};
      for (int i = 0; i < 5; i++) l.push_back(i);
      std::printf("  with list: allocated=%d\n", count);
      std::map<int, int, std::less<int>, CountingAlloc<std::pair<const int, int>>> m{
          CountingAlloc<std::pair<const int, int>>(&count)};
      for (int i = 0; i < 7; i++) m[i] = i;
      std::printf("  with map: allocated=%d\n", count);
      std::basic_string<char, std::char_traits<char>, CountingAlloc<char>> s{CountingAlloc<char>(&count)};
      s.assign(40, 'x');
      std::printf("  with string(40): allocated=%d\n", count);
    }
    std::printf("  after scope allocated=%d\n", count);
  }

  std::printf("== polymorphic memory resources\n");
  {
    LoggingResource logNew(std::pmr::new_delete_resource());
    {
      std::pmr::vector<int> v(&logNew);
      for (int i = 0; i < 100; i++) v.push_back(i);
      std::pmr::string s("a pmr string that is long enough to allocate", &logNew);
      std::printf("  new_delete: allocs=%d deallocs=%d bytes=%zu\n", logNew.allocs, logNew.deallocs, logNew.bytes);
    }
    std::printf("  after scope: allocs=%d deallocs=%d\n", logNew.allocs, logNew.deallocs);

    alignas(std::max_align_t) unsigned char arena[4096];
    LoggingResource logUp(std::pmr::null_memory_resource());
    std::size_t globalBefore = g_news;
    {
      std::pmr::monotonic_buffer_resource mono(arena, sizeof arena, &logUp);
      std::pmr::vector<std::pmr::string> words(&mono);
      for (int i = 0; i < 20; i++) words.emplace_back(std::string(i + 1, (char)('a' + i)));
      std::size_t total = 0;
      for (auto &w : words) total += w.size();
      std::printf("  monotonic: words=%zu total chars=%zu upstream allocs=%d global news=%zu\n", words.size(),
                  total, logUp.allocs, g_news - globalBefore);
      try {
        std::pmr::vector<char> huge(&mono);
        huge.resize(100000);
        std::printf("  not reached\n");
      } catch (const std::bad_alloc &) {
        std::printf("  arena exhausted -> bad_alloc from null_memory_resource\n");
      }
    }
    LoggingResource logPool(std::pmr::new_delete_resource());
    {
      std::pmr::unsynchronized_pool_resource pool(&logPool);
      std::pmr::list<int> lst(&pool);
      for (int i = 0; i < 200; i++) lst.push_back(i);
      for (int i = 0; i < 100; i++) lst.pop_front();
      for (int i = 0; i < 100; i++) lst.push_back(i);
      long sum = 0;
      for (int x : lst) sum += x;
      std::printf("  pool: list size=%zu sum=%ld upstream allocs>0 %d\n", lst.size(), sum, logPool.allocs > 0);
    }
    std::printf("  pool released everything: %d\n", logPool.allocs == logPool.deallocs);
  }

  std::printf("== C allocation functions\n");
  {
    int *z = static_cast<int *>(std::calloc(16, sizeof(int)));
    int nonzero = 0;
    for (int i = 0; i < 16; i++) nonzero += z[i] != 0;
    std::printf("  calloc nonzero=%d\n", nonzero);
    for (int i = 0; i < 16; i++) z[i] = i * i;
    z = static_cast<int *>(std::realloc(z, 1000 * sizeof(int)));
    bool kept = true;
    for (int i = 0; i < 16; i++) kept &= z[i] == i * i;
    z = static_cast<int *>(std::realloc(z, 4 * sizeof(int)));
    std::printf("  realloc grow kept=%d shrink kept=%d %d %d %d\n", kept, z[0], z[1], z[2], z[3]);
    std::free(z);
    char text[32] = "0123456789abcdefghij";
    std::memmove(text + 4, text, 10);
    std::printf("  memmove forward overlap: %s\n", text);
    std::memmove(text, text + 6, 10);
    std::printf("  memmove backward overlap: %s\n", text);
    std::memset(text + 2, '#', 5);
    std::printf("  memset: %s\n", text);
    std::printf("  memcmp: %d %d %d\n", std::memcmp("abc", "abd", 3) < 0, std::memcmp("abc", "abc", 3) == 0,
                std::memcmp("b", "a", 1) > 0);
    unsigned char big[300], copy[300];
    for (int i = 0; i < 300; i++) big[i] = (unsigned char)(i * 7 + 3);
    std::memcpy(copy, big, 300);
    unsigned sum = 0;
    for (int i = 0; i < 300; i++) sum = sum * 31 + copy[i];
    std::printf("  memcpy 300 checksum=%u equal=%d\n", sum, std::memcmp(big, copy, 300) == 0);
  }
  std::printf("totals: news=%zu deletes=%zu (difference is what is still live)\n", g_news, g_deletes);
  return 0;
}
