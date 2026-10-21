#include "warmc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MH_MAGIC_64   0xfeedfacfu
#define CPU_TYPE_ARM64 0x0100000c
#define CPU_SUBTYPE_ARM64_ALL 0
#define MH_OBJECT     1

#define LC_SEGMENT_64   0x19
#define LC_SYMTAB       0x02
#define LC_DYSYMTAB     0x0b
#define LC_BUILD_VERSION 0x32

#define S_ATTR_PURE_INSTRUCTIONS 0x80000000u
#define S_ATTR_SOME_INSTRUCTIONS 0x00000400u

#define N_EXT  0x01
#define N_UNDF 0x00
#define N_SECT 0x0e

#define ARM64_RELOC_UNSIGNED 0
#define ARM64_RELOC_BRANCH26 2
#define ARM64_RELOC_PAGE21   3
#define ARM64_RELOC_PAGEOFF12 4

static uint32_t str_add(Buf *b, const char *s, int underscore) {
  uint32_t off = (uint32_t)b->len;
  if (underscore) buf_u8(b, '_');
  buf_bytes(b, s, strlen(s) + 1);
  return off;
}

static bool sym_is_local(const Sym *s) {
  return s->kind == SYM_LOCAL_FUNC || s->kind == SYM_LOCAL_DATA;
}
static bool sym_is_undef(const Sym *s) { return s->section < 0; }

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

