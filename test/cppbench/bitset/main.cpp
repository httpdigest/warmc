// Conway's Life on a 512-by-512 board held as one std::bitset per row, the
// neighbour counts computed for a whole row at once with a bitwise adder over
// shifted copies of the rows above, beside and below.
//
// std::bitset<512> is sixteen 32-bit words on wasm32, and libc++ writes its
// operators as loops over them: & | ^ ~ word by word, << and >> carrying bits
// across word boundaries with a pair of shifts per word, count() an
// i32.popcnt per word.  Every operator returns a 64-byte bitset by value, and
// clang unrolls all of those loops and scalarises all of those temporaries,
// so one row of step() is a single loop body of some 640 i32 and, or, xor and
// shift instructions over 63 locals, with only the 48 loads of the three
// input rows and the 16 stores of the result touching memory.  Bit-parallel
// code written against a library type rather than against integers, which
// arrives as one long block whose every value lives in a local.
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr int W = 512, H = 512;
using Row = std::bitset<W>;

// Two-bit sum s1:s0 of three one-bit inputs.
inline void add3(const Row &a, const Row &b, const Row &c, Row &s0, Row &s1) {
  Row t = a ^ b;
  s0 = t ^ c;
  s1 = (a & b) | (t & c);
}

void step(const std::vector<Row> &cur, std::vector<Row> &next) {
  const Row empty;
  for (int y = 0; y < H; y++) {
    const Row &up = y > 0 ? cur[y - 1] : empty;
    const Row &mid = cur[y];
    const Row &dn = y + 1 < H ? cur[y + 1] : empty;
    // Column sums of the three rows, then those sums shifted left and right.
    Row c0, c1;
    add3(up, mid, dn, c0, c1);
    Row l0 = c0 << 1, l1 = c1 << 1, r0 = c0 >> 1, r1 = c1 >> 1;
    // Total over the 3x3 block, as bits of weight 1, 2 and 4 (and 8, folded
    // into "four or more" since it never matters which).
    Row s0, k1;
    add3(l0, c0, r0, s0, k1);
    Row a1, a2;
    add3(l1, c1, r1, a1, a2);
    Row s1 = k1 ^ a1;
    Row s2 = (k1 & a1) | a2;
    // The block sum includes the cell itself: alive with 2 or 3 neighbours is
    // a block sum of 3 or 4, and a birth is a block sum of exactly 3.
    Row three = s0 & s1 & ~s2;
    Row four = ~s0 & ~s1 & s2 & ~(k1 & a1 & a2);
    next[y] = three | (mid & four);
  }
}

}  // namespace

int main() {
  std::vector<Row> a(H), b(H);
  uint32_t x = 6502u;
  for (auto &row : a) {
    for (int i = 0; i < W; i++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      row[i] = (x % 8) < 3;
    }
  }
  uint64_t check = 0;
  for (int gen = 0; gen < 2000; gen++) {
    step(a, b);
    a.swap(b);
    if (gen % 64 == 0) {
      uint64_t pop = 0;
      for (int y = 0; y < H; y++) pop += a[y].count() * (y + 1);
      check = check * 1000003 + pop;
    }
  }
  uint64_t alive = 0;
  for (const auto &row : a) alive += row.count();
  std::printf("alive = %llu check = %llu row = %d%d%d%d\n",
              static_cast<unsigned long long>(alive), static_cast<unsigned long long>(check),
              int(a[H / 2][0]), int(a[H / 2][W / 3]), int(a[7][W - 1]), int(a[H - 1][W / 2]));
  return 0;
}
