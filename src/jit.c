/* The JIT's compiler side: a module compiled in this process, as `warmc -c`
   compiles it, and placed where it can run (src/jit.h).

   Compiling takes the compiler's global settings, so one compile runs at a
   time, each from the settings the CLI starts from.  Its object -- text,
   rodata, data and custom sections, the symbols and the relocations -- is
   kept as an image, whose relocations name either a place in its own
   sections or a symbol of the runtime.

   Loading maps the text within branch range of the runtime where there is
   room, so that its calls of warmc_trap, warmc_gc_load_stub and the rest
   are the BL (on x86-64, the call rel32) a linked program has.  On AArch64
   a process whose own runtime has taken the room around the library -- a
   Go program's, Node's -- has its text placed where the system has room,
   and a call of the runtime out of a BL's reach goes through a veneer at
   the end of the module's text, which branches through x16: nothing the
   compiler keeps is in x16 or x17 at a call, warmc_gc_load_stub's
   included (rt/start.S), and the veneer leaves the stack and the return
   address as the call made them, so that the runtime sees the frames it
   would have.  The data goes within ADRP's (RIP-relative addressing's)
   reach of the text.  The code is otherwise byte for byte what the object
   `-c` writes holds, at its new address. */

#include "warmc.h"
#include "ir.h"
#include "target.h"
#include "wat.h"
#include "component.h"
#include "jit.h"

#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <malloc.h>
/* The Win32 memory calls, declared here: windows.h names types after
   warmc.h's macros (VT_I8 among them).  The structures are x64's. */
typedef struct {
  void *BaseAddress, *AllocationBase;
  unsigned long AllocationProtect;
  unsigned short PartitionId;
  size_t RegionSize;
  unsigned long State, Protect, Type;
} MEMORY_BASIC_INFORMATION;
typedef struct {
  unsigned long oem, dwPageSize;
  void *lpMinimumApplicationAddress, *lpMaximumApplicationAddress;
  uintptr_t dwActiveProcessorMask;
  unsigned long dwNumberOfProcessors, dwProcessorType, dwAllocationGranularity;
  unsigned short wProcessorLevel, wProcessorRevision;
} SYSTEM_INFO;
_Static_assert(sizeof(MEMORY_BASIC_INFORMATION) == 48, "x64's MEMORY_BASIC_INFORMATION");
_Static_assert(sizeof(SYSTEM_INFO) == 48, "x64's SYSTEM_INFO");
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_DECOMMIT 0x4000u
#define MEM_RELEASE 0x8000u
#define MEM_FREE 0x10000u
#define PAGE_NOACCESS 0x01u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define PAGE_EXECUTE_READ 0x20u
__declspec(dllimport) void *__stdcall VirtualAlloc(void *p, size_t n, unsigned long type,
                                                   unsigned long prot);
__declspec(dllimport) int __stdcall VirtualFree(void *p, size_t n, unsigned long type);
__declspec(dllimport) int __stdcall VirtualProtect(void *p, size_t n, unsigned long prot,
                                                   unsigned long *was);
__declspec(dllimport) size_t __stdcall VirtualQuery(const void *p, MEMORY_BASIC_INFORMATION *m,
                                                    size_t n);
__declspec(dllimport) int __stdcall FlushInstructionCache(void *proc, const void *p, size_t n);
__declspec(dllimport) void *__stdcall GetCurrentProcess(void);
__declspec(dllimport) void __stdcall GetSystemInfo(SYSTEM_INFO *si);
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#endif

#ifndef WARMC_JIT_BUILD_TAG
#define WARMC_JIT_BUILD_TAG 0ull
#endif

/* ---- errors ------------------------------------------------------------ */

static char *fmt_err(const char *fmt, ...) {
  va_list ap, aq;
  va_start(ap, fmt);
  va_copy(aq, ap);
  int n = vsnprintf(NULL, 0, fmt, aq);
  va_end(aq);
  char *s = malloc(n > 0 ? (size_t)n + 1 : 1);
  if (s) vsnprintf(s, (size_t)n + 1, fmt, ap);
  va_end(ap);
  return s;
}

void jit_free(void *p) { free(p); }

/* ---- memory -------------------------------------------------------------- */

#ifdef __APPLE__
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

/* The pages the allocator holds free, given back to the system: a compile's
   working memory is freed when it ends, and malloc keeps freed memory for
   the next allocation, which in a JIT's host may be long in coming. */
static void release_heap(void) {
#ifdef __APPLE__
  malloc_zone_pressure_relief(NULL, 0);
#elif defined(__GLIBC__)
  malloc_trim(0);
#elif defined(_WIN32)
  _heapmin();
#endif
}

static void obj_free(ObjBuilder *o);

/* ---- the compiler's settings ------------------------------------------- */

static pthread_mutex_t g_compile_lock = PTHREAD_MUTEX_INITIALIZER;

/* What a compile starts from, whatever the one before it set: the CLI's
   defaults for the host as target, with the module an object other
   instances may call into (-c), not a program's only instance. */
static void reset_settings(OptOpts *opt) {
#if defined(__x86_64__)
  g_target = &warmc_target_x64;
  /* a host thread's stack is committed a guard page at a time on
     Windows, as a DLL's is (warmc --shared) */
  g_stack_probes = true;
#else
  g_target = &warmc_target_a64;
  g_stack_probes = false;
#endif
#if defined(__APPLE__)
  codegen_set_darwin_abi(true);
#else
  codegen_set_darwin_abi(false);
#endif
  codegen_set_symbol_prefix("warmc");
  codegen_set_display_prefix("");
  codegen_set_custom_sections(CUSTOM_DEFAULT);
  codegen_set_function_names(true);
  codegen_set_load_barrier(true);
  codegen_set_merge(true);
  codegen_set_compress_data(true);
  g_no_export_names = false;
  compile_set_jobs(0);
  compile_default_opts(opt);
}

/* Runs fn under the compile lock with the compiler's diagnostics caught:
   false with *err set when one was raised. */
typedef struct {
  const uint8_t *bin;
  size_t len;
  const JitOpts *opts;
  const char *text;
  /* what a diagnostic leaves for the catcher to free */
  Module *m;
  ObjBuilder *o;
  JitModule *partial;
  /* which tier the compile is of: 0 untiered, 1 the first, 2 the second,
     placed near the first's code (near) */
  int tier;
  void *near;           /* an ArenaPair, which lives as long as the process */
  /* results */
  JitModule *mod;
  uint8_t *wasm;
  size_t wasm_len;
  /* a tier's: where each function's code starts, by its index in the
     module (0 where it has none), and the second tier's code and image */
  uint64_t *entries;
  uint32_t nentries;
  JitCode *code2;
  JitImage *image2;
} CompileJob;

static bool run_caught(void (*fn)(CompileJob *), CompileJob *job, char **err) {
  pthread_mutex_lock(&g_compile_lock);
  xheap_begin();
  DiagCatch c;
  memset(&c, 0, sizeof c);
  DiagCatch *outer = warmc_diag_catch(&c);
  bool ok = true;
  if (setjmp(c.jb)) {
    ok = false;
    if (err) *err = fmt_err("%s: %s", c.tag ? c.tag : "error", c.msg ? c.msg : "");
    free(c.msg);
    /* what the job had made by then */
    if (job->o) obj_free(job->o);
    if (job->m) module_free(job->m);
    jit_module_free(job->partial, true);
    free(job->entries);
    job->entries = NULL;
  } else {
    fn(job);
  }
  /* nothing of a compile outlives it: the process goes on */
  compile_release();
  xheap_end();
  release_heap();
  warmc_diag_catch(outer);
  pthread_mutex_unlock(&g_compile_lock);
  return ok;
}

/* ---- types -------------------------------------------------------------- */

static uint8_t kind_of(const Module *m, ValType t) {
  switch (t & VT_KIND_MASK) {
    case VT_I32: return JIT_I32;
    case VT_I64: return JIT_I64;
    case VT_F32: return JIT_F32;
    case VT_F64: return JIT_F64;
    case VT_V128: return JIT_V128;
    default: break;
  }
  HeapType h = vt_heap(t);
  if (h == HT_EXTERN || h == HT_NOEXTERN) return JIT_EXTERNREF;
  if (h == HT_FUNC || h == HT_NOFUNC) return JIT_FUNCREF;
  if (h == HT_EXN || h == HT_NOEXN) return JIT_EXNREF;
  if (h >= 0 && (uint32_t)h < m->ntypes && m->types[h].kind == CT_FUNC) return JIT_FUNCREF;
  return JIT_ANYREF;
}

static char *dup_bytes(const char *s, uint32_t n) {
  char *d = malloc((size_t)n + 1);
  if (!d) warmc_fatal("out of memory");
  memcpy(d, s, n);
  d[n] = 0;
  return d;
}

static void func_type(const Module *m, uint32_t ti, JitFuncType *ft) {
  const CompType *t = &m->types[ti];
  ft->nparams = t->nparams;
  ft->nresults = t->nresults;
  uint32_t n = t->nparams + t->nresults;
  ft->types = malloc(n ? n : 1);
  if (!ft->types) warmc_fatal("out of memory");
  for (uint32_t i = 0; i < t->nparams; i++) ft->types[i] = kind_of(m, t->params[i]);
  for (uint32_t i = 0; i < t->nresults; i++)
    ft->types[t->nparams + i] = kind_of(m, t->results[i]);
}