void write_macho64(ObjBuilder *o, const char *path) {

  uint32_t n = o->nsyms;
  uint32_t *order = xmalloc((n + 1) * sizeof(uint32_t));
  uint32_t *newidx = xmalloc((n + 1) * sizeof(uint32_t));
  uint32_t k = 0, nlocal = 0, nextdef = 0, nundef = 0;
  for (uint32_t i = 0; i < n; i++)
    if (!sym_is_undef(&o->syms[i]) && sym_is_local(&o->syms[i])) { order[k++] = i; nlocal++; }
  for (uint32_t i = 0; i < n; i++)
    if (!sym_is_undef(&o->syms[i]) && !sym_is_local(&o->syms[i])) { order[k++] = i; nextdef++; }
  for (uint32_t i = 0; i < n; i++)
    if (sym_is_undef(&o->syms[i])) { order[k++] = i; nundef++; }
  for (uint32_t j = 0; j < n; j++) newidx[order[j]] = j;

  uint64_t text_addr = 0;
  uint64_t const_addr = align_up(text_addr + o->text.len, 16);
  uint64_t data_addr = align_up(const_addr + o->rodata.len, 16);
  uint64_t vmsize = align_up(data_addr + o->data.len, 16);
  const uint64_t sec_addr[3] = { text_addr, const_addr, data_addr };

  for (uint32_t i = 0; i < o->nrelocs; i++) {
    Reloc *r = &o->relocs[i];
    if (r->kind != REL_ABS64) continue;
    Buf *sec = r->section == 1 ? &o->rodata : &o->data;
    if (r->offset + 8 > sec->len) warmc_fatal("internal: relocation past end of section");
    uint64_t v = (uint64_t)r->addend;
    for (int b = 0; b < 8; b++) sec->data[r->offset + b] = (uint8_t)(v >> (8 * b));
  }

  Buf reloc[3];
  for (int i = 0; i < 3; i++) memset(&reloc[i], 0, sizeof reloc[i]);
  for (uint32_t i = 0; i < o->nrelocs; i++) {
    Reloc *r = &o->relocs[i];
    uint32_t type, pcrel, length;
    switch (r->kind) {
      case REL_CALL26:     type = ARM64_RELOC_BRANCH26;  pcrel = 1; length = 2; break;
      case REL_ADR_PAGE21: type = ARM64_RELOC_PAGE21;    pcrel = 1; length = 2; break;
      case REL_ADD_LO12:
      case REL_LDST_LO12:  type = ARM64_RELOC_PAGEOFF12; pcrel = 0; length = 2; break;
      case REL_ABS64:      type = ARM64_RELOC_UNSIGNED;  pcrel = 0; length = 3; break;
      default:
        warmc_fatal("macho: relocation kind %d has no arm64 mapping", (int)r->kind);
    }
    Buf *rb = &reloc[r->section];
    buf_u32(rb, (uint32_t)r->offset);
    buf_u32(rb, (newidx[r->sym] & 0xffffffu) | (pcrel << 24) | (length << 25) |
                (1u << 27) | (type << 28));
  }

  Buf strtab; memset(&strtab, 0, sizeof strtab);
  buf_u8(&strtab, 0);
  Buf symtab; memset(&symtab, 0, sizeof symtab);
  for (uint32_t j = 0; j < n; j++) {
    Sym *s = &o->syms[order[j]];
    uint32_t nameoff = str_add(&strtab, s->name, 1);
    uint8_t n_type, n_sect;
    uint64_t n_value;
    if (sym_is_undef(s)) { n_type = N_UNDF | N_EXT; n_sect = 0; n_value = 0; }
    else {
      n_type = (uint8_t)(N_SECT | (sym_is_local(s) ? 0 : N_EXT));
      n_sect = (uint8_t)(s->section + 1);
      n_value = sec_addr[s->section] + s->value;
    }
    buf_u32(&symtab, nameoff);
    buf_u8(&symtab, n_type);
    buf_u8(&symtab, n_sect);
    buf_u16(&symtab, 0);
    buf_u64(&symtab, n_value);
  }
  while (strtab.len % 8) buf_u8(&strtab, 0);

  uint32_t ncmds = 4;
  uint32_t sizeofcmds = (72 + 80 * 3) + 24 + 24 + 80;
  uint64_t hdr_end = 32 + sizeofcmds;

  uint64_t off_text = align_up(hdr_end, 16);
  uint64_t off_const = align_up(off_text + o->text.len, 16);
  uint64_t off_data = align_up(off_const + o->rodata.len, 16);
  uint64_t sec_off[3] = { off_text, off_const, off_data };
  uint64_t cur = align_up(off_data + o->data.len, 8);
  uint64_t rel_off[3];
  for (int i = 0; i < 3; i++) { rel_off[i] = cur; cur += reloc[i].len; }
  uint64_t symoff = align_up(cur, 8);
  uint64_t stroff = symoff + symtab.len;
  uint64_t total = stroff + strtab.len;

  Buf out; memset(&out, 0, sizeof out);

  buf_u32(&out, MH_MAGIC_64);
  buf_u32(&out, CPU_TYPE_ARM64);
  buf_u32(&out, CPU_SUBTYPE_ARM64_ALL);
  buf_u32(&out, MH_OBJECT);
  buf_u32(&out, ncmds);
  buf_u32(&out, sizeofcmds);
  buf_u32(&out, 0);
  buf_u32(&out, 0);

  buf_u32(&out, LC_SEGMENT_64);
  buf_u32(&out, 72 + 80 * 3);
  buf_zero(&out, 16);
  buf_u64(&out, 0);
  buf_u64(&out, vmsize);
  buf_u64(&out, off_text);
  buf_u64(&out, align_up(off_data + o->data.len, 16) - off_text);
  buf_u32(&out, 7);
  buf_u32(&out, 7);
  buf_u32(&out, 3);
  buf_u32(&out, 0);

  struct { const char *sect, *seg; const Buf *data; uint64_t addr, off;
           uint32_t align, flags; const Buf *rel; uint64_t reloff; } S[3] = {
    { "__text",  "__TEXT", &o->text,   text_addr,  off_text,
      6, S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS, &reloc[0], rel_off[0] },
    { "__const", "__TEXT", &o->rodata, const_addr, off_const, 4, 0, &reloc[1], rel_off[1] },
    { "__data",  "__DATA", &o->data,   data_addr,  off_data,  4, 0, &reloc[2], rel_off[2] },
  };
  for (int i = 0; i < 3; i++) {
    char nm[16] = { 0 }, sg[16] = { 0 };
    memcpy(nm, S[i].sect, strlen(S[i].sect));
    memcpy(sg, S[i].seg, strlen(S[i].seg));
    buf_bytes(&out, nm, 16);
    buf_bytes(&out, sg, 16);
    buf_u64(&out, S[i].addr);
    buf_u64(&out, S[i].data->len);
    buf_u32(&out, (uint32_t)(S[i].data->len ? S[i].off : 0));
    buf_u32(&out, S[i].align);
    buf_u32(&out, (uint32_t)(S[i].rel->len ? S[i].reloff : 0));
    buf_u32(&out, (uint32_t)(S[i].rel->len / 8));
    buf_u32(&out, S[i].flags);
    buf_u32(&out, 0);
    buf_u32(&out, 0);
    buf_u32(&out, 0);
  }

  buf_u32(&out, LC_BUILD_VERSION);
  buf_u32(&out, 24);
  buf_u32(&out, 1);
  buf_u32(&out, 11 << 16);
  buf_u32(&out, 11 << 16);
  buf_u32(&out, 0);

  buf_u32(&out, LC_SYMTAB);
  buf_u32(&out, 24);
  buf_u32(&out, (uint32_t)symoff);
  buf_u32(&out, n);
  buf_u32(&out, (uint32_t)stroff);
  buf_u32(&out, (uint32_t)strtab.len);

  buf_u32(&out, LC_DYSYMTAB);
  buf_u32(&out, 80);
  buf_u32(&out, 0);              buf_u32(&out, nlocal);
  buf_u32(&out, nlocal);         buf_u32(&out, nextdef);
  buf_u32(&out, nlocal + nextdef); buf_u32(&out, nundef);
  for (int i = 0; i < 12; i++) buf_u32(&out, 0);

  for (int i = 0; i < 3; i++) {
    while (out.len < sec_off[i]) buf_u8(&out, 0);
    buf_bytes(&out, S[i].data->data, S[i].data->len);
  }
  for (int i = 0; i < 3; i++) {
    while (out.len < rel_off[i]) buf_u8(&out, 0);
    buf_bytes(&out, reloc[i].data, reloc[i].len);
  }
  while (out.len < symoff) buf_u8(&out, 0);
  buf_bytes(&out, symtab.data, symtab.len);
  while (out.len < stroff) buf_u8(&out, 0);
  buf_bytes(&out, strtab.data, strtab.len);
  while (out.len < total) buf_u8(&out, 0);

  FILE *f = fopen(path, "wb");
  if (!f) warmc_fatal("cannot open %s for writing", path);
  if (fwrite(out.data, 1, out.len, f) != out.len)
    warmc_fatal("cannot write %s", path);
  if (fclose(f)) warmc_fatal("cannot write %s", path);

  buf_free(&out); buf_free(&symtab); buf_free(&strtab);
  for (int i = 0; i < 3; i++) buf_free(&reloc[i]);
  xfree(order); xfree(newidx);
}
