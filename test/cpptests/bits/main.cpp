// std::bitset and <bit>: bitsets of sizes that fit in one word, span two
// and straddle a word boundary (31, 32, 33, 64, 65, 100, 200), with set,
// reset, flip, test, count, any/none/all, shifts by every interesting
// amount, the bitwise operators, construction from strings and integers,
// to_string with custom characters, to_ulong/to_ullong; then the C++20 bit
// utilities -- popcount, countl_zero/countr_zero/countl_one/countr_one,
// bit_width, bit_ceil, bit_floor, has_single_bit, rotl/rotr with positive
// and negative counts -- for 8, 16, 32 and 64-bit unsigned types; and
// std::bit_cast between floats and integers and between structs.
//
// A sieve of Eratosthenes and a small Game of Life on bitsets give the
// shifts and masks a workout over many words.  libc++ implements multi-word
// bitset shifts with carries between words, and the <bit> functions lower
// to wasm's clz/ctz/popcnt/rotl, so both are worth checking value by value.
#include <bit>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

template <std::size_t N>
void report(const char *label, const std::bitset<N> &b) {
  std::printf("%-14s %s count=%zu any=%d none=%d all=%d\n", label, b.to_string().c_str(), b.count(),
              b.any(), b.none(), b.all());
}

template <class T>
void bitTable(const char *name, std::initializer_list<T> vals) {
  for (T v : vals) {
    std::printf("%s %20llu: pop=%2d clz=%2d ctz=%2d clo=%2d cto=%2d width=%2d single=%d ceil=%llu floor=%llu\n",
                name, (unsigned long long)v, std::popcount(v), std::countl_zero(v), std::countr_zero(v),
                std::countl_one(v), std::countr_one(v), (int)std::bit_width(v), std::has_single_bit(v),
                (unsigned long long)(v <= (T(1) << (sizeof(T) * 8 - 1)) ? std::bit_ceil(v) : 0),
                (unsigned long long)std::bit_floor(v));
  }
}

struct Pair16 {
  uint16_t lo, hi;
};

}  // namespace

