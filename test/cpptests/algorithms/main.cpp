// <algorithm>: sorting (sort, stable_sort, partial_sort, partial_sort_copy,
// nth_element, is_sorted) with default and custom comparators, binary
// search (lower_bound, upper_bound, equal_range, binary_search), the
// modifying algorithms (unique, remove_if, replace_if, rotate, reverse,
// shuffle with a seeded engine, fill, generate, transform, copy_if,
// partition, stable_partition), permutations (next_permutation and
// prev_permutation through every ordering of a small range), merges and
// set operations on sorted ranges, searching (find, find_if, search,
// adjacent_find, mismatch, equal, includes, count_if, all_of/any_of/
// none_of), min/max/minmax/clamp, and lexicographical_compare.
//
// libc++'s std::sort is introsort with insertion sort below a threshold and
// a branchless partition for arithmetic types; inputs here are sized and
// shaped to reach those paths (sorted, reversed, all-equal, sawtooth).  The
// stable algorithms must keep equal keys in input order, which the output
// shows by tagging each element with its original position.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

void dump(const char *label, const std::vector<int> &v) {
  std::printf("%-22s", label);
  for (int x : v) std::printf(" %d", x);
  std::printf("\n");
}

struct Rec {
  int key;
  int pos;
  std::string name;
};

uint64_t fingerprint(const std::vector<int> &v) {
  uint64_t h = 1469598103934665603ull;
  for (int x : v) h = (h ^ (uint32_t)x) * 1099511628211ull;
  return h;
}

}  // namespace

