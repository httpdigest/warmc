// Inheritance: single, multiple and virtual, with the construction and
// destruction order of each; a diamond on a virtual base whose most derived
// class constructs it; virtual dispatch through every base pointer; pure
// virtual functions and abstract classes, including a pure virtual with a
// body; final; covariant returns; name hiding and using-declarations;
// virtual calls made from constructors and destructors, which dispatch to the
// class being built rather than the most derived one; virtual destructors.
//
// The interesting code here is what the compiler writes for itself: this
// adjustments on calls through a second base, thunks in the vtables,
// construction vtables while a virtual base is being built, and the vbase
// offset loaded from the vtable to reach a member of a virtual base.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string g_log;
void say(const char *s) {
  std::printf("  %s\n", s);
  g_log += s[0];
}

// ---- single inheritance, three levels deep
struct Animal {
  std::string name;
  explicit Animal(std::string n) : name(std::move(n)) { say("Animal()"); }
  virtual ~Animal() { say("~Animal()"); }
  virtual std::string sound() const { return "..."; }
  virtual int legs() const { return 4; }
  std::string describe() const {
    return name + " says " + sound() + " on " + std::to_string(legs()) + " legs";
  }
};

struct Bird : Animal {
  explicit Bird(std::string n) : Animal(std::move(n)) { say("Bird()"); }
  ~Bird() override { say("~Bird()"); }
  std::string sound() const override { return "tweet"; }
  int legs() const override { return 2; }
};

struct Parrot final : Bird {
  int words;
  Parrot(std::string n, int w) : Bird(std::move(n)), words(w) { say("Parrot()"); }
  ~Parrot() override { say("~Parrot()"); }
  std::string sound() const override {
    return "hello x" + std::to_string(words);
  }
};

struct Dog : Animal {
  explicit Dog(std::string n) : Animal(std::move(n)) { say("Dog()"); }
  ~Dog() override { say("~Dog()"); }
  std::string sound() const override { return "woof"; }
};

// ---- abstract classes
struct Shape {
  virtual ~Shape() = default;
  virtual double area() const = 0;
  virtual double perimeter() const = 0;
  virtual const char *kind() const = 0;
};

// A pure virtual function may still have a definition, callable explicitly.
struct Logger {
  virtual void log(const char *m) = 0;
  virtual ~Logger() = default;
};
void Logger::log(const char *m) { std::printf("  base log: %s\n", m); }

struct PrefixLogger : Logger {
  void log(const char *m) override {
    std::printf("  [prefix] ");
    Logger::log(m);
  }
};

struct Rect : Shape {
  double w, h;
  Rect(double w, double h) : w(w), h(h) {}
  double area() const override { return w * h; }
  double perimeter() const override { return 2 * (w + h); }
  const char *kind() const override { return "rect"; }
};
struct Square final : Rect {
  explicit Square(double s) : Rect(s, s) {}
  const char *kind() const override { return "square"; }
};
struct Circle : Shape {
  double r;
  explicit Circle(double r) : r(r) {}
  double area() const override { return 3.14159265358979 * r * r; }
  double perimeter() const override { return 2 * 3.14159265358979 * r; }
  const char *kind() const override { return "circle"; }
};
struct Tri : Shape {
  double a, b, c;
  Tri(double a, double b, double c) : a(a), b(b), c(c) {}
  double area() const override {
    double s = (a + b + c) / 2;
    double x = s * (s - a) * (s - b) * (s - c);
    // Newton's square root so the result does not depend on libm
    double g = x;
    for (int i = 0; i < 40; i++) g = 0.5 * (g + x / g);
    return g;
  }
  double perimeter() const override { return a + b + c; }
  const char *kind() const override { return "tri"; }
};