static void table_type(const Module *m, const Table *t, JitExtern *x) {
  x->valkind = kind_of(m, t->elem_type);
  x->min = t->min;
  x->max = t->max;
  x->has_max = t->has_max;
  x->index64 = t->index64;
}

static void memory_type(const Memory *t, JitExtern *x) {
  x->min = t->min;
  x->max = t->max;
  x->has_max = t->has_max;
  x->index64 = t->index64;
}

/* Each import and export and its type, read before compile_module, which
   rewrites signatures (the trampoline, Go's stack pointer) and renumbers
   functions. */
static void read_types(const Module *m, JitModule *jm) {
  jm->nimports = m->nimports;
  jm->imports = calloc(m->nimports ? m->nimports : 1, sizeof(JitExtern));
  jm->nexports = m->nexports;
  jm->exports = calloc(m->nexports ? m->nexports : 1, sizeof(JitExtern));
  if (!jm->imports || !jm->exports) warmc_fatal("out of memory");
  uint32_t fi = 0, ti = 0, mi = 0, gi = 0, tgi = 0;
  for (uint32_t i = 0; i < m->nimports; i++) {
    const Import *im = &m->imports[i];
    JitExtern *x = &jm->imports[i];
    x->module = dup_bytes(im->module, im->module_len);
    x->module_len = im->module_len;
    x->name = dup_bytes(im->name, im->name_len);
    x->name_len = im->name_len;
    switch (im->kind) {
      case EK_FUNC:
        x->kind = JIT_EXT_FUNC;
        func_type(m, m->funcs[fi++].type_index, &x->ft);
        break;
      case EK_TABLE:
        x->kind = JIT_EXT_TABLE;
        table_type(m, &m->tables[ti++], x);
        break;
      case EK_MEM:
        x->kind = JIT_EXT_MEMORY;
        memory_type(&m->memories[mi++], x);
        break;
      case EK_GLOBAL:
        x->kind = JIT_EXT_GLOBAL;
        x->valkind = kind_of(m, m->globals[gi].type);
        x->mutable_ = m->globals[gi].mutable_;
        gi++;
        break;
      case EK_TAG:
        x->kind = JIT_EXT_TAG;
        func_type(m, m->tags[tgi++].type_index, &x->ft);
        break;
    }
  }
  jm->ftypes = calloc(m->ntypes ? m->ntypes : 1, sizeof(JitTypeId));
  if (!jm->ftypes) warmc_fatal("out of memory");
  for (uint32_t i = 0; i < m->ntypes; i++) {
    if (m->types[i].kind != CT_FUNC) continue;
    JitTypeId *t = &jm->ftypes[jm->nftypes++];
    t->id = m->types[i].id;
    func_type(m, i, &t->ft);
  }
  for (uint32_t i = 0; i < m->nexports; i++) {
    const Export *e = &m->exports[i];
    JitExtern *x = &jm->exports[i];
    x->name = dup_bytes(e->name, e->name_len);
    x->name_len = e->name_len;
    switch (e->kind) {
      case EK_FUNC:
        x->kind = JIT_EXT_FUNC;
        func_type(m, m->funcs[e->index].type_index, &x->ft);
        break;
      case EK_TABLE:
        x->kind = JIT_EXT_TABLE;
        table_type(m, &m->tables[e->index], x);
        break;
      case EK_MEM:
        x->kind = JIT_EXT_MEMORY;
        memory_type(&m->memories[e->index], x);
        break;
      case EK_GLOBAL:
        x->kind = JIT_EXT_GLOBAL;
        x->valkind = kind_of(m, m->globals[e->index].type);
        x->mutable_ = m->globals[e->index].mutable_;
        break;
      case EK_TAG:
        x->kind = JIT_EXT_TAG;
        func_type(m, m->tags[e->index].type_index, &x->ft);
        break;
    }
  }
}

static void free_externs(JitExtern *x, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    free(x[i].module);
    free(x[i].name);
    free(x[i].ft.types);
  }
  free(x);
}

/* ---- the image ------------------------------------------------------------
   What a module's sections need once they are placed: the relocations whose
   targets move with where they land -- a call of the runtime, an address
   in another section -- and the runtime's names they refer to.  One within
   a section, from a place to another place in it, is the same wherever the
   section lands (each lands on a page), and is made in the bytes as the
   compile ends, which keeps half of a large module's relocations out of
   what it keeps.  The sections themselves are not kept: they are copied to
   where they run as the compile ends, and serialising a module writes them
   from there (a load makes every field it relocates again, whatever it
   held). */

#define NSEC 4
#define JIT_EXT 0xff

/* at off in section sec, of the given kind (RelKind): to target in section
   tsec, the addend folded in, or with tsec JIT_EXT, to the runtime symbol
   ext[target] plus addend */
typedef struct {
  uint32_t off, target;
  int32_t addend;
  uint8_t kind, sec, tsec, pad;
} JitRel;
_Static_assert(sizeof(JitRel) == 16, "a relocation kept is sixteen bytes");

struct JitImage {
  JitRel *rel;
  uint32_t nrel;
  char **ext;
  uint32_t next;
  uint64_t seclen[NSEC];
  uint8_t md_sec;
  uint64_t md_off;
};

static void image_free(JitImage *im) {
  if (!im) return;
  free(im->rel);
  for (uint32_t i = 0; i < im->next; i++) free(im->ext[i]);
  free(im->ext);
  free(im);
}

static void obj_free(ObjBuilder *o) {
  for (uint32_t i = 0; i < o->nsyms; i++) {
    xfree(o->syms[i].name);
    xfree(o->syms[i].display);
  }
  xfree(o->syms);
  xfree(o->sym_ix);
  xfree(o->relocs);
  buf_free(&o->text);
  buf_free(&o->rodata);
  buf_free(&o->data);
  buf_free(&o->custom);
  memset(o, 0, sizeof *o);
}

static bool apply(uint8_t *at, uint64_t P, uint64_t SA, uint8_t kind);

static bool pc_relative(uint8_t kind) {
  return kind == REL_CALL26 || kind == REL_ADR_PAGE21 || kind == REL_ADD_LO12 ||
         kind == REL_LDST_LO12 || kind == REL_PC32;
}

/* The object's relocations as the image keeps them, those within a
   section made in its bytes now */
static JitImage *image_of(ObjBuilder *o) {
  JitImage *im = calloc(1, sizeof *im);
  if (!im) warmc_fatal("out of memory");
  Buf *secs[NSEC] = { &o->text, &o->rodata, &o->data, &o->custom };
  for (int i = 0; i < NSEC; i++) {
    im->seclen[i] = secs[i]->len;
    if (secs[i]->len > UINT32_MAX) warmc_fatal("a section of more than 4 GB");
  }
  uint32_t *ext_of = malloc(sizeof(uint32_t) * (o->nsyms ? o->nsyms : 1));
  if (!ext_of) warmc_fatal("out of memory");
  for (uint32_t i = 0; i < o->nsyms; i++) ext_of[i] = UINT32_MAX;
  uint32_t nkeep = 0, next = 0;
  for (int pass = 0; pass < 2; pass++) {
    if (pass) {
      im->rel = malloc(sizeof(JitRel) * (nkeep ? nkeep : 1));
      im->ext = malloc(sizeof(char *) * (next ? next : 1));
      if (!im->rel || !im->ext) warmc_fatal("out of memory");
    }
    for (uint32_t i = 0; i < o->nrelocs; i++) {
      const Reloc *r = &o->relocs[i];
      const Sym *s = &o->syms[r->sym];
      if (r->section < 0 || r->section >= NSEC || r->offset > secs[r->section]->len)
        warmc_fatal("internal: a relocation outside its section");
      if (s->section >= 0 && s->section == r->section && pc_relative((uint8_t)r->kind)) {
        if (pass) continue;
        uint64_t P = r->offset, SA = s->value + (uint64_t)r->addend;
        if (!apply(secs[r->section]->data + r->offset, P, SA, (uint8_t)r->kind))
          warmc_fatal("internal: a relocation within a section is out of reach");
        continue;
      }
      if (!pass) {
        nkeep++;
        if (s->section < 0 && ext_of[r->sym] == UINT32_MAX) ext_of[r->sym] = next++;
        continue;
      }
      JitRel *jr = &im->rel[im->nrel++];
      memset(jr, 0, sizeof *jr);
      jr->kind = (uint8_t)r->kind;
      jr->sec = (uint8_t)r->section;
      jr->off = (uint32_t)r->offset;
      if (s->section < 0) {
        uint32_t e = ext_of[r->sym];
        jr->tsec = JIT_EXT;
        jr->target = e;
        if (r->addend < INT32_MIN || r->addend > INT32_MAX)
          warmc_fatal("internal: a relocation's addend past 32 bits");
        jr->addend = (int32_t)r->addend;
      } else {
        if (s->section >= NSEC) warmc_fatal("internal: a symbol in section %d", s->section);
        uint64_t t = s->value + (uint64_t)r->addend;
        if (t > secs[s->section]->len) warmc_fatal("internal: a relocation's target outside its section");
        jr->tsec = (uint8_t)s->section;
        jr->target = (uint32_t)t;
      }
    }
  }
  /* the names, by the index each was given */
  for (uint32_t i = 0; i < o->nsyms; i++)
    if (ext_of[i] != UINT32_MAX)
      im->ext[ext_of[i]] = dup_bytes(o->syms[i].name, (uint32_t)strlen(o->syms[i].name));
  im->next = next;
  free(ext_of);
  uint32_t md = obj_find_sym(o, "warmc_module");
  if (md == UINT32_MAX || o->syms[md].section < 0)
    warmc_fatal("internal: the object defines no module descriptor");
  im->md_sec = (uint8_t)o->syms[md].section;
  im->md_off = o->syms[md].value;
  return im;
}

