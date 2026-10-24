#include "clink.h"

#include "wat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { void *v; uint32_t n, cap; size_t elem; } Vec;

static void vec_init(Vec *a, size_t elem) { a->v = NULL; a->n = a->cap = 0; a->elem = elem; }

static void *vec_push(Vec *a) {
  if (a->n == a->cap) {
    a->cap = a->cap ? a->cap * 2 : 8;
    a->v = xrealloc(a->v, a->cap * a->elem);
  }
  return (char *)a->v + (size_t)a->n++ * a->elem;
}

static void *vec_at(const Vec *a, uint32_t i) {
  return i < a->n ? (char *)a->v + (size_t)i * a->elem : NULL;
}

typedef struct {
  bool real;
  uint32_t inst;
  char **names; LinkDef *defs; uint32_t n;
} CoreView;

typedef struct {
  char **names;
  uint8_t *sorts;
  uint32_t *idx;
  uint32_t n;

  bool host;
  const char *iface;
} CompView;

bool clink_host_iface(const char *name) {
  return !strncmp(name, "wasi:", 5) && strstr(name, "@0.2.") != NULL;
}

/* rustc asks for wasi:io/streams@0.2.12 and tinygo for @0.2.0, and both export
   run at @0.2.0 today -- but the version on an export is no more fixed than
   the one on an import, so it is skipped here too. */
bool clink_is_command(const char *name) {
  return !strncmp(name, "wasi:cli/run@0.2.", 17) &&
         !strcmp(strchr(name + 17, '#') ? strchr(name + 17, '#') : "", "#run");
}

typedef struct Env Env;

typedef struct { const Component *c; const Env *defenv; } CompRef;

struct Env {
  const Env *outer;
  const Component *c;

  uint32_t serial;
  Vec mod;
  Vec comp;
  Vec cinst;
  Vec cfunc, ctab, cmem, cglob, ctag;
  Vec func;
  Vec inst;
};

typedef struct { const Component *c; uint32_t ty; } ResKey;

typedef struct {
  LinkPlan *p;
  Vec cviews;
  Vec iviews;
  Vec restypes;
  uint32_t nwalks;
} Builder;

static uint32_t res_rt_id(Builder *b, const Component *c, uint32_t ty) {
  for (uint32_t i = 0; i < b->restypes.n; i++) {
    const ResKey *k = vec_at(&b->restypes, i);
    if (k->c == c && k->ty == ty) return i;
  }
  ResKey *k = vec_push(&b->restypes);
  k->c = c;
  k->ty = ty;
  return b->restypes.n - 1;
}

static void env_init(Env *e, const Env *outer, const Component *c) {
  memset(e, 0, sizeof *e);
  e->outer = outer;
  e->c = c;
  vec_init(&e->mod, sizeof(uint32_t));
  vec_init(&e->comp, sizeof(CompRef));
  vec_init(&e->cinst, sizeof(uint32_t));
  vec_init(&e->cfunc, sizeof(LinkDef));
  vec_init(&e->ctab, sizeof(LinkDef));
  vec_init(&e->cmem, sizeof(LinkDef));
  vec_init(&e->cglob, sizeof(LinkDef));
  vec_init(&e->ctag, sizeof(LinkDef));
  vec_init(&e->func, sizeof(uint32_t));
  vec_init(&e->inst, sizeof(uint32_t));
}

static void env_free(Env *e) {
  xfree(e->mod.v); xfree(e->comp.v); xfree(e->cinst.v);
  xfree(e->cfunc.v); xfree(e->ctab.v); xfree(e->cmem.v);
  xfree(e->cglob.v); xfree(e->ctag.v);
  xfree(e->func.v); xfree(e->inst.v);
}

static Vec *core_space(Env *e, uint8_t core_sort) {
  switch (core_sort) {
    case CMCS_FUNC:   return &e->cfunc;
    case CMCS_TABLE:  return &e->ctab;
    case CMCS_MEMORY: return &e->cmem;
    case CMCS_GLOBAL: return &e->cglob;
    case CMCS_TAG:    return &e->ctag;
    default:          return NULL;
  }
}

static ExternKind sort_extern(uint8_t core_sort) {
  switch (core_sort) {
    case CMCS_TABLE:  return EK_TABLE;
    case CMCS_MEMORY: return EK_MEM;
    case CMCS_GLOBAL: return EK_GLOBAL;
    case CMCS_TAG:    return EK_TAG;
    default:          return EK_FUNC;
  }
}

static void blocked(Builder *b, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  LinkPlan *p = b->p;
  for (uint32_t i = 0; i < p->nblockers; i++)
    if (!strcmp(p->blockers[i], buf)) return;
  p->blockers = xrealloc(p->blockers, (p->nblockers + 1) * sizeof *p->blockers);
  p->blockers[p->nblockers++] = xstrndup(buf, strlen(buf));
}

static LinkDef unresolved(const char *why, ExternKind sort) {
  LinkDef d;
  memset(&d, 0, sizeof d);
  d.kind = LD_UNRESOLVED;
  d.sort = sort;
  d.why = why;
  return d;
}

static bool core_view_lookup(Builder *b, const CoreView *v, const char *name,
                             LinkDef *out) {
  if (!v->real) {
    for (uint32_t i = 0; i < v->n; i++)
      if (!strcmp(v->names[i], name)) { *out = v->defs[i]; return true; }
    return false;
  }
  const LinkInst *in = &b->p->insts[v->inst];
  const Module *m = b->p->modules[in->module].m;
  for (uint32_t i = 0; i < m->nexports; i++)
    if (!strcmp(m->exports[i].name, name)) {
      memset(out, 0, sizeof *out);
      out->kind = LD_EXPORT;
      out->sort = m->exports[i].kind;
      out->inst = v->inst;
      out->exp = i;
      return true;
    }
  return false;
}

static CoreView *core_view(Builder *b, Env *e, uint32_t i) {
  uint32_t *slot = vec_at(&e->cinst, i);
  return slot ? vec_at(&b->cviews, *slot) : NULL;
}

static CompView *comp_view(Builder *b, Env *e, uint32_t i) {
  uint32_t *slot = vec_at(&e->inst, i);
  return slot ? vec_at(&b->iviews, *slot) : NULL;
}

static CompView walk(Builder *b, Env *e, const CompView *args);

static LinkDef resolve_core(Builder *b, Env *e, const CmSortIdx *si) {
  Vec *sp = core_space(e, si->core_sort);
  LinkDef *d = sp ? vec_at(sp, si->index) : NULL;
  if (!d) return unresolved("core definition out of scope", sort_extern(si->core_sort));
  return *d;
}

