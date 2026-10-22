// C++20 ranges: the views -- iota (bounded and unbounded), filter,
// transform, take, take_while, drop, drop_while, reverse, join, split and
// lazy_split, keys/values/elements over pairs and tuples, common, counted,
// single, empty -- composed into pipelines with |, including views over
// views over views; and the constrained algorithms in std::ranges (sort
// with projections and comparators, find/find_if with projections,
// count_if, min/max/minmax, any_of/all_of, equal, copy, transform into an
// output iterator, binary search, reverse, unique, partition, fold-like
// accumulation by hand) over vectors, arrays, lists and strings.
//
// Every view is a class template wrapping the one below it, and clang
// inlines the whole stack into the loop that consumes it, so what arrives
// is loops whose control flow is the composition of the adaptors' own small
// loops.  Each pipeline's full output is printed.
#include <algorithm>
#include <array>
#include <cstdio>
#include <list>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

namespace rv = std::views;
namespace rg = std::ranges;

template <class R>
void dump(const char *label, R &&r) {
  std::printf("%-30s", label);
  for (auto &&x : r) {
    if constexpr (std::is_floating_point_v<std::decay_t<decltype(x)>>)
      std::printf(" %.2f", (double)x);
    else
      std::printf(" %lld", (long long)x);
  }
  std::printf("\n");
}

struct Person {
  std::string name;
  int age;
  double score;
};

}  // namespace