/* ---- compiling ---------------------------------------------------------- */

static JitCode *load(const JitImage *im, const uint8_t *const sec[NSEC],
                     void *near, char **err);
static uint64_t code_text(const JitCode *c);

/* Where each function's code starts in the object's text, by the
   function's index in the module as it was read (parse.c names it
   wasm_func_N, whatever pruning renumbers), and UINT64_MAX where it has
   none: imported, or left out as nothing reaches it (its symbol then names
   the stub that says so, codegen_drop_function) -- the functions a tier-up
   sends on. */
static uint64_t *func_entries(const Module *m, const ObjBuilder *o, uint32_t *n) {
  uint32_t top = 0;
  for (uint32_t j = 0; j < m->nfuncs; j++) {
    unsigned k;
    if (m->funcs[j].sym && sscanf(m->funcs[j].sym, "wasm_func_%u", &k) == 1 && k + 1 > top)
      top = k + 1;
  }
  uint64_t *e = malloc(sizeof(uint64_t) * (top ? top : 1));
  if (!e) warmc_fatal("out of memory");
  for (uint32_t k = 0; k < top; k++) e[k] = UINT64_MAX;
  for (uint32_t j = 0; j < m->nfuncs; j++) {
    unsigned k;
    if (m->funcs[j].imported || (m->reached && !m->reached[j]) || !m->funcs[j].sym ||
        sscanf(m->funcs[j].sym, "wasm_func_%u", &k) != 1)
      continue;
    uint32_t si = obj_find_sym((ObjBuilder *)o, m->funcs[j].sym);
    if (si != UINT32_MAX && o->syms[si].section == 0) e[k] = o->syms[si].value;
  }
  *n = top;
  return e;
}

static void do_compile(CompileJob *job) {
  OptOpts opt;
  reset_settings(&opt);
  if (job->opts) {
    if (job->opts->opt_level >= 0 && job->opts->opt_level <= 3)
      compile_set_level(&opt, job->opts->opt_level);
    else
      warmc_fatal("an optimisation level of %d; the levels are 0 to 3",
                  job->opts->opt_level);
    for (uint32_t i = 0; i < job->opts->nflags; i++)
      if (!compile_parse_flag(&opt, job->opts->flags[i]))
        warmc_fatal("unknown option %s: the JIT takes -O<n> and -f[no-]<pass>",
                    job->opts->flags[i]);
    if (job->opts->threads) compile_set_jobs(job->opts->threads);
  }
  compile_finish_opts(&opt);
  opt.sole_instance = false;
  if (job->tier == 1) {
    /* The first tier: -O0, and as the second tier has them what makes a
       function's code callable from the other tier's and the instance the
       same -- the signatures Go's and GHC's rewrites give their functions
       (go-sp, trampoline), and guard pages under the memories, which the
       instance is made with -- and an entry the tier-up can send on.  The
       register allocator tries one eviction rule, not five: what it saves
       is a quarter of an -O0 compile, for code that runs until the second
       tier's arrives. */
    OptOpts o2 = opt;
    compile_set_level(&opt, 0);
    opt.go_sp = o2.go_sp;
    opt.trampoline = o2.trampoline;
    opt.guard_pages = o2.guard_pages;
    opt.patchable = true;
    opt.ra_one_rule = true;
    /* every function its own code, sent on to its own: two that compile
       alike at -O0 need not at the level asked for, nor be of one type */
    codegen_set_merge(false);
  }

  if (wasm_is_component(job->bin, job->len))
    warmc_fatal("a component: the C API compiles core modules");
  g_wasm30 = opt.wasm30;
  Module *m = wasm_parse(job->bin, job->len);
  job->m = m;
  wasm_validate(m);

  JitModule *jm = NULL;
  if (job->tier != 2) {
    jm = calloc(1, sizeof *jm);
    if (!jm) warmc_fatal("out of memory");
    job->partial = jm;
    jm->build_tag = WARMC_JIT_BUILD_TAG;
    read_types(m, jm);
  }

  ObjBuilder o;
  memset(&o, 0, sizeof o);
  job->o = &o;
  compile_module(m, &o, &opt);
  if (job->tier) job->entries = func_entries(m, &o, &job->nentries);
  /* the module first: what is left is the object */
  module_free(m);
  job->m = NULL;
  JitImage *im = image_of(&o);
  const uint8_t *sec[NSEC] = { o.text.data, o.rodata.data, o.data.data, o.custom.data };
  char *lerr = NULL;
  JitCode *code = load(im, sec, job->near, &lerr);
  if (!code) {
    char msg[512];
    snprintf(msg, sizeof msg, "%s", lerr ? lerr : "the module cannot be placed");
    free(lerr);
    image_free(im);
    warmc_fatal("%s", msg);
  }
  obj_free(&o);
  job->o = NULL;
  for (uint32_t k = 0; job->tier && k < job->nentries; k++)
    if (job->entries[k] != UINT64_MAX) job->entries[k] += code_text(code);
  if (job->tier == 2) {
    job->code2 = code;
    job->image2 = im;
    return;
  }
  jm->image = im;
  jm->code = code;
  job->partial = NULL;
  job->mod = jm;
}

static void tier_start(JitModule *jm, const uint8_t *bin, size_t len, const JitOpts *o,
                       uint64_t *entries, uint32_t nentries);

JitModule *jit_compile(const uint8_t *bin, size_t len, const JitOpts *o,
                       char **err) {
  CompileJob job;
  memset(&job, 0, sizeof job);
  job.bin = bin;
  job.len = len;
  job.opts = o;
  job.tier = o && o->tiered ? 1 : 0;
  if (!run_caught(do_compile, &job, err)) return NULL;
  if (job.tier) tier_start(job.mod, bin, len, o, job.entries, job.nentries);
  return job.mod;
}

static void do_validate(CompileJob *job) {
  OptOpts opt;
  reset_settings(&opt);
  g_wasm30 = false;
  if (wasm_is_component(job->bin, job->len))
    warmc_fatal("a component: the C API compiles core modules");
  Module *m = wasm_parse(job->bin, job->len);
  job->m = m;
  wasm_validate(m);
  module_free(m);
  job->m = NULL;
}

bool jit_validate(const uint8_t *bin, size_t len, char **err) {
  CompileJob job;
  memset(&job, 0, sizeof job);
  job.bin = bin;
  job.len = len;
  return run_caught(do_validate, &job, err);
}

static void do_wat(CompileJob *job) {
  size_t n = 0;
  uint8_t *w = wat_assemble(job->text, job->len, "<wat>", &n);
  job->wasm = malloc(n ? n : 1);
  if (!job->wasm) warmc_fatal("out of memory");
  memcpy(job->wasm, w, n);
  job->wasm_len = n;
  xfree(w);
}

uint8_t *jit_wat2wasm(const char *text, size_t len, size_t *out_len, char **err) {
  CompileJob job;
  memset(&job, 0, sizeof job);
  job.text = text;
  job.len = len;
  if (!run_caught(do_wat, &job, err)) return NULL;
  *out_len = job.wasm_len;
  return job.wasm;
}

void jit_unload(JitCode *c);

static void tier_free(JitTier *t, bool unload);

void jit_module_free(JitModule *m, bool unload) {
  if (!m) return;
  if (m->tier) tier_free(m->tier, unload);
  if (unload) jit_unload(m->code);
  image_free(m->image);
  free_externs(m->imports, m->nimports);
  free_externs(m->exports, m->nexports);
  for (uint32_t i = 0; i < m->nftypes; i++) free(m->ftypes[i].ft.types);
  free(m->ftypes);
  free(m);
}

/* ---- the runtime's symbols ---------------------------------------------- */

static void *rt_sym(const char *name) {
  unsigned lo = 0, hi = warmc_jit_nrtsyms;
  while (lo < hi) {
    unsigned mid = lo + (hi - lo) / 2;
    int c = strcmp(name, warmc_jit_rtsyms[mid].name);
    if (!c) return warmc_jit_rtsyms[mid].addr;
    if (c < 0) hi = mid;
    else lo = mid + 1;
  }
  return NULL;
}

/* ---- code memory ---------------------------------------------------------
   Arenas reserved beside the runtime, in pairs: one for text, within branch
   range of every function of the runtime (MAP_JIT on macOS), and one for
   data, within ADRP's reach of that text.  The first pair is reserved
   before anything else of the library's takes address space -- the
   collector's heap, a gigabyte, and a linear memory's four were placed by
   the kernel right after the library, where no text could follow them --
   and a module takes pages of a pair and gives them back when it is
   unloaded.  When a pair is full, another is reserved as near the runtime
   as there is room. */

#if defined(__x86_64__)
#define CALL_REACH (1ull << 31)
#define DATA_REACH (1ull << 31)
#else
#define CALL_REACH (1ull << 27)
#define DATA_REACH (1ull << 32)
#endif
#define REACH_SLACK (1ull << 20)
#define TEXT_ARENA (96ull << 20)
#define DATA_ARENA (512ull << 20)
#define MIN_ARENA (4ull << 20)
/* a veneer: ldr x16, #8; br x16; and the address it branches to */
#if defined(__aarch64__)
#define VENEER_BYTES 16u
#else
#define VENEER_BYTES 0u
#endif

