#include "linkobj.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

static void layout_macos(Linker *L);
static void write_macho(Linker *L, const char *path, uint64_t entry);

static uint32_t gs_hash(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
  return h;
}

static void gs_ix_put(Linker *L, uint32_t i) {
  uint32_t mask = L->gs_ix_cap - 1;
  uint32_t h = gs_hash(L->gsyms[i].name) & mask;
  while (L->gs_ix[h]) h = (h + 1) & mask;
  L->gs_ix[h] = i + 1;
}

static void gs_ix_grow(Linker *L) {
  uint32_t want = L->gs_ix_cap ? L->gs_ix_cap * 2 : 1024;
  xfree(L->gs_ix);
  L->gs_ix = xcalloc(want, sizeof(uint32_t));
  L->gs_ix_cap = want;
  for (uint32_t i = 0; i < L->ngsyms; i++) gs_ix_put(L, i);
}

static int32_t gs_find(Linker *L, const char *name) {
  if (!L->gs_ix_cap) return -1;
  uint32_t mask = L->gs_ix_cap - 1;
  uint32_t h = gs_hash(name) & mask;
  while (L->gs_ix[h]) {
    uint32_t i = L->gs_ix[h] - 1;
    if (!strcmp(L->gsyms[i].name, name)) return (int32_t)i;
    h = (h + 1) & mask;
  }
  return -1;
}

static uint32_t gs_intern(Linker *L, const char *name) {
  int32_t i = gs_find(L, name);
  if (i >= 0) return (uint32_t)i;
  if (L->ngsyms == L->cap_gsyms) {
    L->cap_gsyms = L->cap_gsyms ? L->cap_gsyms * 2 : 256;
    L->gsyms = xrealloc(L->gsyms, L->cap_gsyms * sizeof(GSym));
  }
  GSym *g = &L->gsyms[L->ngsyms];
  memset(g, 0, sizeof *g);
  g->name = xstrndup(name, strlen(name));
  g->obj = g->sym = -1;
  g->got_slot = g->stub = NO_SLOT;
  uint32_t idx = L->ngsyms++;
  if ((uint64_t)L->ngsyms * 3 >= (uint64_t)L->gs_ix_cap * 2) gs_ix_grow(L);
  else gs_ix_put(L, idx);
  return idx;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
  return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static double lk_ms(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

void chmod_exec(const char *path) {
#ifdef _WIN32
  (void)path;
#else
  if (chmod(path, 0755) != 0) warmc_fatal("link: cannot make %s executable", path);
#endif
}

static uint8_t *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) warmc_fatal("link: cannot open %s", path);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  if (n < 0) warmc_fatal("link: cannot size %s", path);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = xmalloc((size_t)n ? (size_t)n : 1);
  if (n && fread(b, 1, (size_t)n, f) != (size_t)n)
    warmc_fatal("link: short read on %s", path);
  fclose(f);
  *len = (size_t)n;
  return b;
}

static InObj *obj_new(Linker *L, const char *name, uint8_t *buf, size_t len) {
  if (L->nobjs == L->cap_objs) {
    L->cap_objs = L->cap_objs ? L->cap_objs * 2 : 16;
    L->objs = xrealloc(L->objs, L->cap_objs * sizeof(InObj));
  }
  InObj *o = &L->objs[L->nobjs++];
  memset(o, 0, sizeof *o);
  o->name = xstrndup(name, strlen(name));
  o->buf = buf;
  o->len = len;
  return o;
}

typedef struct { const char *name; const uint8_t *data; size_t size; } ArMember;

typedef struct {
  uint8_t *buf; size_t len, pos;
  const char *longnames; size_t nlongnames;
  char namebuf[256];
} ArReader;

static void ar_open(ArReader *a, uint8_t *buf, size_t len, const char *path) {
  if (len < 8 || memcmp(buf, "!<arch>\n", 8))
    warmc_fatal("link: %s is not an archive", path);
  memset(a, 0, sizeof *a);
  a->buf = buf; a->len = len; a->pos = 8;
}

static bool ar_next(ArReader *a, ArMember *m) {
  while (a->pos + 60 <= a->len) {
    const uint8_t *h = a->buf + a->pos;
    char szbuf[11];
    memcpy(szbuf, h + 48, 10); szbuf[10] = 0;
    size_t size = (size_t)strtoul(szbuf, NULL, 10);
    const uint8_t *data = h + 60;
    size_t adv = 60 + size + (size & 1);
    if (a->pos + 60 + size > a->len) return false;

    char raw[17];
    memcpy(raw, h, 16); raw[16] = 0;
    for (int i = 15; i >= 0 && (raw[i] == ' ' || raw[i] == '/'); i--) raw[i] = 0;

    a->pos += adv;

    if (!strncmp(raw, "#1/", 3)) {
      size_t nl = (size_t)strtoul(raw + 3, NULL, 10);
      if (nl > size) return false;
      size_t n = nl < sizeof a->namebuf - 1 ? nl : sizeof a->namebuf - 1;
      memcpy(a->namebuf, data, n);
      a->namebuf[n] = 0;

      m->name = a->namebuf;
      m->data = data + nl;
      m->size = size - nl;
    } else if (raw[0] == '/' && raw[1] >= '0' && raw[1] <= '9') {

      size_t off = (size_t)strtoul(raw + 1, NULL, 10);
      if (off >= a->nlongnames) return false;
      const char *s = a->longnames + off;
      size_t n = 0;
      while (off + n < a->nlongnames && s[n] != '/' && s[n] != '\n') n++;
      if (n >= sizeof a->namebuf) n = sizeof a->namebuf - 1;
      memcpy(a->namebuf, s, n);
      a->namebuf[n] = 0;
      m->name = a->namebuf;
      m->data = data;
      m->size = size;
    } else if (raw[0] == '/' || raw[0] == 0) {
      char first[17];
      memcpy(first, h, 16); first[16] = 0;
      if (!strncmp(first, "//", 2)) { a->longnames = (const char *)data; a->nlongnames = size; }
      continue;
    } else {
      snprintf(a->namebuf, sizeof a->namebuf, "%s", raw);
      m->name = a->namebuf;
      m->data = data;
      m->size = size;
    }

    if (!strncmp(m->name, "__.SYMDEF", 9)) continue;
    return true;
  }
  return false;
}

#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8

#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4

#define STB_LOCAL 0
#define STB_WEAK  2
#define SHN_UNDEF  0
#define SHN_ABS    0xfff1
#define SHN_COMMON 0xfff2

#define R_AARCH64_ABS64              257
#define R_AARCH64_ABS32              258
#define R_AARCH64_PREL32             261
#define R_AARCH64_PREL64             260
#define R_AARCH64_ADR_PREL_PG_HI21   275
#define R_AARCH64_ADD_ABS_LO12_NC    277
#define R_AARCH64_CALL26             283
#define R_AARCH64_JUMP26             282
#define R_AARCH64_LDST8_ABS_LO12_NC  278
#define R_AARCH64_LDST16_ABS_LO12_NC 284
#define R_AARCH64_LDST32_ABS_LO12_NC 285
#define R_AARCH64_LDST64_ABS_LO12_NC 286
#define R_AARCH64_LDST128_ABS_LO12_NC 299
#define R_AARCH64_ADR_GOT_PAGE       311
#define R_AARCH64_LD64_GOT_LO12_NC   312

static SecKind elf_kind(const char *name, uint32_t type, uint64_t flags) {
  if (!(flags & SHF_ALLOC)) return K_DROP;
  if (type == SHT_NOBITS) return K_BSS;
  if (flags & SHF_EXECINSTR) return K_TEXT;
  if (flags & SHF_WRITE) return K_DATA;
  if (!strcmp(name, ".wasm.custom")) return K_CUSTOM;
  return K_RODATA;
}

static void read_elf_obj(InObj *o) {
  const uint8_t *b = o->buf;
  if (o->len < 64 || memcmp(b, "\177ELF", 4) || b[4] != 2 || b[5] != 1)
    warmc_fatal("link: %s is not a little-endian ELF64 object", o->name);
  uint64_t shoff = rd64(b + 0x28);
  uint16_t shentsize = rd16(b + 0x3a), shnum = rd16(b + 0x3c), shstrndx = rd16(b + 0x3e);
  const uint8_t *sh = b + shoff;
  const char *shstr = (const char *)(b + rd64(sh + (size_t)shstrndx * shentsize + 0x18));

  o->nsecs = shnum;
  o->secs = xcalloc(shnum ? shnum : 1, sizeof(InSec));
  for (uint32_t i = 0; i < shnum; i++) {
    const uint8_t *s = sh + (size_t)i * shentsize;
    uint32_t type = rd32(s + 4);
    uint64_t flags = rd64(s + 8), off = rd64(s + 0x18), size = rd64(s + 0x20);
    uint64_t align = rd64(s + 0x30);
    InSec *is = &o->secs[i];
    is->name = xstrndup(shstr + rd32(s), strlen(shstr + rd32(s)));
    is->size = size;
    is->align = align ? align : 1;
    is->kind = elf_kind(is->name, type, flags);
    is->data = (type == SHT_NOBITS || is->kind == K_DROP) ? NULL : b + off;
  }

  for (uint32_t i = 0; i < shnum; i++) {
    const uint8_t *s = sh + (size_t)i * shentsize;
    if (rd32(s + 4) != SHT_SYMTAB) continue;
    uint64_t off = rd64(s + 0x18), size = rd64(s + 0x20), esz = rd64(s + 0x38);
    uint32_t strtab = rd32(s + 0x28);
    const char *str = (const char *)(b + rd64(sh + (size_t)strtab * shentsize + 0x18));
    uint32_t n = esz ? (uint32_t)(size / esz) : 0;
    o->nsyms = n;
    o->syms = xcalloc(n ? n : 1, sizeof(InSym));
    for (uint32_t k = 0; k < n; k++) {
      const uint8_t *e = b + off + (size_t)k * esz;
      InSym *sy = &o->syms[k];
      const char *nm = str + rd32(e);
      sy->name = xstrndup(nm, strlen(nm));
      uint8_t info = e[4];
      uint16_t shndx = rd16(e + 6);
      sy->value = rd64(e + 8);
      sy->global = (info >> 4) != STB_LOCAL;
      sy->weak = (info >> 4) == STB_WEAK;
      if (shndx == SHN_COMMON) {
        sy->common = true; sy->sec = -1;
        sy->comm_size = rd64(e + 16);
        sy->comm_align = sy->value ? sy->value : 1;
      } else if (shndx == SHN_UNDEF || shndx >= SHN_ABS) {
        sy->sec = -1;
      } else {
        sy->sec = (int32_t)shndx;
      }
    }
    break;
  }

  uint32_t total = 0;
  for (uint32_t i = 0; i < shnum; i++)
    if (rd32(sh + (size_t)i * shentsize + 4) == SHT_RELA)
      total += (uint32_t)(rd64(sh + (size_t)i * shentsize + 0x20) / 24);
  o->rels = xcalloc(total ? total : 1, sizeof(InRel));
  for (uint32_t i = 0; i < shnum; i++) {
    const uint8_t *s = sh + (size_t)i * shentsize;
    if (rd32(s + 4) != SHT_RELA) continue;
    uint32_t target = rd32(s + 0x2c);
    uint64_t off = rd64(s + 0x18), size = rd64(s + 0x20);
    if (target >= shnum || o->secs[target].kind == K_DROP) continue;
    for (uint64_t p = 0; p + 24 <= size; p += 24) {
      const uint8_t *r = b + off + p;
      InRel *ir = &o->rels[o->nrels++];
      ir->sec = target;
      ir->off = rd64(r);
      uint64_t info = rd64(r + 8);
      ir->sym = (int32_t)(info >> 32);
      ir->type = (uint32_t)(info & 0xffffffffu);
      ir->addend = (int64_t)rd64(r + 16);
    }
  }
}

#define MH_MAGIC_64 0xfeedfacfu
#define LC_SEGMENT_64 0x19
#define LC_SYMTAB     0x02

#define MH_SUBSECTIONS_VIA_SYMBOLS 0x2000u

#define S_REGULAR           0x0
#define S_ZEROFILL          0x1
#define SECTION_TYPE        0x000000ffu
#define S_ATTR_PURE_INSTRUCTIONS 0x80000000u
#define S_ATTR_SOME_INSTRUCTIONS 0x00000400u

#define N_STAB 0xe0
#define N_TYPE 0x0e
#define N_EXT  0x01
#define N_WEAK_DEF 0x0080
#define N_UNDF 0x0
#define N_SECT 0xe
#define N_ABS  0x2

#define ARM64_RELOC_UNSIGNED            0
#define ARM64_RELOC_SUBTRACTOR          1
#define ARM64_RELOC_BRANCH26            2
#define ARM64_RELOC_PAGE21              3
#define ARM64_RELOC_PAGEOFF12           4
#define ARM64_RELOC_GOT_LOAD_PAGE21     5
#define ARM64_RELOC_GOT_LOAD_PAGEOFF12  6
#define ARM64_RELOC_ADDEND             10

static SecKind macho_kind(const char *seg, const char *sect, uint32_t flags) {
  if ((flags & SECTION_TYPE) == S_ZEROFILL && !strcmp(seg, "__DATA")) return K_BSS;
  if (!strcmp(seg, "__TEXT")) {
    if (flags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS)) return K_TEXT;
    if (!strcmp(sect, "__wasm_custom")) return K_CUSTOM;
    return K_RODATA;
  }
  if (!strcmp(seg, "__DATA")) {
    if (!strcmp(sect, "__bss") || !strcmp(sect, "__common")) return K_BSS;
    return K_DATA;
  }

  return K_DROP;
}

static int cmp_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}

static uint32_t atom_of(const uint64_t *bnd, uint32_t n, uint64_t off) {
  uint32_t lo = 0, hi = n;
  while (hi - lo > 1) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (bnd[mid] <= off) lo = mid; else hi = mid;
  }
  return lo;
}

static void split_subsections(InObj *o) {
  uint32_t old_n = o->nsecs;
  uint64_t **bnd = xcalloc(old_n ? old_n : 1, sizeof(uint64_t *));
  uint32_t *nbnd = xcalloc(old_n ? old_n : 1, sizeof(uint32_t));
  uint32_t *base = xcalloc(old_n ? old_n : 1, sizeof(uint32_t));

  for (uint32_t s = 0; s < old_n; s++) {
    InSec *is = &o->secs[s];
    if (is->kind == K_DROP || !is->size) continue;
    uint32_t n = 0;
    for (uint32_t k = 0; k < o->nsyms; k++)
      if (o->syms[k].sec == (int32_t)s && o->syms[k].value < is->size) n++;
    if (!n) continue;
    uint64_t *v = xmalloc((n + 1) * sizeof(uint64_t));
    n = 0;
    v[n++] = 0;
    for (uint32_t k = 0; k < o->nsyms; k++)
      if (o->syms[k].sec == (int32_t)s && o->syms[k].value < is->size)
        v[n++] = o->syms[k].value;
    qsort(v, n, sizeof(uint64_t), cmp_u64);
    uint32_t u = 1;
    for (uint32_t i = 1; i < n; i++) if (v[i] != v[u - 1]) v[u++] = v[i];
    bnd[s] = v; nbnd[s] = u;
  }

  uint32_t total = 0;
  for (uint32_t s = 0; s < old_n; s++) {
    base[s] = total;
    total += nbnd[s] ? nbnd[s] : 1;
  }
  if (total == old_n) {
    for (uint32_t s = 0; s < old_n; s++) xfree(bnd[s]);
    xfree(bnd); xfree(nbnd); xfree(base);
    return;
  }

  InSec *ns = xcalloc(total ? total : 1, sizeof(InSec));
  for (uint32_t s = 0; s < old_n; s++) {
    InSec *is = &o->secs[s];
    if (!nbnd[s]) { ns[base[s]] = *is; continue; }
    for (uint32_t a = 0; a < nbnd[s]; a++) {
      uint64_t from = bnd[s][a];
      uint64_t to = a + 1 < nbnd[s] ? bnd[s][a + 1] : is->size;
      InSec *d = &ns[base[s] + a];
      d->name = xstrndup(is->name, strlen(is->name));
      d->data = is->data ? is->data + from : NULL;
      d->size = to - from;
      /* A function keeps the alignment its start had in the section: warmc
         pads a function or loop to 64 bytes within its object, and placing
         each one at the section's alignment after whatever dead stripping
         left before it would undo that.  A variable gets the section's whole
         alignment, as every atom used to: the runtime's .bss is 128-aligned,
         so that puts each of its variables on cache lines of its own, which
         is what keeps the collector thread and the program from writing to
         one line -- packed the way the object has them, ktbench/list ran in
         130 ms instead of 70. */
      d->align = is->align;
      if (is->kind == K_TEXT) {
        d->align = from ? (from & -from) : is->align;
        if (d->align > is->align) d->align = is->align;
      }
      d->kind = is->kind;
    }
    xfree(is->name);
  }
  xfree(o->secs);
  o->secs = ns;
  o->nsecs = total;

  for (uint32_t k = 0; k < o->nsyms; k++) {
    InSym *sy = &o->syms[k];
    if (sy->sec < 0 || !nbnd[sy->sec]) {
      if (sy->sec >= 0) sy->sec = (int32_t)base[sy->sec];
      continue;
    }
    uint32_t a = atom_of(bnd[sy->sec], nbnd[sy->sec], sy->value);
    sy->value -= bnd[sy->sec][a];
    sy->sec = (int32_t)(base[sy->sec] + a);
  }
  for (uint32_t r = 0; r < o->nrels; r++) {
    InRel *ir = &o->rels[r];
    if (!nbnd[ir->sec]) { ir->sec = base[ir->sec]; continue; }
    uint32_t a = atom_of(bnd[ir->sec], nbnd[ir->sec], ir->off);
    ir->off -= bnd[ir->sec][a];
    ir->sec = base[ir->sec] + a;
  }

  for (uint32_t pass = 0; pass < 2; pass++)
    for (uint32_t k = 0; k < o->nsyms; k++) {
      InSym *sy = &o->syms[k];
      if (sy->sec < 0 || sy->value || !sy->name[0]) continue;
      if (sy->global != (pass == 0)) continue;
      InSec *is = &o->secs[sy->sec];
      if (!is->name || strchr(is->name, '(')) continue;
      char full[160];
      snprintf(full, sizeof full, "%s(%s)", is->name, sy->name);
      xfree(is->name);
      is->name = xstrndup(full, strlen(full));
    }

  for (uint32_t s = 0; s < old_n; s++) xfree(bnd[s]);
  xfree(bnd); xfree(nbnd); xfree(base);
}

