// std::any as a heterogeneous property bag: a vector of slots holding ints,
// doubles, strings and a record too large for the small buffer, read back
// with any_cast, copied, reassigned, swapped and moved between slots.
//
// The open-ended form of type erasure, where variant is the closed one.
// libc++ keeps one function pointer per std::any, a handler instantiated per
// stored type, and every operation on the object -- copy, move, destroy, the
// type check inside any_cast -- is a call_indirect to that handler with an
// action code it dispatches on with a br_table.  The check itself compares
// the stored type_info's name pointer with the one asked for, and because the
// call sits in a noexcept path each any_cast is wrapped in a try_table whose
// handler calls std::terminate.  The small buffer is three pointers, twelve
// bytes aligned to four, so a uint32_t and a std::string live inline but a
// double, wanting eight-byte alignment, goes to the heap with Big: a copy of
// either is a malloc.  Dispatch, allocation and RTTI comparison together, in
// proportions only a type-erased container produces.
#include <any>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Big {
  uint32_t a[8];
};

uint64_t weigh(const std::any &v) {
  if (const auto *i = std::any_cast<uint32_t>(&v)) return *i;
  if (const auto *d = std::any_cast<double>(&v)) return static_cast<uint64_t>(*d * 16);
  if (const auto *s = std::any_cast<std::string>(&v)) return s->size() * 131 + (*s)[0];
  if (const auto *b = std::any_cast<Big>(&v)) return b->a[0] + b->a[7];
  return v.has_value() ? 1 : 0;
}

}  // namespace

int main() {
  std::vector<std::any> slots(512);
  uint32_t x = 1013904223u;
  uint64_t check = 0;
  for (int i = 0; i < 2800000; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    std::any &dst = slots[x % slots.size()];
    std::any &src = slots[(x >> 9) % slots.size()];
    switch ((x >> 20) & 7) {
      case 0: dst = x & 0xffff; break;
      case 1: dst = (x & 1023) * 0.125; break;
      case 2: dst = std::string((x >> 24) % 30 + 1, static_cast<char>('a' + x % 26)); break;
      case 3: {
        Big b;
        for (uint32_t k = 0; k < 8; k++) b.a[k] = x >> k;
        dst = b;
        break;
      }
      case 4: dst = src; break;  // copy through the handler
      case 5:
        if (&dst != &src) dst = std::move(src);
        break;
      case 6:
        dst.swap(src);
        break;
      default:
        if (auto *n = std::any_cast<uint32_t>(&dst)) *n += 3;
        else if (auto *s = std::any_cast<std::string>(&dst)) s->push_back('z');
        else dst.reset();
        break;
    }
    check += weigh(dst);
  }
  uint64_t tail = 0;
  for (const auto &s : slots) tail = tail * 7 + weigh(s);
  std::printf("check = %llu tail = %llu\n", static_cast<unsigned long long>(check),
              static_cast<unsigned long long>(tail));
  return 0;
}