typedef struct { uintptr_t at; size_t len; } Span;
typedef struct {
  uint8_t *base;
  size_t len;
  bool text;
  Span *free_;
  size_t nfree, cap;
} Arena;
typedef struct ArenaPair { Arena text, data; struct ArenaPair *next; } ArenaPair;

static pthread_mutex_t g_map_lock = PTHREAD_MUTEX_INITIALIZER;
static ArenaPair *g_arenas;
static uintptr_t g_rt_lo, g_rt_hi;

static size_t page_size(void) {
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwAllocationGranularity;
#else
  return (size_t)sysconf(_SC_PAGESIZE);
#endif
}

static size_t round_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

/* Address space at hint or the nearest the system gives above it: text
   executable (on macOS, MAP_JIT, writable by a thread that asks), data
   inaccessible until committed. */
#ifndef _WIN32
static void *reserve_at(uintptr_t hint, size_t len, bool text) {
  int prot = PROT_NONE;
  int flags = MAP_PRIVATE | MAP_ANON;
#ifdef __APPLE__
  if (text) { prot = PROT_READ | PROT_WRITE | PROT_EXEC; flags |= MAP_JIT; }
#endif
  void *p = mmap((void *)hint, len, prot, flags, -1, 0);
  return p == MAP_FAILED ? NULL : p;
}
#endif

static void release(void *p, size_t len) {
#ifdef _WIN32
  (void)len;
  VirtualFree(p, 0, MEM_RELEASE);
#else
  munmap(p, len);
#endif
}

/* Pages of an arena made writable, and given back to the system: on
   macOS marked reusable, which takes them out of the process's footprint
   at once, as its malloc does with what it frees (MADV_FREE leaves them
   counted until the system needs them), and marked in use again before
   they are written. */
static bool commit(const Arena *a, void *p, size_t len) {
#ifdef _WIN32
  (void)a;
  return VirtualAlloc(p, len, MEM_COMMIT, PAGE_READWRITE) != NULL;
#else
#ifdef __APPLE__
  madvise(p, len, MADV_FREE_REUSE);
  if (a->text) return true;
#else
  (void)a;
#endif
  return mprotect(p, len, PROT_READ | PROT_WRITE) == 0;
#endif
}

static void decommit(const Arena *a, void *p, size_t len) {
#ifdef _WIN32
  (void)a;
  VirtualFree(p, len, MEM_DECOMMIT);
#elif defined(__APPLE__)
  if (!a->text) mprotect(p, len, PROT_READ | PROT_WRITE);
  madvise(p, len, MADV_FREE_REUSABLE);
  if (!a->text) mprotect(p, len, PROT_NONE);
#else
  (void)a;
  madvise(p, len, MADV_DONTNEED);
  mprotect(p, len, PROT_NONE);
#endif
}

/* len bytes of address space starting at or above lo and ending at or
   below hi, as near the runtime as the system gives: tried above it, then
   below; NULL when none lands there. */
#ifdef _WIN32
/* VirtualAlloc takes the address it is given or fails, so the room is
   found first: the free regions VirtualQuery lists, up from above, then
   down from below the runtime. */
static void *reserve_within(uintptr_t lo, uintptr_t hi, size_t len, bool text,
                            uintptr_t above) {
  (void)text;
  size_t gran = page_size();
  if (hi < lo || hi - lo < len) return NULL;
  MEMORY_BASIC_INFORMATION mi;
  for (uintptr_t a = round_up(above < lo ? lo : above, gran); a <= hi - len;) {
    if (!VirtualQuery((void *)a, &mi, sizeof mi)) break;
    uintptr_t end = (uintptr_t)mi.BaseAddress + mi.RegionSize;
    if (mi.State == MEM_FREE && end >= a + len) {
      void *p = VirtualAlloc((void *)a, len, MEM_RESERVE, PAGE_NOACCESS);
      if (p) return p;
    }
    if (end <= a) break;
    a = round_up(end, gran);
  }
  for (uintptr_t top = g_rt_lo & ~(uintptr_t)(gran - 1); top >= lo + len;) {
    if (!VirtualQuery((void *)(top - 1), &mi, sizeof mi)) break;
    uintptr_t base = (uintptr_t)mi.BaseAddress;
    if (mi.State == MEM_FREE) {
      uintptr_t a = (top - len) & ~(uintptr_t)(gran - 1);
      if (a >= base && a >= lo) {
        void *p = VirtualAlloc((void *)a, len, MEM_RESERVE, PAGE_NOACCESS);
        if (p) return p;
      }
    }
    if (base >= top) break;
    top = base & ~(uintptr_t)(gran - 1);
  }
  return NULL;
}
#else
/* The free gaps of the address space between lo and hi, in ascending
   order, as the system lists what is mapped: at most max of them, their
   count returned.  A hint is where mmap places a mapping only when the
   range there is free -- elsewhere it puts it where it likes, which on
   macOS is not the first room above the hint, and a mapping with MAP_JIT
   cannot be placed with MAP_FIXED -- so the room is found first, as
   Windows' VirtualQuery finds it.  In a Go program the pages just above
   the library are Go's own and malloc's, and the room in reach was
   missed. */
#define MAX_GAPS 256
static size_t free_gaps(uintptr_t lo, uintptr_t hi, uintptr_t gaps[][2], size_t max) {
  size_t n = 0;
  uintptr_t prev = 0;
#ifdef __APPLE__
  /* nothing maps below the first region, the main executable: the
     system's lowest address is where that was loaded */
  bool first = true;
#endif
#ifdef __APPLE__
  mach_vm_address_t addr = 0;
  for (;;) {
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t obj = MACH_PORT_NULL;
    if (mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64,
                       (vm_region_info_t)&info, &cnt, &obj) != KERN_SUCCESS)
      break;
    uintptr_t start = (uintptr_t)addr, end = (uintptr_t)(addr + size);
    if (first) { prev = start; first = false; }
#else
  FILE *maps = fopen("/proc/self/maps", "r");
  if (!maps) return 0;
  char line[512];
  while (fgets(line, sizeof line, maps)) {
    unsigned long a, b;
    if (sscanf(line, "%lx-%lx", &a, &b) != 2) continue;
    uintptr_t start = a, end = b;
#endif
    if (start > prev && start > lo && prev < hi && n < max) {
      gaps[n][0] = prev > lo ? prev : lo;
      gaps[n][1] = start < hi ? start : hi;
      n++;
    }
    if (end > prev) prev = end;
    if (prev >= hi) break;
#ifdef __APPLE__
    addr = end;
  }
#else
  }
  fclose(maps);
#endif
  if (prev < hi && n < max) {
    gaps[n][0] = prev > lo ? prev : lo;
    gaps[n][1] = hi;
    n++;
  }
  return n;
}

static void *reserve_in_gap(uintptr_t at, size_t len, bool text) {
  uint8_t *p = reserve_at(at, len, text);
  if (p && (uintptr_t)p == at) return p;
  /* taken meanwhile by another thread's mapping, or placed elsewhere */
  if (p) release(p, len);
  return NULL;
}

static void *reserve_within(uintptr_t lo, uintptr_t hi, size_t len, bool text,
                            uintptr_t above) {
  size_t gran = page_size();
  if (hi < lo || hi - lo < len) return NULL;
  uintptr_t gaps[MAX_GAPS][2];
  size_t n = free_gaps(lo, hi, gaps, MAX_GAPS);
  /* up from above the runtime, then down from below it */
  for (size_t i = 0; i < n; i++) {
    uintptr_t a = round_up(gaps[i][0] > above ? gaps[i][0] : above, gran);
    if (a < gaps[i][1] && gaps[i][1] - a >= len) {
      void *p = reserve_in_gap(a, len, text);
      if (p) return p;
    }
  }
  for (size_t i = n; i-- > 0;) {
    uintptr_t top = gaps[i][1] < g_rt_lo ? gaps[i][1] : g_rt_lo;
    if (top < len) continue;
    uintptr_t a = (top - len) & ~(uintptr_t)(gran - 1);
    if (a >= gaps[i][0] && a + len <= gaps[i][1]) {
      void *p = reserve_in_gap(a, len, text);
      if (p) return p;
    }
  }
  return NULL;
}
#endif

/* The runtime's span: where its functions, which text calls, and its data
   are.  False when the library has no table of them to say (a build that
   made it empty fails, see the Makefile): nothing can be placed then. */
static bool rt_span(void) {
  if (!warmc_jit_nrtsyms) return false;
  if (g_rt_hi) return true;
  uintptr_t lo = UINTPTR_MAX, hi = 0;
  for (unsigned i = 0; i < warmc_jit_nrtsyms; i++) {
    uintptr_t a = (uintptr_t)warmc_jit_rtsyms[i].addr;
    if (a < lo) lo = a;
    if (a > hi) hi = a;
  }
  g_rt_lo = lo;
  g_rt_hi = hi;
  return true;
}

static void arena_init(Arena *a, uint8_t *base, size_t len, bool text) {
  a->base = base;
  a->len = len;
  a->text = text;
  a->cap = 16;
  a->free_ = calloc(a->cap, sizeof(Span));
  a->free_[0].at = (uintptr_t)base;
  a->free_[0].len = len;
  a->nfree = a->free_ ? 1 : 0;
}

/* Another pair, as near the runtime as there is room; NULL when there is
   none within reach.  Under g_map_lock. */
