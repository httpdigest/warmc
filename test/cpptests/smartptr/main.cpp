// Smart pointers: unique_ptr with make_unique, release, reset, swap, arrays,
// custom deleters (function pointer, stateful functor, lambda) and
// polymorphic ownership; shared_ptr with make_shared, use_count through
// copies and moves, the aliasing constructor, custom deleters, arrays,
// static/dynamic_pointer_cast and enable_shared_from_this; weak_ptr lock and
// expired, a parent/child graph whose cycle is broken by weak_ptr, an
// observer list that prunes dead entries, and bad_weak_ptr when constructing
// a shared_ptr from an expired weak_ptr.
//
// shared_ptr's counts live in a control block updated on every copy and
// destroy (libc++ uses atomic increments, which become plain wasm atomics
// or loads and stores); the output prints use_count after each step and
// logs every destruction, so a lost or doubled refcount update is a diff.
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_freed = 0;

struct Obj {
  std::string name;
  explicit Obj(std::string n) : name(std::move(n)) { std::printf("  make %s\n", name.c_str()); }
  virtual ~Obj() {
    ++g_freed;
    std::printf("  free %s\n", name.c_str());
  }
  virtual std::string kind() const { return "Obj"; }
};
struct Special : Obj {
  int extra;
  Special(std::string n, int e) : Obj(std::move(n)), extra(e) {}
  std::string kind() const override { return "Special(" + std::to_string(extra) + ")"; }
};

void fnDeleter(Obj *p) {
  std::printf("  fnDeleter for %s\n", p->name.c_str());
  delete p;
}

struct CountingDeleter {
  int *count;
  void operator()(Obj *p) const {
    ++*count;
    std::printf("  CountingDeleter #%d for %s\n", *count, p->name.c_str());
    delete p;
  }
};

struct Node {
  std::string name;
  std::vector<std::shared_ptr<Node>> children;
  std::weak_ptr<Node> parent;
  explicit Node(std::string n) : name(std::move(n)) {}
  ~Node() { std::printf("  ~Node %s\n", name.c_str()); }
  std::string path() const {
    auto p = parent.lock();
    return p ? p->path() + "/" + name : name;
  }
};

struct Session : std::enable_shared_from_this<Session> {
  int id;
  explicit Session(int i) : id(i) {}
  std::shared_ptr<Session> self() { return shared_from_this(); }
  std::weak_ptr<Session> weak() { return weak_from_this(); }
  ~Session() { std::printf("  ~Session %d\n", id); }
};

struct Subject {
  std::vector<std::weak_ptr<std::string>> observers;
  int notify(const char *msg) {
    int delivered = 0;
    for (auto it = observers.begin(); it != observers.end();) {
      if (auto s = it->lock()) {
        std::printf("  notify %s: %s\n", s->c_str(), msg);
        ++delivered;
        ++it;
      } else {
        it = observers.erase(it);
      }
    }
    return delivered;
  }
};

struct Point {
  int x, y;
};

}  // namespace

