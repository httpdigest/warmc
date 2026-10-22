// RAII and object lifetime: destruction order of locals (reverse of
// construction), of members (reverse of declaration, after the body of the
// destructor), of bases (after members), of array elements (last first), of
// temporaries (end of the full expression) and of temporaries whose lifetime
// is extended by binding to a reference; objects in nested scopes, in loops,
// on early return, break and continue, and through goto; a scope guard that
// runs a lambda on exit and can be dismissed; a lock-style guard over a
// fake mutex; placement new with explicit destructor calls; and objects in
// std::vector and std::map being destroyed when the container is.
//
// Every constructor and destructor writes one line, so the output is the
// full lifetime trace and any reordering the compiler should not have done
// is a diff.
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

int depth = 0;

struct Noisy {
  std::string name;
  explicit Noisy(std::string n) : name(std::move(n)) {
    std::printf("%*s+ %s\n", depth * 2, "", name.c_str());
  }
  Noisy(const Noisy &o) : name(o.name + "(copy)") {
    std::printf("%*s+ %s\n", depth * 2, "", name.c_str());
  }
  ~Noisy() { std::printf("%*s- %s\n", depth * 2, "", name.c_str()); }
  const Noisy &self() const { return *this; }
  int len() const { return (int)name.size(); }
};

struct BaseA {
  Noisy a{"BaseA::a"};
  BaseA() { std::printf("  BaseA body\n"); }
  ~BaseA() { std::printf("  ~BaseA body\n"); }
};
struct BaseB {
  Noisy b{"BaseB::b"};
  BaseB() { std::printf("  BaseB body\n"); }
  ~BaseB() { std::printf("  ~BaseB body\n"); }
};
struct Composite : BaseA, BaseB {
  Noisy first{"Composite::first"};
  Noisy second{"Composite::second"};
  Noisy arr[2] = {Noisy("arr[0]"), Noisy("arr[1]")};
  Composite() { std::printf("  Composite body\n"); }
  ~Composite() { std::printf("  ~Composite body\n"); }
};

class ScopeGuard {
  std::function<void()> f_;
  bool active_ = true;

 public:
  explicit ScopeGuard(std::function<void()> f) : f_(std::move(f)) {}
  ScopeGuard(ScopeGuard &&o) noexcept : f_(std::move(o.f_)), active_(o.active_) {
    o.active_ = false;
  }
  ~ScopeGuard() {
    if (active_) f_();
  }
  void dismiss() { active_ = false; }
};

struct FakeMutex {
  int locked = 0;
  int lockCount = 0;
  void lock() {
    ++locked;
    ++lockCount;
    std::printf("  lock (depth %d)\n", locked);
  }
  void unlock() {
    std::printf("  unlock (depth %d)\n", locked);
    --locked;
  }
};

template <class M>
class LockGuard {
  M &m_;

 public:
  explicit LockGuard(M &m) : m_(m) { m_.lock(); }
  ~LockGuard() { m_.unlock(); }
  LockGuard(const LockGuard &) = delete;
};

Noisy make(const char *n) { return Noisy(n); }

int earlyReturn(int k) {
  Noisy a("er.a");
  if (k == 0) return 0;
  Noisy b("er.b");
  if (k == 1) return 1;
  Noisy c("er.c");
  return 2;
}

struct Resource {
  int id;
  static int open;
  explicit Resource(int i) : id(i) {
    ++open;
    std::printf("  open resource %d (open=%d)\n", id, open);
  }
  ~Resource() {
    --open;
    std::printf("  close resource %d (open=%d)\n", id, open);
  }
};
int Resource::open = 0;

}  // namespace

