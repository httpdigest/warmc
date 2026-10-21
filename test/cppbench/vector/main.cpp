// std::vector growth and editing: rows of a jagged table pushed onto, inserted
// into the middle of, erased from and resized, and the table itself grown a
// row at a time.
//
// The container everything in C++ is built on.  push_back is a bounds test
// and a store until the capacity runs out and the whole row moves; insert and
// erase in the middle are memmove under libc++'s trivially-copyable path; a
// vector of vectors grows by moving each row's three pointers rather than its
// elements.
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
  uint32_t x = 31415926u;
  uint64_t sum = 0;
  for (int round = 0; round < 40; round++) {
    std::vector<std::vector<uint32_t>> rows;
    for (int r = 0; r < 400; r++) {
      rows.emplace_back();
      auto &row = rows.back();
      for (int i = 0; i < 200; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        row.push_back(x);
      }
    }
    for (int op = 0; op < 20000; op++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      auto &row = rows[x % rows.size()];
      switch ((x >> 24) & 3) {
        case 0:
          row.insert(row.begin() + (x >> 8) % (row.size() + 1), x);
          break;
        case 1:
          if (!row.empty()) row.erase(row.begin() + (x >> 8) % row.size());
          break;
        case 2:
          row.resize(row.size() + (x & 7), x >> 16);
          break;
        default:
          for (uint32_t v : row) sum += v & 0xffff;
          break;
      }
    }
    for (auto &row : rows) sum = sum * 3 + row.size() + (row.empty() ? 0 : row.front());
  }
  std::printf("sum = %llu\n", static_cast<unsigned long long>(sum));
  return 0;
}
