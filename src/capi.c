/* The WebAssembly C API (include/wasm.h) over warmc's JIT (src/jit.c) and
   runtime (rt/), with include/warm.h's additions.

   Objects.  Every reference wasm.h hands out -- function, global, table,
   memory, instance, module, trap, foreign object -- is an Ent, counted:
   each `own` pointer the caller holds is one count, and a copy is the same
   pointer counted again, so that wasm_X_same is pointer equality.  What
   wasm code can reach -- instances and what they export, host functions,
   a host object passed in as an externref -- is also held by its store,
   and lives until the store is deleted.  A module is held only by its
   handles and by the instances made of it: its code is shared by every
   store that obtains it.

   Imports.  A function is bound as the runtime binds a host function, its
   code and the context its first argument is: a host function's is the
   generic thunk below, a wasm function's its own code and instance, so a
   call of it from another instance goes through its entry check as any
   cross-instance call does.  A memory, table, global or tag is bound as
   the export of the instance that has it; one the host makes
   (wasm_memory_new and the like) is the export of a tiny module made for
   it, as is a host function that wasm code is handed as a reference.

   Threads.  The runtime runs one thread's wasm at a time: every call into
   it takes one lock, which a host function calling back in already holds,
   and a thread that takes it sets the stack limit wasm runs to from its
   own stack. */

/* the library's exports: wasm.h's functions and warm.h's, and nothing else */
#if defined(_WIN32)
#define WASM_API_EXTERN __declspec(dllexport)
#else
#define WASM_API_EXTERN __attribute__((visibility("default")))
#endif
#include "wasm.h"
#include "warm.h"
#include "warm_rt.h"
#include "jit.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the runtime's, which warm_rt.h does not declare */
void *warmc_table_get(WarmInstance *v, u64 i, u32 tab);
void warmc_table_set(WarmInstance *v, u64 i, void *ref, u32 tab);
u64 warmc_table_grow(WarmInstance *v, void *init, u64 delta, u32 tab);
u64 warmc_memory_grow(WarmInstance *v, u64 delta, u32 mem);
FuncRef *warmc_funcref_cell(WarmInstance *inst, u32 func);
void *warmc_memory_view(WarmInstance *inst, u32 idx, u8 **base, u64 *size,
                        u64 *pages, u64 *max_pages, u64 *decl_max, u32 *flags);
void *warmc_table_view(WarmInstance *inst, u32 idx, u64 *len, u64 *max_len,
                       u32 *flags);
void warmc_rt_thread_enter(u64 limit);
void warmc_instance_free(WarmInstance *v);
int warmc_rt_forget_desc(const ModuleDesc *md);
extern void *(*warmc_rt_malloc_hook)(u64 bytes);
extern void (*warmc_rt_free_hook)(void *p);

#define own

static void oom(void) {
  fputs("libwarmc: out of memory\n", stderr);
  abort();
}

static void *xzalloc(size_t n) {
  void *p = calloc(1, n ? n : 1);
  if (!p) oom();
  return p;
}

static char *xstrdup_n(const char *s, size_t n) {
  char *d = malloc(n + 1);
  if (!d) oom();
  memcpy(d, s, n);
  d[n] = 0;
  return d;
}

/* ---- the runtime's lock ------------------------------------------------ */

static pthread_once_t g_rt_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_rt_lock;
static bool g_rt_ok;
static _Thread_local unsigned g_depth;
static _Thread_local u64 g_limit;

static void *rt_malloc(u64 n) { return calloc(1, (size_t)n); }

static void rt_init(void) {
  /* first: the collector's heap and the linear memories go wherever the
     system puts them, which may be the room beside the runtime */
  jit_reserve();
  warmc_rt_malloc_hook = rt_malloc;
  warmc_rt_free_hook = free;
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&g_rt_lock, &a);
  pthread_mutexattr_destroy(&a);
  g_rt_ok = warmc_rt_library_mode() != 0;
}

/* The limit wasm's frames run to on this thread: its stack less a margin
   for the runtime and a signal frame, or a fixed budget below here where
   the platform does not say where the stack ends. */
static u64 thread_limit(void) {
  if (g_limit) return g_limit;
  u64 sp = (u64)(uptr)__builtin_frame_address(0);
  u64 lo = 0, hi = 0;
  warmc_rt_stack_bounds(&lo, &hi);
  if (!lo || sp <= lo || sp > hi) {
    g_limit = sp > (1ull << 20) ? sp - (1ull << 20) : 1;
  } else {
    u64 margin = (hi - lo) / 8;
    if (margin < (64u << 10)) margin = 64u << 10;
    if (margin > (256u << 10)) margin = 256u << 10;
    g_limit = lo + margin;
  }
  return g_limit;
}

static void rt_enter(void) {
  pthread_once(&g_rt_once, rt_init);
  pthread_mutex_lock(&g_rt_lock);
  if (g_depth++ == 0) warmc_rt_thread_enter(thread_limit());
}

static void rt_leave(void) {
  g_depth--;
  pthread_mutex_unlock(&g_rt_lock);
}

/* ---- vectors ------------------------------------------------------------ */

#define VEC_COMMON(name, elem)                                                  \
  void wasm_##name##_vec_new_empty(own wasm_##name##_vec_t *out) {              \
    out->size = 0;                                                              \
    out->data = NULL;                                                           \
  }                                                                             \
  void wasm_##name##_vec_new_uninitialized(own wasm_##name##_vec_t *out,        \
                                           size_t n) {                          \
    out->size = n;                                                              \
    out->data = n ? xzalloc(n * sizeof(elem)) : NULL;                           \
  }                                                                             \
  void wasm_##name##_vec_new(own wasm_##name##_vec_t *out, size_t n,            \
                             own elem const src[]) {                            \
    wasm_##name##_vec_new_uninitialized(out, n);                                \
    if (n) memcpy(out->data, src, n * sizeof(elem));                            \
  }

/* a vector of plain values: copied bytewise, nothing to delete inside */
#define VEC_PLAIN(name, elem)                                                   \
  VEC_COMMON(name, elem)                                                        \
  void wasm_##name##_vec_copy(own wasm_##name##_vec_t *out,                     \
                              const wasm_##name##_vec_t *v) {                   \
    wasm_##name##_vec_new(out, v->size, v->data);                               \
  }                                                                             \
  void wasm_##name##_vec_delete(own wasm_##name##_vec_t *v) {                   \
    free(v->data);                                                              \
    v->data = NULL;                                                             \
    v->size = 0;                                                                \
  }

/* a vector of owned pointers: copied and deleted element by element */
#define VEC_OWNED(name)                                                         \
  VEC_COMMON(name, wasm_##name##_t *)                                           \
  void wasm_##name##_vec_copy(own wasm_##name##_vec_t *out,                     \
                              const wasm_##name##_vec_t *v) {                   \
    wasm_##name##_vec_new_uninitialized(out, v->size);                          \
    for (size_t i = 0; i < v->size; i++)                                        \
      out->data[i] = v->data[i] ? wasm_##name##_copy(v->data[i]) : NULL;        \
  }                                                                             \
  void wasm_##name##_vec_delete(own wasm_##name##_vec_t *v) {                   \
    for (size_t i = 0; i < v->size; i++)                                        \
      if (v->data[i]) wasm_##name##_delete(v->data[i]);                         \
    free(v->data);                                                              \
    v->data = NULL;                                                             \
    v->size = 0;                                                                \
  }

VEC_PLAIN(byte, wasm_byte_t)

/* ---- types ---------------------------------------------------------------- */

struct wasm_valtype_t { wasm_valkind_t kind; };

/* Every extern type starts with its kind, which wasm_externtype_t is. */
struct wasm_externtype_t { wasm_externkind_t kind; };
struct wasm_functype_t {
  wasm_externtype_t x;
  wasm_valtype_vec_t params, results;
};
struct wasm_globaltype_t {
  wasm_externtype_t x;
  wasm_valtype_t *content;
  wasm_mutability_t mutability;
};
struct wasm_tabletype_t {
  wasm_externtype_t x;
  wasm_valtype_t *element;
  wasm_limits_t limits;
};
struct wasm_memorytype_t {
  wasm_externtype_t x;
  wasm_limits_t limits;
};
struct wasm_tagtype_t {
  wasm_externtype_t x;
  wasm_functype_t *functype;
};
struct wasm_importtype_t {
  wasm_name_t module, name;
  wasm_externtype_t *type;
};
struct wasm_exporttype_t {
  wasm_name_t name;
  wasm_externtype_t *type;
};

own wasm_valtype_t *wasm_valtype_new(wasm_valkind_t k) {
  wasm_valtype_t *t = xzalloc(sizeof *t);
  t->kind = k;
  return t;
}
wasm_valkind_t wasm_valtype_kind(const wasm_valtype_t *t) { return t->kind; }
own wasm_valtype_t *wasm_valtype_copy(const wasm_valtype_t *t) {
  return wasm_valtype_new(t->kind);
}
void wasm_valtype_delete(own wasm_valtype_t *t) { free(t); }
VEC_OWNED(valtype)

own wasm_functype_t *wasm_functype_new(own wasm_valtype_vec_t *params,
                                       own wasm_valtype_vec_t *results) {
  wasm_functype_t *t = xzalloc(sizeof *t);
  t->x.kind = WASM_EXTERN_FUNC;
  t->params = *params;
  t->results = *results;
  return t;
}
const wasm_valtype_vec_t *wasm_functype_params(const wasm_functype_t *t) {
  return &t->params;
}
const wasm_valtype_vec_t *wasm_functype_results(const wasm_functype_t *t) {
  return &t->results;
}
own wasm_functype_t *wasm_functype_copy(const wasm_functype_t *t) {
  wasm_valtype_vec_t p, r;
  wasm_valtype_vec_copy(&p, &t->params);
  wasm_valtype_vec_copy(&r, &t->results);
  return wasm_functype_new(&p, &r);
}
void wasm_functype_delete(own wasm_functype_t *t) {
  if (!t) return;
  wasm_valtype_vec_delete(&t->params);
  wasm_valtype_vec_delete(&t->results);
  free(t);
}
VEC_OWNED(functype)

own wasm_globaltype_t *wasm_globaltype_new(own wasm_valtype_t *content,
                                           wasm_mutability_t m) {
  wasm_globaltype_t *t = xzalloc(sizeof *t);
  t->x.kind = WASM_EXTERN_GLOBAL;
  t->content = content;
  t->mutability = m;
  return t;
}
const wasm_valtype_t *wasm_globaltype_content(const wasm_globaltype_t *t) {
  return t->content;
}
wasm_mutability_t wasm_globaltype_mutability(const wasm_globaltype_t *t) {
  return t->mutability;
}
own wasm_globaltype_t *wasm_globaltype_copy(const wasm_globaltype_t *t) {
  return wasm_globaltype_new(wasm_valtype_copy(t->content), t->mutability);
}
void wasm_globaltype_delete(own wasm_globaltype_t *t) {
  if (!t) return;
  wasm_valtype_delete(t->content);
  free(t);
}
VEC_OWNED(globaltype)

own wasm_tabletype_t *wasm_tabletype_new(own wasm_valtype_t *elem,
                                         const wasm_limits_t *limits) {
  wasm_tabletype_t *t = xzalloc(sizeof *t);
  t->x.kind = WASM_EXTERN_TABLE;
  t->element = elem;
  t->limits = *limits;
  return t;
}
const wasm_valtype_t *wasm_tabletype_element(const wasm_tabletype_t *t) {
  return t->element;
}
const wasm_limits_t *wasm_tabletype_limits(const wasm_tabletype_t *t) {
  return &t->limits;
}
own wasm_tabletype_t *wasm_tabletype_copy(const wasm_tabletype_t *t) {
  return wasm_tabletype_new(wasm_valtype_copy(t->element), &t->limits);
}
void wasm_tabletype_delete(own wasm_tabletype_t *t) {
  if (!t) return;
  wasm_valtype_delete(t->element);
  free(t);
}
VEC_OWNED(tabletype)

own wasm_memorytype_t *wasm_memorytype_new(const wasm_limits_t *limits) {
  wasm_memorytype_t *t = xzalloc(sizeof *t);
  t->x.kind = WASM_EXTERN_MEMORY;
  t->limits = *limits;
  return t;
}
const wasm_limits_t *wasm_memorytype_limits(const wasm_memorytype_t *t) {
  return &t->limits;
}
own wasm_memorytype_t *wasm_memorytype_copy(const wasm_memorytype_t *t) {
  return wasm_memorytype_new(&t->limits);
}
void wasm_memorytype_delete(own wasm_memorytype_t *t) { free(t); }
VEC_OWNED(memorytype)

