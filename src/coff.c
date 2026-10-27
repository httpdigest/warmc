#include "warmc.h"

#include <stdio.h>
#include <string.h>

/* COFF/AMD64 relocatable writer, the PE-world peer of write_elf64 and
   write_macho64.  Three sections in the fixed ObjBuilder order, and the
   custom sections as a fourth, .wcustom, when there are any; addends
   are folded in-situ (COFF relocations carry none), following the
   Mach-O writer's precedent for REL_ABS64. */

#define IMAGE_FILE_MACHINE_AMD64 0x8664

#define IMAGE_SCN_CNT_CODE             0x00000020u
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040u
#define IMAGE_SCN_LNK_NRELOC_OVFL      0x01000000u
#define IMAGE_SCN_ALIGN_8BYTES         0x00400000u
#define IMAGE_SCN_ALIGN_16BYTES        0x00500000u
#define IMAGE_SCN_ALIGN_64BYTES        0x00700000u
#define IMAGE_SCN_MEM_EXECUTE          0x20000000u
#define IMAGE_SCN_MEM_READ             0x40000000u
#define IMAGE_SCN_MEM_WRITE            0x80000000u

#define IMAGE_REL_AMD64_ABSOLUTE 0x0000
#define IMAGE_REL_AMD64_ADDR64   0x0001
#define IMAGE_REL_AMD64_REL32    0x0004

#define IMAGE_SYM_CLASS_EXTERNAL 2
#define IMAGE_SYM_CLASS_STATIC   3

static uint16_t coff_reltype(RelKind k) {
  switch (k) {
    case REL_ABS64: return IMAGE_REL_AMD64_ADDR64;
    case REL_PC32: return IMAGE_REL_AMD64_REL32;
    default: break;
  }
  warmc_fatal("coff: relocation kind %d has no AMD64 mapping", (int)k);
  return 0;
}

static bool sym_is_local(const Sym *s) {
  return s->kind == SYM_LOCAL_FUNC || s->kind == SYM_LOCAL_DATA;
}

