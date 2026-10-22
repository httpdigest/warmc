// The other sequence containers and views: std::deque growing and shrinking
// at both ends, indexed and inserted into the middle; std::list with splice
// (whole, one element, a range), merge, sort, unique, reverse, remove_if
// and iterator stability across those; std::forward_list with
// insert_after, erase_after, splice_after, sort, reverse and merge;
// std::array with fill, swap, comparisons, structured bindings and
// std::to_array; and std::span with static and dynamic extents, subspan,
// first/last, as_bytes and spans over each of the containers above.
//
// Each operation's result is printed whole.  The node-based containers
// relink pointers on every splice and merge, so a wrong link shows as a
// missing or repeated element rather than a crash.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <forward_list>
#include <list>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace {

template <class C>
void dump(const char *label, const C &c) {
  std::printf("%-24s [", label);
  bool first = true;
  for (const auto &x : c) {
    std::printf(first ? "%d" : " %d", (int)x);
    first = false;
  }
  std::printf("]\n");
}

int sumSpan(std::span<const int> s) { return std::accumulate(s.begin(), s.end(), 0); }

template <std::size_t N>
int sumFixed(std::span<const int, N> s) {
  int t = 0;
  for (int x : s) t += x;
  return t * (int)N;
}

void doubleAll(std::span<int> s) {
  for (int &x : s) x *= 2;
}

}  // namespace

