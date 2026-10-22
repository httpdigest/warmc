// The ordered associative containers: std::map (operator[], at, insert,
// emplace, try_emplace, insert_or_assign, erase by key, iterator and range,
// find, count, contains, lower_bound/upper_bound/equal_range, iteration
// forward and backward), std::multimap with equal_range over duplicate
// keys, std::set and std::multiset with custom comparators and
// heterogeneous lookup (std::less<>), node extract and merge between
// containers, maps keyed by a struct with a user comparator, and a word
// count and an interval index built on top of them.
//
// libc++'s map is a red-black tree; every insert and erase rebalances, so a
// sequence of a few hundred inserts and erases in a fixed pseudo-random
// order walks most of the rotation cases, and the tree is checked by
// printing it in order after each phase.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Key {
  std::string dept;
  int id;
};
struct KeyLess {
  bool operator()(const Key &a, const Key &b) const {
    if (a.dept != b.dept) return a.dept < b.dept;
    return a.id > b.id;  // descending ids within a department
  }
};

template <class M>
void dumpMap(const char *label, const M &m) {
  std::printf("%-20s {", label);
  bool first = true;
  for (const auto &[k, v] : m) {
    std::printf(first ? "%d:%d" : " %d:%d", (int)k, (int)v);
    first = false;
  }
  std::printf("} size=%zu\n", m.size());
}

template <class S>
void dumpSet(const char *label, const S &s) {
  std::printf("%-20s {", label);
  bool first = true;
  for (const auto &k : s) {
    std::printf(first ? "%d" : " %d", (int)k);
    first = false;
  }
  std::printf("} size=%zu\n", s.size());
}

uint32_t rng(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

}  // namespace

