// The hashed containers: std::unordered_map and unordered_set with the
// default hash and with user hashes and equality, unordered_multimap and
// unordered_multiset, bucket_count/load_factor/rehash/reserve and the
// growth policy libc++ follows, find/count/contains/erase, iteration, and
// node extract/merge.
//
// The iteration order of a hashed container is fixed by libc++'s code and
// the hash values, and both engines run the same libc++ compiled into the
// same module, so the raw order is printed as well as a sorted copy; a
// disagreement in the raw order is a miscompile of the hashing or bucket
// code rather than an ordering the standard leaves open.  std::hash of
// integers is the identity in libc++, and of strings is MurmurHash2 over
// the bytes (32-bit size_t on wasm32), so the bucket of every key is a
// computed value worth checking too.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct Point {
  int x, y;
  bool operator==(const Point &) const = default;
};
struct PointHash {
  std::size_t operator()(const Point &p) const {
    return (std::size_t)p.x * 73856093u ^ (std::size_t)p.y * 19349663u;
  }
};

struct Caseless {
  std::size_t operator()(const std::string &s) const {
    std::size_t h = 5381;
    for (char c : s) h = h * 33 + (std::size_t)(c | 0x20);
    return h;
  }
  bool operator()(const std::string &a, const std::string &b) const {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++)
      if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
    return true;
  }
};

template <class M>
std::vector<std::pair<typename M::key_type, typename M::mapped_type>> sortedOf(const M &m) {
  std::vector<std::pair<typename M::key_type, typename M::mapped_type>> v(m.begin(), m.end());
  std::sort(v.begin(), v.end());
  return v;
}

}  // namespace

