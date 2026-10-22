// dynamic_cast and typeid over a class hierarchy with multiple and virtual
// inheritance: a diamond on a virtual base, downcasts from that base, cross
// casts between the two sides, and casts that fail.
//
// The parts of C++'s object model the virtual kernel leaves out.  A member
// of a virtual base is reached by loading the base's offset from the vtable
// and adding it to this, and an override called through the base goes via a
// virtual thunk that does the same.  A cast to a non-final class is a call
// into libc++abi's __dynamic_cast, which reads the object's type_info out of
// its vtable and walks the __vmi_class_type_info base lists looking for the
// target, comparing type_info by address and, where the addresses differ, by
// strcmp of the names.  Casts to the final classes clang folds into a compare
// of the vtable pointer against a constant, and typeid == into a compare of
// the name pointer, so the row is the runtime library's search through RTTI
// tables in linear memory beside the two checks the compiler did itself.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <typeinfo>
#include <vector>

namespace {

struct Node {
  uint32_t id;
  explicit Node(uint32_t id) : id(id) {}
  virtual ~Node() = default;
  virtual uint32_t weight() const { return id & 15; }
};

struct Named : virtual Node {
  uint32_t name;
  Named(uint32_t id, uint32_t name) : Node(id), name(name) {}
  virtual uint32_t label() const { return name * 3 + id; }
};

struct Valued : virtual Node {
  uint32_t value;
  Valued(uint32_t id, uint32_t v) : Node(id), value(v) {}
  virtual uint32_t eval() const { return value; }
  uint32_t weight() const override { return value & 31; }
};

struct Counter {
  uint32_t hits = 0;
  virtual ~Counter() = default;
  virtual void hit() { hits++; }
};

// The diamond: one Node shared by both sides.
struct Var final : Named, Valued {
  Var(uint32_t id, uint32_t name, uint32_t v) : Node(id), Named(id, name), Valued(id, v) {}
  uint32_t eval() const override { return value * 2 + name; }
};

struct Const final : Valued {
  Const(uint32_t id, uint32_t v) : Node(id), Valued(id, v) {}
};

struct Op : Named, Counter {
  Op(uint32_t id, uint32_t name) : Node(id), Named(id, name) {}
  uint32_t label() const override { return name ^ 0x55; }
};

struct Call final : Op, Valued {
  Call(uint32_t id, uint32_t name, uint32_t v)
      : Node(id), Op(id, name), Valued(id, v) {}
  uint32_t eval() const override { return value + hits; }
  void hit() override { hits += 2; }
};

struct Plain final : Node {
  using Node::Node;
};

__attribute__((noinline)) uint64_t visit(Node *n) {
  uint64_t s = n->weight();
  if (auto *v = dynamic_cast<Valued *>(n)) s += v->eval();
  if (auto *m = dynamic_cast<Named *>(n)) {
    s += m->label();
    // A cross cast: from one side of the hierarchy to the other.
    if (auto *v = dynamic_cast<Valued *>(m)) s += v->value << 1;
    if (auto *c = dynamic_cast<Counter *>(m)) {
      c->hit();
      s += c->hits;
    }
  }
  if (dynamic_cast<Var *>(n)) s += 7;
  if (auto *c = dynamic_cast<Call *>(n)) s += c->hits & 3;
  if (typeid(*n) == typeid(Const)) s += 11;
  return s;
}

}  // namespace

int main() {
  std::vector<std::unique_ptr<Node>> nodes;
  uint32_t x = 19937u;
  for (uint32_t i = 0; i < 2048; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    switch (x % 5) {
      case 0: nodes.push_back(std::make_unique<Var>(i, x >> 20, x >> 8 & 255)); break;
      case 1: nodes.push_back(std::make_unique<Const>(i, x >> 12 & 1023)); break;
      case 2: nodes.push_back(std::make_unique<Op>(i, x >> 16)); break;
      case 3: nodes.push_back(std::make_unique<Call>(i, x >> 18, x >> 4 & 63)); break;
      default: nodes.push_back(std::make_unique<Plain>(i)); break;
    }
  }
  uint64_t check = 0;
  for (int round = 0; round < 800; round++) {
    for (auto &n : nodes) check += visit(n.get());
    check = check * 3 + round;
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
