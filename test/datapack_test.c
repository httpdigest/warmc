/* src/datapack.c's block compressor and rt/lzunpack.inc's decoder, on their
   own: every block the compressor writes decodes to the bytes it was given,
   into memory that starts zero, and nothing outside the block is written --
   for zeros, random bytes, data in Go's shape (two bytes in twenty), every
   period of repetition, mixtures of literals, runs of zeros and copies from
   anywhere behind, at every length to 80 and others to 300 KB.  And the
   decoder's checks: streams that claim literals or a match past the block's
   end, or a match from before its start, trap; streams with bytes changed at
   random trap or decode, and in neither case write outside the block.

   Exits 0 when all of that holds; prints what did not otherwise. */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/buf.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

enum { TRAP_MEM_OOB = 2 };

static jmp_buf g_trap;

__attribute__((noreturn)) static void warmc_trap(int t) {
  (void)t;
  longjmp(g_trap, 1);
}

static void mem_move(u8 *d, const u8 *s, u64 n) { memmove(d, s, n); }

#include "../rt/lzunpack.inc"

void datapack_lz_block(const uint8_t *in, uint64_t n, Buf *out);

/* datapack.o asks this of compile.c, which the test does not link */
uint32_t compile_want_threads(void *m, uint32_t n) {
  (void)m; (void)n;
  return 1;
}

static int g_fail;
static u64 g_rng = 0x9e3779b97f4a7c15ull;

static u64 rnd(void) {
  g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
  return g_rng;
}

static u64 below(u64 n) { return n ? rnd() % n : 0; }

#define GUARD 64

/* in decoded into fresh zero memory between guards: 1 if it came out the
   same, the stream consumed to its end and the guards untouched */
static int round_trip(const u8 *in, u64 n, const char *what) {
  Buf b = { 0 };
  datapack_lz_block(in, n, &b);
  u64 slen = b.len;
  buf_zero(&b, 16);
  u8 *mem = malloc(n + 2 * GUARD);
  memset(mem, 0xa5, GUARD);
  memset(mem + GUARD, 0, n);
  memset(mem + GUARD + n, 0xa5, GUARD);
  int ok = 1;
  if (setjmp(g_trap)) {
    printf("FAIL[datapack] %s, %llu bytes: the decoder trapped on the compressor's stream\n",
           what, (unsigned long long)n);
    ok = 0;
  } else {
    const u8 *end = lz_unpack(mem + GUARD, n, b.data);
    if ((u64)(end - b.data) != slen) {
      printf("FAIL[datapack] %s, %llu bytes: the decoder read %lld of %llu stream bytes\n",
             what, (unsigned long long)n, (long long)(end - b.data), (unsigned long long)slen);
      ok = 0;
    }
    if (memcmp(mem + GUARD, in, n)) {
      u64 at = 0;
      while (mem[GUARD + at] == in[at]) at++;
      printf("FAIL[datapack] %s, %llu bytes: byte %llu is %02x, not %02x\n", what,
             (unsigned long long)n, (unsigned long long)at, mem[GUARD + at], in[at]);
      ok = 0;
    }
  }
  for (u64 i = 0; i < GUARD; i++)
    if (mem[i] != 0xa5 || mem[GUARD + n + i] != 0xa5) {
      printf("FAIL[datapack] %s, %llu bytes: a write outside the block\n", what,
             (unsigned long long)n);
      ok = 0;
      break;
    }
  if (!ok) g_fail = 1;
  free(mem);
  buf_free(&b);
  return ok;
}

static void gen(u8 *p, u64 n, int kind) {
  switch (kind) {
  case 0: memset(p, 0, n); break;
  case 1: for (u64 i = 0; i < n; i++) p[i] = (u8)rnd(); break;
  case 2:   /* Go's: two bytes in every twenty */
    memset(p, 0, n);
    for (u64 i = 0; i < n; i += 20) {
      p[i] = (u8)rnd();
      if (i + 1 < n) p[i + 1] = (u8)(rnd() | 1);
    }
    break;
  case 3: {   /* a pattern of 1 to 24 bytes, repeated: matches that overlap */
    u64 per = 1 + below(24);
    for (u64 i = 0; i < n; i++) p[i] = i < per ? (u8)(rnd() | 1) : p[i - per];
    break;
  }
  case 4:   /* literals, runs of zeros, and copies of what came before */
    for (u64 i = 0; i < n;) {
      u64 r = below(3), len = 1 + below(r == 2 ? 300 : 40);
      if (len > n - i) len = n - i;
      if (r == 0) for (u64 k = 0; k < len; k++) p[i + k] = (u8)rnd();
      else if (r == 1 || i == 0) memset(p + i, 0, len);
      else {
        u64 off = 1 + below(i < 70000 ? i : 70000);
        for (u64 k = 0; k < len; k++) p[i + k] = p[i + k - off];
      }
      i += len;
    }
    break;
  case 5:   /* text: few symbols, many short matches */
    for (u64 i = 0; i < n; i++) p[i] = (u8)("etaoin shrdlu"[below(13)]);
    break;
  case 6:   /* zeros with a byte here and there, the runs 1 to 40 long */
    memset(p, 0, n);
    for (u64 i = below(40); i < n; i += 1 + below(40)) p[i] = (u8)(rnd() | 1);
    break;
  }
}

