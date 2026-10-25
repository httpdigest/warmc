#ifndef WARMC_READER_H
#define WARMC_READER_H

#include "warmc.h"

#include <string.h>

typedef struct {
  const uint8_t *p, *end, *start;
} Rd;

static inline Rd rd_make(const uint8_t *p, const uint8_t *end, const uint8_t *start) {
  Rd r = { p, end, start ? start : p };
  return r;
}

void wasm_skip_core_externtype(Rd *r);
void wasm_skip_core_rectype(Rd *r);

static inline size_t rd_pos(const Rd *r) { return (size_t)(r->p - r->start); }
static inline bool rd_eof(const Rd *r) { return r->p >= r->end; }

static inline void rd_check(Rd *r, size_t n) {
  if ((size_t)(r->end - r->p) < n)
    warmc_malformed("unexpected end of section at byte 0x%zx", rd_pos(r));
}

static inline uint8_t rd_u8(Rd *r) { rd_check(r, 1); return *r->p++; }

static inline uint32_t rd_u32le(Rd *r) {
  rd_check(r, 4);
  uint32_t v = (uint32_t)r->p[0] | ((uint32_t)r->p[1] << 8) |
               ((uint32_t)r->p[2] << 16) | ((uint32_t)r->p[3] << 24);
  r->p += 4;
  return v;
}

static inline uint64_t rd_u64le(Rd *r) {
  uint64_t lo = rd_u32le(r), hi = rd_u32le(r);
  return lo | (hi << 32);
}

static inline uint64_t rd_uleb(Rd *r, int bits) {
  uint64_t result = 0;
  int shift = 0;
  for (;;) {
    uint8_t byte = rd_u8(r);
    if (shift + 7 >= bits) {

      uint32_t room = (uint32_t)(bits - shift);
      if (byte & 0x80) warmc_malformed("integer representation too long");
      if (room < 7 && (byte >> room) != 0)
        warmc_malformed("integer too large");
      return result | ((uint64_t)byte << shift);
    }
    result |= (uint64_t)(byte & 0x7f) << shift;
    if (!(byte & 0x80)) return result;
    shift += 7;
  }
}

static inline int64_t rd_sleb(Rd *r, int bits) {
  uint64_t result = 0;
  int shift = 0;
  for (;;) {
    uint8_t byte = rd_u8(r);
    if (shift + 7 >= bits) {
      if (byte & 0x80) warmc_malformed("integer representation too long");

      uint32_t room = (uint32_t)(bits - shift);
      uint8_t mask = (uint8_t)(0x7f & ~((1u << (room - 1)) - 1u));
      uint8_t top = (uint8_t)(byte & mask);
      if (top != 0 && top != mask) warmc_malformed("integer too large");
      result |= (uint64_t)(byte & 0x7f) << shift;
      shift += 7;
      if (shift < 64 && (byte & 0x40)) result |= ~(uint64_t)0 << shift;
      return (int64_t)result;
    }
    result |= (uint64_t)(byte & 0x7f) << shift;
    shift += 7;
    if (!(byte & 0x80)) {
      if (shift < 64 && (byte & 0x40)) result |= ~(uint64_t)0 << shift;
      return (int64_t)result;
    }
  }
}

static inline uint32_t rd_u32(Rd *r) { return (uint32_t)rd_uleb(r, 32); }
static inline uint64_t rd_u64(Rd *r) { return rd_uleb(r, 64); }
static inline int32_t rd_i32(Rd *r) { return (int32_t)rd_sleb(r, 32); }
static inline int64_t rd_i64(Rd *r) { return rd_sleb(r, 64); }

static inline int64_t rd_s33(Rd *r) { return rd_sleb(r, 33); }

static inline HeapType rd_heaptype(Rd *r) {
  const uint8_t *save = r->p;
  uint8_t b = rd_u8(r);
  if (b >= HT_BYTE_LO && b <= HT_BYTE_HI) return HT_OF_BYTE(b);
  if (b == 0x62)
    warmc_malformed(
        "malformed heap type: exact reference types (the custom-descriptors "
        "proposal) are not supported -- if this came from wasm-opt, rerun it "
        "without --enable-custom-descriptors (--all-features enables it)");
  r->p = save;
  int64_t x = rd_s33(r);
  if (x < 0)
    warmc_malformed("malformed heap type 0x%02x at offset 0x%zx", b,
                    (size_t)(save - r->start));

  /* a non-negative s33 is a type index, however large, and well-formed; one
     past what a value type can name is past every type a module may have
     here too (parse_types refuses more), so it is unknown: invalid, and a
     decoder holding that goes on with an abstract type in its place */
  if ((uint64_t)x > VT_HEAP_MAX) {
    warmc_invalid_held("unknown type %lld: a value type names at most %u",
                       (long long)x, VT_HEAP_MAX);
    return HT_NONE;
  }
  return (HeapType)x;
}

static inline bool vt_starts_here(uint8_t b) {
  return b == VT_I32 || b == VT_I64 || b == VT_F32 || b == VT_F64 ||
         b == VT_V128 || b == 0x63 || b == 0x64 ||
         (b >= HT_BYTE_LO && b <= HT_BYTE_HI);
}

static inline ValType rd_valtype(Rd *r) {
  uint8_t b = rd_u8(r);
  switch (b) {
    case VT_I32: case VT_I64: case VT_F32: case VT_F64: case VT_V128:
      return (ValType)b;
    case 0x63: return vt_ref(true, rd_heaptype(r));
    case 0x64: return vt_ref(false, rd_heaptype(r));
  }
  if (b >= HT_BYTE_LO && b <= HT_BYTE_HI) return vt_ref(true, HT_OF_BYTE(b));
  warmc_malformed("unknown value type 0x%02x", b);
}

static inline ValType rd_reftype(Rd *r) {
  const uint8_t *save = r->p;
  ValType t = rd_valtype(r);
  if (!vt_is_ref(t)) {
    r->p = save;
    warmc_malformed("malformed reference type");
  }
  return t;
}

typedef struct {
  bool empty;
  bool single;
  ValType vt;
  uint64_t typeidx;
} BlockType;

static inline BlockType rd_blocktype(Rd *r) {
  BlockType bt;
  bt.empty = bt.single = false;
  bt.vt = VT_BOT;
  bt.typeidx = 0;
  const uint8_t *save = r->p;
  uint8_t b = rd_u8(r);
  if (b == 0x40) { bt.empty = true; return bt; }
  if (vt_starts_here(b)) {
    r->p = save;
    bt.single = true;
    bt.vt = rd_valtype(r);
    return bt;
  }
  r->p = save;
  int64_t x = rd_s33(r);
  if (x < 0) warmc_malformed("malformed block type 0x%02x", b);
  bt.typeidx = (uint64_t)x;
  return bt;
}

#endif
