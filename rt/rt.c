#include "warm_rt.h"

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_PRIVATE 0x02

#define WASM_GUARD_OFFSET_MAX (32ull << 20)
#define GUARD_RESERVE \
  ((4ull << 30) + WASM_GUARD_OFFSET_MAX + (2ull << 20) + (1ull << 16))

#if defined(WARMC_WINDOWS)

#include "win.inc"

#elif defined(WARMC_DARWIN)

#define MAP_ANONYMOUS 0x1000
extern long write(int, const void *, usz);
extern void *mmap(void *, usz, int, int, int, long long);
extern int mprotect(void *, usz, int);
extern void exit(int) __attribute__((noreturn));
extern int *__error(void);

static long sys_write(int fd, const void *buf, usz n) {
  long r = write(fd, buf, n);
  return r < 0 ? -(long)*__error() : r;
}
__attribute__((noreturn)) static void sys_exit(int code) { exit(code); }
static void *sys_mmap(void *addr, usz len, int prot, int flags) {
  void *p = mmap(addr, len, prot, flags, -1, 0);
  return ((long)p == -1) ? (void *)0 : p;
}
static int sys_mprotect(void *addr, usz len, int prot) {
  return mprotect(addr, len, prot);
}
extern int munmap(void *, usz);
static void sys_munmap(void *addr, usz len) { munmap(addr, len); }

extern int madvise(void *, usz, int);
#define WARMC_MADV_FREE 7

#define WARMC_MADV_ZEROES 0
static int sys_madvise(void *addr, usz len, int advice) {
  return madvise(addr, len, advice);
}

extern unsigned long long clock_gettime_nsec_np(int);
static u64 sys_nanos(void) { return clock_gettime_nsec_np(8); }

static u64 sys_realtime_nanos(void) { return clock_gettime_nsec_np(0); }

extern long read(int, void *, usz);
static long sys_read(int fd, void *buf, usz n) {
  long r = read(fd, buf, n);
  return r < 0 ? -(long)*__error() : r;
}
extern void arc4random_buf(void *, usz);
static int sys_random(void *buf, usz n) {
  arc4random_buf(buf, n);
  return 0;
}

extern int pthread_set_qos_class_self_np(int, int);
static void sys_prefer_fast_core(void) {
  pthread_set_qos_class_self_np(0x21 , 0);
}

#else

#define MAP_ANONYMOUS 0x20
#define SYS_write  64
#define SYS_exit   94
#define SYS_mmap   222
#define SYS_mprotect 226
#define SYS_munmap 215
#define SYS_madvise 233

static long sys(long n, long a, long b, long c, long d, long e, long f) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  register long x4 __asm__("x4") = e;
  register long x5 __asm__("x5") = f;
  __asm__ volatile("svc #0"
                   : "+r"(x0)
                   : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                   : "memory", "cc");
  return x0;
}

static long sys_write(int fd, const void *buf, usz n) {
  return sys(SYS_write, fd, (long)buf, (long)n, 0, 0, 0);
}
__attribute__((noreturn)) static void sys_exit(int code) {
  sys(SYS_exit, code, 0, 0, 0, 0, 0);
  __builtin_unreachable();
}
static void *sys_mmap(void *addr, usz len, int prot, int flags) {
  long r = sys(SYS_mmap, (long)addr, (long)len, prot, flags, -1, 0);
  if (r < 0 && r > -4096) return (void *)0;
  return (void *)r;
}
static int sys_mprotect(void *addr, usz len, int prot) {
  return (int)sys(SYS_mprotect, (long)addr, (long)len, prot, 0, 0, 0);
}
static void sys_munmap(void *addr, usz len) {
  sys(SYS_munmap, (long)addr, (long)len, 0, 0, 0, 0);
}

#define WARMC_MADV_FREE 4

#define WARMC_MADV_ZEROES 1
static int sys_madvise(void *addr, usz len, int advice) {
  return (int)sys(SYS_madvise, (long)addr, (long)len, advice, 0, 0, 0);
}

#define SYS_clock_gettime 113
#define SYS_read 63
#define SYS_getrandom 278
static u64 sys_nanos(void) {
  long ts[2] = { 0, 0 };
  sys(SYS_clock_gettime, 1 , (long)ts, 0, 0, 0, 0);
  return (u64)ts[0] * 1000000000ull + (u64)ts[1];
}

static u64 sys_realtime_nanos(void) {
  long ts[2] = { 0, 0 };
  sys(SYS_clock_gettime, 0 , (long)ts, 0, 0, 0, 0);
  return (u64)ts[0] * 1000000000ull + (u64)ts[1];
}
static long sys_read(int fd, void *buf, usz n) {
  return sys(SYS_read, fd, (long)buf, (long)n, 0, 0, 0);
}
static int sys_random(void *buf, usz n) {

  usz got = 0;
  while (got < n) {
    long r = sys(SYS_getrandom, (long)((char *)buf + got), (long)(n - got), 0, 0, 0, 0);
    if (r <= 0) return -1;
    got += (usz)r;
  }
  return 0;
}
static void sys_prefer_fast_core(void) { }

#endif

static inline void warmc_cpu_relax(void) {
#if defined(__aarch64__)
  __asm__ volatile("yield" ::: "memory");
#else
  __asm__ volatile("pause" ::: "memory");
#endif
}

static void out(const char *s, usz n) { sys_write(1, s, n); }

long warmc_rt_write(int fd, const void *buf, usz n) {
  return sys_write(fd, (const char *)buf, n);
}
long warmc_rt_read(int fd, void *buf, usz n) {
  return sys_read(fd, buf, n);
}
u64 warmc_rt_nanos(int realtime) {
  return realtime ? sys_realtime_nanos() : sys_nanos();
}
int warmc_rt_random(void *buf, usz n) { return sys_random(buf, n); }
static usz slen(const char *s) { const char *p = s; while (*p) p++; return (usz)(p - s); }
static void puts_(const char *s) { out(s, slen(s)); }
static void eputs(const char *s) { sys_write(2, s, slen(s)); }

/* ---- memory committed where it is first touched ----

   The collector reserves its heap and side tables at full size, some 2 GB
   for the default heap, and touches a fraction of them.  Windows charges
   whatever is committed against the system's commit limit at once, and so
   does Linux for writable private memory, though only under strict
   overcommit (vm.overcommit_memory=2) does that limit refuse anything: a
   few collecting programs at a time exhausted it, as they did a CI
   runner's Windows ("cannot reserve the managed heap").  A lazy mapping is
   reserved inaccessible, which neither charges, and the fault handler
   makes a chunk of it read-write where it is first touched.  macOS keeps
   no commit charge, so there it is an ordinary mapping. */
#if defined(WARMC_DARWIN)
static void *sys_mmap_lazy(usz len) {
  return sys_mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS);
}
#else
#define LAZY_MAX   8
#define LAZY_CHUNK (1ull << 20)
static struct { u8 *base; usz len; } lazy_maps[LAZY_MAX];
static u32 nlazy;
static void install_fault_handler(void);

static void *sys_mmap_lazy(usz len) {
  if (nlazy == LAZY_MAX)
    return sys_mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS);
  u8 *p = (u8 *)sys_mmap(0, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
  if (!p) return 0;
  install_fault_handler();
  lazy_maps[nlazy].base = p;
  lazy_maps[nlazy].len = len;
  __atomic_store_n(&nlazy, nlazy + 1, __ATOMIC_RELEASE);
  return p;
}

/* From the fault handler: 1 when addr lies in a lazy mapping, which now
   has memory under it.  Out of commit, the program stops, as one the
   kernel refused a page would. */
static int lazy_commit(u8 *addr) {
  u32 n = __atomic_load_n(&nlazy, __ATOMIC_ACQUIRE);
  for (u32 i = 0; i < n; i++) {
    u8 *b = lazy_maps[i].base;
    usz len = lazy_maps[i].len;
    if (addr < b || addr >= b + len) continue;
    usz off = (usz)(addr - b) & ~(usz)(LAZY_CHUNK - 1);
    usz bytes = len - off < LAZY_CHUNK ? len - off : LAZY_CHUNK;
    if (sys_mprotect(b + off, bytes, PROT_READ | PROT_WRITE) != 0) {
      eputs("warmc-rt: out of memory: the system will commit no more\n");
      sys_exit(1);
    }
    return 1;
  }
  return 0;
}
#endif

static void put_u64(u64 v) {
  char buf[24];
  int i = 24;
  if (!v) { puts_("0"); return; }
  while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
  out(buf + i, (usz)(24 - i));
}
static void put_u64_e(u64 v) {
  char buf[24];
  int i = 24;
  if (!v) { eputs("0"); return; }
  while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
  sys_write(2, buf + i, (usz)(24 - i));
}
static void put_i64(i64 v) {
  if (v < 0) { puts_("-"); put_u64((u64)(-(v + 1)) + 1); }
  else put_u64((u64)v);
}
static void put_hex(u64 v, int digits) {
  static const char hexdig[] = "0123456789abcdef";
  char buf[19];
  buf[0] = '0'; buf[1] = 'x';
  for (int i = 0; i < digits; i++) buf[2 + i] = hexdig[(v >> (4 * (digits - 1 - i))) & 0xf];
  out(buf, (usz)(2 + digits));
}

static void put_hex_raw(u64 v, int digits) {
  static const char hexdig[] = "0123456789abcdef";
  char buf[16];
  for (int i = 0; i < digits; i++) buf[i] = hexdig[(v >> (4 * (digits - 1 - i))) & 0xf];
  out(buf, (usz)digits);
}

#define MAX_GLOBALS 4096
#define MAX_DATA_SEGS 4096
#define MAX_TABLE_ENTRIES (1u << 20)
#define MAX_INSTANCES 4096
#define ARENA_WORDS (1u << 21)

typedef struct {
  u8 *base; u64 size; u64 pages; u64 max_pages; u32 has_max; u32 index64;
} WarmMemory;

typedef struct {
  TableEntry *entries; u64 len; u64 cap; u64 max_len; u32 has_max; u32 index64;
  u32 dirty;
} WarmTable;

static int limits_match(u64 have_now, u64 have_max, u32 have_has_max,
                        u32 have_index64,
                        u64 want_min, u64 want_max, u32 want_has_max,
                        u32 want_index64) {

  if ((have_index64 != 0) != (want_index64 != 0)) return 0;
  if (have_now < want_min) return 0;
  if (!want_has_max) return 1;
  if (!have_has_max) return 0;
  return have_max <= want_max;
}

typedef struct Instance {
  const ModuleDesc *md;

  void **tags;

  WarmMemory **mems;
  WarmTable **tabs;
  u8 *data_dropped;
  u8 *elem_dropped;

  void **elem_values;
  u64 nelem_values;

  u32 gctype_base;

  u32 exntype_base;

  _Alignas(16) WarmInstance vm;
} Instance;

_Alignas(16) static u64 arena[ARENA_WORDS];
static u64 arena_used;

static void die(const char *msg);

static void *arena_alloc(u64 bytes) {
  u64 words = (bytes + 15) / 16 * 2;
  if (arena_used + words > ARENA_WORDS) die("out of instance memory");
  u64 *p = &arena[arena_used];
  arena_used += words;
  for (u64 i = 0; i < words; i++) p[i] = 0;
  return p;
}

/* Zeroed memory that lives as long as the process, for rt/lib.c. */
void *warmc_rt_zalloc(u64 bytes) { return arena_alloc(bytes); }

static Instance *instances[MAX_INSTANCES];
static u32 ninstances;

static struct { const char *name; u32 name_len; WarmInstance *inst; }
  registry[MAX_INSTANCES];
static u32 nregistry;

#define INST_OF(v) ((Instance *)((u8 *)(v) - (u64)&((Instance *)0)->vm))

_Static_assert((u64)&((Instance *)0)->vm % 16 == 0,
               "the instance header must be 16-byte aligned within its block");
_Static_assert(INSTANCE_GLOBALS_OFF % INSTANCE_GLOBAL_SIZE == 0,
               "global cells must land on their own alignment");

static u64 *inst_global(Instance *in, u32 i) {
  return (u64 *)((u8 *)&in->vm + INSTANCE_GLOBALS_OFF + INSTANCE_GLOBAL_SIZE * i);
}
static u64 *inst_imports(Instance *in) {
  return (u64 *)((u8 *)&in->vm + INSTANCE_IMPORT_OFF(in->md->nglobals));
}
static FuncRef *inst_funcrefs(Instance *in) {
  return (FuncRef *)((u8 *)&in->vm +
                     INSTANCE_FUNCREF_OFF(in->md->nglobals, in->md->nimported_funcs,
                                          in->md->ntables, in->md->nmemories));
}

typedef struct { TableEntry *entries; u64 len; } TableSlot;
static TableSlot *inst_tables(Instance *in) {
  return (TableSlot *)((u8 *)&in->vm +
                       INSTANCE_TABLE_OFF(in->md->nglobals, in->md->nimported_funcs));
}

typedef struct { u8 *base; u64 size; } MemSlot;
static MemSlot *inst_mems(Instance *in) {
  return (MemSlot *)((u8 *)&in->vm +
                     INSTANCE_MEM_OFF(in->md->nglobals, in->md->nimported_funcs,
                                      in->md->ntables));
}

static WarmMemory *memory_of(Instance *in, u32 i) {
  if (i >= in->md->nmemories || !in->mems) warmc_trap(TRAP_MEM_OOB);
  return in->mems[i];
}

static WarmTable *table_of(Instance *in, u32 i) {
  if (i >= in->md->ntables || !in->tabs) warmc_trap(TRAP_TABLE_OOB);
  return in->tabs[i];
}

static u64 *global_cell(Instance *in, u32 index) {
  u64 *slot = inst_global(in, index);
  return (in->md->globals[index].flags & GLOBAL_IMPORTED) ? (u64 *)(uptr)*slot : slot;
}

static void arr_store(u8 *at, u32 width, u64 lo, u64 hi);
static u8 *arr_data(const GcTypeInfo *ti, void *a);
static void *rt_struct_new(WarmInstance *vm, u32 type_index);
static void *rt_array_new(WarmInstance *vm, u32 type_index, u64 len);
static void *rt_array_new_fill(WarmInstance *vm, u32 type_index, u64 len,
                               u64 lo, u64 hi);

const RtAlloc warmc_rt_alloc = { rt_array_new, rt_array_new_fill,
                                 rt_struct_new };

typedef struct GcRoots {
  struct GcRoots *next;
  u64 *base;
  const u32 *n;
} GcRoots;

static const RtAlloc *const_alloc(const Instance *in) {
  if (!in->md->alloc)
    die("a constant expression allocates in a module with no heap types");
  return in->md->alloc;
}

#define CONST_STACK 32
static void const_eval(Instance *in, u32 at, u64 *out) {

  u32 depth = in->md->const_depth ? in->md->const_depth : CONST_STACK;
  if (depth > 1024) die("constant expression too deep");
  u64 lo[depth], hi[depth];

  u64 roots[depth];
  u32 sp = 0;
  GcRoots hold;
  warmc_gc_hold(roots, &sp, &hold);
  const ConstOp *p = &in->md->consts[at];

#define CPUSH(l, h) do {                                                       \
    if (sp >= depth) die("constant expression too deep");                       \
    lo[sp] = (l); hi[sp] = (h); roots[sp] = 0; sp++;                            \
  } while (0)

#define CPUSH_OBJ(v) do {                                                      \
    CPUSH((u64)(uptr)(v), 0);                                          \
    roots[sp - 1] = lo[sp - 1];                                                 \
  } while (0)

#define CREFRESH() do {                                                        \
    for (u32 k = 0; k < sp; k++) if (roots[k]) lo[k] = roots[k];               \
  } while (0)

  for (; p->op != IOP_END; p += const_op_units(p->op)) {
    switch (p->op) {

      case IOP_I32_CONST: case IOP_F32_CONST: CPUSH(p->a, 0); break;
      case IOP_I64_CONST: case IOP_F64_CONST:
        CPUSH(const_op_wide(p, 0), 0);
        break;
      case IOP_V128_CONST:
        CPUSH(const_op_wide(p, 0), const_op_wide(p, 1));
        break;
      case IOP_REF_NULL: CPUSH(0, 0); break;
      case IOP_REF_FUNC:
        CPUSH((u64)(uptr)&inst_funcrefs(in)[p->a], 0);
        break;
      case IOP_GLOBAL_GET: {
        const u64 *src = global_cell(in, p->a);
        CPUSH(src[0], src[1]);
        if (p->a < in->md->nglobals &&
            in->md->globals[p->a].type == VT_REF)
          roots[sp - 1] = lo[sp - 1];
        break;
      }

      case IOP_REF_CONVERT:
        if (sp < 1) die("malformed constant expression");
        break;

      case IOP_REF_I31: {
        if (sp < 1) die("malformed constant expression");
        lo[sp - 1] = ((lo[sp - 1] & 0x7fffffffull) << 1) | GC_I31_TAG;
        hi[sp - 1] = 0;
        break;
      }
      case IOP_ARRAY_NEW: case IOP_ARRAY_NEW_DEFAULT:
      case IOP_ARRAY_NEW_FIXED: {
        const GcTypeInfo *ti = &in->md->gctypes[p->a];
        const RtAlloc *A = const_alloc(in);
        u8 *a;
        if (p->op == IOP_ARRAY_NEW_FIXED) {
          u32 n = (u32)const_op_wide(p, 0);
          if (sp < n) die("malformed constant expression");
          a = (u8 *)A->array_new(&in->vm, p->a, n);
          CREFRESH();
          sp -= n;
          for (u32 j = 0; j < n; j++)
            arr_store(arr_data(ti, a) + (u64)j * ti->body_size, ti->body_size,
                      lo[sp + j], hi[sp + j]);
        } else {
          if (sp < 1) die("malformed constant expression");
          u64 len = (u32)lo[--sp];
          if (p->op == IOP_ARRAY_NEW) {
            if (sp < 1) die("malformed constant expression");
            sp--;
            a = (u8 *)A->array_new_fill(&in->vm, p->a, len, lo[sp], hi[sp]);
            CREFRESH();
          } else {
            a = (u8 *)A->array_new(&in->vm, p->a, len);
            CREFRESH();
          }
        }
        CPUSH_OBJ(a);
        break;
      }
      case IOP_STRUCT_NEW: case IOP_STRUCT_NEW_DEFAULT: {

        u8 *o = (u8 *)const_alloc(in)->struct_new(&in->vm, p->a);
        CREFRESH();
        if (p->op == IOP_STRUCT_NEW) {
          const GcTypeInfo *ti = &in->md->gctypes[p->a];
          if (sp < ti->nfields) die("malformed constant expression");
          sp -= ti->nfields;
          u8 *ow = (u8 *)(uptr)GC_CANON((u64)(uptr)o);
          for (u32 j = 0; j < ti->nfields; j++) {

            u8 *at = ow + gcti_fields(ti)[j].offset;
            u64 x = lo[sp + j];
            switch (gcti_fields(ti)[j].width) {
              case 1: *(u8 *)at = (u8)x; break;
              case 2: *(u16 *)at = (u16)x; break;
              case 4: *(u32 *)at = (u32)x; break;
              case 16: ((u64 *)at)[0] = x; ((u64 *)at)[1] = hi[sp + j]; break;
              default: *(u64 *)at = x; break;
            }
          }
        }
        CPUSH_OBJ(o);
        break;
      }
      default: {
        if (sp < 2) die("malformed constant expression");
        u64 a = lo[sp - 2], b = lo[sp - 1], r = 0;
        switch (p->op) {
          case IOP_I32_ADD: r = (u32)((u32)a + (u32)b); break;
          case IOP_I32_SUB: r = (u32)((u32)a - (u32)b); break;
          case IOP_I32_MUL: r = (u32)((u32)a * (u32)b); break;
          case IOP_I64_ADD: r = a + b; break;
          case IOP_I64_SUB: r = a - b; break;
          case IOP_I64_MUL: r = a * b; break;
          default: die("unknown constant expression opcode");
        }
        sp--;
        lo[sp - 1] = r; hi[sp - 1] = 0;
      }
    }
  }
#undef CPUSH
  if (sp != 1) die("malformed constant expression");
  warmc_gc_release(&hold);
  out[0] = lo[0];
  out[1] = hi[0];
}

static WarmInstance *vm;

static const char *trap_name(u32 code) {
  switch (code) {
    case TRAP_UNREACHABLE: return "unreachable executed";
    case TRAP_MEM_OOB: return "out of bounds memory access";
    case TRAP_DIV_ZERO: return "integer divide by zero";
    case TRAP_DIV_OVERFLOW: return "integer overflow";
    case TRAP_INDIRECT_OOB: return "undefined element (index out of bounds)";
    case TRAP_INDIRECT_NULL: return "uninitialized element";
    case TRAP_INDIRECT_TYPE: return "indirect call type mismatch";
    case TRAP_BAD_CONVERSION: return "invalid conversion to integer";
    case TRAP_UNINIT_ELEM: return "uninitialized element";
    case TRAP_TABLE_OOB: return "out of bounds table access";
    case TRAP_STACK_OVERFLOW: return "call stack exhausted";
    case TRAP_NULL_REF: return "null reference";
    case TRAP_UNCAUGHT_EXN: return "uncaught exception";
    case TRAP_HEAP_OOM: return "allocation failed";
    case TRAP_ARRAY_OOB: return "out of bounds array access";
    case TRAP_CAST: return "cast failure";

    case TRAP_LINK: return "link failure, or an internal error above";
  }
  return "unknown trap";
}

int warmc_setjmp(u64 *buf);
__attribute__((noreturn)) void warmc_longjmp(u64 *buf, int val);

#if defined(__x86_64__)
/* the Win64 callee-saved set plus full-width xmm6-15 needs 240 bytes */
static u64 g_catch_buf[32];
#else
static u64 g_catch_buf[24];
#endif
static int g_catching;
static u32 g_trap_code;

static void gc_after_trap(void);
static void gc_trap_round_arm(void);
extern u64 warmc_gc_invoke_depth;

void warmc_unreached(void) {

  die("a call reached a function the compiler proved nothing could reach");
}

__attribute__((noreturn)) void warmc_trap(u32 code) {
  if (g_catching) {
    g_catching = 0;
    g_trap_code = code;
    gc_trap_round_arm();
    warmc_longjmp(g_catch_buf, 1);
  }
  eputs("trap: ");
  eputs(trap_name(code));
  eputs("\n");
  sys_exit(1);
}

const char *warmc_trap_name(u32 code) { return trap_name(code); }

