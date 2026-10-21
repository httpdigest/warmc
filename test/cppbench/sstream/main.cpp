// iostreams: format records into a std::ostringstream with manipulators, then
// parse them back with a std::istringstream and check the round trip.
//
// The heaviest corner of the C++ library to compile and to run: every << is a
// virtual call into a stream buffer, number formatting goes through the
// locale's num_put facet, and >> through num_get with its own parser.  The
// module this produces is the largest in the corpus by far, which is part of
// what the row is for.
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

int main() {
  uint32_t x = 2718281u;
  uint64_t check = 0;
  for (int round = 0; round < 1500; round++) {
    std::ostringstream out;
    for (int i = 0; i < 40; i++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      out << "rec " << std::setw(6) << (x % 1000000) << ' ' << std::hex << (x >> 12) << std::dec
          << ' ' << std::fixed << std::setprecision(3) << (x % 100000) / 7.0 << '\n';
    }
    std::istringstream in(out.str());
    std::string tag;
    uint32_t a, b;
    double d;
    while (in >> tag >> a >> std::hex >> b >> std::dec >> d) {
      check = check * 1000003 + a + b + static_cast<uint64_t>(d * 1000.0 + 0.5) + tag.size();
    }
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
