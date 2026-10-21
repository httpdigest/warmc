// std::function callbacks: a table of lambdas with captures of several sizes,
// some held in the std::function's own buffer and some on the heap, applied
// in a data-dependent order.
//
// Type erasure in its library form.  A call through std::function is a call
// through the vtable of an internal holder, so the row has the indirect-call
// shape of virtual, but the receivers are lambdas the compiler sees whole,
// and copying a std::function copies or clones its target.
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

int main() {
  std::vector<std::function<uint32_t(uint32_t)>> fs;
  uint32_t k1 = 2654435761u, k2 = 40503u;
  std::array<uint32_t, 16> big{};
  for (uint32_t i = 0; i < big.size(); i++) big[i] = i * i + 7;

  fs.emplace_back([](uint32_t v) { return v + 1; });
  fs.emplace_back([k1](uint32_t v) { return v * k1; });
  fs.emplace_back([k1, k2](uint32_t v) { return (v ^ k2) + (v >> 3) * k1; });
  fs.emplace_back([big](uint32_t v) { return v + big[v & 15]; });
  fs.emplace_back([&fs](uint32_t v) { return v ^ static_cast<uint32_t>(fs.size()); });
  fs.emplace_back([big, k1](uint32_t v) { return (v + big[(v >> 4) & 15]) * k1; });

  uint32_t v = 1;
  uint64_t sum = 0;
  for (int i = 0; i < 20000000; i++) {
    v = fs[v % fs.size()](v);
    sum += v >> 7;
    if ((i & 0xfffff) == 0) {
      std::function<uint32_t(uint32_t)> copy = fs[i & 3];
      fs[(i >> 20) % fs.size()].swap(copy);
      fs.push_back(copy);
    }
  }
  std::printf("v = %u sum = %llu n = %zu\n", v, static_cast<unsigned long long>(sum),
              fs.size());
  return 0;
}