static LinkDef adapter_bind(const LinkPlan *p, const LinkCanon *lc, uint8_t kind) {
  const LinkCanon *li = &p->canons[p->funcs[lc->func].canon];
  switch (kind) {
    case CAI_CALLER_MEM:     return lc->memory;
    case CAI_CALLEE_MEM:     return li->memory;
    case CAI_CALLER_REALLOC: return lc->realloc;
    case CAI_CALLEE_REALLOC: return li->realloc;
    case CAI_CALLEE_FUNC:    return li->core;
    default:                 return li->post_return;
  }
}

static uint32_t adapter_export(const Module *m) {
  for (uint32_t i = 0; i < m->nexports; i++)
    if (m->exports[i].kind == EK_FUNC &&
        !strcmp(m->exports[i].name, CANON_ADAPTER_EXPORT))
      return i;
  warmc_fatal("adapter: the emitted module does not export \"%s\"",
              CANON_ADAPTER_EXPORT);
  return 0;
}

typedef struct { char *s; size_t n, cap; } Text;

static void textf(Text *t, const char *fmt, ...) {
  va_list ap;
  for (;;) {
    size_t room = t->cap - t->n;
    va_start(ap, fmt);
    int r = vsnprintf(t->s ? t->s + t->n : NULL, room, fmt, ap);
    va_end(ap);
    if (r >= 0 && (size_t)r < room) { t->n += (size_t)r; return; }
    t->cap = t->cap ? t->cap * 2 : 512;
    t->s = xrealloc(t->s, t->cap);
  }
}

static uint32_t add_module(Builder *b, char *text, const char *prefix,
                           uint32_t tag) {
  LinkPlan *p = b->p;
  size_t blen = 0;
  uint8_t *bin = wat_assemble(text, strlen(text), "<generated>", &blen);
  xfree(text);
  Module *m = wasm_parse(bin, blen);
  wasm_validate(m);

  p->modules = xrealloc(p->modules, (p->nmodules + 1) * sizeof *p->modules);
  LinkModule *lm = &p->modules[p->nmodules];
  memset(lm, 0, sizeof *lm);
  lm->m = m;
  lm->bytes = bin;
  snprintf(lm->prefix, sizeof lm->prefix, "%s%u", prefix, tag);
  return p->nmodules++;
}

static LinkDef host_helper(const char *name) {
  LinkDef d;
  memset(&d, 0, sizeof d);
  d.kind = LD_HOST;
  d.sort = EK_FUNC;
  d.inst = UINT32_MAX;
  d.host_name = name;
  return d;
}

static LinkDef bind_resource(Builder *b, const LinkDef *d) {
  LinkPlan *p = b->p;
  LinkCanon *lc = &p->canons[d->canon];

  if (lc->adapter_inst == UINT32_MAX) {
    Text w = { 0 };
    textf(&w, "(module\n");
    if (lc->res_op == 0) {
      textf(&w, "  (import \"warmc\" \"h\" (func $h (param i32 i32) (result i32)))\n"
                "  (func (export \"%s\") (param $rep i32) (result i32)\n"
                "    (call $h (i32.const %u) (local.get $rep)))\n",
            CANON_ADAPTER_EXPORT, lc->res_rt);
    } else if (lc->res_op == 2) {
      textf(&w, "  (import \"warmc\" \"h\" (func $h (param i32 i32) (result i32)))\n"
                "  (func (export \"%s\") (param $i i32) (result i32)\n"
                "    (call $h (i32.const %u) (local.get $i)))\n",
            CANON_ADAPTER_EXPORT, lc->res_rt);
    } else {
      textf(&w, "  (import \"warmc\" \"h\" (func $h (param i32 i32) (result i64)))\n");
      if (lc->has_dtor)
        textf(&w, "  (import \"dtor\" \"f\" (func $d (param i32)))\n");
      textf(&w, "  (func (export \"%s\") (param $i i32) (local $v i64)\n"
                "    (local.set $v (call $h (i32.const %u) (local.get $i)))\n",
            CANON_ADAPTER_EXPORT, lc->res_rt);
      if (lc->has_dtor)
        textf(&w, "    (if (i64.ne (i64.and (local.get $v)"
                  " (i64.const 0x100000000)) (i64.const 0))\n"
                  "      (then (call $d (i32.wrap_i64 (local.get $v)))))\n");
      textf(&w, "  )\n");
    }
    textf(&w, ")\n");

    uint32_t mi = add_module(b, w.s, "rs", d->canon);
    const Module *m = p->modules[mi].m;

    p->insts = xrealloc(p->insts, (p->ninsts + 1) * sizeof *p->insts);
    LinkInst *in = &p->insts[p->ninsts];
    memset(in, 0, sizeof *in);
    in->module = mi;
    in->nbinds = m->nimports;
    in->binds = xcalloc(in->nbinds ? in->nbinds : 1, sizeof *in->binds);
    in->binds[0] = host_helper(lc->res_op == 0 ? "warmc:resource#new"
                             : lc->res_op == 2 ? "warmc:resource#rep"
                                               : "warmc:resource#drop");
    if (in->nbinds > 1) in->binds[1] = lc->dtor;
    lc->adapter_inst = p->ninsts++;
  }

  LinkDef out;
  memset(&out, 0, sizeof out);
  out.kind = LD_EXPORT;
  out.sort = EK_FUNC;
  out.inst = lc->adapter_inst;
  out.exp = adapter_export(p->modules[p->insts[lc->adapter_inst].module].m);
  return out;
}

static LinkDef bind_adapter(Builder *b, const LinkDef *d) {
  LinkPlan *p = b->p;
  LinkCanon *lc = &p->canons[d->canon];

  if (lc->host) {
    LinkDef out;
    memset(&out, 0, sizeof out);
    out.kind = LD_HOST;
    out.sort = EK_FUNC;
    out.canon = d->canon;
    out.host_name = lc->host_name;
    out.inst = lc->memory.kind == LD_EXPORT ? lc->memory.inst : UINT32_MAX;
    return out;
  }

  if (lc->resource) return bind_resource(b, d);

  if (!lc->adapter) return *d;

  if (lc->adapter_inst == UINT32_MAX) {
    size_t blen = 0;
    uint8_t *bin = wat_assemble(lc->adapter, strlen(lc->adapter),
                                "<adapter>", &blen);
    Module *m = wasm_parse(bin, blen);
    wasm_validate(m);
    if (m->nimports != lc->aimports.n)
      warmc_fatal("adapter: emitted %u imports and asked to bind %u",
                  m->nimports, lc->aimports.n);

    p->modules = xrealloc(p->modules, (p->nmodules + 1) * sizeof *p->modules);
    LinkModule *lm = &p->modules[p->nmodules];
    memset(lm, 0, sizeof *lm);
    lm->m = m;
    lm->bytes = bin;
    snprintf(lm->prefix, sizeof lm->prefix, "ad%u", d->canon);
    uint32_t mi = p->nmodules++;

    p->insts = xrealloc(p->insts, (p->ninsts + 1) * sizeof *p->insts);
    LinkInst *in = &p->insts[p->ninsts];
    memset(in, 0, sizeof *in);
    in->module = mi;
    in->nbinds = m->nimports;
    in->binds = xcalloc(in->nbinds ? in->nbinds : 1, sizeof *in->binds);
    for (uint32_t k = 0; k < in->nbinds; k++)
      in->binds[k] = adapter_bind(p, lc, lc->aimports.kind[k]);
    lc->adapter_inst = p->ninsts++;
  }

  LinkDef out;
  memset(&out, 0, sizeof out);
  out.kind = LD_EXPORT;
  out.sort = EK_FUNC;
  out.inst = lc->adapter_inst;
  out.exp = adapter_export(p->modules[p->insts[lc->adapter_inst].module].m);
  return out;
}

