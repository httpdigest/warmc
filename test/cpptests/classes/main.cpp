// Classes as values: every kind of constructor (default, parameterised,
// delegating, copy, move, converting, explicit), member initialisation order
// against the order the initialiser list is written in, static and const
// members, friends, nested classes, conversion operators, aggregates and
// designated initialisers, bit-fields, anonymous unions and pointers to
// members.
//
// Each special member logs what it did, so the output is the sequence of
// constructions, copies and assignments a program makes.  That sequence is
// what the compiler decides from the source -- which copies it elides, which
// temporaries it materialises -- so a miscompiled call or a skipped store in
// one of these small functions shows up as a changed line rather than as a
// different checksum.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_seq = 0;

struct Tracer {
  std::string name;
  int id;
  explicit Tracer(std::string n) : name(std::move(n)), id(++g_seq) {
    std::printf("  ctor %s#%d\n", name.c_str(), id);
  }
  Tracer() : Tracer("default") {}
  Tracer(const Tracer &o) : name(o.name + "'"), id(++g_seq) {
    std::printf("  copy %s#%d from #%d\n", name.c_str(), id, o.id);
  }
  Tracer(Tracer &&o) noexcept : name(std::move(o.name)), id(++g_seq) {
    o.name = "<moved>";
    std::printf("  move %s#%d from #%d\n", name.c_str(), id, o.id);
  }
  Tracer &operator=(const Tracer &o) {
    std::printf("  copy-assign #%d(%s) = #%d(%s)\n", id, name.c_str(), o.id,
                o.name.c_str());
    name = o.name;
    return *this;
  }
  Tracer &operator=(Tracer &&o) noexcept {
    std::printf("  move-assign #%d(%s) = #%d(%s)\n", id, name.c_str(), o.id,
                o.name.c_str());
    name = std::move(o.name);
    o.name = "<moved>";
    return *this;
  }
  ~Tracer() { std::printf("  dtor %s#%d\n", name.c_str(), id); }
};

// Members are initialised in declaration order whatever order the list says.
struct Ordered {
  int a;
  int b;
  int c;
  static int log[3];
  static int n;
  static int note(int v) {
    log[n++] = v;
    return v;
  }
  Ordered(int x) : c(note(x * 3)), a(note(x)), b(note(x * 2)) {}
};
int Ordered::log[3];
int Ordered::n;

class Counter {
 public:
  static int live;
  static int total;
  static constexpr int kLimit = 5;
  Counter() {
    ++live;
    ++total;
  }
  Counter(const Counter &) : Counter() {}
  ~Counter() { --live; }
};
int Counter::live = 0;
int Counter::total = 0;

class Account {
  std::string owner_;
  int64_t cents_;
  friend void audit(const Account &a);
  friend class Bank;

 public:
  Account(std::string o, int64_t c) : owner_(std::move(o)), cents_(c) {}
  int64_t balance() const { return cents_; }
  const std::string &owner() const { return owner_; }
  Account &deposit(int64_t c) {
    cents_ += c;
    return *this;
  }
  Account &withdraw(int64_t c) {
    cents_ -= c;
    return *this;
  }
};

void audit(const Account &a) {
  std::printf("audit %s: %lld.%02lld\n", a.owner_.c_str(),
              (long long)(a.cents_ / 100), (long long)(a.cents_ % 100));
}

class Bank {
 public:
  static void transfer(Account &from, Account &to, int64_t c) {
    from.cents_ -= c;
    to.cents_ += c;
  }
};

// Converting and explicit constructors, conversion operators.
struct Meters {
  double v;
  Meters(double d) : v(d) {}  // implicit
  explicit operator int() const { return static_cast<int>(v); }
  operator double() const { return v; }
};

struct Celsius {
  double deg;
  explicit Celsius(double d) : deg(d) {}
  explicit operator bool() const { return deg > 0; }
};

double twice(Meters m) { return m.v * 2; }

// Nested classes and access to the enclosing class's private statics.
class Outer {
  static int secret;
  int inst = 11;