static void read_macho_obj(InObj *o) {
  const uint8_t *b = o->buf;
  if (o->len < 32 || rd32(b) != MH_MAGIC_64)
    warmc_fatal("link: %s is not a 64-bit Mach-O object", o->name);
  uint32_t ncmds = rd32(b + 16);
  const uint8_t *lc = b + 32;

  uint32_t nsect = 0;
  const uint8_t *p = lc;
  for (uint32_t i = 0; i < ncmds; i++) {
    uint32_t cmd = rd32(p), sz = rd32(p + 4);
    if (cmd == LC_SEGMENT_64) nsect += rd32(p + 64);
    p += sz;
  }
  o->nsecs = nsect + 1;
  o->secs = xcalloc(o->nsecs, sizeof(InSec));

  uint32_t si = 1;
  uint32_t nreloc_total = 0;
  p = lc;
  for (uint32_t i = 0; i < ncmds; i++) {
    uint32_t cmd = rd32(p), sz = rd32(p + 4);
    if (cmd == LC_SEGMENT_64) {
      uint32_t ns = rd32(p + 64);
      const uint8_t *s = p + 72;
      for (uint32_t k = 0; k < ns; k++, s += 80, si++) {
        char sect[17], seg[17];
        memcpy(sect, s, 16); sect[16] = 0;
        memcpy(seg, s + 16, 16); seg[16] = 0;
        InSec *is = &o->secs[si];
        uint64_t size = rd64(s + 40);
        uint32_t off = rd32(s + 48), align = rd32(s + 52), flags = rd32(s + 64);
        char full[40];
        snprintf(full, sizeof full, "%s,%s", seg, sect);
        is->name = xstrndup(full, strlen(full));
        is->size = size;
        is->align = 1ull << align;
        is->kind = macho_kind(seg, sect, flags);
        is->data = (is->kind == K_BSS || is->kind == K_DROP) ? NULL : b + off;
        nreloc_total += rd32(s + 56);
      }
    }
    p += sz;
  }

  p = lc;
  for (uint32_t i = 0; i < ncmds; i++) {
    uint32_t cmd = rd32(p), sz = rd32(p + 4);
    if (cmd == LC_SYMTAB) {
      uint32_t symoff = rd32(p + 8), nsyms = rd32(p + 12), stroff = rd32(p + 16);
      const char *str = (const char *)(b + stroff);
      o->nsyms = nsyms;
      o->syms = xcalloc(nsyms ? nsyms : 1, sizeof(InSym));
      for (uint32_t k = 0; k < nsyms; k++) {
        const uint8_t *e = b + symoff + (size_t)k * 16;
        InSym *sy = &o->syms[k];
        const char *nm = str + rd32(e);
        sy->name = xstrndup(nm, strlen(nm));
        uint8_t type = e[4], sect = e[5];
        uint16_t desc = rd16(e + 6);
        sy->value = rd64(e + 8);
        if (type & N_STAB) { sy->sec = -1; sy->global = false; continue; }
        sy->global = (type & N_EXT) != 0;
        sy->weak = (desc & N_WEAK_DEF) != 0;
        sy->sec = ((type & N_TYPE) == N_SECT) ? (int32_t)sect : -1;

        if (sy->sec > 0 && (uint32_t)sy->sec >= o->nsecs)
          warmc_fatal("link: a symbol names section %d of %u",
                      sy->sec, o->nsecs);
      }
    }
    p += sz;
  }

  {
    uint64_t *secaddr = xcalloc(o->nsecs ? o->nsecs : 1, sizeof(uint64_t));
    uint32_t j = 1;
    p = lc;
    for (uint32_t i = 0; i < ncmds; i++) {
      uint32_t cmd = rd32(p), sz = rd32(p + 4);
      if (cmd == LC_SEGMENT_64) {
        uint32_t ns = rd32(p + 64);
        const uint8_t *s = p + 72;
        for (uint32_t k = 0; k < ns; k++, s += 80, j++) secaddr[j] = rd64(s + 32);
      }
      p += sz;
    }
    for (uint32_t k = 0; k < o->nsyms; k++)
      if (o->syms[k].sec > 0) o->syms[k].value -= secaddr[o->syms[k].sec];
    xfree(secaddr);
  }

  o->rels = xcalloc(nreloc_total ? nreloc_total : 1, sizeof(InRel));
  si = 1;
  p = lc;
  for (uint32_t i = 0; i < ncmds; i++) {
    uint32_t cmd = rd32(p), sz = rd32(p + 4);
    if (cmd == LC_SEGMENT_64) {
      uint32_t ns = rd32(p + 64);
      const uint8_t *s = p + 72;
      for (uint32_t k = 0; k < ns; k++, s += 80, si++) {
        uint32_t reloff = rd32(s + 56), nrel = rd32(s + 60);
        if (o->secs[si].kind == K_DROP) continue;
        int64_t pending_addend = 0;
        bool have_addend = false;
        for (uint32_t r = 0; r < nrel; r++) {
          const uint8_t *e = b + reloff + (size_t)r * 8;
          uint32_t addr = rd32(e), word = rd32(e + 4);
          if (addr & 0x80000000u)
            warmc_fatal("link: %s has a scattered relocation, which is not supported",
                        o->name);
          uint32_t symnum = word & 0xffffffu;
          uint32_t pcrel = (word >> 24) & 1, length = (word >> 25) & 3;
          uint32_t ext = (word >> 27) & 1, type = (word >> 28) & 0xf;
          if (type == ARM64_RELOC_ADDEND) {

            pending_addend = (int64_t)(int32_t)(symnum << 8) >> 8;
            have_addend = true;
            continue;
          }
          if (type == ARM64_RELOC_SUBTRACTOR)
            warmc_fatal("link: %s uses ARM64_RELOC_SUBTRACTOR, which is not supported",
                        o->name);
          if (!ext)
            warmc_fatal("link: %s has a section-relative relocation, which is "
                        "not supported (compile the runtime with -ffunction-sections?)",
                        o->name);
          InRel *ir = &o->rels[o->nrels++];
          ir->sec = si;
          ir->off = addr;
          ir->sym = (int32_t)symnum;
          ir->type = type;
          ir->pcrel = pcrel != 0;
          ir->length = (uint8_t)length;
          ir->addend = have_addend ? pending_addend : 0;
          have_addend = false;
          pending_addend = 0;
        }
      }
    }
    p += sz;
  }

  if (rd32(b + 24) & MH_SUBSECTIONS_VIA_SYMBOLS) split_subsections(o);
}

#define IMAGE_SCN_CNT_CODE_L             0x00000020u
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080u
#define IMAGE_SCN_LNK_INFO               0x00000200u
#define IMAGE_SCN_LNK_REMOVE             0x00000800u
#define IMAGE_SCN_LNK_COMDAT             0x00001000u
#define IMAGE_SCN_LNK_NRELOC_OVFL_L      0x01000000u
#define IMAGE_SCN_MEM_DISCARDABLE        0x02000000u
#define IMAGE_SCN_MEM_EXECUTE_L          0x20000000u
#define IMAGE_SCN_MEM_WRITE_L            0x80000000u

#define IMAGE_REL_AMD64_ADDR64   1
#define IMAGE_REL_AMD64_ADDR32   2
#define IMAGE_REL_AMD64_ADDR32NB 3
#define IMAGE_REL_AMD64_REL32    4
#define IMAGE_REL_AMD64_REL32_5  9

#define IMAGE_SYM_CLASS_EXTERNAL_L    2
#define IMAGE_SYM_CLASS_STATIC_L      3
#define IMAGE_SYM_CLASS_LABEL         6
#define IMAGE_SYM_CLASS_WEAK_EXTERNAL 105

static SecKind coff_kind(const char *name, uint32_t ch) {
  if (ch & (IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE | IMAGE_SCN_MEM_DISCARDABLE))
    return K_DROP;
  /* SEH tables and debug sections have no consumer in our runtime */
  if (!strncmp(name, ".debug", 6) || !strncmp(name, ".pdata", 6) ||
      !strncmp(name, ".xdata", 6) || !strncmp(name, ".llvm", 5))
    return K_DROP;
  if (ch & IMAGE_SCN_CNT_UNINITIALIZED_DATA) return K_BSS;
  if (ch & (IMAGE_SCN_CNT_CODE_L | IMAGE_SCN_MEM_EXECUTE_L)) return K_TEXT;
  if (ch & IMAGE_SCN_MEM_WRITE_L) return K_DATA;
  if (!strcmp(name, ".wcustom")) return K_CUSTOM;
  return K_RODATA;
}

static void read_coff_obj(InObj *o) {
  const uint8_t *b = o->buf;
  if (o->len < 20 || rd16(b) != 0x8664)
    warmc_fatal("link: %s is not an AMD64 COFF object", o->name);
  uint32_t nsec = rd16(b + 2);
  uint32_t symoff = rd32(b + 8), nraw = rd32(b + 12);
  const uint8_t *sh = b + 20 + rd16(b + 16);
  const char *strtab = (const char *)(b + symoff + 18ull * nraw);

  /* COFF section numbers are 1-based; slot 0 is a dummy */
  o->nsecs = nsec + 1;
  o->secs = xcalloc(o->nsecs, sizeof(InSec));
  o->secs[0].name = xstrndup("", 0);
  o->secs[0].kind = K_DROP;
  o->secs[0].align = 1;
  bool *comdat = xcalloc(o->nsecs, sizeof(bool));
  for (uint32_t i = 0; i < nsec; i++) {
    const uint8_t *s = sh + (size_t)i * 40;
    char name[64];
    if (s[0] == '/' && s[1] >= '0' && s[1] <= '9') {
      char digits[8];
      memcpy(digits, s + 1, 7); digits[7] = 0;
      snprintf(name, sizeof name, "%s", strtab + strtoul(digits, NULL, 10));
    } else {
      memcpy(name, s, 8); name[8] = 0;
    }
    uint32_t rawsize = rd32(s + 16), rawptr = rd32(s + 20), ch = rd32(s + 36);
    InSec *is = &o->secs[i + 1];
    is->name = xstrndup(name, strlen(name));
    is->size = rawsize;
    uint32_t al = (ch >> 20) & 0xf;
    is->align = al ? 1ull << (al - 1) : 16;
    is->kind = coff_kind(name, ch);
    is->data = (is->kind == K_BSS || is->kind == K_DROP || !rawptr) ? NULL : b + rawptr;
    comdat[i + 1] = (ch & IMAGE_SCN_LNK_COMDAT) != 0;
  }

  o->nsyms = nraw;
  o->syms = xcalloc(nraw ? nraw : 1, sizeof(InSym));
  uint8_t *sel = xcalloc(o->nsecs, 1);
  uint32_t *weak_at = xmalloc((nraw ? nraw : 1) * sizeof(uint32_t));
  uint32_t *weak_tag = xmalloc((nraw ? nraw : 1) * sizeof(uint32_t));
  uint32_t nweak = 0;
  for (uint32_t k = 0; k < nraw; k++) {
    const uint8_t *e = b + symoff + (size_t)k * 18;
    InSym *sy = &o->syms[k];
    char inbuf[9];
    const char *nm;
    if (rd32(e) == 0) {
      nm = strtab + rd32(e + 4);
    } else {
      memcpy(inbuf, e, 8); inbuf[8] = 0;
      nm = inbuf;
    }
    sy->name = xstrndup(nm, strlen(nm));
    uint32_t value = rd32(e + 8);
    int32_t secn = (int16_t)rd16(e + 12);
    uint8_t cls = e[16], naux = e[17];
    sy->sec = -1;
    if (cls == IMAGE_SYM_CLASS_EXTERNAL_L) {
      sy->global = true;
      if (secn == 0 && value > 0) {
        sy->common = true;
        sy->comm_size = value;
        sy->comm_align = 16;
      } else if (secn > 0 && (uint32_t)secn < o->nsecs) {
        sy->sec = secn;
        sy->value = value;
      }
    } else if (cls == IMAGE_SYM_CLASS_STATIC_L || cls == IMAGE_SYM_CLASS_LABEL) {
      if (secn > 0 && (uint32_t)secn < o->nsecs) {
        sy->sec = secn;
        sy->value = value;
      }
      /* a section symbol's aux record carries the COMDAT selection */
      if (cls == IMAGE_SYM_CLASS_STATIC_L && naux >= 1 && sy->sec > 0 &&
          value == 0 && !strcmp(sy->name, o->secs[sy->sec].name))
        sel[sy->sec] = b[symoff + (size_t)(k + 1) * 18 + 14];
    } else if (cls == IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
      sy->global = true;
      sy->weak = true;
      if (naux >= 1) {
        weak_at[nweak] = k;
        weak_tag[nweak] = rd32(b + symoff + (size_t)(k + 1) * 18);
        nweak++;
      }
    }
    for (uint8_t a = 0; a < naux && k + 1 < nraw; a++) {
      k++;
      o->syms[k].name = xstrndup("", 0);
      o->syms[k].sec = -1;
    }
  }

  /* a weak external adopts the default definition its aux record names */
  for (uint32_t i = 0; i < nweak; i++) {
    if (weak_tag[i] >= nraw) continue;
    InSym *t = &o->syms[weak_tag[i]];
    if (t->sec >= 0) {
      o->syms[weak_at[i]].sec = t->sec;
      o->syms[weak_at[i]].value = t->value;
    }
  }

  /* COMDAT ANY/SAME_SIZE/EXACT_MATCH/LARGEST definitions dedup like weak
     symbols: the first definition wins, duplicates are laid out unreferenced
     (and dead-stripped when -strip is on) */
  for (uint32_t k = 0; k < nraw; k++) {
    InSym *sy = &o->syms[k];
    if (!sy->global || sy->sec <= 0 || !comdat[sy->sec]) continue;
    uint8_t s = sel[sy->sec];
    if (s == 2 || s == 3 || s == 4 || s == 6) sy->weak = true;
  }
  xfree(weak_at); xfree(weak_tag); xfree(sel); xfree(comdat);

  uint32_t total = 0;
  for (uint32_t i = 0; i < nsec; i++) {
    const uint8_t *s = sh + (size_t)i * 40;
    uint32_t n = rd16(s + 32);
    if ((rd32(s + 36) & IMAGE_SCN_LNK_NRELOC_OVFL_L) && n == 0xffff)
      n = rd32(b + rd32(s + 24)) - 1;
    total += n;
  }
  o->rels = xcalloc(total ? total : 1, sizeof(InRel));
  for (uint32_t i = 0; i < nsec; i++) {
    const uint8_t *s = sh + (size_t)i * 40;
    InSec *is = &o->secs[i + 1];
    if (is->kind == K_DROP || is->kind == K_BSS) continue;
    uint32_t va0 = rd32(s + 12);
    uint32_t n = rd16(s + 32);
    const uint8_t *r = b + rd32(s + 24);
    if ((rd32(s + 36) & IMAGE_SCN_LNK_NRELOC_OVFL_L) && n == 0xffff) {
      n = rd32(r) - 1;
      r += 10;
    }
    for (uint32_t j = 0; j < n; j++, r += 10) {
      InRel *ir = &o->rels[o->nrels++];
      ir->sec = i + 1;
      ir->off = rd32(r) - va0;
      ir->sym = (int32_t)rd32(r + 4);
      ir->type = rd16(r + 8);
      /* the addend lives in the section bytes (COFF relocations carry
         none); REL32+k counts from k bytes past the end of the field, so
         normalize to our S + A - P reading */
      uint64_t need = ir->type == IMAGE_REL_AMD64_ADDR64 ? 8u : 4u;
      if (ir->off > is->size || need > is->size - ir->off)
        warmc_fatal("link: %s: relocation past the end of %s", o->name, is->name);
      const uint8_t *p = is->data + ir->off;
      switch (ir->type) {
        case IMAGE_REL_AMD64_ADDR64:
          ir->addend = (int64_t)rd64(p);
          break;
        case IMAGE_REL_AMD64_ADDR32:
        case IMAGE_REL_AMD64_ADDR32NB:
          ir->addend = (int32_t)rd32(p);
          break;
        default:
          if (ir->type >= IMAGE_REL_AMD64_REL32 && ir->type <= IMAGE_REL_AMD64_REL32_5) {
            ir->addend = (int64_t)(int32_t)rd32(p) - 4 -
                         (int64_t)(ir->type - IMAGE_REL_AMD64_REL32);
            break;
          }
          warmc_fatal("link: %s: unsupported COFF relocation type %u in %s",
                      o->name, ir->type, is->name);
      }
    }
  }
}