int main() {
  std::printf("== basics\n");
  std::bitset<12> b;
  report("empty", b);
  b.set(0).set(3).set(11);
  report("set 0,3,11", b);
  b.flip(3).flip(4);
  report("flip 3,4", b);
  b.set();
  report("set all", b);
  b.reset(5);
  report("reset 5", b);
  b.flip();
  report("flip all", b);
  b.reset();
  report("reset all", b);
  std::bitset<12> fromStr(std::string("101100111000"));
  std::bitset<12> fromInt(0xABCu);
  report("from string", fromStr);
  report("from int", fromInt);
  report("and", fromStr & fromInt);
  report("or", fromStr | fromInt);
  report("xor", fromStr ^ fromInt);
  report("not", ~fromStr);
  std::printf("test(2)=%d [3]=%d to_ulong=%lu equal=%d custom=%s\n", fromStr.test(2), (bool)fromStr[3],
              fromStr.to_ulong(), fromStr == fromInt, fromStr.to_string('.', '#').c_str());
  std::bitset<8> partial(std::string("xx1101yy"), 2, 4);
  report("substring", partial);

  std::printf("== shifts across words\n");
  std::bitset<100> w;
  w.set(0).set(31).set(32).set(63).set(64).set(99);
  for (int sh : {0, 1, 7, 31, 32, 33, 63, 64, 65, 98, 99, 100}) {
    std::bitset<100> l = w << sh, r = w >> sh;
    std::string ls = l.to_string(), rs = r.to_string();
    std::printf("sh=%3d left count=%zu first=%zu  right count=%zu first=%zu\n", sh, l.count(),
                ls.find('1') == std::string::npos ? 999 : 99 - ls.find('1'), r.count(),
                rs.rfind('1') == std::string::npos ? 999 : 99 - rs.rfind('1'));
  }
  std::bitset<33> b33;
  b33.set(32);
  report("b33 bit32", b33);
  report("b33 >> 1", b33 >> 1);
  report("b33 << 1", b33 << 1);
  std::bitset<64> b64(0x8000000000000001ull);
  std::printf("b64 to_ullong=%llx rotated-ish=%llx\n", b64.to_ullong(),
              ((b64 << 4) | (b64 >> 60)).to_ullong());
  std::bitset<65> b65;
  b65.set(64).set(0);
  std::printf("b65 count=%zu after <<1 test(64)=%d test(1)=%d\n", b65.count(), (b65 << 1).test(64),
              (b65 << 1).test(1));

  std::printf("== sieve on bitset<200>\n");
  std::bitset<200> composite;
  composite.set(0).set(1);
  for (std::size_t i = 2; i * i < 200; i++)
    if (!composite[i])
      for (std::size_t j = i * i; j < 200; j += i) composite.set(j);
  std::bitset<200> primes = ~composite;
  std::printf("primes below 200: %zu:", primes.count());
  for (std::size_t i = 0; i < 200; i++)
    if (primes[i]) std::printf(" %zu", i);
  std::printf("\n");
  std::bitset<200> twin = primes & (primes >> 2);
  std::printf("twin prime starts:");
  for (std::size_t i = 0; i < 200; i++)
    if (twin[i]) std::printf(" %zu", i);
  std::printf("\n");

  std::printf("== life on 16x16 rows\n");
  std::bitset<16> grid[16];
  grid[1].set(2);
  grid[2].set(3);
  grid[3].set(1).set(2).set(3);  // glider
  grid[8].set(8).set(9).set(10);  // blinker
  for (int gen = 0; gen < 6; gen++) {
    std::printf("gen %d:", gen);
    for (int r = 0; r < 16; r++)
      if (grid[r].any()) std::printf(" r%d=%04lx", r, grid[r].to_ulong());
    std::printf("\n");
    std::bitset<16> next[16];
    for (int r = 0; r < 16; r++)
      for (int c = 0; c < 16; c++) {
        int n = 0;
        for (int dr = -1; dr <= 1; dr++)
          for (int dc = -1; dc <= 1; dc++) {
            if (!dr && !dc) continue;
            int rr = (r + dr + 16) % 16, cc = (c + dc + 16) % 16;
            n += grid[rr][cc];
          }
        next[r][c] = n == 3 || (n == 2 && grid[r][c]);
      }
    for (int r = 0; r < 16; r++) grid[r] = next[r];
  }

  std::printf("== <bit> functions\n");
  bitTable<uint8_t>("u8 ", {0, 1, 2, 3, 0x7F, 0x80, 0xF0, 0xFF});
  bitTable<uint16_t>("u16", {0, 1, 0x100, 0x1234, 0x8000, 0xFFFF});
  bitTable<uint32_t>("u32", {0u, 1u, 5u, 1000u, 0x80000000u, 0xFFFFFFFFu, 0x00FF0000u});
  bitTable<uint64_t>("u64", {0ull, 1ull, 0x100000000ull, 0x8000000000000000ull, ~0ull, 123456789012345ull});
  for (int s : {0, 1, 4, 7, 8, -1, -3, 9, 33}) {
    std::printf("rot %3d: rotl8=%02x rotr8=%02x rotl32=%08x rotr32=%08x rotl64=%016llx\n", s,
                std::rotl((uint8_t)0x96, s), std::rotr((uint8_t)0x96, s), std::rotl(0x12345678u, s),
                std::rotr(0x12345678u, s), (unsigned long long)std::rotl(0x0123456789ABCDEFull, s));
  }

  std::printf("== bit_cast\n");
  for (float f : {0.0f, -0.0f, 1.0f, -2.5f, 3.14159f, 1e-40f, 65504.0f}) {
    uint32_t u = std::bit_cast<uint32_t>(f);
    std::printf("float %-12g bits=%08x sign=%u exp=%3u mant=%06x back=%g\n", (double)f, u, u >> 31,
                (u >> 23) & 0xFF, u & 0x7FFFFF, (double)std::bit_cast<float>(u));
  }
  for (double d : {1.0, 0.1, -1e300, 5e-324}) {
    uint64_t u = std::bit_cast<uint64_t>(d);
    std::printf("double %-10g bits=%016llx exp=%4llu\n", d, (unsigned long long)u,
                (unsigned long long)((u >> 52) & 0x7FF));
  }
  Pair16 p = std::bit_cast<Pair16>(0xBEEFCAFEu);
  std::printf("struct from u32: lo=%04x hi=%04x back=%08x\n", p.lo, p.hi, std::bit_cast<uint32_t>(p));
  std::printf("endian little=%d\n", std::endian::native == std::endian::little);
  // fast inverse square root through bit_cast
  for (float x : {1.0f, 2.0f, 10.0f, 0.25f}) {
    float y = std::bit_cast<float>(0x5f3759dfu - (std::bit_cast<uint32_t>(x) >> 1));
    y = y * (1.5f - 0.5f * x * y * y);
    std::printf("rsqrt(%g) ~ %.6f\n", (double)x, (double)y);
  }
  return 0;
}
