// Move semantics on a vector of heap-sized strings: insert a third of the way
// in, erase from the middle, rotate, partition, sort by length and hand ranges
// to a second vector through move iterators, with no string's characters
// ever copied after it is built.
//
// What std::move buys and what it still costs.  A libc++ std::string is
// twelve bytes on wasm32, and every string here is longer than the ten
// characters its inline buffer holds, so it owns a heap block and a move
// never touches the characters.  A move construction copies the twelve bytes
// as an i64 and an i32 and zeroes the source the same way; a move assignment
// first tests the destination's long-mode bit, the sign of its last byte,
// and frees any block it still owns, then copies and resets the source with
// two byte stores.  insert and erase shift every element after the gap one
// move at a time, sort and stable_partition move through temporaries, and
// only growth is cheap: libc++ counts std::string as trivially relocatable,
// so a reallocation is one memory.copy.  The row is that traffic of small
// fixed-size copies and the stores that empty each source.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

int main() {
  uint32_t x = 1597334677u;
  auto next = [&x] {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
  };
  uint64_t check = 0;
  std::vector<std::string> pool;
  for (int i = 0; i < 3000; i++) {
    uint32_t r = next();
    pool.push_back(std::string(16 + r % 48, static_cast<char>('A' + r % 26)) +
                   std::to_string(i));
  }

  std::vector<std::string> a, b;
  for (int round = 0; round < 72; round++) {
    // Take the pool's strings by move; the pool keeps empty husks.
    a.clear();
    for (auto &s : pool) {
      if (next() & 1) a.insert(a.begin() + (a.size() / 3), std::move(s));
      else a.push_back(std::move(s));
    }
    for (int k = 0; k < 200; k++) {
      uint32_t r = next();
      size_t at = r % a.size();
      std::string s = std::move(a[at]);
      a.erase(a.begin() + at);
      a.insert(a.begin() + (r >> 16) % (a.size() + 1), std::move(s));
    }
    std::rotate(a.begin(), a.begin() + (next() % a.size()), a.end());
    auto mid = std::stable_partition(a.begin(), a.end(),
                                     [](const std::string &s) { return s.size() & 1; });
    std::sort(a.begin(), mid, [](const std::string &p, const std::string &q) {
      return p.size() != q.size() ? p.size() < q.size() : p < q;
    });
    // Hand the second half over to b, then swap the vectors themselves.
    b.assign(std::make_move_iterator(a.begin() + a.size() / 2), std::make_move_iterator(a.end()));
    a.erase(a.begin() + a.size() / 2, a.end());
    a.swap(b);
    for (size_t i = 0; i < a.size(); i += 17) check = check * 31 + a[i].size() + a[i][0];
    for (size_t i = 0; i < b.size(); i += 13) check = check * 37 + b[i].size() + b[i].back();
    // Return everything to the pool, in its new order.
    size_t j = 0;
    for (auto &s : a) pool[j++] = std::move(s);
    for (auto &s : b) pool[j++] = std::move(s);
    b.clear();
  }
  std::printf("check = %llu first = %zu%c\n", static_cast<unsigned long long>(check),
              pool[0].size(), pool[0][0]);
  return 0;
}