static void collect_globals(Linker *L, uint32_t oi) {
  InObj *o = &L->objs[oi];
  for (uint32_t i = 0; i < o->nsyms; i++) {
    InSym *s = &o->syms[i];
    if (!s->global || !s->name[0]) continue;
    uint32_t g = gs_intern(L, s->name);
    GSym *gs = &L->gsyms[g];
    bool defines = s->sec >= 0 || s->common;
    if (!defines) continue;
    if (gs->defined && !gs->weak && !s->weak)
      warmc_fatal("link: %s defined twice (%s and %s)", s->name,
                  L->objs[gs->obj].name, o->name);

    if (gs->defined && !gs->weak) continue;
    gs->defined = true;
    gs->weak = s->weak;
    gs->obj = (int32_t)oi;
    gs->sym = (int32_t)i;
  }
}

static bool any_undefined(Linker *L) {
  for (uint32_t i = 0; i < L->ngsyms; i++)
    if (!L->gsyms[i].defined && !L->gsyms[i].imported) return true;
  return false;
}

typedef struct {
  const char *path;
  uint8_t *buf; size_t len;
  bool *taken; uint32_t nmem;
} Archive;

static void ar_load(Archive *a, const char *path) {
  a->path = path;
  a->buf = read_file(path, &a->len);
  ArReader r; ArMember m;
  ar_open(&r, a->buf, a->len, path);
  a->nmem = 0;
  while (ar_next(&r, &m)) a->nmem++;
  a->taken = xcalloc(a->nmem ? a->nmem : 1, sizeof(bool));
}

static bool ar_take_one(Linker *L, Archive *a, void (*rd)(InObj *)) {
  ArReader r; ArMember m;
  ar_open(&r, a->buf, a->len, a->path);
  uint32_t mi = 0;
  while (ar_next(&r, &m)) {
    uint32_t me = mi++;
    if (a->taken[me]) continue;
    uint8_t *copy = xmalloc(m.size ? m.size : 1);
    memcpy(copy, m.data, m.size);
    InObj probe;
    memset(&probe, 0, sizeof probe);
    probe.name = xstrndup(m.name, strlen(m.name));
    probe.buf = copy; probe.len = m.size;
    rd(&probe);
    bool want = false;
    for (uint32_t i = 0; i < probe.nsyms && !want; i++) {
      InSym *sy = &probe.syms[i];
      if (!sy->global || (sy->sec < 0 && !sy->common) || !sy->name[0]) continue;
      int32_t g = gs_find(L, sy->name);
      if (g >= 0 && !L->gsyms[g].defined) want = true;
    }
    if (!want) {
      for (uint32_t i = 0; i < probe.nsecs; i++) xfree(probe.secs[i].name);
      for (uint32_t i = 0; i < probe.nsyms; i++) xfree(probe.syms[i].name);
      xfree(probe.secs); xfree(probe.syms); xfree(probe.rels);
      xfree(probe.name); xfree(copy);
      continue;
    }
    a->taken[me] = true;
    if (warmc_getenv("WARMC_LINK_DEBUG"))
      for (uint32_t q = 0; q < probe.nsecs; q++)
        if (probe.secs[q].kind == K_DROP && probe.secs[q].size)
          fprintf(stderr, "link:   drop %s %s (%llu bytes)\n", probe.name,
                  probe.secs[q].name, (unsigned long long)probe.secs[q].size);
    InObj *o = obj_new(L, probe.name, probe.buf, probe.len);
    o->secs = probe.secs; o->nsecs = probe.nsecs;
    o->syms = probe.syms; o->nsyms = probe.nsyms;
    o->rels = probe.rels; o->nrels = probe.nrels;
    xfree(probe.name);
    collect_globals(L, L->nobjs - 1);
    for (uint32_t i = 0; i < o->nsyms; i++)
      if (o->syms[i].global && o->syms[i].sec < 0 && !o->syms[i].common && o->syms[i].name[0])
        gs_intern(L, o->syms[i].name);
    return true;
  }
  return false;
}

static void pull_archives(Linker *L, Archive *ars, uint32_t nars, void (*rd)(InObj *)) {
  for (;;) {
    bool progress = false;
    for (uint32_t i = 0; i < nars && !progress; i++)
      progress = ar_take_one(L, &ars[i], rd);
    if (!progress) return;
  }
}

typedef struct { uint32_t obj, sec; } Site;
typedef struct { Site *v; uint32_t n, cap; } SiteStack;

typedef struct { int32_t obj, sec; } Prev;
typedef struct {
  SiteStack st;
  bool **live;
  Prev **prev;
  Site at;
  bool root;
} Walk;

static void ds_push(Walk *w, uint32_t oi, uint32_t s) {
  if (w->live[oi][s]) return;
  w->live[oi][s] = true;
  if (w->prev) {
    w->prev[oi][s].obj = w->root ? -1 : (int32_t)w->at.obj;
    w->prev[oi][s].sec = w->root ? -1 : (int32_t)w->at.sec;
  }
  SiteStack *st = &w->st;
  if (st->n == st->cap) {
    st->cap = st->cap ? st->cap * 2 : 256;
    st->v = xrealloc(st->v, st->cap * sizeof(Site));
  }
  st->v[st->n].obj = oi;
  st->v[st->n].sec = s;
  st->n++;
}

static void ds_sym(Linker *L, Walk *w, uint32_t oi, int32_t si) {
  InObj *o = &L->objs[oi];
  if (si < 0 || (uint32_t)si >= o->nsyms) return;
  InSym *s = &o->syms[si];
  if (!s->global) {
    if (s->sec >= 0 && (uint32_t)s->sec < o->nsecs) ds_push(w, oi, (uint32_t)s->sec);
    return;
  }
  int32_t g = gs_find(L, s->name);
  if (g < 0 || !L->gsyms[g].defined || L->gsyms[g].obj < 0) return;
  InObj *d = &L->objs[L->gsyms[g].obj];
  InSym *ds = &d->syms[L->gsyms[g].sym];
  if (ds->sec >= 0 && (uint32_t)ds->sec < d->nsecs)
    ds_push(w, (uint32_t)L->gsyms[g].obj, (uint32_t)ds->sec);
}

static void ds_why(Linker *L, Walk *w, const char *why) {
  for (uint32_t i = 0; i < L->nobjs; i++)
    for (uint32_t s = 0; s < L->objs[i].nsecs; s++) {
      const char *nm = L->objs[i].secs[s].name;
      if (!w->live[i][s] || !nm || !strstr(nm, why)) continue;
      fprintf(stderr, "why: %s %s\n", nm, L->objs[i].name);
      int32_t oi = (int32_t)i, si = (int32_t)s;
      for (uint32_t hop = 0; hop < 64; hop++) {
        Prev p = w->prev[oi][si];
        if (p.obj < 0) { fprintf(stderr, "why:   <- a root\n"); break; }
        fprintf(stderr, "why:   <- %s %s\n", L->objs[p.obj].secs[p.sec].name,
                L->objs[p.obj].name);
        oi = p.obj; si = p.sec;
      }
    }
}

static void dead_strip(Linker *L, const char *entry) {
  const char *why = warmc_getenv("WARMC_LINK_WHY");
  Walk w;
  memset(&w, 0, sizeof w);
  bool **live = xcalloc(L->nobjs ? L->nobjs : 1, sizeof(bool *));
  bool **hasdef = xcalloc(L->nobjs ? L->nobjs : 1, sizeof(bool *));
  w.live = live;
  if (why) w.prev = xcalloc(L->nobjs ? L->nobjs : 1, sizeof(Prev *));

  uint32_t **rstart = xcalloc(L->nobjs ? L->nobjs : 1, sizeof(uint32_t *));
  uint32_t **rcount = xcalloc(L->nobjs ? L->nobjs : 1, sizeof(uint32_t *));
  uint32_t **rord = xcalloc(L->nobjs ? L->nobjs : 1, sizeof(uint32_t *));
  for (uint32_t i = 0; i < L->nobjs; i++) {
    InObj *o = &L->objs[i];
    uint32_t n = o->nsecs ? o->nsecs : 1;
    live[i] = xcalloc(n, sizeof(bool));
    hasdef[i] = xcalloc(n, sizeof(bool));
    if (why) w.prev[i] = xcalloc(n, sizeof(Prev));
    rstart[i] = xcalloc(n, sizeof(uint32_t));
    rcount[i] = xcalloc(n, sizeof(uint32_t));
    rord[i] = xmalloc((o->nrels ? o->nrels : 1) * sizeof(uint32_t));
    for (uint32_t k = 0; k < o->nsyms; k++)
      if (o->syms[k].sec >= 0 && (uint32_t)o->syms[k].sec < o->nsecs)
        hasdef[i][o->syms[k].sec] = true;
    for (uint32_t r = 0; r < o->nrels; r++) rcount[i][o->rels[r].sec]++;
    uint32_t at = 0;
    for (uint32_t s = 0; s < o->nsecs; s++) {
      rstart[i][s] = at; at += rcount[i][s]; rcount[i][s] = 0;
    }
    for (uint32_t r = 0; r < o->nrels; r++) {
      uint32_t s = o->rels[r].sec;
      rord[i][rstart[i][s] + rcount[i][s]++] = r;
    }
  }

  w.root = true;
  for (uint32_t s = 0; s < L->objs[0].nsecs; s++)
    if (L->objs[0].secs[s].kind != K_DROP) ds_push(&w, 0, s);
  for (uint32_t i = 0; i < L->nobjs; i++)
    for (uint32_t s = 0; s < L->objs[i].nsecs; s++)
      if (L->objs[i].secs[s].kind != K_DROP && !hasdef[i][s])
        ds_push(&w, i, s);
  int32_t e = gs_find(L, entry);
  if (e >= 0 && L->gsyms[e].defined && L->gsyms[e].obj >= 0) {
    InObj *d = &L->objs[L->gsyms[e].obj];
    InSym *ds = &d->syms[L->gsyms[e].sym];
    if (ds->sec >= 0) ds_push(&w, (uint32_t)L->gsyms[e].obj, (uint32_t)ds->sec);
  }
  w.root = false;

  while (w.st.n) {
    Site at = w.st.v[--w.st.n];
    w.at = at;
    InObj *o = &L->objs[at.obj];
    uint32_t from = rstart[at.obj][at.sec], n = rcount[at.obj][at.sec];
    for (uint32_t k = 0; k < n; k++)
      ds_sym(L, &w, at.obj, o->rels[rord[at.obj][from + k]].sym);
  }
  if (why) ds_why(L, &w, why);

  uint64_t gone[K_DROP], kept[K_DROP];
  memset(gone, 0, sizeof gone);
  memset(kept, 0, sizeof kept);
  uint32_t ndrop = 0;
  for (uint32_t i = 0; i < L->nobjs; i++)
    for (uint32_t s = 0; s < L->objs[i].nsecs; s++) {
      InSec *is = &L->objs[i].secs[s];
      if (is->kind == K_DROP) continue;
      if (live[i][s]) { kept[is->kind] += is->size; continue; }
      gone[is->kind] += is->size;
      ndrop++;
      is->kind = K_DROP;
      if (warmc_getenv("WARMC_LINK_STRIP"))
        fprintf(stderr, "strip: %-8llu %-52s %s\n", (unsigned long long)is->size,
                is->name, L->objs[i].name);
    }
  if (warmc_getenv("WARMC_LINK_DEBUG"))
    fprintf(stderr, "link: dead-stripped %u sections: text %llu of %llu, "
                    "rodata %llu of %llu, data %llu of %llu, bss %llu of %llu\n",
            ndrop,
            (unsigned long long)gone[K_TEXT],
            (unsigned long long)(gone[K_TEXT] + kept[K_TEXT]),
            (unsigned long long)gone[K_RODATA],
            (unsigned long long)(gone[K_RODATA] + kept[K_RODATA]),
            (unsigned long long)gone[K_DATA],
            (unsigned long long)(gone[K_DATA] + kept[K_DATA]),
            (unsigned long long)gone[K_BSS],
            (unsigned long long)(gone[K_BSS] + kept[K_BSS]));

  xfree(w.st.v);
  for (uint32_t i = 0; i < L->nobjs; i++) {
    xfree(live[i]); xfree(hasdef[i]); xfree(rstart[i]); xfree(rcount[i]); xfree(rord[i]);
    if (why) xfree(w.prev[i]);
  }
  xfree(live); xfree(hasdef); xfree(rstart); xfree(rcount); xfree(rord); xfree(w.prev);
}

static uint64_t align_up(uint64_t v, uint64_t a) {
  return a <= 1 ? v : (v + a - 1) & ~(a - 1);
}

uint64_t place(Linker *L, SecKind k, uint64_t at) {
  for (uint32_t i = 0; i < L->nobjs; i++) {
    InObj *o = &L->objs[i];
    for (uint32_t s = 0; s < o->nsecs; s++) {
      InSec *is = &o->secs[s];
      if (is->kind != k || (!is->size && k != K_BSS)) continue;
      if (!is->size) continue;
      at = align_up(at, is->align);
      is->addr = at;
      at += is->size;
    }
  }
  return at;
}

static void add_rebase(Linker *L, uint64_t addr, uint64_t val) {
  if (L->nrebase == L->cap_rebase) {
    L->cap_rebase = L->cap_rebase ? L->cap_rebase * 2 : 64;
    L->rebase = xrealloc(L->rebase, L->cap_rebase * sizeof(uint64_t));
    L->rebase_val = xrealloc(L->rebase_val, L->cap_rebase * sizeof(uint64_t));
  }
  L->rebase_val[L->nrebase] = val;
  L->rebase[L->nrebase++] = addr;
}

