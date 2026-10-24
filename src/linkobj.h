#ifndef WARMC_LINKOBJ_H
#define WARMC_LINKOBJ_H

#include "warmc.h"

/* The static linker's object model, shared between link.c (readers, symbol
   resolution, relocation) and pe.c (Windows layout and the PE32+ writer). */

/* K_CUSTOM: the module's custom sections (module_data.c), read-only and
   kept in a section of their own */
typedef enum { K_TEXT, K_RODATA, K_DATA, K_BSS, K_CUSTOM, K_DROP } SecKind;

typedef struct {
  char *name;
  const uint8_t *data;
  uint64_t size, align;
  uint64_t addr;
  SecKind kind;
} InSec;

typedef struct {
  char *name;
  int32_t sec;
  uint64_t value;
  bool global, weak, common;
  uint64_t comm_size, comm_align;
} InSym;

typedef struct {
  uint32_t sec;
  uint64_t off;
  int32_t sym;
  uint32_t type;
  int64_t addend;
  bool pcrel;
  uint8_t length;
} InRel;

typedef struct {
  char *name;
  uint8_t *buf;
  size_t len;
  InSec *secs; uint32_t nsecs;
  InSym *syms; uint32_t nsyms;
  InRel *rels; uint32_t nrels;
} InObj;

typedef struct {
  char *name;
  int32_t obj;
  int32_t sym;
  uint64_t addr;
  bool defined, weak, imported;
  bool exported;             /* a shared library's dynamic symbol */
  uint32_t got_slot;
  uint32_t stub;
} GSym;

#define NO_SLOT 0xffffffffu

typedef struct {
  InObj *objs; uint32_t nobjs, cap_objs;
  GSym *gsyms; uint32_t ngsyms, cap_gsyms;
  uint32_t *gs_ix; uint32_t gs_ix_cap;

  uint64_t text_addr, text_size;
  uint64_t rodata_addr, rodata_size;
  uint64_t custom_addr, custom_size;
  uint64_t data_addr, data_size;
  uint64_t bss_addr, bss_size;

  uint64_t *rebase; uint32_t nrebase, cap_rebase;
  uint64_t *rebase_val;      /* what each slot holds at base 0 (ELF RELA) */
  uint32_t *got;  uint32_t ngot,  cap_got;
  uint32_t *stub; uint32_t nstub, cap_stub;
  uint64_t got_addr, stub_addr;

  TargetFmt fmt;

  /* A shared library (warmc --shared) rather than a program: the image
     sits at base 0, and only the symbols marked exported are seen from
     outside.  base is the program's fixed address otherwise. */
  bool shared;
  uint64_t base;
  const char *install_name;

  /* PE: the import blob (IAT, ILT, directory, names) placed at the start of
     .rdata; got_addr doubles as the IAT address and got_slot as the final
     IAT index. */
  Buf pe_imp;
  uint64_t pe_idir_addr;
  uint32_t pe_idir_size, pe_iat_size;
} Linker;

/* link.c */
uint64_t place(Linker *L, SecKind k, uint64_t at);
void place_commons(Linker *L, uint64_t *bss_end);
void emit_secs(Buf *b, Linker *L, SecKind k, uint64_t seg_addr, uint64_t seg_off);
void chmod_exec(const char *path);

/* pe.c */
#define PE_BASE 0x140000000ull
/* a DLL's preferred base, where the usual ones load: away from programs' */
#define PE_DLL_BASE 0x180000000ull
const char *pe_dll_for(const char *name);
void layout_windows(Linker *L);
void write_pe_exec(Linker *L, const char *path, uint64_t entry);

#endif
