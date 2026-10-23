#ifndef WARMC_SORT_H
#define WARMC_SORT_H

#include <string.h>

#include "warmc.h"

/* WARMC_SORT(name, T, LESS): static void name(T *a, size_t n), a stable sort
   with the comparison LESS(const T *x, const T *y) inlined.  The library's
   qsort calls its comparator through a pointer for every comparison, and the
   register allocator's sorts of intervals and ranges were 2% of compiling Go
   for it.  Runs of 16 by insertion, then merges through one buffer.  Its
   lists of a register's ranges come nearly in order, most of them wholly,
   so an array already sorted is left as it is, and two runs in order are
   copied without a merge. */
#define WARMC_SORT(name, T, LESS)                                             \
  static void name(T *a, size_t n) {                                          \
    size_t up = 1;                                                            \
    while (up < n && !LESS((&a[up]), (&a[up - 1]))) up++;                     \
    if (up >= n) return;                                                      \
    for (size_t lo = up / 16 * 16; lo < n; lo += 16) {                        \
      size_t hi = lo + 16 < n ? lo + 16 : n;                                  \
      for (size_t i = lo + 1; i < hi; i++) {                                  \
        T x = a[i];                                                           \
        size_t j = i;                                                         \
        while (j > lo && LESS((&x), (&a[j - 1]))) { a[j] = a[j - 1]; j--; }   \
        a[j] = x;                                                             \
      }                                                                       \
    }                                                                         \
    if (n <= 16) return;                                                      \
    T *buf = xmalloc(n * sizeof(T)), *src = a, *dst = buf;                    \
    for (size_t w = 16; w < n; w *= 2) {                                      \
      for (size_t lo = 0; lo < n; lo += 2 * w) {                              \
        size_t mid = lo + w < n ? lo + w : n;                                 \
        size_t hi = lo + 2 * w < n ? lo + 2 * w : n;                          \
        size_t i = lo, j = mid, k = lo;                                       \
        if (mid == hi || !LESS((&src[mid]), (&src[mid - 1]))) {               \
          memcpy(dst + lo, src + lo, (hi - lo) * sizeof(T));                  \
          continue;                                                           \
        }                                                                     \
        while (i < mid && j < hi)                                             \
          dst[k++] = LESS((&src[j]), (&src[i])) ? src[j++] : src[i++];        \
        while (i < mid) dst[k++] = src[i++];                                  \
        while (j < hi) dst[k++] = src[j++];                                   \
      }                                                                       \
      T *t = src; src = dst; dst = t;                                         \
    }                                                                         \
    if (src != a) memcpy(a, src, n * sizeof(T));                              \
    xfree(buf);                                                               \
  }

#endif
