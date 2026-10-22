// LZ77 compression and decompression in LZ4's block format: hash chains over a
// 32K window to find matches, a token byte holding literal and match lengths,
// and a decoder that copies each match out of the output it has already
// written -- the inner loops of every fast compressor C programs link.
//
// The compressor hashes an unaligned 4-byte read (memcpy into a uint32_t
// becomes one `i32.load align=1`), follows dependent loads down the chain, and
// compares bytes in a loop with an early exit.  The decoder is memory.copy for
// literals and for matches that do not overlap their source, and a byte loop
// for those that do, since such a match repeats the bytes it is producing.
// The input mixes words, runs of one byte and noise, so every path is taken.
// crc32 reads its buffer once; this writes one and reads it back at an offset.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

enum { WBITS = 15, WSIZE = 1 << WBITS, HBITS = 14, MINM = 4, MAXCHAIN = 24 };

static int32_t head[1 << HBITS];
static int32_t prev[WSIZE];

static uint32_t hash4(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return (v * 2654435761u) >> (32 - HBITS);
}

static uint8_t *put_len(uint8_t *o, size_t n) {
  for (; n >= 255; n -= 255) *o++ = 255;
  *o++ = (uint8_t)n;
  return o;
}

// LZ4's block format: a token of two nibbles (literal count, match length),
// the literals, then a two-byte offset.  Matches come from hash chains over a
// 32K window.
static size_t compress(const uint8_t *in, size_t n, uint8_t *out) {
  for (size_t i = 0; i < (1 << HBITS); i++) head[i] = -1;
  uint8_t *o = out;
  size_t lit = 0, i = 0;
  while (i + MINM <= n) {
    uint32_t h = hash4(in + i);
    int32_t cand = head[h];
    prev[i & (WSIZE - 1)] = cand;
    head[h] = (int32_t)i;
    size_t best = 0, boff = 0;
    for (int chain = 0; cand >= 0 && i - (size_t)cand < WSIZE && chain < MAXCHAIN; chain++) {
      const uint8_t *a = in + cand, *b = in + i;
      size_t lim = n - i, k = 0;
      while (k < lim && a[k] == b[k]) k++;
      if (k > best) { best = k; boff = i - (size_t)cand; }
      cand = prev[cand & (WSIZE - 1)];
    }
    if (best < MINM) { i++; continue; }
    size_t nl = i - lit, ml = best - MINM;
    uint8_t *tok = o++;
    *tok = (uint8_t)((nl < 15 ? nl : 15) << 4 | (ml < 15 ? ml : 15));
    if (nl >= 15) o = put_len(o, nl - 15);
    memcpy(o, in + lit, nl);
    o += nl;
    *o++ = (uint8_t)boff;
    *o++ = (uint8_t)(boff >> 8);
    if (ml >= 15) o = put_len(o, ml - 15);
    // Positions inside the match go into the chains too.
    size_t end = i + best;
    for (i++; i < end && i + MINM <= n; i++) {
      uint32_t hh = hash4(in + i);
      prev[i & (WSIZE - 1)] = head[hh];
      head[hh] = (int32_t)i;
    }
    i = end;
    lit = i;
  }
  size_t nl = n - lit;
  *o++ = (uint8_t)((nl < 15 ? nl : 15) << 4);
  if (nl >= 15) o = put_len(o, nl - 15);
  memcpy(o, in + lit, nl);
  return (size_t)(o + nl - out);
}

static size_t decompress(const uint8_t *in, size_t n, uint8_t *out) {
  const uint8_t *end = in + n;
  uint8_t *o = out;
  for (;;) {
    uint8_t tok = *in++;
    size_t nl = tok >> 4;
    if (nl == 15) { uint8_t b; do { b = *in++; nl += b; } while (b == 255); }
    memcpy(o, in, nl);
    o += nl;
    in += nl;
    if (in >= end) break;
    size_t off = (size_t)in[0] | (size_t)in[1] << 8;
    in += 2;
    size_t ml = tok & 15;
    if (ml == 15) { uint8_t b; do { b = *in++; ml += b; } while (b == 255); }
    ml += MINM;
    const uint8_t *src = o - off;
    // An overlapping match repeats the bytes it is producing, so it is copied
    // forwards one byte at a time; memmove would copy the wrong thing.
    if (off >= ml) memcpy(o, src, ml);
    else for (size_t k = 0; k < ml; k++) o[k] = src[k];
    o += ml;
  }
  return (size_t)(o - out);
}

int main(void) {
  enum { N = 1 << 18 };
  static const char *const words[] = {
    "the ", "quick ", "brown ", "fox ", "jumps ", "over ", "lazy ", "dog ",
    "compression ", "window ", "match ", "literal ", "offset ", "length ", "\n", ", ",
  };
  uint8_t *in = malloc(N), *comp = malloc(N + N / 8 + 64), *back = malloc(N);
  uint32_t x = 31337;
  size_t at = 0;
  while (at < N) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    size_t k;
    if ((x & 63) == 0) {
      // A run of one byte, which only an overlapping match can encode cheaply.
      k = 8 + (x >> 8) % 120;
      if (at + k > N) k = N - at;
      memset(in + at, 'a' + (int)((x >> 16) % 26), k);
    } else if ((x & 63) < 4) {
      // Bytes that do not repeat.
      k = 1 + (x >> 8) % 24;
      if (at + k > N) k = N - at;
      for (size_t j = 0; j < k; j++) in[at + j] = (uint8_t)((x >> (j % 24)) * 131 + j);
    } else {
      const char *w = words[(x >> 6) & 15];
      k = strlen(w);
      if (at + k > N) k = N - at;
      memcpy(in + at, w, k);
    }
    at += k;
  }
  uint64_t sum = 0;
  for (int r = 0; r < 28; r++) {
    size_t cn = compress(in, N, comp);
    size_t dn = decompress(comp, cn, back);
    if (dn != N || memcmp(in, back, N)) { printf("mismatch at round %d\n", r); return 0; }
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < cn; i++) h = (h ^ comp[i]) * 16777619u;
    sum = sum * 1000003 + cn + h;
    for (size_t i = (size_t)r; i < N; i += 4093) in[i] ^= (uint8_t)(r + 1);
  }
  printf("sum = %016llx\n", (unsigned long long)sum);
  free(in); free(comp); free(back);
  return 0;
}
