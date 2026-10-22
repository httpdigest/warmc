// Polymorphism patterns beyond plain virtual calls: the visitor pattern
// (double dispatch through accept/visit) over an AST, with three visitors
// producing a printout, a value and a node count; hand-written type
// erasure with a small-buffer optimisation (a Shape value type that holds
// any type with area() and name(), copied, moved and stored in a vector);
// virtual functions with default arguments, which bind statically to the
// declared type while the body dispatches dynamically; pointers to virtual
// member functions, which dispatch dynamically through the vtable index
// they encode, including through a second base; overloaded virtuals and
// hiding; an interface with a non-virtual public wrapper calling private
// virtuals (the template-method pattern); and the same computation done
// through virtual dispatch, std::variant + visit and a function-pointer
// table, checked to agree.
#include <cstdio>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

// ---- visitor over an AST
struct Num;
struct Add;
struct Mul;
struct Neg;
struct Visitor {
  virtual ~Visitor() = default;
  virtual void visit(const Num &) = 0;
  virtual void visit(const Add &) = 0;
  virtual void visit(const Mul &) = 0;
  virtual void visit(const Neg &) = 0;
};
struct Node {
  virtual ~Node() = default;
  virtual void accept(Visitor &v) const = 0;
};
struct Num : Node {
  long long v;
  explicit Num(long long v) : v(v) {}
  void accept(Visitor &vis) const override { vis.visit(*this); }
};
struct Add : Node {
  std::unique_ptr<Node> l, r;
  Add(std::unique_ptr<Node> a, std::unique_ptr<Node> b) : l(std::move(a)), r(std::move(b)) {}
  void accept(Visitor &vis) const override { vis.visit(*this); }
};
struct Mul : Node {
  std::unique_ptr<Node> l, r;
  Mul(std::unique_ptr<Node> a, std::unique_ptr<Node> b) : l(std::move(a)), r(std::move(b)) {}
  void accept(Visitor &vis) const override { vis.visit(*this); }
};
struct Neg : Node {
  std::unique_ptr<Node> e;
  explicit Neg(std::unique_ptr<Node> x) : e(std::move(x)) {}
  void accept(Visitor &vis) const override { vis.visit(*this); }
};

struct Printer : Visitor {
  std::string out;
  void visit(const Num &n) override { out += std::to_string(n.v); }
  void visit(const Add &a) override {
    out += '(';
    a.l->accept(*this);
    out += " + ";
    a.r->accept(*this);
    out += ')';
  }
  void visit(const Mul &m) override {
    m.l->accept(*this);
    out += " * ";
    m.r->accept(*this);
  }
  void visit(const Neg &n) override {
    out += "-";
    n.e->accept(*this);
  }
};
struct Evaluator : Visitor {
  long long value = 0;
  void visit(const Num &n) override { value = n.v; }
  void visit(const Add &a) override {
    a.l->accept(*this);
    long long x = value;
    a.r->accept(*this);
    value += x;
  }
  void visit(const Mul &m) override {
    m.l->accept(*this);
    long long x = value;
    m.r->accept(*this);
    value *= x;
  }
  void visit(const Neg &n) override {
    n.e->accept(*this);
    value = -value;
  }
};
struct Counter : Visitor {
  int nums = 0, adds = 0, muls = 0, negs = 0;
  void visit(const Num &) override { ++nums; }
  void visit(const Add &a) override {
    ++adds;
    a.l->accept(*this);
    a.r->accept(*this);
  }
  void visit(const Mul &m) override {
    ++muls;
    m.l->accept(*this);
    m.r->accept(*this);
  }
  void visit(const Neg &n) override {
    ++negs;
    n.e->accept(*this);
  }
};

std::unique_ptr<Node> buildExpr(int depth, unsigned &seed) {
  seed = seed * 1103515245u + 12345u;
  unsigned pick = (seed >> 16) & 7;
  if (depth == 0 || pick == 0) return std::make_unique<Num>((long long)((seed >> 8) % 10));
  if (pick < 4) return std::make_unique<Add>(buildExpr(depth - 1, seed), buildExpr(depth - 1, seed));
  if (pick < 7) return std::make_unique<Mul>(buildExpr(depth - 1, seed), buildExpr(depth - 1, seed));
  return std::make_unique<Neg>(buildExpr(depth - 1, seed));
}

// ---- type erasure with small buffer
class AnyShape {
  struct Concept {
    virtual ~Concept() = default;
    virtual double area() const = 0;
    virtual std::string name() const = 0;
    virtual Concept *cloneInto(void *buf) const = 0;
    virtual Concept *clone() const = 0;
  };
  template <class T>
  struct Model final : Concept {
    T obj;
    explicit Model(T o) : obj(std::move(o)) {}
    double area() const override { return obj.area(); }
    std::string name() const override { return obj.name(); }
    Concept *cloneInto(void *buf) const override { return new (buf) Model(obj); }
    Concept *clone() const override { return new Model(obj); }
  };
  static constexpr std::size_t kBuf = 24;
  alignas(std::max_align_t) unsigned char buf_[kBuf];
  Concept *p_ = nullptr;
  bool inline_ = false;

