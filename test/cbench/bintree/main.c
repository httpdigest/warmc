// binary-trees from the Benchmarks Game, with malloc and free: build complete
// trees of a few depths, walk them, free them, and keep one long-lived tree
// throughout.
//
// The same algorithm as ktbench/bintree, gobench/bintree and asbench/bintree,
// so the four rows compare a garbage collector, a TinyGo collector, a TLSF
// allocator and here dlmalloc on one allocation pattern: many small nodes that
// die together.
#include <stdio.h>
#include <stdlib.h>

typedef struct Node { struct Node *l, *r; } Node;

static Node *make(int d) {
  Node *n = malloc(sizeof *n);
  if (d > 0) { n->l = make(d - 1); n->r = make(d - 1); }
  else n->l = n->r = 0;
  return n;
}

static int check(const Node *n) { return 1 + (n->l ? check(n->l) + check(n->r) : 0); }

static void drop(Node *n) {
  if (n->l) { drop(n->l); drop(n->r); }
  free(n);
}

int main(void) {
  enum { MIN = 4, MAX = 15 };
  Node *keep = make(MAX);
  long total = 0;
  for (int d = MIN; d <= MAX; d += 2) {
    int iters = 1 << (MAX - d + MIN);
    for (int i = 0; i < iters; i++) {
      Node *t = make(d);
      total += check(t);
      drop(t);
    }
  }
  total += check(keep);
  drop(keep);
  printf("total = %ld\n", total);
  return 0;
}
