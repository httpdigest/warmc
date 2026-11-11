/* Code inside one 4 KB page.

   What runs over a 4 KB boundary takes a cycle more to fetch each time it
   does: a loop of a few instructions that is one cycle a trip inside a page
   is two across a boundary (Apple M4: 0.26 ns a trip and 0.52 for a loop of
   five instructions, 0.40 and 0.52 for one of a dozen; one of sixteen or
   more, two cycles a trip anyway, loses nothing), and a small function
   called in a loop takes a cycle more a call (a leaf of 2 to 12
   instructions 3 cycles and 4, a recursion of nine instructions 0.62 ns a
   level and 0.92).  Where in a 64-byte line the code starts changes
   nothing, and a boundary of any size under 4 KB neither; a call or a
   branch from one page to another costs nothing of itself.

   So each small loop of a function lies inside one page -- one of at most
   PAGEFIT_MAX bytes, whose trips may be of a few instructions whatever its
   size: gobench/utf8scan's byte loop, 40 bytes ahead of the arms for the
   longer characters -- and so does a function of at most PAGEFIT_MAX bytes:
   where one would not, the function starts later, by the least number of
   its steps -- what its alignment leaves it -- that puts the stretch past
   the boundary, and what is left ahead of it nothing runs.

   The compiler places its own functions so (codegen.c page_fit, by the
   stretches isel.c measured, FuncOut.fit): the text section is 4096-aligned
   for that (macho.c, elf.c) and one atom in the linker, so an offset in it
   is an address but for whole pages, and the JIT's is mapped at a page's
   start.  The linker places the code of others, the runtime's, by the same
   rule (link.c place), reading the loops off the code.  arm64 alone:
   nothing is measured on x86-64. */
#include "pagefit.h"

#include <stddef.h>
#include <string.h>

static int over_page(uint64_t a, uint64_t b) {
  return b > a && a / PAGEFIT_PAGE != (b - 1) / PAGEFIT_PAGE;
}

/* the least multiple of step that takes `from` to the next boundary */
static uint64_t to_boundary(uint64_t from, uint64_t step) {
  uint64_t need = PAGEFIT_PAGE - from % PAGEFIT_PAGE;
  return (need + step - 1) / step * step;
}

uint64_t pagefit_stretches(uint64_t at, uint32_t step, uint32_t lead, uint32_t len,
                           const uint32_t *fit, uint32_t nfit) {
  int whole = len - lead <= PAGEFIT_MAX;
  if (!whole && !nfit) return at;
  uint64_t p = at;
  while (p - at < PAGEFIT_PAGE) {
    uint64_t from = UINT64_MAX;
    if (whole && over_page(p + lead, p + len)) from = p + lead;
    for (uint32_t i = 0; i < nfit && from == UINT64_MAX; i++)
      if (over_page(p + fit[2 * i], p + fit[2 * i + 1])) from = p + fit[2 * i];
    if (from == UINT64_MAX) return p;
    p += to_boundary(from, step);
  }
  return at;
}

/* how far back the word at code + o branches, in bytes, or 0: a b.cc, a
   cbz or cbnz, a tbz or tbnz, or a b, to a place before it */
static uint64_t back_branch(const uint8_t *code, uint64_t o) {
  uint32_t w;
  memcpy(&w, code + o, 4);
  int64_t d;
  if ((w & 0xff000010u) == 0x54000000u || (w & 0x7e000000u) == 0x34000000u)
    d = (int64_t)((uint64_t)((w >> 5) & 0x7ffffu) << 45) >> 45;
  else if ((w & 0x7e000000u) == 0x36000000u)
    d = (int64_t)((uint64_t)((w >> 5) & 0x3fffu) << 50) >> 50;
  else if ((w & 0xfc000000u) == 0x14000000u)
    d = (int64_t)((uint64_t)(w & 0x3ffffffu) << 38) >> 38;
  else
    return 0;
  return d < 0 ? (uint64_t)-d * 4 : 0;
}

uint64_t pagefit_code(const uint8_t *code, uint64_t size, uint64_t align, uint64_t at) {
  if (align >= PAGEFIT_PAGE || !code || (size & 3)) return at;
  uint64_t step = align < 4 ? 4 : align;
  uint64_t p = at;
  while (p - at < PAGEFIT_PAGE) {
    uint64_t from = UINT64_MAX;
    if (size <= PAGEFIT_MAX && over_page(p, p + size)) from = p;
    for (uint64_t o = 0; o + 4 <= size && from == UINT64_MAX; o += 4) {
      uint64_t d = back_branch(code, o);
      /* (a word of data that reads as such a branch costs a few bytes at
         worst; a branch out of the code is, unrelocated, one to itself) */
      if (!d || d + 4 > PAGEFIT_MAX || d > o) continue;
      if (over_page(p + o - d, p + o + 4)) from = p + o - d;
    }
    if (from == UINT64_MAX) return p;
    p += to_boundary(from, step);
  }
  return at;
}