int main() {
  std::printf("== unordered_map<int,int>\n");
  std::unordered_map<int, int> m;
  std::size_t lastBuckets = m.bucket_count();
  for (int i = 0; i < 200; i++) {
    m[i * 37 % 211] = i;
    if (m.bucket_count() != lastBuckets) {
      std::printf("size %3zu: buckets %zu -> %zu load=%.3f\n", m.size(), lastBuckets,
                  m.bucket_count(), m.load_factor());
      lastBuckets = m.bucket_count();
    }
  }
  std::printf("size=%zu max_load=%.2f\n", m.size(), m.max_load_factor());
  int shown = 0;
  std::printf("raw order head:");
  for (auto &[k, v] : m) {
    if (shown++ == 16) break;
    std::printf(" %d:%d", k, v);
  }
  std::printf("\n");
  std::printf("find(37)=%d count(1)=%zu contains(210)=%d contains(209)=%d\n", m.find(37)->second,
              m.count(1), m.contains(210), m.contains(209));
  std::size_t erased = 0;
  for (int k = 0; k < 211; k += 3) erased += m.erase(k);
  std::printf("erased=%zu size=%zu\n", erased, m.size());
  long ks = 0, vs = 0;
  for (auto &[k, v] : m) {
    ks += k;
    vs += (long)v * k;
  }
  std::printf("key sum=%ld weighted=%ld\n", ks, vs);
  for (int k : {1, 2, 4, 5, 100, 200})
    std::printf("bucket(%d)=%zu size=%zu\n", k, m.bucket(k), m.bucket_size(m.bucket(k)));

  std::printf("== rehash and reserve\n");
  std::unordered_set<int> s;
  s.reserve(100);
  std::printf("after reserve(100) buckets=%zu\n", s.bucket_count());
  for (int i = 0; i < 50; i++) s.insert(i * i);
  s.rehash(500);
  std::printf("after rehash(500) buckets=%zu size=%zu\n", s.bucket_count(), s.size());
  s.max_load_factor(4.0f);
  s.rehash(0);
  std::printf("after max_load 4 rehash(0) buckets=%zu load=%.3f\n", s.bucket_count(),
              s.load_factor());
  std::vector<int> sv(s.begin(), s.end());
  std::printf("raw first 10:");
  for (int i = 0; i < 10; i++) std::printf(" %d", sv[i]);
  std::sort(sv.begin(), sv.end());
  std::printf("\nsorted last 5:");
  for (std::size_t i = sv.size() - 5; i < sv.size(); i++) std::printf(" %d", sv[i]);
  std::printf("\n");

  std::printf("== string keys\n");
  std::unordered_map<std::string, int> wc;
  const char *text =
      "one two three two three three four four four four five five five five five "
      "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu";
  std::string w;
  for (const char *p = text;; ++p) {
    if (*p == ' ' || !*p) {
      if (!w.empty()) wc[w]++;
      w.clear();
      if (!*p) break;
    } else {
      w += *p;
    }
  }
  std::printf("distinct=%zu buckets=%zu\n", wc.size(), wc.bucket_count());
  for (auto &[k, v] : sortedOf(wc)) std::printf("%s=%d ", k.c_str(), v);
  std::printf("\nraw:");
  for (auto &[k, v] : wc) std::printf(" %s", k.c_str());
  std::printf("\n");
  std::hash<std::string> hs;
  for (const char *k : {"", "a", "abc", "hello world", "The quick brown fox"})
    std::printf("hash(\"%s\") = %zu\n", k, hs(k));
  std::hash<int> hi;
  std::hash<double> hd;
  std::printf("hash(int 12345)=%zu hash(double 0.0)=%zu hash(-0.0)=%zu hash(1.5)=%zu\n",
              hi(12345), hd(0.0), hd(-0.0), hd(1.5));

  std::printf("== custom hash and equality\n");
  std::unordered_map<Point, std::string, PointHash> grid;
  for (int x = -2; x <= 2; x++)
    for (int y = -2; y <= 2; y++)
      grid[{x, y}] = std::to_string(x) + "," + std::to_string(y);
  std::printf("grid size=%zu (0,0)=%s (2,-1)=%s missing=%d\n", grid.size(),
              grid[{0, 0}].c_str(), grid.at({2, -1}).c_str(), grid.count({3, 3}) == 0);
  std::unordered_set<std::string, Caseless, Caseless> ci;
  for (const char *k : {"Hello", "HELLO", "world", "World", "hello", "Mixed", "mIxEd", "x"})
    ci.insert(k);
  std::printf("caseless set size=%zu contains(\"WORLD\")=%d\n", ci.size(),
              ci.contains("WORLD"));
  std::vector<std::string> civ(ci.begin(), ci.end());
  std::sort(civ.begin(), civ.end());
  for (auto &x : civ) std::printf(" %s", x.c_str());
  std::printf("\n");

  std::printf("== multi containers\n");
  std::unordered_multimap<int, std::string> mm;
  for (int i = 0; i < 12; i++) mm.emplace(i % 4, "v" + std::to_string(i));
  for (int k = 0; k < 5; k++) {
    auto [lo, hi2] = mm.equal_range(k);
    std::vector<std::string> vals;
    for (auto it = lo; it != hi2; ++it) vals.push_back(it->second);
    std::sort(vals.begin(), vals.end());
    std::printf("key %d count=%zu:", k, mm.count(k));
    for (auto &v : vals) std::printf(" %s", v.c_str());
    std::printf("\n");
  }
  std::unordered_multiset<char> letters;
  for (char c : std::string("mississippi")) letters.insert(c);
  for (char c : std::string("mispq")) std::printf("%c:%zu ", c, letters.count(c));
  std::printf("\n");
  letters.erase('s');
  std::printf("after erase s size=%zu\n", letters.size());

  std::printf("== extract and merge\n");
  std::unordered_map<int, char> ua{{1, 'a'}, {2, 'b'}, {3, 'c'}};
  std::unordered_map<int, char> ub{{3, 'C'}, {4, 'D'}, {5, 'E'}};
  auto nh = ua.extract(1);
  nh.key() = 10;
  ua.insert(std::move(nh));
  ua.merge(ub);
  for (auto &[k, v] : sortedOf(ua)) std::printf("%d=%c ", k, v);
  std::printf("| ub left:");
  for (auto &[k, v] : sortedOf(ub)) std::printf(" %d=%c", k, v);
  std::printf("\n");

  std::printf("== dedupe and set algebra\n");
  std::vector<int> data;
  uint32_t seed = 99;
  for (int i = 0; i < 400; i++) {
    seed = seed * 1103515245u + 12345u;
    data.push_back((int)((seed >> 16) % 150));
  }
  std::unordered_set<int> seen, dups;
  for (int x : data)
    if (!seen.insert(x).second) dups.insert(x);
  std::unordered_set<int> evens;
  for (int i = 0; i < 150; i += 2) evens.insert(i);
  int both = 0, onlySeen = 0;
  for (int x : seen) (evens.contains(x) ? both : onlySeen)++;
  std::printf("distinct=%zu dups=%zu seen&evens=%d seen-evens=%d\n", seen.size(), dups.size(),
              both, onlySeen);
  std::vector<int> missing;
  for (int i = 0; i < 150; i++)
    if (!seen.contains(i)) missing.push_back(i);
  std::printf("missing:");
  for (int x : missing) std::printf(" %d", x);
  std::printf("\n");
  return 0;
}