int main() {
  std::printf("== sort shapes\n");
  const int N = 300;
  std::vector<std::vector<int>> shapes(6);
  std::mt19937 gen(2024);
  for (int i = 0; i < N; i++) {
    shapes[0].push_back(i);                       // sorted
    shapes[1].push_back(N - i);                   // reversed
    shapes[2].push_back(7);                       // all equal
    shapes[3].push_back(i % 17);                  // sawtooth
    shapes[4].push_back((int)(gen() % 1000) - 500);  // random
    shapes[5].push_back(i % 2 ? i : -i);          // alternating
  }
  const char *names[] = {"sorted", "reversed", "equal", "sawtooth", "random", "alternating"};
  for (int s = 0; s < 6; s++) {
    auto v = shapes[s];
    std::sort(v.begin(), v.end());
    auto d = shapes[s];
    std::sort(d.begin(), d.end(), std::greater<>());
    std::printf("%-12s sorted=%d desc-sorted=%d min=%d max=%d median=%d fp=%016llx\n", names[s],
                std::is_sorted(v.begin(), v.end()),
                std::is_sorted(d.begin(), d.end(), std::greater<>()), v.front(), v.back(),
                v[N / 2], (unsigned long long)fingerprint(v));
  }

  std::printf("== custom comparators\n");
  std::vector<int> nums{-7, 3, -2, 8, 0, -9, 5, 1, -4, 6};
  auto byAbs = nums;
  std::sort(byAbs.begin(), byAbs.end(), [](int a, int b) { return std::abs(a) < std::abs(b); });
  dump("by abs", byAbs);
  auto evenFirst = nums;
  std::sort(evenFirst.begin(), evenFirst.end(), [](int a, int b) {
    bool ea = a % 2 == 0, eb = b % 2 == 0;
    return ea != eb ? ea : a < b;
  });
  dump("evens first", evenFirst);
  std::vector<std::string> words{"Banana", "apple", "Cherry", "date", "Elder", "fig", "apple"};
  std::sort(words.begin(), words.end(), [](const std::string &a, const std::string &b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
      return std::tolower((unsigned char)x) < std::tolower((unsigned char)y);
    });
  });
  std::printf("case-insensitive:");
  for (auto &w : words) std::printf(" %s", w.c_str());
  std::printf("\n");

  std::printf("== stability\n");
  std::vector<Rec> recs;
  const char *nm[] = {"ann", "bob", "cid", "dee", "eve", "fay", "gus", "hal", "ida", "jon", "kim", "lee"};
  for (int i = 0; i < 12; i++) recs.push_back({(i * 5) % 4, i, nm[i]});
  auto st = recs;
  std::stable_sort(st.begin(), st.end(), [](const Rec &a, const Rec &b) { return a.key < b.key; });
  std::printf("stable_sort:");
  for (auto &r : st) std::printf(" %d/%s", r.key, r.name.c_str());
  std::printf("\n");
  auto sp = recs;
  std::stable_partition(sp.begin(), sp.end(), [](const Rec &r) { return r.key >= 2; });
  std::printf("stable_partition:");
  for (auto &r : sp) std::printf(" %d/%s", r.key, r.name.c_str());
  std::printf("\n");

  std::printf("== partial orderings\n");
  std::vector<int> base{15, 3, 9, 1, 12, 7, 20, 4, 18, 6, 11, 2};
  auto ps = base;
  std::partial_sort(ps.begin(), ps.begin() + 4, ps.end());
  std::printf("partial_sort 4 smallest: %d %d %d %d\n", ps[0], ps[1], ps[2], ps[3]);
  std::vector<int> top3(3);
  std::partial_sort_copy(base.begin(), base.end(), top3.begin(), top3.end(), std::greater<>());
  dump("partial_sort_copy top3", top3);
  for (int k : {0, 3, 6, 11}) {
    auto ne = base;
    std::nth_element(ne.begin(), ne.begin() + k, ne.end());
    bool okLeft = std::all_of(ne.begin(), ne.begin() + k, [&](int x) { return x <= ne[k]; });
    bool okRight = std::all_of(ne.begin() + k, ne.end(), [&](int x) { return x >= ne[k]; });
    std::printf("nth_element k=%2d -> %d partitioned=%d\n", k, ne[k], okLeft && okRight);
  }
  std::printf("is_sorted_until=%ld\n",
              (long)(std::is_sorted_until(base.begin(), base.end()) - base.begin()));

  std::printf("== binary search\n");
  std::vector<int> sorted{1, 3, 3, 3, 5, 8, 8, 13, 21, 34};
  for (int q : {0, 1, 3, 4, 8, 34, 35}) {
    auto lo = std::lower_bound(sorted.begin(), sorted.end(), q);
    auto hi = std::upper_bound(sorted.begin(), sorted.end(), q);
    auto [a, b] = std::equal_range(sorted.begin(), sorted.end(), q);
    std::printf("q=%2d lower=%ld upper=%ld range=[%ld,%ld) found=%d\n", q,
                (long)(lo - sorted.begin()), (long)(hi - sorted.begin()),
                (long)(a - sorted.begin()), (long)(b - sorted.begin()),
                std::binary_search(sorted.begin(), sorted.end(), q));
  }

  std::printf("== modifying\n");
  std::vector<int> m{1, 1, 2, 2, 2, 3, 1, 1, 4, 5, 5, 5, 5, 6};
  auto u = m;
  u.erase(std::unique(u.begin(), u.end()), u.end());
  dump("unique", u);
  auto r = m;
  r.erase(std::remove_if(r.begin(), r.end(), [](int x) { return x == 1 || x == 5; }), r.end());
  dump("remove_if 1,5", r);
  auto rp = m;
  std::replace_if(rp.begin(), rp.end(), [](int x) { return x % 2 == 0; }, 0);
  dump("replace_if even->0", rp);
  std::vector<int> rot(10);
  std::iota(rot.begin(), rot.end(), 0);
  std::rotate(rot.begin(), rot.begin() + 3, rot.end());
  dump("rotate left 3", rot);
  std::rotate(rot.rbegin(), rot.rbegin() + 1, rot.rend());
  dump("rotate right 1", rot);
  std::reverse(rot.begin() + 2, rot.end() - 2);
  dump("reverse middle", rot);
  std::vector<int> sh(16);
  std::iota(sh.begin(), sh.end(), 0);
  std::shuffle(sh.begin(), sh.end(), std::mt19937(12345));
  dump("shuffle seed 12345", sh);
  std::vector<int> gv(8);
  int n = 1;
  std::generate(gv.begin(), gv.end(), [&n] { return n *= 3; });
  dump("generate powers of 3", gv);
  std::fill_n(gv.begin() + 2, 3, -1);
  dump("fill_n", gv);
  std::vector<int> tr;
  std::transform(sorted.begin(), sorted.end(), std::back_inserter(tr), [](int x) { return x * x - 1; });
  dump("transform x*x-1", tr);
  std::vector<int> sum2(5);
  std::transform(sorted.begin(), sorted.begin() + 5, sorted.rbegin(), sum2.begin(), std::plus<>());
  dump("binary transform", sum2);
  std::vector<int> odd;
  std::copy_if(m.begin(), m.end(), std::back_inserter(odd), [](int x) { return x & 1; });
  dump("copy_if odd", odd);
  auto pa = base;
  auto mid = std::partition(pa.begin(), pa.end(), [](int x) { return x < 10; });
  std::printf("partition <10 count=%ld is_partitioned=%d point=%ld\n", (long)(mid - pa.begin()),
              std::is_partitioned(pa.begin(), pa.end(), [](int x) { return x < 10; }),
              (long)(std::partition_point(pa.begin(), pa.end(), [](int x) { return x < 10; }) -
                     pa.begin()));

  std::printf("== permutations\n");
  std::vector<int> perm{1, 2, 3, 4};
  int count = 0;
  do {
    if (count < 8 || count >= 20) {
      std::printf("#%2d:", count);
      for (int x : perm) std::printf(" %d", x);
      std::printf("\n");
    }
    ++count;
  } while (std::next_permutation(perm.begin(), perm.end()));
  std::printf("count=%d back to start: %d %d %d %d\n", count, perm[0], perm[1], perm[2], perm[3]);
  std::string ps2 = "aabc";
  int pc = 0;
  std::string all;
  do {
    all += ps2 + " ";
    ++pc;
  } while (std::next_permutation(ps2.begin(), ps2.end()));
  std::printf("multiset perms=%d: %s\n", pc, all.c_str());
  std::string pv = "dcba";
  std::prev_permutation(pv.begin(), pv.end());
  std::printf("prev_permutation(dcba)=%s\n", pv.c_str());
  std::printf("is_permutation=%d\n",
              std::is_permutation(base.begin(), base.end(), ps.begin()));

  std::printf("== merges and set operations\n");
  std::vector<int> A{1, 2, 4, 4, 6, 8, 10}, B{2, 3, 4, 7, 8, 8, 11};
  std::vector<int> out;
  std::merge(A.begin(), A.end(), B.begin(), B.end(), std::back_inserter(out));
  dump("merge", out);
  out.clear();
  std::set_union(A.begin(), A.end(), B.begin(), B.end(), std::back_inserter(out));
  dump("set_union", out);
  out.clear();
  std::set_intersection(A.begin(), A.end(), B.begin(), B.end(), std::back_inserter(out));
  dump("set_intersection", out);
  out.clear();
  std::set_difference(A.begin(), A.end(), B.begin(), B.end(), std::back_inserter(out));
  dump("set_difference", out);
  out.clear();
  std::set_symmetric_difference(A.begin(), A.end(), B.begin(), B.end(), std::back_inserter(out));
  dump("symmetric_difference", out);
  std::vector<int> im{5, 1, 9, 2, 6, 8};
  std::sort(im.begin(), im.begin() + 3);
  std::sort(im.begin() + 3, im.end());
  std::inplace_merge(im.begin(), im.begin() + 3, im.end());
  dump("inplace_merge", im);
  const int sub46[] = {4, 6};
  std::printf("includes A{4,6}=%d includes A{3}=%d\n",
              std::includes(A.begin(), A.end(), std::begin(sub46), std::end(sub46)),
              std::includes(A.begin(), A.end(), B.begin() + 1, B.begin() + 2));

  std::printf("== searching\n");
  std::vector<int> hay{4, 7, 1, 7, 7, 3, 9, 1, 7, 3, 9, 2};
  std::vector<int> needle{7, 3, 9};
  auto f1 = std::find(hay.begin(), hay.end(), 7);
  auto f2 = std::find_if(hay.begin(), hay.end(), [](int x) { return x > 8; });
  auto f3 = std::search(hay.begin(), hay.end(), needle.begin(), needle.end());
  auto f4 = std::find_end(hay.begin(), hay.end(), needle.begin(), needle.end());
  auto f5 = std::adjacent_find(hay.begin(), hay.end());
  auto f6 = std::search_n(hay.begin(), hay.end(), 2, 7);
  auto f7 = std::find_first_of(hay.begin(), hay.end(), needle.rbegin(), needle.rend());
  std::printf("find=%ld find_if=%ld search=%ld find_end=%ld adjacent=%ld search_n=%ld first_of=%ld\n",
              (long)(f1 - hay.begin()), (long)(f2 - hay.begin()), (long)(f3 - hay.begin()),
              (long)(f4 - hay.begin()), (long)(f5 - hay.begin()), (long)(f6 - hay.begin()),
              (long)(f7 - hay.begin()));
  std::printf("count(7)=%ld count_if(odd)=%ld all>0=%d any>8=%d none<0=%d\n",
              (long)std::count(hay.begin(), hay.end(), 7),
              (long)std::count_if(hay.begin(), hay.end(), [](int x) { return x & 1; }),
              std::all_of(hay.begin(), hay.end(), [](int x) { return x > 0; }),
              std::any_of(hay.begin(), hay.end(), [](int x) { return x > 8; }),
              std::none_of(hay.begin(), hay.end(), [](int x) { return x < 0; }));
  std::vector<int> hay2 = hay;
  hay2[6] = 0;
  auto [mm1, mm2] = std::mismatch(hay.begin(), hay.end(), hay2.begin());
  std::printf("mismatch at %ld (%d vs %d) equal=%d equal-prefix=%d\n", (long)(mm1 - hay.begin()),
              *mm1, *mm2, std::equal(hay.begin(), hay.end(), hay2.begin()),
              std::equal(hay.begin(), hay.begin() + 6, hay2.begin()));

  std::printf("== min, max, clamp\n");
  auto [mn, mx] = std::minmax_element(hay.begin(), hay.end());
  std::printf("min_element=%d@%ld max_element=%d@%ld\n", *mn, (long)(mn - hay.begin()), *mx,
              (long)(mx - hay.begin()));
  std::printf("min=%d max=%d minmax={%d,%d} min(list)=%d max(list)=%d\n", std::min(3, -2),
              std::max(3, -2), std::minmax(9, 4).first, std::minmax(9, 4).second,
              std::min({5, 2, 8, -1, 4}), std::max({5, 2, 8, -1, 4}));
  for (int x : {-50, -1, 0, 50, 101, 1000})
    std::printf("clamp(%d,0,100)=%d ", x, std::clamp(x, 0, 100));
  std::printf("\n");
  std::printf("max by length=%s\n",
              std::max(std::string("pear"), std::string("banana"),
                       [](auto &a, auto &b) { return a.size() < b.size(); })
                  .c_str());
  std::printf("lexicographical: %d %d %d\n",
              std::lexicographical_compare(A.begin(), A.end(), B.begin(), B.end()),
              std::lexicographical_compare(B.begin(), B.end(), A.begin(), A.end()),
              std::lexicographical_compare(A.begin(), A.begin() + 2, A.begin(), A.end()));
  return 0;
}