static void instantiate_core(Builder *b, Env *e, const CmInstance *in, CoreView *out) {
  LinkPlan *p = b->p;
  uint32_t *mp = vec_at(&e->mod, in->index);
  memset(out, 0, sizeof *out);
  out->real = true;

  if (!mp || *mp == UINT32_MAX) {
    blocked(b, "a core module this component imports is instantiated");
    out->real = false;
    return;
  }
  const Module *m = p->modules[*mp].m;

  LinkDef *binds = xcalloc(m->nimports ? m->nimports : 1, sizeof *binds);
  for (uint32_t i = 0; i < m->nimports; i++) {
    const Import *im = &m->imports[i];
    const CoreView *from = NULL;
    for (uint32_t a = 0; a < in->nargs; a++)
      if (!strcmp(in->arg_names[a], im->module)) {
        from = core_view(b, e, in->args[a].index);
        break;
      }
    if (!from) {
      binds[i] = unresolved("no instance supplies it", im->kind);
      blocked(b, "core module import \"%s\" \"%s\" is unbound", im->module, im->name);
      continue;
    }
    if (!core_view_lookup(b, from, im->name, &binds[i])) {
      binds[i] = unresolved("the instance does not export it", im->kind);
      blocked(b, "core module import \"%s\" \"%s\" is unbound", im->module, im->name);
      continue;
    }
    if (binds[i].kind == LD_CANON && !p->canons[binds[i].canon].builtin)
      binds[i] = bind_adapter(b, &binds[i]);
    if (binds[i].kind == LD_EXPORT && binds[i].sort != im->kind) {
      binds[i] = unresolved("the export is of another sort", im->kind);
      blocked(b, "core module import \"%s\" \"%s\" is bound to another sort",
              im->module, im->name);
    }
  }

  p->insts = xrealloc(p->insts, (p->ninsts + 1) * sizeof *p->insts);
  LinkInst *li = &p->insts[p->ninsts];
  memset(li, 0, sizeof *li);
  li->module = *mp;
  li->nbinds = m->nimports;
  li->binds = binds;
  out->inst = p->ninsts++;
}

static void plan_adapter(Builder *b, LinkCanon *lc, const CanonScope *at) {
  LinkPlan *p = b->p;

  if (lc->func >= p->nfuncs || p->funcs[lc->func].import) {
    blocked(b, "a lowered function the component imports has nothing to lift");
    return;
  }
  const LinkCanon *li = &p->canons[p->funcs[lc->func].canon];
  if (li->builtin || !li->lift || !li->ft || li->core.kind != LD_EXPORT) {
    blocked(b, "a lowered function is not a lift of a core function");
    return;
  }

  if (lc->cinst == li->cinst) {
    blocked(b, "a component instance lowers a lift of its own, which wasmtime "
               "traps as re-entrance and the reference implementation permits");
    return;
  }
  if (lc->opts.string_encoding != li->opts.string_encoding) {
    blocked(b, "an adapter whose two sides disagree on string encoding is a "
               "transcode, which is not emitted yet");
    return;
  }
  {
    CanonScope lift_top = { NULL, li->c, NULL };
    CanonCtx lcx = { lc->c, at, false, lc->opts.string_encoding,
                     lc->opts.string_encoding, 0, 0 };
    CanonCtx icx = { li->c, &lift_top, false, li->opts.string_encoding,
                     li->opts.string_encoding, 0, 0 };
    if (!canon_functype_same(&lcx, lc->ft, &icx, li->ft)) {
      blocked(b, "a lowered import is declared with a type the function it "
                 "binds to was not lifted with");
      return;
    }
  }

  CanonCtx dn = { lc->c, at, false, lc->opts.string_encoding,
                  li->opts.string_encoding, 0, 1 };
  CanonCtx up = { lc->c, at, false, li->opts.string_encoding,
                  lc->opts.string_encoding, 1, 0 };
  const char *why = canon_adapter_why_not(&dn, &up, lc->ft);
  if (why) { blocked(b, "an adapter would have to move %s", why); return; }

  if (lc->needs_memory &&
      (lc->memory.kind != LD_EXPORT || li->memory.kind != LD_EXPORT ||
       lc->realloc.kind != LD_EXPORT || li->realloc.kind != LD_EXPORT)) {
    blocked(b, "an adapter needs a memory and a realloc on both of its sides");
    return;
  }
  if (li->opts.has_post_return && li->post_return.kind != LD_EXPORT) {
    blocked(b, "a post-return is not a core function this plan resolved");
    return;
  }
  lc->adapter = canon_adapter_text(&dn, &up, lc->ft, li->opts.has_post_return,
                                   &lc->aimports);
}

static void host_resource(Builder *b, Env *e, uint32_t ty, const char **iface,
                          const char **name) {
  *iface = NULL;
  *name = NULL;
  const Component *c = e->c;
  if (ty >= c->ntype_space) return;
  const CmTypeSlot *ts = &c->type_space[ty];
  if (ts->kind != CMTS_ALIAS || ts->index >= c->naliases) return;
  const CmAlias *a = &c->aliases[ts->index];
  if (a->kind != CMA_EXPORT || a->target.sort != CMS_TYPE || !a->name) return;
  const CompView *v = comp_view(b, e, a->instance);
  if (!v || !v->host) return;
  *iface = v->iface;
  *name = a->name;
}

