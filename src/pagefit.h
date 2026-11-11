/* Code inside one 4 KB page (pagefit.c). */
#ifndef WARMC_PAGEFIT_H
#define WARMC_PAGEFIT_H

#include <stdint.h>

#define PAGEFIT_PAGE 4096u
/* the bytes of a function, and of a loop, that is to lie inside one page */
#define PAGEFIT_MAX 256u

/* Where a function's code starts: at `at`, or the least multiple of step
   past it from which neither a stretch of it -- nfit pairs of byte offsets
   [start, end) in fit -- nor, where it is of at most PAGEFIT_MAX bytes
   behind the `lead` ahead of its symbol, the function itself runs over a
   page's boundary; `at` again where the next page's bytes hold no such
   place. */
uint64_t pagefit_stretches(uint64_t at, uint32_t step, uint32_t lead, uint32_t len,
                           const uint32_t *fit, uint32_t nfit);

/* The same for code of another's making, arm64's: its stretches are read
   off it, each backward branch with its target at most PAGEFIT_MAX bytes
   back and inside the code, and the code itself where it is of at most
   PAGEFIT_MAX bytes; it moves by its alignment.  `at` for code aligned to a
   page or more, whose maker placed what is in it. */
uint64_t pagefit_code(const uint8_t *code, uint64_t size, uint64_t align, uint64_t at);

#endif