own wasm_tagtype_t *wasm_tagtype_new(own wasm_functype_t *ft) {
  wasm_tagtype_t *t = xzalloc(sizeof *t);
  t->x.kind = WASM_EXTERN_TAG;
  t->functype = ft;
  return t;
}
const wasm_functype_t *wasm_tagtype_functype(const wasm_tagtype_t *t) {
  return t->functype;
}
own wasm_tagtype_t *wasm_tagtype_copy(const wasm_tagtype_t *t) {
  return wasm_tagtype_new(wasm_functype_copy(t->functype));
}
void wasm_tagtype_delete(own wasm_tagtype_t *t) {
  if (!t) return;
  wasm_functype_delete(t->functype);
  free(t);
}
VEC_OWNED(tagtype)

wasm_externkind_t wasm_externtype_kind(const wasm_externtype_t *t) { return t->kind; }

#define EXTERNTYPE_CASTS(name, KIND)                                            \
  wasm_externtype_t *wasm_##name##_as_externtype(wasm_##name##_t *t) {          \
    return &t->x;                                                               \
  }                                                                             \
  const wasm_externtype_t *wasm_##name##_as_externtype_const(                   \
      const wasm_##name##_t *t) {                                               \
    return &t->x;                                                               \
  }                                                                             \
  wasm_##name##_t *wasm_externtype_as_##name(wasm_externtype_t *t) {            \
    return t && t->kind == KIND ? (wasm_##name##_t *)t : NULL;                  \
  }                                                                             \
  const wasm_##name##_t *wasm_externtype_as_##name##_const(                     \
      const wasm_externtype_t *t) {                                             \
    return t && t->kind == KIND ? (const wasm_##name##_t *)t : NULL;            \
  }
EXTERNTYPE_CASTS(functype, WASM_EXTERN_FUNC)
EXTERNTYPE_CASTS(globaltype, WASM_EXTERN_GLOBAL)
EXTERNTYPE_CASTS(tabletype, WASM_EXTERN_TABLE)
EXTERNTYPE_CASTS(memorytype, WASM_EXTERN_MEMORY)
EXTERNTYPE_CASTS(tagtype, WASM_EXTERN_TAG)

own wasm_externtype_t *wasm_externtype_copy(const wasm_externtype_t *t) {
  switch (t->kind) {
    case WASM_EXTERN_FUNC: return &wasm_functype_copy((const wasm_functype_t *)t)->x;
    case WASM_EXTERN_GLOBAL: return &wasm_globaltype_copy((const wasm_globaltype_t *)t)->x;
    case WASM_EXTERN_TABLE: return &wasm_tabletype_copy((const wasm_tabletype_t *)t)->x;
    case WASM_EXTERN_MEMORY: return &wasm_memorytype_copy((const wasm_memorytype_t *)t)->x;
    case WASM_EXTERN_TAG: return &wasm_tagtype_copy((const wasm_tagtype_t *)t)->x;
  }
  return NULL;
}

void wasm_externtype_delete(own wasm_externtype_t *t) {
  if (!t) return;
  switch (t->kind) {
    case WASM_EXTERN_FUNC: wasm_functype_delete((wasm_functype_t *)t); break;
    case WASM_EXTERN_GLOBAL: wasm_globaltype_delete((wasm_globaltype_t *)t); break;
    case WASM_EXTERN_TABLE: wasm_tabletype_delete((wasm_tabletype_t *)t); break;
    case WASM_EXTERN_MEMORY: wasm_memorytype_delete((wasm_memorytype_t *)t); break;
    case WASM_EXTERN_TAG: wasm_tagtype_delete((wasm_tagtype_t *)t); break;
  }
}
VEC_OWNED(externtype)

own wasm_importtype_t *wasm_importtype_new(own wasm_name_t *module,
                                           own wasm_name_t *name,
                                           own wasm_externtype_t *type) {
  wasm_importtype_t *t = xzalloc(sizeof *t);
  t->module = *module;
  t->name = *name;
  t->type = type;
  return t;
}
const wasm_name_t *wasm_importtype_module(const wasm_importtype_t *t) { return &t->module; }
const wasm_name_t *wasm_importtype_name(const wasm_importtype_t *t) { return &t->name; }
const wasm_externtype_t *wasm_importtype_type(const wasm_importtype_t *t) { return t->type; }
own wasm_importtype_t *wasm_importtype_copy(const wasm_importtype_t *t) {
  wasm_name_t m, n;
  wasm_byte_vec_copy(&m, &t->module);
  wasm_byte_vec_copy(&n, &t->name);
  return wasm_importtype_new(&m, &n, wasm_externtype_copy(t->type));
}
void wasm_importtype_delete(own wasm_importtype_t *t) {
  if (!t) return;
  wasm_byte_vec_delete(&t->module);
  wasm_byte_vec_delete(&t->name);
  wasm_externtype_delete(t->type);
  free(t);
}
VEC_OWNED(importtype)

own wasm_exporttype_t *wasm_exporttype_new(own wasm_name_t *name,
                                           own wasm_externtype_t *type) {
  wasm_exporttype_t *t = xzalloc(sizeof *t);
  t->name = *name;
  t->type = type;
  return t;
}
const wasm_name_t *wasm_exporttype_name(const wasm_exporttype_t *t) { return &t->name; }
const wasm_externtype_t *wasm_exporttype_type(const wasm_exporttype_t *t) { return t->type; }
own wasm_exporttype_t *wasm_exporttype_copy(const wasm_exporttype_t *t) {
  wasm_name_t n;
  wasm_byte_vec_copy(&n, &t->name);
  return wasm_exporttype_new(&n, wasm_externtype_copy(t->type));
}
void wasm_exporttype_delete(own wasm_exporttype_t *t) {
  if (!t) return;
  wasm_byte_vec_delete(&t->name);
  wasm_externtype_delete(t->type);
  free(t);
}
VEC_OWNED(exporttype)

/* The API's types of what the compiler said (jit.h) */
static wasm_functype_t *functype_of(const JitFuncType *ft) {
  wasm_valtype_vec_t p, r;
  wasm_valtype_vec_new_uninitialized(&p, ft->nparams);
  wasm_valtype_vec_new_uninitialized(&r, ft->nresults);
  for (uint32_t i = 0; i < ft->nparams; i++) p.data[i] = wasm_valtype_new(ft->types[i]);
  for (uint32_t i = 0; i < ft->nresults; i++)
    r.data[i] = wasm_valtype_new(ft->types[ft->nparams + i]);
  return wasm_functype_new(&p, &r);
}

static wasm_limits_t limits_of(uint64_t min, uint64_t max, bool has_max) {
  wasm_limits_t l;
  l.min = min > UINT32_MAX ? UINT32_MAX : (uint32_t)min;
  l.max = !has_max || max > UINT32_MAX ? wasm_limits_max_default : (uint32_t)max;
  return l;
}

static wasm_externtype_t *externtype_of(const JitExtern *x) {
  switch (x->kind) {
    case JIT_EXT_FUNC: return &functype_of(&x->ft)->x;
    case JIT_EXT_GLOBAL:
      return &wasm_globaltype_new(wasm_valtype_new(x->valkind),
                                  x->mutable_ ? WASM_VAR : WASM_CONST)->x;
    case JIT_EXT_TABLE: {
      wasm_limits_t l = limits_of(x->min, x->max, x->has_max);
      return &wasm_tabletype_new(wasm_valtype_new(x->valkind), &l)->x;
    }
    case JIT_EXT_MEMORY: {
      wasm_limits_t l = limits_of(x->min, x->max, x->has_max);
      return &wasm_memorytype_new(&l)->x;
    }
    case JIT_EXT_TAG: return &wasm_tagtype_new(functype_of(&x->ft))->x;
  }
  return NULL;
}

static bool functype_eq(const wasm_functype_t *a, const JitFuncType *b) {
  if (a->params.size != b->nparams || a->results.size != b->nresults) return false;
  for (uint32_t i = 0; i < b->nparams; i++)
    if (a->params.data[i]->kind != b->types[i]) return false;
  for (uint32_t i = 0; i < b->nresults; i++)
    if (a->results.data[i]->kind != b->types[b->nparams + i]) return false;
  return true;
}

/* ---- function types by id ----------------------------------------------
   Every function type a module compiled here has, by the id the compiler
   gives it, which is structural: what a function reference coming out of
   wasm is a function of. */

typedef struct { uint64_t id; wasm_functype_t *ft; } TypeSlot;
static TypeSlot *g_types;
static size_t g_ntypes, g_cap_types;
static pthread_mutex_t g_types_lock = PTHREAD_MUTEX_INITIALIZER;

static TypeSlot *type_slot(uint64_t id) {
  size_t mask = g_cap_types - 1;
  size_t h = (size_t)((id * 0x9e3779b97f4a7c15ull) >> 17) & mask;
  while (g_types[h].ft && g_types[h].id != id) h = (h + 1) & mask;
  return &g_types[h];
}

static void types_note(const JitModule *m) {
  pthread_mutex_lock(&g_types_lock);
  for (uint32_t i = 0; i < m->nftypes; i++) {
    if ((g_ntypes + 1) * 2 > g_cap_types) {
      TypeSlot *old = g_types;
      size_t oldcap = g_cap_types;
      g_cap_types = g_cap_types ? g_cap_types * 2 : 64;
      g_types = xzalloc(g_cap_types * sizeof(TypeSlot));
      for (size_t k = 0; k < oldcap; k++)
        if (old[k].ft) *type_slot(old[k].id) = old[k];
      free(old);
    }
    TypeSlot *s = type_slot(m->ftypes[i].id);
    if (s->ft) continue;
    s->id = m->ftypes[i].id;
    s->ft = functype_of(&m->ftypes[i].ft);
    g_ntypes++;
  }
  pthread_mutex_unlock(&g_types_lock);
}

static wasm_functype_t *type_by_id(uint64_t id) {
  pthread_mutex_lock(&g_types_lock);
  wasm_functype_t *ft = g_cap_types ? type_slot(id)->ft : NULL;
  pthread_mutex_unlock(&g_types_lock);
  return ft;
}

/* ---- a map of three words to a pointer ------------------------------------ */

typedef struct { u64 a, b, c; void *v; } MapSlot;
typedef struct { MapSlot *s; size_t n, cap; } Map;

static size_t map_hash(u64 a, u64 b, u64 c) {
  return (size_t)(((a * 0x9e3779b97f4a7c15ull) ^ (b * 0xc2b2ae3d27d4eb4full) ^
                   (c * 0x165667b19e3779f9ull)) >> 13);
}

static MapSlot *map_find3(const Map *m, u64 a, u64 b, u64 c) {
  if (!m->cap) return NULL;
  size_t mask = m->cap - 1, h = map_hash(a, b, c) & mask;
  while (m->s[h].v) {
    if (m->s[h].a == a && m->s[h].b == b && m->s[h].c == c) return &m->s[h];
    h = (h + 1) & mask;
  }
  return NULL;
}

static void map_put3(Map *m, u64 a, u64 b, u64 c, void *v) {
  if ((m->n + 1) * 2 > m->cap) {
    MapSlot *old = m->s;
    size_t oldcap = m->cap;
    m->cap = m->cap ? m->cap * 2 : 64;
    m->s = xzalloc(m->cap * sizeof(MapSlot));
    m->n = 0;
    for (size_t i = 0; i < oldcap; i++)
      if (old[i].v) map_put3(m, old[i].a, old[i].b, old[i].c, old[i].v);
    free(old);
  }
  size_t mask = m->cap - 1, h = map_hash(a, b, c) & mask;
  while (m->s[h].v && !(m->s[h].a == a && m->s[h].b == b && m->s[h].c == c))
    h = (h + 1) & mask;
  if (!m->s[h].v) m->n++;
  m->s[h].a = a;
  m->s[h].b = b;
  m->s[h].c = c;
  m->s[h].v = v;
}

static MapSlot *map_find(const Map *m, u64 a, u64 b) { return map_find3(m, a, b, 0); }
static void map_put(Map *m, u64 a, u64 b, void *v) { map_put3(m, a, b, 0, v); }

/* ---- configuration, engine, store ------------------------------------------ */

struct wasm_config_t {
  int level;
  char **flags;
  size_t nflags;
  uint32_t threads;
};

own wasm_config_t *wasm_config_new(void) {
  wasm_config_t *c = xzalloc(sizeof *c);
  c->level = 2;
  return c;
}

void wasm_config_delete(own wasm_config_t *c) {
  if (!c) return;
  for (size_t i = 0; i < c->nflags; i++) free(c->flags[i]);
  free(c->flags);
  free(c);
}

bool warm_config_opt_level_set(wasm_config_t *c, int level) {
  if (level < 0 || level > 3) return false;
  c->level = level;
  return true;
}

void warm_config_compile_threads_set(wasm_config_t *c, uint32_t n) { c->threads = n; }

void warm_config_flag_add(wasm_config_t *c, const char *flag) {
  c->flags = realloc(c->flags, (c->nflags + 1) * sizeof(char *));
  if (!c->flags) oom();
  c->flags[c->nflags++] = xstrdup_n(flag, strlen(flag));
}

typedef struct ModuleRec ModuleRec;
typedef struct { char *key; ModuleRec *mod; } Proxy;

struct wasm_engine_t {
  wasm_config_t *config;
  JitOpts opts;
  /* the modules made for host objects (proxy_module), by their text */
  pthread_mutex_t lock;
  Proxy *proxies;
  size_t nproxies;
};

own wasm_engine_t *wasm_engine_new_with_config(own wasm_config_t *c) {
  pthread_once(&g_rt_once, rt_init);
  if (!g_rt_ok) {
    wasm_config_delete(c);
    return NULL;
  }
  wasm_engine_t *e = xzalloc(sizeof *e);
  e->config = c ? c : wasm_config_new();
  e->opts.opt_level = e->config->level;
  e->opts.flags = (const char *const *)e->config->flags;
  e->opts.nflags = (uint32_t)e->config->nflags;
  e->opts.threads = e->config->threads;
  pthread_mutex_init(&e->lock, NULL);
  return e;
}

own wasm_engine_t *wasm_engine_new(void) { return wasm_engine_new_with_config(NULL); }

static void module_release(ModuleRec *m);

void wasm_engine_delete(own wasm_engine_t *e) {
  if (!e) return;
  for (size_t i = 0; i < e->nproxies; i++) {
    free(e->proxies[i].key);
    module_release(e->proxies[i].mod);
  }
  free(e->proxies);
  pthread_mutex_destroy(&e->lock);
  wasm_config_delete(e->config);
  free(e);
}

typedef struct Ent Ent;

/* A runtime instance the store made, and the module it is of */
typedef struct { WarmInstance *vm; ModuleRec *mod; } RtInst;

struct wasm_store_t {
  wasm_engine_t *engine;
  Ent *pinned;          /* what the store holds, newest first */
  /* a function's Ent by its code, instance and type: two functions whose
     bodies compiled alike share their code (codegen's merge), and are
     told apart by their types, the one thing about them wasm can see */
  Map funcs;
  Map refs;             /* a reference's Ent by its value */
  RtInst *insts;        /* every instance it made, freed with it */
  size_t ninsts, cap_insts;
  Map inst_ents;        /* an InstanceEnt by its runtime instance */
};

own wasm_store_t *wasm_store_new(wasm_engine_t *e) {
  wasm_store_t *s = xzalloc(sizeof *s);
  s->engine = e;
  return s;
}

/* ---- modules ------------------------------------------------------------- */

struct ModuleRec {
  _Atomic uint32_t refs;
  JitModule *jm;
  const ModuleDesc *md;
};

/* A module no handle and no instance holds any more: its code goes, unless
   the collector still refers to it (its heap types). */
static void module_release(ModuleRec *m) {
  if (!m || atomic_fetch_sub(&m->refs, 1) != 1) return;
  rt_enter();
  bool forget = warmc_rt_forget_desc(m->md) != 0;
  rt_leave();
  jit_module_free(m->jm, forget);
  free(m);
}

static ModuleRec *module_retain(ModuleRec *m) {
  atomic_fetch_add(&m->refs, 1);
  return m;
}

/* ---- references ---------------------------------------------------------- */

enum {
  K_FUNC, K_GLOBAL, K_TABLE, K_MEMORY, K_TAG, K_INSTANCE, K_MODULE,
  K_TRAP, K_FOREIGN, K_RAW
};

struct Ent {
  uint8_t kind;
  bool pinned;
  bool released;        /* what it held is given back (ent_release) */
  uint32_t refs;
  wasm_store_t *store;
  void *host_info;
  void (*finalizer)(void *);
  Ent *next, *prev;     /* in the store's list, while pinned */
};

typedef struct HostFunc HostFunc;

typedef struct {
  Ent e;
  FuncRef *cell;                /* a wasm function's */
  HostFunc *host;               /* or a host function */
  void *native;                 /* or the WASI host's, called with its importer */
  wasm_functype_t *type;
  /* the instance it is an export of, and the export's name, when it is
     one: an import of it is bound as that export, whose type the runtime
     checks against the import's as wasm's subtyping has it */
  WarmInstance *owner;
  char *export_name;
} FuncEnt;

struct HostFunc {
  wasm_func_callback_t cb;
  wasm_func_callback_with_env_t cb_env;
  void *env;
  void (*env_finalizer)(void *);
  wasm_store_t *store;
  wasm_functype_t *type;
  FuncRef *cell;                /* its reference, once wasm is handed one */
  FuncEnt *ent;
};

/* A global, table, memory or tag: the export of an instance */
typedef struct {
  Ent e;
  WarmInstance *owner;
  char *name;
  u32 index;
  uint8_t valkind;              /* a global's type, a table's elements */
  bool mutable_;
  u64 *cell;                    /* a global's */
  wasm_tagtype_t *tagtype;
} ExtEnt;

typedef struct {
  Ent e;
  WarmInstance *vm;
  ModuleRec *mod;
  Ent **exports;
  size_t slot;          /* its place in the store's insts */
  /* nothing it imports can take a reference of its out (no table, no
     global or function whose type has one), and nothing of it was handed
     out: no export, no reference to one of its functions */
  bool sealed, escaped;
} InstanceEnt;

typedef struct {
  Ent e;
  ModuleRec *mod;
} ModuleEnt;

typedef struct {
  Ent e;
  wasm_message_t msg;
  bool is_exit;
  int exit_status;
} TrapEnt;

/* A reference from wasm the host has no object of its own for: an anyref,
   an exnref, or an externref wasm made of one (extern.convert_any) */
typedef struct {
  Ent e;
  u64 raw;
} RawEnt;

static void *ent_new(size_t size, uint8_t kind, wasm_store_t *s) {
  Ent *e = xzalloc(size);
  e->kind = kind;
  e->refs = 1;
  e->store = s;
  return e;
}

/* The store holds e from now on, as long as it lives. */
static void ent_pin(Ent *e) {
  if (e->pinned || !e->store) return;
  e->pinned = true;
  e->refs++;
  e->prev = NULL;
  e->next = e->store->pinned;
  if (e->next) e->next->prev = e;
  e->store->pinned = e;
}

static void ent_unlink(Ent *e) {
  if (e->prev) e->prev->next = e->next;
  else e->store->pinned = e->next;
  if (e->next) e->next->prev = e->prev;
  e->next = e->prev = NULL;
  e->pinned = false;
}

/* The store's instance in slot i, freed now: nothing reaches it. */
static void rt_inst_free(wasm_store_t *s, size_t i) {
  RtInst *ri = &s->insts[i];
  if (!ri->vm) return;
  MapSlot *m = map_find(&s->inst_ents, (u64)(uptr)ri->vm, 0);
  if (m) m->v = NULL;
  rt_enter();
  warmc_instance_free(ri->vm);
  rt_leave();
  ri->vm = NULL;
  module_release(ri->mod);
  ri->mod = NULL;
}

static size_t rt_inst_add(wasm_store_t *s, WarmInstance *vm, ModuleRec *mod) {
  if (s->ninsts == s->cap_insts) {
    s->cap_insts = s->cap_insts ? s->cap_insts * 2 : 16;
    s->insts = realloc(s->insts, s->cap_insts * sizeof(RtInst));
    if (!s->insts) oom();
  }
  s->insts[s->ninsts].vm = vm;
  s->insts[s->ninsts].mod = module_retain(mod);
  return s->ninsts++;
}

/* What e holds given back, once: its finalizer run, its types and names
   freed, what it counts of a module or host function released.  When its
   store goes, everything in it is, whatever handles to it are left: they
   point at no more than its shell from then on, which the last of them
   frees. */
static void ent_release(Ent *e) {
  if (e->released) return;
  e->released = true;
  if (e->finalizer) e->finalizer(e->host_info);
  switch (e->kind) {
    case K_FUNC: {
      FuncEnt *f = (FuncEnt *)e;
      wasm_functype_delete(f->type);
      f->type = NULL;
      free(f->export_name);
      f->export_name = NULL;
      if (f->host) {
        if (f->host->env_finalizer) f->host->env_finalizer(f->host->env);
        wasm_functype_delete(f->host->type);
        free(f->host);
        f->host = NULL;
      }
      break;
    }
    case K_GLOBAL: case K_TABLE: case K_MEMORY: case K_TAG: {
      ExtEnt *x = (ExtEnt *)e;
      free(x->name);
      x->name = NULL;
      wasm_tagtype_delete(x->tagtype);
      x->tagtype = NULL;
      break;
    }
    case K_INSTANCE: {
      InstanceEnt *in = (InstanceEnt *)e;
      free(in->exports);
      in->exports = NULL;
      if (in->e.store) rt_inst_free(in->e.store, in->slot);
      module_release(in->mod);
      in->mod = NULL;
      break;
    }
    case K_MODULE:
      module_release(((ModuleEnt *)e)->mod);
      ((ModuleEnt *)e)->mod = NULL;
      break;
    case K_TRAP:
      wasm_byte_vec_delete(&((TrapEnt *)e)->msg);
      break;
    default: break;
  }
}

static void ent_free(Ent *e) {
  ent_release(e);
  free(e);
}

static void ent_unref(Ent *e) {
  if (!e) return;
  if (--e->refs == 0) { ent_free(e); return; }
  /* an instance only its store holds, which nothing can reach: freed now
     rather than with the store */
  if (e->kind == K_INSTANCE && e->refs == 1 && e->pinned) {
    InstanceEnt *in = (InstanceEnt *)e;
    if (in->sealed && !in->escaped) {
      ent_unlink(e);
      e->refs = 0;
      ent_free(e);
    }
  }
}

static Ent *ent_ref(const Ent *e) {
  if (!e) return NULL;
  ((Ent *)e)->refs++;
  return (Ent *)e;
}

void wasm_store_delete(own wasm_store_t *s) {
  if (!s) return;
  /* what it holds first, which their finalizers see whole; then the
     instances, which go together, whatever they import of one another */
  Ent *e = s->pinned;
  while (e) {
    Ent *next = e->next;
    e->pinned = false;
    e->next = e->prev = NULL;
    e->store = NULL;
    if (--e->refs == 0) ent_free(e);
    else ent_release(e);
    e = next;
  }
  for (size_t i = 0; i < s->ninsts; i++) rt_inst_free(s, i);
  free(s->insts);
  free(s->funcs.s);
  free(s->refs.s);
  free(s->inst_ents.s);
  free(s);
}

/* the reference functions every kind has, wasm.h's WASM_DECLARE_REF_BASE */
#define REF_BASE(name)                                                          \
  void wasm_##name##_delete(own wasm_##name##_t *r) { ent_unref((Ent *)r); }    \
  own wasm_##name##_t *wasm_##name##_copy(const wasm_##name##_t *r) {           \
    return (wasm_##name##_t *)ent_ref((const Ent *)r);                          \
  }                                                                             \
  bool wasm_##name##_same(const wasm_##name##_t *a, const wasm_##name##_t *b) { \
    return a == b;                                                              \
  }                                                                             \
  void *wasm_##name##_get_host_info(const wasm_##name##_t *r) {                 \
    return ((const Ent *)r)->host_info;                                         \
  }                                                                             \
  void wasm_##name##_set_host_info(wasm_##name##_t *r, void *info) {            \
    ((Ent *)r)->host_info = info;                                               \
    ((Ent *)r)->finalizer = NULL;                                               \
  }                                                                             \
  void wasm_##name##_set_host_info_with_finalizer(                              \
      wasm_##name##_t *r, void *info, void (*fin)(void *)) {                    \
    ((Ent *)r)->host_info = info;                                               \
    ((Ent *)r)->finalizer = fin;                                                \
  }