static void do_canon(Builder *b, Env *e, uint32_t ci) {
  LinkPlan *p = b->p;
  const CmCanon *cn = &e->c->canons[ci];
  CanonScope top = { NULL, e->c, NULL };
  CanonCtx cx = { e->c, &top, false, CSE_UTF8, CSE_UTF8, 0, 0 };

  p->canons = xrealloc(p->canons, (p->ncanons + 1) * sizeof *p->canons);
  LinkCanon *lc = &p->canons[p->ncanons];
  memset(lc, 0, sizeof *lc);
  lc->c = e->c;
  lc->cinst = e->serial;
  lc->adapter_inst = UINT32_MAX;
  uint32_t self = p->ncanons++;

  if (cn->kind != CMC_LIFT && cn->kind != CMC_LOWER) {
    const char *iface = NULL, *res = NULL;
    if (cn->kind == CMC_RESOURCE_DROP)
      host_resource(b, e, cn->type, &iface, &res);
    if (iface) {
      char buf[512];
      snprintf(buf, sizeof buf, "[resource-drop]%s", res);
      lc->host = true;
      lc->host_iface = iface;
      lc->host_fn = xstrndup(buf, strlen(buf));
      snprintf(buf, sizeof buf, "%s#[resource-drop]%s", iface, res);
      lc->host_name = xstrndup(buf, strlen(buf));
      lc->sig.nparams = 1;
      lc->sig.params[0] = CF_I32;

      LinkDef d;
      memset(&d, 0, sizeof d);
      d.kind = LD_CANON;
      d.sort = EK_FUNC;
      d.canon = self;
      *(LinkDef *)vec_push(&e->cfunc) = d;
      return;
    }

    const CmType *rt = cn->kind == CMC_RESOURCE_NEW ||
                       cn->kind == CMC_RESOURCE_DROP ||
                       cn->kind == CMC_RESOURCE_REP
                         ? canon_type_in(&top, cn->type) : NULL;
    if (rt && rt->kind == CMT_RESOURCE) {
      lc->resource = true;
      lc->res_op = cn->kind == CMC_RESOURCE_NEW  ? 0
                 : cn->kind == CMC_RESOURCE_REP  ? 2 : 1;
      lc->res_rt = res_rt_id(b, e->c, cn->type);
      if (lc->res_op == 1 && rt->has_dtor) {
        CmSortIdx si;
        memset(&si, 0, sizeof si);
        si.sort = CMS_CORE;
        si.core_sort = CMCS_FUNC;
        si.index = rt->dtor;
        lc->dtor = resolve_core(b, e, &si);
        if (lc->dtor.kind != LD_EXPORT)
          blocked(b, "a resource destructor is not a core function this plan "
                     "resolved");
        else
          lc->has_dtor = true;
      }
      lc->sig.nparams = 1;
      lc->sig.params[0] = CF_I32;
      if (lc->res_op != 1) { lc->sig.nresults = 1; lc->sig.results[0] = CF_I32; }

      LinkDef d;
      memset(&d, 0, sizeof d);
      d.kind = LD_CANON;
      d.sort = EK_FUNC;
      d.canon = self;
      *(LinkDef *)vec_push(&e->cfunc) = d;
      return;
    }

    lc->builtin = true;
    lc->builtin_name = cn->kind == CMC_RESOURCE_NEW  ? "resource.new"
                     : cn->kind == CMC_RESOURCE_DROP ? "resource.drop"
                     : cn->kind == CMC_RESOURCE_REP  ? "resource.rep"
                     : "an async or threading built-in";
    blocked(b, "%s is not emitted", lc->builtin_name);

    LinkDef d;
    memset(&d, 0, sizeof d);
    d.kind = LD_CANON;
    d.sort = EK_FUNC;
    d.canon = self;
    *(LinkDef *)vec_push(&e->cfunc) = d;
    return;
  }

  lc->lift = cn->kind == CMC_LIFT;
  canon_options(cn, lc->lift, &lc->opts);

  CmSortIdx si;
  memset(&si, 0, sizeof si);
  si.sort = CMS_CORE;
  if (lc->opts.has_memory) {
    si.core_sort = CMCS_MEMORY; si.index = lc->opts.memory;
    lc->memory = resolve_core(b, e, &si);
    /* the canonical ABI is 32-bit here: a 64-bit memory is refused now,
       not at the start of a program whose adapter imports a 32-bit one */
    if (lc->memory.kind == LD_EXPORT) {
      const Module *mm = b->p->modules[b->p->insts[lc->memory.inst].module].m;
      const Export *x = &mm->exports[lc->memory.exp];
      if (x->kind == EK_MEM && x->index < mm->nmemories && mm->memories[x->index].index64) {
        blocked(b, "a canonical definition names a 64-bit memory, and the canonical ABI "
                   "here is 32-bit");
        return;
      }
    }
  }
  if (lc->opts.has_realloc) {
    si.core_sort = CMCS_FUNC; si.index = lc->opts.realloc;
    lc->realloc = resolve_core(b, e, &si);
  }
  if (lc->opts.has_post_return) {
    si.core_sort = CMCS_FUNC; si.index = lc->opts.post_return;
    lc->post_return = resolve_core(b, e, &si);
  }

  CanonScope at = top;
  if (lc->lift) {
    const CmType *t = canon_type_in(&top, cn->type);
    lc->ft = t && t->kind == CMT_FUNC ? &t->func : NULL;
    si.core_sort = CMCS_FUNC; si.index = cn->func;
    lc->core = resolve_core(b, e, &si);
  } else {
    lc->ft = canon_func_type_in(&top, cn->func, &at);
    uint32_t *fp = vec_at(&e->func, cn->func);
    lc->func = fp ? *fp : 0;
    if (!fp) blocked(b, "a lowered function is out of scope");
  }
  if (lc->ft) {
    cx.scope = &at;
    canon_flatten_functype(&cx, lc->ft, lc->lift, &lc->sig);
    lc->needs_memory = canon_needs_memory(&cx, lc->ft, lc->lift);
  } else {
    blocked(b, "a canonical definition's signature is not resolvable here");
  }

  if (!lc->lift && lc->ft) {
    const LinkFunc *lf = lc->func < p->nfuncs ? &p->funcs[lc->func] : NULL;
    if (lf && lf->host) {
      char buf[512];
      snprintf(buf, sizeof buf, "%s#%s", lf->host_iface, lf->impname);
      lc->host = true;
      lc->host_iface = lf->host_iface;
      lc->host_fn = lf->impname;
      lc->host_name = xstrndup(buf, strlen(buf));
    } else {
      plan_adapter(b, lc, &at);
    }
  }

  if (lc->lift) {
    p->funcs = xrealloc(p->funcs, (p->nfuncs + 1) * sizeof *p->funcs);
    LinkFunc *lf = &p->funcs[p->nfuncs];
    memset(lf, 0, sizeof *lf);
    lf->canon = self;
    *(uint32_t *)vec_push(&e->func) = p->nfuncs++;
  } else {
    LinkDef d;
    memset(&d, 0, sizeof d);
    d.kind = LD_CANON;
    d.sort = EK_FUNC;
    d.canon = self;
    *(LinkDef *)vec_push(&e->cfunc) = d;
  }
}

