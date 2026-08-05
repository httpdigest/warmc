#include "component.h"
#include "canon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  const CmDeclList *decls;
  CanonScope scope;
  bool import;

  bool world;
} Iface;

typedef struct {
  Iface *v; uint32_t n, cap;

  CanonScope **sc; uint32_t nsc, cap_sc;

  char **skipped; uint32_t nskipped, cap_skipped;
} Found;

static void found_add(Found *f, const char *name, const CmDeclList *dl,
                      const CanonScope *sc, bool import, bool world) {

  for (uint32_t k = 0; k < f->n; k++)
    if (!strcmp(f->v[k].name, name)) return;
  if (f->n == f->cap) {
    f->cap = f->cap ? f->cap * 2 : 8;
    f->v = xrealloc(f->v, f->cap * sizeof *f->v);
  }
  f->v[f->n].name = name;
  f->v[f->n].decls = dl;
  f->v[f->n].scope = *sc;
  f->v[f->n].import = import;
  f->v[f->n].world = world;
  f->n++;
}

static CanonScope *found_scope(Found *f, const CanonScope *from) {
  if (f->nsc == f->cap_sc) {
    f->cap_sc = f->cap_sc ? f->cap_sc * 2 : 8;
    f->sc = xrealloc(f->sc, f->cap_sc * sizeof *f->sc);
  }
  CanonScope *s = xmalloc(sizeof *s);
  *s = *from;
  f->sc[f->nsc++] = s;
  return s;
}

static void found_skip(Found *f, const char *what, const char *name) {
  if (f->nskipped == f->cap_skipped) {
    f->cap_skipped = f->cap_skipped ? f->cap_skipped * 2 : 4;
    f->skipped = xrealloc(f->skipped, f->cap_skipped * sizeof *f->skipped);
  }
  size_t n = strlen(what) + strlen(name) + 4;
  char *s = xmalloc(n);
  snprintf(s, n, "%s %s", what, name);
  f->skipped[f->nskipped++] = s;
}

static bool split_name(const char *full, char **pkg, char **iface) {
  const char *slash = strchr(full, '/');
  if (!slash) return false;
  const char *at = strchr(slash, '@');
  size_t plen = (size_t)(slash - full);
  size_t ilen = at ? (size_t)(at - slash - 1) : strlen(slash + 1);
  size_t vlen = at ? strlen(at) : 0;
  char *p = xmalloc(plen + vlen + 1);
  memcpy(p, full, plen);
  if (at) memcpy(p + plen, at, vlen);
  p[plen + vlen] = 0;
  char *i = xmalloc(ilen + 1);
  memcpy(i, slash + 1, ilen);
  i[ilen] = 0;
  *pkg = p; *iface = i;
  return true;
}

static const char *prim_name(uint8_t p) {
  switch (p) {
    case CMP_BOOL: return "bool";
    case CMP_S8: return "s8";       case CMP_U8: return "u8";
    case CMP_S16: return "s16";     case CMP_U16: return "u16";
    case CMP_S32: return "s32";     case CMP_U32: return "u32";
    case CMP_S64: return "s64";     case CMP_U64: return "u64";
    case CMP_F32: return "f32";     case CMP_F64: return "f64";
    case CMP_CHAR: return "char";   case CMP_STRING: return "string";
    case CMP_ERROR_CONTEXT: return "error-context";
  }
  return "bool";
}

typedef struct {
  const CmType **ty; const char **name; uint32_t n, cap;
  const CanonScope *scope;
} Names;

static void names_add(Names *m, const CmType *t, const char *n) {
  if (!t) return;
  if (m->n == m->cap) {
    m->cap = m->cap ? m->cap * 2 : 8;
    m->ty = xrealloc(m->ty, m->cap * sizeof *m->ty);
    m->name = xrealloc(m->name, m->cap * sizeof *m->name);
  }
  m->ty[m->n] = t; m->name[m->n] = n; m->n++;
}

static const char *names_get(const Names *m, const CmType *t) {
  if (!t) return NULL;
  for (uint32_t k = 0; k < m->n; k++) if (m->ty[k] == t) return m->name[k];
  return NULL;
}

static void put_valtype(FILE *out, const Names *m, CmValType t);

static void put_opt(FILE *out, const Names *m, CmOptValType o) {
  if (o.present) put_valtype(out, m, o.t);
  else fputc('_', out);
}

static void put_defval(FILE *out, const Names *m, const CmDefVal *d);

