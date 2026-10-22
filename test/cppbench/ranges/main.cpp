// C++20 ranges: lazy view pipelines -- filter, transform, take_while, drop,
// reverse, join, split -- composed with | over vectors, iota and a string,
// and the constrained algorithms with projections at the end of them.
//
// Every view is a class template wrapping the one below it, and every
// iterator increment of a pipeline is a nest of the adaptors' own increments:
// filter's is a search loop for the next match, join's steps an outer and an
// inner iterator, split's is a search for the delimiter.  clang inlines the
// whole nest into the loop that consumes it, so what reaches warmc is plain
// wasm loops, but loops whose control flow is the composition of a dozen
// small ones rather than anything a person would write by hand: main alone
// is some 3,200 lines of wasm text holding 46 loops, and only ranges::sort's
// introsort is left as a call.  The row measures how much of that
// abstraction survives as branches.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace views = std::views;

int main() {
  std::vector<uint32_t> data(6000);
  std::vector<std::vector<uint32_t>> rows(64);
  std::string text;
  uint32_t x = 777767777u;
  uint64_t check = 0;
  for (int round = 0; round < 600; round++) {
    for (auto &v : data) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      v = x % 100000;
    }
    for (auto &r : rows) {
      r.resize(x % 40);
      for (auto &v : r) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        v = x & 1023;
      }
    }
    text.clear();
    for (int i = 0; i < 1500; i++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      text.append(1 + x % 7, static_cast<char>('a' + (x >> 8) % 26));
      text += (x >> 20) % 5 ? ' ' : ',';
    }

    // Filter, transform, drop and take_while over the data.
    auto odd = [](uint32_t v) { return v & 1; };
    auto sq = [](uint32_t v) { return static_cast<uint64_t>(v) * v % 1000003; };
    for (uint64_t limit : {200000ull, 900000ull}) {
      for (uint64_t v : data | views::filter(odd) | views::transform(sq) | views::drop(5) |
                            views::take_while([limit](uint64_t w) { return w < limit; }))
        check += v;
    }

    // Reverse, stride by hand with a filter on an iota index, and join.
    for (uint32_t v : data | views::reverse | views::take(4000) | views::drop(17))
      check = check * 3 + (v & 7);
    for (uint32_t i : views::iota(0u, static_cast<uint32_t>(data.size())) |
                          views::filter([](uint32_t i) { return i % 3 == 0; }) |
                          views::transform([&](uint32_t i) { return data[i] ^ i; }))
      check += i >> 4;
    for (uint32_t v : rows | views::join | views::filter([](uint32_t v) { return v > 100; }))
      check += v;

    // Split the text into words, and then words into comma-separated fields.
    uint32_t words = 0, longest = 0;
    for (auto field : text | views::split(',')) {
      for (auto word : field | views::split(' ')) {
        auto n = static_cast<uint32_t>(std::ranges::distance(word));
        if (n == 0) continue;
        words++;
        longest = std::max(longest, n);
        check += static_cast<unsigned char>(*word.begin()) * n;
      }
    }
    check += words * 1000 + longest;

    // Constrained algorithms with projections.
    struct Item {
      uint32_t key, weight;
    };
    auto made = views::iota(0u, 3000u) |
                views::transform([&](uint32_t i) { return Item{data[i], i}; });
    std::vector<Item> items(made.begin(), made.end());
    std::ranges::sort(items, {}, &Item::key);
    auto top = std::ranges::max_element(items, {}, &Item::weight);
    check += top->key + static_cast<uint64_t>(std::ranges::count_if(items, odd, &Item::key));
    auto lb = std::ranges::lower_bound(items, 50000u, {}, &Item::key);
    check += lb == items.end() ? 0 : lb->weight;
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