/* and the casts to and from wasm_ref_t, wasm.h's WASM_DECLARE_REF */
#define REF(name, test)                                                         \
  REF_BASE(name)                                                                \
  wasm_ref_t *wasm_##name##_as_ref(wasm_##name##_t *r) {                        \
    return (wasm_ref_t *)r;                                                     \
  }                                                                             \
  const wasm_ref_t *wasm_##name##_as_ref_const(const wasm_##name##_t *r) {      \
    return (const wasm_ref_t *)r;                                               \
  }                                                                             \
  wasm_##name##_t *wasm_ref_as_##name(wasm_ref_t *r) {                          \
    const Ent *e = (const Ent *)r;                                              \
    return e && (test) ? (wasm_##name##_t *)r : NULL;                           \
  }                                                                             \
  const wasm_##name##_t *wasm_ref_as_##name##_const(const wasm_ref_t *r) {      \
    const Ent *e = (const Ent *)r;                                              \
    return e && (test) ? (const wasm_##name##_t *)r : NULL;                     \
  }

REF_BASE(ref)
REF(trap, e->kind == K_TRAP)
REF(foreign, e->kind == K_FOREIGN)
REF(module, e->kind == K_MODULE)
REF(func, e->kind == K_FUNC)
REF(global, e->kind == K_GLOBAL)
REF(table, e->kind == K_TABLE)
REF(memory, e->kind == K_MEMORY)
REF(extern, e->kind <= K_TAG)
REF(instance, e->kind == K_INSTANCE)

