/* src/pagefit.c by itself: where a function's code and another's start,
   for stretches and code made to measure.  test/run_pagefit.py has what the
   compiler and the linker make of a module and of the runtime with it. */
#include "../src/pagefit.h"

#include <stdio.h>
#include <string.h>

static int fails, checks;

static void want(const char *what, uint64_t got, uint64_t exp) {
  checks++;
  if (got == exp) return;
  fails++;
  printf("FAIL[pagefit-rule]: %s: %llu, not %llu\n", what, (unsigned long long)got,
         (unsigned long long)exp);
}

#define P PAGEFIT_PAGE
#define NOP 0xd503201fu

/* code of n words, all nops but the word at byte `at`, a branch d bytes
   back (forward for d < 0) of a kind: 0 b.ne, 1 cbz, 2 cbnz x, 3 tbz,
   4 tbnz of bit 63, 5 b */
static uint8_t g_code[8192];
static const uint8_t *code_with(uint32_t n, uint32_t at, int kind, int d) {
  for (uint32_t i = 0; i < n; i++) { uint32_t w = NOP; memcpy(g_code + 4 * i, &w, 4); }
  if (kind < 0) return g_code;
  int32_t off = -d / 4;
  uint32_t w;
  switch (kind) {
    case 0:  w = 0x54000001u | (((uint32_t)off & 0x7ffffu) << 5); break;
    case 1:  w = 0x34000003u | (((uint32_t)off & 0x7ffffu) << 5); break;
    case 2:  w = 0xb5000003u | (((uint32_t)off & 0x7ffffu) << 5); break;
    case 3:  w = 0x36080003u | (((uint32_t)off & 0x3fffu) << 5); break;
    case 4:  w = 0xb7f80003u | (((uint32_t)off & 0x3fffu) << 5); break;
    default: w = 0x14000000u | ((uint32_t)off & 0x3ffffffu); break;
  }
  memcpy(g_code + at, &w, 4);
  return g_code;
}