static uint32_t got_for(Linker *L, uint32_t g) {
  if (L->gsyms[g].got_slot != NO_SLOT) return L->gsyms[g].got_slot;
  if (L->ngot == L->cap_got) {
    L->cap_got = L->cap_got ? L->cap_got * 2 : 32;
    L->got = xrealloc(L->got, L->cap_got * sizeof(uint32_t));
  }
  L->got[L->ngot] = g;
  return L->gsyms[g].got_slot = L->ngot++;
}

static uint32_t stub_for(Linker *L, uint32_t g) {
  if (L->gsyms[g].stub != NO_SLOT) return L->gsyms[g].stub;
  if (L->nstub == L->cap_stub) {
    L->cap_stub = L->cap_stub ? L->cap_stub * 2 : 32;
    L->stub = xrealloc(L->stub, L->cap_stub * sizeof(uint32_t));
  }
  got_for(L, g);
  L->stub[L->nstub] = g;
  return L->gsyms[g].stub = L->nstub++;
}

static uint64_t sym_addr(Linker *L, uint32_t oi, int32_t si, const char *what) {
  InObj *o = &L->objs[oi];
  if (si < 0 || (uint32_t)si >= o->nsyms)
    warmc_fatal("link: %s: relocation names symbol %d, which does not exist", o->name, si);
  InSym *s = &o->syms[si];
  if (s->global) {
    int32_t g = gs_find(L, s->name);
    if (g < 0) warmc_fatal("link: undefined symbol %s (referenced from %s, %s)",
                           s->name, o->name, what);

    if (L->gsyms[g].imported) return 0;
    if (!L->gsyms[g].defined)
      warmc_fatal("link: undefined symbol %s (referenced from %s, %s)",
                  s->name, o->name, what);
    return L->gsyms[g].addr;
  }
  if (s->sec < 0 || (uint32_t)s->sec >= o->nsecs)
    warmc_fatal("link: %s: local symbol %s has no section", o->name, s->name);
  return o->secs[s->sec].addr + s->value;
}

static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }

static void patch_branch26(uint8_t *p, int64_t disp, const char *nm) {
  if (disp < -(1 << 27) || disp >= (1 << 27) || (disp & 3))
    warmc_fatal("link: branch to %s is %lld bytes away, out of range", nm, (long long)disp);
  uint32_t insn = rd32(p);
  wr32(p, (insn & 0xfc000000u) | (((uint32_t)(disp >> 2)) & 0x03ffffffu));
}

static void patch_page21(uint8_t *p, uint64_t target, uint64_t pc, const char *nm) {
  int64_t d = (int64_t)(target & ~0xfffull) - (int64_t)(pc & ~0xfffull);
  if (d < -(1LL << 32) || d >= (1LL << 32))
    warmc_fatal("link: %s is more than 4GB from its ADRP", nm);
  uint64_t pages = (uint64_t)(d >> 12);
  uint32_t insn = rd32(p);
  insn &= ~((3u << 29) | (0x7ffffu << 5));
  insn |= (uint32_t)((pages & 3) << 29);
  insn |= (uint32_t)(((pages >> 2) & 0x7ffff) << 5);
  wr32(p, insn);
}

static void patch_lo12(uint8_t *p, uint64_t value, int shift_or_neg1) {
  uint32_t insn = rd32(p);
  uint32_t imm = (uint32_t)(value & 0xfff);
  int shift = shift_or_neg1;
  if (shift < 0) {
    shift = 0;
    if ((insn & 0x3b000000u) == 0x39000000u) {
      shift = (int)(insn >> 30);
      uint32_t v = (insn >> 26) & 1, opc = (insn >> 22) & 3;
      if (v && (opc & 2)) shift = 4;
    }
  }
  if (imm & ((1u << shift) - 1))
    warmc_fatal("link: unaligned low-12 relocation (%u, shift %d)", imm, shift);
  imm >>= shift;
  wr32(p, (insn & ~(0xfffu << 10)) | ((imm & 0xfff) << 10));
}

static uint8_t *sec_writable(InSec *is) {
  if (!is->data || !is->size) return NULL;
  uint8_t *c = xmalloc(is->size);
  memcpy(c, is->data, is->size);
  is->data = c;
  return c;
}

static void apply_relocs(Linker *L) {
  for (uint32_t oi = 0; oi < L->nobjs; oi++) {
    InObj *o = &L->objs[oi];
    for (uint32_t s = 0; s < o->nsecs; s++)
      if (o->secs[s].kind != K_DROP && o->secs[s].kind != K_BSS) sec_writable(&o->secs[s]);
    for (uint32_t r = 0; r < o->nrels; r++) {
      InRel *ir = &o->rels[r];
      InSec *is = &o->secs[ir->sec];
      if (is->kind == K_DROP || !is->data) continue;
      uint64_t need = 4;
      if (L->fmt == TGT_MACHO) need = ir->length == 3 ? 8u : 4u;
      else if (L->fmt == TGT_PE) need = ir->type == IMAGE_REL_AMD64_ADDR64 ? 8u : 4u;
      else if (ir->type == R_AARCH64_ABS64 || ir->type == R_AARCH64_PREL64)
        need = 8;
      if (ir->off > is->size || need > is->size - ir->off)
        warmc_fatal("link: %s: relocation past the end of %s", o->name, is->name);
      if (ir->sym < 0 || (uint32_t)ir->sym >= o->nsyms)
        warmc_fatal("link: %s: relocation names symbol %d of %u",
                    o->name, ir->sym, o->nsyms);
      uint8_t *p = (uint8_t *)is->data + ir->off;
      uint64_t pc = is->addr + ir->off;
      InSym *sy = &o->syms[ir->sym];
      const char *nm = sy->name;
      uint64_t S = sym_addr(L, oi, ir->sym, is->name);
      int64_t A = ir->addend;

      if (L->fmt == TGT_MACHO) {
        switch (ir->type) {
          case ARM64_RELOC_BRANCH26: {
            uint64_t t = S + (uint64_t)A;
            int32_t g = sy->global ? gs_find(L, sy->name) : -1;
            if (g >= 0 && L->gsyms[g].imported)
              t = L->stub_addr + (uint64_t)stub_for(L, (uint32_t)g) * 12;
            patch_branch26(p, (int64_t)t - (int64_t)pc, nm);
            break;
          }
          case ARM64_RELOC_PAGE21:
          case ARM64_RELOC_PAGEOFF12: {
            int32_t g = sy->global ? gs_find(L, sy->name) : -1;
            if (g >= 0 && L->gsyms[g].imported)
              warmc_fatal("link: %s takes the address of the imported symbol %s "
                          "directly; only calls and GOT loads of an import are "
                          "supported", o->name, nm);
            if (ir->type == ARM64_RELOC_PAGE21) patch_page21(p, S + (uint64_t)A, pc, nm);
            else patch_lo12(p, S + (uint64_t)A, -1);
            break;
          }
          case ARM64_RELOC_GOT_LOAD_PAGE21: {
            int32_t g = gs_find(L, sy->name);
            uint64_t ga = L->got_addr + (uint64_t)got_for(L, (uint32_t)g) * 8;
            patch_page21(p, ga, pc, nm); break;
          }
          case ARM64_RELOC_GOT_LOAD_PAGEOFF12: {
            int32_t g = gs_find(L, sy->name);
            uint64_t ga = L->got_addr + (uint64_t)got_for(L, (uint32_t)g) * 8;
            patch_lo12(p, ga, 3); break;
          }
          case ARM64_RELOC_UNSIGNED: {
            int32_t g = sy->global ? gs_find(L, sy->name) : -1;
            if (g >= 0 && L->gsyms[g].imported)
              warmc_fatal("link: %s stores a pointer to the imported symbol %s, "
                          "which would need a rebase this linker does not emit",
                          o->name, nm);

            if (ir->length == 3) {
              wr64(p, S + (uint64_t)A + rd64(p));
              if (is->kind == K_DATA) add_rebase(L, is->addr + ir->off, rd64(p));
              else warmc_fatal("link: %s puts an absolute pointer in %s, which "
                               "is not writable and so cannot be slid",
                               o->name, is->name);
            } else if (ir->length == 2) {
              wr32(p, (uint32_t)(S + (uint64_t)A + rd32(p)));
            } else {
              warmc_fatal("link: %s: an absolute relocation on %s is %u bytes "
                          "wide, which this linker does not write",
                          o->name, nm, 1u << ir->length);
            }
            break;
          }
          default:
            warmc_fatal("link: %s: unsupported Mach-O relocation type %u on %s",
                        o->name, ir->type, nm);
        }
        continue;
      }

      if (L->fmt == TGT_PE) {
        int32_t g = sy->global ? gs_find(L, sy->name) : -1;
        bool imp = g >= 0 && L->gsyms[g].imported;
        switch (ir->type) {
          case IMAGE_REL_AMD64_ADDR64:
            if (imp)
              warmc_fatal("link: %s stores a pointer to the imported symbol %s; "
                          "only rip-relative references to an import are supported",
                          o->name, nm);
            wr64(p, S + (uint64_t)A);
            /* a DLL may load elsewhere: the loader adds the difference */
            if (L->shared && S) add_rebase(L, is->addr + ir->off, S + (uint64_t)A);
            break;
          case IMAGE_REL_AMD64_ADDR32NB:
            if (imp)
              warmc_fatal("link: %s takes the image-relative address of the "
                          "imported symbol %s", o->name, nm);
            wr32(p, (uint32_t)(S + (uint64_t)A - L->base));
            break;
          case IMAGE_REL_AMD64_REL32:
          case IMAGE_REL_AMD64_REL32 + 1: case IMAGE_REL_AMD64_REL32 + 2:
          case IMAGE_REL_AMD64_REL32 + 3: case IMAGE_REL_AMD64_REL32 + 4:
          case IMAGE_REL_AMD64_REL32_5: {
            uint64_t t = S;
            if (imp)
              t = !strncmp(nm, "__imp_", 6)
                    ? L->got_addr + (uint64_t)L->gsyms[g].got_slot * 8
                    : L->stub_addr + (uint64_t)L->gsyms[g].stub * 6;
            wr32(p, (uint32_t)((int64_t)(t + (uint64_t)A) - (int64_t)pc));
            break;
          }
          default:
            warmc_fatal("link: %s: unsupported COFF relocation type %u on %s",
                        o->name, ir->type, nm);
        }
        continue;
      }

      switch (ir->type) {
        case R_AARCH64_CALL26: case R_AARCH64_JUMP26:
          patch_branch26(p, (int64_t)(S + (uint64_t)A) - (int64_t)pc, nm); break;
        case R_AARCH64_ADR_PREL_PG_HI21:
          patch_page21(p, S + (uint64_t)A, pc, nm); break;
        case R_AARCH64_ADD_ABS_LO12_NC:   patch_lo12(p, S + (uint64_t)A, 0); break;
        case R_AARCH64_LDST8_ABS_LO12_NC: patch_lo12(p, S + (uint64_t)A, 0); break;
        case R_AARCH64_LDST16_ABS_LO12_NC: patch_lo12(p, S + (uint64_t)A, 1); break;
        case R_AARCH64_LDST32_ABS_LO12_NC: patch_lo12(p, S + (uint64_t)A, 2); break;
        case R_AARCH64_LDST64_ABS_LO12_NC: patch_lo12(p, S + (uint64_t)A, 3); break;
        case R_AARCH64_LDST128_ABS_LO12_NC: patch_lo12(p, S + (uint64_t)A, 4); break;
        case R_AARCH64_ABS64:
          wr64(p, S + (uint64_t)A);
          /* A library is loaded anywhere: the loader adds its base to each
             such pointer, which has to sit where it may write.  A pointer
             to an undefined weak symbol stays null. */
          if (L->shared && S) {
            if (is->kind != K_DATA)
              warmc_fatal("link: %s puts an absolute pointer in %s, which a "
                          "shared library cannot relocate", o->name, is->name);
            add_rebase(L, is->addr + ir->off, S + (uint64_t)A);
          }
          break;
        case R_AARCH64_ABS32:
          if (L->shared)
            warmc_fatal("link: %s: a 32-bit absolute address of %s cannot be "
                        "relocated in a shared library", o->name, nm);
          wr32(p, (uint32_t)(S + (uint64_t)A));
          break;
        case R_AARCH64_PREL32: wr32(p, (uint32_t)((int64_t)(S + (uint64_t)A) - (int64_t)pc)); break;
        case R_AARCH64_PREL64: wr64(p, (uint64_t)((int64_t)(S + (uint64_t)A) - (int64_t)pc)); break;
        case R_AARCH64_ADR_GOT_PAGE: {
          int32_t g = gs_find(L, sy->name);
          uint64_t ga = L->got_addr + (uint64_t)got_for(L, (uint32_t)g) * 8;
          patch_page21(p, ga, pc, nm); break;
        }
        case R_AARCH64_LD64_GOT_LO12_NC: {
          int32_t g = gs_find(L, sy->name);
          uint64_t ga = L->got_addr + (uint64_t)got_for(L, (uint32_t)g) * 8;
          patch_lo12(p, ga, 3); break;
        }
        default:
          warmc_fatal("link: %s: unsupported ELF relocation type %u on %s",
                      o->name, ir->type, nm);
      }
    }
  }
}

static void reserve_indirection(Linker *L) {
  for (uint32_t oi = 0; oi < L->nobjs; oi++) {
    InObj *o = &L->objs[oi];
    for (uint32_t r = 0; r < o->nrels; r++) {
      InRel *ir = &o->rels[r];
      if (o->secs[ir->sec].kind == K_DROP) continue;
      if (ir->sym < 0 || (uint32_t)ir->sym >= o->nsyms)
        warmc_fatal("link: %s: relocation names symbol %d of %u",
                    o->name, ir->sym, o->nsyms);
      InSym *sy = &o->syms[ir->sym];
      int32_t g = sy->global ? gs_find(L, sy->name) : -1;
      if (L->fmt == TGT_MACHO) {
        if (ir->type == ARM64_RELOC_GOT_LOAD_PAGE21 ||
            ir->type == ARM64_RELOC_GOT_LOAD_PAGEOFF12) {
          if (g < 0) warmc_fatal("link: GOT relocation on non-global %s", sy->name);
          got_for(L, (uint32_t)g);
        } else if (ir->type == ARM64_RELOC_BRANCH26 && g >= 0 && L->gsyms[g].imported) {
          stub_for(L, (uint32_t)g);
        }
      } else if (L->fmt == TGT_PE) {
        if (g >= 0 && L->gsyms[g].imported) {
          if (ir->type < IMAGE_REL_AMD64_REL32 || ir->type > IMAGE_REL_AMD64_REL32_5)
            warmc_fatal("link: %s: relocation type %u on the imported symbol %s; "
                        "only rip-relative references to an import are supported",
                        o->name, ir->type, sy->name);
          if (!strncmp(sy->name, "__imp_", 6)) got_for(L, (uint32_t)g);
          else stub_for(L, (uint32_t)g);
        }
      } else if (ir->type == R_AARCH64_ADR_GOT_PAGE ||
                 ir->type == R_AARCH64_LD64_GOT_LO12_NC) {
        if (g < 0) warmc_fatal("link: GOT relocation on non-global %s", sy->name);
        got_for(L, (uint32_t)g);
      }
    }
  }
}

void place_commons(Linker *L, uint64_t *bss_end) {
  for (uint32_t i = 0; i < L->ngsyms; i++) {
    GSym *g = &L->gsyms[i];
    if (!g->defined || g->obj < 0) continue;
    InSym *s = &L->objs[g->obj].syms[g->sym];
    if (!s->common) continue;
    *bss_end = align_up(*bss_end, s->comm_align ? s->comm_align : 8);
    g->addr = *bss_end;
    *bss_end += s->comm_size;
  }
}

static void resolve_addrs(Linker *L) {
  for (uint32_t i = 0; i < L->ngsyms; i++) {
    GSym *g = &L->gsyms[i];
    if (!g->defined || g->obj < 0) continue;
    InSym *s = &L->objs[g->obj].syms[g->sym];
    if (s->common) continue;
    if (s->sec < 0) continue;
    g->addr = L->objs[g->obj].secs[s->sec].addr + s->value;
  }
}

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4
#define ELF_PAGE 0x10000ull
#define ELF_BASE 0x400000ull
/* A program has two program headers; a library five (write_elf_shared). */
#define ELF_SHARED_PHDRS 5

/* where the read-only part after the code ends: rodata, then the custom
   sections when there are any */
