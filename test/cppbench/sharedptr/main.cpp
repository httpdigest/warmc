// std::shared_ptr churn: a pool of shared nodes copied into and out of slots,
// each node holding shared links to two others and a weak link back.
//
// Reference counting the program asks for, rather than a collector: every
// copy of a shared_ptr is an increment and every overwrite a decrement that may
// run a destructor, and a weak_ptr::lock is a check of the count before an
// increment.  Built single-threaded, so libc++'s atomic counts are plain loads
// and stores, but still read-modify-writes through the control block.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

struct Node {
  uint32_t value;
  std::shared_ptr<Node> left, right;
  std::weak_ptr<Node> back;
  explicit Node(uint32_t v) : value(v) {}
};

}  // namespace

int main() {
  constexpr int kSlots = 1024;
  std::vector<std::shared_ptr<Node>> slots(kSlots);
  uint32_t x = 1234567u;
  uint64_t sum = 0;
  for (int i = 0; i < kSlots; i++) slots[i] = std::make_shared<Node>(i);
  for (int i = 0; i < 1500000; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    uint32_t a = x % kSlots, b = (x >> 10) % kSlots, c = (x >> 20) % kSlots;
    switch (x >> 30) {
      case 0:
        slots[a] = std::make_shared<Node>(x);
        break;
      case 1:
        slots[a]->left = slots[b];
        slots[a]->right = slots[c];
        break;
      case 2:
        slots[b]->back = slots[a];
        slots[c] = slots[b];
        break;
      default:
        if (auto p = slots[a]->back.lock()) sum += p->value;
        if (slots[a]->left) sum += slots[a]->left->value & 0xff;
        sum += slots[a].use_count();
        break;
    }
    // Drop the links now and then, or the left/right edges join every node
    // into one graph and nothing is ever freed.
    if ((i & 4095) == 0)
      for (auto &s : slots) s->left.reset();
  }
  std::printf("sum = %llu\n", static_cast<unsigned long long>(sum));
  return 0;
}