static void put_valtype(FILE *out, const Names *m, CmValType t) {
  if (!t.is_index) { fputs(prim_name(t.prim), out); return; }
  const CmType *ct = canon_type_in(m->scope, t.index);
  const char *n = names_get(m, ct);
  if (n) { fputs(n, out); return; }

  if (!ct || ct->kind != CMT_DEFVAL) { fputs("unknown", out); return; }
  put_defval(out, m, &ct->defval);
}

static void put_defval(FILE *out, const Names *m, const CmDefVal *d) {
  switch (d->kind) {
    case CMD_PRIM: fputs(prim_name(d->prim), out); break;
    case CMD_LIST: fputs("list<", out); put_valtype(out, m, d->elem);
                   fputc('>', out); break;
    case CMD_FIXED_LIST: fputs("list<", out); put_valtype(out, m, d->elem);
                   fprintf(out, ", %u>", d->len); break;
    case CMD_OPTION: fputs("option<", out); put_valtype(out, m, d->elem);
                   fputc('>', out); break;
    case CMD_TUPLE:
      fputs("tuple<", out);
      for (uint32_t i = 0; i < d->nelems; i++) {
        if (i) fputs(", ", out);
        put_valtype(out, m, d->elems[i]);
      }
      fputc('>', out);
      break;
    case CMD_RESULT:

      if (!d->ok.present && !d->err.present) { fputs("result", out); break; }
      fputs("result<", out);
      put_opt(out, m, d->ok);
      if (d->err.present) { fputs(", ", out); put_valtype(out, m, d->err.t); }
      fputc('>', out);
      break;
    case CMD_OWN: put_valtype(out, m, d->elem); break;
    case CMD_BORROW: fputs("borrow<", out); put_valtype(out, m, d->elem);
                   fputc('>', out); break;
    case CMD_STREAM: fputs("stream<", out); put_opt(out, m, d->ok);
                   fputc('>', out); break;
    case CMD_FUTURE: fputs("future<", out); put_opt(out, m, d->ok);
                   fputc('>', out); break;

    default: fputs("unknown", out); break;
  }
}

static void put_func(FILE *out, const Names *m, const char *name,
                     const CmFuncType *ft) {
  fprintf(out, "  %s: %sfunc(", name, ft->is_async ? "async " : "");
  for (uint32_t i = 0; i < ft->nparams; i++) {
    if (i) fputs(", ", out);
    fprintf(out, "%s: ", ft->params[i].label);
    put_valtype(out, m, ft->params[i].t);
  }
  fputc(')', out);
  if (ft->has_result) { fputs(" -> ", out); put_valtype(out, m, ft->result); }
  fputs(";\n", out);
}

static void put_typedef(FILE *out, const Names *m, const char *name,
                        const CmType *ct) {
  if (ct->kind == CMT_RESOURCE) { fprintf(out, "  resource %s;\n", name); return; }
  if (ct->kind != CMT_DEFVAL) return;
  const CmDefVal *d = &ct->defval;
  switch (d->kind) {
    case CMD_RECORD:
      fprintf(out, "  record %s {\n", name);
      for (uint32_t i = 0; i < d->nfields; i++) {
        fprintf(out, "    %s: ", d->fields[i].label);
        put_valtype(out, m, d->fields[i].t);
        fputs(",\n", out);
      }
      fputs("  }\n", out);
      break;
    case CMD_VARIANT:
      fprintf(out, "  variant %s {\n", name);
      for (uint32_t i = 0; i < d->ncases; i++) {
        fprintf(out, "    %s", d->cases[i].label);
        if (d->cases[i].t.present) {
          fputc('(', out);
          put_valtype(out, m, d->cases[i].t.t);
          fputc(')', out);
        }
        fputs(",\n", out);
      }
      fputs("  }\n", out);
      break;
    case CMD_ENUM:
      fprintf(out, "  enum %s {\n", name);
      for (uint32_t i = 0; i < d->nlabels; i++)
        fprintf(out, "    %s,\n", d->labels[i]);
      fputs("  }\n", out);
      break;
    case CMD_FLAGS:
      fprintf(out, "  flags %s {\n", name);
      for (uint32_t i = 0; i < d->nlabels; i++)
        fprintf(out, "    %s,\n", d->labels[i]);
      fputs("  }\n", out);
      break;
    default:

      fprintf(out, "  type %s = ", name);
      put_defval(out, m, d);
      fputs(";\n", out);
      break;
  }
}