static uint64_t ro_end_addr(const Linker *L) {
  return L->custom_size ? L->custom_addr + L->custom_size : L->rodata_addr + L->rodata_size;
}

void emit_secs(Buf *b, Linker *L, SecKind k, uint64_t seg_addr, uint64_t seg_off) {
  for (uint32_t i = 0; i < L->nobjs; i++) {
    InObj *o = &L->objs[i];
    for (uint32_t s = 0; s < o->nsecs; s++) {
      InSec *is = &o->secs[s];
      if (is->kind != k || !is->size || !is->data) continue;
      uint64_t want = seg_off + (is->addr - seg_addr);
      while (b->len < want) buf_u8(b, 0);
      buf_bytes(b, is->data, is->size);
    }
  }
}

typedef struct { const char *name; uint64_t addr, size; bool global; } TextSym;

static int cmp_textsym(const void *a, const void *b) {
  const TextSym *x = a, *y = b;
  return x->addr < y->addr ? -1 : x->addr > y->addr;
}

/* Every symbol that names code which survived dead stripping, the local ones
   included: a compiled module's functions (wasm_func_N) and the runtime's
   static functions are all local, and without them a profiler or a debugger
   can only say an address.  Labels the assembler makes for itself are not
   functions and stay out -- on Mach-O those are the names that start with
   'l' or 'L' (a C name starts with '_'), on ELF the ".L" ones and AArch64's
   mapping symbols ($x, $d), which mark where instructions and literals start.
   Sorted by address, and each one's size runs to the next address up. */
/* -s: no names for the functions only debuggers and profilers look up,
   the module's own and the runtime's -- a Go net/http server's 5547 of
   them, 284 KB of its symbol table */
static bool g_strip_locals;
void link_set_strip(bool on) { g_strip_locals = on; }

static TextSym *text_syms(Linker *L, bool locals_only, uint32_t *count) {
  uint32_t n = 0, cap = 256;
  TextSym *v = xmalloc(cap * sizeof *v);
  for (uint32_t oi = 0; oi < L->nobjs; oi++) {
    InObj *o = &L->objs[oi];
    for (uint32_t si = 0; si < o->nsyms; si++) {
      InSym *sy = &o->syms[si];
      if (sy->common || sy->sec < 0 || (uint32_t)sy->sec >= o->nsecs) continue;
      if (o->secs[sy->sec].kind != K_TEXT) continue;
      const char *nm = sy->name;
      if (!nm[0]) continue;
      if (L->fmt == TGT_MACHO ? (nm[0] == 'l' || nm[0] == 'L')
                              : (nm[0] == '$' || (nm[0] == '.' && nm[1] == 'L')))
        continue;
      if (!sy->global && g_strip_locals) continue;
      if (sy->global) {
        if (locals_only) continue;
        int32_t g = gs_find(L, nm);
        if (g < 0 || L->gsyms[g].obj != (int32_t)oi || L->gsyms[g].sym != (int32_t)si)
          continue;
      }
      if (n == cap) { cap *= 2; v = xrealloc(v, cap * sizeof *v); }
      v[n].name = nm;
      v[n].addr = o->secs[sy->sec].addr + sy->value;
      v[n].global = sy->global;
      n++;
    }
  }
  qsort(v, n, sizeof *v, cmp_textsym);
  uint64_t end = L->text_addr + L->text_size;
  for (uint32_t i = n; i-- > 0;) {
    uint64_t next = end;
    for (uint32_t j = i + 1; j < n; j++)
      if (v[j].addr > v[i].addr) { next = v[j].addr; break; }
    v[i].size = next > v[i].addr ? next - v[i].addr : 0;
  }
  *count = n;
  return v;
}

#define SHT_PROGBITS_E 1
#define SHT_SYMTAB_E   2
#define SHT_STRTAB_E   3
#define SHF_ALLOC_E    0x2
#define SHF_EXEC_E     0x4

static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* Section headers for the function names and nothing else: the loader reads
   only the program headers, but perf, gdb and objdump find symbols through
   .symtab, and a symbol is code to them only if its section is executable,
   so .text gets a header too. */
static void elf_append_symbols(Linker *L, Buf *out) {
  uint32_t n;
  TextSym *v = text_syms(L, false, &n);

  Buf str; memset(&str, 0, sizeof str);
  buf_u8(&str, 0);
  Buf sym; memset(&sym, 0, sizeof sym);
  for (int i = 0; i < 24; i++) buf_u8(&sym, 0);
  uint32_t nlocal = 1;
  for (int pass = 0; pass < 2; pass++)
    for (uint32_t i = 0; i < n; i++) {
      if (v[i].global != (pass == 1)) continue;
      buf_u32(&sym, (uint32_t)str.len);
      buf_bytes(&str, v[i].name, strlen(v[i].name) + 1);
      buf_u8(&sym, (uint8_t)(((v[i].global ? 1 : 0) << 4) | 2));
      buf_u8(&sym, 0);
      buf_u16(&sym, 1);
      buf_u64(&sym, v[i].addr);
      buf_u64(&sym, v[i].size);
      if (!v[i].global) nlocal++;
    }
  xfree(v);

  static const char shstr[] = "\0.text\0.symtab\0.strtab\0.shstrtab\0.wasm.custom";
  while (out->len & 7) buf_u8(out, 0);
  uint64_t sym_off = out->len;
  buf_bytes(out, sym.data, sym.len);
  uint64_t str_off = out->len;
  buf_bytes(out, str.data, str.len);
  uint64_t shstr_off = out->len;
  buf_bytes(out, shstr, sizeof shstr);
  while (out->len & 7) buf_u8(out, 0);
  uint64_t sh_off = out->len;

  struct { uint32_t name, type; uint64_t flags, addr, off, size;
           uint32_t link, info; uint64_t align, entsize; } sh[6] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, SHT_PROGBITS_E, SHF_ALLOC_E | SHF_EXEC_E, L->text_addr,
      64 + 2 * 56, L->text_size, 0, 0, 16, 0 },
    { 7, SHT_SYMTAB_E, 0, 0, sym_off, sym.len, 3, nlocal, 8, 24 },
    { 15, SHT_STRTAB_E, 0, 0, str_off, str.len, 0, 0, 1, 0 },
    { 23, SHT_STRTAB_E, 0, 0, shstr_off, sizeof shstr, 0, 0, 1, 0 },
    /* the custom sections, when there are any */
    { (uint32_t)(sizeof shstr - sizeof ".wasm.custom"), SHT_PROGBITS_E, SHF_ALLOC_E,
      L->custom_addr, 64 + 2 * 56 + (L->custom_addr - L->text_addr), L->custom_size,
      0, 0, 8, 0 },
  };
  int nsh = L->custom_size ? 6 : 5;
  for (int i = 0; i < nsh; i++) {
    buf_u32(out, sh[i].name); buf_u32(out, sh[i].type);
    buf_u64(out, sh[i].flags); buf_u64(out, sh[i].addr);
    buf_u64(out, sh[i].off); buf_u64(out, sh[i].size);
    buf_u32(out, sh[i].link); buf_u32(out, sh[i].info);
    buf_u64(out, sh[i].align); buf_u64(out, sh[i].entsize);
  }
  wr64(out->data + 0x28, sh_off);
  wr16(out->data + 0x3c, (uint16_t)nsh);
  wr16(out->data + 0x3e, 4);
  xfree(sym.data); xfree(str.data);
}

static void write_elf_exec(Linker *L, const char *path, uint64_t entry) {
  Buf out; memset(&out, 0, sizeof out);

  uint64_t hdr = 64 + 2 * 56;
  uint64_t text_off = hdr;

  uint64_t ro_off  = text_off + (L->rodata_addr - L->text_addr);
  uint64_t ro_end  = text_off + (ro_end_addr(L) - L->text_addr);

  uint64_t rw_off  = align_up(ro_end, ELF_PAGE) + (L->got_addr & (ELF_PAGE - 1));

  uint64_t data_off = rw_off + (L->data_addr - L->got_addr);
  uint64_t rw_file = (L->data_addr + L->data_size) - L->got_addr;

  buf_bytes(&out, "\177ELF\2\1\1", 7);
  for (int i = 7; i < 16; i++) buf_u8(&out, 0);
  buf_u16(&out, 2);
  buf_u16(&out, 183);
  buf_u32(&out, 1);
  buf_u64(&out, entry);
  buf_u64(&out, 64);
  buf_u64(&out, 0);
  buf_u32(&out, 0);
  buf_u16(&out, 64); buf_u16(&out, 56); buf_u16(&out, 2);
  buf_u16(&out, 64); buf_u16(&out, 0); buf_u16(&out, 0);

  buf_u32(&out, PT_LOAD); buf_u32(&out, PF_R | PF_X);
  buf_u64(&out, 0); buf_u64(&out, L->text_addr - hdr); buf_u64(&out, L->text_addr - hdr);
  buf_u64(&out, ro_end); buf_u64(&out, ro_end); buf_u64(&out, ELF_PAGE);

  buf_u32(&out, PT_LOAD); buf_u32(&out, PF_R | PF_W);
  buf_u64(&out, rw_off); buf_u64(&out, L->got_addr); buf_u64(&out, L->got_addr);
  buf_u64(&out, rw_file); buf_u64(&out, (L->bss_addr + L->bss_size) - L->got_addr);
  buf_u64(&out, ELF_PAGE);

  emit_secs(&out, L, K_TEXT, L->text_addr, text_off);
  emit_secs(&out, L, K_RODATA, L->rodata_addr, ro_off);
  emit_secs(&out, L, K_CUSTOM, L->custom_addr, text_off + (L->custom_addr - L->text_addr));
  while (out.len < rw_off) buf_u8(&out, 0);
  for (uint32_t i = 0; i < L->ngot; i++) buf_u64(&out, L->gsyms[L->got[i]].addr);
  emit_secs(&out, L, K_DATA, L->data_addr, data_off);
  elf_append_symbols(L, &out);

  FILE *f = fopen(path, "wb");
  if (!f) warmc_fatal("link: cannot write %s", path);
  if (out.len && fwrite(out.data, 1, out.len, f) != out.len)
    warmc_fatal("link: short write on %s", path);
  fclose(f);
  chmod_exec(path);
  xfree(out.data);
}

#define PT_DYNAMIC   2
#define PT_GNU_STACK 0x6474e551u
#define SHT_RELA_E    4
#define SHT_HASH_E    5
#define SHT_DYNAMIC_E 6
#define SHT_DYNSYM_E  11
#define SHF_WRITE_E   0x1
#define R_AARCH64_RELATIVE 1027

static uint32_t elf_hash(const char *s) {
  uint32_t h = 0, g;
  while (*s) {
    h = (h << 4) + (uint8_t)*s++;
    g = h & 0xf0000000u;
    if (g) h ^= g >> 24;
    h &= ~g;
  }
  return h;
}

typedef struct { uint32_t name, type; uint64_t flags, addr, off, size;
                 uint32_t link, info; uint64_t align, entsize; } ElfShdr;

/* A shared object.  The program's two segments sit at base 0, and a third
   follows them for what the dynamic loader reads: the dynamic section --
   which glibc relocates in place on AArch64, so it is writable -- the
   exported symbols with their hash table, and a relative relocation for
   each absolute pointer.  There is no DT_NEEDED: the runtime asks nothing
   of libc, so the library loads into whatever process opens it. */
static void write_elf_shared(Linker *L, const char *path) {
  const uint64_t hdr = 64 + ELF_SHARED_PHDRS * 56;
  const char *soname = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
  uint64_t ro_end = ro_end_addr(L);
  uint64_t rw_off = L->got_addr;
  uint64_t rw_file = (L->data_addr + L->data_size) - L->got_addr;
  uint64_t rw_mem = (L->bss_addr + L->bss_size) - L->got_addr;

  /* the dynamic symbols: the null symbol, then each export */
  Buf dstr; memset(&dstr, 0, sizeof dstr);
  buf_u8(&dstr, 0);
  uint32_t soname_at = (uint32_t)dstr.len;
  buf_bytes(&dstr, soname, strlen(soname) + 1);
  Buf dsym; memset(&dsym, 0, sizeof dsym);
  for (int i = 0; i < 24; i++) buf_u8(&dsym, 0);
  uint32_t nsym = 1;
  for (uint32_t i = 0; i < L->ngsyms; i++) {
    GSym *g = &L->gsyms[i];
    if (!g->exported) continue;
    buf_u32(&dsym, (uint32_t)dstr.len);
    buf_bytes(&dstr, g->name, strlen(g->name) + 1);
    buf_u8(&dsym, (1 << 4) | 2);
    buf_u8(&dsym, 0);
    buf_u16(&dsym, 1);
    buf_u64(&dsym, g->addr);
    buf_u64(&dsym, 0);
    nsym++;
  }
  while (dstr.len & 7) buf_u8(&dstr, 0);

  Buf hash; memset(&hash, 0, sizeof hash);
  {
    uint32_t nb = nsym | 1;
    uint32_t *bucket = xcalloc(nb, sizeof(uint32_t));
    uint32_t *chain = xcalloc(nsym, sizeof(uint32_t));
    for (uint32_t k = 1; k < nsym; k++) {
      uint32_t nm = rd32(dsym.data + 24 * k);
      uint32_t b = elf_hash((const char *)dstr.data + nm) % nb;
      chain[k] = bucket[b];
      bucket[b] = k;
    }
    buf_u32(&hash, nb);
    buf_u32(&hash, nsym);
    for (uint32_t k = 0; k < nb; k++) buf_u32(&hash, bucket[k]);
    for (uint32_t k = 0; k < nsym; k++) buf_u32(&hash, chain[k]);
    while (hash.len & 7) buf_u8(&hash, 0);
    xfree(bucket);
    xfree(chain);
  }

  /* the relative relocations: every GOT slot, all of them local, and every
     absolute pointer apply_relocs recorded */
  Buf rela; memset(&rela, 0, sizeof rela);
  uint32_t nrela = 0;
  for (uint32_t i = 0; i < L->ngot; i++, nrela++) {
    buf_u64(&rela, L->got_addr + (uint64_t)i * 8);
    buf_u64(&rela, R_AARCH64_RELATIVE);
    buf_u64(&rela, L->gsyms[L->got[i]].addr);
  }
  for (uint32_t i = 0; i < L->nrebase; i++, nrela++) {
    buf_u64(&rela, L->rebase[i]);
    buf_u64(&rela, R_AARCH64_RELATIVE);
    buf_u64(&rela, L->rebase_val[i]);
  }

  uint64_t dyn_addr = align_up(L->got_addr + rw_mem, ELF_PAGE);
  uint64_t dyn_off = align_up(rw_off + rw_file, ELF_PAGE);
  const uint32_t ndyn = 12;
  uint64_t dynsym_addr = dyn_addr + 16 * ndyn;
  uint64_t dynstr_addr = dynsym_addr + dsym.len;
  uint64_t hash_addr = dynstr_addr + dstr.len;
  uint64_t rela_addr = hash_addr + hash.len;
  uint64_t dynseg = rela_addr + rela.len - dyn_addr;

  Buf dyn; memset(&dyn, 0, sizeof dyn);
#define DYN(tag, val) do { buf_u64(&dyn, (tag)); buf_u64(&dyn, (val)); } while (0)
  DYN(14, soname_at);
  DYN(4, hash_addr);
  DYN(5, dynstr_addr);
  DYN(6, dynsym_addr);
  DYN(10, dstr.len);
  DYN(11, 24);
  DYN(7, rela_addr);
  DYN(8, rela.len);
  DYN(9, 24);
  DYN(0x6ffffff9, nrela);
  DYN(0x6ffffffb, 0);
  DYN(0, 0);
#undef DYN
  if (dyn.len != 16 * ndyn) warmc_fatal("link: dynamic section miscounted");

  Buf out; memset(&out, 0, sizeof out);
  buf_bytes(&out, "\177ELF\2\1\1", 7);
  for (int i = 7; i < 16; i++) buf_u8(&out, 0);
  buf_u16(&out, 3);
  buf_u16(&out, 183);
  buf_u32(&out, 1);
  buf_u64(&out, 0);
  buf_u64(&out, 64);
  buf_u64(&out, 0);
  buf_u32(&out, 0);
  buf_u16(&out, 64); buf_u16(&out, 56); buf_u16(&out, ELF_SHARED_PHDRS);
  buf_u16(&out, 64); buf_u16(&out, 0); buf_u16(&out, 0);

#define PHDR(type, flags, off, addr, fsz, msz, align) do { \
    buf_u32(&out, (type)); buf_u32(&out, (flags)); buf_u64(&out, (off)); \
    buf_u64(&out, (addr)); buf_u64(&out, (addr)); buf_u64(&out, (fsz)); \
    buf_u64(&out, (msz)); buf_u64(&out, (align)); } while (0)
  PHDR(PT_LOAD, PF_R | PF_X, 0, 0, ro_end, ro_end, ELF_PAGE);
  PHDR(PT_LOAD, PF_R | PF_W, rw_off, L->got_addr, rw_file, rw_mem, ELF_PAGE);
  PHDR(PT_LOAD, PF_R | PF_W, dyn_off, dyn_addr, dynseg, dynseg, ELF_PAGE);
  PHDR(PT_DYNAMIC, PF_R | PF_W, dyn_off, dyn_addr, dyn.len, dyn.len, 8);
  PHDR(PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0, 16);
