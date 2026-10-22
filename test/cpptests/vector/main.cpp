// std::vector in depth: construction in every form (count, count+value,
// range, initializer list, copy, move), push_back/emplace_back/pop_back,
// insert of one, many, a range and an initializer list at the front, middle
// and end; erase of one and of a range; resize up and down with and without
// a value; reserve and shrink_to_fit with the capacity libc++ chooses;
// assign; swap; comparisons; data() and pointer arithmetic; iterators and
// reverse iterators; std::erase/erase_if; vector<bool> with its proxy
// references and flip(); 2D vectors; and a vector of strings sorted and
// deduplicated.
//
// After every mutation the contents are printed in full, so an element
// shifted by the wrong amount in a memmove, or a capacity computed wrongly,
// is visible on the line where it happened.
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

namespace {

template <class T>
void dump(const char *label, const std::vector<T> &v) {
  std::printf("%-22s size=%2zu [", label, v.size());
  for (std::size_t i = 0; i < v.size(); i++) {
    if constexpr (std::is_same_v<T, std::string>)
      std::printf("%s%s", i ? " " : "", v[i].c_str());
    else if constexpr (std::is_floating_point_v<T>)
      std::printf("%s%.2f", i ? " " : "", v[i]);
    else
      std::printf("%s%lld", i ? " " : "", (long long)v[i]);
  }
  std::printf("]\n");
}

}  // namespace