void write_coff64(ObjBuilder *o, const char *path) {
  Buf *secs[4] = { &o->text, &o->rodata, &o->data, &o->custom };
  int ns = o->custom.len ? 4 : 3;

  /* Addends live in the section bytes.  ADDR64 carries the addend
     directly (the linker adds S); REL32's hardware convention counts
     from the end of the 4-byte field, so the +4 restores our
     "S + A - P" reading of Reloc.addend. */
  for (uint32_t i = 0; i < o->nrelocs; i++) {
    Reloc *r = &o->relocs[i];
    Buf *sec = secs[r->section];
    if (r->kind == REL_ABS64) {
      if (r->offset + 8 > sec->len)
        warmc_fatal("coff: relocation past end of section");
      uint64_t v = (uint64_t)r->addend;
      for (int b = 0; b < 8; b++) sec->data[r->offset + b] = (uint8_t)(v >> (8 * b));
    } else {
      if (r->offset + 4 > sec->len)
        warmc_fatal("coff: relocation past end of section");
      uint32_t v = (uint32_t)(int32_t)(r->addend + 4);
      for (int b = 0; b < 4; b++) sec->data[r->offset + b] = (uint8_t)(v >> (8 * b));
    }
  }

  Buf reloc[4];
  uint32_t nrel[4] = { 0, 0, 0, 0 };
  for (int i = 0; i < 4; i++) memset(&reloc[i], 0, sizeof reloc[i]);
  for (uint32_t i = 0; i < o->nrelocs; i++) nrel[o->relocs[i].section]++;
  for (int s = 0; s < ns; s++) {
    if (nrel[s] < 0xffff) continue;
    /* count overflow: the real total rides in a leading marker entry */
    buf_u32(&reloc[s], nrel[s] + 1);
    buf_u32(&reloc[s], 0);
    buf_u16(&reloc[s], IMAGE_REL_AMD64_ABSOLUTE);
  }
  for (uint32_t i = 0; i < o->nrelocs; i++) {
    Reloc *r = &o->relocs[i];
    Buf *rb = &reloc[r->section];
    buf_u32(rb, (uint32_t)r->offset);
    buf_u32(rb, r->sym);
    buf_u16(rb, coff_reltype(r->kind));
  }

  Buf strtab; memset(&strtab, 0, sizeof strtab);
  buf_u32(&strtab, 0);   /* total size, patched below */
  Buf symtab; memset(&symtab, 0, sizeof symtab);
  for (uint32_t i = 0; i < o->nsyms; i++) {
    Sym *s = &o->syms[i];
    const char *nm = s->display ? s->display : s->name;
    size_t len = strlen(nm);
    if (len <= 8) {
      char name[8] = { 0 };
      memcpy(name, nm, len);
      buf_bytes(&symtab, name, 8);
    } else {
      buf_u32(&symtab, 0);
      buf_u32(&symtab, (uint32_t)strtab.len);
      buf_bytes(&strtab, nm, len + 1);
    }
    bool undef = s->section < 0;
    buf_u32(&symtab, undef ? 0 : (uint32_t)s->value);
    buf_u16(&symtab, undef ? 0 : (uint16_t)(s->section + 1));
    bool func = s->kind == SYM_LOCAL_FUNC || s->kind == SYM_GLOBAL_FUNC ||
                s->kind == SYM_UNDEF_FUNC;
    buf_u16(&symtab, func ? 0x20 : 0);
    buf_u8(&symtab, sym_is_local(s) ? IMAGE_SYM_CLASS_STATIC
                                    : IMAGE_SYM_CLASS_EXTERNAL);
    buf_u8(&symtab, 0);
  }
  uint32_t total = (uint32_t)strtab.len;
  for (int b = 0; b < 4; b++) strtab.data[b] = (uint8_t)(total >> (8 * b));

  static const uint32_t chars[4] = {
    IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ |
        IMAGE_SCN_ALIGN_64BYTES,
    IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES,
    IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE |
        IMAGE_SCN_ALIGN_16BYTES,
    IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_8BYTES,
  };
  static const char *names[4] = { ".text", ".rdata", ".data", ".wcustom" };

  uint32_t off = 20 + (uint32_t)ns * 40;
  uint32_t raw_ptr[4], rel_ptr[4];
  for (int s = 0; s < ns; s++) {
    off = (off + 15u) & ~15u;
    raw_ptr[s] = secs[s]->len ? off : 0;
    off += (uint32_t)secs[s]->len;
  }
  for (int s = 0; s < ns; s++) {
    rel_ptr[s] = reloc[s].len ? off : 0;
    off += (uint32_t)reloc[s].len;
  }
  uint32_t sym_ptr = off;

  Buf out; memset(&out, 0, sizeof out);
  buf_u16(&out, IMAGE_FILE_MACHINE_AMD64);
  buf_u16(&out, (uint16_t)ns);
  buf_u32(&out, 0);
  buf_u32(&out, sym_ptr);
  buf_u32(&out, o->nsyms);
  buf_u16(&out, 0);
  buf_u16(&out, 0);
  for (int s = 0; s < ns; s++) {
    char name[8] = { 0 };
    memcpy(name, names[s], strlen(names[s]));
    buf_bytes(&out, name, 8);
    buf_u32(&out, 0);
    buf_u32(&out, 0);
    buf_u32(&out, (uint32_t)secs[s]->len);
    buf_u32(&out, raw_ptr[s]);
    buf_u32(&out, rel_ptr[s]);
    buf_u32(&out, 0);
    uint32_t nr = nrel[s] >= 0xffff ? nrel[s] + 1 : nrel[s];
    buf_u16(&out, (uint16_t)(nr > 0xffff ? 0xffff : nr));
    buf_u16(&out, 0);
    buf_u32(&out, chars[s] | (nrel[s] >= 0xffff ? IMAGE_SCN_LNK_NRELOC_OVFL : 0));
  }
  for (int s = 0; s < ns; s++) {
    while (out.len & 15u) buf_u8(&out, 0);
    buf_bytes(&out, secs[s]->data, secs[s]->len);
  }
  for (int s = 0; s < ns; s++) buf_bytes(&out, reloc[s].data, reloc[s].len);
  buf_bytes(&out, symtab.data, symtab.len);
  buf_bytes(&out, strtab.data, strtab.len);

  warmc_remove_file(path);  /* a new file: the old may be a compile cache's entry (main.c) */
  FILE *f = fopen(path, "wb");
  if (!f) warmc_fatal("cannot open %s for writing", path);
  if (fwrite(out.data, 1, out.len, f) != out.len)
    warmc_fatal("cannot write %s", path);
  if (fclose(f)) warmc_fatal("cannot write %s", path);

  buf_free(&out); buf_free(&symtab); buf_free(&strtab);
  for (int i = 0; i < 4; i++) buf_free(&reloc[i]);
}