// ---- multiple inheritance with this adjustment
struct Readable {
  int rpos = 0;
  virtual ~Readable() { say("~Readable()"); }
  virtual int read() { return rpos++; }
  virtual const char *rname() const { return "Readable"; }
};
struct Writable {
  int wcount = 0;
  virtual ~Writable() { say("~Writable()"); }
  virtual void write(int v) { wcount += v; }
  virtual const char *wname() const { return "Writable"; }
};
struct Stream : Readable, Writable {
  std::vector<int> buf;
  Stream() { say("Stream()"); }
  ~Stream() override { say("~Stream()"); }
  int read() override {
    if (rpos < (int)buf.size()) return buf[rpos++];
    return -1;
  }
  void write(int v) override {
    buf.push_back(v);
    Writable::write(v);
  }
  const char *rname() const override { return "Stream/r"; }
  const char *wname() const override { return "Stream/w"; }
};

// ---- the diamond on a virtual base
struct Base {
  int id;
  explicit Base(int i) : id(i) { std::printf("  Base(%d)\n", i); }
  virtual ~Base() { std::printf("  ~Base(%d)\n", id); }
  virtual std::string who() const { return "Base"; }
  virtual int weight() const { return 1; }
};
struct Left : virtual Base {
  int l = 10;
  Left() : Base(-1) { std::printf("  Left() sees id=%d\n", id); }
  ~Left() override { std::printf("  ~Left()\n"); }
  std::string who() const override { return "Left>" + Base::who(); }
  int weight() const override { return 10 + l; }
};
struct Right : virtual Base {
  int r = 20;
  Right() : Base(-2) { std::printf("  Right() sees id=%d\n", id); }
  ~Right() override { std::printf("  ~Right()\n"); }
  std::string who() const override { return "Right>" + Base::who(); }
};
struct Join : Left, Right {
  explicit Join(int i) : Base(i) { std::printf("  Join()\n"); }
  ~Join() override { std::printf("  ~Join()\n"); }
  std::string who() const override {
    return "Join(" + Left::who() + "," + Right::who() + ")";
  }
};

// ---- non-virtual diamond: two copies of the base
struct Counted {
  int hits = 0;
  void hit() { ++hits; }
};
struct A1 : Counted {};
struct A2 : Counted {};
struct Twin : A1, A2 {};

// ---- virtual calls from constructors and destructors
struct Phase {
  Phase() { std::printf("  in Phase(): %s\n", stage()); }
  virtual ~Phase() { std::printf("  in ~Phase(): %s\n", stage()); }
  virtual const char *stage() const { return "Phase"; }
  void report() const { std::printf("  report: %s\n", stage()); }
};
struct Build : Phase {
  Build() { std::printf("  in Build(): %s\n", stage()); }
  ~Build() override { std::printf("  in ~Build(): %s\n", stage()); }
  const char *stage() const override { return "Build"; }
};
struct Ship : Build {
  Ship() { std::printf("  in Ship(): %s\n", stage()); }
  ~Ship() override { std::printf("  in ~Ship(): %s\n", stage()); }
  const char *stage() const override { return "Ship"; }
};

// ---- covariant return types
struct Doc {
  virtual ~Doc() = default;
  virtual Doc *clone() const { return new Doc(*this); }
  virtual const char *tag() const { return "doc"; }
};
struct Memo : Doc {
  int n = 0;
  Memo *clone() const override { return new Memo(*this); }
  const char *tag() const override { return "memo"; }
};

// ---- name hiding and using
struct Calc {
  int f(int x) const { return x + 1; }
  int f(double x) const { return (int)(x * 10); }
  virtual int g(int x) const { return x; }
  virtual ~Calc() = default;
};
struct Hider : Calc {
  int f(int x) const { return x * 100; }  // hides both Calc::f
};
struct User : Calc {
  using Calc::f;
  int f(int x) const { return x * 1000; }  // hides only f(int)
  int g(int x) const override { return -x; }
};

// ---- non-virtual destructor through derived pointer only, protected base dtor
struct Mixin {
 protected:
  ~Mixin() { say("~Mixin()"); }

 public:
  int mixval = 3;
};
struct Final : Mixin {
  ~Final() { say("~Final()"); }
};

}  // namespace