 public:
  class Inner {
   public:
    int peek() const { return secret * 2; }
    int peek(const Outer &o) const { return o.inst + secret; }
  };
  struct Node {
    int v;
    Node *next;
  };
};
int Outer::secret = 21;

// Aggregates, default member initialisers, designated initialisers.
struct Point3 {
  int x = 1;
  int y = 2;
  int z = 3;
};
struct Rect {
  Point3 lo;
  Point3 hi{10, 20, 30};
  const char *label = "rect";
};

// Bit-fields.
struct Flags {
  unsigned a : 3;
  unsigned b : 5;
  signed c : 4;
  unsigned d : 1;
  unsigned e : 19;
};

// Anonymous union inside a struct, discriminated by hand.
struct Value {
  enum Kind { Int, Dbl, Chars } kind;
  union {
    int32_t i;
    double d;
    char s[8];
  };
};

void show(const Value &v) {
  switch (v.kind) {
    case Value::Int: std::printf("value int %d\n", v.i); break;
    case Value::Dbl: std::printf("value dbl %.3f\n", v.d); break;
    case Value::Chars: std::printf("value chars %s\n", v.s); break;
  }
}

// Pointers to data members and member functions.
struct Vec3 {
  double x, y, z;
  double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
  double sum() const { return x + y + z; }
  void scale(double k) {
    x *= k;
    y *= k;
    z *= k;
  }
};

// Const and non-const overloads, ref-qualified overloads.
struct Box {
  std::string s;
  std::string &get() & {
    std::printf("  get() & \n");
    return s;
  }
  const std::string &get() const & {
    std::printf("  get() const &\n");
    return s;
  }
  std::string get() && {
    std::printf("  get() &&\n");
    return std::move(s);
  }
};

// A class with a user-declared copy that suppresses implicit move.
struct CopyOnly {
  int v;
  explicit CopyOnly(int x) : v(x) {}
  CopyOnly(const CopyOnly &o) : v(o.v + 100) {}
};

// Empty base and [[no_unique_address]] layout.
struct Empty {};
struct WithEmpty {
  [[no_unique_address]] Empty e;
  int32_t v;
};

}  // namespace