int main() {
  std::printf("== unique_ptr basics\n");
  {
    auto a = std::make_unique<Obj>("a");
    std::unique_ptr<Obj> b;
    std::printf("b empty=%d\n", !b);
    b = std::move(a);
    std::printf("after move: a=%d b=%s\n", (bool)a, b->name.c_str());
    Obj *raw = b.release();
    std::printf("released, b=%d raw=%s\n", (bool)b, raw->name.c_str());
    b.reset(raw);
    b.reset(new Obj("a2"));
    auto c = std::make_unique<Obj>("c");
    b.swap(c);
    std::printf("swapped: b=%s c=%s\n", b->name.c_str(), c->name.c_str());
    std::unique_ptr<Obj> poly = std::make_unique<Special>("poly", 7);
    std::printf("poly kind=%s\n", poly->kind().c_str());
    std::printf("leaving scope\n");
  }

  std::printf("== unique_ptr arrays and deleters\n");
  {
    auto arr = std::make_unique<int[]>(10);
    for (int i = 0; i < 10; i++) arr[i] = i * i;
    int s = 0;
    for (int i = 0; i < 10; i++) s += arr[i];
    std::printf("array sum=%d\n", s);
    std::unique_ptr<Obj, void (*)(Obj *)> f(new Obj("viaFn"), fnDeleter);
    int count = 0;
    {
      std::unique_ptr<Obj, CountingDeleter> c1(new Obj("cd1"), CountingDeleter{&count});
      std::unique_ptr<Obj, CountingDeleter> c2(new Obj("cd2"), CountingDeleter{&count});
      c1 = std::move(c2);  // deletes cd1 first
      std::printf("after move-assign count=%d\n", count);
    }
    std::printf("count=%d\n", count);
    auto lam = [](Obj *p) {
      std::printf("  lambda deleter %s\n", p->name.c_str());
      delete p;
    };
    std::unique_ptr<Obj, decltype(lam)> l(new Obj("viaLambda"), lam);
    std::printf("sizes: plain=%zu fnptr=%zu lambda=%zu\n", sizeof(std::unique_ptr<Obj>),
                sizeof(f), sizeof(l));
  }

  std::printf("== shared_ptr counts\n");
  {
    auto p = std::make_shared<Obj>("shared");
    std::printf("count=%ld\n", p.use_count());
    {
      auto q = p;
      std::shared_ptr<Obj> r(q);
      std::printf("count=%ld\n", p.use_count());
      auto m = std::move(r);
      std::printf("after move count=%ld r empty=%d\n", p.use_count(), !r);
      std::vector<std::shared_ptr<Obj>> many(5, p);
      std::printf("with vector count=%ld\n", p.use_count());
    }
    std::printf("count=%ld\n", p.use_count());
    std::shared_ptr<Obj> other = std::make_shared<Special>("other", 3);
    p = other;
    std::printf("after reassign count=%ld kind=%s\n", p.use_count(), p->kind().c_str());
    p.reset();
    std::printf("after reset other count=%ld\n", other.use_count());
  }

  std::printf("== aliasing, casts, deleters\n");
  {
    auto pt = std::make_shared<Point>(Point{3, 4});
    std::shared_ptr<int> py(pt, &pt->y);
    std::printf("aliased y=%d count=%ld\n", *py, pt.use_count());
    pt.reset();
    std::printf("point kept alive through alias: y=%d count=%ld\n", *py, py.use_count());
    std::shared_ptr<Obj> base = std::make_shared<Special>("sp", 11);
    auto sp = std::dynamic_pointer_cast<Special>(base);
    auto none = std::dynamic_pointer_cast<Special>(std::make_shared<Obj>("plain"));
    std::printf("dyn cast ok=%d extra=%d none=%d count=%ld\n", (bool)sp, sp ? sp->extra : -1,
                (bool)none, base.use_count());
    auto st = std::static_pointer_cast<Obj>(sp);
    std::printf("static cast count=%ld\n", st.use_count());
    int count = 0;
    {
      std::shared_ptr<Obj> d(new Obj("customShared"), CountingDeleter{&count});
      auto d2 = d;
      std::printf("deleter not yet run: %d\n", count);
    }
    std::printf("deleter ran: %d\n", count);
    std::shared_ptr<int[]> arr(new int[4]{1, 2, 3, 4});
    std::printf("shared array: %d %d %d %d\n", arr[0], arr[1], arr[2], arr[3]);
  }

  std::printf("== weak_ptr\n");
  {
    std::weak_ptr<Obj> w;
    std::printf("default expired=%d\n", w.expired());
    {
      auto s = std::make_shared<Obj>("watched");
      w = s;
      std::printf("expired=%d count=%ld\n", w.expired(), w.use_count());
      if (auto l = w.lock()) std::printf("locked %s count=%ld\n", l->name.c_str(), l.use_count());
    }
    std::printf("expired=%d lock null=%d\n", w.expired(), w.lock() == nullptr);
    try {
      std::shared_ptr<Obj> fail(w);
      std::printf("not reached\n");
    } catch (const std::bad_weak_ptr &) {
      std::printf("bad_weak_ptr caught\n");
    }
  }

  std::printf("== tree with weak parents\n");
  {
    auto root = std::make_shared<Node>("root");
    for (int i = 0; i < 3; i++) {
      auto c = std::make_shared<Node>("c" + std::to_string(i));
      c->parent = root;
      for (int j = 0; j < 2; j++) {
        auto g = std::make_shared<Node>("g" + std::to_string(i) + std::to_string(j));
        g->parent = c;
        c->children.push_back(g);
      }
      root->children.push_back(c);
    }
    std::shared_ptr<Node> keep = root->children[1]->children[0];
    std::printf("keep path=%s count=%ld\n", keep->path().c_str(), keep.use_count());
    std::printf("root count=%ld\n", root.use_count());
    root.reset();
    std::printf("root gone; keep path=%s parent expired=%d\n", keep->path().c_str(),
                keep->parent.expired());
  }

  std::printf("== enable_shared_from_this\n");
  {
    auto s = std::make_shared<Session>(1);
    auto s2 = s->self();
    auto w = s->weak();
    std::printf("count=%ld same=%d weak count=%ld\n", s.use_count(), s == s2, w.use_count());
    Session stack(2);
    std::printf("weak of non-shared expired=%d\n", stack.weak().expired());
    try {
      auto bad = stack.self();
      std::printf("not reached\n");
    } catch (const std::bad_weak_ptr &) {
      std::printf("shared_from_this on non-shared object throws\n");
    }
  }

  std::printf("== observers\n");
  {
    Subject subj;
    auto o1 = std::make_shared<std::string>("one");
    auto o2 = std::make_shared<std::string>("two");
    auto o3 = std::make_shared<std::string>("three");
    subj.observers = {o1, o2, o3};
    std::printf("delivered=%d\n", subj.notify("first"));
    o2.reset();
    int d2 = subj.notify("second");
    std::printf("delivered=%d remaining=%zu\n", d2, subj.observers.size());
    o1.reset();
    o3.reset();
    int d3 = subj.notify("third");
    std::printf("delivered=%d remaining=%zu\n", d3, subj.observers.size());
  }

  std::printf("== shared_ptr in containers\n");
  {
    std::map<std::string, std::shared_ptr<Obj>> registry;
    auto shared = std::make_shared<Obj>("registered");
    registry["x"] = shared;
    registry["y"] = shared;
    registry["z"] = std::make_shared<Obj>("lonely");
    std::printf("shared count=%ld\n", shared.use_count());
    registry.erase("x");
    std::printf("after erase count=%ld\n", shared.use_count());
    shared.reset();
    std::printf("dropping registry\n");
  }
  std::printf("freed=%d\n", g_freed);
  return 0;
}