int main() {
  std::printf("== single inheritance\n");
  {
    std::vector<std::unique_ptr<Animal>> zoo;
    zoo.push_back(std::make_unique<Dog>("rex"));
    zoo.push_back(std::make_unique<Bird>("tweety"));
    zoo.push_back(std::make_unique<Parrot>("polly", 3));
    zoo.push_back(std::make_unique<Animal>("thing"));
    for (auto &a : zoo) std::printf("%s\n", a->describe().c_str());
    std::printf("  destroying zoo\n");
  }

  std::printf("== abstract shapes\n");
  {
    std::vector<std::unique_ptr<Shape>> shapes;
    for (int i = 1; i <= 6; i++) {
      switch (i % 4) {
        case 0: shapes.push_back(std::make_unique<Rect>(i, i + 1)); break;
        case 1: shapes.push_back(std::make_unique<Circle>(i * 0.5)); break;
        case 2: shapes.push_back(std::make_unique<Square>(i)); break;
        case 3: shapes.push_back(std::make_unique<Tri>(3, 4, i + 1.5)); break;
      }
    }
    double ta = 0, tp = 0;
    for (auto &s : shapes) {
      std::printf("%-6s area=%9.4f perim=%8.4f\n", s->kind(), s->area(),
                  s->perimeter());
      ta += s->area();
      tp += s->perimeter();
    }
    std::printf("total area=%.4f perim=%.4f\n", ta, tp);
    PrefixLogger pl;
    Logger &lg = pl;
    lg.log("message one");
    pl.Logger::log("direct base");
  }

  std::printf("== multiple inheritance\n");
  {
    auto *s = new Stream;
    Readable *r = s;
    Writable *w = s;
    for (int i = 1; i <= 5; i++) w->write(i * i);
    std::printf("rname=%s wname=%s wcount=%d\n", r->rname(), w->wname(),
                s->wcount);
    for (int i = 0; i < 7; i++) std::printf("read -> %d\n", r->read());
    std::printf("same object via both bases: %d\n",
                static_cast<Stream *>(r) == static_cast<Stream *>(w));
    delete w;  // virtual dtor through the second base
  }

  std::printf("== virtual diamond\n");
  {
    Join *j = new Join(42);
    Left *l = j;
    Right *r = j;
    Base *b = j;
    std::printf("who via Join : %s\n", j->who().c_str());
    std::printf("who via Left : %s\n", l->who().c_str());
    std::printf("who via Right: %s\n", r->who().c_str());
    std::printf("who via Base : %s\n", b->who().c_str());
    std::printf("weight=%d id via l=%d via r=%d via b=%d\n", b->weight(), l->id,
                r->id, b->id);
    l->id = 99;
    std::printf("after write via Left, Right sees id=%d\n", r->id);
    std::printf("l=%d r=%d\n", j->l, j->r);
    delete b;
  }
  std::printf("== Left alone\n");
  {
    Left l;
    std::printf("who=%s weight=%d\n", l.who().c_str(), l.weight());
  }

  std::printf("== non-virtual diamond\n");
  {
    Twin t;
    static_cast<A1 &>(t).hit();
    static_cast<A2 &>(t).hit();
    static_cast<A2 &>(t).hit();
    std::printf("A1 hits=%d A2 hits=%d\n", t.A1::hits, t.A2::hits);
  }

  std::printf("== dispatch during construction\n");
  {
    Ship s;
    s.report();
    Phase &p = s;
    p.report();
  }

  std::printf("== covariant returns\n");
  {
    Memo m;
    m.n = 17;
    Memo *mc = m.clone();
    Doc *d = &m;
    Doc *dc = d->clone();
    std::printf("mc->n=%d mc tag=%s dc tag=%s\n", mc->n, mc->tag(), dc->tag());
    delete mc;
    delete dc;
  }

  std::printf("== hiding and using\n");
  {
    Hider h;
    User u;
    const Calc &c = u;
    std::printf("Hider f(3)=%d f(2.5)=%d Calc::f(2.5)=%d\n", h.f(3), h.f(2.5),
                h.Calc::f(2.5));
    std::printf("User f(3)=%d f(2.5)=%d g(5)=%d via Calc g(5)=%d f(3)=%d\n",
                u.f(3), u.f(2.5), u.g(5), c.g(5), c.f(3));
  }

  std::printf("== protected base dtor\n");
  {
    Final f;
    std::printf("mixval=%d\n", f.mixval);
  }
  std::printf("log=%s\n", g_log.c_str());
  return 0;
}
