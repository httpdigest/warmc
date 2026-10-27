#include "warmc.h"

#include <stdlib.h>
#include <string.h>

/* The active data segments, as rt.c's data_init reads them at instantiation:
   a memory the module defines, whose every active segment has a constant
   offset that fits its initial size, is written as the image those segments
   make of it, compressed; every other memory's segments are a stream, in
   index order, each its own bytes.

     data_init := varint block_log2, varint nspans, span*, varint nstream,
                  entry*, 16 zero bytes for the decoder to read ahead into
     span      := varint mem, varint base, varint len, block*
     entry     := varint mem, varint kind, varint at, varint len, len bytes

   A span is a stretch of the memory, from base, that its segments cover with
   gaps of less than SPAN_GAP between them; it is cut into blocks of 1 <<
   block_log2 bytes, the last shorter, each compressed on its own -- on the
   compiler's threads, and so that a block's matches reach back only into it.
   A stream entry's kind says whether `at` is the offset (0) or the constant
   expression that works it out (1).

   A block is LZ77 in LZ4's tokens: a byte whose high four bits are the
   literal count and low four the match length less four, 15 meaning more in
   bytes that follow, 255 at a time; the literals; then, unless the block
   ends there, a two-byte offset back and the match.  Offset 0 is a run of
   zero bytes instead, which the decoder skips: the memory is new, and zero
   already.  The last sequence of a block is its literals alone, ending
   where the block does.

   Go's linker splits its data at every run of zeros: 90688 segments for a
   net/http server, two bytes long and 18 apart, whose 16-byte descriptors
   came to 1.45 MB and the bytes to 2.5 MB; as one image it is 1.1 MB.

   WARMC_PACK_BLOCK_LOG2 sets the block size, from 4 to 24, for the tests:
   small blocks put their ends where small modules have data. */

#define SPAN_GAP   64
#define BLOCK_LOG2 18
#define BLOCK_LOG2_MAX 24
#define LZ_MINM    4
#define LZ_MAXOFF  0xffffu
#define LZ_DEPTH   64

static bool g_compress_data = true;

void codegen_set_compress_data(bool on) { g_compress_data = on; }

static void put_var(Buf *b, uint64_t v) {
  while (v >= 0x80) { buf_u8(b, (uint8_t)(v | 0x80)); v >>= 7; }
  buf_u8(b, (uint8_t)v);
}

static void put_len(Buf *b, uint64_t v) {
  while (v >= 255) { buf_u8(b, 255); v -= 255; }
  buf_u8(b, (uint8_t)v);
}

static void lz_emit(Buf *b, const uint8_t *lit, uint64_t nlit, uint64_t off,
                    uint64_t ml, bool last) {
  uint64_t m = last ? 0 : ml - LZ_MINM;
  buf_u8(b, (uint8_t)((nlit < 15 ? nlit : 15) << 4 | (m < 15 ? m : 15)));
  if (nlit >= 15) put_len(b, nlit - 15);
  buf_bytes(b, lit, nlit);
  if (last) return;
  buf_u8(b, (uint8_t)off);
  buf_u8(b, (uint8_t)(off >> 8));
  if (m >= 15) put_len(b, m - 15);
}

typedef struct {
  uint32_t *head, *prev;
  uint32_t hbits;
} LzWork;

static uint32_t lz_hash(const uint8_t *p, uint32_t hbits) {
  uint32_t v;
  memcpy(&v, p, 4);
  return (v * 2654435761u) >> (32 - hbits);
}

static void lz_insert(LzWork *w, const uint8_t *in, uint64_t i, uint64_t n) {
  if (i + LZ_MINM > n) return;
  uint32_t h = lz_hash(in + i, w->hbits);
  w->prev[i] = w->head[h];
  w->head[h] = (uint32_t)i;
}

static uint64_t lz_common(const uint8_t *a, const uint8_t *b, uint64_t max) {
  uint64_t l = 0;
  while (l + 8 <= max) {
    uint64_t x, y;
    memcpy(&x, a + l, 8);
    memcpy(&y, b + l, 8);
    if (x != y) return l + (uint64_t)(__builtin_ctzll(x ^ y) >> 3);
    l += 8;
  }
  while (l < max && a[l] == b[l]) l++;
  return l;
}

