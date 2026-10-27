/* src/datapack.c's data_init, read: included by rt.c, and by
   test/datapack_test.c, which brings its own warmc_trap and mem_move and
   puts the decoder through streams the compiler would never write. */

static u64 rd_var(const u8 **p) {
  u64 v = 0;
  u32 sh = 0;
  u8 b;
  do { b = *(*p)++; v |= (u64)(b & 0x7f) << sh; sh += 7; } while (b & 0x80);
  return v;
}

static u64 rd_more(const u8 **p, u64 v) {
  u8 b;
  do { b = *(*p)++; v += b; } while (b == 255);
  return v;
}

static inline void cp8(u8 *d, const u8 *s) {
  u64 a;
  __builtin_memcpy(&a, s, 8);
  __builtin_memcpy(d, &a, 8);
}

static inline void cp16(u8 *d, const u8 *s) {
  u64 a, b;
  __builtin_memcpy(&a, s, 8);
  __builtin_memcpy(&b, s + 8, 8);
  __builtin_memcpy(d, &a, 8);
  __builtin_memcpy(d + 8, &b, 8);
}

/* One block of src/datapack.c's LZ77 into [dst, dst + n), the memory's and
   zero; where src goes on after it.  A copy short enough is made 16 bytes
   at a time, writing past its end as long as the block goes on that far:
   the next literals or match write there over it, and a run of zeros, which
   writes nothing else, clears the 16 bytes where it starts.  Nothing is
   written outside the block, whatever the stream says: a length past it, or
   a match from before it, traps. */
static const u8 *lz_unpack(u8 *dst, u64 n, const u8 *s) {
  u8 *o = dst, *end = dst + n;
  for (;;) {
    u32 t = *s++;
    u64 lit = t >> 4;
    if (lit == 15) lit = rd_more(&s, lit);
    if (lit > (u64)(end - o)) warmc_trap(TRAP_MEM_OOB);
    if (lit <= 16 && end - o >= 16) cp16(o, s);
    else mem_move(o, s, lit);
    o += lit;
    s += lit;
    if (o == end) return s;
    u64 off = s[0] | (u64)s[1] << 8;
    s += 2;
    u64 ml = (t & 15) + 4;
    if ((t & 15) == 15) ml = rd_more(&s, ml);
    if (ml > (u64)(end - o)) warmc_trap(TRAP_MEM_OOB);
    if (!off) {
      if (end - o >= 16) { u64 z = 0; __builtin_memcpy(o, &z, 8); __builtin_memcpy(o + 8, &z, 8); }
      else for (u8 *c = o; c < end; c++) *c = 0;
      o += ml;
      continue;
    }
    if (off > (u64)(o - dst)) warmc_trap(TRAP_MEM_OOB);
    const u8 *m = o - off;
    if (ml <= 16 && off >= 16 && end - o >= 16) {
      cp16(o, m);
    } else if (off >= 8 && (u64)(end - o) >= ml + 8) {
      for (u8 *c = o; c < o + ml; c += 8, m += 8) cp8(c, m);
    } else {
      for (u64 k = 0; k < ml; k++) o[k] = m[k];
    }
    o += ml;
  }
}