static const char *sort_word(const CmSortIdx *t) {
  if (t->sort != CMS_CORE) {
    switch (t->sort) {
      case CMS_FUNC:      return "function";
      case CMS_VALUE:     return "value";
      case CMS_TYPE:      return "type";
      case CMS_INSTANCE:  return "instance";
      case CMS_COMPONENT: return "component";
      default:            return "definition";
    }
  }
  switch (t->core_sort) {
    case CMCS_FUNC:     return "core function";
    case CMCS_TABLE:    return "core table";
    case CMCS_MEMORY:   return "core memory";
    case CMCS_GLOBAL:   return "core global";
    case CMCS_TAG:      return "core tag";
    case CMCS_TYPE:     return "core type";
    case CMCS_MODULE:   return "core module";
    case CMCS_INSTANCE: return "core instance";
    default:            return "core definition";
  }
}

static bool space_tracked(const CmSortIdx *t) {
  if (t->sort == CMS_CORE) return t->core_sort != CMCS_TYPE;
  return t->sort == CMS_FUNC || t->sort == CMS_INSTANCE ||
         t->sort == CMS_COMPONENT;
}

static void space_placeholder(Env *e, const CmSortIdx *t) {
  if (t->sort == CMS_CORE) {
    Vec *sp = core_space(e, t->core_sort);
    if (sp) {
      *(LinkDef *)vec_push(sp) = unresolved("this plan cannot follow it",
                                            sort_extern(t->core_sort));
      return;
    }
    if (t->core_sort == CMCS_MODULE)
      *(uint32_t *)vec_push(&e->mod) = UINT32_MAX;
    else if (t->core_sort == CMCS_INSTANCE)
      *(uint32_t *)vec_push(&e->cinst) = UINT32_MAX;
    return;
  }
  if (t->sort == CMS_FUNC) *(uint32_t *)vec_push(&e->func) = UINT32_MAX;
  else if (t->sort == CMS_INSTANCE) *(uint32_t *)vec_push(&e->inst) = UINT32_MAX;
  else if (t->sort == CMS_COMPONENT) {
    CompRef null_cr = { NULL, NULL };
    *(CompRef *)vec_push(&e->comp) = null_cr;
  }
}

static void do_alias(Builder *b, Env *e, const CmAlias *a) {
  const CmSortIdx *t = &a->target;
  switch (a->kind) {
    case CMA_CORE_EXPORT: {
      Vec *sp = core_space(e, t->core_sort);
      CoreView *v = core_view(b, e, a->instance);
      LinkDef d = unresolved("the core instance does not export it",
                             sort_extern(t->core_sort));
      if (v) core_view_lookup(b, v, a->name, &d);
      if (!sp) {

        blocked(b, "an alias names a core %s export", "type or module");
        return;
      }
      if (d.kind == LD_UNRESOLVED)
        blocked(b, "core instance export \"%s\" is missing", a->name);
      *(LinkDef *)vec_push(sp) = d;
      return;
    }
    case CMA_EXPORT: {
      CompView *v = comp_view(b, e, a->instance);
      if (t->sort == CMS_FUNC) {
        uint32_t fi = 0;
        bool got = false;
        if (v)
          for (uint32_t i = 0; i < v->n; i++)
            if (v->sorts[i] == CMS_FUNC && !strcmp(v->names[i], a->name)) {
              fi = v->idx[i]; got = true; break;
            }
        if (!got) {
          if (!v || !v->host)
            blocked(b, "instance export \"%s\" is not a function this plan has",
                    a->name);

          LinkPlan *p = b->p;
          p->funcs = xrealloc(p->funcs, (p->nfuncs + 1) * sizeof *p->funcs);
          LinkFunc *lf = &p->funcs[p->nfuncs];
          memset(lf, 0, sizeof *lf);
          lf->import = true;
          lf->impname = xstrndup(a->name, strlen(a->name));
          if (v && v->host) { lf->host = true; lf->host_iface = v->iface; }
          fi = p->nfuncs++;
        }
        *(uint32_t *)vec_push(&e->func) = fi;
      } else if (t->sort == CMS_INSTANCE) {

        uint32_t at = UINT32_MAX;
        if (v)
          for (uint32_t i = 0; i < v->n; i++)
            if (v->sorts[i] == CMS_INSTANCE && !strcmp(v->names[i], a->name)) {
              at = v->idx[i]; break;
            }
        if (at == UINT32_MAX) {
          CompView cv;
          memset(&cv, 0, sizeof cv);
          *(CompView *)vec_push(&b->iviews) = cv;
          at = b->iviews.n - 1;
          blocked(b, "instance export \"%s\" is not an instance this plan has",
                  a->name);
        }
        *(uint32_t *)vec_push(&e->inst) = at;
      } else if (t->sort == CMS_CORE && t->core_sort == CMCS_MODULE) {

        blocked(b, "instance export \"%s\" is a core module, which this plan "
                   "cannot alias out yet", a->name);
        *(uint32_t *)vec_push(&e->mod) = UINT32_MAX;
      } else if (t->sort == CMS_COMPONENT) {
        CompRef cr = { NULL, NULL };
        blocked(b, "instance export \"%s\" is a component, which this plan "
                   "cannot alias out yet", a->name);
        *(CompRef *)vec_push(&e->comp) = cr;
      } else if (space_tracked(t)) {
        blocked(b, "instance export \"%s\" is a %s, which this plan cannot "
                   "alias out", a->name, sort_word(t));
        space_placeholder(e, t);
      }

      return;
    }
    case CMA_OUTER: {
      const Env *o = e;
      for (uint32_t k = 0; k < a->count && o; k++) o = o->outer;
      if (!o) { blocked(b, "an outer alias climbs past the outermost scope"); return; }
      if (t->sort == CMS_CORE && t->core_sort == CMCS_MODULE) {
        uint32_t *m = vec_at((Vec *)&o->mod, t->index);
        if (!m) blocked(b, "an outer alias names a core module out of scope");
        *(uint32_t *)vec_push(&e->mod) = m ? *m : UINT32_MAX;
      } else if (t->sort == CMS_COMPONENT) {
        CompRef null_cr = { NULL, NULL };
        CompRef *cr = vec_at((Vec *)&o->comp, t->index);
        if (!cr) blocked(b, "an outer alias names a component out of scope");
        *(CompRef *)vec_push(&e->comp) = cr ? *cr : null_cr;
      } else if (space_tracked(t)) {
        blocked(b, "an outer alias names a %s, which this plan cannot follow",
                sort_word(t));
        space_placeholder(e, t);
      }

      return;
    }
  }
}

