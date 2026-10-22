// Linear memory growth: allocations large enough that malloc has to grow
// the wasm memory (memory.grow) many times, written through and checked at
// both ends of every block; a std::vector grown one element at a time to
// several megabytes (each reallocation copies the whole buffer to newly
// grown memory); blocks freed and reallocated so that new allocations reuse
// the space in between; memmove and memcpy across megabyte distances; a
// large std::string and a large std::deque; and a final checksum over a
// 16 MB array addressed near the top of memory.
//
// warmc maps the wasm memory into a native reservation and has to keep its
// bounds handling right as the memory grows; every access here lands in
// memory that did not exist when the program started.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace {

uint64_t fnv(const unsigned char *p, std::size_t n, uint64_t h = 1469598103934665603ull) {
  for (std::size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

uint32_t xorshift(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

}  // namespace

int main() {
  std::printf("== growing blocks\n");
  std::vector<unsigned char *> blocks;
  std::vector<std::size_t> sizes;
  std::size_t total = 0;
  for (int i = 0; i < 12; i++) {
    std::size_t n = (std::size_t)(256 * 1024) << (i % 4);  // 256K .. 2M
    auto *p = static_cast<unsigned char *>(std::malloc(n));
    if (!p) {
      std::printf("malloc %zu failed\n", n);
      return 1;
    }
    std::memset(p, (int)(i * 17 + 1), n);
    p[0] = (unsigned char)i;
    p[n - 1] = (unsigned char)(255 - i);
    blocks.push_back(p);
    sizes.push_back(n);
    total += n;
    std::printf("block %2d size=%7zu first=%3u last=%3u mid=%3u total=%zu\n", i, n, p[0], p[n - 1], p[n / 2], total);
  }
  uint64_t h = 0;
  for (std::size_t i = 0; i < blocks.size(); i++) h ^= fnv(blocks[i], sizes[i]) + i;
  std::printf("blocks hash=%016llx\n", (unsigned long long)h);

  std::printf("== free and reuse\n");
  for (std::size_t i = 0; i < blocks.size(); i += 2) {
    std::free(blocks[i]);
    blocks[i] = nullptr;
  }
  for (std::size_t i = 0; i < blocks.size(); i += 2) {
    blocks[i] = static_cast<unsigned char *>(std::calloc(sizes[i], 1));
    std::size_t nonzero = 0;
    for (std::size_t k = 0; k < sizes[i]; k += 4093) nonzero += blocks[i][k] != 0;
    blocks[i][sizes[i] / 3] = 0xAB;
    std::printf("realloc'd block %2zu calloc nonzero samples=%zu marker=%02x\n", i, nonzero, blocks[i][sizes[i] / 3]);
  }
  for (std::size_t i = 1; i < blocks.size(); i += 2)
    std::printf("kept block %2zu first=%3u last=%3u\n", i, blocks[i][0], blocks[i][sizes[i] - 1]);

  std::printf("== realloc growth\n");
  {
    std::size_t n = 1000;
    auto *p = static_cast<uint32_t *>(std::malloc(n * sizeof(uint32_t)));
    for (std::size_t i = 0; i < n; i++) p[i] = (uint32_t)(i * 2654435761u);
    for (int step = 0; step < 8; step++) {
      std::size_t m = n * 3;
      p = static_cast<uint32_t *>(std::realloc(p, m * sizeof(uint32_t)));
      bool ok = true;
      for (std::size_t i = 0; i < n; i += 97) ok &= p[i] == (uint32_t)(i * 2654435761u);
      for (std::size_t i = n; i < m; i++) p[i] = (uint32_t)(i * 2654435761u);
      std::printf("realloc %8zu -> %8zu elements, prefix kept=%d last=%u\n", n, m, ok, p[m - 1]);
      n = m;
    }
    std::free(p);
  }

  std::printf("== vector growth\n");
  {
    std::vector<uint64_t> v;
    uint32_t s = 12345;
    std::size_t lastCap = 0;
    for (int i = 0; i < 1500000; i++) {
      v.push_back(xorshift(s));
      if (v.capacity() != lastCap) {
        lastCap = v.capacity();
        if (lastCap >= 65536) std::printf("capacity -> %zu (%zu bytes)\n", lastCap, lastCap * sizeof(uint64_t));
      }
    }
    uint64_t sum = 0;
    for (std::size_t i = 0; i < v.size(); i += 1) sum += v[i] ^ i;
    std::printf("size=%zu sum=%llu front=%llu back=%llu\n", v.size(), (unsigned long long)sum,
                (unsigned long long)v.front(), (unsigned long long)v.back());
  }

  std::printf("== long-distance memmove\n");
  {
    const std::size_t N = 6 * 1024 * 1024;
    auto *buf = static_cast<unsigned char *>(std::malloc(N));
    for (std::size_t i = 0; i < N; i++) buf[i] = (unsigned char)(i * 7 + (i >> 12));
    uint64_t before = fnv(buf, 1 << 20);
    std::memmove(buf + 4 * 1024 * 1024, buf, 1 << 20);
    std::memmove(buf + 1000, buf + 4 * 1024 * 1024, 1 << 20);
    std::memcpy(buf + 2 * 1024 * 1024 + 3, buf + 1000, 1 << 20);
    std::printf("hash before=%016llx after moves=%016llx %016llx %016llx\n", (unsigned long long)before,
                (unsigned long long)fnv(buf + 4 * 1024 * 1024, 1 << 20), (unsigned long long)fnv(buf + 1000, 1 << 20),
                (unsigned long long)fnv(buf + 2 * 1024 * 1024 + 3, 1 << 20));
    std::printf("whole buffer hash=%016llx\n", (unsigned long long)fnv(buf, N));
    std::free(buf);
  }

  std::printf("== big string and deque\n");
  {
    std::string s;
    for (int i = 0; i < 200000; i++) s += "abcdefghij"[i % 10];
    s.append(s);
    s.append(s);
    std::printf("string size=%zu hash=%016llx count('j')=%ld\n", s.size(),
                (unsigned long long)fnv(reinterpret_cast<const unsigned char *>(s.data()), s.size()),
                (long)std::count(s.begin(), s.end(), 'j'));
    std::deque<uint32_t> d;
    uint32_t x = 777;
    for (int i = 0; i < 600000; i++) {
      if (i % 3)
        d.push_back(xorshift(x));
      else
        d.push_front(xorshift(x));
    }
    uint64_t dh = 0;
    for (std::size_t i = 0; i < d.size(); i += 101) dh = dh * 31 + d[i];
    std::printf("deque size=%zu sampled hash=%llu front=%u back=%u\n", d.size(), (unsigned long long)dh, d.front(),
                d.back());
  }

  std::printf("== high array\n");
  {
    const std::size_t N = 4 * 1024 * 1024;
    auto *arr = static_cast<uint32_t *>(std::malloc(N * sizeof(uint32_t)));
    uint32_t s = 99;
    for (std::size_t i = 0; i < N; i++) arr[i] = xorshift(s);
    uint64_t acc = 0;
    for (std::size_t stride : {1u, 7u, 4096u, 65537u}) {
      uint64_t part = 0;
      for (std::size_t i = 0; i < N; i += stride) part += arr[i];
      std::printf("stride %6zu sum=%llu\n", stride, (unsigned long long)part);
      acc ^= part;
    }
    std::printf("last=%u acc=%llu\n", arr[N - 1], (unsigned long long)acc);
    std::free(arr);
  }
  for (auto *p : blocks) std::free(p);
  std::printf("done\n");
  return 0;
}