int main() {
  std::printf("== tracer lifetimes\n");
  {
    Tracer a("a");
    Tracer b = a;
    Tracer c(std::move(a));
    Tracer d;
    d = b;
    d = std::move(c);
    std::vector<Tracer> v;
    v.reserve(4);
    v.emplace_back("e1");
    v.push_back(Tracer("t2"));
    v.push_back(b);
    std::printf("  vector holds %zu\n", v.size());
    std::printf("  leaving scope\n");
  }

  std::printf("== member init order\n");
  {
    Ordered o(7);
    std::printf("a=%d b=%d c=%d log=%d,%d,%d\n", o.a, o.b, o.c, Ordered::log[0],
                Ordered::log[1], Ordered::log[2]);
  }

  std::printf("== static members\n");
  {
    Counter a, b;
    {
      Counter c = a;
      Counter arr[3];
      std::printf("live=%d total=%d limit=%d\n", Counter::live, Counter::total,
                  Counter::kLimit);
    }
    std::printf("live=%d total=%d\n", Counter::live, Counter::total);
  }
  std::printf("live=%d total=%d\n", Counter::live, Counter::total);

  std::printf("== friends and chaining\n");
  {
    Account x("xavier", 12345), y("yolanda", 500);
    x.deposit(55).withdraw(1000).deposit(7);
    Bank::transfer(x, y, 2500);
    audit(x);
    audit(y);
    std::printf("%s=%lld %s=%lld\n", x.owner().c_str(), (long long)x.balance(),
                y.owner().c_str(), (long long)y.balance());
  }

  std::printf("== conversions\n");
  {
    Meters m = 3.75;
    std::printf("twice(2.5)=%.2f int(m)=%d double(m)=%.2f m+1=%.2f\n",
                twice(2.5), static_cast<int>(m), static_cast<double>(m), m + 1.0);
    Celsius hot(30), cold(-4);
    std::printf("hot=%d cold=%d\n", hot ? 1 : 0, cold ? 1 : 0);
    if (!cold) std::printf("cold is false\n");
  }

  std::printf("== nested\n");
  {
    Outer::Inner in;
    Outer o;
    std::printf("peek=%d peek(o)=%d\n", in.peek(), in.peek(o));
    Outer::Node n3{3, nullptr}, n2{2, &n3}, n1{1, &n2};
    int s = 0;
    for (Outer::Node *p = &n1; p; p = p->next) s = s * 10 + p->v;
    std::printf("chain=%d\n", s);
  }

  std::printf("== aggregates\n");
  {
    Point3 p;
    Point3 q{7};
    Point3 r{.x = 4, .z = 9};
    Rect rc{.lo = {.y = -1}, .label = "named"};
    Rect rd{};
    std::printf("p=%d,%d,%d q=%d,%d,%d r=%d,%d,%d\n", p.x, p.y, p.z, q.x, q.y,
                q.z, r.x, r.y, r.z);
    std::printf("rc lo=%d,%d,%d hi=%d,%d,%d %s\n", rc.lo.x, rc.lo.y, rc.lo.z,
                rc.hi.x, rc.hi.y, rc.hi.z, rc.label);
    std::printf("rd hi.z=%d %s\n", rd.hi.z, rd.label);
    Point3 arr[3] = {{1, 1, 1}, {2}, {}};
    for (auto &a : arr) std::printf("  arr %d %d %d\n", a.x, a.y, a.z);
  }

  std::printf("== bit-fields\n");
  {
    Flags f{};
    for (int i = 0; i < 12; i++) {
      f.a = i;
      f.b = i * 3;
      f.c = i - 6;
      f.d = i & 1;
      f.e = i * 40000;
      std::printf("i=%2d a=%u b=%2u c=%3d d=%u e=%6u\n", i, f.a, f.b, (int)f.c,
                  f.d, f.e);
    }
    f.a = 7;
    f.a++;
    std::printf("a wrapped to %u\n", f.a);
  }

  std::printf("== unions\n");
  {
    Value v{};
    v.kind = Value::Int;
    v.i = -42;
    show(v);
    v.kind = Value::Dbl;
    v.d = 6.25;
    show(v);
    v.kind = Value::Chars;
    std::strcpy(v.s, "hello");
    show(v);
  }

  std::printf("== pointers to members\n");
  {
    Vec3 a{1, 2, 3}, b{4, -5, 6};
    double Vec3::*fields[] = {&Vec3::x, &Vec3::y, &Vec3::z};
    for (int i = 0; i < 3; i++) std::printf("a.f%d=%.1f b.f%d=%.1f\n", i,
                                            a.*fields[i], i, b.*fields[i]);
    double (Vec3::*f)(const Vec3 &) const = &Vec3::dot;
    double (Vec3::*g)() const = &Vec3::sum;
    void (Vec3::*h)(double) = &Vec3::scale;
    Vec3 *pa = &a;
    std::printf("dot=%.1f sum=%.1f\n", (a.*f)(b), (pa->*g)());
    (pa->*h)(-2);
    std::printf("scaled sum=%.1f\n", (a.*g)());
  }

  std::printf("== ref qualifiers\n");
  {
    Box b{"boxed"};
    const Box &cb = b;
    b.get() += "!";
    std::printf("  %s\n", cb.get().c_str());
    std::string taken = std::move(b).get();
    std::printf("  taken=%s left=%zu\n", taken.c_str(), b.s.size());
    std::string t2 = Box{"temp"}.get();
    std::printf("  t2=%s\n", t2.c_str());
  }

  std::printf("== copy-only\n");
  {
    CopyOnly a(1);
    CopyOnly b = std::move(a);  // falls back to copy
    CopyOnly c = b;
    std::printf("a=%d b=%d c=%d\n", a.v, b.v, c.v);
  }

  std::printf("== layout\n");
  std::printf("sizeof Empty=%zu WithEmpty=%zu Flags=%zu Value=%zu Point3=%zu\n",
              sizeof(Empty), sizeof(WithEmpty), sizeof(Flags), sizeof(Value),
              sizeof(Point3));
  std::printf("done seq=%d\n", g_seq);
  return 0;
}