  void reset() {
    if (!p_) return;
    if (inline_)
      p_->~Concept();
    else
      delete p_;
    p_ = nullptr;
  }

 public:
  static int heapModels;
  template <class T, class = std::enable_if_t<!std::is_same_v<std::decay_t<T>, AnyShape>>>
  AnyShape(T obj) {
    if constexpr (sizeof(Model<T>) <= kBuf && alignof(Model<T>) <= alignof(std::max_align_t)) {
      p_ = new (buf_) Model<T>(std::move(obj));
      inline_ = true;
    } else {
      p_ = new Model<T>(std::move(obj));
      ++heapModels;
    }
  }
  AnyShape(const AnyShape &o) : inline_(o.inline_) {
    if (o.p_) p_ = inline_ ? o.p_->cloneInto(buf_) : o.p_->clone();
    if (p_ && !inline_) ++heapModels;
  }
  AnyShape &operator=(const AnyShape &o) {
    if (this != &o) {
      reset();
      inline_ = o.inline_;
      if (o.p_) p_ = inline_ ? o.p_->cloneInto(buf_) : o.p_->clone();
      if (p_ && !inline_) ++heapModels;
    }
    return *this;
  }
  ~AnyShape() { reset(); }
  double area() const { return p_->area(); }
  std::string name() const { return p_->name() + (inline_ ? "[sbo]" : "[heap]"); }
};
int AnyShape::heapModels = 0;

struct Circle {
  double r;
  double area() const { return 3.141592653589793 * r * r; }
  std::string name() const { return "circle"; }
};
struct Rect {
  double w, h;
  double area() const { return w * h; }
  std::string name() const { return "rect"; }
};
struct Polygon {
  std::vector<std::pair<double, double>> pts;
  double area() const {
    double s = 0;
    for (std::size_t i = 0; i < pts.size(); i++) {
      auto [x1, y1] = pts[i];
      auto [x2, y2] = pts[(i + 1) % pts.size()];
      s += x1 * y2 - x2 * y1;
    }
    return s < 0 ? -s / 2 : s / 2;
  }
  std::string name() const { return "polygon" + std::to_string(pts.size()); }
};
struct BigShape {
  double pad[8];
  double area() const { return pad[0] * pad[7]; }
  std::string name() const { return "big"; }
};

// ---- default arguments and pointers to virtual members
struct Greeter {
  virtual ~Greeter() = default;
  virtual std::string greet(const std::string &who = "base-default") const { return "Greeter hi " + who; }
  virtual int code(int x) const { return x; }
  virtual int code(double x) const { return (int)(x * 10); }
};
struct LoudGreeter : Greeter {
  std::string greet(const std::string &who = "derived-default") const override { return "LOUD HI " + who; }
  using Greeter::code;
  int code(int x) const override { return x * 100; }
};
struct Other {
  virtual ~Other() = default;
  int tag = 7;
  virtual int otherFn(int x) const { return x + tag; }
};
struct Both : Other, LoudGreeter {
  int otherFn(int x) const override { return x * tag; }
  int code(int x) const override { return -x; }
};

// ---- template method
class Report {
 public:
  std::string render() const { return header() + "|" + body() + "|" + footer(); }
  virtual ~Report() = default;

 private:
  virtual std::string header() const { return "H"; }
  virtual std::string body() const = 0;
  virtual std::string footer() const { return "F"; }
};
struct Sales : Report {
 private:
  std::string body() const override { return "sales"; }
};
struct Audit : Report {
 private:
  std::string header() const override { return "AUDIT"; }
  std::string body() const override { return "ledger"; }
  std::string footer() const override { return "signed"; }
};

// ---- three ways to dispatch the same computation
struct Op {
  virtual ~Op() = default;
  virtual long long apply(long long a, long long b) const = 0;
};
struct OpAdd : Op {
  long long apply(long long a, long long b) const override { return a + b; }
};
struct OpSub : Op {
  long long apply(long long a, long long b) const override { return a - b; }
};
struct OpMul : Op {
  long long apply(long long a, long long b) const override { return a * b % 1000003; }
};
struct OpXor : Op {
  long long apply(long long a, long long b) const override { return a ^ b; }
};
struct VAdd {};
struct VSub {};
struct VMul {};
struct VXor {};
using VOp = std::variant<VAdd, VSub, VMul, VXor>;
long long applyV(const VOp &op, long long a, long long b) {
  return std::visit([&](auto o) -> long long {
    using T = decltype(o);
    if constexpr (std::is_same_v<T, VAdd>) return a + b;
    else if constexpr (std::is_same_v<T, VSub>) return a - b;
    else if constexpr (std::is_same_v<T, VMul>) return a * b % 1000003;
    else return a ^ b;
  }, op);
}
long long fAdd(long long a, long long b) { return a + b; }
long long fSub(long long a, long long b) { return a - b; }
long long fMul(long long a, long long b) { return a * b % 1000003; }
long long fXor(long long a, long long b) { return a ^ b; }

}  // namespace