#undef PHDR
  if (out.len != hdr) warmc_fatal("link: program headers miscounted");

  emit_secs(&out, L, K_TEXT, L->text_addr, L->text_addr);
  emit_secs(&out, L, K_RODATA, L->rodata_addr, L->rodata_addr);
  emit_secs(&out, L, K_CUSTOM, L->custom_addr, L->custom_addr);
  while (out.len < rw_off) buf_u8(&out, 0);
  for (uint32_t i = 0; i < L->ngot; i++) buf_u64(&out, L->gsyms[L->got[i]].addr);
  emit_secs(&out, L, K_DATA, L->data_addr, L->data_addr - L->got_addr + rw_off);
  while (out.len < dyn_off) buf_u8(&out, 0);
  buf_bytes(&out, dyn.data, dyn.len);
  buf_bytes(&out, dsym.data, dsym.len);
  buf_bytes(&out, dstr.data, dstr.len);
  buf_bytes(&out, hash.data, hash.len);
  buf_bytes(&out, rela.data, rela.len);

  /* .symtab for debuggers and profilers, the exports global in it */
  uint32_t n;
  TextSym *v = text_syms(L, false, &n);
  Buf str; memset(&str, 0, sizeof str);
  buf_u8(&str, 0);
  Buf sym; memset(&sym, 0, sizeof sym);
  for (int i = 0; i < 24; i++) buf_u8(&sym, 0);
  uint32_t nlocal = 1;
  for (int pass = 0; pass < 2; pass++)
    for (uint32_t i = 0; i < n; i++) {
      int32_t g = v[i].global ? gs_find(L, v[i].name) : -1;
      bool global = g >= 0 && L->gsyms[g].exported;
      if (global != (pass == 1)) continue;
      buf_u32(&sym, (uint32_t)str.len);
      buf_bytes(&str, v[i].name, strlen(v[i].name) + 1);
      buf_u8(&sym, (uint8_t)(((global ? 1 : 0) << 4) | 2));
      buf_u8(&sym, 0);
      buf_u16(&sym, 1);
      buf_u64(&sym, v[i].addr);
      buf_u64(&sym, v[i].size);
      if (!global) nlocal++;
    }
  xfree(v);

  static const char shstr[] = "\0.text\0.dynamic\0.dynsym\0.dynstr\0.hash\0"
                              ".rela.dyn\0.symtab\0.strtab\0.shstrtab\0.wasm.custom";
  while (out.len & 7) buf_u8(&out, 0);
  uint64_t sym_off = out.len;
  buf_bytes(&out, sym.data, sym.len);
  uint64_t str_off = out.len;
  buf_bytes(&out, str.data, str.len);
  uint64_t shstr_off = out.len;
  buf_bytes(&out, shstr, sizeof shstr);
  while (out.len & 7) buf_u8(&out, 0);
  uint64_t sh_off = out.len;
  uint64_t dpos = dyn_off;
  ElfShdr sh[11] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, SHT_PROGBITS_E, SHF_ALLOC_E | SHF_EXEC_E, L->text_addr, L->text_addr,
      L->text_size, 0, 0, 16, 0 },
    { 7, SHT_DYNAMIC_E, SHF_ALLOC_E | SHF_WRITE_E, dyn_addr, dpos, dyn.len, 4, 0, 8, 16 },
    { 16, SHT_DYNSYM_E, SHF_ALLOC_E, dynsym_addr, dpos + (dynsym_addr - dyn_addr),
      dsym.len, 4, 1, 8, 24 },
    { 24, SHT_STRTAB_E, SHF_ALLOC_E, dynstr_addr, dpos + (dynstr_addr - dyn_addr),
      dstr.len, 0, 0, 1, 0 },
    { 32, SHT_HASH_E, SHF_ALLOC_E, hash_addr, dpos + (hash_addr - dyn_addr),
      hash.len, 3, 0, 8, 4 },
    { 38, SHT_RELA_E, SHF_ALLOC_E, rela_addr, dpos + (rela_addr - dyn_addr),
      rela.len, 3, 0, 8, 24 },
    { 48, SHT_SYMTAB_E, 0, 0, sym_off, sym.len, 8, nlocal, 8, 24 },
    { 56, SHT_STRTAB_E, 0, 0, str_off, str.len, 0, 0, 1, 0 },
    { 64, SHT_STRTAB_E, 0, 0, shstr_off, sizeof shstr, 0, 0, 1, 0 },
    /* the custom sections, when there are any */
    { (uint32_t)(sizeof shstr - sizeof ".wasm.custom"), SHT_PROGBITS_E, SHF_ALLOC_E,
      L->custom_addr, L->custom_addr, L->custom_size, 0, 0, 8, 0 },
  };
  int nsh = L->custom_size ? 11 : 10;
  for (int i = 0; i < nsh; i++) {
    buf_u32(&out, sh[i].name); buf_u32(&out, sh[i].type);
    buf_u64(&out, sh[i].flags); buf_u64(&out, sh[i].addr);
    buf_u64(&out, sh[i].off); buf_u64(&out, sh[i].size);
    buf_u32(&out, sh[i].link); buf_u32(&out, sh[i].info);
    buf_u64(&out, sh[i].align); buf_u64(&out, sh[i].entsize);
  }
  wr64(out.data + 0x28, sh_off);
  wr16(out.data + 0x3c, (uint16_t)nsh);
  wr16(out.data + 0x3e, 9);

  FILE *f = fopen(path, "wb");
  if (!f) warmc_fatal("link: cannot write %s", path);
  if (out.len && fwrite(out.data, 1, out.len, f) != out.len)
    warmc_fatal("link: short write on %s", path);
  fclose(f);
  chmod_exec(path);
  xfree(out.data); xfree(dyn.data); xfree(dsym.data); xfree(dstr.data);
  xfree(hash.data); xfree(rela.data); xfree(sym.data); xfree(str.data);
}

static void load_main(Linker *L, const char *objpath) {
  size_t len; uint8_t *buf = read_file(objpath, &len);
  InObj *o = obj_new(L, objpath, buf, len);
  if (L->fmt == TGT_MACHO) read_macho_obj(o);
  else if (L->fmt == TGT_PE) read_coff_obj(o);
  else read_elf_obj(o);
  collect_globals(L, 0);
  for (uint32_t i = 0; i < o->nsyms; i++)
    if (o->syms[i].global && o->syms[i].sec < 0 && !o->syms[i].common && o->syms[i].name[0])
      gs_intern(L, o->syms[i].name);
}

static void layout_linux(Linker *L) {
  uint64_t hdr = 64 + (L->shared ? ELF_SHARED_PHDRS : 2) * 56;
  L->base = L->shared ? 0 : ELF_BASE;
  L->text_addr = L->base + hdr;
  uint64_t at = place(L, K_TEXT, L->text_addr);
  L->text_size = at - L->text_addr;
  at = align_up(at, 16);
  L->rodata_addr = at;
  at = place(L, K_RODATA, at);
  L->rodata_size = at - L->rodata_addr;
  at = align_up(at, 8);
  L->custom_addr = at;
  at = place(L, K_CUSTOM, at);
  L->custom_size = at - L->custom_addr;

  at = align_up(at, ELF_PAGE);
  L->got_addr = at;
  at += (uint64_t)L->ngot * 8;
  at = align_up(at, 16);
  L->data_addr = at;
  at = place(L, K_DATA, at);
  L->data_size = at - L->data_addr;
  at = align_up(at, 16);
  L->bss_addr = at;
  at = place(L, K_BSS, at);
  place_commons(L, &at);
  L->bss_size = at - L->bss_addr;
}

static void link_image(TargetFmt fmt, const char *out, const char *objpath,
                       const char *wasi, const char *rt, const char *entry,
                       bool strip, char *const *exports, uint32_t nexports) {
  bool macho = fmt == TGT_MACHO;
  Linker L;
  memset(&L, 0, sizeof L);
  L.fmt = fmt;
  L.shared = exports != NULL;
  if (fmt == TGT_PE) L.base = L.shared ? PE_DLL_BASE : PE_BASE;
  char install[4096];
  if (L.shared) {
    const char *b = strrchr(out, '/');
    snprintf(install, sizeof install, "@rpath/%s", b ? b + 1 : out);
    L.install_name = install;
  }

  bool tm = warmc_getenv("WARMC_TIME") != NULL;
  double t0 = tm ? lk_ms() : 0, t_read, t_pull, t_lay, t_rel;
  load_main(&L, objpath);

  gs_intern(&L, entry);
  void (*rd)(InObj *) = macho ? read_macho_obj
                      : fmt == TGT_PE ? read_coff_obj : read_elf_obj;
  Archive ars[2];
  uint32_t nars = 0;
  if (wasi) ar_load(&ars[nars++], wasi);
  ar_load(&ars[nars++], rt);
  if (tm) { t_read = lk_ms() - t0; t0 = lk_ms(); }
  pull_archives(&L, ars, nars, rd);
  if (tm) { t_pull = lk_ms() - t0; t0 = lk_ms(); }

  if (fmt == TGT_PE)
    for (uint32_t i = 0; i < L.ngsyms; i++) {
      GSym *g = &L.gsyms[i];
      if (g->defined) continue;
      if (!strcmp(g->name, "__ImageBase")) {
        g->defined = true;
        g->addr = L.base;
      } else if (pe_dll_for(g->name)) {
        g->imported = true;
      }
    }

  if (!macho && any_undefined(&L)) {

    uint32_t nu = 0;
    for (uint32_t i = 0; i < L.ngsyms; i++)
      if (!L.gsyms[i].defined) nu++;
    for (uint32_t i = 0, shown = 0; i < L.ngsyms && shown < 8; i++) {
      if (L.gsyms[i].defined) continue;
      fprintf(stderr, "warmc: link: undefined symbol %s\n", L.gsyms[i].name);
      shown++;
    }
    if (nu > 8) fprintf(stderr, "warmc: link: and %u more\n", nu - 8);

    /* A Linux WASI host carries rt/linuxposix.c for the POSIX calls it makes,
       so one that leaves them undefined is an archive built before it was. */
    if (wasi)
      fprintf(stderr,
              "warmc: the WASI host %s should define every POSIX call it "
              "makes (rt/linuxposix.c);\nwarmc: an archive built before "
              "that file existed does not -- run `make` to rebuild it\n",
              wasi);
    warmc_fatal("link: %u undefined symbol%s", nu, nu == 1 ? "" : "s");
  }

  for (uint32_t i = 0; i < L.ngsyms; i++)
    if (!L.gsyms[i].defined) L.gsyms[i].imported = true;

  for (uint32_t i = 0; i < nexports; i++) {
    char name[1024];
    snprintf(name, sizeof name, "%s%s", macho ? "_" : "", exports[i]);
    int32_t g = gs_find(&L, name);
    if (g < 0 || !L.gsyms[g].defined || L.gsyms[g].imported)
      warmc_fatal("link: the library's export %s is not defined", exports[i]);
    L.gsyms[g].exported = true;
  }

  if (strip) dead_strip(&L, entry);
  reserve_indirection(&L);
  if (warmc_getenv("WARMC_LINK_DEBUG")) {
    uint32_t nimp = 0;
    for (uint32_t i = 0; i < L.ngsyms; i++) if (L.gsyms[i].imported) nimp++;
    fprintf(stderr, "link: %u objects, %u globals, %u imported, %u got, %u stubs\n",
            L.nobjs, L.ngsyms, nimp, L.ngot, L.nstub);
    for (uint32_t i = 0; i < L.nobjs; i++)
      fprintf(stderr, "link:   obj %s\n", L.objs[i].name);
    for (uint32_t i = 0; i < L.ngsyms; i++)
      if (L.gsyms[i].imported)
        fprintf(stderr, "link:   import %-24s got=%u stub=%u\n", L.gsyms[i].name,
                L.gsyms[i].got_slot, L.gsyms[i].stub);
  }
  if (macho) layout_macos(&L);
  else if (fmt == TGT_PE) layout_windows(&L);
  else layout_linux(&L);
  resolve_addrs(&L);
  if (tm) { t_lay = lk_ms() - t0; t0 = lk_ms(); }

  if (warmc_getenv("WARMC_LINK_MAP")) {
    for (uint32_t i = 0; i < L.ngsyms; i++)
      if (L.gsyms[i].defined)
        fprintf(stderr, "map %016llx %s\n",
                (unsigned long long)L.gsyms[i].addr, L.gsyms[i].name);
  }
  if (warmc_getenv("WARMC_LINK_DEBUG"))
    fprintf(stderr, "link: text %llx+%llx rodata %llx+%llx got %llx+%x "
            "data %llx+%llx bss %llx+%llx\n",
            (unsigned long long)L.text_addr, (unsigned long long)L.text_size,
            (unsigned long long)L.rodata_addr, (unsigned long long)L.rodata_size,
            (unsigned long long)L.got_addr, L.ngot * 8,
            (unsigned long long)L.data_addr, (unsigned long long)L.data_size,
            (unsigned long long)L.bss_addr, (unsigned long long)L.bss_size);

  if (warmc_getenv("WARMC_LINK_SECS"))
    for (uint32_t i = 0; i < L.nobjs; i++)
      for (uint32_t s = 0; s < L.objs[i].nsecs; s++) {
        InSec *is = &L.objs[i].secs[s];
        if (is->kind == K_DROP || (!is->size && is->kind != K_BSS)) continue;
        fprintf(stderr, "link:   sec %016llx+%-6llx a%-3llu k%d %-20s %s\n",
                (unsigned long long)is->addr, (unsigned long long)is->size,
                (unsigned long long)is->align, (int)is->kind, is->name,
                L.objs[i].name);
      }
  if (warmc_getenv("WARMC_LINK_DEBUG"))
    for (uint32_t i = 0; i < L.ngot; i++)
      fprintf(stderr, "link:   got[%u] +0x%02x %016llx %s%s\n", i, i * 8,
              (unsigned long long)L.gsyms[L.got[i]].addr, L.gsyms[L.got[i]].name,
              L.gsyms[L.got[i]].imported ? " (imported)" : "");
  int32_t e = gs_find(&L, entry);
  if (!L.shared && (e < 0 || !L.gsyms[e].defined))
    warmc_fatal("link: no entry point %s", entry);
  apply_relocs(&L);
  if (warmc_getenv("WARMC_LINK_DEBUG")) {
    uint32_t own = 0;
    for (uint32_t i = 0; i < L.ngot; i++) if (!L.gsyms[L.got[i]].imported) own++;
    fprintf(stderr, "link: %u rebases from data + %u from the got\n", L.nrebase, own);
    if (warmc_getenv("WARMC_LINK_REBASE"))
      for (uint32_t i = 0; i < L.nrebase; i++)
        fprintf(stderr, "reb %016llx\n", (unsigned long long)L.rebase[i]);
  }

  if (tm) { t_rel = lk_ms() - t0; t0 = lk_ms(); }
  if (macho) write_macho(&L, out, L.shared ? 0 : L.gsyms[e].addr);
  else if (fmt == TGT_PE) write_pe_exec(&L, out, L.shared ? 0 : L.gsyms[e].addr);
  else if (L.shared) write_elf_shared(&L, out);
  else write_elf_exec(&L, out, L.gsyms[e].addr);
  if (tm)
    fprintf(stderr, "link time: read %.1f pull %.1f layout %.1f reloc %.1f write %.1f\n",
            t_read, t_pull, t_lay, t_rel, lk_ms() - t0);
}

