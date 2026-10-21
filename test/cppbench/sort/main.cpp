// std::sort and std::stable_sort on records with a lambda comparator, every
// round on freshly shuffled keys.
//
// The other side of cbench/qsort: the same kind of records, but the
// comparison is a template argument, so libc++'s introsort is instantiated
// for it and the comparator inlines into the partition loop.  What remains is
// the sort's own control flow -- pivots, insertion sort on short runs, the
// merge buffer stable_sort allocates -- on 16-byte elements moved as values.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Rec {
  uint32_t key;
  uint32_t tie;
  double weight;
};

int main() {
  constexpr int N = 40000;
  std::vector<Rec> v(N);
  uint32_t x = 12345;
  uint64_t sum = 0;
  for (int round = 0; round < 30; round++) {
    for (int i = 0; i < N; i++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      v[i] = Rec{x % 5000, static_cast<uint32_t>(i), (x >> 8) * 0x1p-24};
    }
    if (round & 1) {
      std::stable_sort(v.begin(), v.end(),
                       [](const Rec &a, const Rec &b) { return a.key < b.key; });
    } else {
      std::sort(v.begin(), v.end(), [](const Rec &a, const Rec &b) {
        if (a.key != b.key) return a.key > b.key;
        return a.tie < b.tie;
      });
    }
    for (int i = 0; i < N; i += 97) sum = sum * 31 + v[i].key * 7 + v[i].tie;
  }
  std::printf("sum = %llu\n", static_cast<unsigned long long>(sum));
  return 0;
}