static ArenaPair *pair_new(size_t want_text, size_t want_data) {
  if (!rt_span()) return NULL;
  uintptr_t lo = g_rt_hi > CALL_REACH - REACH_SLACK ? g_rt_hi - (CALL_REACH - REACH_SLACK) : 0;
  uintptr_t hi = g_rt_lo + (CALL_REACH - REACH_SLACK);
  uint8_t *t = NULL;
  size_t tlen = TEXT_ARENA;
  if (tlen < want_text) tlen = round_up(want_text, MIN_ARENA);
  for (; tlen >= MIN_ARENA && tlen >= want_text && !t; tlen /= 2)
    t = reserve_within(lo, hi, tlen, true, g_rt_hi + REACH_SLACK);
#if defined(__aarch64__)
  /* no room within reach: anywhere, the runtime's calls through veneers */
  if (!t) {
    tlen = TEXT_ARENA < want_text ? round_up(want_text, MIN_ARENA) : TEXT_ARENA;
    for (; tlen >= MIN_ARENA && tlen >= want_text && !t; tlen /= 2)
      t = reserve_at(0, tlen, true);
  }
#endif
  if (!t) return NULL;
  tlen *= 2;
  uintptr_t tb = (uintptr_t)t;
  uintptr_t dlo = tb + tlen > DATA_REACH - REACH_SLACK ? tb + tlen - (DATA_REACH - REACH_SLACK) : 0;
  uintptr_t dhi = tb + (DATA_REACH - REACH_SLACK);
  uint8_t *d = NULL;
  size_t dlen = DATA_ARENA;
  if (dlen < want_data) dlen = round_up(want_data, MIN_ARENA);
  for (; dlen >= MIN_ARENA && dlen >= want_data && !d; dlen /= 2)
    d = reserve_within(dlo, dhi, dlen, false, tb + tlen);
  if (!d) { release(t, tlen); return NULL; }
  dlen *= 2;
  ArenaPair *p = calloc(1, sizeof *p);
  if (!p) { release(t, tlen); release(d, dlen); return NULL; }
  arena_init(&p->text, t, tlen, true);
  arena_init(&p->data, d, dlen, false);
  p->next = g_arenas;
  g_arenas = p;
  return p;
}

void jit_reserve(void) {
  pthread_mutex_lock(&g_map_lock);
  if (!g_arenas) pair_new(0, 0);
  pthread_mutex_unlock(&g_map_lock);
}

/* As the library is loaded: before the program's own start-up runs, and
   whatever it maps then -- the Go runtime's heap arenas and malloc's zones
   leave no room within a branch of the runtime in a Go program by the time
   its main makes an engine.  Address space only: nothing is committed
   until a module is placed. */
__attribute__((constructor)) static void jit_reserve_at_load(void) { jit_reserve(); }

static uint8_t *arena_take(Arena *a, size_t len) {
  for (size_t i = 0; i < a->nfree; i++) {
    if (a->free_[i].len < len) continue;
    uint8_t *p = (uint8_t *)a->free_[i].at;
    a->free_[i].at += len;
    a->free_[i].len -= len;
    if (!a->free_[i].len) {
      memmove(&a->free_[i], &a->free_[i + 1], (a->nfree - i - 1) * sizeof(Span));
      a->nfree--;
    }
    return p;
  }
  return NULL;
}

/* the span back, kept sorted and merged with its neighbours */
static void arena_give(Arena *a, uint8_t *p, size_t len) {
  uintptr_t at = (uintptr_t)p;
  size_t i = 0;
  while (i < a->nfree && a->free_[i].at < at) i++;
  bool joins_prev = i > 0 && a->free_[i - 1].at + a->free_[i - 1].len == at;
  bool joins_next = i < a->nfree && at + len == a->free_[i].at;
  if (joins_prev && joins_next) {
    a->free_[i - 1].len += len + a->free_[i].len;
    memmove(&a->free_[i], &a->free_[i + 1], (a->nfree - i - 1) * sizeof(Span));
    a->nfree--;
  } else if (joins_prev) {
    a->free_[i - 1].len += len;
  } else if (joins_next) {
    a->free_[i].at = at;
    a->free_[i].len += len;
  } else {
    if (a->nfree == a->cap) {
      Span *n = realloc(a->free_, a->cap * 2 * sizeof(Span));
      if (!n) return;   /* the span is lost to the arena, not to correctness */
      a->free_ = n;
      a->cap *= 2;
    }
    memmove(&a->free_[i + 1], &a->free_[i], (a->nfree - i) * sizeof(Span));
    a->free_[i].at = at;
    a->free_[i].len = len;
    a->nfree++;
  }
}

struct JitCode {
  ArenaPair *pair;
  uint8_t *text;
  size_t text_map;
  uint8_t *data;
  size_t data_map;
  uint64_t base[NSEC];
  const void *md;
};

static uint64_t code_text(const JitCode *c) { return (uint64_t)(uintptr_t)c->text; }

void jit_unload(JitCode *c) {
  if (!c) return;
  pthread_mutex_lock(&g_map_lock);
  if (c->text) {
    decommit(&c->pair->text, c->text, c->text_map);
    arena_give(&c->pair->text, c->text, c->text_map);
  }
  if (c->data) {
    decommit(&c->pair->data, c->data, c->data_map);
    arena_give(&c->pair->data, c->data, c->data_map);
  }
  pthread_mutex_unlock(&g_map_lock);
  free(c);
}

/* Pages for a module's text and its data, from one pair; false when no
   pair has room and no new one can be reserved within reach.  The pair
   near first, when given: a second tier's code within a branch of the
   first's. */
static bool place(JitCode *c, ArenaPair *near) {
  pthread_mutex_lock(&g_map_lock);
  for (int round = near ? -1 : 0; round < 2 && !c->text; round++) {
    for (ArenaPair *p = round < 0 ? near : round ? pair_new(c->text_map, c->data_map) : g_arenas;
         p; p = round ? NULL : p->next) {
      uint8_t *t = arena_take(&p->text, c->text_map);
      if (!t) continue;
      uint8_t *d = arena_take(&p->data, c->data_map);
      if (!d) { arena_give(&p->text, t, c->text_map); continue; }
      if (!commit(&p->text, t, c->text_map) || !commit(&p->data, d, c->data_map)) {
        decommit(&p->text, t, c->text_map);
        decommit(&p->data, d, c->data_map);
        arena_give(&p->text, t, c->text_map);
        arena_give(&p->data, d, c->data_map);
        continue;
      }
      c->pair = p;
      c->text = t;
      c->data = d;
      break;
    }
  }
  pthread_mutex_unlock(&g_map_lock);
  return c->text != NULL;
}

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Applies one relocation at P (where its bytes are written, `at`), its
   target S + A; false when the target is out of the instruction's reach. */
static bool apply(uint8_t *at, uint64_t P, uint64_t SA, uint8_t kind) {
  switch ((RelKind)kind) {
    case REL_CALL26: {
      int64_t d = (int64_t)(SA - P);
      if ((d & 3) || d < -(1ll << 27) || d >= (1ll << 27)) return false;
      wr32(at, (rd32(at) & 0xfc000000u) | ((uint32_t)(d >> 2) & 0x03ffffffu));
      return true;
    }
    case REL_ADR_PAGE21: {
      int64_t d = (int64_t)((SA & ~0xfffull) - (P & ~0xfffull)) >> 12;
      if (d < -(1ll << 20) || d >= (1ll << 20)) return false;
      uint32_t imm = (uint32_t)d & 0x1fffffu;
      wr32(at, (rd32(at) & 0x9f00001fu) | ((imm & 3u) << 29) | ((imm >> 2) << 5));
      return true;
    }
    case REL_ADD_LO12: {
      wr32(at, (rd32(at) & 0xffc003ffu) | ((uint32_t)(SA & 0xfffu) << 10));
      return true;
    }
    case REL_LDST_LO12: {
      uint32_t insn = rd32(at);
      uint32_t scale = insn >> 30;
      /* a 128-bit vector register's: V set, size 0, opc's high bit set */
      if ((insn & (1u << 26)) && scale == 0 && (insn & (1u << 23))) scale = 4;
      uint32_t off = (uint32_t)(SA & 0xfffu);
      if (off & ((1u << scale) - 1)) return false;
      wr32(at, (insn & 0xffc003ffu) | ((off >> scale) << 10));
      return true;
    }
    case REL_ABS64:
      for (int b = 0; b < 8; b++) at[b] = (uint8_t)(SA >> (8 * b));
      return true;
    case REL_PC32: {
      int64_t d = (int64_t)(SA - P);
      if (d < INT32_MIN || d > INT32_MAX) return false;
      wr32(at, (uint32_t)(int32_t)d);
      return true;
    }
  }
  return false;
}

#if defined(__aarch64__)
static bool veneers_always(void) {
  static int on = -1;
  if (on < 0) on = getenv("WARMC_JIT_VENEERS") != NULL;
  return on;
}
#endif

/* Places an image's sections, sec[i] its bytes, and makes their
   relocations against where they landed; NULL with *err set when there is
   no room within reach of the runtime. */