void warmc_link_exec(TargetFmt fmt, const char *out, const char *objpath,
                     const char *wasi, const char *rt, const char *entry,
                     bool strip) {
  link_image(fmt, out, objpath, wasi, rt, entry, strip, NULL, 0);
}

void warmc_link_shared(TargetFmt fmt, const char *out, const char *objpath,
                       const char *rt, char *const *exports, uint32_t nexports,
                       bool strip) {
  link_image(fmt, out, objpath, NULL, rt,
             fmt == TGT_MACHO ? "_warmc_lib_thunk" : "warmc_lib_thunk", strip,
             exports, nexports);
}

#if defined(__APPLE__)

#include <CommonCrypto/CommonDigest.h>
#define SHA_HW 1
#endif

typedef struct { uint32_t h[8]; uint64_t n; uint8_t buf[64]; size_t nbuf; } Sha256;

static const uint32_t SHA_K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha_block(Sha256 *s, const uint8_t *p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
           ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ror32(w[i-15],7) ^ ror32(w[i-15],18) ^ (w[i-15] >> 3);
    uint32_t s1 = ror32(w[i-2],17) ^ ror32(w[i-2],19) ^ (w[i-2] >> 10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ror32(e,6) ^ ror32(e,11) ^ ror32(e,25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + SHA_K[i] + w[i];
    uint32_t S0 = ror32(a,2) ^ ror32(a,13) ^ ror32(a,22);
    uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + mj;
    h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
  }
  s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d;
  s->h[4]+=e; s->h[5]+=f; s->h[6]+=g; s->h[7]+=h;
}

__attribute__((unused))
static void sha_init(Sha256 *s) {
  static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                 0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memcpy(s->h, iv, sizeof iv); s->n = 0; s->nbuf = 0;
}
__attribute__((unused))
static void sha_update(Sha256 *s, const void *p, size_t n) {
  const uint8_t *q = p; s->n += n;
  while (n) {
    size_t k = 64 - s->nbuf; if (k > n) k = n;
    memcpy(s->buf + s->nbuf, q, k);
    s->nbuf += k; q += k; n -= k;
    if (s->nbuf == 64) { sha_block(s, s->buf); s->nbuf = 0; }
  }
}
__attribute__((unused))
static void sha_final(Sha256 *s, uint8_t out[32]) {
  uint64_t bits = s->n * 8;
  uint8_t pad = 0x80;
  sha_update(s, &pad, 1);
  uint8_t z = 0;
  while (s->nbuf != 56) sha_update(s, &z, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8*i));
  sha_update(s, len, 8);
  for (int i = 0; i < 8; i++) {
    out[i*4]   = (uint8_t)(s->h[i] >> 24); out[i*4+1] = (uint8_t)(s->h[i] >> 16);
    out[i*4+2] = (uint8_t)(s->h[i] >> 8);  out[i*4+3] = (uint8_t)s->h[i];
  }
}

#define MACHO_BASE     0x100000000ull
#define MACHO_PAGE     0x4000ull
#define STUB_SIZE      12

#define MACHO_DYLIB "/usr/lib/libSystem.B.dylib"
#define MACHO_DYLD  "/usr/lib/dyld"

/* whether an input section of kind k is in what is linked */
static bool has_kind(const Linker *L, SecKind k) {
  for (uint32_t i = 0; i < L->nobjs; i++)
    for (uint32_t s = 0; s < L->objs[i].nsecs; s++)
      if (L->objs[i].secs[s].kind == k && L->objs[i].secs[s].size) return true;
  return false;
}

static uint64_t macho_hdr_size(const Linker *L) {
  uint64_t n = 32;
  n += 4 * 72;
  n += (has_kind(L, K_CUSTOM) ? 7 : 6) * 80;
  n += 48 + 24 + 80 + 24 + 24 + 24 + 16;

  n += align_up(12 + strlen(MACHO_DYLD) + 1, 8);
  n += align_up(24 + strlen(MACHO_DYLIB) + 1, 8);
  /* A library names itself in LC_ID_DYLIB; it has no __PAGEZERO, dyld or
     LC_MAIN, so what they would take is spare. */
  if (L->shared) n += align_up(24 + strlen(L->install_name) + 1, 8);
  return align_up(n, 16);
}

static void layout_macos(Linker *L) {
  if (!L->shared) L->base = MACHO_BASE;
  L->text_addr = L->base + macho_hdr_size(L);
  uint64_t at = place(L, K_TEXT, L->text_addr);
  at = align_up(at, 4);
  L->stub_addr = at;
  at += (uint64_t)L->nstub * STUB_SIZE;
  L->text_size = at - L->text_addr;
  at = align_up(at, 16);
  L->rodata_addr = at;
  at = place(L, K_RODATA, at);
  L->rodata_size = at - L->rodata_addr;
  at = align_up(at, 8);
  L->custom_addr = at;
  at = place(L, K_CUSTOM, at);
  L->custom_size = at - L->custom_addr;

  at = align_up(at, MACHO_PAGE);
  L->got_addr = at;
  at += (uint64_t)L->ngot * 8;
  at = align_up(at, 16);
  L->data_addr = at;
  at = place(L, K_DATA, at);
  L->data_size = at - L->data_addr;
  at = align_up(at, 16);
  L->bss_addr = at;
  at = place(L, K_BSS, at);
  place_commons(L, &at);
  L->bss_size = at - L->bss_addr;
}

static void be32(Buf *b, uint32_t v) {
  buf_u8(b, (uint8_t)(v >> 24)); buf_u8(b, (uint8_t)(v >> 16));
  buf_u8(b, (uint8_t)(v >> 8));  buf_u8(b, (uint8_t)v);
}
static void be64(Buf *b, uint64_t v) { be32(b, (uint32_t)(v >> 32)); be32(b, (uint32_t)v); }

static void uleb(Buf *b, uint64_t v) {
  do { uint8_t byte = v & 0x7f; v >>= 7; if (v) byte |= 0x80; buf_u8(b, byte); } while (v);
}

static void lc_str16(Buf *b, const char *s) {
  char t[16]; memset(t, 0, 16);
  snprintf(t, 16, "%s", s);
  buf_bytes(b, t, 16);
}

#define LC_REQ_DYLD 0x80000000u
#define LC_SYMTAB_C       0x02
#define LC_UUID_C         0x1b
#define LC_DYSYMTAB       0x0b
#define LC_LOAD_DYLIB     0x0c
#define LC_LOAD_DYLINKER  0x0e
#define LC_CODE_SIGNATURE 0x1d
#define LC_DYLD_INFO_ONLY (0x22 | LC_REQ_DYLD)
#define LC_MAIN           (0x28 | LC_REQ_DYLD)
#define LC_BUILD_VERSION  0x32

#define MH_EXECUTE  2
#define MH_DYLIB    6
#define LC_ID_DYLIB 0x0d
#define MH_NO_REEXPORTED_DYLIBS 0x100000
#define MH_NOUNDEFS 0x1
#define MH_DYLDLINK 0x4
#define MH_TWOLEVEL 0x80
#define MH_PIE      0x200000

#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXEC 4

#define BIND_OPCODE_DONE                          0x00
#define BIND_OPCODE_SET_DYLIB_ORDINAL_IMM         0x10
#define BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM 0x40
#define BIND_OPCODE_SET_TYPE_IMM                  0x50
#define BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB   0x70
#define BIND_OPCODE_DO_BIND                       0x90
#define BIND_TYPE_POINTER 1

static void seg_cmd(Buf *b, const char *name, uint64_t vmaddr, uint64_t vmsize,
                    uint64_t fileoff, uint64_t filesize, uint32_t maxprot,
                    uint32_t initprot, uint32_t nsects) {
  buf_u32(b, LC_SEGMENT_64);
  buf_u32(b, 72 + 80 * nsects);
  lc_str16(b, name);
  buf_u64(b, vmaddr); buf_u64(b, vmsize);
  buf_u64(b, fileoff); buf_u64(b, filesize);
  buf_u32(b, maxprot); buf_u32(b, initprot);
  buf_u32(b, nsects); buf_u32(b, 0);
}

static void sect_cmd(Buf *b, const char *sect, const char *seg, uint64_t addr,
                     uint64_t size, uint32_t off, uint32_t align, uint32_t flags) {
  lc_str16(b, sect); lc_str16(b, seg);
  buf_u64(b, addr); buf_u64(b, size);
  buf_u32(b, off); buf_u32(b, align);
  buf_u32(b, 0); buf_u32(b, 0);
  buf_u32(b, flags);
  buf_u32(b, 0); buf_u32(b, 0); buf_u32(b, 0);
}

/* A library's export trie, what dlsym and a linker read its symbols from:
   a radix tree over the names, each node its terminal information -- the
   address, for a name that ends there -- and its edges, each a run of the
   name and the ULEB128 offset of the node it leads to.  The offsets size
   the nodes they sit in, so the layout is redone until it holds still. */
typedef struct TNode {
  bool term;
  uint64_t addr;
  uint32_t nkids, off;
  struct TKid { const char *lab; uint32_t len; struct TNode *to; } *kids;
} TNode;

static uint32_t uleb_len(uint64_t v) {
  uint32_t n = 1;
  while (v >= 0x80) { v >>= 7; n++; }
  return n;
}

/* names[lo, hi) share their first depth characters and are sorted. */
static TNode *trie_build(char **names, const uint64_t *addrs, uint32_t lo,
                         uint32_t hi, uint32_t depth) {
  TNode *t = xcalloc(1, sizeof *t);
  t->kids = xcalloc(hi - lo + 1, sizeof *t->kids);
  uint32_t i = lo;
  if (i < hi && !names[i][depth]) { t->term = true; t->addr = addrs[i]; i++; }
  while (i < hi) {
    char c = names[i][depth];
    uint32_t j = i;
    while (j < hi && names[j][depth] == c) j++;
    uint32_t len = 1;
    for (;;) {
      char d = names[i][depth + len];
      bool same = d != 0;
      for (uint32_t k = i + 1; k < j && same; k++)
        if (names[k][depth + len] != d) same = false;
      if (!same) break;
      len++;
    }
    struct TKid *kd = &t->kids[t->nkids++];
    kd->lab = names[i] + depth;
    kd->len = len;
    kd->to = trie_build(names, addrs, i, j, depth + len);
    i = j;
  }
  return t;
}

static void trie_order(TNode *t, TNode ***v, uint32_t *n, uint32_t *cap) {
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 16; *v = xrealloc(*v, *cap * sizeof **v); }
  (*v)[(*n)++] = t;
  for (uint32_t i = 0; i < t->nkids; i++) trie_order(t->kids[i].to, v, n, cap);
}

static uint32_t trie_term_len(const TNode *t) {
  return uleb_len(0) + uleb_len(t->addr);
}

static void export_trie(Linker *L, Buf *out) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < L->ngsyms; i++) if (L->gsyms[i].exported) n++;
  if (!n) return;
  char **names = xmalloc(n * sizeof(char *));
  uint64_t *addrs = xmalloc(n * sizeof(uint64_t));
  uint32_t k = 0;
  for (uint32_t i = 0; i < L->ngsyms; i++)
    if (L->gsyms[i].exported) names[k++] = L->gsyms[i].name;
  for (uint32_t i = 1; i < n; i++) {
    char *v = names[i]; uint32_t j = i;
    while (j && strcmp(names[j - 1], v) > 0) { names[j] = names[j - 1]; j--; }
    names[j] = v;
  }
  for (uint32_t i = 0; i < n; i++)
    addrs[i] = L->gsyms[gs_find(L, names[i])].addr - L->base;

  TNode *root = trie_build(names, addrs, 0, n, 0);
  TNode **order = NULL;
  uint32_t nn = 0, cap = 0;
  trie_order(root, &order, &nn, &cap);
  for (bool moved = true; moved;) {
    moved = false;
    uint32_t off = 0;
    for (uint32_t i = 0; i < nn; i++) {
      TNode *t = order[i];
      if (t->off != off) { t->off = off; moved = true; }
      off += t->term ? uleb_len(trie_term_len(t)) + trie_term_len(t) : 1;
      off += 1;
      for (uint32_t c = 0; c < t->nkids; c++)
        off += t->kids[c].len + 1 + uleb_len(t->kids[c].to->off);
    }
  }
  for (uint32_t i = 0; i < nn; i++) {
    TNode *t = order[i];
    if (t->term) {
      uleb(out, trie_term_len(t));
      uleb(out, 0);
      uleb(out, t->addr);
    } else {
      buf_u8(out, 0);
    }
    buf_u8(out, (uint8_t)t->nkids);
    for (uint32_t c = 0; c < t->nkids; c++) {
      buf_bytes(out, t->kids[c].lab, t->kids[c].len);
      buf_u8(out, 0);
      uleb(out, t->kids[c].to->off);
    }
  }
  while (out->len & 7) buf_u8(out, 0);
  for (uint32_t i = 0; i < nn; i++) { xfree(order[i]->kids); xfree(order[i]); }
  xfree(order);
  xfree(names);
  xfree(addrs);
}

/* The ordinal of the section an address is in, as write_macho orders them:
   __text, __stubs, __const, __wasm_custom when there is one, __got,
   __data, __bss. */
static uint32_t macho_sect_of(Linker *L, uint64_t a) {
  uint32_t c = L->custom_size ? 1 : 0;
  if (a < L->stub_addr) return 1;
  if (a < L->rodata_addr) return 2;
  if (a < (c ? L->custom_addr : L->got_addr)) return 3;
  if (a < L->got_addr) return 4;
  if (a < L->data_addr) return 4 + c;
  if (a < L->bss_addr) return 5 + c;
  return 6 + c;
}

/* A global whose section the link dropped as unreachable: no address. */
static bool gs_dropped(const Linker *L, const GSym *g) {
  if (g->obj < 0) return false;
  const InSym *s = &L->objs[g->obj].syms[g->sym];
  return !s->common && s->sec >= 0 && L->objs[g->obj].secs[s->sec].kind == K_DROP;
}