/* ---- values -------------------------------------------------------------- */

void wasm_val_delete(own wasm_val_t *v) {
  if (wasm_valkind_is_ref(v->kind) && v->of.ref) {
    wasm_ref_delete(v->of.ref);
    v->of.ref = NULL;
  }
}

void wasm_val_copy(own wasm_val_t *out, const wasm_val_t *v) {
  *out = *v;
  if (wasm_valkind_is_ref(v->kind) && v->of.ref) out->of.ref = wasm_ref_copy(v->of.ref);
}

static void val_vec_delete_elems(wasm_val_t *d, size_t n) {
  for (size_t i = 0; i < n; i++) wasm_val_delete(&d[i]);
}

VEC_COMMON(val, wasm_val_t)
void wasm_val_vec_copy(own wasm_val_vec_t *out, const wasm_val_vec_t *v) {
  wasm_val_vec_new_uninitialized(out, v->size);
  for (size_t i = 0; i < v->size; i++) wasm_val_copy(&out->data[i], &v->data[i]);
}
void wasm_val_vec_delete(own wasm_val_vec_t *v) {
  val_vec_delete_elems(v->data, v->size);
  free(v->data);
  v->data = NULL;
  v->size = 0;
}

static FuncRef *host_cell(HostFunc *h);

/* A value as wasm has it; false when it cannot be one of kind k. */
static bool to_raw(wasm_store_t *s, const wasm_val_t *v, uint8_t k, u64 *lo) {
  if (v->kind != k) return false;
  switch (k) {
    case WASM_I32: *lo = (u64)(u32)v->of.i32; return true;
    case WASM_I64: *lo = (u64)v->of.i64; return true;
    case WASM_F32: { u32 b; memcpy(&b, &v->of.f32, 4); *lo = b; return true; }
    case WASM_F64: memcpy(lo, &v->of.f64, 8); return true;
    default: break;
  }
  Ent *e = (Ent *)v->of.ref;
  if (!e) { *lo = 0; return true; }
  if (e->kind == K_RAW) { *lo = ((RawEnt *)e)->raw; return true; }
  if (k == WASM_FUNCREF) {
    if (e->kind != K_FUNC) return false;
    FuncEnt *f = (FuncEnt *)e;
    if (f->native) return false;
    FuncRef *c = f->host ? host_cell(f->host) : f->cell;
    if (!c) return false;
    *lo = (u64)(uptr)c;
    return true;
  }
  if (k != WASM_EXTERNREF && k != WARM_ANYREF) return false;
  /* any object of the host's, held by the store from now on: wasm may keep
     it anywhere.  In the any hierarchy it is tagged as the runtime tags a
     host reference (GC_HOST_TAG), as any.convert_extern would make it. */
  if (!e->store) e->store = s;
  ent_pin(e);
  map_put(&s->refs, (u64)(uptr)e, 0, e);
  *lo = (u64)(uptr)e | (k == WARM_ANYREF ? GC_HOST_TAG : 0);
  return true;
}

static Ent *func_ent_of_cell(wasm_store_t *s, FuncRef *c);

/* A value wasm returned, as the API has it (a reference counted once more) */
static void from_raw(wasm_store_t *s, u64 lo, uint8_t k, wasm_val_t *v) {
  v->kind = k;
  switch (k) {
    case WASM_I32: v->of.i32 = (int32_t)(u32)lo; return;
    case WASM_I64: v->of.i64 = (int64_t)lo; return;
    case WASM_F32: { u32 b = (u32)lo; memcpy(&v->of.f32, &b, 4); return; }
    case WASM_F64: memcpy(&v->of.f64, &lo, 8); return;
    default: break;
  }
  if (!lo) { v->of.ref = NULL; return; }
  if (k == WASM_FUNCREF) {
    v->of.ref = (wasm_ref_t *)func_ent_of_cell(s, (FuncRef *)(uptr)lo);
    return;
  }
  /* a host object in the any hierarchy is the object, tagged */
  MapSlot *m = GC_IS_HOST(lo) ? map_find(&s->refs, lo & ~(u64)3, 0) : NULL;
  if (!m) m = map_find(&s->refs, lo, 0);
  if (m) { v->of.ref = (wasm_ref_t *)ent_ref(m->v); return; }
  RawEnt *r = ent_new(sizeof *r, K_RAW, s);
  r->raw = lo;
  ent_pin(&r->e);
  map_put(&s->refs, lo, 0, r);
  v->of.ref = (wasm_ref_t *)&r->e;
}

/* ---- traps and frames ------------------------------------------------------ */

static TrapEnt *trap_make(wasm_store_t *s, const char *msg, size_t len) {
  TrapEnt *t = ent_new(sizeof *t, K_TRAP, s);
  wasm_byte_vec_new_uninitialized(&t->msg, len + 1);
  memcpy(t->msg.data, msg, len);
  t->msg.data[len] = 0;
  return t;
}

own wasm_trap_t *wasm_trap_new(wasm_store_t *s, const wasm_message_t *msg) {
  size_t n = msg && msg->size ? msg->size : 0;
  /* the message counts its NUL, as wasm_name_new_from_string_nt makes it */
  if (n && msg->data[n - 1] == 0) n--;
  return (wasm_trap_t *)trap_make(s, msg ? msg->data : "", n);
}

void wasm_trap_message(const wasm_trap_t *t, own wasm_message_t *out) {
  wasm_byte_vec_copy(out, &((const TrapEnt *)t)->msg);
}

struct wasm_frame_t {
  wasm_instance_t *instance;
  uint32_t func_index;
  size_t func_offset, module_offset;
};

void wasm_frame_delete(own wasm_frame_t *f) { free(f); }
own wasm_frame_t *wasm_frame_copy(const wasm_frame_t *f) {
  wasm_frame_t *c = xzalloc(sizeof *c);
  *c = *f;
  return c;
}
wasm_instance_t *wasm_frame_instance(const wasm_frame_t *f) { return f->instance; }
uint32_t wasm_frame_func_index(const wasm_frame_t *f) { return f->func_index; }
size_t wasm_frame_func_offset(const wasm_frame_t *f) { return f->func_offset; }
size_t wasm_frame_module_offset(const wasm_frame_t *f) { return f->module_offset; }
VEC_OWNED(frame)

/* No frames: warmc's code keeps no map from its addresses to the module's
   bytes. */
own wasm_frame_t *wasm_trap_origin(const wasm_trap_t *t) { (void)t; return NULL; }
void wasm_trap_trace(const wasm_trap_t *t, own wasm_frame_vec_t *out) {
  (void)t;
  wasm_frame_vec_new_empty(out);
}

bool warm_trap_exit_status(const wasm_trap_t *t, int *status) {
  const TrapEnt *e = (const TrapEnt *)t;
  if (!e->is_exit) return false;
  if (status) *status = e->exit_status;
  return true;
}

own wasm_foreign_t *wasm_foreign_new(wasm_store_t *s) {
  return (wasm_foreign_t *)ent_new(sizeof(Ent), K_FOREIGN, s);
}

/* The trap a host function raised, which the call it unwound to takes */
static _Thread_local TrapEnt *g_host_trap;

/* The trap a call into wasm ended with, as an object */
static wasm_trap_t *trap_of(wasm_store_t *s, u32 code) {
  if (code == TRAP_HOST && g_host_trap) {
    TrapEnt *t = g_host_trap;
    g_host_trap = NULL;
    return (wasm_trap_t *)t;
  }
  const char *msg = warmc_trap_name(code);
  return (wasm_trap_t *)trap_make(s, msg, strlen(msg));
}

/* ---- modules, continued ----------------------------------------------------- */

static void set_message(wasm_message_t *out, const char *msg) {
  if (!out) return;
  size_t n = strlen(msg);
  wasm_byte_vec_new_uninitialized(out, n + 1);
  memcpy(out->data, msg, n + 1);
}

/* A placed module's record, counted once; NULL for no module */
static ModuleRec *module_rec(JitModule *jm) {
  if (!jm) return NULL;
  ModuleRec *m = xzalloc(sizeof *m);
  atomic_init(&m->refs, 1);
  m->jm = jm;
  m->md = jit_module_desc(jm);
  types_note(jm);
  return m;
}

static wasm_module_t *module_ent(wasm_store_t *s, ModuleRec *m) {
  ModuleEnt *e = ent_new(sizeof *e, K_MODULE, s);
  e->mod = m;
  return (wasm_module_t *)&e->e;
}

own wasm_module_t *warm_module_new(wasm_store_t *s, const wasm_byte_vec_t *bin,
                                   own wasm_message_t *error_out) {
  char *err = NULL;
  ModuleRec *m = module_rec(jit_compile((const uint8_t *)bin->data, bin->size,
                                         &s->engine->opts, &err));
  if (!m) {
    set_message(error_out, err ? err : "the module does not compile");
    jit_free(err);
    return NULL;
  }
  if (error_out) wasm_byte_vec_new_empty(error_out);
  return module_ent(s, m);
}

own wasm_module_t *wasm_module_new(wasm_store_t *s, const wasm_byte_vec_t *bin) {
  return warm_module_new(s, bin, NULL);
}

bool wasm_module_validate(wasm_store_t *s, const wasm_byte_vec_t *bin) {
  (void)s;
  char *err = NULL;
  bool ok = jit_validate((const uint8_t *)bin->data, bin->size, &err);
  jit_free(err);
  return ok;
}

static const JitModule *jm_of(const wasm_module_t *m) {
  return ((const ModuleEnt *)m)->mod->jm;
}

void wasm_module_imports(const wasm_module_t *m, own wasm_importtype_vec_t *out) {
  const JitModule *jm = jm_of(m);
  wasm_importtype_vec_new_uninitialized(out, jm->nimports);
  for (uint32_t i = 0; i < jm->nimports; i++) {
    const JitExtern *x = &jm->imports[i];
    wasm_name_t mod, name;
    wasm_byte_vec_new(&mod, x->module_len, x->module);
    wasm_byte_vec_new(&name, x->name_len, x->name);
    out->data[i] = wasm_importtype_new(&mod, &name, externtype_of(x));
  }
}

void wasm_module_exports(const wasm_module_t *m, own wasm_exporttype_vec_t *out) {
  const JitModule *jm = jm_of(m);
  wasm_exporttype_vec_new_uninitialized(out, jm->nexports);
  for (uint32_t i = 0; i < jm->nexports; i++) {
    const JitExtern *x = &jm->exports[i];
    wasm_name_t name;
    wasm_byte_vec_new(&name, x->name_len, x->name);
    out->data[i] = wasm_exporttype_new(&name, externtype_of(x));
  }
}

void wasm_module_serialize(const wasm_module_t *m, own wasm_byte_vec_t *out) {
  size_t n = 0;
  uint8_t *p = jit_serialize(jm_of(m), &n);
  if (!p) { wasm_byte_vec_new_empty(out); return; }
  wasm_byte_vec_new(out, n, (const wasm_byte_t *)p);
  jit_free(p);
}

own wasm_module_t *wasm_module_deserialize(wasm_store_t *s, const wasm_byte_vec_t *b) {
  char *err = NULL;
  ModuleRec *m = module_rec(jit_deserialize((const uint8_t *)b->data, b->size, &err));
  jit_free(err);
  return m ? module_ent(s, m) : NULL;
}

