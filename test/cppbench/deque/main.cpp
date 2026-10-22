// std::deque used from both ends: a 0-1 breadth-first search over a grid,
// which pushes free moves on the front and costly ones on the back, and a
// sliding-window maximum kept in a monotonic deque of indices.
//
// The container no other kernel touches.  libc++'s deque is a map of
// fixed-size blocks -- 512 bytes under the version-2 ABI wasi-sdk builds it
// with, so 128 four-byte elements -- and every push, pop and index splits a
// running offset into a block and a slot before the load or store: a shift
// right by five and a mask for the block pointer's byte offset, an and with
// 127 for the slot.  A push that needs a new block is a call to
// __add_front_capacity or __add_back_capacity, which recycles a spare block
// from the other end or allocates one, and pop_front frees a block once two
// are spare.  front() and back() are the arithmetic plus two loads; the row
// is mostly those, around a grid scan whose own loads are plain.
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

int main() {
  constexpr int W = 256, H = 256, N = W * H;
  std::vector<uint8_t> wall(N);
  std::vector<uint32_t> dist(N);
  std::vector<uint32_t> series(200000);
  std::deque<uint32_t> q;
  uint32_t x = 2166136261u;
  uint64_t check = 0;
  for (int round = 0; round < 14; round++) {
    for (auto &c : wall) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      c = (x % 100) < 38;
    }
    for (uint32_t src : {0u, static_cast<uint32_t>(N - 1), static_cast<uint32_t>(N / 2 + W / 3)}) {
      // 0-1 BFS: stepping into an open cell is free, breaking a wall costs 1.
      for (auto &d : dist) d = UINT32_MAX;
      dist[src] = 0;
      q.clear();
      q.push_back(src);
      while (!q.empty()) {
        uint32_t u = q.front();
        q.pop_front();
        uint32_t ux = u % W, uy = u / W;
        uint32_t nb[4];
        int n = 0;
        if (ux > 0) nb[n++] = u - 1;
        if (ux + 1 < W) nb[n++] = u + 1;
        if (uy > 0) nb[n++] = u - W;
        if (uy + 1 < H) nb[n++] = u + W;
        for (int k = 0; k < n; k++) {
          uint32_t v = nb[k];
          uint32_t w = wall[v];
          if (dist[u] + w < dist[v]) {
            dist[v] = dist[u] + w;
            if (w) q.push_back(v); else q.push_front(v);
          }
        }
      }
      uint64_t s = 0;
      for (int i = 0; i < N; i += 7) s += dist[i];
      check = check * 1000003 + s;
    }

    // Sliding-window maximum over a random series, two window widths.
    for (auto &v : series) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      v = x >> 8;
    }
    for (uint32_t win : {37u, 3001u}) {
      q.clear();
      uint64_t s = 0;
      for (uint32_t i = 0; i < series.size(); i++) {
        while (!q.empty() && series[q.back()] <= series[i]) q.pop_back();
        q.push_back(i);
        if (q.front() + win <= i) q.pop_front();
        if (i + 1 >= win) s += series[q.front()];
      }
      check = check * 1000003 + s + q.size();
    }
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
