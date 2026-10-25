#include "warmc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R_AARCH64_ABS64             257
#define R_AARCH64_ADR_PREL_PG_HI21  275
#define R_AARCH64_ADD_ABS_LO12_NC   277
#define R_AARCH64_LDST64_ABS_LO12_NC 286
#define R_AARCH64_CALL26            283

#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4

#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4
#define SHF_INFO_LINK 0x40

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC   2

typedef struct { Buf b; } Strtab;

static uint32_t str_add(Buf *b, const char *s) {
  uint32_t off = (uint32_t)b->len;
  buf_bytes(b, s, strlen(s) + 1);
  return off;
}

static uint32_t elf_reltype(RelKind k) {
  switch (k) {
    case REL_CALL26: return R_AARCH64_CALL26;
    case REL_ADR_PAGE21: return R_AARCH64_ADR_PREL_PG_HI21;
    case REL_ADD_LO12: return R_AARCH64_ADD_ABS_LO12_NC;
    case REL_LDST_LO12: return R_AARCH64_LDST64_ABS_LO12_NC;
    case REL_ABS64: return R_AARCH64_ABS64;
    default: break;
  }
  warmc_fatal("elf: relocation kind %d has no AArch64 mapping", (int)k);
  return 0;
}

static bool sym_is_local(const Sym *s) {
  return s->kind == SYM_LOCAL_FUNC || s->kind == SYM_LOCAL_DATA;
}