/* A module shared between stores is its record, counted once more. */
struct wasm_shared_module_t { ModuleRec *mod; };

void wasm_shared_module_delete(own wasm_shared_module_t *sm) {
  if (!sm) return;
  module_release(sm->mod);
  free(sm);
}

own wasm_shared_module_t *wasm_module_share(const wasm_module_t *m) {
  wasm_shared_module_t *sm = xzalloc(sizeof *sm);
  sm->mod = module_retain(((const ModuleEnt *)m)->mod);
  return sm;
}

own wasm_module_t *wasm_module_obtain(wasm_store_t *s, const wasm_shared_module_t *sm) {
  return module_ent(s, module_retain(sm->mod));
}

size_t warm_module_custom_section(const wasm_module_t *m, const char *name,
                                  size_t index, const wasm_byte_t **data,
                                  size_t *size) {
  const u8 *d = NULL;
  usz n = 0;
  usz count = warmc_custom_section(((const ModuleEnt *)m)->mod->md, name, index, &d, &n);
  if (data) *data = (const wasm_byte_t *)d;
  if (size) *size = n;
  return count;
}

bool warm_wat2wasm(const char *text, size_t len, own wasm_byte_vec_t *out,
                   own wasm_message_t *error_out) {
  char *err = NULL;
  size_t n = 0;
  uint8_t *w = jit_wat2wasm(text, len, &n, &err);
  if (!w) {
    set_message(error_out, err ? err : "the text does not assemble");
    jit_free(err);
    wasm_byte_vec_new_empty(out);
    return false;
  }
  wasm_byte_vec_new(out, n, (const wasm_byte_t *)w);
  jit_free(w);
  if (error_out) wasm_byte_vec_new_empty(error_out);
  return true;
}

/* ---- the modules host objects are exports of ---------------------------------
   A memory, table or global the host makes, and a host function wasm is
   handed a reference to, is the export of an instance of a module made for
   its type, compiled once per engine. */

static ModuleRec *proxy_module(wasm_engine_t *e, const char *wat) {
  pthread_mutex_lock(&e->lock);
  for (size_t i = 0; i < e->nproxies; i++)
    if (!strcmp(e->proxies[i].key, wat)) {
      ModuleRec *m = e->proxies[i].mod;
      pthread_mutex_unlock(&e->lock);
      return m;
    }
  pthread_mutex_unlock(&e->lock);
  char *err = NULL;
  size_t n = 0;
  uint8_t *bin = jit_wat2wasm(wat, strlen(wat), &n, &err);
  ModuleRec *m = bin ? module_rec(jit_compile(bin, n, &e->opts, &err)) : NULL;
  jit_free(bin);
  if (!m) {
    fprintf(stderr, "libwarmc: internal: %s: %s\n", wat, err ? err : "?");
    abort();
  }
  pthread_mutex_lock(&e->lock);
  /* another thread may have made it meanwhile: keep the first */
  for (size_t i = 0; i < e->nproxies; i++)
    if (!strcmp(e->proxies[i].key, wat)) {
      ModuleRec *first = e->proxies[i].mod;
      pthread_mutex_unlock(&e->lock);
      module_release(m);
      return first;
    }
  e->proxies = realloc(e->proxies, (e->nproxies + 1) * sizeof(Proxy));
  if (!e->proxies) oom();
  e->proxies[e->nproxies].key = xstrdup_n(wat, strlen(wat));
  e->proxies[e->nproxies].mod = m;
  e->nproxies++;
  pthread_mutex_unlock(&e->lock);
  return m;
}

static WarmInstance *proxy_instance(wasm_store_t *s, const char *wat,
                                    const WarmBinding *binds) {
  ModuleRec *m = proxy_module(s->engine, wat);
  WarmBinding none = { 0, 0, 0, 0 };
  u32 err = 0;
  rt_enter();
  WarmInstance *v = warmc_instantiate_bound_catching(m->md, binds ? binds : &none, &err);
  rt_leave();
  if (!v) {
    fprintf(stderr, "libwarmc: internal: %s does not instantiate: %s\n", wat,
            warmc_trap_name(err));
    abort();
  }
  rt_inst_add(s, v, m);
  return v;
}

static const char *valkind_wat(uint8_t k) {
  switch (k) {
    case WASM_I32: return "i32";
    case WASM_I64: return "i64";
    case WASM_F32: return "f32";
    case WASM_F64: return "f64";
    case WARM_V128: return "v128";
    case WASM_EXTERNREF: return "externref";
    case WASM_FUNCREF: return "funcref";
    case WARM_ANYREF: return "anyref";
    case WARM_EXNREF: return "exnref";
  }
  return NULL;
}

/* ---- functions ---------------------------------------------------------------- */

void warm_host_thunk(void);

static Ent *func_ent_of_cell(wasm_store_t *s, FuncRef *c) {
  /* a host function's, whatever type the module that imported it gave it */
  if (c->code == (void *)warm_host_thunk) return ent_ref(&((HostFunc *)c->instance)->ent->e);
  MapSlot *m = map_find3(&s->funcs, (u64)(uptr)c->code, (u64)(uptr)c->instance,
                         c->type_id);
  if (m) return ent_ref(m->v);
  MapSlot *im = map_find(&s->inst_ents, (u64)(uptr)c->instance, 0);
  if (im && im->v) ((InstanceEnt *)im->v)->escaped = true;
  FuncEnt *f = ent_new(sizeof *f, K_FUNC, s);
  f->cell = c;
  wasm_functype_t *ft = type_by_id(c->type_id);
  f->type = ft ? wasm_functype_copy(ft) : wasm_functype_new_0_0();
  ent_pin(&f->e);
  map_put3(&s->funcs, (u64)(uptr)c->code, (u64)(uptr)c->instance, c->type_id, f);
  return &f->e;
}

void warm_host_thunk(void);

static FuncRef *host_cell(HostFunc *h) {
  if (h->cell) return h->cell;
  char wat[4096];
  size_t n = (size_t)snprintf(wat, sizeof wat, "(module (type (func");
  const wasm_valtype_vec_t *ps = &h->type->params, *rs = &h->type->results;
  if (ps->size) {
    n += (size_t)snprintf(wat + n, sizeof wat - n, " (param");
    for (size_t i = 0; i < ps->size && n < sizeof wat; i++) {
      const char *t = valkind_wat(ps->data[i]->kind);
      if (!t) return NULL;
      n += (size_t)snprintf(wat + n, sizeof wat - n, " %s", t);
    }
    n += (size_t)snprintf(wat + n, sizeof wat - n, ")");
  }
  if (rs->size) {
    n += (size_t)snprintf(wat + n, sizeof wat - n, " (result");
    for (size_t i = 0; i < rs->size && n < sizeof wat; i++) {
      const char *t = valkind_wat(rs->data[i]->kind);
      if (!t) return NULL;
      n += (size_t)snprintf(wat + n, sizeof wat - n, " %s", t);
    }
    n += (size_t)snprintf(wat + n, sizeof wat - n, ")");
  }
  n += (size_t)snprintf(wat + n, sizeof wat - n,
                        ")) (import \"\" \"f\" (func (type 0))) (export \"f\" (func 0)))");
  if (n >= sizeof wat) return NULL;
  WarmBinding b = { (WarmInstance *)h, "f", (void *)warm_host_thunk, 0 };
  WarmInstance *v = proxy_instance(h->store, wat, &b);
  h->cell = warmc_funcref_cell(v, 0);
  return h->cell;
}

static own wasm_func_t *host_func_new(wasm_store_t *s, const wasm_functype_t *type,
                                      wasm_func_callback_t cb,
                                      wasm_func_callback_with_env_t cb_env,
                                      void *env, void (*fin)(void *)) {
  FuncEnt *f = ent_new(sizeof *f, K_FUNC, s);
  HostFunc *h = xzalloc(sizeof *h);
  h->cb = cb;
  h->cb_env = cb_env;
  h->env = env;
  h->env_finalizer = fin;
  h->store = s;
  h->type = wasm_functype_copy(type);
  h->ent = f;
  f->host = h;
  f->type = wasm_functype_copy(type);
  ent_pin(&f->e);
  return (wasm_func_t *)&f->e;
}

own wasm_func_t *wasm_func_new(wasm_store_t *s, const wasm_functype_t *type,
                               wasm_func_callback_t cb) {
  return host_func_new(s, type, cb, NULL, NULL, NULL);
}

own wasm_func_t *wasm_func_new_with_env(wasm_store_t *s, const wasm_functype_t *type,
                                        wasm_func_callback_with_env_t cb,
                                        void *env, void (*fin)(void *)) {
  return host_func_new(s, type, NULL, cb, env, fin);
}

own wasm_functype_t *wasm_func_type(const wasm_func_t *f) {
  return wasm_functype_copy(((const FuncEnt *)f)->type);
}
size_t wasm_func_param_arity(const wasm_func_t *f) {
  return ((const FuncEnt *)f)->type->params.size;
}
size_t wasm_func_result_arity(const wasm_func_t *f) {
  return ((const FuncEnt *)f)->type->results.size;
}

static bool is_float(uint8_t k) { return k == WASM_F32 || k == WASM_F64 || k == WARM_V128; }

/* What wasm's calling convention gives a callee in registers, past the
   instance: the internal convention's, which src/isel.c and rt/start.S
   agree on. */
#if defined(__x86_64__)
#define NREG_INT 3
#define NREG_FLT 4
#define STK_SLOT 8
#else
#define NREG_INT 7
#define NREG_FLT 8
#define STK_SLOT 16
#endif

static wasm_trap_t *call_host(HostFunc *h, const wasm_val_vec_t *args,
                              wasm_val_vec_t *results) {
  return h->cb_env ? h->cb_env(h->env, args, results) : h->cb(args, results);
}

static wasm_trap_t *bad_call(wasm_store_t *s, const char *why) {
  return (wasm_trap_t *)trap_make(s, why, strlen(why));
}

/* A wasm function called with each parameter's bits in a slot of two words
   (a v128 both, any other value the first), its results written to slots
   alike, nres of them at most: what wasm's convention passes in registers
   goes there, and the rest in the stack slots warmc_invoke_s copies. */
static wasm_trap_t *invoke(const FuncEnt *f, const u64 *argv, u64 *resv, size_t nres) {
  wasm_store_t *s = f->e.store;
  const wasm_functype_t *ft = f->type;
  size_t np = ft->params.size, nr = ft->results.size;
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 stk_small[32];
  u64 *stk = stk_small;
  if (np * 2 > sizeof stk_small / sizeof stk_small[0]) stk = xzalloc(np * 16);
  size_t nstk = 0;
  u32 ni = 0, nf = 0;
  for (size_t i = 0; i < np; i++) {
    uint8_t k = ft->params.data[i]->kind;
    bool fl = is_float(k);
    if (fl ? nf < NREG_FLT : ni < NREG_INT) {
      if (fl) { fargs[2 * nf] = argv[2 * i]; fargs[2 * nf + 1] = argv[2 * i + 1]; }
      else iargs[ni] = argv[2 * i];
    } else {
#if defined(__x86_64__)
      /* eight bytes a scalar, an aligned sixteen a v128 */
      if (k == WARM_V128) {
        nstk = (nstk + 1) & ~(size_t)1;
        stk[nstk++] = argv[2 * i];
        stk[nstk++] = argv[2 * i + 1];
      } else {
        stk[nstk++] = argv[2 * i];
      }
#else
      stk[nstk++] = argv[2 * i];
      stk[nstk++] = k == WARM_V128 ? argv[2 * i + 1] : 0;
#endif
    }
    if (fl) nf++;
    else ni++;
  }
  u64 ret_small[WARMC_RETBUF_WORDS];
  u64 *ret = ret_small;
  if (nr > WARMC_RET_MAX_RESULTS) ret = xzalloc((WARMC_RET_SCRATCH + 2 * nr) * 8);
  rt_enter();
  u32 trap = warmc_invoke_catching_s(f->cell->code, (WarmInstance *)f->cell->instance,
                                     iargs, fargs, ret, stk, nstk * 8);
  rt_leave();
  if (stk != stk_small) free(stk);
  wasm_trap_t *t = NULL;
  if (trap) {
    t = trap_of(s, trap);
  } else if (nr == 1 && nres) {
    uint8_t k = ft->results.data[0]->kind;
    const u64 *at = is_float(k) ? &ret[WARMC_RET_V0] : &ret[WARMC_RET_X0];
    resv[0] = at[0];
    resv[1] = k == WARM_V128 ? at[1] : 0;
  } else {
    for (size_t i = 0; i < nr && i < nres; i++) {
      resv[2 * i] = ret[WARMC_RET_MULTI(i)];
      resv[2 * i + 1] = ft->results.data[i]->kind == WARM_V128 ? ret[WARMC_RET_MULTI(i) + 1] : 0;
    }
  }
  if (ret != ret_small) free(ret);
  return t;
}

