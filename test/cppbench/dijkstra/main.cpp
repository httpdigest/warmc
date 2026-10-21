// Dijkstra's shortest paths with std::priority_queue on a grid graph with
// random edge weights, from a few different sources.
//
// A binary heap in a vector, a pair compared lexicographically with
// std::greater, and an adjacency list of vectors: the standard library's
// containers composed the way a textbook algorithm composes them, with the
// heap's sift loops doing most of the work.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

int main() {
  constexpr uint32_t W = 160, H = 160, N = W * H;
  struct Edge {
    uint32_t to, w;
  };
  std::vector<std::vector<Edge>> adj(N);
  uint32_t x = 99991u;
  auto rnd = [&x] {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
  };
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t c = 0; c < W; c++) {
      uint32_t v = y * W + c;
      if (c + 1 < W) {
        uint32_t w = 1 + rnd() % 100;
        adj[v].push_back({v + 1, w});
        adj[v + 1].push_back({v, w});
      }
      if (y + 1 < H) {
        uint32_t w = 1 + rnd() % 100;
        adj[v].push_back({v + W, w});
        adj[v + W].push_back({v, w});
      }
    }

  uint64_t check = 0;
  std::vector<uint32_t> dist(N);
  for (int src = 0; src < 24; src++) {
    std::fill(dist.begin(), dist.end(), UINT32_MAX);
    using Item = std::pair<uint32_t, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    uint32_t s = rnd() % N;
    dist[s] = 0;
    pq.push({0, s});
    while (!pq.empty()) {
      auto [d, v] = pq.top();
      pq.pop();
      if (d != dist[v]) continue;
      for (const Edge &e : adj[v]) {
        uint32_t nd = d + e.w;
        if (nd < dist[e.to]) {
          dist[e.to] = nd;
          pq.push({nd, e.to});
        }
      }
    }
    for (uint32_t v = 0; v < N; v += 37) check = check * 1000003 + dist[v];
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