void write_elf64(ObjBuilder *o, const char *path) {

  uint32_t n = o->nsyms;
  uint32_t *order = xmalloc((n + 1) * sizeof(uint32_t));
  uint32_t *newidx = xmalloc((n + 1) * sizeof(uint32_t));
  uint32_t k = 0;
  for (uint32_t i = 0; i < n; i++) if (sym_is_local(&o->syms[i])) order[k++] = i;
  uint32_t first_global = k + 1;
  for (uint32_t i = 0; i < n; i++) if (!sym_is_local(&o->syms[i])) order[k++] = i;
  for (uint32_t j = 0; j < n; j++) newidx[order[j]] = j + 1;

  Buf strtab; memset(&strtab, 0, sizeof strtab);
  buf_u8(&strtab, 0);
  Buf symtab; memset(&symtab, 0, sizeof symtab);
  buf_zero(&symtab, 24);

  /* section 3, the custom sections, comes last when there are any */
  static const int sec_of[4] = { 1, 3, 5, 10 };
  for (uint32_t j = 0; j < n; j++) {
    Sym *s = &o->syms[order[j]];
    uint32_t nameoff = str_add(&strtab, s->display ? s->display : s->name);
    uint8_t bind = sym_is_local(s) ? STB_LOCAL : STB_GLOBAL;
    uint8_t type = STT_NOTYPE;
    if (s->section >= 0)
      type = (s->kind == SYM_LOCAL_FUNC || s->kind == SYM_GLOBAL_FUNC) ? STT_FUNC : STT_OBJECT;
    uint16_t shndx = s->section >= 0 ? (uint16_t)sec_of[s->section] : 0;
    buf_u32(&symtab, nameoff);
    buf_u8(&symtab, (uint8_t)((bind << 4) | type));
    buf_u8(&symtab, 0);
    buf_u16(&symtab, shndx);
    buf_u64(&symtab, s->section >= 0 ? s->value : 0);
    buf_u64(&symtab, s->size);
  }

  Buf rela[3];
  for (int i = 0; i < 3; i++) memset(&rela[i], 0, sizeof rela[i]);
  for (uint32_t i = 0; i < o->nrelocs; i++) {
    Reloc *r = &o->relocs[i];
    Buf *rb = &rela[r->section];
    buf_u64(rb, r->offset);
    buf_u64(rb, ((uint64_t)newidx[r->sym] << 32) | elf_reltype(r->kind));
    buf_u64(rb, (uint64_t)r->addend);
  }

  Buf shstr; memset(&shstr, 0, sizeof shstr);
  buf_u8(&shstr, 0);
  uint32_t n_text = str_add(&shstr, ".text");
  uint32_t n_rtext = str_add(&shstr, ".rela.text");
  uint32_t n_rodata = str_add(&shstr, ".rodata");
  uint32_t n_rrodata = str_add(&shstr, ".rela.rodata");
  uint32_t n_data = str_add(&shstr, ".data");
  uint32_t n_rdata = str_add(&shstr, ".rela.data");
  uint32_t n_symtab = str_add(&shstr, ".symtab");
  uint32_t n_strtab = str_add(&shstr, ".strtab");
  uint32_t n_shstrtab = str_add(&shstr, ".shstrtab");
  uint32_t n_custom = o->custom.len ? str_add(&shstr, ".wasm.custom") : 0;
  int nsh = o->custom.len ? 11 : 10;

  struct { uint32_t name, type; uint64_t flags; const Buf *data;
           uint32_t link, info; uint64_t align, entsize; } sh[11] = {
    { 0, 0, 0, NULL, 0, 0, 0, 0 },
    { n_text,     SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, &o->text,   0, 0, 64, 0 },
    { n_rtext,    SHT_RELA,     SHF_INFO_LINK,             &rela[0],   7, 1, 8, 24 },
    { n_rodata,   SHT_PROGBITS, SHF_ALLOC,                 &o->rodata, 0, 0, 16, 0 },
    { n_rrodata,  SHT_RELA,     SHF_INFO_LINK,             &rela[1],   7, 3, 8, 24 },
    { n_data,     SHT_PROGBITS, SHF_ALLOC | SHF_WRITE,     &o->data,   0, 0, 16, 0 },
    { n_rdata,    SHT_RELA,     SHF_INFO_LINK,             &rela[2],   7, 5, 8, 24 },
    { n_symtab,   SHT_SYMTAB,   0,                         &symtab,    8, first_global, 8, 24 },
    { n_strtab,   SHT_STRTAB,   0,                         &strtab,    0, 0, 1, 0 },
    { n_shstrtab, SHT_STRTAB,   0,                         &shstr,     0, 0, 1, 0 },
    { n_custom,   SHT_PROGBITS, SHF_ALLOC,                 &o->custom, 0, 0, 8, 0 },
  };

  Buf out; memset(&out, 0, sizeof out);
  buf_zero(&out, 64);

  uint64_t offsets[11] = { 0 };
  for (int i = 1; i < nsh; i++) {
    uint64_t align = sh[i].align ? sh[i].align : 1;
    while (out.len % align) buf_u8(&out, 0);
    offsets[i] = out.len;
    if (sh[i].data) buf_bytes(&out, sh[i].data->data, sh[i].data->len);
  }
  while (out.len % 8) buf_u8(&out, 0);
  uint64_t shoff = out.len;
  for (int i = 0; i < nsh; i++) {
    buf_u32(&out, sh[i].name);
    buf_u32(&out, sh[i].type);
    buf_u64(&out, sh[i].flags);
    buf_u64(&out, 0);
    buf_u64(&out, offsets[i]);
    buf_u64(&out, sh[i].data ? sh[i].data->len : 0);
    buf_u32(&out, sh[i].link);
    buf_u32(&out, sh[i].info);
    buf_u64(&out, sh[i].align);
    buf_u64(&out, sh[i].entsize);
  }

  uint8_t *h = out.data;
  memcpy(h, "\177ELF", 4);
  h[4] = 2;
  h[5] = 1;
  h[6] = 1;
  h[7] = 0;
  uint16_t e_type = 1;
  uint16_t e_machine = 183;
  memcpy(h + 16, &e_type, 2);
  memcpy(h + 18, &e_machine, 2);
  uint32_t one = 1;
  memcpy(h + 20, &one, 4);
  memcpy(h + 40, &shoff, 8);
  uint16_t ehsize = 64, shentsize = 64, shnum = (uint16_t)nsh, shstrndx = 9;
  memcpy(h + 52, &ehsize, 2);
  memcpy(h + 58, &shentsize, 2);
  memcpy(h + 60, &shnum, 2);
  memcpy(h + 62, &shstrndx, 2);

  FILE *f = fopen(path, "wb");
  if (!f) warmc_fatal("cannot open %s for writing", path);
  if (fwrite(out.data, 1, out.len, f) != out.len)
    warmc_fatal("cannot write %s", path);
  if (fclose(f)) warmc_fatal("cannot write %s", path);

  buf_free(&out); buf_free(&symtab); buf_free(&strtab); buf_free(&shstr);
  for (int i = 0; i < 3; i++) buf_free(&rela[i]);
  xfree(order); xfree(newidx);
}