int main() {
  std::printf("== visitors\n");
  unsigned seed = 2024;
  for (int i = 0; i < 6; i++) {
    auto e = buildExpr(4, seed);
    Printer p;
    Evaluator ev;
    Counter c;
    e->accept(p);
    e->accept(ev);
    e->accept(c);
    std::printf("%s = %lld  [num=%d add=%d mul=%d neg=%d]\n", p.out.c_str(), ev.value, c.nums, c.adds, c.muls,
                c.negs);
  }

  std::printf("== type erasure\n");
  {
    std::vector<AnyShape> shapes;
    shapes.emplace_back(Circle{1.5});
    shapes.emplace_back(Rect{2, 3.5});
    shapes.emplace_back(Polygon{{{0, 0}, {4, 0}, {4, 3}, {0, 3}}});
    shapes.emplace_back(BigShape{{2, 0, 0, 0, 0, 0, 0, 5}});
    shapes.push_back(shapes[0]);
    AnyShape copy = shapes[2];
    shapes.push_back(copy);
    shapes[1] = shapes[3];
    double total = 0;
    for (auto &s : shapes) {
      std::printf("%-16s area=%.4f\n", s.name().c_str(), s.area());
      total += s.area();
    }
    std::printf("total=%.4f heap models created=%d\n", total, AnyShape::heapModels);
  }

  std::printf("== default arguments and member pointers\n");
  {
    LoudGreeter lg;
    Greeter &g = lg;
    std::printf("%s\n", g.greet().c_str());
    std::printf("%s\n", lg.greet().c_str());
    std::printf("%s\n", g.greet("explicit").c_str());
    std::string (Greeter::*pg)(const std::string &) const = &Greeter::greet;
    Greeter base;
    std::printf("via pmf on base: %s\n", (base.*pg)("x").c_str());
    std::printf("via pmf on derived: %s\n", (g.*pg)("y").c_str());
    int (Greeter::*ci)(int) const = &Greeter::code;
    int (Greeter::*cd)(double) const = &Greeter::code;
    std::printf("code(int) base=%d derived=%d; code(double) base=%d derived=%d\n", (base.*ci)(3), (g.*ci)(3),
                (base.*cd)(0.5), (g.*cd)(0.5));
    Both both;
    Greeter &bg = both;
    Other &bo = both;
    int (Other::*of)(int) const = &Other::otherFn;
    std::printf("Both via Greeter pmf code=%d via Other pmf=%d greet=%s\n", (bg.*ci)(9), (bo.*of)(6),
                bg.greet().c_str());
    std::function<int(const Greeter &, int)> fcode = ci;
    std::printf("std::function over pmf: %d %d %d\n", fcode(base, 4), fcode(lg, 4), fcode(both, 4));
  }

  std::printf("== template method\n");
  {
    std::vector<std::unique_ptr<Report>> reports;
    reports.push_back(std::make_unique<Sales>());
    reports.push_back(std::make_unique<Audit>());
    for (auto &r : reports) std::printf("%s\n", r->render().c_str());
  }

  std::printf("== dispatch strategies agree\n");
  {
    std::vector<std::unique_ptr<Op>> vops;
    vops.push_back(std::make_unique<OpAdd>());
    vops.push_back(std::make_unique<OpSub>());
    vops.push_back(std::make_unique<OpMul>());
    vops.push_back(std::make_unique<OpXor>());
    std::vector<VOp> varops{VAdd{}, VSub{}, VMul{}, VXor{}};
    long long (*table[])(long long, long long) = {fAdd, fSub, fMul, fXor};
    long long a1 = 1, a2 = 1, a3 = 1;
    unsigned s = 99;
    int disagreements = 0;
    for (int i = 0; i < 3000; i++) {
      s = s * 1664525u + 1013904223u;
      int k = (s >> 28) & 3;
      long long b = (long long)((s >> 8) & 0xFFFF);
      a1 = vops[k]->apply(a1, b) % 1000000007;
      a2 = applyV(varops[k], a2, b) % 1000000007;
      a3 = table[k](a3, b) % 1000000007;
      if (a1 != a2 || a2 != a3) ++disagreements;
      if (i % 500 == 499) std::printf("step %4d: virtual=%lld variant=%lld table=%lld\n", i + 1, a1, a2, a3);
    }
    std::printf("disagreements=%d\n", disagreements);
  }
  return 0;
}