own wasm_trap_t *wasm_func_call(const wasm_func_t *fn, const wasm_val_vec_t *args,
                                wasm_val_vec_t *results) {
  const FuncEnt *f = (const FuncEnt *)fn;
  wasm_store_t *s = f->e.store;
  const wasm_functype_t *ft = f->type;
  size_t np = ft->params.size, nr = ft->results.size;
  if (!args || args->size < np) return bad_call(s, "too few arguments");
  if (f->host) return call_host(f->host, args, results);
  if (f->native) return bad_call(s, "a WASI function, which only the program importing it calls");
  /* results past what the vector has room for are dropped: a call made
     only to see it trap may pass none */
  size_t room = results ? results->size : 0;
  if (room > nr) room = nr;
  u64 small[64];
  u64 *argv = 2 * (np + nr) <= 64 ? small : xzalloc(16 * (np + nr));
  u64 *resv = argv + 2 * np;
  wasm_trap_t *t = NULL;
  for (size_t i = 0; i < np && !t; i++) {
    uint8_t k = ft->params.data[i]->kind;
    argv[2 * i + 1] = 0;
    if (k == WARM_V128) t = bad_call(s, "a v128 argument, which wasm_val_t cannot hold");
    else if (!to_raw(s, &args->data[i], k, &argv[2 * i]))
      t = bad_call(s, "an argument of the wrong type");
  }
  for (size_t i = 0; i < room && !t; i++)
    if (ft->results.data[i]->kind == WARM_V128)
      t = bad_call(s, "a v128 result, which wasm_val_t cannot hold");
  if (!t) t = invoke(f, argv, resv, room);
  for (size_t i = 0; i < room && !t; i++)
    from_raw(s, resv[2 * i], ft->results.data[i]->kind, &results->data[i]);
  if (argv != small) free(argv);
  return t;
}

own wasm_trap_t *warm_func_call_raw(const wasm_func_t *fn, warm_val_raw_t *vals,
                                    size_t nvals) {
  const FuncEnt *f = (const FuncEnt *)fn;
  wasm_store_t *s = f->e.store;
  const wasm_functype_t *ft = f->type;
  size_t np = ft->params.size, nr = ft->results.size;
  if (nvals < np || nvals < nr) return bad_call(s, "too few slots for the call");
  if (f->host || f->native) {
    /* through the vectors: a host function takes wasm_val_t */
    wasm_val_vec_t a, r;
    wasm_val_vec_new_uninitialized(&a, np);
    wasm_val_vec_new_uninitialized(&r, nr);
    for (size_t i = 0; i < np; i++) {
      uint8_t k = ft->params.data[i]->kind;
      a.data[i].kind = k;
      if (wasm_valkind_is_ref(k)) a.data[i].of.ref = vals[i].ref;
      else memcpy(&a.data[i].of, &vals[i], 8);
    }
    wasm_trap_t *t = wasm_func_call(fn, &a, &r);
    for (size_t i = 0; i < nr && !t; i++) {
      if (wasm_valkind_is_ref(r.data[i].kind)) vals[i].ref = r.data[i].of.ref;
      else memcpy(&vals[i], &r.data[i].of, 8);
    }
    free(a.data);
    free(r.data);
    return t;
  }
  u64 small[64];
  u64 *argv = 2 * (np + nr) <= 64 ? small : xzalloc(16 * (np + nr));
  u64 *resv = argv + 2 * np;
  for (size_t i = 0; i < np; i++) {
    uint8_t k = ft->params.data[i]->kind;
    memcpy(&argv[2 * i], &vals[i], 16);
    if (wasm_valkind_is_ref(k)) {
      wasm_val_t v;
      v.kind = k;
      v.of.ref = vals[i].ref;
      if (!to_raw(s, &v, k, &argv[2 * i])) {
        if (argv != small) free(argv);
        return bad_call(s, "an argument of the wrong type");
      }
      argv[2 * i + 1] = 0;
    }
  }
  wasm_trap_t *t = invoke(f, argv, resv, nr);
  for (size_t i = 0; i < nr && !t; i++) {
    uint8_t k = ft->results.data[i]->kind;
    memset(&vals[i], 0, sizeof vals[i]);
    if (wasm_valkind_is_ref(k)) {
      wasm_val_t v;
      from_raw(s, resv[2 * i], k, &v);
      vals[i].ref = v.of.ref;
    } else {
      memcpy(&vals[i], &resv[2 * i], k == WARM_V128 ? 16 : 8);
    }
  }
  if (argv != small) free(argv);
  return t;
}

/* A host function called from wasm: warm_host_thunk keeps the argument
   registers and calls this with them, its HostFunc the context the call
   passed as the instance.  Results go back in x0 / d0 for one, and to the
   area x8 points at for several, as wasm returns them. */
/* used: only the thunk's asm calls it, which LTO does not see */
__attribute__((used))
void warm_host_dispatch(HostFunc *h, const u64 *gpr, const u64 *fpr,
                        const u8 *stk, u64 *ret) {
  wasm_store_t *s = h->store;
  const wasm_functype_t *ft = h->type;
  size_t np = ft->params.size, nr = ft->results.size;
  wasm_val_t small_a[8], small_r[8];
  wasm_val_t *a = np <= 8 ? small_a : xzalloc(np * sizeof(wasm_val_t));
  wasm_val_t *r = nr <= 8 ? small_r : xzalloc(nr * sizeof(wasm_val_t));
  u32 ni = 0, nf = 0, ns = 0;
  for (size_t i = 0; i < np; i++) {
    uint8_t k = ft->params.data[i]->kind;
    bool fl = is_float(k);
    u64 v;
    if (fl ? nf < NREG_FLT : ni < NREG_INT) v = fl ? fpr[2 * nf] : gpr[ni];
    else { memcpy(&v, stk + (size_t)STK_SLOT * ns, 8); ns++; }
    if (fl) nf++;
    else ni++;
    from_raw(s, v, k, &a[i]);
  }
  for (size_t i = 0; i < nr; i++) {
    r[i].kind = ft->results.data[i]->kind;
    r[i].of.i64 = 0;
  }
  wasm_val_vec_t av = { np, a }, rv = { nr, r };
  wasm_trap_t *t = call_host(h, &av, &rv);
  val_vec_delete_elems(a, np);
  if (a != small_a) free(a);
  u64 out[16];
  u64 *o = nr <= 16 ? out : xzalloc(nr * 8);
  bool bad = false;
  for (size_t i = 0; i < nr && !t; i++)
    if (!to_raw(s, &r[i], ft->results.data[i]->kind, &o[i])) bad = true;
  if (!t) val_vec_delete_elems(r, nr);
  if (r != small_r) free(r);
  if (!t && bad) {
    static const char why[] = "a host function returned a result of the wrong type";
    t = (wasm_trap_t *)trap_make(s, why, sizeof why - 1);
  }
  if (t) {
    if (o != out) free(o);
    g_host_trap = (TrapEnt *)t;
    warmc_trap(TRAP_HOST);
  }
  if (nr == 1) {
    if (is_float(ft->results.data[0]->kind)) ret[1] = o[0];
    else ret[0] = o[0];
  } else if (nr > 1) {
    u8 *area = (u8 *)(uptr)gpr[7];
    for (size_t i = 0; i < nr; i++) {
      memcpy(area + 16 * i, &o[i], 8);
      memset(area + 16 * i + 8, 0, 8);
    }
  }
  if (o != out) free(o);
}

#if defined(__aarch64__)
#if defined(__APPLE__)
#define CSYM(x) "_" #x
#else
#define CSYM(x) #x
#endif
/* x0 the HostFunc, x1-x7 and q0-q7 the arguments, the rest in sixteen-byte
   slots at sp, x8 where several results go: x1-x8 and q0-q7 to this frame,
   the slots' address, and two words for the result, which come back as x0
   and d0. */
__attribute__((naked)) void warm_host_thunk(void) {
  __asm__ volatile(
    "stp    x29, x30, [sp, #-224]!\n"
    "mov    x29, sp\n"
    "stp    x1, x2, [sp, #16]\n"
    "stp    x3, x4, [sp, #32]\n"
    "stp    x5, x6, [sp, #48]\n"
    "stp    x7, x8, [sp, #64]\n"
    "stp    q0, q1, [sp, #80]\n"
    "stp    q2, q3, [sp, #112]\n"
    "stp    q4, q5, [sp, #144]\n"
    "stp    q6, q7, [sp, #176]\n"
    "stp    xzr, xzr, [sp, #208]\n"
    "add    x1, sp, #16\n"
    "add    x2, sp, #80\n"
    "add    x3, sp, #224\n"
    "add    x4, sp, #208\n"
    "bl     " CSYM(warm_host_dispatch) "\n"
    "ldr    x0, [sp, #208]\n"
    "ldr    d0, [sp, #216]\n"
    "ldp    x29, x30, [sp], #224\n"
    "ret\n");
}
#elif defined(__x86_64__) && defined(_WIN32)
/* RCX the HostFunc, RDX, R8 and R9 the first integer arguments, XMM0-3 the
   first floats, the rest in slots 32 bytes above the return address, R10
   where several results go: RDX, R8, R9 and R10 to this frame as gpr[0..2]
   and gpr[7], where the dispatch looks for them as AArch64's x1-x3 and x8,
   XMM0-3 to it, the slots' address, and two words for the result, which
   come back as RAX and XMM0.  Every register the internal convention has a
   callee keep, Win64 has the dispatch keep too.  On entry RSP is 8 past a
   multiple of sixteen, as at every call wasm makes. */
__attribute__((naked)) void warm_host_thunk(void) {
  __asm__ volatile(
    "subq   $200, %rsp\n"
    "movq   %rdx, 48(%rsp)\n"
    "movq   %r8, 56(%rsp)\n"
    "movq   %r9, 64(%rsp)\n"
    "movq   %r10, 104(%rsp)\n"
    "movups %xmm0, 112(%rsp)\n"
    "movups %xmm1, 128(%rsp)\n"
    "movups %xmm2, 144(%rsp)\n"
    "movups %xmm3, 160(%rsp)\n"
    "movq   $0, 176(%rsp)\n"
    "movq   $0, 184(%rsp)\n"
    "leaq   176(%rsp), %rax\n"
    "movq   %rax, 32(%rsp)\n"
    "leaq   48(%rsp), %rdx\n"
    "leaq   112(%rsp), %r8\n"
    "leaq   240(%rsp), %r9\n"
    "callq  warm_host_dispatch\n"
    "movq   176(%rsp), %rax\n"
    "movq   184(%rsp), %xmm0\n"
    "addq   $200, %rsp\n"
    "retq\n");
}
#else
#error "warm_host_thunk: no thunk for this architecture yet"
#endif

/* ---- globals, tables, memories, tags: an instance's exports ------------- */

static ExtEnt *ext_new(wasm_store_t *s, uint8_t kind, WarmInstance *owner,
                       const char *name, size_t name_len, u32 index) {
  ExtEnt *x = ent_new(sizeof *x, kind, s);
  x->owner = owner;
  x->name = xstrdup_n(name, name_len);
  x->index = index;
  ent_pin(&x->e);
  return x;
}

static const char *global_wat_init(uint8_t k) {
  switch (k) {
    case WASM_I32: return "(i32.const 0)";
    case WASM_I64: return "(i64.const 0)";
    case WASM_F32: return "(f32.const 0)";
    case WASM_F64: return "(f64.const 0)";
    case WASM_EXTERNREF: return "(ref.null extern)";
    case WASM_FUNCREF: return "(ref.null func)";
  }
  return NULL;
}

own wasm_global_t *wasm_global_new(wasm_store_t *s, const wasm_globaltype_t *t,
                                   const wasm_val_t *init) {
  uint8_t k = t->content->kind;
  const char *zero = global_wat_init(k);
  u64 raw = 0;
  if (!zero || !to_raw(s, init, k, &raw)) return NULL;
  char wat[256];
  bool var = t->mutability == WASM_VAR;
  snprintf(wat, sizeof wat, "(module (global (export \"g\") %s%s%s %s))",
           var ? "(mut " : "", valkind_wat(k), var ? ")" : "", zero);
  WarmInstance *v = proxy_instance(s, wat, NULL);
  ExtEnt *x = ext_new(s, K_GLOBAL, v, "g", 1, 0);
  x->valkind = k;
  x->mutable_ = var;
  x->cell = warmc_global_cell(v, 0);
  x->cell[0] = raw;
  x->cell[1] = 0;
  return (wasm_global_t *)&x->e;
}