int main() {
  std::printf("== deque\n");
  std::deque<int> dq;
  for (int i = 0; i < 10; i++) {
    if (i % 2)
      dq.push_back(i);
    else
      dq.push_front(i);
  }
  dump("alternating ends", dq);
  std::printf("front=%d back=%d [3]=%d at(7)=%d\n", dq.front(), dq.back(), dq[3], dq.at(7));
  dq.insert(dq.begin() + 5, {100, 101});
  dump("insert middle", dq);
  dq.erase(dq.begin() + 2, dq.begin() + 4);
  dump("erase range", dq);
  dq.pop_front();
  dq.pop_back();
  dump("pop both", dq);
  for (int i = 0; i < 1000; i++) dq.push_back(i);
  for (int i = 0; i < 995; i++) dq.pop_front();
  dump("after churn", dq);
  dq.resize(8, -1);
  dump("resize", dq);
  std::sort(dq.begin(), dq.end());
  dump("sorted", dq);
  std::deque<std::string> sdq;
  for (int i = 0; i < 300; i++) sdq.push_front(std::to_string(i));
  std::printf("string deque front=%s back=%s [150]=%s\n", sdq.front().c_str(),
              sdq.back().c_str(), sdq[150].c_str());

  std::printf("== list\n");
  std::list<int> l1{5, 1, 4, 1, 5, 9, 2, 6};
  std::list<int> l2{3, 5, 8, 9, 7, 9};
  auto keep = std::next(l1.begin(), 2);  // points at 4
  dump("l1", l1);
  dump("l2", l2);
  l1.sort();
  l2.sort();
  dump("l1 sorted", l1);
  dump("l2 sorted", l2);
  std::printf("iterator still at %d\n", *keep);
  l1.merge(l2);
  dump("merged", l1);
  std::printf("l2 empty=%d size=%zu\n", l2.empty(), l1.size());
  l1.unique();
  dump("unique", l1);
  l1.reverse();
  dump("reversed", l1);
  l1.remove_if([](int x) { return x % 3 == 0; });
  dump("remove multiples of 3", l1);
  std::list<int> other{-1, -2, -3, -4};
  auto pos = std::find(l1.begin(), l1.end(), 5);
  l1.splice(pos, other, std::next(other.begin()));  // move -2 before 5
  dump("splice one", l1);
  dump("other after", other);
  l1.splice(l1.begin(), other, other.begin(), std::prev(other.end()));
  dump("splice range", l1);
  l1.splice(l1.end(), other);
  dump("splice rest", l1);
  std::printf("other size=%zu, kept iterator=%d\n", other.size(), *keep);
  l1.remove(1);
  l1.push_front(77);
  l1.emplace_back(88);
  dump("final list", l1);
  std::list<std::string> ls{"pear", "apple", "fig", "apple", "kiwi"};
  ls.sort([](const std::string &a, const std::string &b) { return a.size() < b.size(); });
  std::printf("by length:");
  for (auto &s : ls) std::printf(" %s", s.c_str());
  std::printf("\n");

  std::printf("== forward_list\n");
  std::forward_list<int> fl{4, 8, 15, 16, 23, 42};
  fl.push_front(0);
  dump("initial", fl);
  auto fit = fl.begin();
  std::advance(fit, 3);
  fl.insert_after(fit, {100, 200});
  dump("insert_after", fl);
  fl.erase_after(fl.begin());
  dump("erase_after begin", fl);
  fl.reverse();
  dump("reversed", fl);
  fl.sort();
  dump("sorted", fl);
  std::forward_list<int> fl2{1, 9, 20, 300};
  fl.merge(fl2);
  dump("merged", fl);
  fl.remove_if([](int x) { return x > 100; });
  dump("remove >100", fl);
  std::forward_list<int> fl3{-5, -6};
  fl.splice_after(fl.before_begin(), fl3);
  dump("splice_after front", fl);
  std::printf("distance=%ld\n", (long)std::distance(fl.begin(), fl.end()));
  fl.unique([](int a, int b) { return a / 10 == b / 10; });
  dump("unique by decade", fl);

  std::printf("== array\n");
  std::array<int, 6> arr{3, 1, 4, 1, 5, 9};
  std::array<int, 6> arr2;
  arr2.fill(2);
  dump("arr", arr);
  dump("filled", arr2);
  arr.swap(arr2);
  dump("swapped", arr);
  std::printf("compare: %d %d %d\n", arr < arr2, arr == arr2, arr2 > arr);
  auto [p0, p1, p2] = std::array<double, 3>{1.5, 2.5, 3.5};
  std::printf("bindings: %.1f %.1f %.1f\n", p0, p1, p2);
  auto ta = std::to_array({10, 20, 30, 40});
  std::printf("to_array size=%zu back=%d get<1>=%d\n", ta.size(), ta.back(), std::get<1>(ta));
  std::array<std::array<int, 3>, 3> mat{};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) mat[i][j] = i * 3 + j;
  for (auto &row : mat) dump("matrix row", row);
  std::array<int, 0> empty{};
  std::printf("empty size=%zu empty()=%d\n", empty.size(), empty.empty());

  std::printf("== span\n");
  int carr[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  std::span<int> all(carr);
  std::span<int, 8> fixed(carr);
  std::printf("extent dyn=%zu fixed=%zu size=%zu bytes=%zu\n", all.extent == std::dynamic_extent ? (std::size_t)0 : all.extent,
              fixed.extent, all.size(), all.size_bytes());
  dump("first(3)", all.first(3));
  dump("last(2)", all.last(2));
  dump("subspan(2,4)", all.subspan(2, 4));
  dump("subspan(5)", all.subspan(5));
  std::printf("sum all=%d sum mid=%d fixed=%d\n", sumSpan(all), sumSpan(all.subspan(2, 3)),
              sumFixed(std::span<const int, 4>(carr + 2, 4)));
  doubleAll(all.subspan(4));
  dump("doubled tail", all);
  std::vector<int> vec{9, 8, 7};
  std::array<int, 3> sarr{1, 1, 1};
  std::printf("span over vector=%d over array=%d over init=%d\n", sumSpan(vec), sumSpan(sarr),
              sumSpan(std::span<const int>(vec.data() + 1, 2)));
  auto bytes = std::as_bytes(std::span<const int>(carr, 2));
  std::printf("as_bytes size=%zu:", bytes.size());
  for (auto by : bytes) std::printf(" %02x", (unsigned)by);
  std::printf("\n");
  std::span<int> sp = all;
  std::sort(sp.rbegin(), sp.rend());
  dump("sorted descending", sp);
  std::printf("front=%d back=%d [3]=%d data==carr %d\n", sp.front(), sp.back(), sp[3],
              sp.data() == carr);
  return 0;
}