static void instantiate_component(Builder *b, Env *e, const CmInstance *in,
                                  CompView *out) {
  memset(out, 0, sizeof *out);
  CompRef *cr = vec_at(&e->comp, in->index);

  if (!cr || !cr->c) {
    blocked(b, "a component this component imports is instantiated");
    return;
  }

  CompView args;
  memset(&args, 0, sizeof args);
  if (in->nargs) {
    args.names = xcalloc(in->nargs, sizeof *args.names);
    args.sorts = xcalloc(in->nargs, sizeof *args.sorts);
    args.idx = xcalloc(in->nargs, sizeof *args.idx);
    for (uint32_t i = 0; i < in->nargs; i++) {
      args.names[args.n] = in->arg_names[i];
      args.sorts[args.n] = in->args[i].sort;
      if (in->args[i].sort == CMS_FUNC) {
        uint32_t *fp = vec_at(&e->func, in->args[i].index);
        args.idx[args.n] = fp ? *fp : 0;
      } else if (in->args[i].sort == CMS_INSTANCE) {

        uint32_t *ip = vec_at(&e->inst, in->args[i].index);
        args.idx[args.n] = ip ? *ip : 0;
        if (!ip) blocked(b, "an instance argument is out of scope");
      }
      args.n++;
    }
  }

  Env inner;
  env_init(&inner, cr->defenv, cr->c);
  *out = walk(b, &inner, &args);
  env_free(&inner);
  xfree(args.names); xfree(args.sorts); xfree(args.idx);
}

static CompView walk(Builder *b, Env *e, const CompView *args) {
  LinkPlan *p = b->p;
  const Component *c = e->c;
  CompView out;
  memset(&out, 0, sizeof out);
  e->serial = b->nwalks++;

  for (uint32_t k = 0; k < c->norder; k++) {
    const CmOrderEntry *o = &c->order[k];
    switch (o->kind) {
      case CMO_MODULE: {

        Module *m = c->modules[o->index];
        uint32_t at = p->nmodules;
        for (uint32_t j = 0; j < p->nmodules; j++)
          if (p->modules[j].m == m) { at = j; break; }
        if (at == p->nmodules) {
          p->modules = xrealloc(p->modules, (p->nmodules + 1) * sizeof *p->modules);
          LinkModule *lm = &p->modules[p->nmodules];
          memset(lm, 0, sizeof *lm);
          lm->m = m;
          snprintf(lm->prefix, sizeof lm->prefix, "cm%u", p->nmodules);
          p->nmodules++;
        }
        *(uint32_t *)vec_push(&e->mod) = at;
        break;
      }
      case CMO_COMPONENT: {
        CompRef cr = { c->components[o->index], e };
        *(CompRef *)vec_push(&e->comp) = cr;
        break;
      }
      case CMO_CORE_INSTANCE: {
        const CmInstance *in = &c->core_instances[o->index];
        CoreView v;
        memset(&v, 0, sizeof v);
        if (in->instantiate) {
          instantiate_core(b, e, in, &v);
        } else {
          v.real = false;
          v.n = in->nexports;
          v.names = xcalloc(v.n ? v.n : 1, sizeof *v.names);
          v.defs = xcalloc(v.n ? v.n : 1, sizeof *v.defs);
          for (uint32_t i = 0; i < in->nexports; i++) {
            v.names[i] = in->export_names[i];
            v.defs[i] = resolve_core(b, e, &in->exports[i]);
          }
        }
        *(CoreView *)vec_push(&b->cviews) = v;
        *(uint32_t *)vec_push(&e->cinst) = b->cviews.n - 1;
        break;
      }
      case CMO_INSTANCE: {
        const CmInstance *in = &c->instances[o->index];
        CompView v;
        if (in->instantiate) {
          instantiate_component(b, e, in, &v);
        } else {
          memset(&v, 0, sizeof v);
          v.n = in->nexports;
          v.names = xcalloc(v.n ? v.n : 1, sizeof *v.names);
          v.sorts = xcalloc(v.n ? v.n : 1, sizeof *v.sorts);
          v.idx = xcalloc(v.n ? v.n : 1, sizeof *v.idx);
          for (uint32_t i = 0; i < in->nexports; i++) {
            v.names[i] = in->export_names[i];
            v.sorts[i] = in->exports[i].sort;
            if (in->exports[i].sort == CMS_FUNC) {
              uint32_t *fp = vec_at(&e->func, in->exports[i].index);
              v.idx[i] = fp ? *fp : 0;
            } else if (in->exports[i].sort == CMS_INSTANCE) {
              uint32_t *ip = vec_at(&e->inst, in->exports[i].index);
              v.idx[i] = ip ? *ip : 0;
            }
          }
        }
        *(CompView *)vec_push(&b->iviews) = v;
        *(uint32_t *)vec_push(&e->inst) = b->iviews.n - 1;
        break;
      }
      case CMO_ALIAS:
        do_alias(b, e, &c->aliases[o->index]);
        break;
      case CMO_CANON:
        do_canon(b, e, o->index);
        break;
      case CMO_IMPORT: {
        const CmImport *im = &c->imports[o->index];

        const CompView *a = args;
        switch (im->et.kind) {
          case CME_FUNC: {
            uint32_t fi = 0;
            bool got = false;
            if (a)
              for (uint32_t i = 0; i < a->n; i++)
                if (a->sorts[i] == CMS_FUNC && !strcmp(a->names[i], im->name)) {
                  fi = a->idx[i]; got = true; break;
                }
            if (!got) {
              p->funcs = xrealloc(p->funcs, (p->nfuncs + 1) * sizeof *p->funcs);
              LinkFunc *lf = &p->funcs[p->nfuncs];
              memset(lf, 0, sizeof *lf);
              lf->import = true;
              lf->impname = xstrndup(im->name, strlen(im->name));
              fi = p->nfuncs++;
              blocked(b, "import \"%s\" has nothing to bind it to", im->name);
            }
            *(uint32_t *)vec_push(&e->func) = fi;
            break;
          }
          case CME_INSTANCE: {

            uint32_t at = UINT32_MAX;
            if (a)
              for (uint32_t k = 0; k < a->n; k++)
                if (a->sorts[k] == CMS_INSTANCE && !strcmp(a->names[k], im->name)) {
                  at = a->idx[k]; break;
                }
            if (at == UINT32_MAX) {

              CompView v;
              memset(&v, 0, sizeof v);
              if (clink_host_iface(im->name)) {
                v.host = true;
                v.iface = im->name;
              } else {
                blocked(b, "import \"%s\" is an instance no host here provides",
                        im->name);
              }
              *(CompView *)vec_push(&b->iviews) = v;
              at = b->iviews.n - 1;
            }
            *(uint32_t *)vec_push(&e->inst) = at;
            break;
          }
          case CME_CORE_MODULE:
            blocked(b, "import \"%s\" is a core module", im->name);
            *(uint32_t *)vec_push(&e->mod) = UINT32_MAX;
            break;
          case CME_COMPONENT: {
            CompRef cr = { NULL, NULL };
            blocked(b, "import \"%s\" is a component", im->name);
            *(CompRef *)vec_push(&e->comp) = cr;
            break;
          }
          default: break;
        }
        break;
      }
      case CMO_EXPORT: {
        const CmExport *ex = &c->exports[o->index];

        if (ex->si.sort == CMS_FUNC) {
          uint32_t *fp = vec_at(&e->func, ex->si.index);
          uint32_t fi = fp ? *fp : 0;
          if (!fp) blocked(b, "export \"%s\" names a function out of scope", ex->name);
          *(uint32_t *)vec_push(&e->func) = fi;
          out.names = xrealloc(out.names, (out.n + 1) * sizeof *out.names);
          out.sorts = xrealloc(out.sorts, (out.n + 1) * sizeof *out.sorts);
          out.idx = xrealloc(out.idx, (out.n + 1) * sizeof *out.idx);
          out.names[out.n] = ex->name;
          out.sorts[out.n] = CMS_FUNC;
          out.idx[out.n] = fi;
          out.n++;
        } else if (ex->si.sort == CMS_INSTANCE) {
          uint32_t *ip = vec_at(&e->inst, ex->si.index);
          if (!ip) blocked(b, "export \"%s\" names an instance out of scope",
                           ex->name);
          *(uint32_t *)vec_push(&e->inst) = ip ? *ip : 0;
          out.names = xrealloc(out.names, (out.n + 1) * sizeof *out.names);
          out.sorts = xrealloc(out.sorts, (out.n + 1) * sizeof *out.sorts);
          out.idx = xrealloc(out.idx, (out.n + 1) * sizeof *out.idx);
          out.names[out.n] = ex->name;
          out.sorts[out.n] = CMS_INSTANCE;
          out.idx[out.n] = ip ? *ip : 0;
          out.n++;
        } else if (ex->si.sort == CMS_CORE && ex->si.core_sort == CMCS_MODULE) {
          uint32_t *m = vec_at(&e->mod, ex->si.index);
          if (!m) blocked(b, "export \"%s\" names a core module out of scope",
                          ex->name);
          *(uint32_t *)vec_push(&e->mod) = m ? *m : UINT32_MAX;
        } else if (ex->si.sort == CMS_COMPONENT) {
          CompRef null_cr = { NULL, NULL };
          CompRef *cr = vec_at(&e->comp, ex->si.index);
          if (!cr) blocked(b, "export \"%s\" names a component out of scope",
                           ex->name);
          *(CompRef *)vec_push(&e->comp) = cr ? *cr : null_cr;
        } else if (space_tracked(&ex->si)) {
          blocked(b, "export \"%s\" names a %s, which this plan cannot "
                     "re-export", ex->name, sort_word(&ex->si));
          space_placeholder(e, &ex->si);
        }
        break;
      }
      case CMO_START:
        blocked(b, "a component start function is not run yet");
        break;
      case CMO_TYPE: case CMO_CORE_TYPE:
        break;
    }
  }
  return out;
}