own wasm_globaltype_t *wasm_global_type(const wasm_global_t *g) {
  const ExtEnt *x = (const ExtEnt *)g;
  return wasm_globaltype_new(wasm_valtype_new(x->valkind),
                             x->mutable_ ? WASM_VAR : WASM_CONST);
}

void wasm_global_get(const wasm_global_t *g, own wasm_val_t *out) {
  const ExtEnt *x = (const ExtEnt *)g;
  rt_enter();
  u64 v = x->cell[0];
  rt_leave();
  from_raw(x->e.store, v, x->valkind, out);
}

void wasm_global_set(wasm_global_t *g, const wasm_val_t *v) {
  ExtEnt *x = (ExtEnt *)g;
  u64 raw;
  if (!x->mutable_ || !to_raw(x->e.store, v, x->valkind, &raw)) return;
  rt_enter();
  x->cell[0] = raw;
  rt_leave();
}

own wasm_table_t *wasm_table_new(wasm_store_t *s, const wasm_tabletype_t *t,
                                 wasm_ref_t *init) {
  uint8_t k = t->element->kind;
  if (k != WASM_FUNCREF && k != WASM_EXTERNREF) return NULL;
  wasm_val_t iv;
  iv.kind = k;
  iv.of.ref = init;
  u64 raw;
  if (!to_raw(s, &iv, k, &raw)) return NULL;
  char wat[256];
  if (t->limits.max != wasm_limits_max_default)
    snprintf(wat, sizeof wat, "(module (table (export \"t\") %u %u %s))",
             t->limits.min, t->limits.max, valkind_wat(k));
  else
    snprintf(wat, sizeof wat, "(module (table (export \"t\") %u %s))",
             t->limits.min, valkind_wat(k));
  WarmInstance *v = proxy_instance(s, wat, NULL);
  ExtEnt *x = ext_new(s, K_TABLE, v, "t", 1, 0);
  x->valkind = k;
  if (raw) {
    rt_enter();
    for (u32 i = 0; i < t->limits.min; i++) warmc_table_set(v, i, (void *)(uptr)raw, 0);
    rt_leave();
  }
  return (wasm_table_t *)&x->e;
}

own wasm_tabletype_t *wasm_table_type(const wasm_table_t *t) {
  const ExtEnt *x = (const ExtEnt *)t;
  u64 len = 0, max = 0;
  u32 flags = 0;
  rt_enter();
  warmc_table_view(x->owner, x->index, &len, &max, &flags);
  rt_leave();
  wasm_limits_t l = limits_of(len, max, flags & 1);
  return wasm_tabletype_new(wasm_valtype_new(x->valkind), &l);
}

own wasm_ref_t *wasm_table_get(const wasm_table_t *t, wasm_table_size_t i) {
  const ExtEnt *x = (const ExtEnt *)t;
  u64 len = 0, max = 0;
  u32 flags = 0;
  rt_enter();
  warmc_table_view(x->owner, x->index, &len, &max, &flags);
  u64 raw = i < len ? (u64)(uptr)warmc_table_get(x->owner, i, x->index) : 0;
  rt_leave();
  wasm_val_t v;
  from_raw(x->e.store, raw, x->valkind, &v);
  return v.of.ref;
}

bool wasm_table_set(wasm_table_t *t, wasm_table_size_t i, wasm_ref_t *r) {
  ExtEnt *x = (ExtEnt *)t;
  wasm_val_t v;
  v.kind = x->valkind;
  v.of.ref = r;
  u64 raw, len = 0, max = 0;
  u32 flags = 0;
  if (!to_raw(x->e.store, &v, x->valkind, &raw)) return false;
  rt_enter();
  warmc_table_view(x->owner, x->index, &len, &max, &flags);
  bool ok = i < len;
  if (ok) warmc_table_set(x->owner, i, (void *)(uptr)raw, x->index);
  rt_leave();
  return ok;
}

wasm_table_size_t wasm_table_size(const wasm_table_t *t) {
  const ExtEnt *x = (const ExtEnt *)t;
  u64 len = 0, max = 0;
  u32 flags = 0;
  rt_enter();
  warmc_table_view(x->owner, x->index, &len, &max, &flags);
  rt_leave();
  return len > UINT32_MAX ? UINT32_MAX : (wasm_table_size_t)len;
}

bool wasm_table_grow(wasm_table_t *t, wasm_table_size_t delta, wasm_ref_t *init) {
  ExtEnt *x = (ExtEnt *)t;
  wasm_val_t v;
  v.kind = x->valkind;
  v.of.ref = init;
  u64 raw;
  if (!to_raw(x->e.store, &v, x->valkind, &raw)) return false;
  rt_enter();
  u64 old = warmc_table_grow(x->owner, (void *)(uptr)raw, delta, x->index);
  rt_leave();
  return old != (u64)-1;
}

own wasm_memory_t *wasm_memory_new(wasm_store_t *s, const wasm_memorytype_t *t) {
  char wat[256];
  if (t->limits.max != wasm_limits_max_default)
    snprintf(wat, sizeof wat, "(module (memory (export \"m\") %u %u))",
             t->limits.min, t->limits.max);
  else
    snprintf(wat, sizeof wat, "(module (memory (export \"m\") %u))", t->limits.min);
  WarmInstance *v = proxy_instance(s, wat, NULL);
  ExtEnt *x = ext_new(s, K_MEMORY, v, "m", 1, 0);
  return (wasm_memory_t *)&x->e;
}

typedef struct { u8 *base; u64 size, pages, max_pages, decl_max; u32 flags; } MemView;

static void *mem_view(const ExtEnt *x, MemView *m) {
  return warmc_memory_view(x->owner, x->index, &m->base, &m->size, &m->pages,
                           &m->max_pages, &m->decl_max, &m->flags);
}

own wasm_memorytype_t *wasm_memory_type(const wasm_memory_t *mem) {
  MemView m;
  mem_view((const ExtEnt *)mem, &m);
  wasm_limits_t l = limits_of(m.pages, m.decl_max, m.flags & 1);
  return wasm_memorytype_new(&l);
}

byte_t *wasm_memory_data(wasm_memory_t *mem) {
  MemView m;
  mem_view((const ExtEnt *)mem, &m);
  return (byte_t *)m.base;
}

size_t wasm_memory_data_size(const wasm_memory_t *mem) {
  MemView m;
  mem_view((const ExtEnt *)mem, &m);
  return (size_t)m.size;
}

wasm_memory_pages_t wasm_memory_size(const wasm_memory_t *mem) {
  MemView m;
  mem_view((const ExtEnt *)mem, &m);
  return m.pages > UINT32_MAX ? UINT32_MAX : (wasm_memory_pages_t)m.pages;
}

bool wasm_memory_grow(wasm_memory_t *mem, wasm_memory_pages_t delta) {
  ExtEnt *x = (ExtEnt *)mem;
  rt_enter();
  u64 old = warmc_memory_grow(x->owner, delta, x->index);
  rt_leave();
  return old != (u64)-1;
}

/* ---- externs ------------------------------------------------------------------ */

wasm_externkind_t wasm_extern_kind(const wasm_extern_t *e) {
  switch (((const Ent *)e)->kind) {
    case K_FUNC: return WASM_EXTERN_FUNC;
    case K_GLOBAL: return WASM_EXTERN_GLOBAL;
    case K_TABLE: return WASM_EXTERN_TABLE;
    case K_MEMORY: return WASM_EXTERN_MEMORY;
    default: return WASM_EXTERN_TAG;
  }
}

own wasm_externtype_t *wasm_extern_type(const wasm_extern_t *e) {
  const Ent *x = (const Ent *)e;
  switch (x->kind) {
    case K_FUNC: return &wasm_func_type((const wasm_func_t *)e)->x;
    case K_GLOBAL: return &wasm_global_type((const wasm_global_t *)e)->x;
    case K_TABLE: return &wasm_table_type((const wasm_table_t *)e)->x;
    case K_MEMORY: return &wasm_memory_type((const wasm_memory_t *)e)->x;
    default: return &wasm_tagtype_copy(((const ExtEnt *)e)->tagtype)->x;
  }
}

#define EXTERN_CASTS(name, KIND)                                                \
  wasm_extern_t *wasm_##name##_as_extern(wasm_##name##_t *x) {                  \
    return (wasm_extern_t *)x;                                                  \
  }                                                                             \
  const wasm_extern_t *wasm_##name##_as_extern_const(const wasm_##name##_t *x) {\
    return (const wasm_extern_t *)x;                                            \
  }                                                                             \
  wasm_##name##_t *wasm_extern_as_##name(wasm_extern_t *x) {                    \
    return x && ((Ent *)x)->kind == KIND ? (wasm_##name##_t *)x : NULL;         \
  }                                                                             \
  const wasm_##name##_t *wasm_extern_as_##name##_const(const wasm_extern_t *x) {\
    return x && ((const Ent *)x)->kind == KIND ? (const wasm_##name##_t *)x     \
                                               : NULL;                          \
  }
EXTERN_CASTS(func, K_FUNC)
EXTERN_CASTS(global, K_GLOBAL)
EXTERN_CASTS(table, K_TABLE)
EXTERN_CASTS(memory, K_MEMORY)

VEC_OWNED(extern)

/* ---- instances ------------------------------------------------------------------ */

static const char *kind_name(uint8_t k) {
  switch (k) {
    case JIT_EXT_FUNC: return "function";
    case JIT_EXT_TABLE: return "table";
    case JIT_EXT_MEMORY: return "memory";
    case JIT_EXT_GLOBAL: return "global";
    default: return "tag";
  }
}

static uint8_t ent_extkind(const Ent *e) {
  switch (e->kind) {
    case K_FUNC: return JIT_EXT_FUNC;
    case K_TABLE: return JIT_EXT_TABLE;
    case K_MEMORY: return JIT_EXT_MEMORY;
    case K_GLOBAL: return JIT_EXT_GLOBAL;
    default: return JIT_EXT_TAG;
  }
}

static char *msgf(const char *fmt, const JitExtern *x, const char *more) {
  size_t n = strlen(fmt) + x->module_len + x->name_len + strlen(more) + 64;
  char *s = malloc(n);
  if (!s) oom();
  snprintf(s, n, fmt, x->module, x->name, more);
  return s;
}

