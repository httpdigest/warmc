#include "linkobj.h"

#include <stdio.h>
#include <string.h>

/* PE32+ executable writer, the Windows peer of write_elf_exec and
   write_macho_exec.  Fixed image base (no ASLR, no .reloc), console
   subsystem, and a curated symbol->DLL import table so cross-linking a
   Windows executable needs no SDK import libraries.  Stack commit equals
   reserve, which is what lets the backend skip stack probing.

   A DLL (warmc --shared) differs: a base it may be moved from, so .reloc
   with every absolute pointer; an export directory, .edata; no entry
   point; and since it runs on its host's threads, whose stacks are only
   reserved, the backend probes its large frames. */

#define PE_SECT_ALIGN 0x1000ull
#define PE_FILE_ALIGN 0x200ull
#define PE_STUB_SIZE  6

typedef struct { const char *sym; const char *dll; } PeImport;

/* Every Win32 API the runtime and WASI hosts may call.  An import that is
   not listed here is a link error: extend the table, do not fall back to
   import libraries. */
static const PeImport pe_imports[] = {
  /* process, threads, synchronization */
  { "ExitProcess", "kernel32.dll" },
  { "ExitThread", "kernel32.dll" },
  { "TerminateProcess", "kernel32.dll" },
  { "GetCurrentProcess", "kernel32.dll" },
  { "GetCurrentProcessId", "kernel32.dll" },
  { "GetCurrentThread", "kernel32.dll" },
  { "GetCurrentThreadId", "kernel32.dll" },
  { "GetCurrentThreadStackLimits", "kernel32.dll" },
  { "CreateThread", "kernel32.dll" },
  { "WaitForSingleObject", "kernel32.dll" },
  { "GetExitCodeThread", "kernel32.dll" },
  { "SwitchToThread", "kernel32.dll" },
  { "Sleep", "kernel32.dll" },
  { "SleepEx", "kernel32.dll" },
  { "GetActiveProcessorCount", "kernel32.dll" },
  { "GetSystemInfo", "kernel32.dll" },
  { "RaiseException", "kernel32.dll" },
  { "AddVectoredExceptionHandler", "kernel32.dll" },
  { "RemoveVectoredExceptionHandler", "kernel32.dll" },
  { "WaitOnAddress", "api-ms-win-core-synch-l1-2-0.dll" },
  { "WakeByAddressSingle", "api-ms-win-core-synch-l1-2-0.dll" },
  { "WakeByAddressAll", "api-ms-win-core-synch-l1-2-0.dll" },
  /* memory */
  { "VirtualAlloc", "kernel32.dll" },
  { "VirtualFree", "kernel32.dll" },
  { "VirtualProtect", "kernel32.dll" },
  { "VirtualQuery", "kernel32.dll" },
  /* time */
  { "QueryPerformanceCounter", "kernel32.dll" },
  { "QueryPerformanceFrequency", "kernel32.dll" },
  { "GetSystemTimeAsFileTime", "kernel32.dll" },
  { "GetSystemTimePreciseAsFileTime", "kernel32.dll" },
  { "GetTickCount64", "kernel32.dll" },
  /* randomness */
  { "ProcessPrng", "bcryptprimitives.dll" },
  /* command line, environment */
  { "GetCommandLineW", "kernel32.dll" },
  { "GetEnvironmentStringsW", "kernel32.dll" },
  { "FreeEnvironmentStringsW", "kernel32.dll" },
  { "GetEnvironmentVariableW", "kernel32.dll" },
  { "SetEnvironmentVariableW", "kernel32.dll" },
  /* errors */
  { "GetLastError", "kernel32.dll" },
  { "SetLastError", "kernel32.dll" },
  /* handles, console and file I/O (WASI preview1 host) */
  { "CloseHandle", "kernel32.dll" },
  { "DuplicateHandle", "kernel32.dll" },
  { "GetStdHandle", "kernel32.dll" },
  { "ReadFile", "kernel32.dll" },
  { "WriteFile", "kernel32.dll" },
  { "GetConsoleMode", "kernel32.dll" },
  { "SetConsoleMode", "kernel32.dll" },
  { "ReadConsoleW", "kernel32.dll" },
  { "WriteConsoleW", "kernel32.dll" },
  { "GetFileType", "kernel32.dll" },
  { "FlushFileBuffers", "kernel32.dll" },
  { "SetFilePointerEx", "kernel32.dll" },
  { "GetFileSizeEx", "kernel32.dll" },
  { "SetEndOfFile", "kernel32.dll" },
  { "CreateFileW", "kernel32.dll" },
  { "DeleteFileW", "kernel32.dll" },
  { "CreateDirectoryW", "kernel32.dll" },
  { "RemoveDirectoryW", "kernel32.dll" },
  { "MoveFileExW", "kernel32.dll" },
  { "GetFileAttributesW", "kernel32.dll" },
  { "GetFileInformationByHandle", "kernel32.dll" },
  { "GetFileInformationByHandleEx", "kernel32.dll" },
  { "SetFileInformationByHandle", "kernel32.dll" },
  { "FindFirstFileW", "kernel32.dll" },
  { "FindNextFileW", "kernel32.dll" },
  { "FindClose", "kernel32.dll" },
  { "GetFullPathNameW", "kernel32.dll" },
  { "CreateHardLinkW", "kernel32.dll" },
  { "CreateSymbolicLinkW", "kernel32.dll" },
  { "DeviceIoControl", "kernel32.dll" },
  { "SetFileTime", "kernel32.dll" },
  { "PeekNamedPipe", "kernel32.dll" },

  /* heap, for the WASI host's malloc */
  { "GetProcessHeap", "kernel32.dll" },
  { "HeapAlloc", "kernel32.dll" },
  { "HeapReAlloc", "kernel32.dll" },
  { "HeapFree", "kernel32.dll" },

  /* CPU clocks */
  { "GetProcessTimes", "kernel32.dll" },
  { "GetThreadTimes", "kernel32.dll" },
  { "QueryProcessCycleTime", "kernel32.dll" },
  { "QueryThreadCycleTime", "kernel32.dll" },
};