int main(void) {
  /* ---- a function by its size ------------------------------------------ */
  want("a function of the most bytes that ends where the page does stays",
       pagefit_stretches(P - PAGEFIT_MAX, 4, 0, PAGEFIT_MAX, NULL, 0), P - PAGEFIT_MAX);
  want("4 bytes on, it starts at the boundary",
       pagefit_stretches(P - PAGEFIT_MAX + 4, 4, 0, PAGEFIT_MAX, NULL, 0), P);
  want("its last instruction alone over the boundary moves it",
       pagefit_stretches(P - 4, 4, 0, 8, NULL, 0), P);
  want("one instruction is never over a boundary",
       pagefit_stretches(P - 4, 4, 0, 4, NULL, 0), P - 4);
  want("a function 4 bytes longer than the most stays over the boundary",
       pagefit_stretches(P - 100, 4, 0, PAGEFIT_MAX + 4, NULL, 0), P - 100);
  want("the bytes ahead of its symbol are not its size",
       pagefit_stretches(P - 100, 4, 20, PAGEFIT_MAX + 20, NULL, 0), P - 20);
  want("nor of what is to lie in the page: the lead may be over the boundary",
       pagefit_stretches(P - 12, 4, 20, 120, NULL, 0), P - 12);
  want("but the function behind it not",
       pagefit_stretches(P - 24, 4, 20, 120, NULL, 0), P - 20);
  want("a function with a boundary just ahead of its symbol stays",
       pagefit_stretches(P - 20, 4, 20, 120, NULL, 0), P - 20);
  want("a second page's boundary is one too",
       pagefit_stretches(3 * P - 8, 4, 0, 64, NULL, 0), 3 * P);
  want("a step of 8 is kept",
       pagefit_stretches(P - 12, 8, 0, 64, NULL, 0), P - 12 + 16);
  want("a step of 64 is kept",
       pagefit_stretches(P - 200, 64, 0, 256, NULL, 0), P - 200 + 256);

  /* ---- a stretch -------------------------------------------------------- */
  {
    uint32_t one[] = { 1000, 1100 };
    want("a stretch inside the page stays", pagefit_stretches(0, 4, 0, 6000, one, 1), 0);
    want("a stretch that ends where the page does stays",
         pagefit_stretches(P - 1100, 4, 0, 6000, one, 1), P - 1100);
    want("4 bytes on, the stretch starts at the boundary",
         pagefit_stretches(P - 1096, 4, 0, 6000, one, 1), P - 1000);
    want("a stretch with its first instruction ahead of the boundary moves",
         pagefit_stretches(P - 1004, 4, 0, 6000, one, 1), P - 1000);
    want("a stretch that starts at the boundary stays",
         pagefit_stretches(P - 1000, 4, 0, 6000, one, 1), P - 1000);
    want("by 64 bytes at a time where the step is 64",
         pagefit_stretches(P - 1040, 64, 0, 6000, one, 1), P - 1040 + 64);
    uint32_t two[] = { 100, 200, 4150, 4250 };
    /* (from 4000 the first is at 4100 and the second at 8150, 42 bytes short of 8192) */
    want("the second stretch moves it as the first does",
         pagefit_stretches(P - 96, 4, 0, 6000, two, 2), P - 96 + 44);
    /* (the first, at 4046, goes to 4098; the second is then at 8148, 44 bytes short) */
    want("and where moving for the first puts the second over a boundary, further",
         pagefit_stretches(P - 150, 4, 0, 6000, two, 2), P - 150 + 52 + 44);
    /* stretches over every place a boundary can be: no start is free of one */
    uint32_t all[2 * 20];
    for (uint32_t i = 0; i < 20; i++) { all[2 * i] = 216 * i; all[2 * i + 1] = 216 * i + 256; }
    want("a function no start of which is free stays where it was",
         pagefit_stretches(100, 4, 0, 6000, all, 20), 100);
    want("a small function is placed by its size and its stretch",
         pagefit_stretches(P - 8, 4, 0, 200, one, 0), P);
  }

  /* ---- code of another's making ---------------------------------------- */
  static const char *const kinds[] = { "b.cc", "cbz", "cbnz", "tbz", "tbnz", "b" };
  for (int k = 0; k < 6; k++) {
    char what[128];
    /* 400 words; the branch at byte 800 goes back 40 bytes: a loop at 760..804 */
    const uint8_t *c = code_with(400, 800, k, 40);
    snprintf(what, sizeof what, "a loop that ends in a %s inside the page stays", kinds[k]);
    want(what, pagefit_code(c, 1600, 4, 0), 0);
    snprintf(what, sizeof what, "a loop that ends in a %s over the boundary: its top at the boundary", kinds[k]);
    want(what, pagefit_code(c, 1600, 4, P - 780), P - 760);
    snprintf(what, sizeof what, "a loop that ends in a %s, with the branch alone over the boundary", kinds[k]);
    want(what, pagefit_code(c, 1600, 4, P - 800), P - 760);
    snprintf(what, sizeof what, "a loop that ends in a %s where the page does stays", kinds[k]);
    want(what, pagefit_code(c, 1600, 4, P - 804), P - 804);
    snprintf(what, sizeof what, "a loop that ends in a %s and starts where the page does stays", kinds[k]);
    want(what, pagefit_code(c, 1600, 4, P - 760), P - 760);
    snprintf(what, sizeof what, "a %s forward is no loop", kinds[k]);
    want(what, pagefit_code(code_with(400, 800, k, -40), 1600, 4, P - 820), P - 820);
    snprintf(what, sizeof what, "a %s to itself is no loop", kinds[k]);
    want(what, pagefit_code(code_with(400, 800, k, 0), 1600, 4, P - 800), P - 800);
    snprintf(what, sizeof what, "a loop of the most bytes that ends in a %s moves", kinds[k]);
    want(what, pagefit_code(code_with(400, 800, k, (int)PAGEFIT_MAX - 4), 1600, 4, P - 700),
         P - 800 + (PAGEFIT_MAX - 4));
    snprintf(what, sizeof what, "a loop 4 bytes longer that ends in a %s stays", kinds[k]);
    want(what, pagefit_code(code_with(400, 800, k, (int)PAGEFIT_MAX), 1600, 4, P - 700), P - 700);
    snprintf(what, sizeof what, "a %s to before the code is no loop of it", kinds[k]);
    want(what, pagefit_code(code_with(400, 32, k, 40), 1600, 4, P - 20), P - 20);
    snprintf(what, sizeof what, "a %s to the code's first instruction is one", kinds[k]);
    want(what, pagefit_code(code_with(400, 40, k, 40), 1600, 4, P - 20), P);
    snprintf(what, sizeof what, "code aligned to 16 moves by 16s for its %s", kinds[k]);
    want(what, pagefit_code(code_with(400, 800, k, 40), 1600, 16, P - 784), P - 784 + 32);
  }
  {
    const uint8_t *nops = code_with(400, 0, -1, 0);
    want("code with no loop stays", pagefit_code(nops, 1600, 4, P - 800), P - 800);
    want("code of the most bytes over a boundary starts at it",
         pagefit_code(nops, PAGEFIT_MAX, 4, P - 4), P);
    want("code of the most bytes that ends where the page does stays",
         pagefit_code(nops, PAGEFIT_MAX, 4, P - PAGEFIT_MAX), P - PAGEFIT_MAX);
    want("code 4 bytes longer stays over the boundary",
         pagefit_code(nops, PAGEFIT_MAX + 4, 4, P - 4), P - 4);
    want("small code aligned to 64 moves by 64s",
         pagefit_code(nops, 128, 64, P - 64), P);
    want("code aligned to less than an instruction moves by instructions",
         pagefit_code(nops, 64, 1, P - 4), P);
    want("from wherever it is", pagefit_code(nops, 64, 2, P - 6), P + 2);
    want("code aligned to a page is its maker's to place",
         pagefit_code(code_with(400, 800, 0, 40), 1600, P, P), P);
    want("code that is no whole instructions is left alone",
         pagefit_code(nops, 66, 4, P - 4), P - 4);
    want("no code is left alone", pagefit_code(NULL, 64, 4, P - 4), P - 4);
    /* a loop round every place a boundary can be, in code longer than a
       page: one of 252 bytes from the first instruction, and one from 4
       bytes on every 64 */
    code_with(1100, 248, 0, 248);
    for (uint32_t o = 252; o + 4 <= 4400; o += 64) {
      uint32_t w = 0x54000001u | (((uint32_t)-62 & 0x7ffffu) << 5);
      memcpy(g_code + o, &w, 4);
    }
    want("code no start of which is free stays where it was",
         pagefit_code(g_code, 4400, 4, 100), 100);
  }
  printf("pagefit-rule: %d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