int main() {
  std::printf("== construction\n");
  std::vector<int> a;
  std::vector<int> b(5);
  std::vector<int> c(4, 7);
  std::vector<int> d{9, 8, 7, 6, 5};
  int raw[] = {10, 20, 30, 40};
  std::vector<int> e(raw, raw + 4);
  std::vector<int> f(d.begin() + 1, d.end() - 1);
  std::vector<int> g(d);
  std::vector<int> h(std::move(g));
  dump("empty", a);
  dump("count", b);
  dump("count+value", c);
  dump("init list", d);
  dump("from array", e);
  dump("sub range", f);
  dump("moved-from", g);
  dump("moved-to", h);

  std::printf("== push, emplace, pop with capacity\n");
  std::vector<long> p;
  std::size_t lastCap = p.capacity();
  for (int i = 0; i < 40; i++) {
    p.push_back(i * i);
    if (p.capacity() != lastCap) {
      std::printf("size %2zu: capacity %zu -> %zu\n", p.size(), lastCap, p.capacity());
      lastCap = p.capacity();
    }
  }
  for (int i = 0; i < 35; i++) p.pop_back();
  dump("after pops", p);
  std::printf("capacity kept=%zu\n", p.capacity());
  p.shrink_to_fit();
  std::printf("after shrink_to_fit capacity=%zu\n", p.capacity());
  p.reserve(100);
  std::printf("after reserve(100) capacity=%zu\n", p.capacity());
  p.reserve(10);
  std::printf("after reserve(10) capacity=%zu\n", p.capacity());

  std::printf("== insert\n");
  std::vector<int> v{1, 2, 3, 4, 5};
  v.insert(v.begin(), 0);
  dump("insert front", v);
  v.insert(v.begin() + 3, 99);
  dump("insert middle", v);
  v.insert(v.end(), 100);
  dump("insert end", v);
  v.insert(v.begin() + 2, 3, -1);
  dump("insert 3 copies", v);
  v.insert(v.begin() + 1, e.begin(), e.end());
  dump("insert range", v);
  v.insert(v.end() - 1, {7, 7, 7});
  dump("insert init list", v);
  auto it = v.insert(v.begin() + 5, 555);
  std::printf("returned iterator index=%ld value=%d\n", (long)(it - v.begin()), *it);
  v.emplace(v.begin(), -100);
  dump("emplace front", v);

  std::printf("== erase\n");
  v.erase(v.begin());
  dump("erase front", v);
  v.erase(v.begin() + 4, v.begin() + 8);
  dump("erase range", v);
  auto next = v.erase(v.end() - 1);
  std::printf("erase last returns end: %d\n", next == v.end());
  dump("erase last", v);
  std::size_t removed = std::erase(v, 7);
  dump("std::erase 7", v);
  std::printf("removed=%zu\n", removed);
  removed = std::erase_if(v, [](int x) { return x < 0; });
  dump("erase_if negative", v);
  std::printf("removed=%zu\n", removed);
  v.erase(std::remove_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; }), v.end());
  dump("remove odd idiom", v);

  std::printf("== resize and assign\n");
  std::vector<double> r{1.5, 2.5};
  r.resize(5);
  dump("resize up", r);
  r.resize(7, 9.25);
  dump("resize up with value", r);
  r.resize(3);
  dump("resize down", r);
  r.assign(4, 0.5);
  dump("assign count", r);
  r.assign({3.0, 1.0, 4.0, 1.0, 5.0, 9.0});
  dump("assign list", r);
  std::vector<double> tail(r.begin() + 2, r.end());
  r.assign(tail.begin(), tail.end());
  dump("assign range", r);

  std::printf("== swap and compare\n");
  std::vector<int> x{1, 2, 3}, y{1, 2, 4}, z{1, 2};
  std::printf("x<y=%d y<x=%d z<x=%d x==x=%d x!=y=%d\n", x < y, y < x, z < x, x == x, x != y);
  x.swap(z);
  dump("x after swap", x);
  dump("z after swap", z);
  std::swap(x, y);
  dump("x after std::swap", x);

  std::printf("== data and iterators\n");
  std::vector<int> w(10);
  std::iota(w.begin(), w.end(), 1);
  int *ptr = w.data();
  long s = 0;
  for (int i = 0; i < 10; i += 3) s += ptr[i];
  std::printf("strided sum via data()=%ld front=%d back=%d\n", s, w.front(), w.back());
  std::printf("reverse:");
  for (auto ri = w.rbegin(); ri != w.rend(); ++ri) std::printf(" %d", *ri);
  std::printf("\n");
  for (auto i2 = w.begin(); i2 != w.end(); i2 += 2) *i2 *= -1;
  dump("negated even idx", w);
  std::printf("distance=%ld at(4)=%d [9]=%d\n", (long)std::distance(w.begin(), w.end()),
              w.at(4), w[9]);

  std::printf("== vector<bool>\n");
  std::vector<bool> bits(20);
  for (std::size_t i = 0; i < bits.size(); i++) bits[i] = (i * 7) % 3 == 1;
  auto show = [&](const char *label) {
    std::printf("%-12s ", label);
    for (bool bb : bits) std::printf("%d", bb ? 1 : 0);
    std::printf(" count=%ld\n", (long)std::count(bits.begin(), bits.end(), true));
  };
  show("initial");
  bits.flip();
  show("flipped");
  bits[0] = bits[1] = false;
  std::vector<bool>::reference ref = bits[19];
  ref.flip();
  show("edited");
  bits.push_back(true);
  bits.insert(bits.begin() + 3, 4, true);
  show("grown");
  bits.resize(70, true);
  std::printf("70 bits, count=%ld\n", (long)std::count(bits.begin(), bits.end(), true));

  std::printf("== 2D\n");
  std::vector<std::vector<int>> grid(6, std::vector<int>(7, 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 7; j++) grid[i][j] = (i == 0 || j == 0) ? 1 : grid[i - 1][j] + grid[i][j - 1];
  for (auto &row : grid) {
    for (int val : row) std::printf("%5d", val);
    std::printf("\n");
  }
  std::vector<std::vector<int>> jag;
  for (int i = 0; i < 5; i++) jag.emplace_back(i + 1, i);
  jag[2].push_back(42);
  for (auto &row : jag) dump("jagged row", row);

  std::printf("== strings\n");
  std::vector<std::string> words;
  const char *text = "the quick brown fox jumps over the lazy dog the end fox";
  std::string cur;
  for (const char *q = text;; q++) {
    if (*q == ' ' || *q == 0) {
      if (!cur.empty()) words.push_back(cur);
      cur.clear();
      if (!*q) break;
    } else {
      cur += *q;
    }
  }
  dump("words", words);
  std::sort(words.begin(), words.end());
  dump("sorted", words);
  words.erase(std::unique(words.begin(), words.end()), words.end());
  dump("unique", words);
  std::vector<std::string> big;
  for (int i = 0; i < 30; i++) big.push_back(std::string(i, (char)('a' + i % 26)));
  std::size_t total = 0;
  for (auto &str : big) total += str.size();
  big.erase(big.begin() + 5, big.begin() + 25);
  std::printf("total len=%zu remaining=%zu last=%s\n", total, big.size(), big.back().c_str());
  return 0;
}