const char *pe_dll_for(const char *name) {
  if (!strncmp(name, "__imp_", 6)) name += 6;
  for (size_t i = 0; i < sizeof pe_imports / sizeof pe_imports[0]; i++)
    if (!strcmp(pe_imports[i].sym, name)) return pe_imports[i].dll;
  return NULL;
}

static uint64_t pe_align(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

void layout_windows(Linker *L) {
  L->text_addr = L->base + PE_SECT_ALIGN;
  uint64_t at = place(L, K_TEXT, L->text_addr);
  at = pe_align(at, 8);
  L->stub_addr = at;
  at += (uint64_t)L->nstub * PE_STUB_SIZE;
  L->text_size = at - L->text_addr;

  at = pe_align(at, PE_SECT_ALIGN);
  L->rodata_addr = at;

  /* Group the imports by DLL: each DLL's IAT run must be contiguous and
     null-terminated, so got_slot is rewritten to the final IAT index here
     (before apply_relocs computes any IAT addresses). */
  uint32_t n = L->ngot;
  const char *dlls[16];
  uint32_t ndll = 0;
  uint32_t *dref = xmalloc((n ? n : 1) * sizeof(uint32_t));
  for (uint32_t i = 0; i < n; i++) {
    const char *dll = pe_dll_for(L->gsyms[L->got[i]].name);
    if (!dll)
      warmc_fatal("link: %s has an import slot but no DLL; extend the table "
                  "in src/pe.c", L->gsyms[L->got[i]].name);
    uint32_t d;
    for (d = 0; d < ndll && strcmp(dlls[d], dll); d++) ;
    if (d == ndll) {
      if (ndll == 16) warmc_fatal("link: more than 16 import DLLs");
      dlls[ndll++] = dll;
    }
    dref[i] = d;
  }

  Buf str; memset(&str, 0, sizeof str);
  uint32_t *hn = xmalloc((n ? n : 1) * sizeof(uint32_t));
  uint32_t *dn = xmalloc((ndll ? ndll : 1) * sizeof(uint32_t));
  for (uint32_t i = 0; i < n; i++) {
    if (str.len & 1) buf_u8(&str, 0);
    hn[i] = (uint32_t)str.len;
    buf_u16(&str, 0);
    const char *fn = L->gsyms[L->got[i]].name;
    if (!strncmp(fn, "__imp_", 6)) fn += 6;
    buf_bytes(&str, fn, strlen(fn) + 1);
  }
  for (uint32_t d = 0; d < ndll; d++) {
    if (str.len & 1) buf_u8(&str, 0);
    dn[d] = (uint32_t)str.len;
    buf_bytes(&str, dlls[d], strlen(dlls[d]) + 1);
  }

  uint32_t nslots = n + ndll;
  uint64_t iat_off = 0;
  uint64_t ilt_off = (uint64_t)nslots * 8;
  uint64_t dir_off = ilt_off + (uint64_t)nslots * 8;
  uint64_t str_off = dir_off + ((uint64_t)ndll + 1) * 20;
  uint64_t rva0 = L->rodata_addr - L->base;

  uint32_t *dstart = xmalloc((ndll ? ndll : 1) * sizeof(uint32_t));
  uint32_t slot = 0;
  for (uint32_t d = 0; d < ndll; d++) {
    dstart[d] = slot;
    for (uint32_t i = 0; i < n; i++)
      if (dref[i] == d) L->gsyms[L->got[i]].got_slot = slot++;
    slot++;
  }

  Buf *blob = &L->pe_imp;
  memset(blob, 0, sizeof *blob);
  for (int pass = 0; pass < 2; pass++)
    for (uint32_t d = 0; d < ndll; d++) {
      for (uint32_t i = 0; i < n; i++)
        if (dref[i] == d) buf_u64(blob, rva0 + str_off + hn[i]);
      buf_u64(blob, 0);
    }
  for (uint32_t d = 0; d < ndll; d++) {
    buf_u32(blob, (uint32_t)(rva0 + ilt_off + (uint64_t)dstart[d] * 8));
    buf_u32(blob, 0);
    buf_u32(blob, 0);
    buf_u32(blob, (uint32_t)(rva0 + str_off + dn[d]));
    buf_u32(blob, (uint32_t)(rva0 + iat_off + (uint64_t)dstart[d] * 8));
  }
  for (int i = 0; i < 5; i++) buf_u32(blob, 0);
  buf_bytes(blob, str.data, str.len);

  L->got_addr = L->rodata_addr + iat_off;
  L->pe_idir_addr = L->rodata_addr + dir_off;
  L->pe_idir_size = (ndll + 1) * 20;
  L->pe_iat_size = nslots * 8;

  at = L->rodata_addr + blob->len;
  at = pe_align(at, 16);
  at = place(L, K_RODATA, at);
  L->rodata_size = at - L->rodata_addr;

  at = pe_align(at, PE_SECT_ALIGN);
  L->data_addr = at;
  at = place(L, K_DATA, at);
  L->data_size = at - L->data_addr;
  at = pe_align(at, 16);
  L->bss_addr = at;
  at = place(L, K_BSS, at);
  place_commons(L, &at);
  L->bss_size = at - L->bss_addr;

  xfree(str.data); xfree(dref); xfree(hn); xfree(dn); xfree(dstart);
}

static void pe_sechdr(Buf *b, const char *name, uint64_t vsz, uint64_t rva,
                      uint64_t raw, uint64_t foff, uint32_t ch) {
  char nm[8] = { 0 };
  memcpy(nm, name, strlen(name));
  buf_bytes(b, nm, 8);
  buf_u32(b, (uint32_t)vsz); buf_u32(b, (uint32_t)rva);
  buf_u32(b, (uint32_t)raw); buf_u32(b, (uint32_t)foff);
  buf_u32(b, 0); buf_u32(b, 0);
  buf_u16(b, 0); buf_u16(b, 0);
  buf_u32(b, ch);
}

/* The export directory, at rva: the DLL's name, and its exports sorted by
   name, as the loader's binary search wants them, each its own ordinal. */
static void pe_exports(Linker *L, const char *dllname, uint64_t rva, Buf *b) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < L->ngsyms; i++) if (L->gsyms[i].exported) n++;
  GSym **v = xmalloc((n ? n : 1) * sizeof(GSym *));
  uint32_t k = 0;
  for (uint32_t i = 0; i < L->ngsyms; i++)
    if (L->gsyms[i].exported) v[k++] = &L->gsyms[i];
  for (uint32_t i = 1; i < n; i++) {
    GSym *t = v[i]; uint32_t j = i;
    while (j && strcmp(v[j - 1]->name, t->name) > 0) { v[j] = v[j - 1]; j--; }
    v[j] = t;
  }
  uint64_t eat = rva + 40, names = eat + 4ull * n, ords = names + 4ull * n;
  uint64_t str = pe_align(ords + 2ull * n, 4);
  buf_u32(b, 0); buf_u32(b, 0); buf_u16(b, 0); buf_u16(b, 0);
  buf_u32(b, (uint32_t)str);
  buf_u32(b, 1);
  buf_u32(b, n); buf_u32(b, n);
  buf_u32(b, (uint32_t)eat); buf_u32(b, (uint32_t)names); buf_u32(b, (uint32_t)ords);
  for (uint32_t i = 0; i < n; i++) buf_u32(b, (uint32_t)(v[i]->addr - L->base));
  uint64_t at = str + strlen(dllname) + 1;
  for (uint32_t i = 0; i < n; i++) {
    buf_u32(b, (uint32_t)at);
    at += strlen(v[i]->name) + 1;
  }
  for (uint32_t i = 0; i < n; i++) buf_u16(b, (uint16_t)i);
  while (b->len < str - rva) buf_u8(b, 0);
  buf_bytes(b, dllname, strlen(dllname) + 1);
  for (uint32_t i = 0; i < n; i++) buf_bytes(b, v[i]->name, strlen(v[i]->name) + 1);
  xfree(v);
}

