// The <algorithm> and <numeric> toolbox applied to one buffer in turn:
// transform, partial_sum, nth_element, unique after sort, rotate, reverse,
// inner_product and lower_bound.
//
// Each of these is a template the compiler instantiates and inlines for the
// iterator and lambda it is given, so what reaches warmc is a series of
// different loops over one vector: straight-line maps, a prefix sum carried
// through a register, a partition loop, a binary search.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <vector>

int main() {
  constexpr int N = 50000;
  std::vector<uint32_t> a(N), b(N);
  uint32_t x = 1618033u;
  uint64_t check = 0;
  for (int round = 0; round < 60; round++) {
    for (auto &v : a) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      v = x;
    }
    std::transform(a.begin(), a.end(), b.begin(), [](uint32_t v) { return (v >> 3) ^ (v * 5); });
    std::partial_sum(b.begin(), b.end(), b.begin());
    std::nth_element(a.begin(), a.begin() + N / 2, a.end());
    uint32_t median = a[N / 2];
    std::sort(b.begin(), b.end());
    auto end = std::unique(b.begin(), b.end(), [](uint32_t p, uint32_t q) { return (p >> 8) == (q >> 8); });
    size_t uniq = static_cast<size_t>(end - b.begin());
    std::rotate(a.begin(), a.begin() + round * 97 % N, a.end());
    std::reverse(a.begin(), a.begin() + N / 3);
    uint64_t dot = std::inner_product(a.begin(), a.begin() + N / 4, b.begin(), uint64_t{0});
    auto it = std::lower_bound(b.begin(), end, median);
    check = check * 1000003 + median + uniq + dot + static_cast<uint64_t>(it - b.begin());
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