int main() {
  std::printf("== locals\n");
  {
    Noisy a("a");
    Noisy b("b");
    {
      ++depth;
      Noisy c("c");
      Noisy d("d");
      --depth;
    }
    Noisy e("e");
  }

  std::printf("== members and bases\n");
  { Composite c; }

  std::printf("== arrays\n");
  {
    Noisy arr[3] = {Noisy("x0"), Noisy("x1"), Noisy("x2")};
    std::printf("  array built\n");
  }
  {
    auto *heap = new Noisy[2]{Noisy("h0"), Noisy("h1")};
    std::printf("  heap array built\n");
    delete[] heap;
  }

  std::printf("== temporaries\n");
  {
    std::printf("  len=%d\n", Noisy("temp1").len());
    std::printf("  after temp1\n");
    int total = make("t2").len() + make("t3").len();
    std::printf("  total=%d (t2,t3 destroyed at end of full expression)\n", total);
    const Noisy &ext = make("extended");
    std::printf("  extended lives: %s\n", ext.name.c_str());
    Noisy &&ext2 = Noisy("extended2");
    std::printf("  extended2 lives: %s\n", ext2.name.c_str());
    const Noisy &notExt = Noisy("not-extended").self();  // dangling after ;
    (void)notExt;
    std::printf("  end of block\n");
  }

  std::printf("== loops and jumps\n");
  for (int i = 0; i < 4; i++) {
    Noisy it("iter" + std::to_string(i));
    if (i == 1) continue;
    Noisy inner("inner" + std::to_string(i));
    if (i == 2) break;
  }
  {
    int n = 0;
  again:
    Noisy g("goto" + std::to_string(n));
    if (++n < 3) goto again;
  }
  std::printf("  earlyReturn -> %d\n", earlyReturn(0));
  std::printf("  earlyReturn -> %d\n", earlyReturn(1));
  std::printf("  earlyReturn -> %d\n", earlyReturn(2));

  std::printf("== scope guards\n");
  {
    std::string log;
    {
      ScopeGuard g1([&] { log += "1"; });
      ScopeGuard g2([&] { log += "2"; });
      ScopeGuard g3([&] { log += "3"; });
      g2.dismiss();
      ScopeGuard moved = std::move(g3);
      log += "|";
    }
    std::printf("  guard log=%s\n", log.c_str());
  }

  std::printf("== lock guards\n");
  {
    FakeMutex m;
    {
      LockGuard<FakeMutex> l1(m);
      {
        LockGuard<FakeMutex> l2(m);
        std::printf("  critical section, locked=%d\n", m.locked);
      }
      std::printf("  after inner, locked=%d\n", m.locked);
    }
    std::printf("  released, locked=%d count=%d\n", m.locked, m.lockCount);
  }

  std::printf("== placement new\n");
  {
    alignas(Noisy) unsigned char buf[sizeof(Noisy) * 3];
    Noisy *p[3];
    for (int i = 0; i < 3; i++)
      p[i] = new (buf + i * sizeof(Noisy)) Noisy("placed" + std::to_string(i));
    std::printf("  middle name=%s\n", p[1]->name.c_str());
    for (int i = 2; i >= 0; i--) std::destroy_at(p[i]);
  }

  std::printf("== containers own their elements\n");
  {
    std::vector<Noisy> v;
    v.reserve(3);
    v.emplace_back("v0");
    v.emplace_back("v1");
    v.emplace_back("v2");
    std::map<int, Noisy> m;
    m.emplace(2, "m2");
    m.emplace(1, "m1");
    m.emplace(3, "m3");
    m.erase(2);
    std::printf("  clearing vector\n");
    v.clear();
    std::printf("  leaving scope with map\n");
  }

  std::printf("== resources in unique_ptr\n");
  {
    std::vector<std::unique_ptr<Resource>> rs;
    for (int i = 1; i <= 4; i++) rs.push_back(std::make_unique<Resource>(i));
    rs.erase(rs.begin() + 1);
    rs[0].reset(new Resource(10));
    std::printf("  open=%d before scope end\n", Resource::open);
  }
  std::printf("  open=%d after\n", Resource::open);
  return 0;
}