/* Base relocations: a block per 4 KiB page, a DIR64 entry per pointer. */
static void pe_relocs(Linker *L, Buf *b) {
  uint32_t n = L->nrebase;
  uint64_t *a = xmalloc((n ? n : 1) * sizeof(uint64_t));
  for (uint32_t i = 0; i < n; i++) a[i] = L->rebase[i] - L->base;
  for (uint32_t i = 1; i < n; i++) {
    uint64_t t = a[i]; uint32_t j = i;
    while (j && a[j - 1] > t) { a[j] = a[j - 1]; j--; }
    a[j] = t;
  }
  for (uint32_t i = 0; i < n;) {
    uint64_t page = a[i] & ~0xfffull;
    uint32_t j = i;
    while (j < n && (a[j] & ~0xfffull) == page) j++;
    uint32_t cnt = j - i, padded = (cnt + 1) & ~1u;
    buf_u32(b, (uint32_t)page);
    buf_u32(b, 8 + 2 * padded);
    for (uint32_t k = i; k < j; k++) buf_u16(b, (uint16_t)(0xa000 | (a[k] & 0xfff)));
    if (padded != cnt) buf_u16(b, 0);
    i = j;
  }
  xfree(a);
}

void write_pe_exec(Linker *L, const char *path, uint64_t entry) {
  const uint64_t base = L->base;
  uint64_t text_rva = L->text_addr - base;
  uint64_t rdata_rva = L->rodata_addr - base;
  uint64_t data_rva = L->data_addr - base;
  uint64_t data_vsz = (L->bss_addr + L->bss_size) - L->data_addr;
  uint64_t text_raw = pe_align(L->text_size, PE_FILE_ALIGN);
  uint64_t rdata_raw = pe_align(L->rodata_size, PE_FILE_ALIGN);
  uint64_t data_raw = pe_align(L->data_size, PE_FILE_ALIGN);
  uint32_t nsec = L->shared ? 5 : 3;
  uint64_t hdr_end = 0x40 + 4 + 20 + 0xf0 + nsec * 40;
  uint64_t hdr_raw = pe_align(hdr_end, PE_FILE_ALIGN);
  uint64_t text_foff = hdr_raw;
  uint64_t rdata_foff = text_foff + text_raw;
  uint64_t data_foff = rdata_foff + rdata_raw;
  uint64_t image_size = pe_align(data_rva + data_vsz, PE_SECT_ALIGN);

  Buf edata, reloc;
  memset(&edata, 0, sizeof edata);
  memset(&reloc, 0, sizeof reloc);
  uint64_t edata_rva = 0, reloc_rva = 0, edata_foff = 0, reloc_foff = 0;
  uint64_t edata_raw = 0, reloc_raw = 0;
  if (L->shared) {
    const char *dllname = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    edata_rva = image_size;
    pe_exports(L, dllname, edata_rva, &edata);
    reloc_rva = pe_align(edata_rva + edata.len, PE_SECT_ALIGN);
    pe_relocs(L, &reloc);
    image_size = pe_align(reloc_rva + (reloc.len ? reloc.len : 1), PE_SECT_ALIGN);
    edata_raw = pe_align(edata.len, PE_FILE_ALIGN);
    reloc_raw = pe_align(reloc.len, PE_FILE_ALIGN);
    edata_foff = data_foff + data_raw;
    reloc_foff = edata_foff + edata_raw;
  }

  Buf out; memset(&out, 0, sizeof out);
  buf_u8(&out, 'M'); buf_u8(&out, 'Z');
  while (out.len < 0x3c) buf_u8(&out, 0);
  buf_u32(&out, 0x40);

  buf_bytes(&out, "PE\0\0", 4);
  buf_u16(&out, 0x8664);
  buf_u16(&out, (uint16_t)nsec);
  buf_u32(&out, 0);
  buf_u32(&out, 0); buf_u32(&out, 0);
  buf_u16(&out, 0xf0);
  /* EXECUTABLE | RELOCS_STRIPPED | LARGE_ADDRESS_AWARE; a DLL keeps its
     relocations and says it is one */
  buf_u16(&out, L->shared ? 0x2022 : 0x0023);

  buf_u16(&out, 0x20b);    /* PE32+ */
  buf_u8(&out, 14); buf_u8(&out, 0);
  buf_u32(&out, (uint32_t)text_raw);
  buf_u32(&out, (uint32_t)(rdata_raw + data_raw + edata_raw + reloc_raw));
  buf_u32(&out, (uint32_t)pe_align(L->bss_size, PE_FILE_ALIGN));
  buf_u32(&out, L->shared ? 0u : (uint32_t)(entry - base));
  buf_u32(&out, (uint32_t)text_rva);
  buf_u64(&out, base);
  buf_u32(&out, (uint32_t)PE_SECT_ALIGN);
  buf_u32(&out, (uint32_t)PE_FILE_ALIGN);
  buf_u16(&out, 6); buf_u16(&out, 0);   /* min OS 6.0 */
  buf_u16(&out, 0); buf_u16(&out, 0);
  buf_u16(&out, 6); buf_u16(&out, 0);   /* subsystem 6.0 */
  buf_u32(&out, 0);
  buf_u32(&out, (uint32_t)image_size);
  buf_u32(&out, (uint32_t)hdr_raw);
  buf_u32(&out, 0);
  buf_u16(&out, 3);        /* console */
  /* NX_COMPAT | TERMINAL_SERVER_AWARE; a DLL moves: HIGH_ENTROPY_VA |
     DYNAMIC_BASE | NX_COMPAT */
  buf_u16(&out, L->shared ? 0x0160 : 0x8100);
  /* commit == reserve: the whole stack is committed up front, so generated
     code never needs __chkstk probing */
  buf_u64(&out, 8ull << 20);
  buf_u64(&out, 8ull << 20);
  buf_u64(&out, 1ull << 20);
  buf_u64(&out, 0x1000);
  buf_u32(&out, 0);
  buf_u32(&out, 16);
  for (int i = 0; i < 16; i++) {
    if (i == 0 && edata.len) {
      buf_u32(&out, (uint32_t)edata_rva);
      buf_u32(&out, (uint32_t)edata.len);
    } else if (i == 1 && L->pe_idir_size) {
      buf_u32(&out, (uint32_t)(L->pe_idir_addr - base));
      buf_u32(&out, L->pe_idir_size);
    } else if (i == 5 && reloc.len) {
      buf_u32(&out, (uint32_t)reloc_rva);
      buf_u32(&out, (uint32_t)reloc.len);
    } else if (i == 12 && L->pe_iat_size) {
      buf_u32(&out, (uint32_t)(L->got_addr - base));
      buf_u32(&out, L->pe_iat_size);
    } else {
      buf_u32(&out, 0); buf_u32(&out, 0);
    }
  }

  pe_sechdr(&out, ".text", L->text_size, text_rva, text_raw, text_foff, 0x60000020u);
  pe_sechdr(&out, ".rdata", L->rodata_size, rdata_rva, rdata_raw, rdata_foff, 0x40000040u);
  pe_sechdr(&out, ".data", data_vsz, data_rva, data_raw, data_foff, 0xc0000040u);
  if (L->shared) {
    pe_sechdr(&out, ".edata", edata.len, edata_rva, edata_raw, edata_foff, 0x40000040u);
    pe_sechdr(&out, ".reloc", reloc.len, reloc_rva, reloc_raw, reloc_foff, 0x42000040u);
  }

  emit_secs(&out, L, K_TEXT, L->text_addr, text_foff);
  while (out.len < text_foff + (L->stub_addr - L->text_addr)) buf_u8(&out, 0);
  for (uint32_t i = 0; i < L->nstub; i++) {
    uint64_t at = L->stub_addr + (uint64_t)i * PE_STUB_SIZE;
    uint64_t ga = L->got_addr + (uint64_t)L->gsyms[L->stub[i]].got_slot * 8;
    buf_u8(&out, 0xff); buf_u8(&out, 0x25);
    buf_u32(&out, (uint32_t)(int32_t)((int64_t)ga - (int64_t)(at + 6)));
  }
  while (out.len < rdata_foff) buf_u8(&out, 0);
  buf_bytes(&out, L->pe_imp.data, L->pe_imp.len);
  emit_secs(&out, L, K_RODATA, L->rodata_addr, rdata_foff);
  while (out.len < data_foff) buf_u8(&out, 0);
  emit_secs(&out, L, K_DATA, L->data_addr, data_foff);
  while (out.len < data_foff + data_raw) buf_u8(&out, 0);
  if (L->shared) {
    buf_bytes(&out, edata.data, edata.len);
    while (out.len < reloc_foff) buf_u8(&out, 0);
    buf_bytes(&out, reloc.data, reloc.len);
    while (out.len < reloc_foff + reloc_raw) buf_u8(&out, 0);
  }
  xfree(edata.data);
  xfree(reloc.data);

  FILE *f = fopen(path, "wb");
  if (!f) warmc_fatal("link: cannot write %s", path);
  if (out.len && fwrite(out.data, 1, out.len, f) != out.len)
    warmc_fatal("link: short write on %s", path);
  fclose(f);
  chmod_exec(path);
  xfree(out.data);
  buf_free(&L->pe_imp);
}