/* The longest match for in[i..n) the chain holds, or 0; *off its distance */
static uint64_t lz_find(LzWork *w, const uint8_t *in, uint64_t i, uint64_t n,
                        uint64_t *off) {
  uint64_t best = 0;
  *off = 0;
  if (i + LZ_MINM > n) return 0;
  uint32_t c = w->head[lz_hash(in + i, w->hbits)];
  for (int d = 0; d < LZ_DEPTH && c != UINT32_MAX && i - c <= LZ_MAXOFF; d++) {
    uint64_t l = lz_common(in + c, in + i, n - i);
    if (l > best) { best = l; *off = i - c; }
    uint32_t nc = w->prev[c];
    if (nc == UINT32_MAX || nc >= c) break;
    c = nc;
  }
  return best >= LZ_MINM ? best : 0;
}

static uint64_t zero_run(const uint8_t *in, uint64_t i, uint64_t n) {
  uint64_t j = i;
  while (j < n && !in[j]) j++;
  return j - i;
}

/* One block, greedy with a step of lazy matching; a run of zeros as long as
   the match there is skipped rather than matched, which writes nothing */
static void lz_block(LzWork *w, const uint8_t *in, uint64_t n, Buf *out) {
  uint32_t hbits = 8;
  while (hbits < 16 && (1ull << hbits) < n) hbits++;
  w->hbits = hbits;
  memset(w->head, 0xff, sizeof(uint32_t) << hbits);
  uint64_t i = 0, lit = 0;
  while (i < n) {
    uint64_t z = in[i] ? 0 : zero_run(in, i, n);
    if (z >= LZ_MINM) {
      lz_emit(out, in + lit, i - lit, 0, z, false);
      i += z;
      lit = i;
      continue;
    }
    uint64_t off, ml = lz_find(w, in, i, n, &off);
    if (!ml) { lz_insert(w, in, i, n); i++; continue; }
    lz_insert(w, in, i, n);
    if (i + 1 < n) {
      uint64_t off2, ml2 = lz_find(w, in, i + 1, n, &off2);
      uint64_t z2 = in[i + 1] ? 0 : zero_run(in, i + 1, n);
      if (ml2 > ml + 1 || (z2 >= LZ_MINM && z2 >= ml)) { i++; continue; }
    }
    lz_emit(out, in + lit, i - lit, off, ml, false);
    for (uint64_t k = i + 1; k < i + ml; k++) lz_insert(w, in, k, n);
    i += ml;
    lit = i;
  }
  lz_emit(out, in + lit, n - lit, 0, 0, true);
}

typedef struct {
  uint32_t mem;
  uint64_t base, len;
} Span;

typedef struct {
  uint32_t span;
  uint64_t at, len;     /* in the span */
  uint32_t first, n;    /* its segments, in index order, in Pack.segs */
} Block;

typedef struct {
  const Module *m;
  Block *blocks;
  uint32_t *segs;
  Span *spans;
  Buf *outs;
  LzWork *work;
} Pack;

static uint64_t seg_offset(const DataSeg *d) { return d->offset.ins[0].lo; }

static LzWork lz_work_new(uint64_t block) {
  return (LzWork){ xmalloc(sizeof(uint32_t) << 16),
                   xmalloc(sizeof(uint32_t) * (block ? block : 1)), 0 };
}

static void lz_work_free(LzWork *w) { xfree(w->head); xfree(w->prev); }

/* one block, for test/datapack_test.c */
void datapack_lz_block(const uint8_t *in, uint64_t n, Buf *out) {
  LzWork w = lz_work_new(n);
  lz_block(&w, in, n, out);
  lz_work_free(&w);
}

static uint32_t block_log2(void) {
  const char *e = warmc_getenv("WARMC_PACK_BLOCK_LOG2");
  int v = e ? atoi(e) : BLOCK_LOG2;
  return v < 4 ? 4 : v > BLOCK_LOG2_MAX ? BLOCK_LOG2_MAX : (uint32_t)v;
}