static JitCode *load(const JitImage *im, const uint8_t *const sec[NSEC],
                     void *near, char **err) {
  void **ext = malloc(sizeof(void *) * (im->next ? im->next : 1));
  if (!ext) { *err = fmt_err("out of memory"); return NULL; }
  for (uint32_t i = 0; i < im->next; i++) {
    ext[i] = rt_sym(im->ext[i]);
    if (!ext[i]) {
      *err = fmt_err("the module refers to %s, which the runtime does not have",
                     im->ext[i]);
      free(ext);
      return NULL;
    }
  }
  size_t gran = page_size();
  JitCode *c = calloc(1, sizeof *c);
  if (!c) { free(ext); *err = fmt_err("out of memory"); return NULL; }
  /* the veneers after the text, one for each of the runtime's symbols */
  uint64_t veneer_at = round_up(im->seclen[0], 8);
  uint64_t text_len = veneer_at + (uint64_t)im->next * VENEER_BYTES;
  c->text_map = round_up(text_len ? text_len : 1, gran);
  /* rodata and the custom sections first, then data on a page of its own;
     all of it read-only once written */
  uint64_t custom_at = round_up(im->seclen[1], 16);
  uint64_t data_at = round_up(custom_at + im->seclen[3], gran);
  c->data_map = round_up(data_at + im->seclen[2] + 1, gran);
  if (!place(c, (ArenaPair *)near)) {
    *err = fmt_err("no memory within reach of the runtime for %zu bytes of code",
                   (size_t)im->seclen[0]);
    free(c);
    free(ext);
    return NULL;
  }
  c->base[0] = (uint64_t)(uintptr_t)c->text;
  c->base[1] = (uint64_t)(uintptr_t)c->data;
  c->base[2] = (uint64_t)(uintptr_t)c->data + data_at;
  c->base[3] = (uint64_t)(uintptr_t)c->data + custom_at;

#ifdef __APPLE__
  pthread_jit_write_protect_np(0);
#endif
  for (int i = 0; i < NSEC; i++)
    if (im->seclen[i]) memcpy((uint8_t *)(uintptr_t)c->base[i], sec[i], im->seclen[i]);
#if defined(__aarch64__)
  /* ldr x16, #8; br x16; the symbol's address */
  for (uint32_t i = 0; i < im->next; i++) {
    uint8_t *v = c->text + veneer_at + (uint64_t)i * VENEER_BYTES;
    wr32(v, 0x58000050u);
    wr32(v + 4, 0xd61f0200u);
    uint64_t a = (uint64_t)(uintptr_t)ext[i];
    for (int b = 0; b < 8; b++) v[8 + b] = (uint8_t)(a >> (8 * b));
  }
#endif
  const char *bad = NULL;
  for (uint32_t i = 0; i < im->nrel && !bad; i++) {
    const JitRel *r = &im->rel[i];
    uint64_t SA = r->tsec == JIT_EXT ? (uint64_t)(uintptr_t)ext[r->target] + (uint64_t)(int64_t)r->addend
                                     : c->base[r->tsec] + r->target;
    uint64_t P = c->base[r->sec] + r->off;
#if defined(__aarch64__)
    /* WARMC_JIT_VENEERS, for tests: every call of the runtime through its
       veneer, as a module placed out of reach makes them */
    if (!(veneers_always() && r->kind == REL_CALL26 && r->tsec == JIT_EXT && !r->addend))
#endif
    if (apply((uint8_t *)(uintptr_t)P, P, SA, r->kind)) continue;
#if defined(__aarch64__)
    /* a call of the runtime out of reach: through its veneer */
    if (r->kind == REL_CALL26 && r->tsec == JIT_EXT && !r->addend &&
        apply((uint8_t *)(uintptr_t)P, P,
              c->base[0] + veneer_at + (uint64_t)r->target * VENEER_BYTES, r->kind))
      continue;
#endif
    bad = r->tsec == JIT_EXT ? im->ext[r->target] : "the module's own code or data";
  }
#ifdef __APPLE__
  pthread_jit_write_protect_np(1);
  sys_icache_invalidate(c->text, text_len);
#elif defined(_WIN32)
  unsigned long was;
  VirtualProtect(c->text, c->text_map, PAGE_EXECUTE_READ, &was);
  FlushInstructionCache(GetCurrentProcess(), c->text, text_len);
#else
  mprotect(c->text, c->text_map, PROT_READ | PROT_EXEC);
  __builtin___clear_cache((char *)c->text, (char *)c->text + text_len);
#endif
  free(ext);
  if (bad) {
    *err = fmt_err("a relocation against %s is out of reach", bad);
    jit_unload(c);
    return NULL;
  }
  /* nothing writes a module's data once it is placed: the descriptor and
     the tables it points to are the runtime's to read */
#ifdef _WIN32
  VirtualProtect(c->data, c->data_map, PAGE_READONLY, &was);
#else
  mprotect(c->data, c->data_map, PROT_READ);
#endif
  c->md = (const void *)(uintptr_t)(c->base[im->md_sec] + im->md_off);
  return c;
}

const void *jit_module_desc(const JitModule *m) { return m->code ? m->code->md : NULL; }

/* ---- tiers ----------------------------------------------------------------
   A tiered module compiles at -O0 first (do_compile's first tier), which is
   what jit_compile hands back, and again at the level asked for on a
   thread of its own, one module at a time, under the compile lock as any
   compile is.  The second tier's code is placed beside the first's and
   its descriptor given to the runtime, which then finds its frames as the
   module's own -- to unwind them, walk them for the collector, trap in
   them.  The first instruction of each function's -O0 code, a nop put
   there for this (OptOpts.patchable), then becomes a branch to its second
   tier's code: every way into a function -- a call from -O0 code, an
   export, a reference in a table, an import of it by another module --
   goes through the -O0 code's entry, and so on to the optimised code from
   then on, while the code addresses the instance, the tables and the C
   API's handles hold stay what they were.  The second tier's code calls
   its own functions straight.

   The two tiers keep to one convention between them: a call from either
   to either.  A function whose call is running when the tier-up comes
   runs to its end in the code it started in -- a loop in -O0 code stays
   there until its function returns -- so it is the calls after the
   tier-up that run optimised.

   Both tiers' code stays until the module goes; a second tier that
   arrives after it is unloaded again unused. */

void warmc_rt_add_desc(const void *md);

enum { TIER_QUEUED, TIER_DONE, TIER_FAILED };

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

struct JitTier {
  JitModule *jm;              /* NULL once the module needs it no more */
  uint8_t *bin;
  size_t len;
  int level;
  char **flags;
  uint32_t nflags, threads;
  uint64_t *entry0;           /* each function's -O0 code, or UINT64_MAX */
  uint32_t nentry0;
  JitCode *code2;
  JitImage *image2;
  int state;
  int refs;                   /* the module's, and the queue's until done */
  double t0;                  /* when the -O0 code was ready (WARMC_TIER_DEBUG) */
  bool wanted;                /* something waits for it (WARMC_TIER_DEFER) */
  JitTier *next;
};

static pthread_mutex_t g_tier_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_tier_cv = PTHREAD_COND_INITIALIZER;
static JitTier *g_tier_head, *g_tier_tail;
static bool g_tier_running;
static bool g_tier_busy;          /* the worker is compiling one */
static JitTier *g_tier_current;   /* which */
/* WARMC_TIER_DEFER, for tests: a second tier goes in place only once
   something waits for it (jit_module_optimized, jit_tiers_wait), never on
   its own -- so that a test knows the -O0 code is what runs until it asks,
   and that the tier-up comes while the frames it has made are live */
static int g_tier_defer = -1;

static void tier_unref_locked(JitTier *t) {
  if (--t->refs) return;
  free(t->bin);
  for (uint32_t i = 0; i < t->nflags; i++) free(t->flags[i]);
  free(t->flags);
  free(t->entry0);
  free(t);
}

/* One function's -O0 entry sent to its second tier's code: the nop
   becomes a B (on x86-64, the five-byte nop a jmp rel32, in one store of
   the aligned word it begins).  Both are an instruction another core may
   be running as it changes, the old one or the new. */
static bool tier_patch_one(uint64_t at, uint64_t to) {
#if defined(__x86_64__)
  if (at & 7) return false;
  uint8_t *p = (uint8_t *)(uintptr_t)at;
  static const uint8_t nop5[5] = { 0x0f, 0x1f, 0x44, 0x00, 0x00 };
  if (memcmp(p, nop5, 5)) return false;
  int64_t d = (int64_t)(to - (at + 5));
  if (d < INT32_MIN || d > INT32_MAX) return false;
  uint64_t w;
  memcpy(&w, p, 8);
  w = (w & ~0xffffffffffull) | 0xe9u | ((uint64_t)(uint32_t)(int32_t)d << 8);
  __atomic_store_n((uint64_t *)(uintptr_t)at, w, __ATOMIC_RELEASE);
  return true;
#else
  uint32_t *p = (uint32_t *)(uintptr_t)at;
  if (*p != 0xd503201fu) return false;
  int64_t d = (int64_t)(to - at);
  if ((d & 3) || d < -(1ll << 27) || d >= (1ll << 27)) return false;
  __atomic_store_n(p, 0x14000000u | ((uint32_t)(d >> 2) & 0x03ffffffu), __ATOMIC_RELEASE);
  return true;
#endif
}

/* Every function of the first tier that the second has sent to it; how
   many were.  The text is writable while it runs: on macOS for this thread
   alone, elsewhere also executable, which other threads go on running. */
