// std::list as the linked list it is: an LRU cache that splices a hit to the
// front and evicts from the back, with a vector of iterators as its index, and
// every so often the cache's contents copied out, sorted with list::sort,
// merged and spliced in ranges.
//
// splice is the operation only a node-based list offers -- a constant number
// of pointer stores, no allocation, every iterator still valid -- and libc++
// does it as an unlink and relink against the sentinel node that lives inside
// the list object.  clang inlines splice, merge, push_front and pop_back into
// main as straight runs of i32 loads and stores; only list::sort stays out of
// line, as a recursive merge sort on the links themselves, so it too is all
// loads and stores of next and prev pointers with no element ever moved.
// Pointer chasing through nodes the allocator scattered, which is what the
// row is for.
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <list>
#include <vector>

namespace {

struct Entry {
  uint32_t key;
  uint32_t value;
};

}  // namespace

int main() {
  constexpr uint32_t Keys = 1 << 14, Cap = 3000;
  std::list<Entry> lru;
  std::vector<std::list<Entry>::iterator> where(Keys, lru.end());
  uint32_t x = 3141592653u;
  uint64_t hits = 0, misses = 0, check = 0;
  for (int i = 0; i < 9000000; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    // Skewed keys: a hot set most of the time, the whole range otherwise.
    uint32_t key = (x & 3) ? (x >> 4) % 4096 : (x >> 4) % Keys;
    auto it = where[key];
    if (it != lru.end()) {
      hits++;
      it->value += i;
      lru.splice(lru.begin(), lru, it);
    } else {
      misses++;
      lru.push_front({key, static_cast<uint32_t>(i)});
      where[key] = lru.begin();
      if (lru.size() > Cap) {
        where[lru.back().key] = lru.end();
        check += lru.back().value;
        lru.pop_back();
      }
    }

    if ((i & 0x3ffff) == 0x3ffff) {
      // Snapshot the cache, sort two halves by value, merge them back and
      // splice a middle range to the end.
      std::list<Entry> a(lru.begin(), lru.end()), b;
      auto mid = a.begin();
      std::advance(mid, a.size() / 2);
      b.splice(b.end(), a, mid, a.end());
      auto byValue = [](const Entry &p, const Entry &q) { return p.value < q.value; };
      a.sort(byValue);
      b.sort(byValue);
      a.merge(b, byValue);
      auto from = a.begin();
      std::advance(from, a.size() / 3);
      auto to = from;
      std::advance(to, a.size() / 3);
      a.splice(a.end(), a, from, to);
      uint32_t pos = 0;
      for (const Entry &e : a) check = check * 31 + (e.key ^ pos++);
      a.reverse();
      check += a.front().key + b.size();
    }
  }
  std::printf("hits = %llu misses = %llu check = %llu\n",
              static_cast<unsigned long long>(hits), static_cast<unsigned long long>(misses),
              static_cast<unsigned long long>(check));
  return 0;
}