static void plan_export(LinkPlan *p, const char *iface, const char *fn,
                        uint32_t func) {
  char buf[512];
  if (fn) snprintf(buf, sizeof buf, "%s#%s", iface, fn);
  else snprintf(buf, sizeof buf, "%s", iface);
  p->exports = xrealloc(p->exports, (p->nexports + 1) * sizeof *p->exports);
  p->exports[p->nexports].name = xstrndup(buf, strlen(buf));
  p->exports[p->nexports].func = func;
  p->nexports++;
}

LinkPlan *clink_plan(const Component *c) {
  Builder b;
  memset(&b, 0, sizeof b);
  b.p = xcalloc(1, sizeof *b.p);
  vec_init(&b.cviews, sizeof(CoreView));
  vec_init(&b.iviews, sizeof(CompView));
  vec_init(&b.restypes, sizeof(ResKey));

  Env top;
  env_init(&top, NULL, c);
  CompView out = walk(&b, &top, NULL);
  env_free(&top);

  LinkPlan *p = b.p;
  for (uint32_t i = 0; i < out.n; i++) {
    if (out.sorts[i] == CMS_FUNC) {
      plan_export(p, out.names[i], NULL, out.idx[i]);
    } else if (out.sorts[i] == CMS_INSTANCE) {
      const CompView *v = vec_at(&b.iviews, out.idx[i]);
      for (uint32_t j = 0; v && j < v->n; j++)
        if (v->sorts[j] == CMS_FUNC)
          plan_export(p, out.names[i], v->names[j], v->idx[j]);
    }
  }
  xfree(out.names); xfree(out.sorts); xfree(out.idx);

  for (uint32_t i = 0; i < b.cviews.n; i++) {
    CoreView *v = vec_at(&b.cviews, i);
    xfree(v->names); xfree(v->defs);
  }
  for (uint32_t i = 0; i < b.iviews.n; i++) {
    CompView *v = vec_at(&b.iviews, i);
    xfree(v->names); xfree(v->sorts); xfree(v->idx);
  }
  xfree(b.cviews.v);
  xfree(b.iviews.v);
  xfree(b.restypes.v);
  return p;
}

void clink_free(LinkPlan *p) {
  if (!p) return;
  for (uint32_t i = 0; i < p->ninsts; i++) xfree(p->insts[i].binds);
  xfree(p->insts);

  for (uint32_t i = 0; i < p->nmodules; i++)
    if (p->modules[i].bytes) {
      module_free(p->modules[i].m);
      xfree(p->modules[i].bytes);
    }
  xfree(p->modules);
  for (uint32_t i = 0; i < p->ncanons; i++) {
    xfree(p->canons[i].adapter);
    xfree(p->canons[i].host_name);
  }
  xfree(p->canons);
  for (uint32_t i = 0; i < p->nfuncs; i++) xfree(p->funcs[i].impname);
  xfree(p->funcs);
  for (uint32_t i = 0; i < p->nexports; i++) xfree(p->exports[i].name);
  xfree(p->exports);
  for (uint32_t i = 0; i < p->nblockers; i++) xfree(p->blockers[i]);
  xfree(p->blockers);
  xfree(p);
}

static const char *sortch(ExternKind k) {
  switch (k) {
    case EK_FUNC: return "func";
    case EK_TABLE: return "table";
    case EK_MEM: return "memory";
    case EK_GLOBAL: return "global";
    default: return "tag";
  }
}

static void put_def(const LinkPlan *p, const LinkDef *d, FILE *out) {
  switch (d->kind) {
    case LD_EXPORT: {
      const LinkInst *in = &p->insts[d->inst];
      const Module *m = p->modules[in->module].m;
      fprintf(out, "instance %u \"%s\"", d->inst, m->exports[d->exp].name);
      break;
    }
    case LD_CANON:
      fprintf(out, "canon %u", d->canon);
      break;
    case LD_HOST:
      fprintf(out, "host \"%s\"", d->host_name ? d->host_name : "?");
      break;
    default:
      fprintf(out, "unresolved (%s)", d->why ? d->why : "?");
      break;
  }
  fprintf(out, " [%s]", sortch(d->sort));
}