static const char *const KIND[] = {
  "zeros", "random", "Go's data", "a period", "a mixture", "text", "sparse" };
#define NKIND 7

/* a stream of hand-made sequences, decoded into n bytes between guards:
   whether it trapped, and the guards are checked */
static int decode_traps(const u8 *s, u64 slen, u64 n, const char *what) {
  /* a changed stream can read on past its end, never past this: each
     sequence takes at most its length bytes from the stream, and at least
     four bytes of the block, or the block's end */
  u8 *src = calloc(3 * slen + 4 * n + 128, 1);
  memcpy(src, s, slen);
  u8 *mem = malloc(n + 2 * GUARD);
  memset(mem, 0xa5, n + 2 * GUARD);
  memset(mem + GUARD, 0, n);
  int trapped = 0;
  if (setjmp(g_trap)) trapped = 1;
  else lz_unpack(mem + GUARD, n, src);
  for (u64 i = 0; i < GUARD; i++)
    if (mem[i] != 0xa5 || mem[GUARD + n + i] != 0xa5) {
      printf("FAIL[datapack] %s: a write outside the block\n", what);
      g_fail = 1;
      break;
    }
  free(mem);
  free(src);
  return trapped;
}

static void must_trap(const u8 *s, u64 slen, u64 n, const char *what) {
  if (!decode_traps(s, slen, n, what)) {
    printf("FAIL[datapack] %s: decoded, where it must trap\n", what);
    g_fail = 1;
  }
}

int main(void) {
  u64 cases = 0;
  u8 *buf = malloc(300 << 10);
  for (int k = 0; k < NKIND; k++) {
    for (u64 n = 1; n <= 80; n++) {
      for (int rep = 0; rep < 4; rep++) { gen(buf, n, k); round_trip(buf, n, KIND[k]); cases++; }
    }
    for (int rep = 0; rep < 40; rep++) {
      u64 n = 81 + below(70000);
      gen(buf, n, k); round_trip(buf, n, KIND[k]); cases++;
    }
    gen(buf, 300 << 10, k); round_trip(buf, 300 << 10, KIND[k]); cases++;
  }

  /* each check the decoder makes, by a stream that needs it */
  {
    /* 20 literals into a block of 16 */
    u8 s[64] = { 0xf0, 5 };
    must_trap(s, sizeof s, 16, "literals past the block's end");
  }
  {
    /* 4 literals, then a match of 4 + 15 + 9 = 28 bytes from 4 back, in 16 */
    u8 s[] = { 0x4f, 'a', 'b', 'c', 'd', 4, 0, 9, 0 };
    must_trap(s, sizeof s, 16, "a match past the block's end");
  }
  {
    /* the same as a run of zeros */
    u8 s[] = { 0x4f, 'a', 'b', 'c', 'd', 0, 0, 9, 0 };
    must_trap(s, sizeof s, 16, "a run of zeros past the block's end");
  }
  {
    /* 1 literal, then a match from 5 back, to the block's end: nothing else
       in the stream to trap on */
    u8 s[] = { 0x10, 'a', 5, 0, 0x00 };
    must_trap(s, sizeof s, 5, "a match from before the block's start");
  }
  {
    /* and that a stream as long as the block, exactly, decodes */
    u8 s[] = { 0x4c, 'a', 'b', 'c', 'd', 4, 0, 0x00 };
    if (decode_traps(s, sizeof s, 20, "a match to the block's end")) {
      printf("FAIL[datapack] a match that ends where the block does trapped\n");
      g_fail = 1;
    }
  }

  /* streams with bytes changed: decoded or trapped, but never outside */
  u64 corrupt = 0, ctraps = 0;
  for (int rep = 0; rep < 3000; rep++) {
    u64 n = 1 + below(600);
    gen(buf, n, (int)below(NKIND));
    Buf b = { 0 };
    datapack_lz_block(buf, n, &b);
    for (int c = 0; c < 1 + (int)below(3); c++) b.data[below(b.len)] = (u8)rnd();
    ctraps += decode_traps(b.data, b.len, n, "a stream with bytes changed");
    corrupt++;
    buf_free(&b);
  }

  free(buf);
  if (g_fail) return 1;
  printf("datapack: %llu blocks round-tripped, 5 hand-made streams, %llu changed "
         "streams (%llu trapped)\n", (unsigned long long)cases,
         (unsigned long long)corrupt, (unsigned long long)ctraps);
  return 0;
}
