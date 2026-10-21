// Virtual calls through a vector of owning base-class pointers: four shapes in
// a random order, each scaled and measured every round.
//
// The C++ form of a dynamic dispatch the other corpora reach differently --
// ktbench/virtual goes through a wasm GC vtable struct and a call_ref, and
// cbench/fnptr through a table index the program keeps itself.  Here clang
// loads the vtable from linear memory and calls through call_indirect, two
// slots per object per round, with the receiver's type unknowable from the
// call site.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Shape {
  virtual ~Shape() = default;
  virtual double area() const = 0;
  virtual void scale(double k) = 0;
};

struct Circle final : Shape {
  double r;
  explicit Circle(double r) : r(r) {}
  double area() const override { return kPi * r * r; }
  void scale(double k) override { r *= k; }
};

struct Rect final : Shape {
  double w, h;
  Rect(double w, double h) : w(w), h(h) {}
  double area() const override { return w * h; }
  void scale(double k) override { w *= k; h *= k; }
};

struct Tri final : Shape {
  double b, h;
  Tri(double b, double h) : b(b), h(h) {}
  double area() const override { return 0.5 * b * h; }
  void scale(double k) override { b *= k; h *= k; }
};

struct Ring final : Shape {
  double r0, r1;
  Ring(double r0, double r1) : r0(r0), r1(r1) {}
  double area() const override { return kPi * (r1 * r1 - r0 * r0); }
  void scale(double k) override { r0 *= k; r1 *= k; }
};

}  // namespace

int main() {
  std::vector<std::unique_ptr<Shape>> shapes;
  uint32_t x = 2463534242u;
  for (int i = 0; i < 4096; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    double d = 1.0 + (x % 1000) / 100.0;
    switch (x >> 30) {
      case 0: shapes.push_back(std::make_unique<Circle>(d)); break;
      case 1: shapes.push_back(std::make_unique<Rect>(d, d * 0.5)); break;
      case 2: shapes.push_back(std::make_unique<Tri>(d, d + 1.0)); break;
      default: shapes.push_back(std::make_unique<Ring>(d, d + 0.25)); break;
    }
  }

  double total = 0;
  for (int round = 0; round < 4000; round++) {
    double k = (round & 1) ? 1.001 : 1.0 / 1.001;
    for (auto &s : shapes) {
      s->scale(k);
      total += s->area();
    }
  }
  std::printf("total = %.9e\n", total);
  return 0;
}