static uint32_t tier_patch(const JitCode *c0, const uint64_t *e0, uint32_t n0,
                           const uint64_t *e2, uint32_t n2) {
  uint32_t n = 0;
#ifdef __APPLE__
  pthread_jit_write_protect_np(0);
#elif defined(_WIN32)
  unsigned long was;
  if (!VirtualProtect(c0->text, c0->text_map, 0x40u /* PAGE_EXECUTE_READWRITE */, &was))
    return 0;
#else
  if (mprotect(c0->text, c0->text_map, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;
#endif
  for (uint32_t k = 0; k < n0 && k < n2; k++)
    if (e0[k] != UINT64_MAX && e2[k] != UINT64_MAX && tier_patch_one(e0[k], e2[k])) n++;
#ifdef __APPLE__
  pthread_jit_write_protect_np(1);
  sys_icache_invalidate(c0->text, c0->text_map);
#elif defined(_WIN32)
  VirtualProtect(c0->text, c0->text_map, PAGE_EXECUTE_READ, &was);
  FlushInstructionCache(GetCurrentProcess(), c0->text, c0->text_map);
#else
  mprotect(c0->text, c0->text_map, PROT_READ | PROT_EXEC);
  __builtin___clear_cache((char *)c0->text, (char *)c0->text + c0->text_map);
#endif
  return n;
}

/* The second tier of t's module: compiled, then put in place unless the
   module has gone meanwhile. */
static void tier_run(JitTier *t) {
  JitOpts o;
  memset(&o, 0, sizeof o);
  o.opt_level = t->level;
  o.flags = (const char *const *)t->flags;
  o.nflags = t->nflags;
  o.threads = t->threads;
  CompileJob job;
  memset(&job, 0, sizeof job);
  job.bin = t->bin;
  job.len = t->len;
  job.opts = &o;
  job.tier = 2;
  pthread_mutex_lock(&g_tier_lock);
  job.near = t->jm ? (void *)t->jm->code->pair : NULL;
  pthread_mutex_unlock(&g_tier_lock);
  bool ok = job.near && run_caught(do_compile, &job, NULL);
  pthread_mutex_lock(&g_tier_lock);
  if (g_tier_defer < 0) g_tier_defer = warmc_getenv("WARMC_TIER_DEFER") != NULL;
  while (ok && g_tier_defer && t->jm && !t->wanted) pthread_cond_wait(&g_tier_cv, &g_tier_lock);
  if (ok && t->jm) {
    warmc_rt_add_desc(job.code2->md);
    uint32_t sent = tier_patch(t->jm->code, t->entry0, t->nentry0, job.entries, job.nentries);
    if (warmc_getenv("WARMC_TIER_DEBUG")) {
      uint32_t n0 = 0;
      for (uint32_t k = 0; k < t->nentry0; k++) n0 += t->entry0[k] != UINT64_MAX;
      fprintf(stderr, "warmc: tier-up: %u of %u functions sent to -O%d code, %.1f ms after the -O0 code\n",
              sent, n0, t->level, now_ms() - t->t0);
    }
    t->code2 = job.code2;
    t->image2 = job.image2;
    t->state = TIER_DONE;
  } else {
    if (ok) {
      jit_unload(job.code2);
      image_free(job.image2);
    }
    t->state = TIER_FAILED;
  }
  pthread_cond_broadcast(&g_tier_cv);
  tier_unref_locked(t);
  pthread_mutex_unlock(&g_tier_lock);
  free(job.entries);
}

static void *tier_worker(void *arg) {
  (void)arg;
  pthread_mutex_lock(&g_tier_lock);
  for (;;) {
    while (!g_tier_head) pthread_cond_wait(&g_tier_cv, &g_tier_lock);
    JitTier *t = g_tier_head;
    g_tier_head = t->next;
    if (!g_tier_head) g_tier_tail = NULL;
    t->next = NULL;
    if (!t->jm) {
      /* the module went before its turn */
      t->state = TIER_FAILED;
      pthread_cond_broadcast(&g_tier_cv);
      tier_unref_locked(t);
      continue;
    }
    g_tier_busy = true;
    g_tier_current = t;
    pthread_mutex_unlock(&g_tier_lock);
    tier_run(t);
    pthread_mutex_lock(&g_tier_lock);
    g_tier_busy = false;
    g_tier_current = NULL;
    pthread_cond_broadcast(&g_tier_cv);
  }
  return NULL;
}

void jit_tiers_wait(void) {
  pthread_mutex_lock(&g_tier_lock);
  for (JitTier *t = g_tier_head; t; t = t->next) t->wanted = true;
  if (g_tier_current) g_tier_current->wanted = true;
  pthread_cond_broadcast(&g_tier_cv);
  while (g_tier_head || g_tier_busy) pthread_cond_wait(&g_tier_cv, &g_tier_lock);
  pthread_mutex_unlock(&g_tier_lock);
}

static char *dup_cstr(const char *s) {
  size_t n = strlen(s);
  char *d = malloc(n + 1);
  if (!d) return NULL;
  memcpy(d, s, n + 1);
  return d;
}

/* Queues the module's second tier; with no memory for it, the module
   keeps its first. */
static void tier_start(JitModule *jm, const uint8_t *bin, size_t len, const JitOpts *o,
                       uint64_t *entries, uint32_t nentries) {
  JitTier *t = calloc(1, sizeof *t);
  uint8_t *b = malloc(len ? len : 1);
  char **fl = calloc(o->nflags ? o->nflags : 1, sizeof(char *));
  bool ok = t && b && fl;
  for (uint32_t i = 0; ok && i < o->nflags; i++) ok = (fl[i] = dup_cstr(o->flags[i])) != NULL;
  if (!ok) {
    if (fl) for (uint32_t i = 0; i < o->nflags; i++) free(fl[i]);
    free(fl); free(b); free(t); free(entries);
    return;
  }
  memcpy(b, bin, len);
  t->jm = jm;
  t->bin = b;
  t->len = len;
  t->level = o->opt_level;
  t->flags = fl;
  t->nflags = o->nflags;
  t->threads = o->threads;
  t->entry0 = entries;
  t->nentry0 = nentries;
  t->state = TIER_QUEUED;
  t->refs = 2;
  t->t0 = now_ms();
  jm->tier = t;
  pthread_mutex_lock(&g_tier_lock);
  if (g_tier_tail) g_tier_tail->next = t;
  else g_tier_head = t;
  g_tier_tail = t;
  if (!g_tier_running) {
    pthread_t th;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    /* the compiler's recursion is deep on a big function */
    pthread_attr_setstacksize(&a, 64u << 20);
    if (pthread_create(&th, &a, tier_worker, NULL) == 0) g_tier_running = true;
    pthread_attr_destroy(&a);
  }
  if (!g_tier_running) {
    /* no thread for it: the module keeps its first tier */
    g_tier_head = g_tier_tail = NULL;
    t->state = TIER_FAILED;
    t->refs = 1;
  }
  pthread_cond_broadcast(&g_tier_cv);
  pthread_mutex_unlock(&g_tier_lock);
}

bool jit_module_optimized(const JitModule *m, bool wait) {
  JitTier *t = m->tier;
  if (!t) return true;
  pthread_mutex_lock(&g_tier_lock);
  if (wait) {
    t->wanted = true;
    pthread_cond_broadcast(&g_tier_cv);
  }
  while (wait && t->state == TIER_QUEUED) pthread_cond_wait(&g_tier_cv, &g_tier_lock);
  bool done = t->state == TIER_DONE;
  pthread_mutex_unlock(&g_tier_lock);
  return done;
}

const void *jit_module_detach_tier(JitModule *m) {
  JitTier *t = m ? m->tier : NULL;
  if (!t) return NULL;
  pthread_mutex_lock(&g_tier_lock);
  t->jm = NULL;
  /* a deferred one waits no more, to be unloaded */
  pthread_cond_broadcast(&g_tier_cv);
  const void *md = t->state == TIER_DONE && t->code2 ? t->code2->md : NULL;
  pthread_mutex_unlock(&g_tier_lock);
  return md;
}

static void tier_free(JitTier *t, bool unload) {
  pthread_mutex_lock(&g_tier_lock);
  t->jm = NULL;
  pthread_cond_broadcast(&g_tier_cv);
  JitCode *c2 = t->code2;
  JitImage *im2 = t->image2;
  t->code2 = NULL;
  t->image2 = NULL;
  tier_unref_locked(t);
  pthread_mutex_unlock(&g_tier_lock);
  if (unload) jit_unload(c2);
  image_free(im2);
}

/* ---- serialising ---------------------------------------------------------
   "WARMJIT\2", the build tag, then the types, the sections as the module's
   code has them where it runs, and the image, each count and length a u64
   and each byte string its bytes, little-endian.  A section's relocated
   fields hold what this process's addresses made them, which the load of
   it makes again. */

static const uint8_t g_magic[8] = { 'W', 'A', 'R', 'M', 'J', 'I', 'T', 2 };

static void put64(Buf *b, uint64_t v) { buf_u64(b, v); }
static void putbytes(Buf *b, const void *p, uint64_t n) { put64(b, n); buf_bytes(b, p, n); }

static void put_extern(Buf *b, const JitExtern *x, bool imp) {
  if (imp) putbytes(b, x->module, x->module_len);
  putbytes(b, x->name, x->name_len);
  uint8_t f[5] = { x->kind, x->valkind, x->mutable_, x->index64, x->has_max };
  buf_bytes(b, f, 5);
  put64(b, x->min);
  put64(b, x->max);
  put64(b, x->ft.nparams);
  put64(b, x->ft.nresults);
  buf_bytes(b, x->ft.types, x->ft.nparams + x->ft.nresults);
}

uint8_t *jit_serialize(const JitModule *m, size_t *len) {
  const JitImage *im = m->image;
  const JitCode *c = m->code;
  /* a tiered module's optimised code, which a load of it runs from the
     start; its -O0 code when that never comes */
  if (m->tier && jit_module_optimized(m, true)) {
    pthread_mutex_lock(&g_tier_lock);
    if (m->tier->code2) { im = m->tier->image2; c = m->tier->code2; }
    pthread_mutex_unlock(&g_tier_lock);
  }
  if (!im || !c) return NULL;
  /* the bytes are a Buf's, xmalloc's: not while a compile has its heap */
  pthread_mutex_lock(&g_compile_lock);
  Buf b;
  memset(&b, 0, sizeof b);
  buf_bytes(&b, g_magic, 8);
  put64(&b, m->build_tag);
  put64(&b, m->nimports);
  for (uint32_t i = 0; i < m->nimports; i++) put_extern(&b, &m->imports[i], true);
  put64(&b, m->nexports);
  for (uint32_t i = 0; i < m->nexports; i++) put_extern(&b, &m->exports[i], false);
  put64(&b, m->nftypes);
  for (uint32_t i = 0; i < m->nftypes; i++) {
    const JitFuncType *ft = &m->ftypes[i].ft;
    put64(&b, m->ftypes[i].id);
    put64(&b, ft->nparams);
    put64(&b, ft->nresults);
    buf_bytes(&b, ft->types, ft->nparams + ft->nresults);
  }
  for (int i = 0; i < NSEC; i++)
    putbytes(&b, (const void *)(uintptr_t)c->base[i], im->seclen[i]);
  put64(&b, im->next);
  for (uint32_t i = 0; i < im->next; i++) putbytes(&b, im->ext[i], strlen(im->ext[i]));
  put64(&b, im->nrel);
  buf_bytes(&b, im->rel, (size_t)im->nrel * sizeof(JitRel));
  buf_u8(&b, im->md_sec);
  put64(&b, im->md_off);
  uint8_t *out = malloc(b.len ? b.len : 1);
  if (out) memcpy(out, b.data, b.len);
  *len = b.len;
  buf_free(&b);
  pthread_mutex_unlock(&g_compile_lock);
  return out;
}

typedef struct { const uint8_t *p, *end; bool bad; } Rd;

static uint64_t get64(Rd *r) {
  if (r->end - r->p < 8) { r->bad = true; r->p = r->end; return 0; }
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= (uint64_t)r->p[i] << (8 * i);
  r->p += 8;
  return v;
}

static const uint8_t *getn(Rd *r, uint64_t n) {
  if ((uint64_t)(r->end - r->p) < n) { r->bad = true; r->p = r->end; return NULL; }
  const uint8_t *q = r->p;
  r->p += n;
  return q;
}

/* a byte string, as a NUL-terminated copy */
static char *getstr(Rd *r, uint32_t *len) {
  uint64_t n = get64(r);
  const uint8_t *q = getn(r, n);
  if (!q || n > UINT32_MAX) { r->bad = true; return NULL; }
  char *s = malloc(n + 1);
  if (!s) { r->bad = true; return NULL; }
  memcpy(s, q, n);
  s[n] = 0;
  if (len) *len = (uint32_t)n;
  return s;
}

static void get_extern(Rd *r, JitExtern *x, bool imp) {
  if (imp) x->module = getstr(r, &x->module_len);
  x->name = getstr(r, &x->name_len);
  const uint8_t *f = getn(r, 5);
  if (!f) return;
  x->kind = f[0]; x->valkind = f[1]; x->mutable_ = f[2]; x->index64 = f[3]; x->has_max = f[4];
  x->min = get64(r);
  x->max = get64(r);
  uint64_t np = get64(r), nr = get64(r);
  if (np > UINT32_MAX || nr > UINT32_MAX || np + nr > (uint64_t)(r->end - r->p)) {
    r->bad = true;
    return;
  }
  x->ft.nparams = (uint32_t)np;
  x->ft.nresults = (uint32_t)nr;
  x->ft.types = malloc(np + nr ? np + nr : 1);
  const uint8_t *t = getn(r, np + nr);
  if (!x->ft.types || !t) { r->bad = true; return; }
  memcpy(x->ft.types, t, np + nr);
}

/* The module p holds, placed: its sections are loaded from p itself, which
   the caller keeps no longer than the call. */
JitModule *jit_deserialize(const uint8_t *p, size_t len, char **err) {
  Rd r = { p, p + len, false };
  const uint8_t *mg = getn(&r, 8);
  if (!mg || memcmp(mg, g_magic, 8)) {
    *err = fmt_err("not a module this warmc serialised");
    return NULL;
  }
  JitModule *m = calloc(1, sizeof *m);
  JitImage *im = calloc(1, sizeof *im);
  if (!m || !im) { free(m); free(im); *err = fmt_err("out of memory"); return NULL; }
  m->image = im;
  m->build_tag = get64(&r);
  if (m->build_tag != WARMC_JIT_BUILD_TAG) {
    jit_module_free(m, false);
    *err = fmt_err("the module was serialised by another build of warmc");
    return NULL;
  }
  uint64_t n = get64(&r);
  if (n > (uint64_t)(r.end - r.p)) r.bad = true;
  if (!r.bad) {
    m->nimports = (uint32_t)n;
    m->imports = calloc(n ? n : 1, sizeof(JitExtern));
    for (uint32_t i = 0; m->imports && i < m->nimports && !r.bad; i++)
      get_extern(&r, &m->imports[i], true);
  }
  n = r.bad ? 0 : get64(&r);
  if (n > (uint64_t)(r.end - r.p)) r.bad = true;
  if (!r.bad) {
    m->nexports = (uint32_t)n;
    m->exports = calloc(n ? n : 1, sizeof(JitExtern));
    for (uint32_t i = 0; m->exports && i < m->nexports && !r.bad; i++)
      get_extern(&r, &m->exports[i], false);
  }
  n = r.bad ? 0 : get64(&r);
  if (n > (uint64_t)(r.end - r.p)) r.bad = true;
  if (!r.bad) {
    m->ftypes = calloc(n ? n : 1, sizeof(JitTypeId));
    for (uint64_t i = 0; m->ftypes && i < n && !r.bad; i++) {
      JitTypeId *t = &m->ftypes[i];
      t->id = get64(&r);
      uint64_t np = get64(&r), nr = get64(&r);
      if (np > UINT32_MAX || nr > UINT32_MAX || np + nr > (uint64_t)(r.end - r.p)) {
        r.bad = true;
        break;
      }
      t->ft.nparams = (uint32_t)np;
      t->ft.nresults = (uint32_t)nr;
      t->ft.types = malloc(np + nr ? np + nr : 1);
      m->nftypes++;
      const uint8_t *q = getn(&r, np + nr);
      if (!t->ft.types || !q) { r.bad = true; break; }
      memcpy(t->ft.types, q, np + nr);
    }
  }
  const uint8_t *sec[NSEC] = { 0 };
  for (int i = 0; i < NSEC && !r.bad; i++) {
    uint64_t sl = get64(&r);
    sec[i] = getn(&r, sl);
    if (!sec[i] || sl > UINT32_MAX) { r.bad = true; break; }
    im->seclen[i] = sl;
  }
  n = r.bad ? 0 : get64(&r);
  if (n > (uint64_t)(r.end - r.p)) r.bad = true;
  if (!r.bad) {
    im->ext = calloc(n ? n : 1, sizeof(char *));
    for (uint64_t i = 0; im->ext && i < n && !r.bad; i++) {
      im->ext[i] = getstr(&r, NULL);
      im->next++;
    }
  }
  n = r.bad ? 0 : get64(&r);
  if (n > (uint64_t)(r.end - r.p) / sizeof(JitRel)) r.bad = true;
  if (!r.bad) {
    const uint8_t *q = getn(&r, n * sizeof(JitRel));
    im->rel = malloc(n ? n * sizeof(JitRel) : 1);
    if (!q || !im->rel) r.bad = true;
    else {
      memcpy(im->rel, q, n * sizeof(JitRel));
      im->nrel = (uint32_t)n;
    }
    /* every place inside the image, every name one it has */
    for (uint32_t i = 0; !r.bad && i < im->nrel; i++) {
      const JitRel *jr = &im->rel[i];
      if (jr->kind > REL_PC32 || jr->sec >= NSEC - 1 ||
          (uint64_t)jr->off + (jr->kind == REL_ABS64 ? 8 : 4) > im->seclen[jr->sec] ||
          (jr->tsec == JIT_EXT ? jr->target >= im->next
                               : jr->tsec >= NSEC || jr->target > im->seclen[jr->tsec]))
        r.bad = true;
    }
  }
  const uint8_t *ms = r.bad ? NULL : getn(&r, 1);
  if (ms) {
    im->md_sec = *ms;
    im->md_off = get64(&r);
    if (im->md_sec >= NSEC || im->md_off > im->seclen[im->md_sec]) r.bad = true;
  }
  if (r.bad || !ms || r.p != r.end) {
    jit_module_free(m, false);
    *err = fmt_err("a serialised module that is cut short or damaged");
    return NULL;
  }
  m->code = load(im, sec, NULL, err);
  if (!m->code) {
    jit_module_free(m, false);
    return NULL;
  }
  return m;
}