static void pack_one(void *ctx, uint32_t k, uint32_t wi) {
  Pack *p = ctx;
  const Block *b = &p->blocks[k];
  uint64_t start = p->spans[b->span].base + b->at;
  uint8_t *img = xcalloc(b->len ? b->len : 1, 1);
  for (uint32_t s = 0; s < b->n; s++) {
    const DataSeg *d = &p->m->datas[p->segs[b->first + s]];
    uint64_t lo = seg_offset(d), hi = lo + d->len;
    uint64_t a = lo > start ? lo : start, e = hi < start + b->len ? hi : start + b->len;
    if (a < e) memcpy(img + (a - start), d->data + (a - lo), e - a);
  }
  lz_block(&p->work[wi], img, b->len, &p->outs[k]);
  xfree(img);
}

static bool image_fits(const Module *m, uint32_t mi) {
  const Memory *mem = &m->memories[mi];
  if (mem->imported) return false;
  uint64_t size = mem->min > (UINT64_MAX >> 16) ? UINT64_MAX : mem->min << 16;
  for (uint32_t i = 0; i < m->ndatas; i++) {
    const DataSeg *d = &m->datas[i];
    if (d->mode != SEG_ACTIVE || d->mem_index != mi) continue;
    const InitExpr *e = &d->offset;
    if (e->n != 1 || (e->ins[0].op != IOP_I32_CONST && e->ins[0].op != IOP_I64_CONST))
      return false;
    uint64_t off = e->ins[0].lo;
    if (off > size || d->len > size - off) return false;
  }
  return true;
}

typedef struct { uint64_t off; uint32_t idx; } OffIdx;

static int cmp_offidx(const void *a, const void *b) {
  const OffIdx *x = a, *y = b;
  if (x->off != y->off) return x->off < y->off ? -1 : 1;
  return x->idx < y->idx ? -1 : x->idx > y->idx;
}

static int cmp_u32(const void *a, const void *b) {
  uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
  return x < y ? -1 : x > y;
}

/* d_at[i]: the constant expression an active segment's offset is, for one
   whose offset is not a constant */