static void collect_names(Names *m, const CmDeclList *dl) {
  for (uint32_t i = 0; i < dl->ndecls; i++) {
    const CmDecl *d = &dl->decls[i];
    if (d->kind != CMDECL_EXPORT || !d->name) continue;
    if (d->et.kind != CME_TYPE) continue;
    names_add(m, canon_type_in(m->scope, d->et.index), d->name);
  }
}

static void put_world(FILE *out, const Iface *f, const char *wname,
                      const char *pkg) {
  fprintf(out, "world %s {\n", wname);
  Names m = { 0 };
  m.scope = &f->scope;
  collect_names(&m, f->decls);
  for (uint32_t i = 0; i < f->decls->ndecls; i++) {
    const CmDecl *d = &f->decls->decls[i];
    bool imp = d->kind == CMDECL_IMPORT;
    if (!imp && d->kind != CMDECL_EXPORT) continue;
    if (!d->name) continue;
    const char *dir = imp ? "import" : "export";
    if (d->et.kind == CME_INSTANCE) {
      char *p = NULL, *n = NULL;
      if (split_name(d->name, &p, &n)) {
        fprintf(out, "  %s %s;\n", dir, strcmp(p, pkg) ? d->name : n);
        xfree(p); xfree(n);
      } else {
        fprintf(out, "  %s %s;\n", dir, d->name);
      }
    } else if (d->et.kind == CME_FUNC) {
      const CmType *ct = canon_type_in(&f->scope, d->et.index);
      if (ct && ct->kind == CMT_FUNC) {
        fprintf(out, "  %s ", dir);

        put_func(out, &m, d->name, &ct->func);
      }
    }
  }
  fputs("}\n", out);
  xfree(m.ty); xfree(m.name);
}

static void put_iface(FILE *out, const Iface *f, const char *iname) {
  Names m = { 0 };
  m.scope = &f->scope;
  collect_names(&m, f->decls);
  fprintf(out, "interface %s {\n", iname);
  for (uint32_t i = 0; i < f->decls->ndecls; i++) {
    const CmDecl *d = &f->decls->decls[i];
    if (d->kind != CMDECL_EXPORT || !d->name) continue;
    if (d->et.kind == CME_TYPE) {
      const CmType *ct = canon_type_in(&f->scope, d->et.index);
      if (ct) put_typedef(out, &m, d->name, ct);
      else fprintf(out, "  resource %s;\n", d->name);
    } else if (d->et.kind == CME_FUNC) {
      const CmType *ct = canon_type_in(&f->scope, d->et.index);
      if (ct && ct->kind == CMT_FUNC) put_func(out, &m, d->name, &ct->func);
    }
  }
  fputs("}\n", out);
  xfree(m.ty); xfree(m.name);
}

static void find_in_component_type(Found *f, const CanonScope *outer,
                                   const CmType *ct) {
  CanonScope tmp = { outer, outer->c, &ct->decls };
  const CanonScope *inner = found_scope(f, &tmp);
  for (uint32_t i = 0; i < ct->decls.ndecls; i++) {
    const CmDecl *d = &ct->decls.decls[i];

    if (d->kind == CMDECL_IMPORT && d->name && d->et.kind == CME_INSTANCE) {
      const CmType *it = canon_type_in(inner, d->et.index);
      if (it && it->kind == CMT_INSTANCE && strchr(d->name, '/')) {
        CanonScope isc = { inner, outer->c, &it->decls };
        found_add(f, d->name, &it->decls, &isc, true, false);
      }
      continue;
    }
    if (d->kind != CMDECL_EXPORT || !d->name) continue;
    if (d->et.kind != CME_INSTANCE && d->et.kind != CME_COMPONENT) continue;
    bool is_world = d->et.kind == CME_COMPONENT;
    const CmType *it = canon_type_in(inner, d->et.index);
    if (!it) continue;
    if (it->kind != (is_world ? CMT_COMPONENT : CMT_INSTANCE)) continue;
    CanonScope isc = { inner, outer->c, &it->decls };
    found_add(f, d->name, &it->decls, &isc, false, is_world);

    if (is_world) {
      const CanonScope *wsc = found_scope(f, &isc);
      for (uint32_t j = 0; j < it->decls.ndecls; j++) {
        const CmDecl *w = &it->decls.decls[j];
        bool imp = w->kind == CMDECL_IMPORT;
        if (!imp && w->kind != CMDECL_EXPORT) continue;
        if (!w->name || w->et.kind != CME_INSTANCE) continue;
        if (!strchr(w->name, '/')) continue;
        const CmType *wt = canon_type_in(wsc, w->et.index);
        if (!wt || wt->kind != CMT_INSTANCE) continue;
        CanonScope jsc = { wsc, outer->c, &wt->decls };
        found_add(f, w->name, &wt->decls, &jsc, imp, false);
      }
    }
  }
}