static void write_macho(Linker *L, const char *path, uint64_t entry) {
  const uint64_t base = L->base;
  const char *dylib = MACHO_DYLIB;
  const char *dyld  = MACHO_DYLD;
  const char *ident = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;

  uint64_t text_end = align_up(ro_end_addr(L), 16);
  uint64_t text_fsz = text_end - base;
  uint64_t data_foff = L->got_addr - base;
  uint64_t data_fsz  = (L->data_addr + L->data_size) - L->got_addr;
  uint64_t data_vsz  = (L->bss_addr + L->bss_size) - L->got_addr;
  uint64_t link_foff = align_up(data_foff + data_fsz, MACHO_PAGE);
  uint64_t link_vaddr = align_up(L->got_addr + data_vsz, MACHO_PAGE);

  /* __DATA's segment index: a program's __PAGEZERO comes first. */
  uint8_t dseg = L->shared ? 1 : 2;
  Buf reb; memset(&reb, 0, sizeof reb);
  {
    uint64_t *all = xmalloc(((size_t)L->ngot + L->nrebase + 1) * sizeof(uint64_t));
    uint32_t n = 0;
    for (uint32_t i = 0; i < L->ngot; i++)
      if (!L->gsyms[L->got[i]].imported) all[n++] = L->got_addr + (uint64_t)i * 8;
    for (uint32_t i = 0; i < L->nrebase; i++) all[n++] = L->rebase[i];
    for (uint32_t i = 1; i < n; i++) {
      uint64_t v = all[i]; uint32_t j = i;
      while (j && all[j-1] > v) { all[j] = all[j-1]; j--; }
      all[j] = v;
    }

    if (n) {
      buf_u8(&reb, 0x10 | 1);
      buf_u8(&reb, 0x20 | dseg);
      uleb(&reb, all[0] - L->got_addr);
      uint32_t i = 0;
      while (i < n) {

        uint32_t run = 1;
        while (i + run < n && all[i + run] == all[i + run - 1] + 8) run++;
        buf_u8(&reb, 0x60); uleb(&reb, run);
        i += run;
        if (i < n) { buf_u8(&reb, 0x30); uleb(&reb, all[i] - all[i-1] - 8); }
      }
    }
    buf_u8(&reb, 0x00);
    while (reb.len & 7) buf_u8(&reb, 0);
    xfree(all);
  }

  Buf bind; memset(&bind, 0, sizeof bind);
  for (uint32_t i = 0; i < L->ngot; i++) {
    GSym *g = &L->gsyms[L->got[i]];
    if (!g->imported) continue;
    buf_u8(&bind, BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | 1);
    buf_u8(&bind, BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM | 0);
    buf_bytes(&bind, g->name, strlen(g->name) + 1);
    buf_u8(&bind, BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER);
    buf_u8(&bind, BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | dseg);
    uleb(&bind, (uint64_t)i * 8);
    buf_u8(&bind, BIND_OPCODE_DO_BIND);
  }
  buf_u8(&bind, BIND_OPCODE_DONE);
  while (bind.len & 7) buf_u8(&bind, 0);

  Buf exp; memset(&exp, 0, sizeof exp);
  if (L->shared) export_trie(L, &exp);

  Buf strt; memset(&strt, 0, sizeof strt);
  buf_u8(&strt, 0);
  Buf symt; memset(&symt, 0, sizeof symt);
  uint32_t nlocal = 0, ndef = 0, nundef = 0;
  {
    uint32_t n;
    TextSym *v = text_syms(L, true, &n);
    for (uint32_t i = 0; i < n; i++) {
      buf_u32(&symt, (uint32_t)strt.len);
      buf_bytes(&strt, v[i].name, strlen(v[i].name) + 1);
      buf_u8(&symt, N_SECT);
      buf_u8(&symt, (uint8_t)macho_sect_of(L, v[i].addr));
      buf_u16(&symt, 0);
      buf_u64(&symt, v[i].addr);
      nlocal++;
    }
    xfree(v);
  }
  /* A library keeps the runtime's globals to itself: local, like its
     functions, so that nothing outside binds to them. */
  if (L->shared)
    for (uint32_t i = 0; i < L->ngsyms; i++) {
      GSym *g = &L->gsyms[i];
      if (!g->defined || g->imported || g->exported || gs_dropped(L, g)) continue;
      buf_u32(&symt, (uint32_t)strt.len);
      buf_bytes(&strt, g->name, strlen(g->name) + 1);
      buf_u8(&symt, N_SECT);
      buf_u8(&symt, (uint8_t)macho_sect_of(L, g->addr));
      buf_u16(&symt, 0);
      buf_u64(&symt, g->addr);
      nlocal++;
    }
  for (uint32_t i = 0; i < L->ngsyms; i++) {
    GSym *g = &L->gsyms[i];
    if (!g->defined || g->imported || (L->shared && !g->exported) || gs_dropped(L, g)) continue;
    uint32_t sx = (uint32_t)strt.len;
    buf_bytes(&strt, g->name, strlen(g->name) + 1);
    buf_u32(&symt, sx);
    buf_u8(&symt, N_SECT | N_EXT);
    buf_u8(&symt, (uint8_t)macho_sect_of(L, g->addr));
    buf_u16(&symt, 0);
    buf_u64(&symt, g->addr);
    ndef++;
  }
  for (uint32_t i = 0; i < L->ngsyms; i++) {
    GSym *g = &L->gsyms[i];
    if (!g->imported) continue;
    uint32_t sx = (uint32_t)strt.len;
    buf_bytes(&strt, g->name, strlen(g->name) + 1);
    buf_u32(&symt, sx);
    buf_u8(&symt, N_UNDF | N_EXT);
    buf_u8(&symt, 0);
    buf_u16(&symt, 1 << 8);
    buf_u64(&symt, 0);
    nundef++;
  }
  while (strt.len & 7) buf_u8(&strt, 0);

  uint64_t reb_off  = link_foff;
  uint64_t bind_off = reb_off + reb.len;
  uint64_t exp_off  = bind_off + bind.len;
  uint64_t sym_off  = exp_off + exp.len;
  uint64_t str_off  = sym_off + symt.len;
  uint64_t sig_off  = align_up(str_off + strt.len, 16);

  Buf lc; memset(&lc, 0, sizeof lc);
  uint32_t ncmds = 0;

  if (!L->shared) { seg_cmd(&lc, "__PAGEZERO", 0, base, 0, 0, 0, 0, 0); ncmds++; }

  seg_cmd(&lc, "__TEXT", base, align_up(text_fsz, MACHO_PAGE), 0, text_fsz,
          VM_PROT_READ | VM_PROT_EXEC, VM_PROT_READ | VM_PROT_EXEC,
          L->custom_size ? 4 : 3); ncmds++;
  sect_cmd(&lc, "__text", "__TEXT", L->text_addr, L->stub_addr - L->text_addr,
           (uint32_t)(L->text_addr - base), 2,
           S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS);
  sect_cmd(&lc, "__stubs", "__TEXT", L->stub_addr, (uint64_t)L->nstub * STUB_SIZE,
           (uint32_t)(L->stub_addr - base), 2,
           S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS);
  sect_cmd(&lc, "__const", "__TEXT", L->rodata_addr, L->rodata_size,
           (uint32_t)(L->rodata_addr - base), 4, 0);
  if (L->custom_size)
    sect_cmd(&lc, "__wasm_custom", "__TEXT", L->custom_addr, L->custom_size,
             (uint32_t)(L->custom_addr - base), 3, 0);

  seg_cmd(&lc, "__DATA", L->got_addr, align_up(data_vsz, MACHO_PAGE), data_foff, data_fsz,
          VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, 3); ncmds++;
  sect_cmd(&lc, "__got", "__DATA", L->got_addr, (uint64_t)L->ngot * 8,
           (uint32_t)data_foff, 3, 0);
  sect_cmd(&lc, "__data", "__DATA", L->data_addr, L->data_size,
           (uint32_t)(L->data_addr - base), 4, 0);
  sect_cmd(&lc, "__bss", "__DATA", L->bss_addr, L->bss_size, 0, 4, S_ZEROFILL);

  uint32_t np0 = (uint32_t)((sig_off + 4095) / 4096);
  uint64_t sig_len = 20 + 88 + strlen(ident) + 1 + (uint64_t)np0 * 32;
  uint64_t link_fsz = (sig_off - link_foff) + sig_len;
  seg_cmd(&lc, "__LINKEDIT", link_vaddr, align_up(link_fsz, MACHO_PAGE),
          link_foff, link_fsz, VM_PROT_READ, VM_PROT_READ, 0); ncmds++;

  buf_u32(&lc, LC_DYLD_INFO_ONLY); buf_u32(&lc, 48);
  buf_u32(&lc, (uint32_t)reb_off); buf_u32(&lc, (uint32_t)reb.len);
  buf_u32(&lc, (uint32_t)bind_off); buf_u32(&lc, (uint32_t)bind.len);
  buf_u32(&lc, 0); buf_u32(&lc, 0);
  buf_u32(&lc, 0); buf_u32(&lc, 0);
  buf_u32(&lc, exp.len ? (uint32_t)exp_off : 0); buf_u32(&lc, (uint32_t)exp.len);
  ncmds++;

  buf_u32(&lc, LC_SYMTAB_C); buf_u32(&lc, 24);
  buf_u32(&lc, (uint32_t)sym_off); buf_u32(&lc, nlocal + ndef + nundef);
  buf_u32(&lc, (uint32_t)str_off); buf_u32(&lc, (uint32_t)strt.len);
  ncmds++;

  buf_u32(&lc, LC_DYSYMTAB); buf_u32(&lc, 80);
  buf_u32(&lc, 0); buf_u32(&lc, nlocal);
  buf_u32(&lc, nlocal); buf_u32(&lc, ndef);
  buf_u32(&lc, nlocal + ndef); buf_u32(&lc, nundef);
  for (int i = 0; i < 12; i++) buf_u32(&lc, 0);
  ncmds++;

  if (!L->shared) {
    uint32_t dl = (uint32_t)align_up(12 + strlen(dyld) + 1, 8);
    buf_u32(&lc, LC_LOAD_DYLINKER); buf_u32(&lc, dl); buf_u32(&lc, 12);
    buf_bytes(&lc, dyld, strlen(dyld) + 1);
    while ((lc.len % 8) != 0) buf_u8(&lc, 0);
    ncmds++;
  }

  buf_u32(&lc, LC_BUILD_VERSION); buf_u32(&lc, 24);
  buf_u32(&lc, 1);
  buf_u32(&lc, 11 << 16); buf_u32(&lc, 11 << 16); buf_u32(&lc, 0);
  ncmds++;

  if (L->shared) {
    const char *id = L->install_name;
    uint32_t idsz = (uint32_t)align_up(24 + strlen(id) + 1, 8);
    buf_u32(&lc, LC_ID_DYLIB); buf_u32(&lc, idsz);
    buf_u32(&lc, 24); buf_u32(&lc, 0);
    buf_u32(&lc, 1 << 16); buf_u32(&lc, 1 << 16);
    buf_bytes(&lc, id, strlen(id) + 1);
    while ((lc.len % 8) != 0) buf_u8(&lc, 0);
    ncmds++;
  } else {
    buf_u32(&lc, LC_MAIN); buf_u32(&lc, 24);
    buf_u64(&lc, entry - base); buf_u64(&lc, 0);
    ncmds++;
  }

  /* Filled in below from a hash of the image.  Instruments, dsymutil and the
     crash reporter pair a binary with its symbols by this; without it every
     executable warmc links has the same placeholder identity, and a profile
     stays a column of addresses. */
  buf_u32(&lc, LC_UUID_C); buf_u32(&lc, 24);
  size_t uuid_at = 32 + lc.len;
  for (int i = 0; i < 16; i++) buf_u8(&lc, 0);
  ncmds++;

  uint32_t dylibsz = (uint32_t)align_up(24 + strlen(dylib) + 1, 8);
  buf_u32(&lc, LC_LOAD_DYLIB); buf_u32(&lc, dylibsz);
  buf_u32(&lc, 24); buf_u32(&lc, 0); buf_u32(&lc, 0); buf_u32(&lc, 0);
  buf_bytes(&lc, dylib, strlen(dylib) + 1);
  while ((lc.len % 8) != 0) buf_u8(&lc, 0);
  ncmds++;

  buf_u32(&lc, LC_CODE_SIGNATURE); buf_u32(&lc, 16);
  buf_u32(&lc, (uint32_t)sig_off); buf_u32(&lc, (uint32_t)sig_len);
  ncmds++;

  if (32 + lc.len > L->text_addr - base)
    warmc_fatal("link: load commands are %llu bytes and __text was placed at "
                "%llu; update macho_hdr_size",
                (unsigned long long)(32 + lc.len),
                (unsigned long long)(L->text_addr - base));

  Buf out; memset(&out, 0, sizeof out);
  buf_u32(&out, MH_MAGIC_64);
  buf_u32(&out, 0x0100000c);
  buf_u32(&out, 0);
  buf_u32(&out, L->shared ? MH_DYLIB : MH_EXECUTE);
  buf_u32(&out, ncmds); buf_u32(&out, (uint32_t)lc.len);

  buf_u32(&out, MH_DYLDLINK | MH_TWOLEVEL |
                (L->shared ? (uint32_t)MH_NO_REEXPORTED_DYLIBS : (uint32_t)MH_PIE) |
                (nundef ? 0u : (uint32_t)MH_NOUNDEFS));
  buf_u32(&out, 0);
  buf_bytes(&out, lc.data, lc.len);

  while (out.len < L->text_addr - base) buf_u8(&out, 0);
  emit_secs(&out, L, K_TEXT, L->text_addr, L->text_addr - base);
  while (out.len < L->stub_addr - base) buf_u8(&out, 0);
  for (uint32_t i = 0; i < L->nstub; i++) {
    uint64_t at = L->stub_addr + (uint64_t)i * STUB_SIZE;
    uint64_t ga = L->got_addr + (uint64_t)L->gsyms[L->stub[i]].got_slot * 8;
    uint8_t ins[12];
    wr32(ins + 0, 0x90000010u);
    wr32(ins + 4, 0xf9400210u);
    wr32(ins + 8, 0xd61f0200u);
    patch_page21(ins + 0, ga, at, "stub");
    patch_lo12(ins + 4, ga, 3);
    buf_bytes(&out, ins, 12);
  }
  while (out.len < L->rodata_addr - base) buf_u8(&out, 0);
  emit_secs(&out, L, K_RODATA, L->rodata_addr, L->rodata_addr - base);
  emit_secs(&out, L, K_CUSTOM, L->custom_addr, L->custom_addr - base);

  while (out.len < data_foff) buf_u8(&out, 0);
  for (uint32_t i = 0; i < L->ngot; i++) {
    GSym *g = &L->gsyms[L->got[i]];
    buf_u64(&out, g->imported ? 0 : g->addr);
  }
  while (out.len < L->data_addr - base) buf_u8(&out, 0);
  emit_secs(&out, L, K_DATA, L->data_addr, L->data_addr - base);

  while (out.len < reb_off) buf_u8(&out, 0);
  buf_bytes(&out, reb.data, reb.len);
  buf_bytes(&out, bind.data, bind.len);
  buf_bytes(&out, exp.data, exp.len);
  buf_bytes(&out, symt.data, symt.len);
  buf_bytes(&out, strt.data, strt.len);
  while (out.len < sig_off) buf_u8(&out, 0);

  {
    uint8_t h[32];
#ifdef SHA_HW
    CC_SHA256(out.data, (CC_LONG)out.len, h);
#else
    Sha256 s;
    sha_init(&s); sha_update(&s, out.data, out.len); sha_final(&s, h);
#endif
    h[6] = (uint8_t)((h[6] & 0x0f) | 0x50);
    h[8] = (uint8_t)((h[8] & 0x3f) | 0x80);
    memcpy(out.data + uuid_at, h, 16);
  }

  uint64_t code_limit = sig_off;
  uint32_t npages = (uint32_t)((code_limit + 4095) / 4096);
  uint32_t idlen = (uint32_t)strlen(ident) + 1;
  uint32_t cd_hdr = 88;
  uint32_t cd_len = cd_hdr + idlen + npages * 32;
  uint32_t sb_len = 20 + cd_len;

  Buf cd; memset(&cd, 0, sizeof cd);
  be32(&cd, 0xfade0c02); be32(&cd, cd_len);
  be32(&cd, 0x20400);
  be32(&cd, 0x00000002);
  be32(&cd, cd_hdr + idlen);
  be32(&cd, cd_hdr);
  be32(&cd, 0);
  be32(&cd, npages);
  be32(&cd, (uint32_t)code_limit);
  buf_u8(&cd, 32); buf_u8(&cd, 2);
  buf_u8(&cd, 0); buf_u8(&cd, 12);
  be32(&cd, 0);
  be32(&cd, 0);
  be32(&cd, 0);
  be32(&cd, 0);
  be64(&cd, 0);
  be64(&cd, 0);
  be64(&cd, text_fsz);
  be64(&cd, L->shared ? 0 : 1);
  buf_bytes(&cd, ident, idlen);
  for (uint32_t p = 0; p < npages; p++) {
    size_t off = (size_t)p * 4096;
    size_t n = code_limit - off < 4096 ? (size_t)(code_limit - off) : 4096;
    uint8_t h[32];
#ifdef SHA_HW
    CC_SHA256(out.data + off, (CC_LONG)n, h);
#else
    Sha256 s;
    sha_init(&s); sha_update(&s, out.data + off, n); sha_final(&s, h);
#endif
    buf_bytes(&cd, h, 32);
  }

  Buf sb; memset(&sb, 0, sizeof sb);
  be32(&sb, 0xfade0cc0);
  be32(&sb, sb_len);
  be32(&sb, 1);
  be32(&sb, 0);
  be32(&sb, 20);
  buf_bytes(&out, sb.data, sb.len);
  buf_bytes(&out, cd.data, cd.len);
  xfree(sb.data);

  if (sb_len != sig_len)
    warmc_fatal("link: signature is %u bytes but %llu were reserved",
                sb_len, (unsigned long long)sig_len);

  FILE *f = fopen(path, "wb");
  if (!f) warmc_fatal("link: cannot write %s", path);
  if (out.len && fwrite(out.data, 1, out.len, f) != out.len)
    warmc_fatal("link: short write on %s", path);
  fclose(f);
  chmod_exec(path);
  xfree(out.data); xfree(lc.data); xfree(cd.data);
  xfree(reb.data); xfree(bind.data); xfree(exp.data); xfree(symt.data);
  xfree(strt.data);
}
