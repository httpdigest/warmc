// Run-time type information: dynamic_cast of pointers and references --
// downcasts, upcasts, cross casts between the two sides of a multiple
// inheritance, casts through a virtual base, casts that fail (null, or
// std::bad_cast for a reference), dynamic_cast<void *> to the most derived
// object -- and typeid of objects, of types, of pointers and of null
// dereferences (std::bad_typeid), with type_info compared, ordered by
// before() and hashed.
//
// dynamic_cast to a non-final class is a call into libc++abi's
// __dynamic_cast, which walks the __vmi_class_type_info tables in linear
// memory; a cast to a final class is folded by clang into a compare of the
// vtable pointer.  Both kinds are here, applied to every object of a mixed
// collection so that each path is taken with every possible answer.
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <map>
#include <vector>

namespace {

struct Node {
  virtual ~Node() = default;
  virtual const char *name() const = 0;
};
struct Leaf : Node {
  int v;
  explicit Leaf(int v) : v(v) {}
  const char *name() const override { return "Leaf"; }
};
struct BigLeaf final : Leaf {
  explicit BigLeaf(int v) : Leaf(v * 1000) {}
  const char *name() const override { return "BigLeaf"; }
};
struct Branch : Node {
  std::vector<std::unique_ptr<Node>> kids;
  const char *name() const override { return "Branch"; }
};

// Cross casting: Printable and Sized are unrelated, but one object may be both.
struct Printable {
  virtual ~Printable() = default;
  virtual std::string print() const = 0;
};
struct Sized {
  virtual ~Sized() = default;
  virtual int size() const = 0;
};
struct Text : Printable, Sized {
  std::string s;
  explicit Text(std::string s) : s(std::move(s)) {}
  std::string print() const override { return "\"" + s + "\""; }
  int size() const override { return (int)s.size(); }
};
struct Number : Printable {
  int n;
  explicit Number(int n) : n(n) {}
  std::string print() const override { return std::to_string(n); }
};
struct Blob : Sized {
  int bytes;
  explicit Blob(int b) : bytes(b) {}
  int size() const override { return bytes; }
};

// A diamond on a virtual base.
struct VB {
  int tag = 7;
  virtual ~VB() = default;
};
struct VL : virtual VB {
  int l = 1;
};
struct VR : virtual VB {
  int r = 2;
};
struct VD : VL, VR {
  int d = 3;
};

// Ambiguous base: two non-virtual copies of Amb, so a downcast from one side
// works but a cast to Amb from the most derived is ambiguous.
struct Amb {
  virtual ~Amb() = default;
  int which = 0;
};
struct AmbL : Amb {
  AmbL() { which = 1; }
};
struct AmbR : Amb {
  AmbR() { which = 2; }
};
struct AmbBoth : AmbL, AmbR {};

// A private base: dynamic_cast must not see through it.
struct Hidden {
  virtual ~Hidden() = default;
};
struct Secretive : private Hidden {
  virtual void f() {}
  Hidden *expose() { return this; }
};

int sumLeaves(const Node *n) {
  if (auto *l = dynamic_cast<const Leaf *>(n)) return l->v;
  int s = 0;
  if (auto *b = dynamic_cast<const Branch *>(n))
    for (auto &k : b->kids) s += sumLeaves(k.get());
  return s;
}

std::unique_ptr<Node> buildTree(int depth, int &counter) {
  if (depth == 0) {
    ++counter;
    if (counter % 4 == 0) return std::make_unique<BigLeaf>(counter);
    return std::make_unique<Leaf>(counter);
  }
  auto b = std::make_unique<Branch>();
  for (int i = 0; i < depth; i++) b->kids.push_back(buildTree(depth - 1, counter));
  return b;
}

void describe(const Node &n, int indent) {
  std::printf("%*s%s typeid=%s", indent, "", n.name(), typeid(n).name());
  if (typeid(n) == typeid(Leaf)) std::printf(" [exactly Leaf]");
  if (dynamic_cast<const BigLeaf *>(&n)) std::printf(" [is BigLeaf]");
  if (auto *l = dynamic_cast<const Leaf *>(&n)) std::printf(" v=%d", l->v);
  std::printf("\n");
  if (auto *b = dynamic_cast<const Branch *>(&n))
    for (auto &k : b->kids) describe(*k, indent + 2);
}

}  // namespace