int main() {
  std::printf("== basic views\n");
  dump("iota(1,11)", rv::iota(1, 11));
  dump("iota(5)|take(4)", rv::iota(5) | rv::take(4));
  dump("filter even", rv::iota(1, 21) | rv::filter([](int x) { return x % 2 == 0; }));
  dump("transform square", rv::iota(1, 9) | rv::transform([](int x) { return x * x; }));
  dump("take_while <30", rv::iota(1) | rv::transform([](int x) { return x * x; }) |
                             rv::take_while([](int x) { return x < 30; }));
  dump("drop 7", rv::iota(0, 12) | rv::drop(7));
  dump("drop_while <5", rv::iota(0, 12) | rv::drop_while([](int x) { return x < 5; }));
  dump("reverse", rv::iota(0, 8) | rv::reverse);
  dump("single", rv::single(42));
  dump("empty", rv::empty<int>);
  std::vector<int> base{3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9};
  dump("counted(begin+2, 5)", rv::counted(base.begin() + 2, 5));
  dump("all(base)", rv::all(base));

  std::printf("== pipelines\n");
  auto pipeline = base | rv::filter([](int x) { return x > 2; }) |
                  rv::transform([](int x) { return x * 10; }) | rv::drop(2) | rv::take(6);
  dump("filter>2,*10,drop2,take6", pipeline);
  dump("same pipeline again", pipeline);
  auto primes = rv::iota(2) | rv::filter([](int n) {
                  for (int d = 2; d * d <= n; d++)
                    if (n % d == 0) return false;
                  return true;
                });
  dump("first 15 primes", primes | rv::take(15));
  dump("primes squared rev", primes | rv::take(8) | rv::transform([](int p) { return p * p; }) |
                                 rv::reverse);
  auto collatz = [](long n) {
    std::vector<long> seq{n};
    while (n != 1) {
      n = n % 2 ? 3 * n + 1 : n / 2;
      seq.push_back(n);
    }
    return seq;
  };
  auto c27 = collatz(27);
  dump("collatz(27) evens>100 take 8",
       c27 | rv::filter([](long x) { return x % 2 == 0 && x > 100; }) | rv::take(8));
  long peak = rg::max(c27);
  std::printf("collatz(27) length=%zu peak=%ld\n", c27.size(), peak);
  dump("fractions", rv::iota(1, 7) | rv::transform([](int x) { return 1.0 / x; }));

  std::printf("== join and split\n");
  std::vector<std::vector<int>> nested{{1, 2}, {}, {3}, {4, 5, 6}, {}};
  dump("join", nested | rv::join);
  std::string text = "the quick  brown fox";
  std::printf("split on ' ':");
  for (auto word : text | rv::split(' ')) {
    std::string_view w(word.begin(), word.end());
    std::printf(" [%.*s]", (int)w.size(), w.data());
  }
  std::printf("\n");
  std::string csv = "10,20,,40,5";
  std::printf("lazy_split csv:");
  int total = 0;
  for (auto field : csv | rv::lazy_split(',')) {
    std::string f;
    for (char ch : field) f += ch;
    total += f.empty() ? 0 : std::stoi(f);
    std::printf(" '%s'", f.c_str());
  }
  std::printf(" total=%d\n", total);
  std::string_view sentence = "alpha beta gamma delta";
  std::string initials;
  for (auto w : sentence | rv::split(' ')) initials += (char)((*w.begin()) - 32);
  std::printf("initials=%s\n", initials.c_str());
  std::vector<std::string> words{"ab", "cde", "", "fghi"};
  std::string joined;
  for (char ch : words | rv::join) joined += ch;
  std::printf("joined strings=%s\n", joined.c_str());

  std::printf("== keys, values, elements\n");
  std::map<std::string, int> inventory{{"apple", 5}, {"pear", 2}, {"fig", 11}, {"kiwi", 0}};
  std::printf("keys:");
  for (auto &k : inventory | rv::keys) std::printf(" %s", k.c_str());
  std::printf("\n");
  dump("values", inventory | rv::values);
  std::vector<std::tuple<int, char, double>> tup{{1, 'a', 0.5}, {2, 'b', 1.5}, {3, 'c', 2.5}};
  dump("elements<0>", tup | rv::elements<0>);
  dump("elements<2>", tup | rv::elements<2>);
  std::printf("in stock:");
  for (auto &[name, qty] : inventory | rv::filter([](auto &kv) { return kv.second > 0; }))
    std::printf(" %s=%d", name.c_str(), qty);
  std::printf("\n");

  std::printf("== common and interop\n");
  auto evens = rv::iota(0) | rv::filter([](int x) { return x % 2 == 0; }) | rv::take(5);
  auto common = evens | rv::common;
  std::vector<int> fromView(common.begin(), common.end());
  dump("vector from common view", fromView);
  std::list<int> lst{9, 8, 7, 6, 5};
  dump("list | reverse | transform", lst | rv::reverse | rv::transform([](int x) { return x - 5; }));
  std::array<int, 6> arr{1, 2, 3, 4, 5, 6};
  dump("array | drop 1 | take 3", arr | rv::drop(1) | rv::take(3));
  int cs[] = {10, 20, 30, 40};
  dump("c array | reverse", cs | rv::reverse);

  std::printf("== ranges algorithms\n");
  std::vector<Person> people{{"ann", 31, 88.5}, {"bob", 25, 92.0}, {"cid", 47, 75.25},
                             {"dee", 25, 81.0}, {"eve", 38, 99.5}, {"fay", 19, 64.0}};
  auto showPeople = [&](const char *label) {
    std::printf("%-22s", label);
    for (auto &p : people) std::printf(" %s/%d/%.1f", p.name.c_str(), p.age, p.score);
    std::printf("\n");
  };
  rg::sort(people, {}, &Person::age);
  showPeople("sort by age");
  rg::stable_sort(people, rg::greater{}, &Person::score);
  showPeople("sort by score desc");
  rg::sort(people, [](const Person &a, const Person &b) { return a.name > b.name; });
  showPeople("sort by name desc");
  auto it = rg::find(people, "dee", &Person::name);
  std::printf("find dee: age=%d\n", it != people.end() ? it->age : -1);
  auto young = rg::find_if(people, [](int a) { return a < 20; }, &Person::age);
  std::printf("find_if age<20: %s\n", young->name.c_str());
  std::printf("count_if score>80: %ld\n",
              (long)rg::count_if(people, [](double s) { return s > 80; }, &Person::score));
  auto oldest = rg::max_element(people, {}, &Person::age);
  auto [lo, hi] = rg::minmax_element(people, {}, &Person::score);
  std::printf("oldest=%s lowest=%s highest=%s\n", oldest->name.c_str(), lo->name.c_str(), hi->name.c_str());
  std::printf("all adults=%d any over 45=%d none negative=%d\n",
              rg::all_of(people, [](int a) { return a >= 18; }, &Person::age),
              rg::any_of(people, [](int a) { return a > 45; }, &Person::age),
              rg::none_of(people, [](double s) { return s < 0; }, &Person::score));
  std::vector<int> nums(12);
  rg::copy(rv::iota(0, 12) | rv::transform([](int x) { return (x * 7) % 12; }), nums.begin());
  dump("copied view", nums);
  rg::sort(nums);
  dump("sorted", nums);
  std::printf("binary_search 5=%d 13=%d lower_bound(6) idx=%ld\n", rg::binary_search(nums, 5),
              rg::binary_search(nums, 13), (long)(rg::lower_bound(nums, 6) - nums.begin()));
  rg::reverse(nums);
  dump("reversed", nums);
  std::vector<int> dups{1, 1, 2, 3, 3, 3, 4, 1, 1};
  auto tail = rg::unique(dups);
  dups.erase(tail.begin(), tail.end());
  dump("unique", dups);
  auto oneToTwelve = rv::iota(1, 13);
  std::vector<int> part(oneToTwelve.begin(), oneToTwelve.end());
  auto rest = rg::partition(part, [](int x) { return x % 3 == 0; });
  std::printf("partition: first group size=%ld\n", (long)(rest.begin() - part.begin()));
  std::vector<std::string> names;
  rg::transform(people, std::back_inserter(names), [](const Person &p) { return p.name + "!"; });
  std::printf("transformed names:");
  for (auto &n : names) std::printf(" %s", n.c_str());
  std::printf("\n");
  std::printf("equal: %d %d\n", rg::equal(rv::iota(1, 4), std::vector<int>{1, 2, 3}),
              rg::equal(rv::iota(1, 4), std::vector<int>{1, 2, 4}));
  long folded = 0;
  for (int x : rv::iota(1, 101) | rv::filter([](int x) { return x % 3 == 0 || x % 5 == 0; })) folded += x;
  std::printf("sum of multiples of 3 or 5 below 101: %ld\n", folded);
  auto [mn, mx] = rg::minmax(base);
  std::printf("minmax(base) = %d %d, min of list=%d\n", mn, mx, rg::min({7, 3, 9}));
  std::printf("distance(filter view)=%ld size(iota)=%zu\n",
              (long)rg::distance(base | rv::filter([](int x) { return x > 4; })),
              (size_t)rg::size(rv::iota(3, 10)));
  return 0;
}