int main() {
  std::printf("== map basics\n");
  std::map<int, int> m;
  for (int i = 0; i < 10; i++) m[(i * 7) % 10] = i * i;
  dumpMap("operator[]", m);
  auto [it1, ok1] = m.insert({3, 333});
  auto [it2, ok2] = m.insert({30, 333});
  std::printf("insert existing ok=%d value=%d; insert new ok=%d value=%d\n", ok1, it1->second,
              ok2, it2->second);
  auto [it3, ok3] = m.try_emplace(4, 444);
  auto [it4, ok4] = m.insert_or_assign(4, 4444);
  std::printf("try_emplace existing ok=%d value=%d; insert_or_assign ok=%d value=%d\n", ok3,
              it3->second, ok4, it4->second);
  m.emplace(11, 121);
  m.emplace_hint(m.end(), 12, 144);
  dumpMap("after inserts", m);
  std::printf("at(5)=%d count(5)=%zu count(50)=%zu contains(30)=%d\n", m.at(5), m.count(5),
              m.count(50), m.contains(30));
  std::printf("erase(30)=%zu erase(31)=%zu\n", m.erase(30), m.erase(31));
  m.erase(m.find(0));
  m.erase(m.find(6), m.find(9));
  dumpMap("after erases", m);
  auto lb = m.lower_bound(5), ub = m.upper_bound(5);
  std::printf("lower_bound(5)=%d upper_bound(5)=%d lower_bound(6)=%d\n", lb->first, ub->first,
              m.lower_bound(6)->first);
  auto er = m.equal_range(10);
  std::printf("equal_range(10) empty=%d at=%d\n", er.first == er.second, er.first->first);
  std::printf("reverse:");
  for (auto r = m.rbegin(); r != m.rend(); ++r) std::printf(" %d", r->first);
  std::printf("\nfirst=%d last=%d\n", m.begin()->first, std::prev(m.end())->first);

  std::printf("== red-black churn\n");
  {
    std::map<uint32_t, uint32_t> big;
    std::set<uint32_t> mirror;
    uint32_t seed = 12345;
    for (int round = 0; round < 6; round++) {
      for (int i = 0; i < 300; i++) {
        uint32_t k = rng(seed) % 500;
        big[k] += i;
        mirror.insert(k);
      }
      for (int i = 0; i < 200; i++) {
        uint32_t k = rng(seed) % 500;
        big.erase(k);
        mirror.erase(k);
      }
      uint64_t ksum = 0, vsum = 0;
      uint32_t prev = 0;
      bool ordered = true, first = true;
      for (auto &[k, v] : big) {
        if (!first && k <= prev) ordered = false;
        prev = k;
        first = false;
        ksum += k;
        vsum += v;
      }
      bool same = big.size() == mirror.size() &&
                  std::equal(mirror.begin(), mirror.end(), big.begin(),
                             [](uint32_t a, const auto &kv) { return a == kv.first; });
      std::printf("round %d: size=%zu ksum=%llu vsum=%llu ordered=%d mirror=%d min=%u max=%u\n",
                  round, big.size(), (unsigned long long)ksum, (unsigned long long)vsum,
                  ordered, same, big.begin()->first, big.rbegin()->first);
    }
  }

  std::printf("== multimap\n");
  std::multimap<std::string, int> mm;
  const char *names[] = {"bob", "amy", "bob", "cat", "amy", "bob", "dan"};
  for (int i = 0; i < 7; i++) mm.insert({names[i], i * 10});
  for (auto &[k, v] : mm) std::printf("%s=%d ", k.c_str(), v);
  std::printf("\n");
  for (const char *n : {"amy", "bob", "cat", "eve"}) {
    auto [lo, hi] = mm.equal_range(n);
    std::printf("%s: count=%zu values:", n, mm.count(n));
    for (auto i = lo; i != hi; ++i) std::printf(" %d", i->second);
    std::printf("\n");
  }
  mm.erase("bob");
  std::printf("after erase bob size=%zu\n", mm.size());

  std::printf("== sets\n");
  std::set<int> s1{5, 3, 9, 1, 7};
  std::set<int, std::greater<int>> s2(s1.begin(), s1.end());
  dumpSet("ascending", s1);
  dumpSet("descending", s2);
  auto [sit, sok] = s1.insert(3);
  std::printf("insert dup ok=%d at=%d\n", sok, *sit);
  s1.insert({2, 4, 6, 8});
  dumpSet("more", s1);
  std::printf("lower_bound(6)=%d upper_bound(6)=%d\n", *s1.lower_bound(6), *s1.upper_bound(6));
  std::multiset<int> ms{4, 1, 4, 2, 4, 1};
  dumpSet("multiset", ms);
  std::printf("count(4)=%zu\n", ms.count(4));
  ms.erase(ms.find(4));
  dumpSet("erase one 4", ms);
  ms.erase(1);
  dumpSet("erase all 1", ms);

  std::printf("== heterogeneous lookup\n");
  std::set<std::string, std::less<>> words{"apple", "banana", "cherry"};
  const char *probe = "banana";
  std::printf("find(const char*)=%d find(string_view)=%d\n", words.find(probe) != words.end(),
              words.find(std::string_view("cherry")) != words.end());
  std::map<std::string, int, std::less<>> prices{{"tea", 3}, {"coffee", 4}};
  std::printf("contains(\"tea\")=%d\n", prices.contains("tea"));

  std::printf("== extract and merge\n");
  std::map<int, std::string> ma{{1, "one"}, {2, "two"}, {3, "three"}};
  std::map<int, std::string> mb{{3, "THREE"}, {4, "FOUR"}};
  auto node = ma.extract(2);
  node.key() = 20;
  ma.insert(std::move(node));
  ma.merge(mb);
  for (auto &[k, v] : ma) std::printf("%d=%s ", k, v.c_str());
  std::printf("| left in mb:");
  for (auto &[k, v] : mb) std::printf(" %d=%s", k, v.c_str());
  std::printf("\n");
  std::set<int> sa{1, 3, 5}, sb{2, 3, 4};
  sa.merge(sb);
  dumpSet("merged set", sa);
  dumpSet("left in sb", sb);

  std::printf("== struct keys\n");
  std::map<Key, double, KeyLess> staff;
  staff[{"eng", 3}] = 1.5;
  staff[{"eng", 10}] = 2.5;
  staff[{"art", 7}] = 3.5;
  staff[{"eng", 1}] = 4.5;
  staff[{"art", 9}] = 5.5;
  staff[{"ops", 2}] = 6.5;
  for (auto &[k, v] : staff) std::printf("%s/%d=%.1f ", k.dept.c_str(), k.id, v);
  std::printf("\n");

  std::printf("== word count\n");
  const char *text =
      "it was the best of times it was the worst of times it was the age of wisdom "
      "it was the age of foolishness";
  std::map<std::string, int> wc;
  std::string w;
  for (const char *p = text;; ++p) {
    if (*p == ' ' || !*p) {
      if (!w.empty()) ++wc[w];
      w.clear();
      if (!*p) break;
    } else {
      w += *p;
    }
  }
  for (auto &[k, v] : wc) std::printf("%s:%d ", k.c_str(), v);
  std::printf("\n");
  std::multimap<int, std::string, std::greater<int>> byCount;
  for (auto &[k, v] : wc) byCount.emplace(v, k);
  int shown = 0;
  for (auto &[c, k] : byCount) {
    if (shown++ == 5) break;
    std::printf("top: %s=%d\n", k.c_str(), c);
  }

  std::printf("== interval index\n");
  std::map<int, std::string> starts{{0, "low"}, {10, "mid"}, {50, "high"}, {100, "top"}};
  for (int q : {-5, 0, 5, 10, 49, 50, 99, 100, 1000}) {
    auto ub2 = starts.upper_bound(q);
    if (ub2 == starts.begin())
      std::printf("%d -> below range\n", q);
    else
      std::printf("%d -> %s\n", q, std::prev(ub2)->second.c_str());
  }
  return 0;
}