void clink_dump(const LinkPlan *p, FILE *out) {
  for (uint32_t i = 0; i < p->nmodules; i++)
    fprintf(out, "module %u: %s, %u funcs, %u imports, %u exports\n", i,
            p->modules[i].prefix, p->modules[i].m->nfuncs,
            p->modules[i].m->nimports, p->modules[i].m->nexports);

  for (uint32_t i = 0; i < p->ninsts; i++) {
    const LinkInst *in = &p->insts[i];
    fprintf(out, "instance %u: module %u, %u imports\n", i, in->module, in->nbinds);
    const Module *m = p->modules[in->module].m;
    for (uint32_t j = 0; j < in->nbinds; j++) {
      fprintf(out, "  bind \"%s\" \"%s\" <- ", m->imports[j].module, m->imports[j].name);
      put_def(p, &in->binds[j], out);
      fprintf(out, "\n");
    }
  }

  for (uint32_t i = 0; i < p->ncanons; i++) {
    const LinkCanon *lc = &p->canons[i];
    if (lc->builtin) { fprintf(out, "canon %u: %s\n", i, lc->builtin_name); continue; }
    if (lc->resource) {
      fprintf(out, "canon %u: resource.%s table %u", i,
              lc->res_op == 0 ? "new" : lc->res_op == 2 ? "rep" : "drop",
              lc->res_rt);
      if (lc->res_op == 1) {
        fprintf(out, ", dtor ");
        if (lc->has_dtor) put_def(p, &lc->dtor, out);
        else fprintf(out, "none");
      }
      fprintf(out, "\n");
      continue;
    }
    fprintf(out, "canon %u: %s ", i, lc->lift ? "lift" : "lower");
    if (lc->lift) put_def(p, &lc->core, out);
    else if (lc->host) fprintf(out, "host \"%s\" \"%s\"", lc->host_iface, lc->host_fn);
    else fprintf(out, "func %u", lc->func);
    fprintf(out, ", core (");
    for (uint32_t j = 0; j < lc->sig.nparams; j++)
      fprintf(out, "%s%s", j ? " " : "", canon_flat_name(lc->sig.params[j]));
    fprintf(out, ") -> (");
    for (uint32_t j = 0; j < lc->sig.nresults; j++)
      fprintf(out, "%s%s", j ? " " : "", canon_flat_name(lc->sig.results[j]));
    fprintf(out, "), memory=%s\n", lc->needs_memory ? "yes" : "no");
  }

  for (uint32_t i = 0; i < p->nfuncs; i++) {
    if (p->funcs[i].host)
      fprintf(out, "func %u: host \"%s\" \"%s\"\n", i, p->funcs[i].host_iface,
              p->funcs[i].impname);
    else if (p->funcs[i].import)
      fprintf(out, "func %u: import \"%s\"\n", i, p->funcs[i].impname);
    else
      fprintf(out, "func %u: canon %u\n", i, p->funcs[i].canon);
  }

  for (uint32_t i = 0; i < p->nexports; i++)
    fprintf(out, "export \"%s\": func %u\n", p->exports[i].name, p->exports[i].func);

  for (uint32_t i = 0; i < p->nblockers; i++)
    fprintf(out, "blocked: %s\n", p->blockers[i]);
}

static bool identity_type(CmValType t) {
  if (t.is_index) return false;
  switch (t.prim) {
    case CMP_S32: case CMP_U32: case CMP_S64: case CMP_U64: return true;
    default: return false;
  }
}

static bool identity_functype(const CmFuncType *ft) {
  for (uint32_t i = 0; i < ft->nparams; i++)
    if (!identity_type(ft->params[i].t)) return false;
  return !ft->has_result || identity_type(ft->result);
}

const char *clink_why_not(const LinkPlan *p) {
  static char buf[320];
  if (p->nblockers) {
    snprintf(buf, sizeof buf, "%s", p->blockers[0]);
    return buf;
  }

  for (uint32_t i = 0; i < p->ninsts; i++) {
    const LinkInst *in = &p->insts[i];
    const Module *m = p->modules[in->module].m;
    for (uint32_t j = 0; j < in->nbinds; j++) {
      if (in->binds[j].kind == LD_EXPORT || in->binds[j].kind == LD_HOST) continue;
      snprintf(buf, sizeof buf,
               in->binds[j].kind == LD_CANON
                 ? "import \"%s\" \"%s\" is a canonical function, and emitting "
                   "one as part of a component is not done yet"
                 : "import \"%s\" \"%s\" has nothing to bind it to",
               m->imports[j].module, m->imports[j].name);
      return buf;
    }
  }
  if (!p->nexports) return "it exports no function";
  for (uint32_t i = 0; i < p->nexports; i++) {
    const LinkFunc *lf = &p->funcs[p->exports[i].func];
    if (lf->import) {
      snprintf(buf, sizeof buf, "export \"%s\" is a function the component "
               "imports", p->exports[i].name);
      return buf;
    }
    const LinkCanon *lc = &p->canons[lf->canon];
    if (!lc->lift || !lc->ft || lc->core.kind != LD_EXPORT) {
      snprintf(buf, sizeof buf, "export \"%s\" is not a lift of a core function",
               p->exports[i].name);
      return buf;
    }
    if (clink_is_command(p->exports[i].name)) {
      if (lc->sig.nparams || lc->sig.nresults != 1 ||
          lc->sig.results[0] != CF_I32) {
        snprintf(buf, sizeof buf, "export \"%s\" does not lift to () -> (i32), "
                 "which is what a command's run is", p->exports[i].name);
        return buf;
      }
      continue;
    }
    if (!identity_functype(lc->ft)) {
      snprintf(buf, sizeof buf, "export \"%s\" needs conversion code, which is "
               "not emitted yet -- only the 32- and 64-bit integers lift "
               "without any", p->exports[i].name);
      return buf;
    }
  }

  return NULL;
}

void clink_compile(LinkPlan *p, ObjBuilder *o, const OptOpts *opts) {

  OptOpts mopts = *opts;
  mopts.closed_world = false;
  for (uint32_t i = 0; i < p->nmodules; i++) {
    codegen_set_symbol_prefix(p->modules[i].prefix);
    codegen_rename_funcs(p->modules[i].m, p->modules[i].prefix);
    compile_module(p->modules[i].m, o, &mopts);
  }
  codegen_set_symbol_prefix("warmc_comp");
  codegen_emit_component(p, o);
}
