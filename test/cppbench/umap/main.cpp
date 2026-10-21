// Word counting in a std::unordered_map keyed by std::string: generate
// pseudo-words, count them, then look up and erase a share of them.
//
// cbench/hashtab is a table the program lays out itself; this is the one most
// C++ programs reach for instead, a node per entry and a bucket array of
// pointers, with libc++'s string hash over each key and short strings held
// inline in the std::string itself.  Allocation, hashing and pointer chasing
// in one row.
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

int main() {
  static const char syll[][4] = {"ka", "lo", "mi", "nu", "pe", "ri", "sa", "to",
                                 "vu", "xe", "zo", "an", "el", "is", "or", "ux"};
  uint32_t x = 88172645u;
  uint64_t check = 0;
  for (int round = 0; round < 8; round++) {
    std::unordered_map<std::string, uint32_t> counts;
    std::string w;
    for (int i = 0; i < 120000; i++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      w.clear();
      int n = 1 + (x & 3);
      for (int k = 0; k < n; k++) w += syll[(x >> (4 + 4 * k)) & 15];
      if (x & 0x100000) w += "-long-suffix-past-the-inline-buffer";
      counts[w]++;
    }
    uint64_t hits = 0;
    for (int i = 0; i < 60000; i++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      w.clear();
      int n = 1 + (x & 3);
      for (int k = 0; k < n; k++) w += syll[(x >> (4 + 4 * k)) & 15];
      auto it = counts.find(w);
      if (it != counts.end()) {
        hits += it->second;
        if (i & 1) counts.erase(it);
      }
    }
    check = check * 1000003 + hits * 131 + counts.size();
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
