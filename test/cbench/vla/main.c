// Variable-length arrays: edit distance between pairs of strings of run-time
// length, once with the full table declared as `int (*d)[m + 1]` and allocated
// with malloc so the alignment can be traced back, and once with the two rows
// it needs as a stack array `int rows[2][m + 1]`.
//
// Indexing the table is an i32.mul by the run-time row length rather than a
// shift by a constant.  The stack VLA is carved out below __stack_pointer at a
// size rounded up to 16 bytes; `distance` makes no calls, so LLVM never writes
// the global back and simply addresses the space under it, choosing between
// the two rows with a select.  The DP loop is a three-way minimum carried along
// the row, the shape of every sequence-alignment and diff program.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// The full table, so the alignment can be traced back: a pointer to rows of
// a run-time length.
static uint32_t align(int n, int m, const char a[n], const char b[m]) {
  int (*d)[m + 1] = malloc(sizeof(int[n + 1][m + 1]));
  for (int j = 0; j <= m; j++) d[0][j] = j;
  for (int i = 1; i <= n; i++) {
    d[i][0] = i;
    for (int j = 1; j <= m; j++) {
      int best = d[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
      int del = d[i - 1][j] + 1, ins = d[i][j - 1] + 1;
      if (del < best) best = del;
      if (ins < best) best = ins;
      d[i][j] = best;
    }
  }
  // Walk back from the corner, counting the edits of each kind.
  uint32_t subs = 0, gaps = 0;
  int i = n, j = m;
  while (i > 0 && j > 0) {
    if (d[i][j] == d[i - 1][j - 1] + (a[i - 1] != b[j - 1])) { subs += a[i - 1] != b[j - 1]; i--; j--; }
    else if (d[i][j] == d[i - 1][j] + 1) { gaps++; i--; }
    else { gaps++; j--; }
  }
  gaps += (uint32_t)(i + j);
  uint32_t r = (uint32_t)d[n][m] * 65536 + subs * 256 + gaps;
  free(d);
  return r;
}

// The distance alone, with the two rows it needs as arrays on the stack whose
// size is known only here.
static int distance(int n, int m, const char a[n], const char b[m]) {
  int rows[2][m + 1];
  for (int j = 0; j <= m; j++) rows[0][j] = j;
  for (int i = 1; i <= n; i++) {
    int *cur = rows[i & 1], *up = rows[(i - 1) & 1];
    cur[0] = i;
    for (int j = 1; j <= m; j++) {
      int best = up[j - 1] + (a[i - 1] != b[j - 1]);
      if (up[j] + 1 < best) best = up[j] + 1;
      if (cur[j - 1] + 1 < best) best = cur[j - 1] + 1;
      cur[j] = best;
    }
  }
  return rows[n & 1][m];
}

int main(void) {
  enum { PAIRS = 70, MAXLEN = 400 };
  static char pool[PAIRS + 1][MAXLEN];
  int len[PAIRS + 1];
  uint32_t x = 777;
  for (int p = 0; p <= PAIRS; p++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    len[p] = 120 + (int)(x % (MAXLEN - 120));
    for (int k = 0; k < len[p]; k++) {
      // Each string is a mutation of the one before, so neighbours are similar.
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      pool[p][k] = (p > 0 && k < len[p - 1] && (x & 7)) ? pool[p - 1][k] : (char)('a' + x % 4);
    }
  }
  uint64_t sum = 0;
  for (int r = 0; r < 14; r++)
    for (int p = 0; p < PAIRS; p++) {
      int q = (p + r) % PAIRS + 1;
      sum = sum * 31 + align(len[p], len[q], pool[p], pool[q]);
      sum += (uint64_t)distance(len[q], len[p], pool[q], pool[p]);
    }
  printf("sum = %016llx\n", (unsigned long long)sum);
  return 0;
}
