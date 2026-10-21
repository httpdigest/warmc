// std::map as an ordered index: insert, lower_bound range scans and erase on a
// red-black tree of a few tens of thousands of nodes.
//
// The balanced tree is the ordered container every standard library ships
// and no other corpus has: each insert or erase is a descent with a rebalance
// on the way back, each node its own allocation, and the range scans walk the
// tree's in-order successor links rather than an array.
#include <cstdint>
#include <cstdio>
#include <map>

int main() {
  std::map<uint32_t, uint32_t> m;
  uint32_t x = 521288629u;
  uint64_t acc = 0;
  for (int i = 0; i < 1200000; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    uint32_t k = x % 65536;
    switch ((x >> 16) & 3) {
      case 0:
      case 1:
        m[k] += i;
        break;
      case 2: {
        auto it = m.lower_bound(k);
        for (int n = 0; n < 4 && it != m.end(); n++, ++it) acc += it->first ^ it->second;
        break;
      }
      default:
        m.erase(k);
        break;
    }
  }
  std::printf("size = %zu acc = %llu\n", m.size(), static_cast<unsigned long long>(acc));
  return 0;
}
