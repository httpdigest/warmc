// A treap whose nodes own their children through std::unique_ptr: insert and
// erase by split and merge, order-statistic queries by subtree size, and the
// whole tree torn down by its root's destructor at the end of every round.
//
// Ownership as the type system spells it.  wasi-sdk builds libc++ with its
// version-2 ABI, which marks unique_ptr trivial_abi, so one passes by value
// as a bare i32 rather than through a copy in the caller's frame: merge is
// (i32, i32) -> i32, and only split's pair comes back through memory.  The
// price moves into the callee, which owns its argument and must destroy it
// if anything below throws, so every split and merge spills its trees to the
// shadow stack and wraps its recursion in a try_table with a catch_all_ref
// cleanup.  Each move is a load, a store of the pointer and a store of null
// into the source, and the final teardown is a recursion through
// ~unique_ptr into operator delete, one call per node.  sharedptr is the
// counted form of the same idea; this is the form C++ code reaches for first.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

namespace {

struct Node;
using Tree = std::unique_ptr<Node>;

struct Node {
  uint32_t key, prio, size = 1;
  Tree left, right;
  Node(uint32_t k, uint32_t p) : key(k), prio(p) {}
};

uint32_t size(const Tree &t) { return t ? t->size : 0; }

void fix(Node *n) { n->size = 1 + size(n->left) + size(n->right); }

// Splits t into keys < key and keys >= key.
std::pair<Tree, Tree> split(Tree t, uint32_t key) {
  if (!t) return {nullptr, nullptr};
  if (t->key < key) {
    auto [l, r] = split(std::move(t->right), key);
    t->right = std::move(l);
    fix(t.get());
    return {std::move(t), std::move(r)};
  }
  auto [l, r] = split(std::move(t->left), key);
  t->left = std::move(r);
  fix(t.get());
  return {std::move(l), std::move(t)};
}

Tree merge(Tree a, Tree b) {
  if (!a) return b;
  if (!b) return a;
  if (a->prio > b->prio) {
    a->right = merge(std::move(a->right), std::move(b));
    fix(a.get());
    return a;
  }
  b->left = merge(std::move(a), std::move(b->left));
  fix(b.get());
  return b;
}

bool contains(const Node *t, uint32_t key) {
  while (t) {
    if (key == t->key) return true;
    t = key < t->key ? t->left.get() : t->right.get();
  }
  return false;
}

uint32_t kth(const Node *t, uint32_t k) {
  for (;;) {
    uint32_t ls = size(t->left);
    if (k < ls) {
      t = t->left.get();
    } else if (k == ls) {
      return t->key;
    } else {
      k -= ls + 1;
      t = t->right.get();
    }
  }
}

}  // namespace

int main() {
  uint32_t x = 1234567u;
  auto next = [&x] {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
  };
  uint64_t check = 0;
  for (int round = 0; round < 6; round++) {
    Tree root;
    for (int i = 0; i < 40000; i++) {
      uint32_t r = next();
      uint32_t key = r % 50000;
      if ((r >> 28) < 11) {
        if (!contains(root.get(), key)) {
          auto [l, rest] = split(std::move(root), key);
          root = merge(merge(std::move(l), std::make_unique<Node>(key, next())), std::move(rest));
        }
      } else {
        auto [l, rest] = split(std::move(root), key);
        auto [mid, r2] = split(std::move(rest), key + 1);
        check += size(mid);  // mid, if any, is freed at the end of this block
        root = merge(std::move(l), std::move(r2));
      }
      if ((i & 63) == 0 && root) check = check * 31 + kth(root.get(), r % size(root));
    }
    check += size(root);
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