int main() {
  std::printf("== tree walk by dynamic_cast\n");
  {
    int counter = 0;
    auto root = buildTree(3, counter);
    describe(*root, 0);
    std::printf("leaves=%d sum=%d\n", counter, sumLeaves(root.get()));
  }

  std::printf("== cross casts\n");
  {
    std::vector<std::unique_ptr<Printable>> ps;
    ps.push_back(std::make_unique<Text>("alpha"));
    ps.push_back(std::make_unique<Number>(42));
    ps.push_back(std::make_unique<Text>("be"));
    ps.push_back(std::make_unique<Number>(-7));
    for (auto &p : ps) {
      Sized *s = dynamic_cast<Sized *>(p.get());
      Text *t = dynamic_cast<Text *>(p.get());
      std::printf("%-8s sized=%d size=%d text=%d back=%d\n", p->print().c_str(),
                  s != nullptr, s ? s->size() : -1, t != nullptr,
                  s ? dynamic_cast<Printable *>(s) == p.get() : -1);
    }
    std::vector<std::unique_ptr<Sized>> ss;
    ss.push_back(std::make_unique<Blob>(1024));
    ss.push_back(std::make_unique<Text>("gamma ray"));
    for (auto &s : ss) {
      auto *p = dynamic_cast<Printable *>(s.get());
      std::printf("size=%d printable=%s\n", s->size(),
                  p ? p->print().c_str() : "(no)");
    }
  }

  std::printf("== reference casts and bad_cast\n");
  {
    Number n(5);
    Text t("x");
    Printable *items[] = {&n, &t, &n};
    for (Printable *p : items) {
      try {
        Sized &s = dynamic_cast<Sized &>(*p);
        std::printf("ref cast ok size=%d\n", s.size());
      } catch (const std::bad_cast &e) {
        std::printf("bad_cast caught for %s\n", p->print().c_str());
      }
    }
  }

  std::printf("== virtual base casts\n");
  {
    VD d;
    VB *b = &d;
    VL *l = dynamic_cast<VL *>(b);
    VR *r = dynamic_cast<VR *>(b);
    VD *dd = dynamic_cast<VD *>(b);
    std::printf("l=%d r=%d d=%d tag=%d\n", l ? l->l : -1, r ? r->r : -1,
                dd ? dd->d : -1, b->tag);
    std::printf("cross l->r ok=%d\n", dynamic_cast<VR *>(l) == r);
    std::printf("void* agree=%d\n",
                dynamic_cast<void *>(b) == dynamic_cast<void *>(r) &&
                    dynamic_cast<void *>(l) == static_cast<void *>(&d));
    VL plain;
    VB *pb = &plain;
    std::printf("plain VL as VR=%d as VD=%d as VL=%d\n",
                dynamic_cast<VR *>(pb) != nullptr,
                dynamic_cast<VD *>(pb) != nullptr,
                dynamic_cast<VL *>(pb) != nullptr);
  }

  std::printf("== ambiguous and private bases\n");
  {
    AmbBoth ab;
    AmbL *al = &ab;
    AmbR *ar = &ab;
    Amb *viaL = al;
    Amb *viaR = ar;
    std::printf("viaL which=%d viaR which=%d\n", viaL->which, viaR->which);
    std::printf("viaL->AmbBoth ok=%d viaR->AmbBoth ok=%d\n",
                dynamic_cast<AmbBoth *>(viaL) == &ab,
                dynamic_cast<AmbBoth *>(viaR) == &ab);
    std::printf("viaL->AmbR cross ok=%d\n", dynamic_cast<AmbR *>(viaL) == ar);
    Secretive s;
    Hidden *h = s.expose();
    std::printf("private base downcast=%d\n",
                dynamic_cast<Secretive *>(h) != nullptr);
  }

  std::printf("== null and failed casts\n");
  {
    Node *none = nullptr;
    std::printf("null cast=%d\n", dynamic_cast<Leaf *>(none) == nullptr);
    Branch br;
    Node *n = &br;
    std::printf("branch as leaf=%d as bigleaf=%d\n",
                dynamic_cast<Leaf *>(n) != nullptr,
                dynamic_cast<BigLeaf *>(n) != nullptr);
  }

  std::printf("== typeid\n");
  {
    Leaf l(1);
    BigLeaf bl(2);
    Node &rn = bl;
    Node *pn = &l;
    std::printf("typeid(int)=%s typeid(double)=%s typeid(char*)=%s\n",
                typeid(int).name(), typeid(double).name(), typeid(char *).name());
    std::printf("typeid(const int&)==int: %d\n",
                typeid(const int &) == typeid(int));
    std::printf("static type of pn: %s\n", typeid(pn).name());
    std::printf("dynamic type of *pn: %s\n", typeid(*pn).name());
    std::printf("dynamic type of rn: %s\n", typeid(rn).name());
    std::printf("rn is BigLeaf=%d is Leaf=%d\n", typeid(rn) == typeid(BigLeaf),
                typeid(rn) == typeid(Leaf));
    std::printf("vector<int>=%s\n", typeid(std::vector<int>).name());
    std::printf("map<string,int>=%s\n", typeid(std::map<std::string, int>).name());
    std::printf("lambda kinds differ=%d\n",
                typeid([] {}) != typeid([] {}));
    std::printf("function type=%s\n", typeid(int(double, char)).name());
    std::printf("member ptr=%s\n", typeid(int Leaf::*).name());

    try {
      Node *nul = nullptr;
      std::printf("not reached %s\n", typeid(*nul).name());
    } catch (const std::bad_typeid &e) {
      std::printf("bad_typeid caught\n");
    }

    std::vector<std::type_index> idx = {typeid(int),  typeid(Leaf),
                                        typeid(char), typeid(BigLeaf),
                                        typeid(int),  typeid(double)};
    std::map<std::type_index, int> counts;
    for (auto &t : idx) counts[t]++;
    std::vector<std::string> names;
    for (auto &[t, c] : counts)
      names.push_back(std::string(t.name()) + "=" + std::to_string(c));
    std::sort(names.begin(), names.end());
    for (auto &s : names) std::printf("count %s\n", s.c_str());
    std::printf("hash equal for equal types=%d\n",
                std::type_index(typeid(Leaf)).hash_code() ==
                    std::type_index(typeid(*pn)).hash_code());
  }
  return 0;
}