static void find_ifaces(Found *f, const Component *c, const CanonScope *top) {
  for (uint32_t i = 0; i < c->nimports; i++) {
    const CmImport *im = &c->imports[i];
    if (im->et.kind != CME_INSTANCE) continue;
    const CmType *it = canon_type_in(top, im->et.index);
    if (!it || it->kind != CMT_INSTANCE) continue;
    CanonScope isc = { top, c, &it->decls };
    found_add(f, im->name, &it->decls, &isc, true, false);
  }
  for (uint32_t i = 0; i < c->nexports; i++) {
    const CmExport *ex = &c->exports[i];
    if (ex->has_type && ex->et.kind == CME_INSTANCE) {
      const CmType *it = canon_type_in(top, ex->et.index);
      if (it && it->kind == CMT_INSTANCE) {
        CanonScope isc = { top, c, &it->decls };
        found_add(f, ex->name, &it->decls, &isc, false, false);
        continue;
      }
    }

    if (ex->si.sort == CMS_TYPE) {
      const CmType *ct = canon_type_in(top, ex->si.index);
      if (ct && ct->kind == CMT_COMPONENT) { find_in_component_type(f, top, ct); continue; }
      if (ct && ct->kind == CMT_INSTANCE) {
        CanonScope isc = { top, c, &ct->decls };
        found_add(f, ex->name, &ct->decls, &isc, false, false);
        continue;
      }
    }

    if (ex->si.sort == CMS_INSTANCE) {
      found_skip(f, "export", ex->name);
      continue;
    }
    found_skip(f, "export", ex->name);
  }
}

void wit_dump_component(const Component *c, FILE *out) {
  CanonScope top = { NULL, c, NULL };
  Found f = { 0 };
  find_ifaces(&f, c, &top);

  char **pkgs = NULL; uint32_t npkgs = 0;
  char **inames = xmalloc((f.n ? f.n : 1) * sizeof *inames);
  uint32_t *pkgof = xmalloc((f.n ? f.n : 1) * sizeof *pkgof);
  for (uint32_t i = 0; i < f.n; i++) {
    char *p = NULL, *n = NULL;
    if (!split_name(f.v[i].name, &p, &n)) {
      p = xstrndup("root:component", 14);
      n = xstrndup(f.v[i].name, strlen(f.v[i].name));
    }
    uint32_t k = 0;
    for (; k < npkgs; k++) if (!strcmp(pkgs[k], p)) break;
    if (k == npkgs) {
      pkgs = xrealloc(pkgs, (npkgs + 1) * sizeof *pkgs);
      pkgs[npkgs++] = p;
    } else {
      xfree(p);
    }
    inames[i] = n;
    pkgof[i] = k;
  }

  if (npkgs == 0) {

    fputs("package root:component;\n\nworld root {\n}\n", out);
  }

  for (uint32_t k = 0; k < npkgs; k++) {
    if (k == 0) fprintf(out, "package %s;\n\n", pkgs[k]);
    else fprintf(out, "\npackage %s {\n", pkgs[k]);
    for (uint32_t i = 0; i < f.n; i++)
      if (pkgof[i] == k && !f.v[i].world) put_iface(out, &f.v[i], inames[i]);
    for (uint32_t i = 0; i < f.n; i++)
      if (pkgof[i] == k && f.v[i].world)
        put_world(out, &f.v[i], inames[i], pkgs[k]);
    if (k) fputs("}\n", out);
  }
  for (uint32_t i = 0; i < f.nskipped; i++)
    fprintf(out, "// not yet: %s\n", f.skipped[i]);

  for (uint32_t i = 0; i < f.n; i++) xfree(inames[i]);
  for (uint32_t k = 0; k < npkgs; k++) xfree(pkgs[k]);
  for (uint32_t i = 0; i < f.nskipped; i++) xfree(f.skipped[i]);
  for (uint32_t i = 0; i < f.nsc; i++) xfree(f.sc[i]);
  xfree(f.sc);
  xfree(inames); xfree(pkgof); xfree(pkgs); xfree(f.skipped); xfree(f.v);
}