static u32 rd_le32(const u8 *p) {
  return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

usz warmc_custom_section(const ModuleDesc *md, const char *name, usz index,
                         const u8 **data, usz *size) {
  const u8 *t = md->custom;
  usz found = 0;
  u32 n = t ? rd_le32(t + 12) : 0;
  for (u32 i = 0; i < n; i++) {
    const u8 *e = t + 16 + 16 * (usz)i;
    const u8 *nm = t + rd_le32(e);
    u32 nl = rd_le32(e + 4), k = 0;
    while (k < nl && name[k] && (u8)name[k] == nm[k]) k++;
    if (k != nl || name[k]) continue;
    if (found++ == index) {
      if (data) *data = t + rd_le32(e + 8);
      if (size) *size = rd_le32(e + 12);
    }
  }
  if (found <= index) {
    if (data) *data = 0;
    if (size) *size = 0;
  }
  return found;
}

static void catch_save(u64 *buf, int *was) {
  *was = g_catching;
  for (u32 i = 0; i < sizeof g_catch_buf / sizeof g_catch_buf[0]; i++)
    buf[i] = g_catch_buf[i];
}

static void catch_restore(const u64 *buf, int was) {
  for (u32 i = 0; i < sizeof g_catch_buf / sizeof g_catch_buf[0]; i++)
    g_catch_buf[i] = buf[i];
  g_catching = was;
}

WarmInstance *warmc_instantiate_catching(const ModuleDesc *md, u32 *err) {
  return warmc_instantiate_bound_catching(md, 0, err);
}

WarmInstance *warmc_instantiate_bound_catching(const ModuleDesc *md,
                                               const WarmBinding *binds,
                                               u32 *err) {
  u64 depth = warmc_gc_invoke_depth;
  u64 outer[sizeof g_catch_buf / sizeof g_catch_buf[0]];
  int was;
  catch_save(outer, &was);
  if (warmc_setjmp(g_catch_buf)) {
    catch_restore(outer, was);
    warmc_gc_invoke_depth = depth;
    gc_after_trap();
    *err = g_trap_code;
    return 0;
  }
  g_catching = 1;
  WarmInstance *v = warmc_instantiate_bound(md, binds);
  catch_restore(outer, was);
  *err = 0;
  return v;
}

u32 warmc_invoke_catching(void *fn, WarmInstance *v, const u64 *iargs,
                          const u64 *fargs, u64 *retbuf) {
  u64 depth = warmc_gc_invoke_depth;
  u64 outer[sizeof g_catch_buf / sizeof g_catch_buf[0]];
  int was;
  catch_save(outer, &was);
  if (warmc_setjmp(g_catch_buf)) {
    catch_restore(outer, was);
    warmc_gc_invoke_depth = depth;
    gc_after_trap();
    return g_trap_code;
  }
  g_catching = 1;
  warmc_invoke(fn, v, iargs, fargs, retbuf);
  catch_restore(outer, was);
  return 0;
}

#if defined(__x86_64__)
u32 warmc_invoke_catching_s(void *fn, WarmInstance *v, const u64 *iargs,
                            const u64 *fargs, u64 *retbuf,
                            const u64 *stk, u64 stk_bytes) {
  u64 depth = warmc_gc_invoke_depth;
  u64 outer[sizeof g_catch_buf / sizeof g_catch_buf[0]];
  int was;
  catch_save(outer, &was);
  if (warmc_setjmp(g_catch_buf)) {
    catch_restore(outer, was);
    warmc_gc_invoke_depth = depth;
    gc_after_trap();
    return g_trap_code;
  }
  g_catching = 1;
  warmc_invoke_s(fn, v, iargs, fargs, retbuf, stk, stk_bytes);
  catch_restore(outer, was);
  return 0;
}
#endif

static void republish_memory(WarmMemory *M) {
  for (u32 i = 0; i < ninstances; i++) {
    Instance *in = instances[i];
    if (!in->mems) continue;
    MemSlot *slots = inst_mems(in);
    for (u32 k = 0; k < in->md->nmemories; k++) {
      if (in->mems[k] != M) continue;
      slots[k].base = M->base;
      slots[k].size = M->size;
      if (k == 0) {
        in->vm.mem_base = M->base;
        in->vm.mem_size = M->size;
        in->vm.mem_pages = M->pages;
        in->vm.mem_max_pages = M->max_pages;
      }
    }
  }
}

static void republish_table(WarmTable *T) {
  for (u32 i = 0; i < ninstances; i++) {
    Instance *in = instances[i];
    if (!in->tabs) continue;
    TableSlot *slots = inst_tables(in);
    for (u32 k = 0; k < in->md->ntables; k++) {
      if (in->tabs[k] != T) continue;
      slots[k].entries = T->entries;
      slots[k].len = T->len;
      if (k == 0) { in->vm.table_base = T->entries; in->vm.table_len = T->len; }
    }
  }
}

u64 warmc_memory_grow(WarmInstance *v, u64 delta, u32 mem) {
  WarmMemory *M = memory_of(INST_OF(v), mem);
  if (!M) return (u64)-1;
  u64 old = M->pages;
  if (delta == 0) return old;
  u64 want = old + delta;
  if (want < old || want > M->max_pages) return (u64)-1;
  usz add = (usz)delta * WASM_PAGE_SIZE;
  if (sys_mprotect(M->base + old * WASM_PAGE_SIZE, add, PROT_READ | PROT_WRITE) != 0)
    return (u64)-1;
  M->pages = want;
  M->size = want * WASM_PAGE_SIZE;
  republish_memory(M);
  return old;
}

static void bounds(u64 a, u64 n, u64 limit, u32 code) {
  u64 end = a + n;
  if (end < a || end > limit) warmc_trap(code);
}

typedef unsigned char v16 __attribute__((vector_size(16), aligned(1)));

#if defined(WARMC_DARWIN)

extern void *memmove(void *, const void *, usz);
#define PLATFORM_MEMMOVE_FROM 65536u

#define PLATFORM_MEMMOVE_OVERLAP 512u

extern void *memset(void *, int, usz);
#define PLATFORM_MEMSET_FROM 1024u
#endif

static int overlaps(const u8 *d, const u8 *s, u64 n) {
  return d < s ? (u64)(s - d) < n : (u64)(d - s) < n;
}

static void mem_move(u8 *d, const u8 *s, u64 n) {
  if (d == s) return;
#if defined(WARMC_DARWIN)
  if (n >= PLATFORM_MEMMOVE_FROM) { memmove(d, s, n); return; }
  if (n >= PLATFORM_MEMMOVE_OVERLAP && overlaps(d, s, n)) {
    memmove(d, s, n);
    return;
  }
#endif

  if (n <= 16) {
    if (n >= 8) {
      u64 a, b;
      __builtin_memcpy(&a, s, 8); __builtin_memcpy(&b, s + n - 8, 8);
      __builtin_memcpy(d, &a, 8); __builtin_memcpy(d + n - 8, &b, 8);
    } else if (n >= 4) {
      u32 a, b;
      __builtin_memcpy(&a, s, 4); __builtin_memcpy(&b, s + n - 4, 4);
      __builtin_memcpy(d, &a, 4); __builtin_memcpy(d + n - 4, &b, 4);
    } else if (n) {
      u8 a = s[0], b = s[n >> 1], c = s[n - 1];
      d[0] = a; d[n >> 1] = b; d[n - 1] = c;
    }
    return;
  }
  if (n <= 32) {
    v16 a, b;
    __builtin_memcpy(&a, s, 16); __builtin_memcpy(&b, s + n - 16, 16);
    __builtin_memcpy(d, &a, 16); __builtin_memcpy(d + n - 16, &b, 16);
    return;
  }
  if (n <= 64) {
    v16 a, b, c, e;
    __builtin_memcpy(&a, s, 16);          __builtin_memcpy(&b, s + 16, 16);
    __builtin_memcpy(&c, s + n - 32, 16); __builtin_memcpy(&e, s + n - 16, 16);
    __builtin_memcpy(d, &a, 16);          __builtin_memcpy(d + 16, &b, 16);
    __builtin_memcpy(d + n - 32, &c, 16); __builtin_memcpy(d + n - 16, &e, 16);
    return;
  }

  if (d < s || !overlaps(d, s, n)) {
    while (n >= 64) {
      v16 a, b, c, e;
      __builtin_memcpy(&a, s, 16);      __builtin_memcpy(&b, s + 16, 16);
      __builtin_memcpy(&c, s + 32, 16); __builtin_memcpy(&e, s + 48, 16);
      __builtin_memcpy(d, &a, 16);      __builtin_memcpy(d + 16, &b, 16);
      __builtin_memcpy(d + 32, &c, 16); __builtin_memcpy(d + 48, &e, 16);
      d += 64; s += 64; n -= 64;
    }
    while (n >= 32) {
      u64 a, b, c, e;
      __builtin_memcpy(&a, s, 8);       __builtin_memcpy(&b, s + 8, 8);
      __builtin_memcpy(&c, s + 16, 8);  __builtin_memcpy(&e, s + 24, 8);
      __builtin_memcpy(d, &a, 8);       __builtin_memcpy(d + 8, &b, 8);
      __builtin_memcpy(d + 16, &c, 8);  __builtin_memcpy(d + 24, &e, 8);
      d += 32; s += 32; n -= 32;
    }
    while (n >= 8) {
      u64 a;
      __builtin_memcpy(&a, s, 8);
      __builtin_memcpy(d, &a, 8);
      d += 8; s += 8; n -= 8;
    }
    while (n) { *d++ = *s++; n--; }
  } else {

    d += n; s += n;
    while (n >= 64) {
      v16 a, b, c, e;
      d -= 64; s -= 64; n -= 64;
      __builtin_memcpy(&a, s, 16);      __builtin_memcpy(&b, s + 16, 16);
      __builtin_memcpy(&c, s + 32, 16); __builtin_memcpy(&e, s + 48, 16);
      __builtin_memcpy(d, &a, 16);      __builtin_memcpy(d + 16, &b, 16);
      __builtin_memcpy(d + 32, &c, 16); __builtin_memcpy(d + 48, &e, 16);
    }
    while (n >= 32) {
      u64 a, b, c, e;
      d -= 32; s -= 32; n -= 32;
      __builtin_memcpy(&a, s, 8);       __builtin_memcpy(&b, s + 8, 8);
      __builtin_memcpy(&c, s + 16, 8);  __builtin_memcpy(&e, s + 24, 8);
      __builtin_memcpy(d, &a, 8);       __builtin_memcpy(d + 8, &b, 8);
      __builtin_memcpy(d + 16, &c, 8);  __builtin_memcpy(d + 24, &e, 8);
    }
    while (n >= 8) {
      u64 a;
      d -= 8; s -= 8; n -= 8;
      __builtin_memcpy(&a, s, 8);
      __builtin_memcpy(d, &a, 8);
    }
    while (n) { *--d = *--s; n--; }
  }
}

static void mem_set(u8 *d, u8 val, u64 n) {
#if defined(WARMC_DARWIN)
  /* Darwin's memset zeroes a cache line per instruction (dc zva) once the
     range is long enough, and a zero fill is what an allocator makes of
     every fresh object: AssemblyScript's __new clears what it hands out. */
  if (n >= PLATFORM_MEMSET_FROM) { memset(d, val, n); return; }
#endif
  u64 w = (u64)val * 0x0101010101010101ull;
  if (n < 16) {
    if (n >= 8) { __builtin_memcpy(d, &w, 8); __builtin_memcpy(d + n - 8, &w, 8); return; }
    if (n >= 4) { __builtin_memcpy(d, &w, 4); __builtin_memcpy(d + n - 4, &w, 4); return; }
    if (n) { d[0] = val; d[n >> 1] = val; d[n - 1] = val; }
    return;
  }
  v16 wv;
  __builtin_memcpy(&wv, &w, 8);
  __builtin_memcpy((u8 *)&wv + 8, &w, 8);
  if (n <= 32) {
    __builtin_memcpy(d, &wv, 16); __builtin_memcpy(d + n - 16, &wv, 16);
    return;
  }
  if (n <= 64) {
    __builtin_memcpy(d, &wv, 16);          __builtin_memcpy(d + 16, &wv, 16);
    __builtin_memcpy(d + n - 32, &wv, 16); __builtin_memcpy(d + n - 16, &wv, 16);
    return;
  }
  while (n >= 64) {
    __builtin_memcpy(d, &wv, 16);      __builtin_memcpy(d + 16, &wv, 16);
    __builtin_memcpy(d + 32, &wv, 16); __builtin_memcpy(d + 48, &wv, 16);
    d += 64; n -= 64;
  }

  if (n) {
    __builtin_memcpy(d + n - 32, &wv, 16); __builtin_memcpy(d + n - 16, &wv, 16);
    if (n > 32) { __builtin_memcpy(d, &wv, 16); __builtin_memcpy(d + 16, &wv, 16); }
  }
}

void warmc_memory_copy(WarmInstance *v, u64 dst, u64 src, u64 n,
                       u32 dmem, u32 smem) {
  if (dmem == 0 && smem == 0) {
    bounds(dst, n, v->mem_size, TRAP_MEM_OOB);
    bounds(src, n, v->mem_size, TRAP_MEM_OOB);
    mem_move(v->mem_base + dst, v->mem_base + src, n);
    return;
  }
  Instance *in = INST_OF(v);
  WarmMemory *D = memory_of(in, dmem), *S = memory_of(in, smem);
  bounds(dst, n, D ? D->size : 0, TRAP_MEM_OOB);
  bounds(src, n, S ? S->size : 0, TRAP_MEM_OOB);
  mem_move(D->base + dst, S->base + src, n);
}

void warmc_memory_fill(WarmInstance *v, u64 dst, u64 val, u64 n, u32 mem) {
  if (mem == 0) {
    bounds(dst, n, v->mem_size, TRAP_MEM_OOB);
    mem_set(v->mem_base + dst, (u8)val, n);
    return;
  }
  WarmMemory *M = memory_of(INST_OF(v), mem);
  bounds(dst, n, M ? M->size : 0, TRAP_MEM_OOB);
  mem_set(M->base + dst, (u8)val, n);
}

void warmc_memory_init(WarmInstance *v, u64 dst, u64 src, u64 n, u32 seg,
                       u32 mem) {
  Instance *in = INST_OF(v);
  WarmMemory *M = memory_of(in, mem);
  if (seg >= in->md->ndata) warmc_trap(TRAP_MEM_OOB);
  const DataSegDesc *d = &in->md->data[seg];
  u64 avail = in->data_dropped[seg] ? 0 : d->len;
  bounds(src, n, avail, TRAP_MEM_OOB);
  bounds(dst, n, M ? M->size : 0, TRAP_MEM_OOB);

  mem_move(M->base + dst, in->md->data_blob + d->blob_off + src, n);
}

void warmc_data_drop(WarmInstance *v, u32 seg) {
  Instance *in = INST_OF(v);
  if (seg < in->md->ndata) in->data_dropped[seg] = 1;
}

static u64 gc_load(u64 *slot);

static void gc_mark_copied(u64 *dst, u64 n);

void *warmc_table_get(WarmInstance *v, u64 i, u32 tab) {
  WarmTable *T = table_of(INST_OF(v), tab);
  if (!T || i >= T->len) warmc_trap(TRAP_TABLE_OOB);

  return (void *)(uptr)gc_load((u64 *)&T->entries[i]);
}

void warmc_table_set(WarmInstance *v, u64 i, void *ref, u32 tab) {
  WarmTable *T = table_of(INST_OF(v), tab);
  if (!T || i >= T->len) warmc_trap(TRAP_TABLE_OOB);
  T->entries[i] = ref;
}

u64 warmc_table_size(WarmInstance *v, u32 tab) {
  WarmTable *T = table_of(INST_OF(v), tab);
  return T ? T->len : 0;
}

u64 warmc_table_grow(WarmInstance *v, void *init, u64 delta, u32 tab) {
  WarmTable *T = table_of(INST_OF(v), tab);
  if (!T) return (u64)-1;
  u64 old = T->len;
  u64 want = old + delta;
  if (want < old || want > T->cap) return (u64)-1;
  for (u64 i = old; i < want; i++) T->entries[i] = init;

  __atomic_store_n(&T->len, want, __ATOMIC_RELEASE);
  republish_table(T);
  return old;
}

void warmc_table_fill(WarmInstance *v, u64 i, void *ref, u64 n, u32 tab) {
  WarmTable *T = table_of(INST_OF(v), tab);
  bounds(i, n, T ? T->len : 0, TRAP_TABLE_OOB);
  if (n)
  for (u64 k = 0; k < n; k++) T->entries[i + k] = ref;
}

void warmc_table_copy(WarmInstance *v, u64 dst, u64 src, u64 n, u32 dt, u32 st) {
  Instance *in = INST_OF(v);
  WarmTable *D = table_of(in, dt), *S = table_of(in, st);
  bounds(dst, n, D ? D->len : 0, TRAP_TABLE_OOB);
  bounds(src, n, S ? S->len : 0, TRAP_TABLE_OOB);
  if (D == S && dst > src)
    for (u64 i = n; i > 0; i--) D->entries[dst + i - 1] = S->entries[src + i - 1];
  else
    for (u64 i = 0; i < n; i++) D->entries[dst + i] = S->entries[src + i];
  if (n) gc_mark_copied((u64 *)&D->entries[dst], n);
}

static void *elem_item(Instance *in, u32 k) {
  return k < in->nelem_values ? in->elem_values[k] : 0;
}

void warmc_table_init(WarmInstance *v, u64 dst, u64 src, u64 n, u32 seg, u32 tab) {
  Instance *in = INST_OF(v);
  WarmTable *T = table_of(in, tab);
  if (seg >= in->md->nelem) warmc_trap(TRAP_TABLE_OOB);
  const ElemSegDesc *e = &in->md->elem[seg];
  u64 avail = in->elem_dropped[seg] ? 0 : e->n;
  bounds(src, n, avail, TRAP_TABLE_OOB);
  bounds(dst, n, T ? T->len : 0, TRAP_TABLE_OOB);
  if (n)
  for (u64 i = 0; i < n; i++) T->entries[dst + i] = elem_item(in, (u32)(e->first + src + i));
}

void warmc_elem_drop(WarmInstance *v, u32 seg) {
  Instance *in = INST_OF(v);
  if (seg < in->md->nelem) in->elem_dropped[seg] = 1;
}

#define MAX_MODULE_DESCS 4096
static const ModuleDesc *descs[MAX_MODULE_DESCS];

static u8 desc_sorted[MAX_MODULE_DESCS];
static u32 ndescs;

static void register_desc(const ModuleDesc *md) {
  for (u32 i = 0; i < ndescs; i++) if (descs[i] == md) return;
  if (ndescs >= MAX_MODULE_DESCS) die("too many modules");
  u8 sorted = 1;
  for (u32 k = 1; k < md->nunwind; k++)
    if ((u64)(uptr)md->unwind[k].code <
        (u64)(uptr)md->unwind[k - 1].code) { sorted = 0; break; }
  desc_sorted[ndescs] = sorted;
  descs[ndescs++] = md;
}

static u32 gc_no_collect;
static void *exn_alloc(Instance *in, u32 tag);
static void *exn_safepoint(void *e, WarmRegs *regs, u64 sp, u64 pc);

static const FuncUnwind *find_unwind_in(u64 pc, const ModuleDesc **owner) {
  for (u32 i = 0; i < ndescs; i++) {
    const ModuleDesc *md = descs[i];
    if (!desc_sorted[i]) {
      for (u32 k = 0; k < md->nunwind; k++) {
        const FuncUnwind *u = &md->unwind[k];
        u64 base = (u64)(uptr)u->code;
        if (pc >= base && pc < base + u->code_len) {
          if (owner) *owner = md;
          return u;
        }
      }
      continue;
    }
    u32 lo = 0, hi = md->nunwind;
    while (lo < hi) {
      u32 mid = lo + (hi - lo) / 2;
      const FuncUnwind *u = &md->unwind[mid];
      u64 base = (u64)(uptr)u->code;
      if (pc < base) hi = mid;
      else if (pc >= base + u->code_len) lo = mid + 1;
      else { if (owner) *owner = md; return u; }
    }
  }
  return 0;
}

static const FuncUnwind *find_unwind(u64 pc) { return find_unwind_in(pc, 0); }

static u64 load64(u64 addr) { return *(const u64 *)(uptr)addr; }

__attribute__((noreturn))
static void unwind(WarmExn *exn, u64 pc, u64 sp, WarmRegs regs) {
  const WarmExn *xw = (const WarmExn *)(uptr)GC_CANON((u64)(uptr)exn);
  for (;;) {
    const FuncUnwind *u = find_unwind(pc);

    if (!u) warmc_trap(TRAP_UNCAUGHT_EXN);
    u32 off = (u32)(pc - (u64)(uptr)u->code);

    const u8 *s = u->sites;
    u32 spc = 0;
    for (u32 i = 0; i < u->nsites; i++) {
      spc += uw_uleb(&s);
      u32 first = uw_uleb(&s), n = uw_uleb(&s);
      if (spc < off) continue;
      if (spc > off) break;
      Instance *in = INST_OF((WarmInstance *)(uptr)regs.gpr[0]);
      for (u32 k = 0; k < n; k++) {
        const UwClause *c = &u->clauses[first + k];
        if (c->kind < 2 &&
            (c->tag >= in->md->ntags || in->tags[c->tag] != xw->tag))
          continue;
        warmc_resume(sp, (u64)(uptr)u->code + c->pad, exn,
                     u->exn_slot, regs.gpr, regs.fpr);
      }
      break;
    }

    if (!(u->saved & UW_HAS_LR)) warmc_trap(TRAP_UNCAUGHT_EXN);
    u64 entry = sp + u->frame + u->saved_bytes;
    u64 next_pc = load64(entry - 8);
    u64 base = entry - 16;
    u32 k = 0;
    for (u32 r = 0; r < 10; r++) {
      if (!(u->saved & (1u << (UW_GPR_SHIFT + r)))) continue;
#if defined(__x86_64__)
      /* Sequential pushes in unwind-index order, first at entry-16. */
      regs.gpr[r] = load64(base - 8ull * k);
#else
      regs.gpr[r] = load64(base - 16ull * (k / 2 + 1) + 8ull * (k % 2));
#endif
      k++;
    }
    k = 0;
    for (u32 r = 0; r < 8; r++) {
      if (!(u->saved & (1u << (UW_FPR_SHIFT + r)))) continue;
      regs.fpr[r] = load64(sp + u->fpsave_off + 8ull * k);
      k++;
    }
    sp = entry;
    pc = next_pc;
  }
}

void warmc_throw_c(WarmInstance *v, u32 tag, const u64 *payload, u32 nvals,
                   const WarmRegs *snap, u64 sp, u64 pc) {
  Instance *in = INST_OF(v);
  if (tag >= in->md->ntags) die("throw names a tag the module does not have");
  WarmExn *e = exn_alloc(in, tag);
  WarmExn *ew = (WarmExn *)(uptr)GC_CANON((u64)(uptr)e);
  ew->tag = in->tags[tag];
  ew->nvals = nvals;

  for (u32 i = 0; i < 2u * nvals; i++) ew->vals[i] = payload[i];
  WarmRegs regs = *snap;
  e = exn_safepoint(e, &regs, sp, pc);
  unwind(e, pc, sp, regs);
}

void warmc_throw_ref_c(WarmExn *exn, const WarmRegs *snap, u64 sp, u64 pc) {
  if (!exn) warmc_trap(TRAP_NULL_REF);
  unwind(exn, pc, sp, *snap);
}

static void report_icount(WarmInstance *inst) {
  if (!inst || !inst->icount) return;
  eputs("icount: ");
  {
    char buf[24];
    int i = 24;
    u64 v = inst->icount;
    while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    sys_write(2, buf + i, (usz)(24 - i));
  }
  eputs("\n");
}

#define MAX_GUARDS 1024
static struct { u8 *base; u64 len; } guards[MAX_GUARDS];
static u32 nguards;

static int addr_in_a_guard(const u8 *addr) {
  for (u32 i = 0; i < nguards; i++)
    if (addr >= guards[i].base && addr < guards[i].base + guards[i].len) return 1;
  return 0;
}

#if !defined(WARMC_DARWIN) && !defined(WARMC_WINDOWS)
#define SYS_rt_sigaction 134
#define SA_SIGINFO   0x00000004u
#define SA_RESTORER  0x04000000u

#define SA_NODEFER   0x40000000u

struct k_sigaction {
  void *handler;
  usz flags;
  void *restorer;
  usz mask;
};

__attribute__((naked)) static void sig_restorer(void) {
  __asm__ volatile("mov x8, #139\n\tsvc #0");
}
#endif

static int gcth_on_collector(void);

/* Windows installs a vectored exception handler instead (win.inc). */
#if !defined(WARMC_WINDOWS)

#if defined(WARMC_DARWIN)
#define SI_ADDR_OFF 24
#else
#define SI_ADDR_OFF 16
#endif

#if defined(WARMC_DARWIN)
extern int sigaction(int, const void *, void *);
typedef struct { void *h; unsigned int mask; int flags; } SigAct;
#define SIGACT_SIGINFO 0x0040
#define SIGACT_FN(a) ((a)->h)
#define SIG_BUS 10
static void set_sigaction(int sig, const SigAct *a, SigAct *old) {
  sigaction(sig, a, old);
}
#else
typedef struct k_sigaction SigAct;
#define SIGACT_SIGINFO SA_SIGINFO
#define SIGACT_FN(a) ((a)->handler)
#define SIG_BUS 7
static void set_sigaction(int sig, const SigAct *a, SigAct *old) {
  sys(SYS_rt_sigaction, sig, (long)a, (long)old, 8, 0, 0);
}
#endif

/* What handled SIGSEGV and SIGBUS before the runtime did.  A program warmc
   linked has nothing there, and a fault outside linear memory ends it; a
   library (rt/lib.c) runs in someone else's process, whose faults -- a
   crash, or a JVM's implicit null check -- are passed on to that. */
static SigAct g_prev_segv, g_prev_bus;
static int g_library;

static void chain_fault(int sig, void *info, void *uc) {
  SigAct *p = sig == 11 ? &g_prev_segv : &g_prev_bus;
  uptr h = (uptr)SIGACT_FN(p);
  if (h > 1) {
    if (p->flags & SIGACT_SIGINFO) ((void (*)(int, void *, void *))h)(sig, info, uc);
    else ((void (*)(int))h)(sig);
    return;
  }
  /* SIG_DFL or SIG_IGN: put it back, and the faulting instruction meets it
     when the handler returns. */
  set_sigaction(sig, p, 0);
}

static void fault_handler(int sig, void *info, void *uc) {
  u8 *addr = info ? *(u8 **)((u8 *)info + SI_ADDR_OFF) : 0;

#if !defined(WARMC_DARWIN)
  if (lazy_commit(addr)) return;
#endif
  if (gcth_on_collector()) {
    eputs("warmc-rt: fault on the collector's thread\n");
    sys_exit(139);
  }
  if (addr_in_a_guard(addr)) warmc_trap(TRAP_MEM_OOB);
  if (g_library) { chain_fault(sig, info, uc); return; }
  eputs("warmc-rt: fault outside linear memory\n");
  sys_exit(139);
}

static void install_fault_handler(void) {
  static int installed;
  if (installed) return;
  installed = 1;
  SigAct act;
#if defined(WARMC_DARWIN)
  act.h = (void *)fault_handler;
  act.mask = 0;
  act.flags = SIGACT_SIGINFO | 0x0010;
#else
  act.handler = (void *)fault_handler;
  act.flags = SA_SIGINFO | SA_RESTORER | SA_NODEFER;
  act.restorer = (void *)sig_restorer;
  act.mask = 0;
#endif
  set_sigaction(11, &act, &g_prev_segv);
  set_sigaction(SIG_BUS, &act, &g_prev_bus);
}

#endif /* !WARMC_WINDOWS */

typedef struct { u32 v; u32 sleepers; } GcthGate;

#define GCTH_SPINS 8000u
static u32 gcth_spin = GCTH_SPINS;

static u64 gcth_nwaits;

#if defined(WARMC_DARWIN)

static void (*gcth_entry_fn)(void);

extern int pthread_create(void **, const void *, void *(*)(void *), void *);
extern int pthread_detach(void *);
extern void *pthread_self(void);

#define UL_COMPARE_AND_WAIT 1u
#define ULF_NO_ERRNO 0x01000000u
extern int __ulock_wait(unsigned int op, void *addr, unsigned long long value,
                        unsigned int timeout_us);
extern int __ulock_wake(unsigned int op, void *addr, unsigned long long wake);

static void gcth_wait(u32 *w, u32 expect, u64 ns) {

  unsigned int us = 0;
  gcth_nwaits++;
  if (ns) { us = (unsigned int)(ns / 1000); if (!us) us = 1; }
  __ulock_wait(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, w, expect, us);
}
static void gcth_wake(u32 *w) {

  __ulock_wake(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, w, 0);
}

static void *gcth_thread;

static int gcth_on_collector(void) {
  void *t = __atomic_load_n(&gcth_thread, __ATOMIC_ACQUIRE);
  return t != 0 && pthread_self() == t;
}

static void *gcth_trampoline(void *arg) {
  (void)arg;

  __atomic_store_n(&gcth_thread, pthread_self(), __ATOMIC_RELEASE);
  gcth_entry_fn();
  return 0;
}

static int gcth_spawn(void (*entry)(void)) {
  void *th = 0;
  gcth_entry_fn = entry;

  if (pthread_create(&th, 0, gcth_trampoline, 0) != 0) return 0;
  pthread_detach(th);
  return 1;
}

#elif defined(WARMC_WINDOWS)

static void (*gcth_entry_fn)(void);
static u32 gcth_tid;

static int gcth_on_collector(void) {
  u32 t = __atomic_load_n(&gcth_tid, __ATOMIC_ACQUIRE);
  return t != 0 && GetCurrentThreadId() == t;
}

static u32 gcth_trampoline(void *arg) {
  (void)arg;
  __atomic_store_n(&gcth_tid, GetCurrentThreadId(), __ATOMIC_RELEASE);
  gcth_entry_fn();
  return 0;
}

static int gcth_spawn(void (*entry)(void)) {
  gcth_entry_fn = entry;
  void *h = CreateThread(0, 0, gcth_trampoline, 0, 0, 0);
  if (!h) return 0;
  CloseHandle(h);
  return 1;
}

static void gcth_wait(u32 *w, u32 expect, u64 ns) {
  u32 ms = 0xffffffffu;                     /* INFINITE */
  gcth_nwaits++;
  if (ns) {
    u64 m = (ns + 999999) / 1000000;        /* round up: never early */
    ms = !m ? 1 : m > 0xfffffffeull ? 0xfffffffeu : (u32)m;
  }
  WaitOnAddress(w, &expect, 4, ms);
}
static void gcth_wake(u32 *w) { WakeByAddressSingle(w); }

#else

#define SYS_futex 98

#define GCTH_CLONE_FLAGS 0x50f00ul

extern long warmc_gcth_clone(usz flags, void *stacktop,
                             void (*entry)(void));

static void gcth_wait(u32 *w, u32 expect, u64 ns) {
  long ts[2], *tp = 0;
  gcth_nwaits++;
  if (ns) { ts[0] = (long)(ns / 1000000000ull);
            ts[1] = (long)(ns % 1000000000ull); tp = ts; }
  sys(SYS_futex, (long)w, 128 , (long)expect, (long)tp, 0, 0);
}
static void gcth_wake(u32 *w) {

  sys(SYS_futex, (long)w, 129 , 1, 0, 0, 0);
}

#define GCTH_STACK_BYTES (1u << 20)
static u64 gcth_stack_lo, gcth_stack_hi;

static int gcth_on_collector(void) {
  u64 sp;
  __asm__("mov %0, sp" : "=r"(sp));
  return sp >= gcth_stack_lo && sp < gcth_stack_hi;
}

static int gcth_spawn(void (*entry)(void)) {
  u8 *st = (u8 *)sys_mmap(0, GCTH_STACK_BYTES, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS);
  if (!st) return 0;
  if (sys_mprotect(st, 4096, PROT_NONE) != 0) return 0;

  gcth_stack_lo = (u64)(uptr)st;
  gcth_stack_hi = gcth_stack_lo + GCTH_STACK_BYTES;
  if (warmc_gcth_clone(GCTH_CLONE_FLAGS, st + GCTH_STACK_BYTES, entry) <= 0) {
    gcth_stack_lo = gcth_stack_hi = 0;
    return 0;
  }
  return 1;
}

#endif

static int gcth_spun_out(GcthGate *g, u32 expect) {
  for (u32 i = __atomic_load_n(&gcth_spin, __ATOMIC_RELAXED); i; i--) {
    if (__atomic_load_n(&g->v, __ATOMIC_ACQUIRE) != expect) return 1;
    warmc_cpu_relax();
  }
  return 0;
}

static void gcth_park(GcthGate *g, u32 expect) {
  if (gcth_spun_out(g, expect)) return;
  __atomic_store_n(&g->sleepers, 1, __ATOMIC_SEQ_CST);

  while (__atomic_load_n(&g->v, __ATOMIC_SEQ_CST) == expect)
    gcth_wait(&g->v, expect, 0);
  __atomic_store_n(&g->sleepers, 0, __ATOMIC_RELAXED);
}

static int gcth_park_timeout(GcthGate *g, u32 expect, u64 ns) {
  if (gcth_spun_out(g, expect)) return 0;
  u64 deadline = sys_nanos() + ns;
  int late = 0;
  __atomic_store_n(&g->sleepers, 1, __ATOMIC_SEQ_CST);
  for (;;) {
    if (__atomic_load_n(&g->v, __ATOMIC_SEQ_CST) != expect) break;
    u64 now = sys_nanos();
    if (now >= deadline) { late = 1; break; }
    gcth_wait(&g->v, expect, deadline - now);
  }
  __atomic_store_n(&g->sleepers, 0, __ATOMIC_RELAXED);
  return late;
}

static void gcth_unpark(GcthGate *g, u32 v) {
  __atomic_store_n(&g->v, v, __ATOMIC_SEQ_CST);
  if (__atomic_load_n(&g->sleepers, __ATOMIC_SEQ_CST)) gcth_wake(&g->v);
}

static void die(const char *msg) {
  if (g_catching && !gcth_on_collector()) {
    eputs("warmc-rt: internal: "); eputs(msg); eputs("\n");
    g_catching = 0; g_trap_code = TRAP_LINK; warmc_longjmp(g_catch_buf, 1);
  }
  eputs("warmc-rt: "); eputs(msg); eputs("\n"); sys_exit(1);
}

static u64 g_stack_limit;
static char **g_environ;

#if !defined(WARMC_DARWIN) && !defined(WARMC_WINDOWS)
/* A library's environment on Linux, where no entry point hands it over:
   /proc/self/environ, the one the process started with. */
static char **proc_environ(void) {
  usz cap = 1u << 20, len = 0;
  char *buf = (char *)sys_mmap(0, cap + 1, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS);
  if (!buf) return 0;
  long fd = sys(56 /* openat */, -100 /* AT_FDCWD */, (long)"/proc/self/environ",
                0, 0, 0, 0);
  if (fd >= 0) {
    long n;
    while (len < cap &&
           (n = sys(63 /* read */, fd, (long)(buf + len), (long)(cap - len), 0, 0, 0)) > 0)
      len += (usz)n;
    sys(57 /* close */, fd, 0, 0, 0, 0, 0);
  }
  /* a variable the cap cut short is left out */
  while (len && buf[len - 1]) len--;
  u32 nent = 0;
  for (usz i = 0; i < len; i++) nent += !buf[i];
  char **envp = (char **)arena_alloc(((u64)nent + 1) * sizeof(char *));
  u32 k = 0;
  for (usz i = 0; i < len; i++)
    if (i == 0 || !buf[i - 1]) envp[k++] = buf + i;
  envp[k] = 0;
  return envp;
}
#endif

#if defined(WARMC_DARWIN)
extern char ***_NSGetEnviron(void);
#endif
static void gc_read_options(void);

/* For the library face (rt/lib.c, warmc --shared), which runs in a process
   the runtime does not own: it says so, reads the WARMC_* options from
   the environment as a program's start does, and returns 0 when the code
   cannot run on this machine.  It also sets the stack limit an instance starts with,
   and needs who is calling, where that thread's stack ends, and a wait to
   block a contended call on. */
int warmc_rt_library_mode(void) {
#if defined(WARMC_DARWIN)
  g_library = 1;
  g_environ = *_NSGetEnviron();
#elif defined(WARMC_WINDOWS)
  if (!warmc_win_isa_ok()) {
    eputs("warmc-rt: this library needs an x86-64-v3 CPU (AVX2, BMI2, FMA)\n");
    return 0;
  }
  g_environ = warmc_win_environ();
#else
  g_library = 1;
  g_environ = proc_environ();
#endif
  gc_read_options();
  return 1;
}
void warmc_rt_set_stack_limit(u64 limit) { g_stack_limit = limit; }
void warmc_rt_wait(u32 *w, u32 expect) { gcth_wait(w, expect, 0); }
void warmc_rt_wake(u32 *w) { gcth_wake(w); }

#if defined(WARMC_DARWIN)
extern void *pthread_get_stackaddr_np(void *);
extern usz pthread_get_stacksize_np(void *);
u64 warmc_rt_thread_id(void) { return (u64)(uptr)pthread_self(); }
void warmc_rt_stack_bounds(u64 *lo, u64 *hi) {
  void *t = pthread_self();
  *hi = (u64)(uptr)pthread_get_stackaddr_np(t);
  *lo = *hi - pthread_get_stacksize_np(t);
}
#elif defined(WARMC_WINDOWS)
u64 warmc_rt_thread_id(void) { return GetCurrentThreadId(); }
void warmc_rt_stack_bounds(u64 *lo, u64 *hi) {
  uptr l = 0, h = 0;
  GetCurrentThreadStackLimits(&l, &h);
  *lo = l;
  *hi = h;
}
#else
u64 warmc_rt_thread_id(void) { return (u64)sys(178 /* gettid */, 0, 0, 0, 0, 0, 0); }

static u64 hex_at(const char **p) {
  u64 v = 0;
  for (;; (*p)++) {
    char c = **p;
    if (c >= '0' && c <= '9') v = v * 16 + (u64)(c - '0');
    else if (c >= 'a' && c <= 'f') v = v * 16 + (u64)(c - 'a' + 10);
    else return v;
  }
}

/* The mapping that holds the stack pointer, read from /proc/self/maps: a
   thread's stack is one mapping, its guard page another.  The main
   thread's [stack] grows down on demand, so its floor is RLIMIT_STACK
   below its top.  Slow -- rt/lib.c keeps the answer per thread. */
void warmc_rt_stack_bounds(u64 *lo, u64 *hi) {
  *lo = *hi = 0;
  u64 sp;
  __asm__("mov %0, sp" : "=r"(sp));
  long fd = sys(56 /* openat */, -100 /* AT_FDCWD */, (long)"/proc/self/maps",
                0, 0, 0, 0);
  if (fd < 0) return;
  char buf[4096], line[512];
  u32 ln = 0;
  long n;
  int found = 0;
  while (!found && (n = sys(63 /* read */, fd, (long)buf, sizeof buf, 0, 0, 0)) > 0) {
    for (long i = 0; i < n && !found; i++) {
      if (buf[i] != '\n') { if (ln < sizeof line - 1) line[ln++] = buf[i]; continue; }
      line[ln] = 0;
      ln = 0;
      const char *p = line;
      u64 a = hex_at(&p);
      if (*p++ != '-') continue;
      u64 b = hex_at(&p);
      if (sp < a || sp >= b) continue;
      *lo = a;
      *hi = b;
      found = 1;
      for (const char *q = p; *q; q++)
        if (q[0] == '[' && q[1] == 's' && q[2] == 't' && q[3] == 'a' &&
            q[4] == 'c' && q[5] == 'k' && q[6] == ']') {
          /* unlimited, or unknown: as much as a default limit allows */
          u64 rl[2] = { 0, 0 }, cap = 8u << 20;
          if (sys(261 /* prlimit64 */, 0, 3 /* RLIMIT_STACK */, 0, (long)rl, 0, 0) == 0 &&
              rl[0] && rl[0] < b)
            cap = rl[0];
          *lo = b > cap ? b - cap : 0;
          break;
        }
    }
  }
  sys(57 /* close */, fd, 0, 0, 0, 0, 0);
}
#endif

static int name_eq(const char *a, u32 an, const char *b, u32 bn) {
  if (an != bn) return 0;
  for (u32 i = 0; i < an; i++) if (a[i] != b[i]) return 0;
  return 1;
}

static u32 name_len(const char *s) {
  u32 n = 0;
  while (s[n]) n++;
  return n;
}

void warmc_register_n(const char *name, u32 len, WarmInstance *inst) {
  if (nregistry >= MAX_INSTANCES) die("too many registered modules");
  registry[nregistry].name = name;
  registry[nregistry].name_len = len;
  registry[nregistry].inst = inst;
  nregistry++;
}

void warmc_register(const char *name, WarmInstance *inst) {
  warmc_register_n(name, name_len(name), inst);
}

static WarmInstance *lookup_module(const char *name, u32 n) {
  for (u32 i = nregistry; i > 0; i--)
    if (name_eq(registry[i - 1].name, registry[i - 1].name_len, name, n))
      return registry[i - 1].inst;
  return 0;
}

const ExportDesc *warmc_find_export_n(WarmInstance *inst, const char *name,
                                      u32 n) {
  const ModuleDesc *md = INST_OF(inst)->md;
  for (u32 i = 0; i < md->nexport; i++)
    if (name_eq(md->exports[i].name, md->exports[i].name_len, name, n))
      return &md->exports[i];
  return 0;
}

const ExportDesc *warmc_find_export(WarmInstance *inst, const char *name) {
  return warmc_find_export_n(inst, name, name_len(name));
}

FuncRef warmc_export_func(WarmInstance *inst, const ExportDesc *e) {
  return inst_funcrefs(INST_OF(inst))[e->index];
}

__attribute__((noreturn)) void warmc_rt_exit(int code) { sys_exit(code); }

u32 warmc_bind_import(WarmInstance *v, const char *module, const char *name,
                      void *code, WarmInstance *self) {
  u32 mn = name_len(module), nn = name_len(name);
  Instance *in = INST_OF(v);
  const ModuleDesc *md = in->md;
  u64 *imports = inst_imports(in);
  FuncRef *frs = inst_funcrefs(in);
  u32 fi = 0, bound = 0;
  for (u32 i = 0; i < md->nimports; i++) {
    const ImportDesc *im = &md->imports[i];
    if (im->kind != 0) continue;
    if (name_eq(im->module, im->module_len, module, mn) &&
        name_eq(im->name, im->name_len, name, nn)) {
      imports[2 * fi] = (u64)(uptr)code;
      imports[2 * fi + 1] = (u64)(uptr)self;
      frs[fi].code = code;
      frs[fi].instance = self;
      bound++;
    }
    fi++;
  }
  return bound;
}

u64 *warmc_global_cell(WarmInstance *inst, u32 index) {
  return global_cell(INST_OF(inst), index);
}

#define GC_HEAP_BYTES (1ull << 30)

static u64 gc_heap_bytes = GC_HEAP_BYTES;

static u8 *gc_base;

#define GC_MAX_TYPES (1u << 20)
_Static_assert(GC_MAX_TYPES <= (1u << 27),
               "a type index has to stay below the mark colour's high bit");
static const GcTypeInfo **gc_typetab;
static u32 gc_ntypes;
static struct { const ModuleDesc *md; u32 base; } gc_desc_types[MAX_MODULE_DESCS];
static u32 gc_ndesc_types;

static const GcTypeInfo gc_type_reserved;

static void gc_typeinfo_fail(u32 id) {
  if (!id) die("a heap object names the reserved type index zero");
  die("a heap object names a type that does not exist");
}

static const GcTypeInfo *gc_typeinfo(u32 w) {
  u32 id = GCW_TYPE(w);

  if (id - 1u >= gc_ntypes - 1u) gc_typeinfo_fail(id);
  return gc_typetab[id];
}

static u64 gc_object_bytes(const GcHeader *h) {
  const GcTypeInfo *ti = gc_typeinfo(h->w);
  u64 payload = ti->kind == GCK_ARRAY ? (u64)h->len * ti->body_size
                                      : (u64)ti->body_size;
  u64 total = ti->hdr_size + payload;
  if (total < GC_UNIT) total = GC_UNIT;
  return (total + (GC_UNIT - 1)) & ~(u64)(GC_UNIT - 1);
}

static u32 gc_opt_stress;
static u32 gc_opt_stats;
static u32 gc_opt_growth = 200;
static u64 gc_opt_min_kb = 4096;
static u32 gc_opt_reloc = 4;
static u64 gc_opt_release;

static u64 gc_live, gc_marked, gc_trigger;

/* What the program writes on its allocation, barrier and mark-queue paths,
   and what the collector thread writes for every object it marks, each in
   cache lines of their own.  A line is 128 bytes on Apple silicon (and x86
   fetches lines in adjacent pairs), so the 64-byte alignment these counters
   had left a hot counter of one side beside the other side's; which ones met
   moved with the size of the code linked in front of them, and ktbench/list
   ran in 70 ms or 130 ms depending on it. */
#define GC_LINE 128u
static struct {
  u64 alloc_since, bytes_alloc, fast_bytes, barrier_slow;
} __attribute__((aligned(GC_LINE))) gc_mut;
/* The program's end of the mark queue: head moves with every object a
   barrier shades, and the collector polls pub while it waits for work, so
   the pair is kept off the counters' line as well. */
static struct {
  u64 head, pub;
} __attribute__((aligned(GC_LINE))) gc_mqp;
static struct {
  u64 ngrey, mark_conc_objs, mark_wait_took, mq_tail;
} __attribute__((aligned(GC_LINE))) gc_col;

static u64 gc_full_req, gc_full_done, gc_cycle_serves;

static u64 gc_ncycles, gc_npages_opened, gc_npages_freed;

static u64 gc_mark_end_took, gc_ncycles_stw;
static u64 gc_released, gc_nreleases;
static u64 gc_t_total, gc_pause_count, gc_pause_max, gc_pause_hist[5];

#define GC_GRAN_SHIFT 16u
#define GC_GRAN       (1ull << GC_GRAN_SHIFT)
#define GC_SMALL_GRAN 1u
#define GC_MEDIUM_GRAN 8u
#define GC_SMALL_MAX  (GC_GRAN / 8)
#define GC_MEDIUM_MAX (GC_GRAN * GC_MEDIUM_GRAN / 8)

enum { GCP_FREE = 0, GCP_SMALL, GCP_MEDIUM, GCP_LARGE, GCP_FWD };

enum { GCP_TAIL = 0xffu };

typedef struct {
  u8  kind;
  u8  dirty;
  u8  reloc;
  u32 ngran;
  u32 head;
  u8 *top;
  u64 live;

  u32 alloc_cycle;
} GcPage;

static GcPage *gc_pages;
static u64 gc_ngran;
static u64 *gc_granfree;
static u64 gc_granfree_words;
static u64 gc_gran_used;
static u64 gc_gran_peak;

static u64 gc_gran_hwm;

static u32 gc_small_page = (u32)-1, gc_medium_page = (u32)-1;

static u32 gc_page_of(const void *p) {
  return (u32)((GC_ADDR(p) - (u64)(uptr)gc_base) >> GC_GRAN_SHIFT);
}
static u32 gc_page_head(u32 g) {
  return __atomic_load_n(&gc_pages[g].kind, __ATOMIC_ACQUIRE) == GCP_TAIL
             ? gc_pages[g].head : g;
}
static u8 *gc_gran_addr(u32 g) { return gc_base + (u64)g * GC_GRAN; }

static u32 gc_in_heap(u64 v) {
  if (!GC_IS_OBJ(v)) return 0;
  u64 off = GC_ADDR(v) - (u64)(uptr)gc_base;
  if (off >= gc_heap_bytes) return 0;
  return __atomic_load_n(&gc_pages[off >> GC_GRAN_SHIFT].kind,
                         __ATOMIC_ACQUIRE) != GCP_FREE;
}

static void gc_check_tbi(void);

static void gc_gran_mark_free(u64 g, u64 n, u32 free) {
  for (u64 i = g; i < g + n; i++) {
    if (free) gc_granfree[i >> 6] |= 1ull << (i & 63);
    else gc_granfree[i >> 6] &= ~(1ull << (i & 63));
  }
}

static u64 gc_gran_take(u32 n) {
  u64 run = 0, start = 0;
  u64 lim = (gc_gran_hwm + n + 64) / 64 + 1;
  if (lim > gc_granfree_words) lim = gc_granfree_words;
  for (u64 w = 0; w < lim; w++) {
    u64 bits = gc_granfree[w];
    if (!bits) { run = 0; continue; }
    for (u64 b = 0; b < 64; b++) {
      u64 g = w * 64 + b;
      if (g >= gc_ngran) break;
      if (bits & (1ull << b)) {
        if (!run) start = g;
        if (++run == n) {
          gc_gran_mark_free(start, n, 0);
          gc_gran_used += n;
          if (start + n > gc_gran_hwm) gc_gran_hwm = start + n;
          if (gc_gran_used > gc_gran_peak) gc_gran_peak = gc_gran_used;
          return start;
        }
      } else {
        run = 0;
      }
    }
  }
  return (u64)-1;
}

static u64 *gc_markbits;
static u32 *gc_page_epoch;

static u8 **gc_grey;
static u64 gc_capgrey;
static u64 gc_cycle_no = 1;

static u8 **gc_mq;

#define GC_MQ_BATCH 32u

enum { GCPH_IDLE = 0, GCPH_MARK = 1, GCPH_RELOC = 2 };
__attribute__((aligned(GC_LINE))) static u32 gc_phase;

static u32 gc_conc_on, gc_conc_tried, gc_opt_conc = 1;

static u32 gc_cycle_running;

#define GC_EPOCH_BUSY(c)  ((u32)((u32)(c) * 2u))
#define GC_EPOCH_READY(c) ((u32)((u32)(c) * 2u + 1u))

static void gc_bits_claim_page(u32 h) {
  u32 ready = GC_EPOCH_READY(gc_cycle_no), busy = GC_EPOCH_BUSY(gc_cycle_no);
  u32 e = __atomic_load_n(&gc_page_epoch[h], __ATOMIC_ACQUIRE);
  for (;;) {
    if (e == ready) return;
    if (e == busy) {
      while (__atomic_load_n(&gc_page_epoch[h], __ATOMIC_ACQUIRE) != ready)
        warmc_cpu_relax();
      return;
    }
    if (__atomic_compare_exchange_n(&gc_page_epoch[h], &e, busy, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
      break;
  }
  u64 first = ((u64)h * GC_GRAN) / GC_UNIT;
  u64 nbits = ((u64)gc_pages[h].ngran * GC_GRAN) / GC_UNIT;
  for (u64 i = 0; i < nbits / 64; i++) gc_markbits[first / 64 + i] = 0;
  __atomic_store_n(&gc_page_epoch[h], ready, __ATOMIC_RELEASE);
}

static void gc_add_live(u32 h, u64 bytes) {
  if (!gc_conc_on) { gc_pages[h].live += bytes; return; }
  __atomic_fetch_add(&gc_pages[h].live, bytes, __ATOMIC_RELAXED);
}

static u32 gc_bit_set(u32 g, u64 addr) {
  u32 h = gc_page_head(g);
  if (__atomic_load_n(&gc_page_epoch[h], __ATOMIC_ACQUIRE) !=
      GC_EPOCH_READY(gc_cycle_no))
    gc_bits_claim_page(h);
  u64 i = (GC_ADDR(addr) - (u64)(uptr)gc_base) / GC_UNIT;
  u64 m = 1ull << (i & 63);

  if (!gc_conc_on) {
    if (gc_markbits[i >> 6] & m) return 0;
    gc_markbits[i >> 6] |= m;
    return 1;
  }
  u64 old = __atomic_fetch_or(&gc_markbits[i >> 6], m, __ATOMIC_RELAXED);
  return (old & m) ? 0 : 1;
}

static u32 gc_bit_test(u64 addr) {
  u64 i = (GC_ADDR(addr) - (u64)(uptr)gc_base) / GC_UNIT;
  return (gc_markbits[i >> 6] >> (i & 63)) & 1u;
}

static u64 gc_bits_next(u64 i, u64 e) {
  while (i < e) {
    u64 w = gc_markbits[i >> 6] & (~0ull << (i & 63));
    if (w) {
      u64 n = (i & ~63ull) + (u64)__builtin_ctzll(w);
      return n < e ? n : e;
    }
    i = (i & ~63ull) + 64;
  }
  return e;
}

#define GC_PAGE_FOREACH(h, var)                                               \
  for (u64 _b = ((u64)(h) * GC_GRAN) / GC_UNIT,                               \
            _e = _b + ((u64)gc_pages[h].ngran * GC_GRAN) / GC_UNIT,           \
            _i = (gc_page_epoch[h] == GC_EPOCH_READY(gc_cycle_no)              \
                      ? gc_bits_next(_b, _e)                                  \
                      : _e);                                                  \
       _i < _e && ((var) = gc_base + _i * GC_UNIT, 1);                        \
       _i = gc_bits_next(_i + 1, _e))

static void gc_release_pages(u32 h, u32 n) {
  if (!gc_opt_release) return;
  u64 bytes = (u64)n * GC_GRAN;
  if (bytes < gc_opt_release) return;
  if (sys_madvise(gc_gran_addr(h), (usz)bytes, WARMC_MADV_FREE) == 0) {
    gc_released += bytes;
    gc_nreleases++;

    if (WARMC_MADV_ZEROES)
      for (u32 i = 0; i < n; i++) gc_pages[h + i].dirty = 0;
  }
}

static void gc_init(void) {
  if (gc_base) return;

  /* Their addresses escape here, or the optimiser splits each struct into
     separate scalars and lays them out as it likes -- padding and line gone. */
  __asm__ volatile("" : : "r"(&gc_mut), "r"(&gc_mqp), "r"(&gc_col) : "memory");

  gc_base = (u8 *)sys_mmap_lazy((usz)(gc_heap_bytes + GC_GRAN));
  if (!gc_base || gc_base == (u8 *)-1) die("cannot reserve the managed heap");

  gc_base += (u64)(-(long)(u64)gc_base) & (GC_GRAN - 1);
  gc_heap_bytes &= ~(GC_GRAN - 1);
  if (gc_heap_bytes < 2 * GC_GRAN) die("the managed heap is smaller than two pages");
  gc_ngran = gc_heap_bytes / GC_GRAN;

  gc_pages = (GcPage *)sys_mmap(0, (usz)(gc_ngran * sizeof(GcPage)),
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS);
  gc_page_epoch = (u32 *)sys_mmap(0, (usz)(gc_ngran * sizeof(u32)),
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS);
  gc_granfree_words = (gc_ngran + 63) / 64;
  gc_granfree = (u64 *)sys_mmap(0, (usz)(gc_granfree_words * 8),
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS);

  gc_markbits = (u64 *)sys_mmap_lazy((usz)(gc_heap_bytes / GC_UNIT / 8));

  /* Each as large as the heap's units, 512 MB a piece by default, and
     touched only as far as a cycle's grey objects reach. */
  gc_capgrey = gc_heap_bytes / GC_UNIT;
  gc_grey = (u8 **)sys_mmap_lazy((usz)(gc_capgrey * sizeof(u8 *)));
  gc_mq = (u8 **)sys_mmap_lazy((usz)(gc_capgrey * sizeof(u8 *)));
  if (!gc_pages || gc_pages == (GcPage *)-1 || !gc_granfree ||
      gc_granfree == (u64 *)-1 || !gc_markbits || gc_markbits == (u64 *)-1 ||
      !gc_grey || gc_grey == (u8 **)-1 || !gc_page_epoch ||
      gc_page_epoch == (u32 *)-1 || !gc_mq || gc_mq == (u8 **)-1)
    die("cannot reserve the collector's side tables");
  gc_gran_mark_free(0, gc_ngran, 1);
  gc_check_tbi();

  gc_typetab = (const GcTypeInfo **)sys_mmap_lazy(
      (usz)(GC_MAX_TYPES * sizeof(void *)));
  if (!gc_typetab || gc_typetab == (const GcTypeInfo **)-1)
    die("cannot reserve the type table");

  gc_typetab[0] = &gc_type_reserved;
  gc_ntypes = 1;
  gc_trigger = gc_opt_min_kb * 1024ull;
}

static u32 gc_register_types(const ModuleDesc *md) {

  if (!md->ngctypes && !md->ntags) return 0;
  gc_init();
  for (u32 i = 0; i < gc_ndesc_types; i++)
    if (gc_desc_types[i].md == md) return gc_desc_types[i].base;
  if (gc_ndesc_types >= MAX_MODULE_DESCS) die("too many modules");
  if (gc_ntypes + md->ngctypes + md->ntags > GC_MAX_TYPES)
    die("too many heap types");
  u32 base = gc_ntypes;
  for (u32 i = 0; i < md->ngctypes; i++) gc_typetab[gc_ntypes++] = &md->gctypes[i];
  for (u32 i = 0; i < md->ntags; i++) gc_typetab[gc_ntypes++] = &md->exntypes[i];
  gc_desc_types[gc_ndesc_types].md = md;
  gc_desc_types[gc_ndesc_types].base = base;
  gc_ndesc_types++;
  return base;
}

static u64 gc_good = GC_C_R;
static u64 gc_badmask = GC_C_M0 | GC_C_M1;
static u32 gc_mark_epoch;

static void gc_publish_colour(void) {
  gc_badmask = GC_COLOUR_MASK & ~gc_good;
  for (u32 i = 0; i < ninstances; i++)
    if (instances[i]) instances[i]->vm.gc_badmask = gc_badmask;
}

static void gc_check_tbi(void) {
#if defined(__x86_64__)
  return;   /* no TBI: every deref masks the colour instead (GC_CANON) */
#endif
  volatile u64 *raw = (volatile u64 *)gc_base;
  volatile u64 *tagged = (volatile u64 *)((u64)(uptr)gc_base | GC_COLOUR_MASK);
  *raw = 0x5aa5c33c0f0f1e1eull;
  if (*tagged != 0x5aa5c33c0f0f1e1eull)
    die("this machine does not ignore the top byte of an address");
  *tagged = 0;
  if (*raw != 0) die("this machine does not ignore the top byte of an address");
}

static void gc_zero(u8 *p, u64 n) {
  v16 z = (v16){0};
  u64 i = 0;
  for (; i + 64 <= n; i += 64) {
    __builtin_memcpy(p + i, &z, 16);      __builtin_memcpy(p + i + 16, &z, 16);
    __builtin_memcpy(p + i + 32, &z, 16); __builtin_memcpy(p + i + 48, &z, 16);
  }
  for (; i + 16 <= n; i += 16) __builtin_memcpy(p + i, &z, 16);
  for (; i < n; i++) p[i] = 0;
}

static u32 gc_page_lock;

static u32 gc_lock(void) {
  if (!__atomic_load_n(&gc_conc_on, __ATOMIC_RELAXED)) return 0;
  while (__atomic_exchange_n(&gc_page_lock, 1u, __ATOMIC_ACQUIRE))
    warmc_cpu_relax();
  return 1;
}

static void gc_unlock(u32 held) {
  if (held) __atomic_store_n(&gc_page_lock, 0u, __ATOMIC_RELEASE);
}

static u32 gc_page_open(u32 ngran, u8 kind) {
  u64 g = gc_gran_take(ngran);
  if (g == (u64)-1) return (u32)-1;
  for (u32 i = 0; i < ngran; i++) {
    GcPage *T = &gc_pages[g + i];
    if (T->dirty) gc_zero(gc_gran_addr((u32)g + i), GC_GRAN);
    T->dirty = 1;
    T->reloc = 0;
    T->head = (u32)g;
    T->ngran = 0;
    T->live = 0;
    __atomic_store_n(&T->kind, (u8)GCP_TAIL, __ATOMIC_RELEASE);
  }
  GcPage *P = &gc_pages[g];
  P->ngran = ngran;
  P->top = gc_gran_addr((u32)g);
  P->live = 0;

  P->alloc_cycle = (u32)gc_cycle_no;

  __atomic_store_n(&P->kind, kind, __ATOMIC_RELEASE);

  gc_page_epoch[g] = 0;
  gc_npages_opened++;
  return (u32)g;
}

static void gc_page_free(u32 h) {
  GcPage *P = &gc_pages[h];
  u32 n = P->ngran;
  for (u32 i = 0; i < n; i++) {
    gc_pages[h + i].kind = GCP_FREE;
    gc_pages[h + i].ngran = 0;
    gc_pages[h + i].live = 0;
    gc_pages[h + i].reloc = 0;
  }
  gc_gran_mark_free(h, n, 1);
  gc_gran_used -= n;
  gc_npages_freed++;
  gc_release_pages(h, n);
}

static u64 gc_page_room(u32 h) {
  return (u64)(gc_gran_addr(h) + (u64)gc_pages[h].ngran * GC_GRAN - gc_pages[h].top);
}

/* [0] the cursor, [1] the end; compiled code bumps [0] on every inline
   allocation through vmctx, so the cell has a line to itself. */
static u64 gc_lab_cell[GC_LINE / 8] __attribute__((aligned(GC_LINE)));
static u8 *gc_lab_start;
static u32 gc_opt_lab = 1;
static u64 gc_nlabs;
#define GC_LAB_CAP (16u << 10)

#define GC_LAB_MIN_TAIL 256u

static u64 gc_lab_sync(void) {
  if (!gc_lab_start) return 0;
  u64 used = GC_ADDR(gc_lab_cell[0]) - (u64)(uptr)gc_lab_start;
  gc_lab_start = (u8 *)(uptr)GC_ADDR(gc_lab_cell[0]);
  gc_mut.alloc_since += used;
  gc_mut.fast_bytes += used;
  return used;
}

static u64 gc_lab_close(void) {
  u64 used = gc_lab_sync();
  if (gc_lab_cell[1]) {
    u32 h = gc_page_head(gc_page_of((void *)(uptr)gc_lab_cell[0]));
    if ((u64)(uptr)gc_pages[h].top == GC_ADDR(gc_lab_cell[1]))
      gc_pages[h].top = (u8 *)(uptr)GC_ADDR(gc_lab_cell[0]);
  }
  gc_lab_start = 0;
  gc_lab_cell[0] = gc_lab_cell[1] = 0;
  return used;
}

static void gc_lab_recolour(void) {
  if (!gc_lab_cell[1]) return;
  gc_lab_cell[0] = GC_ADDR(gc_lab_cell[0]) | gc_good;
  gc_lab_cell[1] = GC_ADDR(gc_lab_cell[1]) | gc_good;
}

static u8 *gc_bump(u32 *pg, u32 ngran, u8 kind, u64 total) {
  for (u32 round = 0; round < 2; round++) {
    if (*pg != (u32)-1 && gc_page_room(*pg) >= total) {
      u8 *p = gc_pages[*pg].top;
      gc_pages[*pg].top += total;

      gc_pages[*pg].alloc_cycle = (u32)gc_cycle_no;
      return p;
    }
    if (round) break;
    u32 h = gc_page_open(ngran, kind);
    if (h == (u32)-1) return 0;
    *pg = h;
  }
  return 0;
}

static u8 *gc_alloc_raw(u64 total) {
  u32 held = gc_lock();
  u8 *p = 0;
  if (total <= GC_SMALL_MAX) {
    p = gc_bump(&gc_small_page, GC_SMALL_GRAN, GCP_SMALL, total);
  } else if (total <= GC_MEDIUM_MAX) {
    p = gc_bump(&gc_medium_page, GC_MEDIUM_GRAN, GCP_MEDIUM, total);
  } else {

    u32 n = (u32)((total + GC_GRAN - 1) / GC_GRAN);
    u32 h = gc_page_open(n, GCP_LARGE);
    if (h != (u32)-1) {
      gc_pages[h].top = gc_gran_addr(h) + total;
      p = gc_gran_addr(h);
    }
  }
  gc_unlock(held);
  return p;
}

static void gc_lab_refill(void) {
  if (!gc_opt_lab || gc_opt_stress) return;
  u64 q = gc_trigger > gc_mut.alloc_since ? gc_trigger - gc_mut.alloc_since : 0;
  if (q > GC_LAB_CAP) q = GC_LAB_CAP;
  q &= ~(u64)(GC_UNIT - 1);
  if (q < 2 * GC_LAB_MIN_TAIL) return;
  u8 *p = gc_alloc_raw(q);
  if (!p) return;
  gc_nlabs++;
  gc_lab_start = p;

  gc_lab_cell[0] = (u64)(uptr)p | gc_good;
  gc_lab_cell[1] = ((u64)(uptr)p + q) | gc_good;
}

static u64 gc_heal_ptr(u64 v);
static u64 gc_heal_reloc(u64 v);

static u64 gc_born_skipped;

static u32 gc_being_born(u64 v) {
  if (GCW_TYPE(((const GcHeader *)(uptr)GC_ADDR(v))->w)) return 0;
  if (!__atomic_load_n(&gc_conc_on, __ATOMIC_RELAXED))
    die("a heap object names the reserved type index zero");
  gc_born_skipped++;
  return 1;
}

static void gc_mark_word(u64 v) {
  if (!gc_in_heap(v)) return;
  if (gc_being_born(v)) return;
  u32 g = gc_page_of((void *)(uptr)v);
  if (!gc_bit_set(g, v)) return;
  u8 *p = (u8 *)(uptr)GC_CANON(v);
  u32 h = gc_page_head(g);
  gc_add_live(h, gc_object_bytes((const GcHeader *)p));
  if (gc_col.ngrey >= gc_capgrey) die("the collector's mark stack overflowed");
  gc_grey[gc_col.ngrey++] = p;
}

static void gc_mark_ref(u64 v) {
  if (!gc_in_heap(v)) return;
  if (gc_being_born(v)) return;
  u32 g = gc_page_of((void *)(uptr)v);
  if (!gc_bit_set(g, v)) return;
  u8 *p = (u8 *)(uptr)GC_CANON(v);
  u32 h = gc_page_head(g);
  gc_add_live(h, gc_object_bytes((const GcHeader *)p));
  gc_mq[gc_mqp.head++] = p;

  if (gc_mqp.head - gc_mqp.pub >= GC_MQ_BATCH)
    __atomic_store_n(&gc_mqp.pub, gc_mqp.head, __ATOMIC_RELEASE);
}

enum {
  GCJ_ROUND,
  GCJ_IDLE,
  GCJ_MARK,
  GCJ_RELOC,
  GCJ_SLOT,
  GCJ_N
};

static void gc_jitter(u32 point);

static void gc_mark_slot(u64 *slot) {
  u64 v = __atomic_load_n(slot, __ATOMIC_RELAXED);
  if (!gc_in_heap(v)) return;
  u64 h = gc_heal_ptr(v);
  if (h != v) {

    gc_jitter(GCJ_SLOT);
    __atomic_compare_exchange_n(slot, &v, h, 0, __ATOMIC_RELAXED,
                                __ATOMIC_RELAXED);
  }
  gc_mark_word(h);
}

static void gc_reloc_slot(u64 *slot) {
  u64 v = *slot;
  if (!gc_in_heap(v)) return;
  *slot = gc_heal_reloc(v);
}

static void gc_scan_object(u8 *p) {
  const GcHeader *h = (const GcHeader *)p;
  const GcTypeInfo *ti = gc_typeinfo(h->w);
  if (ti->kind == GCK_ARRAY) {
    if (!ti->elem_is_ref) return;
    u64 n = h->len;
    u8 *e = p + ti->hdr_size;
    for (u64 i = 0; i < n; i++) gc_mark_slot((u64 *)(e + i * ti->body_size));
    return;
  }
  const u32 *refoffs = gcti_refoffs(ti);
  for (u32 i = 0; i < ti->nrefs; i++) gc_mark_slot((u64 *)(p + refoffs[i]));
}

static void gc_drain(void) {
  while (gc_col.ngrey) gc_scan_object(gc_grey[--gc_col.ngrey]);
}

static u64 gc_take_queue(void) {
  u64 pub = __atomic_load_n(&gc_mqp.pub, __ATOMIC_ACQUIRE);
  u64 n = pub - gc_col.mq_tail;
  while (gc_col.mq_tail < pub) {
    if (gc_col.ngrey >= gc_capgrey) die("the collector's mark stack overflowed");
    gc_grey[gc_col.ngrey++] = gc_mq[gc_col.mq_tail++];
  }
  return n;
}

#define gc_mark_slot_shared gc_mark_slot

static u64 gc_reloc_pages, gc_reloc_bytes, gc_reloc_objs, gc_reloc_stalls;

static u64 gc_reloc_lost, gc_reloc_mut, gc_reloc_pages_all, gc_reloc_objs_all;

static u32 gc_opt_reloc_stall;
static u64 gc_reloc_tried;
static u32 gc_evac_small = (u32)-1, gc_evac_medium = (u32)-1;

static u64 *gc_fwd;
static u64 gc_fwd_slots;

static u64 gc_fwd_hash(u64 a) {
  a >>= 4;
  a *= 0x9e3779b97f4a7c15ull;
  return a >> 32;
}

static u64 gc_fwd_await(u64 i) {
  for (;;) {
    u64 to = __atomic_load_n(&gc_fwd[i * 2 + 1], __ATOMIC_ACQUIRE);
    if (to) return to;
    warmc_cpu_relax();
  }
}

static u64 gc_fwd_get(u64 from) {
  if (!__atomic_load_n(&gc_fwd, __ATOMIC_RELAXED)) return 0;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  u64 m = gc_fwd_slots - 1;
  u64 i = gc_fwd_hash(from) & m;
  for (u64 tries = 0; ; tries++) {
    if (tries > gc_fwd_slots) die("the forwarding table has no free slot");
    u64 k = __atomic_load_n(&gc_fwd[i * 2], __ATOMIC_ACQUIRE);
    if (!k) return 0;
    if (k == from) return gc_fwd_await(i);
    i = (i + 1) & m;
  }
}

static u64 gc_fwd_claim(u64 from, u32 *mine) {
  u64 m = gc_fwd_slots - 1;
  u64 i = gc_fwd_hash(from) & m;

  for (u64 tries = 0; ; tries++) {
    if (tries > gc_fwd_slots) die("the forwarding table filled up");
    u64 k = __atomic_load_n(&gc_fwd[i * 2], __ATOMIC_ACQUIRE);
    if (!k) {
      u64 expect = 0;
      if (__atomic_compare_exchange_n(&gc_fwd[i * 2], &expect, from, 0,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        *mine = 1;
        return i;
      }
      k = expect;
    }
    if (k == from) { *mine = 0; return i; }
    i = (i + 1) & m;
  }
}

static void gc_page_free(u32 h);

static u64 *gc_fwd_dead;
static u64 gc_fwd_dead_slots;

static void gc_fwd_retire(void) {
  gc_fwd_dead = gc_fwd;
  gc_fwd_dead_slots = gc_fwd_slots;
  gc_fwd = 0;
  gc_fwd_slots = 0;

  for (u32 h = 0; h < gc_gran_hwm; h++)
    if (gc_pages[h].kind == GCP_FWD) gc_page_free(h);
}

static void gc_fwd_unmap(void) {
  if (gc_fwd_dead) sys_munmap(gc_fwd_dead, (usz)(gc_fwd_dead_slots * 16));
  gc_fwd_dead = 0;
  gc_fwd_dead_slots = 0;
}

static u64 gc_reloc_object(u64 from);

static u32 gc_reloc_wanted(u64 a) {
  if (__atomic_load_n(&gc_phase, __ATOMIC_ACQUIRE) != GCPH_RELOC) return 0;
  u64 off = a - (u64)(uptr)gc_base;
  if (off >= gc_heap_bytes) return 0;
  u32 h = gc_page_head((u32)(off >> GC_GRAN_SHIFT));
  return __atomic_load_n(&gc_pages[h].reloc, __ATOMIC_ACQUIRE);
}

static u64 gc_heal_ptr(u64 v) {
  u64 a = GC_ADDR(v);
  u64 to = gc_fwd_get(a);
  return (to ? to : a) | gc_good;
}

static u64 gc_heal_reloc(u64 v) {
  u64 a = GC_ADDR(v);
  u64 to = gc_fwd_get(a);
  if (!to) {
    if (gc_reloc_wanted(a)) to = gc_reloc_object(a);
    else to = gc_fwd_get(a);
  }
  return (to ? to : a) | gc_good;
}

static u64 gc_page_nlive(u32 h) {
  u64 b = ((u64)h * GC_GRAN) / GC_UNIT;
  u64 e = b + ((u64)gc_pages[h].ngran * GC_GRAN) / GC_UNIT;
  if (gc_page_epoch[h] != GC_EPOCH_READY(gc_cycle_no)) return 0;
  u64 n = 0;
  for (u64 w = b / 64; w < e / 64; w++) n += (u64)__builtin_popcountll(gc_markbits[w]);
  return n;
}

static u32 gc_reloc_candidate(u32 h, u32 eighths) {
  const GcPage *P = &gc_pages[h];
  if (P->kind != GCP_SMALL && P->kind != GCP_MEDIUM) return 0;
  if (h == gc_small_page || h == gc_medium_page) return 0;

  if (P->alloc_cycle == (u32)gc_cycle_no) return 0;
  if (P->reloc) return 0;
  u64 cap = (u64)P->ngran * GC_GRAN;
  return P->live * 8 <= cap * eighths;
}

static void gc_select_reloc(void) {

  gc_reloc_pages = gc_reloc_bytes = gc_reloc_objs = 0;
  if (!gc_opt_reloc) return;
  u64 room = (gc_ngran - gc_gran_used) * GC_GRAN;

  room = room > 4 * GC_GRAN ? room - 4 * GC_GRAN : 0;
  u64 nobj = 0;
  for (u32 eighths = 1; eighths <= gc_opt_reloc && eighths <= 8; eighths++) {
    for (u32 h = 0; h < gc_gran_hwm; h++) {

      u8 kind = __atomic_load_n(&gc_pages[h].kind, __ATOMIC_ACQUIRE);
      if (kind == GCP_TAIL || kind == GCP_FREE) continue;
      if (!gc_reloc_candidate(h, eighths)) continue;
      if (gc_pages[h].live > room) continue;
      gc_pages[h].reloc = 1;
      room -= gc_pages[h].live;
      gc_reloc_bytes += gc_pages[h].live;
      gc_reloc_pages++;
      gc_reloc_pages_all++;
      nobj += gc_page_nlive(h);
    }
  }
  if (!gc_reloc_pages) return;

  u64 slots = 16;
  while (slots < nobj * 2) slots <<= 1;
  u64 *t = (u64 *)sys_mmap(0, (usz)(slots * 16), PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS);
  if (!t || t == (u64 *)-1) {

    for (u32 h = 0; h < gc_gran_hwm; h++) gc_pages[h].reloc = 0;
    gc_reloc_pages = gc_reloc_bytes = 0;
    return;
  }

  gc_fwd_slots = slots;
  __atomic_store_n(&gc_fwd, t, __ATOMIC_RELEASE);
}

static GcRoots *gc_rootlist;
static const u32 gc_one = 1;

typedef struct { u64 gpr[10]; u64 sp; u64 pc; } GcCtx;
GcCtx warmc_gc_ctx;
static u32 gc_from_wasm;

static u32 gc_may_collect(void) {
  if (gc_no_collect) return 0;
  return !(gc_from_wasm && warmc_gc_invoke_depth > 1);
}

u64 warmc_gc_invoke_depth;

static u32 gc_no_collect;

static const FuncUnwind *find_unwind_in(u64 pc, const ModuleDesc **owner);
static u64 load64(u64 addr);

typedef void (*GcRootFn)(u64 *slot);

static void gc_visit_frames(GcRootFn fn) {
  if (!gc_from_wasm) return;
  u64 pc = warmc_gc_ctx.pc, sp = warmc_gc_ctx.sp;
  u64 *gprloc[10];
  for (u32 i = 0; i < 10; i++) gprloc[i] = &warmc_gc_ctx.gpr[i];
  for (u32 guard = 0; ; guard++) {
    if (guard > (1u << 24)) die("the collector's frame walk did not terminate");

    const ModuleDesc *md = 0;
    const FuncUnwind *u = find_unwind_in(pc, &md);

    if (!u) return;
    u32 off = (u32)(pc - (u64)(uptr)u->code);

    const u8 *s = u->gcsites;
    u32 spc = 0;
    for (u32 i = 0; i < u->ngcsites; i++) {
      spc += uw_uleb(&s);
      u32 first = uw_uleb(&s), n = uw_uleb(&s);
      if (spc < off) continue;
      if (spc > off) break;

      const u8 *r = md->gcroots + first;
      for (u32 k = 0; k < n; k++) {
        u32 e = uw_uleb(&r);
        fn(GCROOT_IS_SLOT(e) ? (u64 *)(uptr)(sp + GCROOT_VAL(e))
                             : gprloc[GCROOT_VAL(e)]);
      }
      break;
    }
    if (!(u->saved & UW_HAS_LR)) return;
    u64 entry = sp + u->frame + u->saved_bytes;
    u64 next_pc = load64(entry - 8);
    u64 base = entry - 16;
    u32 k = 0;
    for (u32 r = 0; r < 10; r++) {
      if (!(u->saved & (1u << (UW_GPR_SHIFT + r)))) continue;

#if defined(__x86_64__)
      gprloc[r] = (u64 *)(uptr)(base - 8ull * k);
#else
      gprloc[r] = (u64 *)(uptr)(base - 16ull * (k / 2 + 1) + 8ull * (k % 2));
#endif
      k++;
    }
    sp = entry;
    pc = next_pc;
  }
}

static void gc_visit_globals(GcRootFn fn) {
  for (u32 i = 0; i < ninstances; i++) {
    Instance *in = instances[i];
    if (!in || !in->md) continue;
    for (u32 g = 0; g < in->md->nglobals; g++) {
      if (in->md->globals[g].type != VT_REF) continue;
      u64 *cell = (u64 *)global_cell(in, g);
      if (cell) fn(cell);
    }
  }
}

static void gc_visit_tables(GcRootFn fn) {
  u32 n = __atomic_load_n(&ninstances, __ATOMIC_ACQUIRE);
  for (u32 i = 0; i < n; i++) {
    Instance *in = instances[i];
    if (!in || !in->md) continue;
    u32 ntab = in->tabs ? in->md->ntables : 0;
    for (u32 t = 0; t < ntab; t++) {
      WarmTable *T = __atomic_load_n(&in->tabs[t], __ATOMIC_ACQUIRE);
      if (!T || !T->entries) continue;
      u64 len = __atomic_load_n(&T->len, __ATOMIC_ACQUIRE);
      for (u64 k = 0; k < len; k++) fn((u64 *)&T->entries[k]);
    }
  }
}

static void gc_visit_strong_roots(GcRootFn fn) {
  gc_visit_frames(fn);
  gc_visit_globals(fn);
  for (u32 i = 0; i < ninstances; i++) {
    Instance *in = instances[i];
    if (!in || !in->md) continue;
    for (u64 k = 0; k < (in->elem_values ? in->nelem_values : 0); k++)
      fn((u64 *)&in->elem_values[k]);
  }
  for (GcRoots *r = gc_rootlist; r; r = r->next)
    for (u32 i = 0; i < *r->n; i++) fn(&r->base[i]);
}

static const unsigned char gc_jitter_shift[GCJ_N] = { 0, 0, 0, 0, 0 };

static const char *const gc_jitter_name[GCJ_N] = { "round", "idle", "mark",
                                                   "reloc", "slot" };

static u32 gc_opt_jitter;
static u64 gc_opt_jitter_seed;
static u64 gc_jitter_state[2];
static u64 gc_jitter_hits[2][GCJ_N];

static u64 gc_jitter_mix(u64 x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return (x ^ (x >> 31)) | 1ull;
}

static u64 gc_jitter_next(u64 *s) {
  u64 x = *s;
  x ^= x << 13; x ^= x >> 7; x ^= x << 17;
  return *s = x;
}

static void gc_jitter(u32 point) {
  if (!gc_opt_jitter) return;
  u32 who = gcth_on_collector() ? 1u : 0u;
  u64 r = gc_jitter_next(&gc_jitter_state[who]);
  u32 sh = gc_jitter_shift[point];
  if ((r & ((256u << sh) - 1u)) >= gc_opt_jitter) return;
  gc_jitter_hits[who][point]++;

  u32 n = (u32)(r >> 8) & 0x3ffu;
  if (!(r & 0xf000ull)) n <<= 6;
  while (n--) warmc_cpu_relax();
}

#define GCR_NONE 0u
#define GCR_NOP  1u
#define GCR_MARK_START 2u
#define GCR_MARK_END   3u
#define GCR_VERIFY     4u
#define GCR_RELOC_START 5u

static void gc_mark_start(void);
static void gc_mark_finish(void);
static void gc_reloc_start(void);
static void gc_verify(void);

#define GCR_KIND(w)           ((u32)((w) & 7u))
#define GCR_ROUND(w)          ((u32)((w) >> 3))
#define GCR_WORD(kind, round) ((u64)(kind) | ((u64)(round) << 3))

static u64 gc_poll;
static GcthGate gc_round_done;

__attribute__((aligned(GC_LINE))) static GcthGate gc_mut_wait;
static u32 gc_round_ok;
static u32 gcth_stopping, gcth_started;
static GcthGate gcth_stopped;

static u64 gc_nrounds_asked, gc_nrounds, gc_nrounds_refused, gc_nrounds_aborted;

static u64 gc_t_rounds, gc_round_max;

#define GC_ROUND_POLL_NS    1000000ull

#define GC_ROUND_BACKOFF_NS  100000ull

static GcthGate gcth_idle;

static u32 gcth_idle_seen(void) {
  return __atomic_load_n(&gcth_idle.v, __ATOMIC_ACQUIRE);
}

static u64 gc_nrests, gc_nrests_late;

static void gcth_pause_since(u32 seen, u64 ns) {
  gc_nrests++;
  gc_nrests_late += (u64)(gcth_park_timeout(&gcth_idle, seen, ns) != 0);
}

static void gcth_pause(u64 ns) { gcth_pause_since(gcth_idle_seen(), ns); }

static void gcth_nudge(void) {
  gcth_unpark(&gcth_idle, __atomic_load_n(&gcth_idle.v, __ATOMIC_RELAXED) + 1u);
}

static void gc_assert_at_safepoint(void) {

  if (!gc_from_wasm) return;
  u64 pc = warmc_gc_ctx.pc;
  if (!find_unwind(pc))
    die("a round was answered at an address no compiled function owns");
#if defined(__x86_64__)
  /* The isel emits exactly two call shapes: E8 rel32 and [REX.B] FF /2
     (call reg).  Decoding backwards on x86 is heuristic, but a return
     address that ends none of them is certainly not a call's. */
  const u8 *p = (const u8 *)(uptr)pc;
  if (!(p[-5] == 0xE8 ||
        (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) ||
        (p[-3] == 0x41 && p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0)))
    die("a round was answered at an address that is not a call's return");
#else
  u32 insn = *(const u32 *)(uptr)(pc - 4);
  if ((insn & 0xFC000000u) != 0x94000000u &&
      (insn & 0xFFFFFC1Fu) != 0xD63F0000u)
    die("a round was answered at an address that is not a call's return");
#endif
}

static u32 gc_round_active;
static u32 gc_opt_die_round;
static u32 gc_rounds_seen;
static u64 gc_claim_slot, gc_claim_from;

static void gc_service_round(void) {
  u64 w = __atomic_load_n(&gc_poll, __ATOMIC_ACQUIRE);
  if (!w) return;
  u32 n = GCR_ROUND(w), kind = GCR_KIND(w);

  gc_jitter(GCJ_ROUND);

  u64 expect = w;
  if (!__atomic_compare_exchange_n(&gc_poll, &expect, (u64)0, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
    return;

  u32 ok = 0;
  gc_round_active = n + 1;

  if (gc_may_collect()) {
    gc_assert_at_safepoint();
    if (gc_opt_die_round && g_catching &&
        ++gc_rounds_seen == gc_opt_die_round)
      die("a round handler was asked to fail");
    u64 t0 = gc_opt_stats ? sys_nanos() : 0;
    switch (kind) {
      case GCR_NOP: break;
      case GCR_MARK_START: gc_mark_start(); break;
      case GCR_MARK_END: gc_mark_finish(); break;
      case GCR_RELOC_START: gc_reloc_start(); break;
      case GCR_VERIFY: gc_verify(); break;
      default: die("the collector asked for a round this runtime does not know");
    }
    if (gc_opt_stats) {
      u64 dt = sys_nanos() - t0;
      gc_t_rounds += dt;
      if (dt > gc_round_max) gc_round_max = dt;

      gc_t_total += dt;
      gc_pause_count++;
      if (dt > gc_pause_max) gc_pause_max = dt;
      gc_pause_hist[dt < 10000 ? 0 : dt < 100000 ? 1 : dt < 1000000 ? 2
                                : dt < 10000000 ? 3 : 4]++;
    }
    ok = 1;
    gc_nrounds++;
  } else {
    gc_nrounds_refused++;
  }
  __atomic_store_n(&gc_round_ok, ok, __ATOMIC_RELAXED);

  gc_jitter(GCJ_ROUND);
  gc_round_active = 0;
  gcth_unpark(&gc_round_done, n);
}

static void gc_answer_outstanding(u64 *counter) {
  if (gc_round_active) {
    u32 n = gc_round_active - 1;
    gc_round_active = 0;
    if (counter) (*counter)++;
    __atomic_store_n(&gc_round_ok, 0u, __ATOMIC_RELAXED);
    gcth_unpark(&gc_round_done, n);
    return;
  }
  u64 w = __atomic_load_n(&gc_poll, __ATOMIC_ACQUIRE);
  if (!w) return;
  u64 expect = w;
  if (!__atomic_compare_exchange_n(&gc_poll, &expect, (u64)0, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
    return;
  if (counter) (*counter)++;
  __atomic_store_n(&gc_round_ok, 0u, __ATOMIC_RELAXED);
  gcth_unpark(&gc_round_done, GCR_ROUND(w));
}

static void gc_abort_round(void) { gc_answer_outstanding(&gc_nrounds_aborted); }

static u32 gc_opt_trap_round;

static void gc_trap_round_arm(void) {
  if (!gc_opt_trap_round) return;
  if (__atomic_load_n(&gcth_started, __ATOMIC_ACQUIRE)) return;
  if (__atomic_load_n(&gc_poll, __ATOMIC_RELAXED)) return;
  u32 n = GCR_ROUND(__atomic_load_n(&gc_poll, __ATOMIC_RELAXED)) + 1;
  u32 last = __atomic_load_n(&gc_round_done.v, __ATOMIC_RELAXED);
  if (last >= n) n = last + 1;

  gc_nrounds_asked++;
  __atomic_store_n(&gc_poll, GCR_WORD(GCR_NOP, n), __ATOMIC_RELEASE);
}

static void gc_mut_nudge(void) {
  gcth_unpark(&gc_mut_wait,
              __atomic_load_n(&gc_mut_wait.v, __ATOMIC_RELAXED) + 1u);
}

static u32 gc_round_ask(u32 kind) {
  u32 n = GCR_ROUND(__atomic_load_n(&gc_poll, __ATOMIC_RELAXED));
  u32 last = __atomic_load_n(&gc_round_done.v, __ATOMIC_RELAXED);
  if (last > n) n = last;
  n++;
  gc_nrounds_asked++;
  __atomic_store_n(&gc_poll, GCR_WORD(kind, n), __ATOMIC_RELEASE);

  gc_mut_nudge();

  gc_jitter(GCJ_ROUND);
  return n;
}

static u32 gc_round_drop(u32 kind, u32 n) {
  u64 expect = GCR_WORD(kind, n);
  return (u32)__atomic_compare_exchange_n(&gc_poll, &expect, (u64)0, 0,
                                          __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

static void gc_round_repost(u32 kind, u32 n) {
  __atomic_store_n(&gc_poll, GCR_WORD(kind, n), __ATOMIC_RELEASE);
  gc_mut_nudge();
}

static u32 gc_round(u32 kind) {
  u32 n = gc_round_ask(kind);
  for (;;) {
    if (__atomic_load_n(&gc_round_done.v, __ATOMIC_ACQUIRE) == n) break;
    if (__atomic_load_n(&gcth_stopping, __ATOMIC_ACQUIRE)) {

      gc_round_drop(kind, n);
      return 0;
    }
    gcth_park_timeout(&gc_round_done, n - 1, GC_ROUND_POLL_NS);
  }
  return __atomic_load_n(&gc_round_ok, __ATOMIC_RELAXED);
}

static u64 gc_nrounds_held, gc_nrounds_collided, gc_nmark_rests;

static u32 gc_queue_pending(void) {
  return __atomic_load_n(&gc_mqp.pub, __ATOMIC_ACQUIRE) !=
         __atomic_load_n(&gc_col.mq_tail, __ATOMIC_RELAXED);
}

#define GC_MARK_END_POLL_NS 100000ull

static void gc_mark_end_rest(u32 n) {
  gc_nmark_rests++;
  for (u32 i = __atomic_load_n(&gcth_spin, __ATOMIC_RELAXED); i; i--) {
    if (__atomic_load_n(&gc_round_done.v, __ATOMIC_ACQUIRE) == n) return;
    if (gc_queue_pending()) return;
    warmc_cpu_relax();
  }
  gcth_park_timeout(&gc_round_done, n - 1, GC_MARK_END_POLL_NS);
}

static u32 gc_round_mark_end(void) {
  u32 n = gc_round_ask(GCR_MARK_END);
  for (;;) {
    if (__atomic_load_n(&gc_round_done.v, __ATOMIC_ACQUIRE) == n) break;
    if (__atomic_load_n(&gcth_stopping, __ATOMIC_ACQUIRE)) {
      gc_round_drop(GCR_MARK_END, n);
      return 0;
    }
    if (gc_queue_pending()) {

      if (!gc_round_drop(GCR_MARK_END, n)) {
        gc_nrounds_collided++;

        gcth_park_timeout(&gc_round_done, n - 1, GC_ROUND_POLL_NS);
        continue;
      }
      gc_nrounds_held++;

      gc_col.mark_wait_took += gc_take_queue();
      while (gc_col.ngrey) {
        gc_scan_object(gc_grey[--gc_col.ngrey]);
        gc_col.mark_conc_objs++;
      }
      gc_jitter(GCJ_MARK);
      gc_round_repost(GCR_MARK_END, n);
      continue;
    }
    gc_mark_end_rest(n);
  }
  return __atomic_load_n(&gc_round_ok, __ATOMIC_RELAXED);
}

static u32 gc_opt_verify;
static void gc_verify_fail(const char *what, u64 a) {
  eputs("gc: heap check failed: ");
  eputs(what);
  eputs(" at ");
  {
    char buf[20];
    int i = 16;
    for (int k = 0; k < 16; k++) { buf[--i] = "0123456789abcdef"[a & 15]; a >>= 4; }
    sys_write(2, buf, 16);
  }
  eputs("\n");
  die("heap check");
}

static void gc_verify_ref_at(u64 v, u32 strong) {
  if (!GC_IS_OBJ(v)) return;
  if (strong && (v & gc_badmask))
    gc_verify_fail("a root wears a colour compiled code would refuse", v);
  u64 off = GC_ADDR(v) - (u64)(uptr)gc_base;
  if (off >= gc_heap_bytes) return;
  u64 healed = gc_heal_ptr(v);
  u32 g = (u32)((GC_ADDR(healed) - (u64)(uptr)gc_base) >> GC_GRAN_SHIFT);
  if (gc_pages[g].kind == GCP_FREE)
    gc_verify_fail("a reference heals to a page that has been freed", v);
  u32 h = gc_page_head(g);
  if (gc_pages[h].reloc)
    gc_verify_fail("a reference heals to a page that was relocated", v);
  if (gc_bit_test(healed)) return;

  if (gc_pages[h].alloc_cycle == (u32)gc_cycle_no &&
      GC_ADDR(healed) >= (u64)(uptr)gc_gran_addr(h) &&
      GC_ADDR(healed) < (u64)(uptr)gc_pages[h].top)
    return;
  gc_verify_fail("a reference does not heal to a live object's first word", v);
}

static void gc_verify_ref(u64 v) { gc_verify_ref_at(v, 0); }
static void gc_verify_ref_slot(u64 *slot) { gc_verify_ref_at(*slot, 1); }
static void gc_verify_ref_weak(u64 *slot) { gc_verify_ref_at(*slot, 0); }

static void gc_verify(void) {
  for (u64 g = 0; g < gc_gran_hwm; g++) {
    u32 free = (gc_granfree[g >> 6] >> (g & 63)) & 1u;
    if (free != (gc_pages[g].kind == GCP_FREE))
      gc_verify_fail("the free bitmap and the page kinds disagree",
                     (u64)(uptr)gc_gran_addr((u32)g));
  }
  u64 total = 0;
  for (u32 h = 0; h < gc_gran_hwm; h++) {
    if (gc_pages[h].kind == GCP_TAIL || gc_pages[h].kind == GCP_FREE) continue;
    if (gc_pages[h].kind == GCP_FWD) continue;
    u64 end = (u64)(uptr)gc_gran_addr(h) + (u64)gc_pages[h].ngran * GC_GRAN;
    u64 seen = 0;
    u8 *p;
    GC_PAGE_FOREACH(h, p) {
      u64 bytes = gc_object_bytes((const GcHeader *)p);
      if ((u64)(uptr)p + bytes > end)
        gc_verify_fail("a live object runs past the end of its page",
                       (u64)(uptr)p);
      seen += bytes;
      const GcHeader *hd = (const GcHeader *)p;
      const GcTypeInfo *ti = gc_typeinfo(hd->w);
      if (ti->kind == GCK_ARRAY) {
        if (ti->elem_is_ref)
          for (u64 i = 0; i < hd->len; i++)
            gc_verify_ref(*(u64 *)(p + ti->hdr_size + i * ti->body_size));
      } else {
        const u32 *refoffs = gcti_refoffs(ti);
        for (u32 i = 0; i < ti->nrefs; i++)
          gc_verify_ref(*(u64 *)(p + refoffs[i]));
      }
    }
    if (seen != gc_pages[h].live)
      gc_verify_fail("a page's live bytes are not what the walk finds",
                     (u64)(uptr)gc_gran_addr(h));
    total += seen;
  }
  if (total != gc_marked)
    gc_verify_fail("the heap's marked bytes are not what the pages hold", total);
  gc_visit_strong_roots(gc_verify_ref_slot);

  gc_visit_tables(gc_verify_ref_weak);
}

static u64 gc_reloc_object(u64 from) {
  u32 mine = 0;
  u64 i = gc_fwd_claim(from, &mine);
  if (!mine) {
    __atomic_fetch_add(&gc_reloc_lost, 1ull, __ATOMIC_RELAXED);
    return gc_fwd_await(i);
  }

  gc_jitter(GCJ_RELOC);
  u32 mut = !gcth_on_collector();
  if (mut) { gc_reloc_mut++; gc_claim_from = from; gc_claim_slot = i + 1; }
  u64 bytes = gc_object_bytes((const GcHeader *)(uptr)from);
  u32 h = gc_page_head(gc_page_of((const void *)(uptr)from));
  u8 *dst = 0;
  if (!gc_opt_reloc_stall ||
      __atomic_fetch_add(&gc_reloc_tried, 1ull, __ATOMIC_RELAXED) %
              gc_opt_reloc_stall) {
    u32 held = gc_lock();
    dst = gc_pages[h].kind == GCP_SMALL
              ? gc_bump(&gc_evac_small, GC_SMALL_GRAN, GCP_SMALL, bytes)
              : gc_bump(&gc_evac_medium, GC_MEDIUM_GRAN, GCP_MEDIUM, bytes);
    gc_unlock(held);
  }

  if (!dst) {
    __atomic_store_n(&gc_pages[h].reloc, (u8)2, __ATOMIC_RELEASE);
    __atomic_fetch_add(&gc_reloc_stalls, 1ull, __ATOMIC_RELAXED);
    __atomic_store_n(&gc_fwd[i * 2 + 1], from, __ATOMIC_RELEASE);
    if (mut) gc_claim_slot = 0;
    return from;
  }
  mem_move(dst, (const u8 *)(uptr)from, bytes);
  u32 dg = gc_page_of(dst);
  gc_bit_set(dg, (u64)(uptr)dst);
  gc_add_live(gc_page_head(dg), bytes);

  __atomic_store_n(&gc_fwd[i * 2 + 1], (u64)(uptr)dst, __ATOMIC_RELEASE);
  if (mut) gc_claim_slot = 0;
  __atomic_fetch_add(&gc_reloc_objs, 1ull, __ATOMIC_RELAXED);
  __atomic_fetch_add(&gc_reloc_objs_all, 1ull, __ATOMIC_RELAXED);
  return (u64)(uptr)dst;
}

static void gc_relocate(void) {
  u64 hwm = __atomic_load_n(&gc_gran_hwm, __ATOMIC_RELAXED);

  gc_jitter(GCJ_RELOC);
  if (gc_reloc_pages) {
    for (u32 h = 0; h < hwm; h++) {
      if (!__atomic_load_n(&gc_pages[h].reloc, __ATOMIC_ACQUIRE)) continue;
      u8 *p;
      GC_PAGE_FOREACH(h, p) gc_reloc_object((u64)(uptr)p);
    }

    for (u32 h = 0; h < hwm; h++) {
      u8 r = __atomic_load_n(&gc_pages[h].reloc, __ATOMIC_ACQUIRE);
      if (!r) continue;
      if (r == 1) {
        gc_pages[h].kind = GCP_FWD;
        gc_pages[h].live = 0;
      }
      __atomic_store_n(&gc_pages[h].reloc, (u8)0, __ATOMIC_RELEASE);
      if (r == 1) gc_release_pages(h, gc_pages[h].ngran);
    }

    gc_marked = 0;
    for (u32 h = 0; h < __atomic_load_n(&gc_gran_hwm, __ATOMIC_RELAXED); h++) {
      if (gc_pages[h].kind == GCP_TAIL || gc_pages[h].kind == GCP_FREE) continue;
      if (gc_pages[h].kind == GCP_FWD) continue;
      gc_marked += gc_pages[h].live;
    }

  }

  __atomic_store_n(&gc_phase, GCPH_IDLE, __ATOMIC_RELEASE);
  __atomic_store_n(&gc_full_done, gc_cycle_serves, __ATOMIC_RELEASE);
  gc_mut_nudge();
}

static void gc_mark_start(void) {

  gc_lab_close();
  gc_cycle_no++;
  gc_col.ngrey = 0;
  gc_mqp.head = gc_col.mq_tail = 0;
  __atomic_store_n(&gc_mqp.pub, (u64)0, __ATOMIC_RELAXED);

  for (u32 h = 0; h < gc_gran_hwm; h++) {
    if (gc_pages[h].kind == GCP_FREE) continue;
    gc_pages[h].live = 0;
    gc_pages[h].reloc = 0;
  }
  gc_mark_epoch ^= 1u;
  gc_good = gc_mark_epoch ? GC_C_M1 : GC_C_M0;
  gc_publish_colour();

  gc_cycle_serves = __atomic_load_n(&gc_full_req, __ATOMIC_ACQUIRE);
  gc_mut.alloc_since = 0;
  __atomic_store_n(&gc_phase, GCPH_MARK, __ATOMIC_RELEASE);
  gc_visit_strong_roots(gc_mark_slot);
}

static void gc_mark_finish(void) {
  __atomic_store_n(&gc_mqp.pub, gc_mqp.head, __ATOMIC_RELEASE);
  gc_mark_end_took += gc_take_queue();
  gc_drain();
  __atomic_store_n(&gc_phase, GCPH_IDLE, __ATOMIC_RELEASE);

  gc_fwd_retire();

  for (u32 h = 0; h < gc_gran_hwm; h++) {
    if (gc_pages[h].kind == GCP_TAIL || gc_pages[h].kind == GCP_FREE) continue;
    if (gc_pages[h].kind == GCP_FWD) continue;
    if (gc_pages[h].live) continue;
    if (gc_pages[h].alloc_cycle == (u32)gc_cycle_no) continue;

    if (h == gc_small_page || h == gc_medium_page) continue;
    gc_page_free(h);
  }

  gc_marked = 0;
  for (u32 h = 0; h < gc_gran_hwm; h++) {
    if (gc_pages[h].kind == GCP_TAIL || gc_pages[h].kind == GCP_FREE) continue;
    if (gc_pages[h].kind == GCP_FWD) continue;
    gc_marked += gc_pages[h].live;
  }

  gc_live = gc_marked;
  gc_trigger = gc_live * gc_opt_growth / 100;
  if (gc_trigger < gc_opt_min_kb * 1024ull) gc_trigger = gc_opt_min_kb * 1024ull;
  gc_ncycles++;
}

static void gc_prepare(void) {
  gc_fwd_unmap();

  gc_evac_small = gc_evac_medium = (u32)-1;
  gc_select_reloc();
}

static void gc_reloc_start(void) {
  gc_good = GC_C_R;
  gc_publish_colour();

  gc_lab_recolour();
  __atomic_store_n(&gc_phase, gc_reloc_pages ? (u32)GCPH_RELOC : (u32)GCPH_IDLE,
                   __ATOMIC_RELEASE);
  gc_visit_strong_roots(gc_reloc_slot);
}

static void gc_reloc_abandon(void) {
  for (u32 h = 0; h < gc_gran_hwm; h++) gc_pages[h].reloc = 0;
  gc_fwd_dead = gc_fwd;
  gc_fwd_dead_slots = gc_fwd_slots;
  gc_fwd = 0;
  gc_fwd_slots = 0;
  gc_fwd_unmap();
  gc_reloc_pages = gc_reloc_bytes = 0;
}

static void gc_collect(void) {
  if (!gc_may_collect()) return;
  gc_ncycles_stw++;
  u64 t0 = gc_opt_stats ? sys_nanos() : 0;
  gc_mark_start();
  gc_visit_tables(gc_mark_slot_shared);
  gc_drain();
  gc_mark_finish();
  gc_prepare();
  gc_reloc_start();
  gc_relocate();
  if (gc_opt_verify) gc_verify();
  if (gc_opt_stats) {
    u64 dt = sys_nanos() - t0;
    gc_t_total += dt;
    gc_pause_count++;
    if (dt > gc_pause_max) gc_pause_max = dt;
    gc_pause_hist[dt < 10000 ? 0 : dt < 100000 ? 1 : dt < 1000000 ? 2
                              : dt < 10000000 ? 3 : 4]++;
  }
}

static void gc_mark_conc(void) {
  for (;;) {
    while (gc_col.ngrey) {
      gc_scan_object(gc_grey[--gc_col.ngrey]);
      gc_col.mark_conc_objs++;
    }
    gc_jitter(GCJ_MARK);
    if (!gc_take_queue()) return;
  }
}

static void gc_cycle_body(void);

static void gc_cycle_conc(void) {

  __atomic_store_n(&gc_cycle_running, 1u, __ATOMIC_RELEASE);
  gc_cycle_body();
  __atomic_store_n(&gc_cycle_running, 0u, __ATOMIC_RELEASE);
}

static void gc_cycle_body(void) {
  if (!gc_round(GCR_MARK_START)) return;
  for (;;) {
    gc_visit_tables(gc_mark_slot_shared);
    gc_mark_conc();
    if (gc_round_mark_end()) break;
    if (__atomic_load_n(&gcth_stopping, __ATOMIC_ACQUIRE)) return;
    gcth_pause(GC_ROUND_BACKOFF_NS);
  }
  gc_prepare();
  for (;;) {
    if (gc_round(GCR_RELOC_START)) break;

    if (__atomic_load_n(&gcth_stopping, __ATOMIC_ACQUIRE)) {
      gc_reloc_abandon();
      return;
    }
    gcth_pause(GC_ROUND_BACKOFF_NS);
  }
  gc_relocate();

  if (gc_opt_verify) gc_round(GCR_VERIFY);
}

static u32 gc_cycle_wanted(void) {
  if (__atomic_load_n(&gc_full_req, __ATOMIC_ACQUIRE) !=
      __atomic_load_n(&gc_full_done, __ATOMIC_ACQUIRE))
    return 1;
  u64 trig = __atomic_load_n(&gc_trigger, __ATOMIC_RELAXED);
  return trig && __atomic_load_n(&gc_mut.alloc_since, __ATOMIC_RELAXED) >= trig;
}

#define GC_PACE_POLL_NS 10000000ull

static void gcth_collector(void) {

  __atomic_store_n(&gc_conc_on, 1u, __ATOMIC_RELEASE);
  while (!__atomic_load_n(&gcth_stopping, __ATOMIC_ACQUIRE)) {

    u32 seen = gcth_idle_seen();
    if (gc_cycle_wanted()) { gc_cycle_conc(); continue; }
    gc_jitter(GCJ_IDLE);
    gcth_pause_since(seen, GC_PACE_POLL_NS);
  }
  gcth_unpark(&gcth_stopped, 1);
}

static void gc_conc_start(void) {
  gc_conc_tried = 1;
  if (!gcth_spawn(gcth_collector)) return;
  __atomic_store_n(&gcth_started, 1u, __ATOMIC_RELEASE);
}

static void gc_request_cycle(void) {
  if (!gc_may_collect()) return;
  if (!__atomic_load_n(&gc_conc_on, __ATOMIC_ACQUIRE)) { gc_collect(); return; }
  u64 want = __atomic_add_fetch(&gc_full_req, 1ull, __ATOMIC_ACQ_REL);
  gcth_nudge();
  for (;;) {
    u32 seen = __atomic_load_n(&gc_mut_wait.v, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&gc_full_done, __ATOMIC_ACQUIRE) >= want) return;
    if (__atomic_load_n(&gc_poll, __ATOMIC_RELAXED)) { gc_service_round(); continue; }

    if (__atomic_load_n(&gcth_stopping, __ATOMIC_ACQUIRE)) return;
    gcth_park_timeout(&gc_mut_wait, seen, GC_ROUND_POLL_NS);
  }
}

static u64 gc_opt_assist = 4;
static u64 gc_nassists, gc_nassists_stw;

#define GC_ASSIST_TRIES  4u
#define GC_ASSIST_WAIT_NS 100000ull

static void gc_assist(void) {
  gc_nassists++;
  u64 start = gc_ncycles;
  for (u32 idle = 0;;) {

    u32 seen = __atomic_load_n(&gc_mut_wait.v, __ATOMIC_ACQUIRE);
    if (gc_ncycles != start) return;
    if (__atomic_load_n(&gc_poll, __ATOMIC_RELAXED)) { gc_service_round(); continue; }
    if (__atomic_load_n(&gcth_stopping, __ATOMIC_ACQUIRE)) return;

    if (__atomic_load_n(&gc_phase, __ATOMIC_ACQUIRE) == GCPH_IDLE &&
        !__atomic_load_n(&gc_cycle_running, __ATOMIC_ACQUIRE) &&
        ++idle > GC_ASSIST_TRIES) {
      gc_nassists_stw++;
      gc_collect();
      return;
    }
    gcth_park_timeout(&gc_mut_wait, seen, GC_ASSIST_WAIT_NS);
  }
}

u64 warmc_gc_load_barrier(u64 ref, u64 *slot) {
  gc_mut.barrier_slow++;
  u64 good = gc_heal_reloc(ref);
  if (slot) *slot = good;

  if (gc_phase == GCPH_MARK) gc_mark_ref(good);
  return good;
}

static u64 gc_load(u64 *slot) {
  u64 v = *slot;
  if (!(v & gc_badmask)) return v;
  return warmc_gc_load_barrier(v, slot);
}

static void gc_mark_copied(u64 *dst, u64 n) {
  if (gc_phase != GCPH_MARK) return;
  for (u64 i = 0; i < n; i++) {
    u64 v = dst[i];
    if (!(v & gc_badmask)) continue;
    if (!gc_in_heap(v)) continue;
    u64 h = gc_heal_ptr(v);
    dst[i] = h;
    gc_mark_ref(h);
  }
}

static void gc_collect_if_due(void) {
  if (!gc_may_collect() || !(gc_opt_stress || gc_mut.alloc_since >= gc_trigger))
    return;
  if (gc_opt_conc && !gc_conc_tried) gc_conc_start();

  if (__atomic_load_n(&gc_conc_on, __ATOMIC_ACQUIRE)) {

    if (__atomic_load_n(&gc_phase, __ATOMIC_ACQUIRE) == GCPH_IDLE) gcth_nudge();
    if (gc_opt_assist && gc_mut.alloc_since >= gc_opt_assist * gc_trigger)
      gc_assist();
  } else if (!__atomic_load_n(&gcth_started, __ATOMIC_ACQUIRE)) {

    gc_collect();
  } else {

    if (gc_opt_assist && gc_mut.alloc_since >= gc_opt_assist * gc_trigger)
      gc_assist();
  }
}

static void *gc_alloc(u32 type_id, const GcTypeInfo *ti, u64 payload, u64 len) {
  gc_init();

  gc_lab_sync();

  if (__atomic_load_n(&gc_poll, __ATOMIC_RELAXED)) gc_service_round();

  if (payload > gc_heap_bytes) warmc_trap(TRAP_HEAP_OOM);
  u64 total = ti->hdr_size + payload;
  if (total < GC_UNIT) total = GC_UNIT;
  total = (total + (GC_UNIT - 1)) & ~(u64)(GC_UNIT - 1);

  gc_collect_if_due();

  for (u32 round = 0; ; round++) {
    u8 *p = gc_alloc_raw(total);
    if (p) {
      gc_mut.alloc_since += total;
      gc_mut.bytes_alloc += total;
      GcHeader *h = (GcHeader *)p;
      h->w = type_id;
      h->len = (u32)len;
      p = (u8 *)(uptr)((u64)(uptr)p | gc_good);

      if (gc_lab_cell[1] - gc_lab_cell[0] < GC_LAB_MIN_TAIL) {
        gc_lab_close();
        gc_lab_refill();
      }
      return p;
    }

    if (round >= 1 || !gc_may_collect()) warmc_trap(TRAP_HEAP_OOM);
    gc_request_cycle();
  }
}

void warmc_gc_collect(void) {
  u32 saved = gc_from_wasm;
  gc_from_wasm = 0;
  gc_init();
  gc_lab_sync();
  gc_request_cycle();
  gc_from_wasm = saved;
}

static void *exn_alloc(Instance *in, u32 tag) {
  const GcTypeInfo *ti = &in->md->exntypes[tag];
  gc_no_collect++;
  void *p = gc_alloc(in->exntype_base + tag, ti, ti->body_size, 0);
  gc_no_collect--;
  return p;
}

/* exn_alloc cannot collect: the payload is still in the throw area, which no
   stack map describes.  Once it has been copied into the object the throw
   site is a safepoint like any call -- the compiler records the references
   live into its pads -- and this is where the collection the allocation put
   off happens.  Without it, a program that puts nothing else on the
   collector's heap (C++, whose every throw allocates one of these) would
   never collect at all and keep every exception it ever threw.  The frame
   walk can move what the throwing frame's callee-saved registers point at,
   so they come back through regs for the unwinder to restore. */
static void *exn_safepoint(void *e, WarmRegs *regs, u64 sp, u64 pc) {
  u32 saved = gc_from_wasm;
  gc_from_wasm = 1;
  if (!gc_may_collect() || !(gc_opt_stress || gc_mut.alloc_since >= gc_trigger ||
                             __atomic_load_n(&gc_poll, __ATOMIC_RELAXED))) {
    gc_from_wasm = saved;
    return e;
  }
  for (u32 i = 0; i < 10; i++) warmc_gc_ctx.gpr[i] = regs->gpr[i];
  warmc_gc_ctx.sp = sp;
  warmc_gc_ctx.pc = pc;
  u64 slot = (u64)(uptr)e;
  GcRoots hold;
  warmc_gc_hold(&slot, &gc_one, &hold);
  gc_lab_sync();
  if (__atomic_load_n(&gc_poll, __ATOMIC_RELAXED)) gc_service_round();
  gc_collect_if_due();
  gc_from_wasm = saved;
  warmc_gc_release(&hold);
  for (u32 i = 0; i < 10; i++) regs->gpr[i] = warmc_gc_ctx.gpr[i];
  return (void *)(uptr)gc_heal_ptr(slot);
}

void warmc_gc_hold(u64 *base, const u32 *n, void *slot) {
  GcRoots *r = (GcRoots *)slot;
  r->next = gc_rootlist;
  r->base = base;
  r->n = n;
  gc_rootlist = r;
}
void warmc_gc_release(void *slot) { gc_rootlist = ((GcRoots *)slot)->next; }

static void gc_after_trap(void) {
  gc_rootlist = 0;
  gc_from_wasm = 0;

  gc_no_collect = 0;
  if (gc_claim_slot) {
    __atomic_store_n(&gc_fwd[(gc_claim_slot - 1) * 2 + 1], gc_claim_from,
                     __ATOMIC_RELEASE);
    gc_claim_slot = 0;
  }
  gc_abort_round();
}
unsigned warmc_gc_hold_bytes(void) { return (unsigned)sizeof(GcRoots); }

static const GcTypeInfo *gc_type_of(WarmInstance *vm, u32 type_index, u32 *id) {
  Instance *in = INST_OF(vm);
  if (type_index >= in->md->ngctypes) die("gc type index out of range");
  *id = in->gctype_base + type_index;
  return &in->md->gctypes[type_index];
}

static void *rt_struct_new(WarmInstance *vm, u32 type_index) {
  u32 id;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);
  return gc_alloc(id, ti, ti->body_size, 0);
}

static void *rt_array_new(WarmInstance *vm, u32 type_index, u64 len) {
  u32 id;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);

  return gc_alloc(id, ti, len * (u64)ti->body_size, len);
}

void *warmc_struct_new_w(WarmInstance *vm, u32 type_index) {
  u32 saved = gc_from_wasm;
  gc_from_wasm = 1;
  void *r = rt_struct_new(vm, type_index);
  gc_from_wasm = saved;
  return r;
}

void *warmc_array_new_w(WarmInstance *vm, u32 type_index, u64 len) {
  u32 saved = gc_from_wasm;
  gc_from_wasm = 1;
  void *r = rt_array_new(vm, type_index, len);
  gc_from_wasm = saved;
  return r;
}

static void arr_store(u8 *at, u32 width, u64 lo, u64 hi) {
  switch (width) {
    case 1: *(u8 *)at = (u8)lo; break;
    case 2: *(u16 *)at = (u16)lo; break;
    case 4: *(u32 *)at = (u32)lo; break;
    case 16: ((u64 *)at)[0] = lo; ((u64 *)at)[1] = hi; break;
    default: *(u64 *)at = lo; break;
  }
}

static u8 *arr_data(const GcTypeInfo *ti, void *a) {
  return (u8 *)(uptr)GC_CANON((u64)(uptr)a) + ti->hdr_size;
}
static u32 arr_len(void *a) {
  return ((const GcHeader *)(uptr)GC_CANON((u64)(uptr)a))->len;
}

static void arr_range(void *a, u64 idx, u64 n) {
  if (!a) warmc_trap(TRAP_NULL_REF);
  u64 len = arr_len(a);

  if (idx + n > len) warmc_trap(TRAP_ARRAY_OOB);
}

static void *rt_array_new_fill(WarmInstance *vm, u32 type_index, u64 len,
                               u64 lo, u64 hi) {
  u32 id;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);

  GcRoots hold;
  if (ti->elem_is_ref) warmc_gc_hold(&lo, &gc_one, &hold);
  u8 *a = (u8 *)gc_alloc(id, ti, len * (u64)ti->body_size, len);
  if (ti->elem_is_ref) warmc_gc_release(&hold);

  if (lo | hi)
    for (u64 i = 0; i < len; i++)
      arr_store(arr_data(ti, a) + i * ti->body_size, ti->body_size, lo, hi);
  return a;
}

void *warmc_array_new_fill_w(WarmInstance *vm, u32 type_index, u64 len,
                             u64 lo, u64 hi) {
  u32 saved = gc_from_wasm;
  gc_from_wasm = 1;
  void *r = rt_array_new_fill(vm, type_index, len, lo, hi);
  gc_from_wasm = saved;
  return r;
}

void warmc_array_fill(WarmInstance *vm, u32 type_index, void *a, u32 idx,
                      u32 n, u64 lo, u64 hi) {
  u32 id;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);
  arr_range(a, idx, n);
  for (u32 i = 0; i < n; i++)
    arr_store(arr_data(ti, a) + (u64)(idx + i) * ti->body_size, ti->body_size, lo, hi);
}

void warmc_array_copy(WarmInstance *vm, u32 type_index, void *d, u32 di,
                      void *s, u32 si, u32 n) {
  u32 id;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);
  arr_range(d, di, n);
  arr_range(s, si, n);
  u64 w = ti->body_size;
  u8 *dp = arr_data(ti, d), *sp = arr_data(ti, s);

  if (ti->elem_is_ref) {
    u64 *dw = (u64 *)dp + di, *sw = (u64 *)sp + si;
    if (dw <= sw) for (u32 i = 0; i < n; i++) dw[i] = sw[i];
    else for (u32 i = n; i > 0; i--) dw[i - 1] = sw[i - 1];
    if (n) gc_mark_copied(dw, n);
    return;
  }

  if (dp + (u64)di * w <= sp + (u64)si * w)
    for (u64 i = 0; i < (u64)n * w; i++) dp[(u64)di * w + i] = sp[(u64)si * w + i];
  else
    for (u64 i = (u64)n * w; i > 0; i--)
      dp[(u64)di * w + i - 1] = sp[(u64)si * w + i - 1];
  if (n && ti->elem_is_ref) gc_mark_copied((u64 *)(dp + (u64)di * w), n);
}

static void arr_init_data(Instance *in, const GcTypeInfo *ti, void *a,
                          u32 seg, u32 di, u32 so, u32 n) {
  if (seg >= in->md->ndata) warmc_trap(TRAP_MEM_OOB);
  const DataSegDesc *d = &in->md->data[seg];
  u64 avail = in->data_dropped[seg] ? 0 : d->len;
  u64 w = ti->body_size;

  if ((u64)so + (u64)n * w > avail) warmc_trap(TRAP_MEM_OOB);
  const u8 *src = in->md->data_blob + d->blob_off + so;
  u8 *dst = arr_data(ti, a);
  for (u64 i = 0; i < (u64)n * w; i++) dst[(u64)di * w + i] = src[i];
}

static void arr_init_elem(Instance *in, const GcTypeInfo *ti, void *a,
                          u32 seg, u32 di, u32 so, u32 n) {
  if (seg >= in->md->nelem) warmc_trap(TRAP_TABLE_OOB);
  const ElemSegDesc *e = &in->md->elem[seg];
  u64 avail = in->elem_dropped[seg] ? 0 : e->n;
  if ((u64)so + (u64)n > avail) warmc_trap(TRAP_TABLE_OOB);

  for (u32 i = 0; i < n; i++)
    ((void **)arr_data(ti, a))[di + i] = elem_item(in, (u32)(e->first + so + i));
}

void warmc_array_init_data(WarmInstance *vm, u32 type_index, u32 seg, void *a,
                           u32 di, u32 so, u32 n) {
  u32 id;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);
  arr_range(a, di, n);
  arr_init_data(INST_OF(vm), ti, a, seg, di, so, n);
}

void warmc_array_init_elem(WarmInstance *vm, u32 type_index, u32 seg, void *a,
                           u32 di, u32 so, u32 n) {
  u32 id;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);
  arr_range(a, di, n);
  arr_init_elem(INST_OF(vm), ti, a, seg, di, so, n);
}

void *warmc_array_new_data_w(WarmInstance *vm, u32 type_index, u32 seg,
                             u32 so, u32 n) {
  u32 id, saved = gc_from_wasm;
  gc_from_wasm = 1;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);
  void *a = gc_alloc(id, ti, (u64)n * ti->body_size, n);
  arr_init_data(INST_OF(vm), ti, a, seg, 0, so, n);
  gc_from_wasm = saved;
  return a;
}

void *warmc_array_new_elem_w(WarmInstance *vm, u32 type_index, u32 seg,
                             u32 so, u32 n) {
  u32 id, saved = gc_from_wasm;
  gc_from_wasm = 1;
  const GcTypeInfo *ti = gc_type_of(vm, type_index, &id);
  void *a = gc_alloc(id, ti, (u64)n * ti->body_size, n);
  arr_init_elem(INST_OF(vm), ti, a, seg, 0, so, n);
  gc_from_wasm = saved;
  return a;
}

u32 warmc_ref_test(WarmInstance *vm, u64 v, u32 kind, u64 id, u32 depth) {
  (void)vm;
  if (!v) return (kind & RTK_NULLABLE) ? 1 : 0;
  switch (kind & ~RTK_NULLABLE) {
    case RTK_ANY: case RTK_FUNC: case RTK_EXTERN: case RTK_EXN:
      return 1;
    case RTK_NONE: case RTK_NOFUNC: case RTK_NOEXTERN: case RTK_NOEXN:
      return 0;
    case RTK_I31:
      return GC_IS_I31(v) ? 1 : 0;

    case RTK_EQ:
      return (GC_IS_I31(v) || GC_IS_OBJ(v)) ? 1 : 0;
    case RTK_STRUCT:
      return (GC_IS_OBJ(v) &&
              gc_typeinfo(((const GcHeader *)(uptr)GC_CANON(v))->w)->kind
                  == GCK_STRUCT) ? 1 : 0;
    case RTK_ARRAY:
      return (GC_IS_OBJ(v) &&
              gc_typeinfo(((const GcHeader *)(uptr)GC_CANON(v))->w)->kind
                  == GCK_ARRAY) ? 1 : 0;
    case RTK_CONCRETE_ANY: {
      if (!GC_IS_OBJ(v)) return 0;
      const u64 *rtt =
          gcti_rtt(gc_typeinfo(((const GcHeader *)(uptr)GC_CANON(v))->w));
      return (rtt[0] > depth && rtt[1 + depth] == id) ? 1 : 0;
    }
    case RTK_CONCRETE_FUNC: {

      const FuncRef *fr = (const FuncRef *)(uptr)v;
      return (fr->rtt[0] > depth && fr->rtt[1 + depth] == id) ? 1 : 0;
    }
  }
  return 0;
}

static WarmMemory *make_memory(u64 min_pages, u64 max_pages, u32 has_max,
                               u32 index64, u32 needs_guard) {
  WarmMemory *M = arena_alloc(sizeof(WarmMemory));

  if (max_pages > 65536) max_pages = 65536;
  if (min_pages > 65536) warmc_trap(TRAP_MEM_OOB);

  if (!index64) {
    if (max_pages > 65535) max_pages = 65535;
    if (min_pages > 65535) warmc_trap(TRAP_MEM_OOB);
  }
  M->has_max = has_max;
  M->index64 = index64;

  usz reserve = (usz)GUARD_RESERVE;
  u8 *base = needs_guard || !nguards
               ? (u8 *)sys_mmap(0, reserve, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS)
               : 0;
  if (base) {
    if (nguards >= MAX_GUARDS) die("too many guarded memories");
    if (!nguards) install_fault_handler();
    guards[nguards].base = base;
    guards[nguards].len = GUARD_RESERVE;
    nguards++;

    usz align = 2u << 20;
    usz adjust = (usz)(-(long)(usz)base) & (align - 1);
    base += adjust;
    reserve -= adjust;
  } else if (needs_guard) {
    die("this module needs a guarded reservation and it is unavailable");
  } else {
    reserve = (usz)max_pages * WASM_PAGE_SIZE;
    while (reserve >= (usz)min_pages * WASM_PAGE_SIZE) {
      base = (u8 *)sys_mmap(0, reserve ? reserve : 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
      if (base) break;
      if (reserve <= 4096) break;
      reserve /= 2;
    }
  }
  if (!base) die("cannot reserve linear memory");
  usz initial = (usz)min_pages * WASM_PAGE_SIZE;
  if (initial && sys_mprotect(base, initial, PROT_READ | PROT_WRITE) != 0)
    die("cannot commit initial linear memory");
  M->base = base;
  M->pages = min_pages;
  M->size = initial;
  u64 cap = reserve / WASM_PAGE_SIZE;
  M->max_pages = cap < max_pages ? cap : max_pages;
  return M;
}

static WarmTable *make_table(u64 min, u64 max, u32 has_max, u32 index64) {
  WarmTable *T = arena_alloc(sizeof(WarmTable));
  if (min > MAX_TABLE_ENTRIES) warmc_trap(TRAP_TABLE_OOB);
  u64 cap = has_max ? max : min + 1024;
  if (cap > MAX_TABLE_ENTRIES) cap = MAX_TABLE_ENTRIES;
  if (cap < min) cap = min;
  usz bytes = ((usz)cap * sizeof(TableEntry) + 4095) & ~4095UL;
  if (bytes == 0) bytes = 4096;
  T->entries = (TableEntry *)sys_mmap(0, bytes, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS);
  if (!T->entries) die("cannot allocate table");
  T->cap = cap;
  T->len = min;
  T->max_len = max;
  T->has_max = has_max;
  T->index64 = index64;

  T->dirty = 1;
  return T;
}

static int type_matches(const ExportDesc *e, const ImportDesc *im) {
  if (e->type_id == im->type_id) return 1;
  if (!(e->vt_flags & VTF_REF) || !(im->vt_flags & VTF_REF)) return 0;
  if ((e->vt_flags >> VTF_HIER_SHIFT) != (im->vt_flags >> VTF_HIER_SHIFT)) return 0;

  if ((e->vt_flags & VTF_NULL) && !(im->vt_flags & VTF_NULL)) return 0;
  if (e->vt_flags & VTF_BOTTOM) return 1;
  if (im->vt_flags & VTF_TOP) return 1;
  if (!e->supers) return 0;
  for (u64 i = 0; i < e->supers[0]; i++)
    if (e->supers[1 + i] == im->heap_id) return 1;
  return 0;
}

__attribute__((noreturn))
static void link_error(const char *what, const char *mod, const char *name) {
  if (g_catching) { g_catching = 0; g_trap_code = TRAP_LINK; warmc_longjmp(g_catch_buf, 1); }
  eputs("warmc-rt: link error: ");
  eputs(what);
  eputs(" \"");
  eputs(mod);
  eputs("\".\"");
  eputs(name);
  eputs("\"\n");
  sys_exit(4);
}

static void set_default_nan_mode(void) {
#if defined(__aarch64__)
  u64 fpcr;
  __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
  fpcr |= 1ull << 25;
  __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}

WarmInstance *warmc_instantiate(const ModuleDesc *md) {
  return warmc_instantiate_bound(md, 0);
}

WarmInstance *warmc_instantiate_bound(const ModuleDesc *md,
                                      const WarmBinding *binds) {
  if (ninstances >= MAX_INSTANCES) die("too many instances");
  if (md->det_profile) set_default_nan_mode();

  u64 tail = (u64)INSTANCE_GLOBAL_SIZE * md->nglobals + 16ull * md->nimported_funcs +
             (u64)sizeof(FuncRef) * md->nfuncrefs +
             (u64)sizeof(TableSlot) * md->ntables +
             (u64)sizeof(MemSlot) * md->nmemories;
  Instance *in = arena_alloc(sizeof(Instance) + tail);
  in->md = md;

  in->gctype_base = gc_register_types(md);
  in->exntype_base = in->gctype_base + md->ngctypes;
  in->data_dropped = arena_alloc(md->ndata ? md->ndata : 1);
  in->elem_dropped = arena_alloc(md->nelem ? md->nelem : 1);
  in->tabs = md->ntables ? arena_alloc(sizeof(WarmTable *) * md->ntables) : 0;
  in->mems = md->nmemories ? arena_alloc(sizeof(WarmMemory *) * md->nmemories) : 0;
  in->tags = md->ntags ? arena_alloc(sizeof(void *) * md->ntags) : 0;

  instances[ninstances] = in;
  __atomic_store_n(&ninstances, ninstances + 1, __ATOMIC_RELEASE);

  register_desc(md);

  WarmInstance *v = &in->vm;
  v->stack_limit = g_stack_limit;

  v->gc_badmask = gc_badmask;

  v->gc_lab = (u64)(uptr)gc_lab_cell;
  v->gc_hdr_base = (u64)in->gctype_base;
  u64 *imports = inst_imports(in);
  FuncRef *funcrefs = inst_funcrefs(in);

  u32 fi = 0, gi = 0, ti = 0, mi = 0, gti = 0;
  for (u32 i = 0; i < md->nimports; i++) {
    const ImportDesc *im = &md->imports[i];

    if (binds && binds[i].code) {
      if (im->kind != 0) link_error("host binding is not a function",
                                    im->module, im->name);
      imports[2 * fi] = (u64)(uptr)binds[i].code;
      imports[2 * fi + 1] = (u64)(uptr)binds[i].inst;
      funcrefs[fi].code = binds[i].code;
      funcrefs[fi].instance = binds[i].inst;
      funcrefs[fi].type_id = im->type_id;
      funcrefs[fi].rtt = 0;
      fi++;
      continue;
    }

    WarmInstance *src = binds ? binds[i].inst
                              : lookup_module(im->module, im->module_len);
    if (!src) link_error("unknown module", im->module, im->name);

    const ExportDesc *e = binds
        ? warmc_find_export(src, binds[i].name)
        : warmc_find_export_n(src, im->name, im->name_len);
    if (!e) link_error("unknown import", im->module, im->name);
    if (e->kind != im->kind) link_error("incompatible import type", im->module, im->name);
    Instance *from = INST_OF(src);
    switch (im->kind) {
      case 0: {
        if (!type_matches(e, im))
          link_error("incompatible import type", im->module, im->name);

        FuncRef fr = inst_funcrefs(from)[e->index];
        imports[2 * fi] = (u64)(uptr)fr.code;
        imports[2 * fi + 1] = (u64)(uptr)fr.instance;
        funcrefs[fi] = fr;
        fi++;
        break;
      }
      case 1: {
        WarmTable *T = from->tabs ? from->tabs[e->index] : 0;

        if (!T || e->type_id != im->type_id ||
            !limits_match(T->len, T->max_len, T->has_max, T->index64,
                          im->min, im->max, im->has_max, im->index64))
          link_error("incompatible import type", im->module, im->name);
        __atomic_store_n(&in->tabs[ti++], T, __ATOMIC_RELEASE);
        break;
      }
      case 2: {

        WarmMemory *M = from->mems ? from->mems[e->index] : 0;
        if (!M || !limits_match(M->pages, M->max_pages, M->has_max, M->index64,
                                im->min, im->max, im->has_max, im->index64))
          link_error("incompatible import type", im->module, im->name);
        in->mems[mi++] = M;
        break;
      }
      case 3:

        if (e->mutable_ != im->mutable_ ||
            (im->mutable_ ? e->type_id != im->type_id : !type_matches(e, im)))
          link_error("incompatible import type", im->module, im->name);
        inst_global(in, gi)[0] = (u64)(uptr)warmc_global_cell(src, e->index);
        gi++;
        break;
      case 4:

        if (e->type_id != im->type_id || !from->tags)
          link_error("incompatible import type", im->module, im->name);
        in->tags[gti++] = from->tags[e->index];
        break;
      default: link_error("unknown import kind", im->module, im->name);
    }
  }

  for (u32 i = 0; i < md->nmemories; i++)
    if (!md->memories[i].imported)
      in->mems[i] = make_memory(md->memories[i].min,
                                md->memories[i].has_max
                                  ? md->memories[i].max : 65536,
                                md->memories[i].has_max,
                                md->memories[i].index64,
                                md->needs_guard);

  for (u32 i = 0; i < md->ntables; i++)
    if (!md->tables[i].imported)
      __atomic_store_n(&in->tabs[i],
                       make_table(md->tables[i].min, md->tables[i].max,
                                  md->tables[i].has_max, md->tables[i].index64),
                       __ATOMIC_RELEASE);

  for (u32 i = 0; i < md->ntags; i++)
    if (!md->tags[i].imported) in->tags[i] = arena_alloc(8);

  for (u32 i = 0; i < md->nmemories; i++)
    if (in->mems[i]) republish_memory(in->mems[i]);
  for (u32 i = 0; i < md->ntables; i++)
    if (in->tabs[i]) republish_table(in->tabs[i]);

  for (u32 i = md->nimported_funcs; i < md->nfuncrefs; i++) {
    funcrefs[i].code = md->funcrefs[i].code;
    funcrefs[i].instance = v;
    funcrefs[i].type_id = md->funcrefs[i].type_id;
    funcrefs[i].rtt = md->funcrefs[i].rtt;
  }

  for (u32 i = 0; i < md->nglobals; i++)
    if (!(md->globals[i].flags & (GLOBAL_IMPORTED | GLOBAL_NOINIT)))
      const_eval(in, md->globals[i].init, inst_global(in, i));

  for (u32 i = 0; i < md->ntables; i++) {
    if (md->tables[i].imported || !md->tables[i].has_init) continue;
    u64 v2[2];
    const_eval(in, md->tables[i].init, v2);
    WarmTable *T = in->tabs[i];
    T->dirty = 1;
    for (u64 k = 0; k < T->len; k++)
      T->entries[k] = (void *)(uptr)v2[0];
  }

  {
    u64 total = 0;
    for (u32 i = 0; i < md->nelem; i++) {
      u64 end = md->elem[i].first + md->elem[i].n;
      if (end > total) total = end;
    }
    in->nelem_values = total;
    in->elem_values = total ? arena_alloc(total * sizeof(void *)) : 0;
    for (u64 k = 0; k < total; k++) {
      u64 v2[2];
      const_eval(in, md->elem_entries[k], v2);
      in->elem_values[k] = (void *)(uptr)v2[0];
    }
  }

  for (u32 i = 0; i < md->nelem; i++) {
    const ElemSegDesc *e = &md->elem[i];
    if (e->mode == 2) { in->elem_dropped[i] = 1; continue; }
    if (e->mode != 0) continue;
    u64 v2[2];
    const_eval(in, e->offset_init, v2);
    u64 off = v2[0];
    WarmTable *T = e->table_index < md->ntables ? in->tabs[e->table_index] : 0;

    if (!T || off > T->len || e->n > T->len - off) warmc_trap(TRAP_TABLE_OOB);
    T->dirty = 1;
    for (u64 k = 0; k < e->n; k++)
      T->entries[off + k] = elem_item(in, (u32)e->first + (u32)k);
    in->elem_dropped[i] = 1;
  }

  for (u32 i = 0; i < md->ndata; i++) {
    const DataSegDesc *d = &md->data[i];
    if (d->info & DSEG_PASSIVE) continue;
    u64 off;
    if (d->info & DSEG_OFF_CONST) {
      off = d->offset_init;
    } else {
      u64 v2[2];
      const_eval(in, d->offset_init, v2);
      off = v2[0];
    }
    u32 mem = DSEG_MEM_INDEX(d->info);
    WarmMemory *M = mem < md->nmemories ? in->mems[mem] : 0;
    if (!M || off > M->size || d->len > M->size - off) warmc_trap(TRAP_MEM_OOB);
    // The blob is in the image, never in a linear memory, so the two cannot
    // overlap.  A byte loop here was 2% of stdgo/regex, whose 1 MB of data
    // comes in 30000 segments.
    mem_move(M->base + off, md->data_blob + d->blob_off, d->len);
    in->data_dropped[i] = 1;
  }

  if (md->has_start) {
    u64 zero[WARMC_FARGS_WORDS] = { 0 }, ret[WARMC_RETBUF_WORDS] = { 0 };
    FuncRef fr = funcrefs[md->start_func];
    warmc_invoke(fr.code, (WarmInstance *)fr.instance, zero, zero, ret);
  }
  return v;
}

static int streq(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

static u64 parse_u64(const char *s, int *neg_out) {
  int neg = 0;
  if (*s == '-') { neg = 1; s++; }
  else if (*s == '+') s++;
  u64 v = 0;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    while (*s) {
      char ch = *s++;
      u64 d;
      if (ch >= '0' && ch <= '9') d = (u64)(ch - '0');
      else if (ch >= 'a' && ch <= 'f') d = (u64)(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F') d = (u64)(ch - 'A' + 10);
      else break;
      v = v * 16 + d;
    }
  } else {
    while (*s >= '0' && *s <= '9') v = v * 10 + (u64)(*s++ - '0');
  }
  if (neg_out) *neg_out = neg;
  return neg ? (u64)(-(i64)v) : v;
}

static void print_value(u8 type, u64 bits, u64 hi) {
  switch (type) {
    case VT_I32: puts_("i32:"); put_i64((i64)(i32)(u32)bits); break;
    case VT_I64: puts_("i64:"); put_i64((i64)bits); break;
    case VT_F32: puts_("f32:"); put_hex(bits & 0xffffffffu, 8); break;
    case VT_F64: puts_("f64:"); put_hex(bits, 16); break;
    case VT_V128: puts_("v128:0x"); put_hex_raw(hi, 16); put_hex_raw(bits, 16); break;
    default: puts_("ref:"); put_hex(bits, 16); break;
  }
}

static const ModuleDesc *g_desc;

static const ExportDesc *find_export(const char *name) {
  u32 n = name_len(name);
  for (u32 i = 0; i < g_desc->nexport; i++)
    if (g_desc->exports[i].kind == 0 &&
        g_desc->exports[i].name_len == n && streq(g_desc->exports[i].name, name))
      return &g_desc->exports[i];
  return 0;
}

static void list_exports(void) {
  puts_("exports:\n");
  for (u32 i = 0; i < g_desc->nexport; i++) {
    const ExportDesc *e = &g_desc->exports[i];
    if (e->kind != 0) continue;
    puts_("  ");
    puts_(e->name);
    puts_(" (");
    for (u32 j = 0; j < e->nparams; j++) {
      if (j) puts_(", ");
      switch (e->ptypes[j]) {
        case VT_I32: puts_("i32"); break;
        case VT_I64: puts_("i64"); break;
        case VT_F32: puts_("f32"); break;
        case VT_F64: puts_("f64"); break;
        case VT_V128: puts_("v128"); break;
        default: puts_("ref"); break;
      }
    }
    puts_(") -> ");
    if (!e->nresults) puts_("()");
    for (u32 j = 0; j < e->nresults; j++) {
      if (j) puts_(", ");
      switch (e->rtypes[j]) {
        case VT_I32: puts_("i32"); break;
        case VT_I64: puts_("i64"); break;
        case VT_F32: puts_("f32"); break;
        case VT_F64: puts_("f64"); break;
        case VT_V128: puts_("v128"); break;
        default: puts_("ref"); break;
      }
    }
    puts_("\n");
  }
}

#define BENCH_MAX 512
#define BENCH_TARGET_NS 3000000ull
#define BENCH_WARM_NS 100000000ull
#define BENCH_CAL_NS 2000000ull

#define BENCH_BUDGET_NS 40000000ull
#define BENCH_MIN_REPS 5
static u64 bench_ns[BENCH_MAX];

static void bench_report(const char *name, int n) {
  for (int i = 1; i < n; i++) {
    u64 v = bench_ns[i];
    int j = i - 1;
    while (j >= 0 && bench_ns[j] > v) { bench_ns[j + 1] = bench_ns[j]; j--; }
    bench_ns[j + 1] = v;
  }
  u64 sum = 0;
  for (int i = 0; i < n; i++) sum += bench_ns[i];
  eputs("bench: ");      eputs(name);
  eputs(" n=");          put_u64_e((u64)n);
  eputs(" min_ns=");     put_u64_e(bench_ns[0]);
  eputs(" med_ns=");     put_u64_e(bench_ns[n / 2]);
  eputs(" max_ns=");     put_u64_e(bench_ns[n - 1]);
  eputs(" mean_ns=");    put_u64_e(sum / (u64)n);
  eputs("\n");
}

static u64 per_call(u64 total, u64 k) { return (total + k / 2) / k; }

static int g_catch_traps;

static int call_export(const ExportDesc *e, const char *name,
                       int argc, char **argv, int reps, int cluster_hot) {
  u64 iargs[8] = { 0 };
  u64 fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  int nint = 0, nflt = 0;

  if ((u32)argc < e->nparams) { eputs("warmc-rt: not enough arguments\n"); return 2; }
  for (u32 i = 0; i < e->nparams; i++) {
    u64 bits = parse_u64(argv[i], 0);
    switch (e->ptypes[i]) {

      case VT_V128:
        eputs("warmc-rt: a v128 argument cannot be given on the command line\n");
        return 2;
      case VT_F32: case VT_F64:
        if (nflt >= 8) { eputs("warmc-rt: too many float arguments\n"); return 2; }
        fargs[2 * nflt++] = bits;
        break;
      default:
        if (nint >= 7) { eputs("warmc-rt: too many integer arguments\n"); return 2; }
        iargs[nint++] = bits;
        break;
    }
  }

  FuncRef fr = warmc_export_func(vm, e);

#if defined(__x86_64__)
  /* Parameters past the register ceilings (3 ints after vmctx, 4 floats)
     ride the callee's incoming stack slots; warmc_invoke_s copies this
     image into them.  The layout is the callee's: eight bytes per scalar in
     parameter order, and a v128 in an aligned sixteen. */
#define RT_STK_WORDS 24
  u64 stk[RT_STK_WORDS];
  u64 stk_bytes = 0;
  {
    u32 ni2 = 0, nf2 = 0, w = 0;
    for (u32 i = 0; i < e->nparams; i++) {
      switch (e->ptypes[i]) {
        case VT_V128:
          if (nf2 >= 4 && w + 2 < RT_STK_WORDS) {
            if (w & 1) w++;
            stk[w++] = fargs[2 * nf2];
            stk[w++] = fargs[2 * nf2 + 1];
          }
          nf2++;
          break;
        case VT_F32: case VT_F64:
          if (nf2 >= 4 && w < RT_STK_WORDS) stk[w++] = fargs[2 * nf2];
          nf2++;
          break;
        default:
          if (ni2 >= 3 && w < RT_STK_WORDS) stk[w++] = iargs[ni2];
          ni2++;
          break;
      }
    }
    stk_bytes = 8ull * w;
  }
#define RT_INVOKE(ia, fa, rr) \
  warmc_invoke_s(fr.code, (WarmInstance *)fr.instance, ia, fa, rr, stk, stk_bytes)
#define RT_INVOKE_CATCHING(ia, fa, rr) \
  warmc_invoke_catching_s(fr.code, (WarmInstance *)fr.instance, ia, fa, rr, \
                          stk, stk_bytes)
#else
#define RT_INVOKE(ia, fa, rr) \
  warmc_invoke(fr.code, (WarmInstance *)fr.instance, ia, fa, rr)
#define RT_INVOKE_CATCHING(ia, fa, rr) \
  warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance, ia, fa, rr)
#endif

  if (reps > 0) {
    if (reps > BENCH_MAX) reps = BENCH_MAX;
    sys_prefer_fast_core();

    u64 target = cluster_hot ? BENCH_CAL_NS : BENCH_WARM_NS;
    u64 warm = sys_nanos();
    u64 nwarm = 0;
    do {
      RT_INVOKE(iargs, fargs, ret);
      nwarm++;
    } while (nwarm < 3 || sys_nanos() - warm < target);

    u64 one = (sys_nanos() - warm) / nwarm;
    u64 batch = one ? (BENCH_TARGET_NS + one - 1) / one : 1;
    if (batch < 1) batch = 1;

    int n = 0;
    u64 spent = 0;
    while (n < reps) {
      u64 t0 = sys_nanos();
      for (u64 k = 0; k < batch; k++) RT_INVOKE(iargs, fargs, ret);
      u64 took = sys_nanos() - t0;
      bench_ns[n++] = per_call(took, batch);
      spent += took;
      if (n >= BENCH_MIN_REPS && spent > BENCH_BUDGET_NS) break;
    }
    bench_report(name, n);
  } else if (g_catch_traps) {

    u32 t = RT_INVOKE_CATCHING(iargs, fargs, ret);
    if (t) {
      eputs("trap: "); eputs(warmc_trap_name(t)); eputs("\n");
      return 1;
    }
  } else {
    RT_INVOKE(iargs, fargs, ret);
  }
#undef RT_INVOKE
#undef RT_INVOKE_CATCHING

  if (e->nresults == 1) {
    u8 t = e->rtypes[0];
    int in_vreg = (t == VT_F32 || t == VT_F64 || t == VT_V128);
    u64 bits = in_vreg ? ret[WARMC_RET_V0] : ret[WARMC_RET_X0];
    u64 hi = ret[WARMC_RET_V0 + 1];
    if (t == VT_F32) bits &= 0xffffffffu;
    if (t == VT_I32) bits &= 0xffffffffu;
    print_value(t, bits, hi);
    puts_("\n");
  } else if (e->nresults > 1) {
    for (u32 i = 0; i < e->nresults && i < WARMC_RET_MAX_RESULTS; i++) {
      if (i) puts_(" ");
      u64 bits = ret[WARMC_RET_MULTI(i)];
      if (e->rtypes[i] == VT_I32 || e->rtypes[i] == VT_F32) bits &= 0xffffffffu;
      print_value(e->rtypes[i], bits, ret[WARMC_RET_MULTI(i) + 1]);
    }
    puts_("\n");
  } else {

    puts_("\n");
  }
  return 0;
}

extern int (*warmc_driver_hook)(int argc, char **argv);

char **warmc_rt_environ(void) { return g_environ; }

static const char *rt_getenv(const char *name) {
  if (!g_environ) return 0;
  for (char **e = g_environ; *e; e++) {
    const char *p = *e, *q = name;
    while (*q && *p == *q) { p++; q++; }
    if (!*q && *p == '=') return p + 1;
  }
  return 0;
}

static u64 rt_getenv_u64(const char *name, u64 dflt) {
  const char *v = rt_getenv(name);
  if (!v || !*v) return dflt;
  u64 r = 0;
  for (; *v >= '0' && *v <= '9'; v++) r = r * 10 + (u64)(*v - '0');
  return r;
}

static GcthGate gcth_req;
static GcthGate gcth_ack;
static u32 gcth_test_stop;
static u64 gcth_send_ns;
static u64 gcth_lat_min, gcth_lat_sum, gcth_lat_n;
static u64 gcth_count;
static u32 gcth_test_die, gcth_test_fault;

#define GCTH_TEST_DEADLINE 5000000000ull

static void gcth_pingpong(void) {
  u32 seen = 0;
  for (;;) {
    while (__atomic_load_n(&gcth_req.v, __ATOMIC_ACQUIRE) == seen)
      gcth_park(&gcth_req, seen);
    seen = __atomic_load_n(&gcth_req.v, __ATOMIC_ACQUIRE);
    if (gcth_test_stop) break;

    u64 dt = sys_nanos() - gcth_send_ns;
    gcth_lat_sum += dt;
    gcth_lat_n++;
    if (dt < gcth_lat_min) gcth_lat_min = dt;
    gcth_count++;
    gcth_unpark(&gcth_ack, seen);
  }

  if (gcth_test_fault) *(volatile u32 *)0 = 0;
  if (gcth_test_die)
    die("the collector's thread reached a state it believes impossible");
  gcth_unpark(&gcth_ack, seen);
}

static u32 gcth_nreq;

static void gcth_rally(const char *what, u64 n) {
  gcth_lat_min = ~0ull; gcth_lat_sum = 0; gcth_lat_n = 0; gcth_nwaits = 0;
  u64 t0 = sys_nanos();
  for (u64 i = 0; i < n; i++) {
    u32 want = gcth_nreq + 1;
    gcth_send_ns = sys_nanos();
    gcth_unpark(&gcth_req, want);
    while (__atomic_load_n(&gcth_ack.v, __ATOMIC_ACQUIRE) != want)
      if (gcth_park_timeout(&gcth_ack, gcth_nreq, GCTH_TEST_DEADLINE))
        die("the collector's thread did not answer a handshake");
    gcth_nreq = want;
  }
  u64 t1 = sys_nanos();
  eputs("gcth: "); put_u64_e(n); eputs(" round trips "); eputs(what);
  eputs(": "); put_u64_e((t1 - t0) / n); eputs(" ns each, wake ");
  put_u64_e(gcth_lat_min); eputs("/");
  put_u64_e(gcth_lat_sum / (gcth_lat_n ? gcth_lat_n : 1));
  eputs(" ns min/mean, "); put_u64_e(gcth_nwaits); eputs(" into the kernel\n");
}

static u64 gcth_round_period_ns;

static void gcth_round_driver(void) {
  while (!__atomic_load_n(&gcth_stopping, __ATOMIC_ACQUIRE)) {
    if (!gc_round(GCR_NOP)) gcth_pause(GC_ROUND_BACKOFF_NS);
    else if (gcth_round_period_ns) gcth_pause(gcth_round_period_ns);
  }
  gcth_unpark(&gcth_stopped, 1);
}

static u64 gcth_stop_late;

static void gcth_stop(void) {
  if (!__atomic_load_n(&gcth_started, __ATOMIC_ACQUIRE)) return;
  __atomic_store_n(&gcth_stopping, 1u, __ATOMIC_SEQ_CST);

  gc_answer_outstanding(0);
  gcth_nudge();

  u64 deadline = sys_nanos() + 50000000ull;
  while (!__atomic_load_n(&gcth_stopped.v, __ATOMIC_ACQUIRE)) {
    if (sys_nanos() >= deadline) { gcth_stop_late++; return; }
    gcth_park_timeout(&gcth_stopped, 0, GC_ROUND_POLL_NS);
  }
}

static void gcth_start_rounds(u64 period_us) {
  gcth_round_period_ns = period_us * 1000ull;
  if (!gcth_spawn(gcth_round_driver))
    die("could not start the collector's thread");
  __atomic_store_n(&gcth_started, 1u, __ATOMIC_RELEASE);
}

__attribute__((noreturn)) static void gcth_selftest(u64 n) {
  gcth_test_die = (u32)rt_getenv_u64("WARMC_GC_THREADTEST_DIE", 0);
  gcth_test_fault = (u32)rt_getenv_u64("WARMC_GC_THREADTEST_FAULT", 0);

  if (gcth_test_fault) install_fault_handler();
  if (!gcth_spawn(gcth_pingpong)) die("could not start the collector's thread");

  gcth_rally("spinning", n);

  {
    static GcthGate idle;
    u64 t0 = sys_nanos();
    for (u32 i = 0; i < 1000; i++) gcth_spun_out(&idle, 0);
    u64 t1 = sys_nanos();
    eputs("gcth: a full spin of "); put_u64_e(gcth_spin);
    eputs(" is "); put_u64_e((t1 - t0) / 1000); eputs(" ns\n");
  }

  __atomic_store_n(&gcth_spin, 0, __ATOMIC_RELAXED);
  u64 m = n / 10;
  if (!m) m = 1;
  gcth_rally("parked", m);

  eputs("gcth: counter "); put_u64_e(gcth_count); eputs(" of ");
  put_u64_e(n + m); eputs("\n");
  if (gcth_count != n + m) die("the ping-pong lost a handshake");

  if (gcth_test_die) {

    if (warmc_setjmp(g_catch_buf)) {
      eputs("gcth: the collector's die longjmped into the mutator\n");
      sys_exit(3);
    }
    g_catching = 1;
  }

  gcth_test_stop = 1;
  gcth_unpark(&gcth_req, gcth_nreq + 1);
  while (__atomic_load_n(&gcth_ack.v, __ATOMIC_ACQUIRE) != gcth_nreq + 1)
    if (gcth_park_timeout(&gcth_ack, gcth_nreq, GCTH_TEST_DEADLINE))
      die("the collector's thread did not stop");
  if (gcth_test_die) {
    eputs("gcth: the collector died and the process did not\n");
    sys_exit(3);
  }

  {
    static GcthGate never;
    if (!gcth_park_timeout(&never, 0, 50000000ull))
      die("a gate nobody ever wrote came back before its deadline");
  }
  eputs("gcth: ok\n");
  sys_exit(0);
}

static void gc_read_options(void) {

  if (rt_getenv("WARMC_GC"))
    die("WARMC_GC names a collector that no longer exists; there is one");
  gc_opt_stress   = (u32)rt_getenv_u64("WARMC_GC_STRESS", 0);
  gc_opt_verify   = (u32)rt_getenv_u64("WARMC_GC_VERIFY", 0);
  gc_opt_stats    = (u32)rt_getenv_u64("WARMC_GC_STATS", 0);
  gc_opt_growth   = (u32)rt_getenv_u64("WARMC_GC_GROWTH", gc_opt_growth);
  gc_opt_min_kb   = rt_getenv_u64("WARMC_GC_MIN_KB", gc_opt_min_kb);
  gc_opt_lab      = (u32)rt_getenv_u64("WARMC_GC_LAB", 1);
  gc_opt_release  = rt_getenv_u64("WARMC_GC_RELEASE", 0);

  gc_opt_reloc    = (u32)rt_getenv_u64("WARMC_GC_RELOC", gc_opt_reloc);

  if (rt_getenv_u64("WARMC_GC_RELOCATE_ALL", 0)) gc_opt_reloc = 8;
  gc_opt_reloc_stall = (u32)rt_getenv_u64("WARMC_GC_RELOC_STALL", 0);
  { u64 kb = rt_getenv_u64("WARMC_GC_HEAP_KB", 0);
    if (kb) gc_heap_bytes = kb * 1024ull; }
  gcth_spin = (u32)rt_getenv_u64("WARMC_GC_THREAD_SPIN", GCTH_SPINS);
  gc_opt_trap_round = (u32)rt_getenv_u64("WARMC_GC_TRAP_ROUND", 0);
  gc_opt_die_round = (u32)rt_getenv_u64("WARMC_GC_DIE_ROUND", 0);
  { u64 j = rt_getenv_u64("WARMC_GC_JITTER", 0);
    gc_opt_jitter = (u32)(j > 256 ? 256 : j); }

  gc_opt_conc     = (u32)rt_getenv_u64("WARMC_GC_CONC", 1);
  gc_opt_assist   = rt_getenv_u64("WARMC_GC_ASSIST", gc_opt_assist);
  if (gc_opt_stress) gc_opt_conc = 0;
  if (rt_getenv("WARMC_GC_ROUNDS")) gc_opt_conc = 0;
  gc_opt_jitter_seed = rt_getenv_u64("WARMC_GC_JITTER_SEED", 1);

  gc_jitter_state[0] = gc_jitter_mix(gc_opt_jitter_seed);
  gc_jitter_state[1] = gc_jitter_mix(gc_opt_jitter_seed + 0x5bf03635ull);
  if (gc_opt_stress) gc_opt_lab = 0;
}

static void gc_report(void) {
  if (!gc_opt_stats) return;
  eputs("gc: ");
  put_u64_e(gc_ncycles);
  eputs(" cycles, ");
  put_u64_e(gc_live);
  eputs(" bytes live, ");
  put_u64_e(gc_mut.bytes_alloc);
  eputs(" bytes allocated, ");
  put_u64_e(gc_gran_used * GC_GRAN);
  eputs(" bytes of pages (peak ");
  put_u64_e(gc_gran_peak * GC_GRAN);
  eputs("), ");
  put_u64_e(gc_npages_opened);
  eputs(" pages opened, ");
  put_u64_e(gc_npages_freed);
  eputs(" freed, ");
  put_u64_e(gc_nlabs);
  eputs(" regions, ");
  put_u64_e(gc_mut.fast_bytes);
  eputs(" bytes fast-allocated, ");

  put_u64_e(__atomic_load_n(&gc_trigger, __ATOMIC_RELAXED));
  eputs(" byte trigger\n");
  eputs("  relocated: ");
  put_u64_e(gc_reloc_pages);
  eputs(" pages, ");
  put_u64_e(gc_reloc_objs);
  eputs(" objects, ");
  put_u64_e(gc_reloc_bytes);
  eputs(" bytes; released ");
  put_u64_e(gc_released);
  eputs(" bytes in ");
  put_u64_e(gc_nreleases);
  eputs(" calls, ");
  put_u64_e(gc_reloc_stalls);
  eputs(" left for want of room\n");

  eputs("  relocation: ");
  put_u64_e(gc_reloc_pages_all);
  eputs(" pages and ");
  put_u64_e(gc_reloc_objs_all);
  eputs(" objects over the run, ");
  put_u64_e(gc_reloc_mut);
  eputs(" moved by the program, ");
  put_u64_e(gc_reloc_lost);
  eputs(" claims lost\n");

  eputs("  barrier: ");
  put_u64_e(gc_mut.barrier_slow);
  eputs(" loads healed\n");

  eputs("  mark: ");
  put_u64_e(gc_col.mark_conc_objs);
  eputs(" objects scanned beside the program, ");
  put_u64_e(gc_mark_end_took);
  eputs(" taken at mark end, ");
  put_u64_e(gc_col.mark_wait_took);
  eputs(" taken waiting for it, ");
  put_u64_e(gc_born_skipped);
  eputs(" still being born, ");
  put_u64_e(gc_nassists);
  eputs(" assists (");
  put_u64_e(gc_nassists_stw);
  eputs(" that gave up waiting), ");
  put_u64_e(gc_ncycles_stw);
  eputs(" cycles run by the mutator, ");
  eputs(gc_conc_on ? "on a thread\n" : "stop-the-world\n");
  eputs("  rounds: ");
  put_u64_e(gc_nrounds_asked);
  eputs(" asked, ");
  put_u64_e(gc_nrounds);
  eputs(" answered, ");
  put_u64_e(gc_nrounds_refused);
  eputs(" refused, ");
  put_u64_e(gc_nrounds_aborted);
  eputs(" lost to a trap, ");

  put_u64_e(gcth_stop_late);
  eputs(" exits the collector outlasted\n");

  eputs("  round time: ");
  put_u64_e(gc_t_rounds);
  eputs(" ns answering, ");
  put_u64_e(gc_round_max);
  eputs(" ns longest; the collector rested ");
  put_u64_e(gc_nrests);
  eputs(" times, ");
  put_u64_e(gc_nrests_late);
  eputs(" of them unwoken until the timeout\n");

  eputs("  mark end: ");
  put_u64_e(gc_nrounds_held);
  eputs(" held back to mark, ");
  put_u64_e(gc_nrounds_collided);
  eputs(" already taken, ");
  put_u64_e(gc_nmark_rests);
  eputs(" rests\n");
  eputs("  pauses: ");
  put_u64_e(gc_pause_count);
  eputs(", ");
  put_u64_e(gc_pause_max);
  eputs(" ns longest, ");
  put_u64_e(gc_t_total);
  eputs(" ns in the collector\n");
  if (gc_opt_jitter) {

    eputs("  jitter:");
    for (u32 w = 0; w < 2; w++)
      for (u32 i = 0; i < GCJ_N; i++) {
        eputs(" ");
        eputs(gc_jitter_name[i]);
        eputs(w ? "/collector " : "/mutator ");
        put_u64_e(gc_jitter_hits[w][i]);
      }
    eputs("\n");
  }
  eputs("  pauses under 10us/100us/1ms/10ms/over: ");
  for (u32 i = 0; i < 5; i++) {
    if (i) eputs("/");
    put_u64_e(gc_pause_hist[i]);
  }
  eputs("\n");
}

static int g_reported;

void warmc_rt_report(WarmInstance *v) {
  report_icount(v);
  gc_report();
}

void warmc_report_run(WarmInstance *v) {
  if (g_reported) return;
  g_reported = 1;

  gcth_stop();

  if (!v) return;
  const ModuleDesc *md = INST_OF(v)->md;
  if (md && md->report) md->report(v);

  else if (warmc_report_extra) warmc_report_extra(v);
}

static WarmInstance *g_cinsts[MAX_INSTANCES];

void *(*warmc_host_lookup)(const char *name);

__attribute__((noreturn)) static void host_unimplemented(const char *name) {
  eputs("warmc-rt: this component called ");
  eputs(name);
  eputs(", which the host linked into it does not implement\n");
  warmc_trap(TRAP_UNREACHABLE);
}

typedef struct {
  u32 rt;
  u32 own;
  u32 lends;
  u32 pad;
  u64 rep;
} ResSlot;

typedef struct {
  ResSlot *slots;
  u32 *freelist;
  u32 len, cap, nfree;
} ResTable;

static ResTable g_restab = { 0, 0, 1, 0, 0 };

static void res_grow(ResTable *t) {
  u32 cap = t->cap ? t->cap * 2 : 8;
  ResSlot *s = arena_alloc(sizeof(ResSlot) * cap);
  u32 *f = arena_alloc(sizeof(u32) * cap);
  for (u32 i = 0; i < t->cap; i++) s[i] = t->slots[i];
  for (u32 i = t->cap; i < cap; i++) {
    s[i].rt = 0; s[i].own = 0; s[i].lends = 0; s[i].pad = 0; s[i].rep = 0;
  }
  for (u32 i = 0; i < t->nfree; i++) f[i] = t->freelist[i];
  t->slots = s;
  t->freelist = f;
  t->cap = cap;
}

static u32 res_add(u32 rt, u64 rep, u32 own) {
  ResTable *t = &g_restab;
  u32 i;
  if (t->nfree) {
    i = t->freelist[--t->nfree];
  } else {
    if (t->len >= t->cap) res_grow(t);
    i = t->len++;
  }
  ResSlot *s = &t->slots[i];
  s->rt = rt + 1;
  s->rep = rep;
  s->own = own;
  s->lends = 0;
  return i;
}

static ResSlot *res_get(u32 rt, u32 h) {
  ResTable *t = &g_restab;
  if (h >= t->len) warmc_trap(TRAP_UNREACHABLE);
  ResSlot *s = &t->slots[h];
  if (!s->rt || s->rt != rt + 1) warmc_trap(TRAP_UNREACHABLE);
  return s;
}

static void res_free_slot(u32 rt, u32 h) {
  ResTable *t = &g_restab;
  (void)rt;
  t->slots[h].rt = 0;
  t->freelist[t->nfree++] = h;
}

u32 warmc_res_new(u32 rt, u64 rep) { return res_add(rt, rep, 1); }

u64 warmc_res_rep(u32 rt, u32 h) { return res_get(rt, h)->rep; }

u32 warmc_res_lower_own(u32 rt, u64 rep) { return res_add(rt, rep, 1); }

u32 warmc_res_lower_borrow(u32 rt, u64 rep) { return res_add(rt, rep, 0); }

u64 warmc_res_lift_own(u32 rt, u32 h) {
  ResSlot *s = res_get(rt, h);
  if (s->lends || !s->own) warmc_trap(TRAP_UNREACHABLE);
  u64 rep = s->rep;
  res_free_slot(rt, h);
  return rep;
}

u64 warmc_res_lift_borrow(u32 rt, u32 h) {
  ResSlot *s = res_get(rt, h);
  s->lends++;
  return s->rep;
}

void warmc_res_release(u32 rt, u32 h) {
  ResTable *t = &g_restab;
  (void)rt;
  if (h < t->len && t->slots[h].lends) t->slots[h].lends--;
}

u64 warmc_res_drop(u32 rt, u32 h, u32 *owned) {
  ResSlot *s = res_get(rt, h);
  if (s->lends) warmc_trap(TRAP_UNREACHABLE);
  u64 rep = s->rep;
  *owned = s->own;
  res_free_slot(rt, h);
  return rep;
}

static u64 res_new_abi(WarmInstance *vm, u64 rt, u64 rep) {
  return warmc_res_new((u32)rt, (u32)rep);
}

static u64 res_rep_abi(WarmInstance *vm, u64 rt, u64 h) {
  return (u32)warmc_res_rep((u32)rt, (u32)h);
}

static u64 res_drop_abi(WarmInstance *vm, u64 rt, u64 h) {
  u32 owned = 0;
  u64 rep = warmc_res_drop((u32)rt, (u32)h, &owned);
  return (rep & 0xffffffffull) | ((u64)owned << 32);
}

static void *builtin_host(const char *name) {
  if (streq(name, "warmc:resource#new")) return (void *)res_new_abi;
  if (streq(name, "warmc:resource#rep")) return (void *)res_rep_abi;
  if (streq(name, "warmc:resource#drop")) return (void *)res_drop_abi;
  return 0;
}

void warmc_component_instantiate(void) {
  const ComponentDesc *cd = warmc_component;
  u32 maxb = 1;
  for (u32 i = 0; i < cd->ninsts; i++)
    if (cd->insts[i].md->nimports > maxb) maxb = cd->insts[i].md->nimports;
  WarmBinding *binds = arena_alloc(sizeof(WarmBinding) * maxb);

  for (u32 i = 0; i < cd->ninsts; i++) {
    const CompInstDesc *ci = &cd->insts[i];
    u32 n = ci->md->nimports;
    for (u32 j = 0; j < n; j++) {
      binds[j].code = 0;
      binds[j].name = ci->binds[j].name;
      binds[j].inst = ci->binds[j].inst == 0xffffffffu
                        ? 0 : g_cinsts[ci->binds[j].inst];
      if (ci->binds[j].host) {
        void *code = builtin_host(ci->binds[j].host);
        if (!code && warmc_host_lookup) code = warmc_host_lookup(ci->binds[j].host);
        binds[j].code = code ? code : (void *)host_unimplemented;
        if (!code) binds[j].inst = (WarmInstance *)(void *)ci->binds[j].host;
      }
    }
    g_cinsts[i] = warmc_instantiate_bound(ci->md, n ? binds : 0);
  }
}

WarmInstance *warmc_component_export(const char *name, const ExportDesc **out) {
  const ComponentDesc *cd = warmc_component;
  for (u32 i = 0; i < cd->nexports; i++) {
    if (!streq(cd->exports[i].name, name)) continue;
    WarmInstance *v = g_cinsts[cd->exports[i].inst];
    const ExportDesc *e = warmc_find_export(v, cd->exports[i].export_name);
    if (!e) return 0;
    *out = e;
    return v;
  }
  return 0;
}

__attribute__((noreturn)) static void component_run(int argc, char **argv) {
  const ComponentDesc *cd = warmc_component;
  warmc_component_instantiate();

  if (argc < 2) {
    puts_("exports:\n");
    for (u32 i = 0; i < cd->nexports; i++) {
      puts_("  ");
      puts_(cd->exports[i].name);
      puts_("\n");
    }
    sys_exit(0);
  }

  int reps = 0;
  if (argc >= 3 && streq(argv[1], "--bench")) {
    reps = (int)parse_u64(argv[2], 0);
    if (reps < 1) reps = 1;
    argc -= 2;
    argv += 2;
  }

  int rc = 0, hot = 0, i = 1;
  while (i < argc) {
    int j = i + 1;
    while (j < argc && !streq(argv[j], "--")) j++;
    u32 k = 0;
    while (k < cd->nexports && !streq(cd->exports[k].name, argv[i])) k++;
    if (k == cd->nexports) {
      eputs("warmc-rt: no such export: "); eputs(argv[i]); eputs("\n");
      sys_exit(2);
    }

    vm = g_cinsts[cd->exports[k].inst];
    g_desc = INST_OF(vm)->md;
    const ExportDesc *e = find_export(cd->exports[k].export_name);
    if (!e) {
      eputs("warmc-rt: the component names a core export that is missing: ");
      eputs(cd->exports[k].export_name); eputs("\n");
      sys_exit(2);
    }
    int r = call_export(e, argv[i], j - i - 1, argv + i + 1, reps, hot);
    if (r) rc = r;
    hot = (reps > 0);
    i = j + 1;
  }
  warmc_report_run(vm);
  sys_exit(rc);
}

void warmc_gcth_run_tests(void) {
  { u64 n = rt_getenv_u64("WARMC_GC_THREADTEST", 0);
    if (n) gcth_selftest(n); }
  if (rt_getenv("WARMC_GC_ROUNDS"))
    gcth_start_rounds(rt_getenv_u64("WARMC_GC_ROUNDS", 0));
}

__attribute__((noreturn)) static void warmc_run(int argc, char **argv) {
  gc_read_options();

  if (warmc_gcth_tests) warmc_gcth_tests();

  u64 here = (u64)(uptr)&argv;
  g_stack_limit = here > (6u << 20) ? here - (6u << 20) : 0;

  sys_exit(warmc_driver_hook(argc, argv));
}

int warmc_cli_main(int argc, char **argv) {

  if (argc >= 2 && streq(argv[1], "--catch")) {
    g_catch_traps = 1;
    argc--;
    argv++;
  }

  if (warmc_component) component_run(argc, argv);

  const ModuleDesc *sel = warmc_modules[0];
  if (warmc_nmodules > 1) {
    if (argc < 2) {
      puts_("modules:\n");
      for (u32 i = 0; i < warmc_nmodules; i++) {
        puts_("  ");
        puts_(warmc_module_names[i]);
        puts_("\n");
      }
      sys_exit(0);
    }
    u32 i = 0;
    while (i < warmc_nmodules && !streq(warmc_module_names[i], argv[1])) i++;
    if (i == warmc_nmodules) {
      eputs("warmc-rt: no such module: "); eputs(argv[1]); eputs("\n");
      sys_exit(2);
    }
    sel = warmc_modules[i];
    argc--;
    argv++;
  }
  g_desc = sel;
  vm = warmc_instantiate(sel);

  int reps = 0;
  if (argc >= 3 && streq(argv[1], "--bench")) {
    reps = (int)parse_u64(argv[2], 0);
    if (reps < 1) reps = 1;
    argc -= 2;
    argv += 2;
  }

  if (argc >= 2) {
    int rc = 0, hot = 0, i = 1;
    while (i < argc) {
      int j = i + 1;
      while (j < argc && !streq(argv[j], "--")) j++;
      const ExportDesc *e = find_export(argv[i]);
      if (!e) { eputs("warmc-rt: no such export: "); eputs(argv[i]); eputs("\n"); sys_exit(2); }
          int r = call_export(e, argv[i], j - i - 1, argv + i + 1, reps, hot);
          if (r) rc = r;
      hot = (reps > 0);
      i = j + 1;
    }
    warmc_report_run(vm);
    sys_exit(rc);
  }

  const ExportDesc *e = find_export("_start");
  if (!e) e = find_export("main");
  if (e) {
    int r = call_export(e, "_start", 0, 0, 0, 0);
    warmc_report_run(vm);
    sys_exit(r);
  }

  list_exports();
  sys_exit(0);
}

#if defined(WARMC_DARWIN)

int main(int argc, char **argv, char **envp) {
  g_environ = envp;
  warmc_run(argc, argv);
}
#elif defined(WARMC_WINDOWS)

/* the PE entry point: called by the loader with a valid stack, no args */
__attribute__((noreturn)) void warmc_win_entry(void) {
  warmc_win_check_isa();
  int argc;
  char **argv;
  warmc_win_cmdline(&argc, &argv);
  g_environ = warmc_win_environ();
  warmc_run(argc, argv);
}
#else

__attribute__((noreturn)) void warmc_start_c(long *sp) {
  int argc = (int)sp[0];
  g_environ = (char **)&sp[1 + argc + 1];
  warmc_run(argc, (char **)&sp[1]);
}
#endif
