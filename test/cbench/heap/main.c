// A binary heap as the priority queue of a discrete-event simulation: a few
// thousand pending events keyed by time and sequence number, every step a pop
// that sifts down and one or two pushes that sift up, with the array grown by
// realloc as the queue fills.
//
// The sift loops are index arithmetic, a two-field compare -- a 64-bit time,
// then a 32-bit tie-breaker -- and 16-byte events moved as pairs of i64 loads
// and stores; push and pop are inlined into the loop.  The branches depend on
// random keys, so the row measures how short the compare-and-move chain is
// kept.  cppbench/dijkstra is std::priority_queue under a graph search; this
// is the heap a C program writes for itself, under the other workload heaps
// have.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct { uint64_t time; uint32_t seq, who; } Event;

typedef struct { Event *a; size_t n, cap; } Heap;

static int before(const Event *x, const Event *y) {
  return x->time != y->time ? x->time < y->time : x->seq < y->seq;
}

static void push(Heap *h, Event e) {
  if (h->n == h->cap) {
    h->cap = h->cap ? h->cap * 2 : 16;
    h->a = realloc(h->a, h->cap * sizeof *h->a);
  }
  size_t i = h->n++;
  while (i > 0) {
    size_t p = (i - 1) / 2;
    if (!before(&e, &h->a[p])) break;
    h->a[i] = h->a[p];
    i = p;
  }
  h->a[i] = e;
}

static Event pop(Heap *h) {
  Event top = h->a[0], last = h->a[--h->n];
  size_t i = 0, n = h->n;
  for (;;) {
    size_t c = 2 * i + 1;
    if (c >= n) break;
    if (c + 1 < n && before(&h->a[c + 1], &h->a[c])) c++;
    if (!before(&h->a[c], &last)) break;
    h->a[i] = h->a[c];
    i = c;
  }
  if (n) h->a[i] = last;
  return top;
}

// A discrete-event simulation: stations that each schedule their next event
// at a random delay, and now and then hand work to another station, so the
// queue holds a few thousand pending events and every step is a pop and one
// or two pushes.
int main(void) {
  enum { STATIONS = 4096, STEPS = 1800000 };
  static uint32_t busy[STATIONS];
  Heap h = { 0 };
  uint64_t x = 0x2545f4914f6cdd1dull;
  uint32_t seq = 0;
  for (uint32_t s = 0; s < STATIONS; s++) {
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    push(&h, (Event){ x % 100000, seq++, s });
  }
  uint64_t sum = 0, now = 0;
  size_t peak = 0;
  for (int i = 0; i < STEPS; i++) {
    Event e = pop(&h);
    now = e.time;
    busy[e.who]++;
    sum = sum * 31 + (now ^ e.who);
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    uint64_t r = x * 0x2545f4914f6cdd1dull;
    push(&h, (Event){ now + 1 + (r >> 40) % 5000, seq++, e.who });
    if ((r & 7) == 0) push(&h, (Event){ now + (r >> 20) % 300, seq++, (uint32_t)(r >> 52) % STATIONS });
    if ((r & 7) == 1 && h.n > STATIONS) (void)pop(&h);
    if (h.n > peak) peak = h.n;
  }
  uint64_t b = 0;
  for (uint32_t s = 0; s < STATIONS; s++) b = b * 7 + busy[s];
  printf("now = %llu peak = %zu sum = %016llx busy = %016llx\n", (unsigned long long)now, peak,
         (unsigned long long)sum, (unsigned long long)b);
  free(h.a);
  return 0;
}