own wasm_instance_t *warm_instance_new(wasm_store_t *s, const wasm_module_t *module,
                                       const wasm_extern_vec_t *imports,
                                       own wasm_trap_t **trap,
                                       own wasm_message_t *error_out) {
  if (trap) *trap = NULL;
  ModuleRec *mr = ((const ModuleEnt *)module)->mod;
  const JitModule *jm = mr->jm;
  const ModuleDesc *md = mr->md;
  WarmBinding *b = xzalloc((jm->nimports ? jm->nimports : 1) * sizeof *b);
  char *why = NULL;
  for (uint32_t i = 0; i < jm->nimports && !why; i++) {
    const JitExtern *x = &jm->imports[i];
    const Ent *e = imports && i < imports->size ? (const Ent *)imports->data[i] : NULL;
    if (!e) { why = msgf("no import given for \"%s\".\"%s\"%s", x, ""); break; }
    if (ent_extkind(e) != x->kind) {
      why = msgf("import \"%s\".\"%s\" is a %s, and was given another kind", x,
                 kind_name(x->kind));
      break;
    }
    if (x->kind == JIT_EXT_FUNC) {
      const FuncEnt *f = (const FuncEnt *)e;
      /* a host function's type is wasm.h's, which has nothing a type
         could differ in but the kinds; a wasm function the runtime checks
         when it is an export, and is otherwise of the import's very type */
      bool ok = f->host || f->native ? functype_eq(f->type, &x->ft)
                : f->owner || f->cell->type_id == md->imports[i].type_id;
      if (!ok) {
        why = msgf("function import \"%s\".\"%s\" is of another type%s", x, "");
        break;
      }
      if (f->owner) {
        b[i].inst = f->owner;
        b[i].name = f->export_name;
      } else if (f->native) {
        b[i].code = f->native;
        b[i].self = 1;
      } else if (f->host) {
        b[i].code = (void *)warm_host_thunk;
        b[i].inst = (WarmInstance *)f->host;
      } else {
        b[i].code = f->cell->code;
        b[i].inst = (WarmInstance *)f->cell->instance;
      }
    } else {
      const ExtEnt *xe = (const ExtEnt *)e;
      b[i].inst = xe->owner;
      b[i].name = xe->name;
    }
  }
  if (why) {
    set_message(error_out, why);
    free(why);
    free(b);
    return NULL;
  }
  u32 err = 0;
  WarmInstance *partial = NULL;
  rt_enter();
  WarmInstance *v = warmc_instantiate_bound_catching_p(md, b, &err, &partial);
  rt_leave();
  free(b);
  /* what a failed instantiation left behind is the store's, as what it
     wrote into the store's other instances may still call it */
  if (partial) rt_inst_add(s, partial, mr);
  if (!v) {
    if (err == TRAP_LINK) {
      set_message(error_out, "an import's type does not match what the module imports");
    } else {
      wasm_trap_t *t = trap_of(s, err);
      if (error_out) {
        wasm_trap_message(t, error_out);
      }
      if (trap) *trap = t;
      else wasm_trap_delete(t);
    }
    return NULL;
  }
  if (error_out) wasm_byte_vec_new_empty(error_out);
  InstanceEnt *in = ent_new(sizeof *in, K_INSTANCE, s);
  in->vm = v;
  in->mod = module_retain(mr);
  in->slot = rt_inst_add(s, v, mr);
  in->sealed = true;
  for (uint32_t i = 0; i < jm->nimports && in->sealed; i++) {
    const JitExtern *x = &jm->imports[i];
    if (x->kind == JIT_EXT_TABLE || x->kind == JIT_EXT_TAG ||
        (x->kind == JIT_EXT_GLOBAL && wasm_valkind_is_ref(x->valkind)))
      in->sealed = false;
    for (uint32_t k = 0; x->kind == JIT_EXT_FUNC && k < x->ft.nparams + x->ft.nresults; k++)
      if (wasm_valkind_is_ref(x->ft.types[k])) in->sealed = false;
  }
  map_put(&s->inst_ents, (u64)(uptr)v, 0, in);
  ent_pin(&in->e);
  return (wasm_instance_t *)&in->e;
}

own wasm_instance_t *wasm_instance_new(wasm_store_t *s, const wasm_module_t *module,
                                       const wasm_extern_vec_t *imports,
                                       own wasm_trap_t **trap) {
  return warm_instance_new(s, module, imports, trap, NULL);
}

static Ent *export_ent(InstanceEnt *in, uint32_t i) {
  const JitExtern *x = &in->mod->jm->exports[i];
  wasm_store_t *s = in->e.store;
  const ModuleDesc *md = in->mod->md;
  const ExportDesc *ed = i < md->nexport && md->exports[i].name_len == x->name_len &&
                         !memcmp(md->exports[i].name, x->name, x->name_len)
                         ? &md->exports[i]
                         : warmc_find_export_n(in->vm, x->name, x->name_len);
  if (!ed) return NULL;
  switch (x->kind) {
    case JIT_EXT_FUNC: {
      FuncRef *c = warmc_funcref_cell(in->vm, ed->index);
      Ent *e = func_ent_of_cell(s, c);
      FuncEnt *f = (FuncEnt *)e;
      if (!f->host && !type_by_id(c->type_id)) {
        wasm_functype_delete(f->type);
        f->type = functype_of(&x->ft);
      }
      if (!f->host && !f->native && !f->owner) {
        f->owner = in->vm;
        f->export_name = xstrdup_n(x->name, x->name_len);
      }
      return e;
    }
    case JIT_EXT_GLOBAL: {
      ExtEnt *g = ext_new(s, K_GLOBAL, in->vm, x->name, x->name_len, ed->index);
      g->valkind = x->valkind;
      g->mutable_ = x->mutable_;
      g->cell = warmc_global_cell(in->vm, ed->index);
      return &g->e;
    }
    case JIT_EXT_TABLE: {
      ExtEnt *t = ext_new(s, K_TABLE, in->vm, x->name, x->name_len, ed->index);
      t->valkind = x->valkind;
      return &t->e;
    }
    case JIT_EXT_MEMORY:
      return &ext_new(s, K_MEMORY, in->vm, x->name, x->name_len, ed->index)->e;
    default: {
      ExtEnt *t = ext_new(s, K_TAG, in->vm, x->name, x->name_len, ed->index);
      t->tagtype = wasm_tagtype_new(functype_of(&x->ft));
      return &t->e;
    }
  }
}

void wasm_instance_exports(const wasm_instance_t *inst, own wasm_extern_vec_t *out) {
  InstanceEnt *in = (InstanceEnt *)inst;
  uint32_t n = in->mod->jm->nexports;
  in->escaped = true;
  if (!in->exports) {
    in->exports = xzalloc((n ? n : 1) * sizeof(Ent *));
    for (uint32_t i = 0; i < n; i++) {
      in->exports[i] = export_ent(in, i);
      /* the instance's own count of it, which the store's pin stands for */
      if (in->exports[i]) in->exports[i]->refs--;
    }
  }
  wasm_extern_vec_new_uninitialized(out, n);
  for (uint32_t i = 0; i < n; i++)
    out->data[i] = (wasm_extern_t *)ent_ref(in->exports[i]);
}

/* ---- WASI preview 1 --------------------------------------------------------
   rt/wasi.c's host, the one `warmc --wasi` links: each function a C one
   called with the instance that imports it, which it reads the arguments'
   memory of.  The types preview1 gives them are rt/wasi.wat's, which the
   library carries as text (build/jit/wasiwat.c) and compiles once per
   engine, as the type an import must have to be bound. */

extern const char warmc_jit_wasi_wat[];
int warmc_wasi_jit_setup(int argc, char **argv, char **env, int nenv,
                         const char *const *dirs, int ndirs,
                         const char *const *listen, int nlisten, const char **bad);
void *warmc_wasi_jit_fn(const char *name, u32 len);
extern void (*warmc_wasi_exit_hook)(int code);
#ifdef __APPLE__
#include <crt_externs.h>
#define host_environ (*_NSGetEnviron())
#elif defined(_WIN32)
#define host_environ _environ
#else
extern char **environ;
#define host_environ environ
#endif

struct warm_wasi_config_t {
  char **argv; size_t argc;
  char **env; size_t nenv;
  bool inherit_env;
  char **dirs; size_t ndirs;
  char **listen; size_t nlisten;
};

own warm_wasi_config_t *warm_wasi_config_new(void) {
  return xzalloc(sizeof(warm_wasi_config_t));
}

static void strv_free(char **v, size_t n) {
  for (size_t i = 0; i < n; i++) free(v[i]);
  free(v);
}

static void strv_add(char ***v, size_t *n, const char *s) {
  *v = realloc(*v, (*n + 2) * sizeof(char *));
  if (!*v) oom();
  (*v)[(*n)++] = xstrdup_n(s, strlen(s));
  (*v)[*n] = NULL;
}

void warm_wasi_config_delete(own warm_wasi_config_t *c) {
  if (!c) return;
  strv_free(c->argv, c->argc);
  strv_free(c->env, c->nenv);
  strv_free(c->dirs, c->ndirs);
  strv_free(c->listen, c->nlisten);
  free(c);
}

void warm_wasi_config_tcplisten(warm_wasi_config_t *c, const char *addr) {
  strv_add(&c->listen, &c->nlisten, addr);
}

void warm_wasi_config_set_argv(warm_wasi_config_t *c, size_t argc,
                               const char *const argv[]) {
  strv_free(c->argv, c->argc);
  c->argv = NULL;
  c->argc = 0;
  for (size_t i = 0; i < argc; i++) strv_add(&c->argv, &c->argc, argv[i]);
}

void warm_wasi_config_env_add(warm_wasi_config_t *c, const char *entry) {
  strv_add(&c->env, &c->nenv, entry);
}

void warm_wasi_config_inherit_env(warm_wasi_config_t *c) { c->inherit_env = true; }

bool warm_wasi_config_preopen_dir(warm_wasi_config_t *c, const char *host,
                                  const char *guest) {
  if (!host || !*host || strstr(host, "::")) return false;
  size_t n = strlen(host) + (guest ? strlen(guest) + 2 : 0) + 1;
  char *spec = malloc(n);
  if (!spec) oom();
  snprintf(spec, n, "%s%s%s", host, guest ? "::" : "", guest ? guest : "");
  strv_add(&c->dirs, &c->ndirs, spec);
  free(spec);
  return true;
}

/* The world rt/wasi.c serves now, whose strings it keeps pointers to */
static warm_wasi_config_t g_wasi_world;
static wasm_store_t *g_wasi_store;

static void wasi_exit(int code) {
  char msg[64];
  int n = snprintf(msg, sizeof msg, "exit status %d", code);
  TrapEnt *t = trap_make(g_wasi_store, msg, (size_t)n);
  t->is_exit = true;
  t->exit_status = code;
  g_host_trap = t;
  warmc_trap(TRAP_HOST);
}

/* A function of the WASI host, bound as one called with the instance that
   imports it */
static Ent *wasi_func(wasm_store_t *s, void *fn, const JitFuncType *ft) {
  FuncEnt *f = ent_new(sizeof *f, K_FUNC, s);
  f->native = fn;
  f->type = functype_of(ft);
  ent_pin(&f->e);
  return &f->e;
}

bool warm_wasi_imports(wasm_store_t *s, const wasm_module_t *module,
                       const warm_wasi_config_t *c, own wasm_extern_vec_t *out) {
  const JitModule *jm = jm_of(module);
  ModuleRec *sig = proxy_module(s->engine, warmc_jit_wasi_wat);

  /* the world, copied: the caller may delete its config */
  warm_wasi_config_t w;
  memset(&w, 0, sizeof w);
  for (size_t i = 0; i < c->argc; i++) strv_add(&w.argv, &w.argc, c->argv[i]);
  if (!w.argv) strv_add(&w.argv, &w.argc, "main");
  if (c->inherit_env)
    for (char **e = host_environ; e && *e; e++) strv_add(&w.env, &w.nenv, *e);
  for (size_t i = 0; i < c->nenv; i++) strv_add(&w.env, &w.nenv, c->env[i]);
  for (size_t i = 0; i < c->ndirs; i++) strv_add(&w.dirs, &w.ndirs, c->dirs[i]);
  for (size_t i = 0; i < c->nlisten; i++) strv_add(&w.listen, &w.nlisten, c->listen[i]);
  rt_enter();
  const char *bad = NULL;
  int ok = warmc_wasi_jit_setup((int)w.argc, w.argv, w.env, (int)w.nenv,
                                (const char *const *)w.dirs, (int)w.ndirs,
                                (const char *const *)w.listen, (int)w.nlisten, &bad);
  if (ok) {
    strv_free(g_wasi_world.argv, g_wasi_world.argc);
    strv_free(g_wasi_world.env, g_wasi_world.nenv);
    strv_free(g_wasi_world.dirs, g_wasi_world.ndirs);
    strv_free(g_wasi_world.listen, g_wasi_world.nlisten);
    g_wasi_world = w;
    g_wasi_store = s;
    warmc_wasi_exit_hook = wasi_exit;
  }
  rt_leave();
  if (!ok) {
    strv_free(w.argv, w.argc);
    strv_free(w.env, w.nenv);
    strv_free(w.dirs, w.ndirs);
    strv_free(w.listen, w.nlisten);
    wasm_extern_vec_new_empty(out);
    return false;
  }

  wasm_extern_vec_new_uninitialized(out, jm->nimports);
  for (uint32_t i = 0; i < jm->nimports; i++) {
    const JitExtern *x = &jm->imports[i];
    if (x->kind != JIT_EXT_FUNC || x->module_len != 22 ||
        memcmp(x->module, "wasi_snapshot_preview1", 22))
      continue;
    void *fn = warmc_wasi_jit_fn(x->name, x->name_len);
    const JitExtern *want = NULL;
    for (uint32_t k = 0; k < sig->jm->nexports && !want; k++)
      if (sig->jm->exports[k].name_len == x->name_len &&
          !memcmp(sig->jm->exports[k].name, x->name, x->name_len))
        want = &sig->jm->exports[k];
    /* bound only at preview1's own type: the C function reads its
       arguments as that type has them */
    if (!fn || !want || want->ft.nparams != x->ft.nparams ||
        want->ft.nresults != x->ft.nresults ||
        memcmp(want->ft.types, x->ft.types, x->ft.nparams + x->ft.nresults))
      continue;
    out->data[i] = (wasm_extern_t *)wasi_func(s, fn, &want->ft);
  }
  return true;
}