void datapack_emit(const Module *m, const uint32_t *d_at, Buf *out) {
  bool *img = xcalloc(m->nmemories ? m->nmemories : 1, sizeof(bool));
  for (uint32_t mi = 0; mi < m->nmemories; mi++)
    img[mi] = g_compress_data && image_fits(m, mi);

  /* each image memory's segments by offset; a span where a gap is too
     long; blocks of it, with the segments that reach into each, in index
     order, as a later one writes over an earlier */
  uint32_t nspans = 0, nblocks = 0, nsegs = 0, cspan = 0, cblock = 0, csegs = 0;
  Span *spans = NULL;
  Block *blocks = NULL;
  uint32_t *segs = NULL;
  OffIdx *bymem = xmalloc(sizeof(OffIdx) * (m->ndatas ? m->ndatas : 1));
  uint32_t *idx = xmalloc(sizeof(uint32_t) * (m->ndatas ? m->ndatas : 1));
  uint32_t blog = block_log2();
  uint64_t blk = 1ull << blog;
  for (uint32_t mi = 0; mi < m->nmemories; mi++) {
    if (!img[mi]) continue;
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->ndatas; i++) {
      const DataSeg *d = &m->datas[i];
      if (d->mode == SEG_ACTIVE && d->mem_index == mi && d->len)
        bymem[n++] = (OffIdx){ seg_offset(d), i };
    }
    qsort(bymem, n, sizeof(OffIdx), cmp_offidx);
    for (uint32_t k = 0, k1; k < n; k = k1) {
      uint64_t base = bymem[k].off, end = base;
      for (k1 = k; k1 < n; k1++) {
        if (k1 > k && bymem[k1].off > end + SPAN_GAP) break;
        uint64_t e = bymem[k1].off + m->datas[bymem[k1].idx].len;
        if (e > end) end = e;
      }
      if (nspans == cspan) {
        cspan = cspan ? cspan * 2 : 8;
        spans = xrealloc(spans, cspan * sizeof(Span));
      }
      spans[nspans] = (Span){ mi, base, end - base };
      uint64_t nb = (end - base + blk - 1) / blk;
      uint32_t b0 = nblocks;
      for (uint64_t j = 0; j < nb; j++) {
        if (nblocks == cblock) {
          cblock = cblock ? cblock * 2 : 16;
          blocks = xrealloc(blocks, cblock * sizeof(Block));
        }
        uint64_t at = j * blk, left = end - base - at;
        blocks[nblocks++] = (Block){ nspans, at, left < blk ? left : blk, 0, 0 };
      }
      uint32_t ns = k1 - k;
      for (uint32_t t = 0; t < ns; t++) idx[t] = bymem[k + t].idx;
      qsort(idx, ns, sizeof(uint32_t), cmp_u32);
      /* count each block's segments, then place them */
      for (uint32_t t = 0; t < ns; t++) {
        const DataSeg *d = &m->datas[idx[t]];
        uint64_t o = seg_offset(d) - base;
        for (uint64_t j = o / blk; j <= (o + d->len - 1) / blk; j++) blocks[b0 + j].n++;
      }
      uint32_t at = nsegs;
      for (uint64_t j = 0; j < nb; j++) {
        blocks[b0 + j].first = at;
        at += blocks[b0 + j].n;
        blocks[b0 + j].n = 0;
      }
      if (at > csegs) {
        while (csegs < at) csegs = csegs ? csegs * 2 : 64;
        segs = xrealloc(segs, csegs * sizeof(uint32_t));
      }
      nsegs = at;
      for (uint32_t t = 0; t < ns; t++) {
        const DataSeg *d = &m->datas[idx[t]];
        uint64_t o = seg_offset(d) - base;
        for (uint64_t j = o / blk; j <= (o + d->len - 1) / blk; j++) {
          Block *bk = &blocks[b0 + j];
          segs[bk->first + bk->n++] = idx[t];
        }
      }
      nspans++;
    }
  }
  xfree(bymem);
  xfree(idx);

  Buf *outs = xcalloc(nblocks ? nblocks : 1, sizeof(Buf));
  if (nblocks) {
    uint32_t nt = compile_want_threads((Module *)m, nblocks);
    if (nt > nblocks) nt = nblocks;
    if (nt < 1) nt = 1;
    LzWork *work = xcalloc(nt, sizeof(LzWork));
    for (uint32_t t = 0; t < nt; t++) work[t] = lz_work_new(blk);
    Pack p = { m, blocks, segs, spans, outs, work };
    warmc_par_for(nblocks, nt, pack_one, &p);
    for (uint32_t t = 0; t < nt; t++) lz_work_free(&work[t]);
    xfree(work);
  }

  put_var(out, blog);
  put_var(out, nspans);
  for (uint32_t s = 0, b = 0; s < nspans; s++) {
    put_var(out, spans[s].mem);
    put_var(out, spans[s].base);
    put_var(out, spans[s].len);
    for (; b < nblocks && blocks[b].span == s; b++) {
      buf_bytes(out, outs[b].data, outs[b].len);
      buf_free(&outs[b]);
    }
  }
  xfree(outs);
  xfree(blocks);
  xfree(segs);
  xfree(spans);

  uint32_t nstream = 0;
  for (uint32_t i = 0; i < m->ndatas; i++)
    if (m->datas[i].mode == SEG_ACTIVE && !img[m->datas[i].mem_index]) nstream++;
  put_var(out, nstream);
  for (uint32_t i = 0; i < m->ndatas; i++) {
    const DataSeg *d = &m->datas[i];
    if (d->mode != SEG_ACTIVE || img[d->mem_index]) continue;
    const InitExpr *e = &d->offset;
    bool cst = e->n == 1 && (e->ins[0].op == IOP_I32_CONST || e->ins[0].op == IOP_I64_CONST);
    put_var(out, d->mem_index);
    put_var(out, cst ? 0 : 1);
    put_var(out, cst ? e->ins[0].lo : d_at[i]);
    put_var(out, d->len);
    buf_bytes(out, d->data, d->len);
  }
  buf_zero(out, 16);
  xfree(img);
}
