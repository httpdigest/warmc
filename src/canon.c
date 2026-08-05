#include <stdarg.h>
#include <stdlib.h>
#include "canon.h"

#include <stdio.h>
#include <string.h>

static uint32_t ptr_size(const CanonCtx *cx) { return cx->ptr64 ? 8u : 4u; }

static uint32_t align_to(uint32_t v, uint32_t a) {
  return a <= 1 ? v : ((v + a - 1) / a) * a;
}

#define CANON_ALIAS_MAXD 64

static const CmType *canon_type_at(const CanonScope *sc, uint32_t i, int depth) {
  if (!sc || depth > CANON_ALIAS_MAXD) return NULL;
  const CmTypeSlot *space = sc->dl ? sc->dl->type_space : sc->c->type_space;
  uint32_t n = sc->dl ? sc->dl->ntype_space : sc->c->ntype_space;
  if (i >= n) return NULL;
  const CmTypeSlot *sl = &space[i];
  if (sl->kind == CMTS_DECLARED) {
    if (sc->dl) {
      if (sl->index >= sc->dl->ndecls) return NULL;
      return &sc->dl->decls[sl->index].type;
    }
    if (sl->index >= sc->c->ntypes) return NULL;
    return &sc->c->types[sl->index];
  }

  if (sl->kind == CMTS_EXPORT || sl->kind == CMTS_IMPORT) {

    const CmExternType *et;
    if (!sc->dl) {
      const Component *c = sc->c;
      if (sl->kind == CMTS_EXPORT) {
        if (sl->index >= c->nexports) return NULL;
        uint32_t to = c->exports[sl->index].si.index;
        if (c->exports[sl->index].si.sort != CMS_TYPE || to >= i) return NULL;
        return canon_type_at(sc, to, depth + 1);
      }
      if (sl->index >= c->nimports) return NULL;
      et = &c->imports[sl->index].et;
    } else {
      if (sl->index >= sc->dl->ndecls) return NULL;
      et = &sc->dl->decls[sl->index].et;
    }
    if (et->kind != CME_TYPE || et->bound != 0x00) return NULL;
    if (et->index >= i) return NULL;
    return canon_type_at(sc, et->index, depth + 1);
  }
  if (sl->kind == CMTS_ALIAS && sc->dl) {
    if (sl->index >= sc->dl->ndecls) return NULL;
    const CmDecl *d = &sc->dl->decls[sl->index];
    if (d->kind != CMDECL_ALIAS) return NULL;

    if (d->alias.kind != CMA_OUTER) return NULL;
    const CanonScope *up = sc;
    for (uint32_t k = 0; k < d->alias.count && up; k++) up = up->outer;

    if (up == sc && d->alias.index >= i) return NULL;
    return canon_type_at(up, d->alias.index, depth + 1);
  }
  return NULL;
}

const CmType *canon_type_in(const CanonScope *sc, uint32_t i) {
  return canon_type_at(sc, i, 0);
}

#define CANON_MAX_STEPS 20000000u
static uint64_t canon_steps;

static const CmDefVal *resolve(const CanonCtx *cx, CmValType t, uint8_t *prim) {
  if (++canon_steps > CANON_MAX_STEPS)
    warmc_invalid("canonical ABI: walking this value type takes more than %u "
                  "steps", CANON_MAX_STEPS);
  if (!t.is_index) { *prim = t.prim; return NULL; }
  const CmType *ct = canon_type_in(cx->scope, t.index);
  if (!ct) { *prim = 0; return NULL; }
  if (ct->kind == CMT_RESOURCE) { *prim = 0; return NULL; }
  if (ct->kind != CMT_DEFVAL)
    warmc_fatal("canonical ABI: type %u is not a value type", t.index);
  const CmDefVal *d = &ct->defval;
  if (d->kind == CMD_PRIM) { *prim = d->prim; return NULL; }
  *prim = 0;
  return d;
}

static uint32_t prim_align(const CanonCtx *cx, uint8_t p) {
  switch (p) {
    case CMP_BOOL: case CMP_S8: case CMP_U8: return 1;
    case CMP_S16: case CMP_U16: return 2;
    case CMP_S32: case CMP_U32: case CMP_F32: case CMP_CHAR: return 4;
    case CMP_S64: case CMP_U64: case CMP_F64: return 8;
    case CMP_STRING: return ptr_size(cx);
    case CMP_ERROR_CONTEXT: return 4;
    default: warmc_fatal("canonical ABI: unknown primitive 0x%02x", p);
  }
  return 1;
}

static uint32_t prim_size(const CanonCtx *cx, uint8_t p) {
  switch (p) {
    case CMP_BOOL: case CMP_S8: case CMP_U8: return 1;
    case CMP_S16: case CMP_U16: return 2;
    case CMP_S32: case CMP_U32: case CMP_F32: case CMP_CHAR: return 4;
    case CMP_S64: case CMP_U64: case CMP_F64: return 8;
    case CMP_STRING: return 2 * ptr_size(cx);
    case CMP_ERROR_CONTEXT: return 4;
    default: warmc_fatal("canonical ABI: unknown primitive 0x%02x", p);
  }
  return 1;
}

static uint32_t variant_ncases(const CmDefVal *d) {
  switch (d->kind) {
    case CMD_VARIANT: return d->ncases;
    case CMD_ENUM:    return d->nlabels;
    case CMD_OPTION:  return 2;
    case CMD_RESULT:  return 2;
    default: return 0;
  }
}

static bool variant_case(const CmDefVal *d, uint32_t i, CmValType *out) {
  switch (d->kind) {
    case CMD_VARIANT:
      if (!d->cases[i].t.present) return false;
      *out = d->cases[i].t.t; return true;
    case CMD_ENUM: return false;
    case CMD_OPTION:
      if (i == 0) return false;
      *out = d->elem; return true;
    case CMD_RESULT: {
      const CmOptValType *o = i == 0 ? &d->ok : &d->err;
      if (!o->present) return false;
      *out = o->t; return true;
    }
    default: return false;
  }
}

static bool is_variant_like(const CmDefVal *d) {
  return d->kind == CMD_VARIANT || d->kind == CMD_ENUM ||
         d->kind == CMD_OPTION || d->kind == CMD_RESULT;
}

static uint8_t discriminant_prim(uint32_t ncases) {
  if (ncases == 0) warmc_fatal("canonical ABI: a variant needs at least one case");
  if (ncases <= 256) return CMP_U8;
  if (ncases <= 65536) return CMP_U16;
  return CMP_U32;
}

static uint32_t max_case_align(const CanonCtx *cx, const CmDefVal *d) {
  uint32_t a = 1, n = variant_ncases(d);
  for (uint32_t i = 0; i < n; i++) {
    CmValType ct;
    if (!variant_case(d, i, &ct)) continue;
    uint32_t ca = canon_align(cx, ct);
    if (ca > a) a = ca;
  }
  return a;
}

static uint32_t flags_bytes(uint32_t n) {
  if (n == 0 || n > 32)
    warmc_fatal("canonical ABI: flags must have between 1 and 32 labels, not %u", n);
  if (n <= 8) return 1;
  if (n <= 16) return 2;
  return 4;
}

uint32_t canon_align(const CanonCtx *cx, CmValType t) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  if (!d) return p ? prim_align(cx, p) : 4u;
  switch (d->kind) {
    case CMD_PRIM: return prim_align(cx, d->prim);
    case CMD_LIST: return ptr_size(cx);
    case CMD_FIXED_LIST: return canon_align(cx, d->elem);
    case CMD_OWN: case CMD_BORROW: case CMD_STREAM: case CMD_FUTURE: return 4;
    case CMD_FLAGS: return flags_bytes(d->nlabels);
    case CMD_RECORD: {
      uint32_t a = 1;
      for (uint32_t i = 0; i < d->nfields; i++) {
        uint32_t fa = canon_align(cx, d->fields[i].t);
        if (fa > a) a = fa;
      }
      return a;
    }
    case CMD_TUPLE: {
      uint32_t a = 1;
      for (uint32_t i = 0; i < d->nelems; i++) {
        uint32_t fa = canon_align(cx, d->elems[i]);
        if (fa > a) a = fa;
      }
      return a;
    }
    default: break;
  }
  if (is_variant_like(d)) {
    uint32_t da = prim_align(cx, discriminant_prim(variant_ncases(d)));
    uint32_t ca = max_case_align(cx, d);
    return da > ca ? da : ca;
  }
  warmc_fatal("canonical ABI: no alignment rule for value type kind %d", d->kind);
  return 1;
}

uint32_t canon_size(const CanonCtx *cx, CmValType t) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  if (!d) return p ? prim_size(cx, p) : 4u;
  switch (d->kind) {
    case CMD_PRIM: return prim_size(cx, d->prim);
    case CMD_LIST: return 2 * ptr_size(cx);
    case CMD_FIXED_LIST: {
      uint64_t z = (uint64_t)d->len * canon_size(cx, d->elem);
      if (z > 0xffffffffull)
        warmc_invalid("canonical ABI: a fixed-size list of %u elements does "
                      "not fit in a memory", d->len);
      return (uint32_t)z;
    }
    case CMD_OWN: case CMD_BORROW: case CMD_STREAM: case CMD_FUTURE: return 4;
    case CMD_FLAGS: return flags_bytes(d->nlabels);
    case CMD_RECORD: {
      uint32_t s = 0;
      for (uint32_t i = 0; i < d->nfields; i++) {
        s = align_to(s, canon_align(cx, d->fields[i].t));
        s += canon_size(cx, d->fields[i].t);
      }
      return align_to(s, canon_align(cx, t));
    }
    case CMD_TUPLE: {
      uint32_t s = 0;
      for (uint32_t i = 0; i < d->nelems; i++) {
        s = align_to(s, canon_align(cx, d->elems[i]));
        s += canon_size(cx, d->elems[i]);
      }
      return align_to(s, canon_align(cx, t));
    }
    default: break;
  }
  if (is_variant_like(d)) {
    uint32_t n = variant_ncases(d);
    uint32_t s = prim_size(cx, discriminant_prim(n));
    s = align_to(s, max_case_align(cx, d));
    uint32_t cs = 0;
    for (uint32_t i = 0; i < n; i++) {
      CmValType ct;
      if (!variant_case(d, i, &ct)) continue;
      uint32_t z = canon_size(cx, ct);
      if (z > cs) cs = z;
    }
    return align_to(s + cs, canon_align(cx, t));
  }
  warmc_fatal("canonical ABI: no size rule for value type kind %d", d->kind);
  return 0;
}

static bool vt_same(const CanonCtx *ca, CmValType a,
                    const CanonCtx *cb, CmValType b);

static bool dv_same(const CanonCtx *ca, const CmDefVal *da,
                    const CanonCtx *cb, const CmDefVal *db) {
  if (da->kind != db->kind) return false;
  switch (da->kind) {
    case CMD_PRIM: return da->prim == db->prim;
    case CMD_LIST: return vt_same(ca, da->elem, cb, db->elem);
    case CMD_FIXED_LIST:
      return da->len == db->len && vt_same(ca, da->elem, cb, db->elem);
    case CMD_OWN: case CMD_BORROW: case CMD_STREAM: case CMD_FUTURE:
      return true;
    case CMD_FLAGS:
      if (da->nlabels != db->nlabels) return false;
      for (uint32_t i = 0; i < da->nlabels; i++)
        if (strcmp(da->labels[i], db->labels[i])) return false;
      return true;
    case CMD_RECORD:
      if (da->nfields != db->nfields) return false;
      for (uint32_t i = 0; i < da->nfields; i++) {
        if (strcmp(da->fields[i].label, db->fields[i].label)) return false;
        if (!vt_same(ca, da->fields[i].t, cb, db->fields[i].t)) return false;
      }
      return true;
    case CMD_TUPLE:
      if (da->nelems != db->nelems) return false;
      for (uint32_t i = 0; i < da->nelems; i++)
        if (!vt_same(ca, da->elems[i], cb, db->elems[i])) return false;
      return true;
    default: break;
  }
  if (!is_variant_like(da)) return false;
  uint32_t n = variant_ncases(da);
  if (n != variant_ncases(db)) return false;
  if (da->kind == CMD_VARIANT || da->kind == CMD_ENUM)
    for (uint32_t i = 0; i < n; i++) {
      const char *la = da->kind == CMD_ENUM ? da->labels[i] : da->cases[i].label;
      const char *lb = db->kind == CMD_ENUM ? db->labels[i] : db->cases[i].label;
      if (strcmp(la, lb)) return false;
    }
  for (uint32_t i = 0; i < n; i++) {
    CmValType ta, tb;
    bool pa = variant_case(da, i, &ta), pb = variant_case(db, i, &tb);
    if (pa != pb) return false;
    if (pa && !vt_same(ca, ta, cb, tb)) return false;
  }
  return true;
}

static bool vt_same(const CanonCtx *ca, CmValType a,
                    const CanonCtx *cb, CmValType b) {
  uint8_t pa, pb;
  const CmDefVal *da = resolve(ca, a, &pa);
  const CmDefVal *db = resolve(cb, b, &pb);
  if (!da || !db) return !da && !db && pa == pb;
  return dv_same(ca, da, cb, db);
}

bool canon_functype_same(const CanonCtx *ca, const CmFuncType *fa,
                         const CanonCtx *cb, const CmFuncType *fb) {
  if (fa->is_async != fb->is_async) return false;
  if (fa->nparams != fb->nparams) return false;
  if (fa->has_result != fb->has_result) return false;
  for (uint32_t i = 0; i < fa->nparams; i++) {
    if (strcmp(fa->params[i].label, fb->params[i].label)) return false;
    if (!vt_same(ca, fa->params[i].t, cb, fb->params[i].t)) return false;
  }
  if (fa->has_result && !vt_same(ca, fa->result, cb, fb->result)) return false;
  return true;
}

static void flat_put(CanonFlat *out, uint32_t cap, uint32_t at, CanonFlat f) {
  if (at < cap) out[at] = f;
}

static CanonFlat join(CanonFlat a, CanonFlat b) {
  if (a == b) return a;
  if ((a == CF_I32 && b == CF_F32) || (a == CF_F32 && b == CF_I32)) return CF_I32;
  return CF_I64;
}

static CanonFlat prim_flat(const CanonCtx *cx, uint8_t p) {
  switch (p) {
    case CMP_BOOL: case CMP_S8: case CMP_U8: case CMP_S16: case CMP_U16:
    case CMP_S32: case CMP_U32: case CMP_CHAR: case CMP_ERROR_CONTEXT:
      return CF_I32;
    case CMP_S64: case CMP_U64: return CF_I64;
    case CMP_F32: return CF_F32;
    case CMP_F64: return CF_F64;
    case CMP_STRING: return cx->ptr64 ? CF_I64 : CF_I32;
    default: warmc_fatal("canonical ABI: unknown primitive 0x%02x", p);
  }
  return CF_I32;
}

uint32_t canon_flatten(const CanonCtx *cx, CmValType t, CanonFlat *out, uint32_t cap) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  CanonFlat ptr = cx->ptr64 ? CF_I64 : CF_I32;
  if (!d) {
    if (!p) { flat_put(out, cap, 0, CF_I32); return 1; }
    if (p == CMP_STRING) {
      flat_put(out, cap, 0, ptr); flat_put(out, cap, 1, ptr);
      return 2;
    }
    flat_put(out, cap, 0, prim_flat(cx, p));
    return 1;
  }
  switch (d->kind) {
    case CMD_PRIM:
      if (d->prim == CMP_STRING) {
        flat_put(out, cap, 0, ptr); flat_put(out, cap, 1, ptr);
        return 2;
      }
      flat_put(out, cap, 0, prim_flat(cx, d->prim));
      return 1;
    case CMD_LIST:
      flat_put(out, cap, 0, ptr); flat_put(out, cap, 1, ptr);
      return 2;
    case CMD_FIXED_LIST: {

      uint32_t per = canon_flatten(cx, d->elem, NULL, 0);
      uint32_t n = 0;
      for (uint32_t i = 0; i < d->len; i++) {
        CanonFlat tmp[CANON_MAX_SIG];
        uint32_t got = canon_flatten(cx, d->elem, tmp, per <= CANON_MAX_SIG ? per : CANON_MAX_SIG);
        for (uint32_t k = 0; k < got && k < CANON_MAX_SIG; k++) flat_put(out, cap, n + k, tmp[k]);
        n += got;
      }
      return n;
    }
    case CMD_OWN: case CMD_BORROW: case CMD_STREAM: case CMD_FUTURE:
    case CMD_FLAGS:
      flat_put(out, cap, 0, CF_I32);
      return 1;
    case CMD_RECORD: {
      uint32_t n = 0;
      for (uint32_t i = 0; i < d->nfields; i++)
        n += canon_flatten(cx, d->fields[i].t, out ? out + n : NULL,
                           cap > n ? cap - n : 0);
      return n;
    }
    case CMD_TUPLE: {
      uint32_t n = 0;
      for (uint32_t i = 0; i < d->nelems; i++)
        n += canon_flatten(cx, d->elems[i], out ? out + n : NULL,
                           cap > n ? cap - n : 0);
      return n;
    }
    default: break;
  }
  if (is_variant_like(d)) {

    uint32_t ncases = variant_ncases(d);
    flat_put(out, cap, 0, CF_I32);
    CanonFlat payload[CANON_MAX_SIG * 4];
    uint32_t np = 0;
    for (uint32_t i = 0; i < ncases; i++) {
      CmValType ct;
      if (!variant_case(d, i, &ct)) continue;
      CanonFlat tmp[CANON_MAX_SIG * 4];
      uint32_t got = canon_flatten(cx, ct, tmp, sizeof tmp / sizeof tmp[0]);
      if (got > sizeof tmp / sizeof tmp[0])
        warmc_fatal("canonical ABI: a variant case flattens to %u values, "
                    "more than this supports", got);
      for (uint32_t k = 0; k < got; k++) {
        if (k < np) payload[k] = join(payload[k], tmp[k]);
        else if (np < sizeof payload / sizeof payload[0]) payload[np++] = tmp[k];
      }
    }
    for (uint32_t k = 0; k < np; k++) flat_put(out, cap, 1 + k, payload[k]);
    return 1 + np;
  }
  warmc_fatal("canonical ABI: no flattening rule for value type kind %d", d->kind);
  return 0;
}

void canon_flatten_functype(const CanonCtx *cx, const CmFuncType *ft, bool lift,
                            CanonSig *out) {
  CanonFlat ptr = cx->ptr64 ? CF_I64 : CF_I32;
  memset(out, 0, sizeof *out);

  CanonFlat ps[CANON_MAX_SIG * 4];
  uint32_t np = 0;
  for (uint32_t i = 0; i < ft->nparams; i++) {
    CanonFlat tmp[CANON_MAX_SIG * 4];
    uint32_t got = canon_flatten(cx, ft->params[i].t, tmp, sizeof tmp / sizeof tmp[0]);
    for (uint32_t k = 0; k < got; k++)
      if (np < sizeof ps / sizeof ps[0]) ps[np++] = tmp[k];
      else np++;
  }
  CanonFlat rs[CANON_MAX_SIG * 4];
  uint32_t nr = 0;
  if (ft->has_result) {
    CanonFlat tmp[CANON_MAX_SIG * 4];
    uint32_t got = canon_flatten(cx, ft->result, tmp, sizeof tmp / sizeof tmp[0]);
    for (uint32_t k = 0; k < got; k++)
      if (nr < sizeof rs / sizeof rs[0]) rs[nr++] = tmp[k];
      else nr++;
  }

  if (ft->is_async)
    warmc_fatal("canonical ABI: async lifting and lowering is not implemented");

  if (np > CANON_MAX_FLAT_PARAMS) {
    out->params[0] = ptr;
    out->nparams = 1;
  } else {
    for (uint32_t i = 0; i < np; i++) out->params[i] = ps[i];
    out->nparams = np;
  }
  if (nr > CANON_MAX_FLAT_RESULTS) {

    if (lift) {
      out->results[0] = ptr;
      out->nresults = 1;
    } else {
      out->params[out->nparams++] = ptr;
      out->nresults = 0;
    }
  } else {
    for (uint32_t i = 0; i < nr; i++) out->results[i] = rs[i];
    out->nresults = nr;
  }
}

const char *canon_flat_name(CanonFlat f) {
  switch (f) {
    case CF_I32: return "i32";
    case CF_I64: return "i64";
    case CF_F32: return "f32";
    case CF_F64: return "f64";
  }
  return "?";
}

static void put_type(const CanonCtx *cx, CmValType t, FILE *out);

static void put_prim(uint8_t p, FILE *out) {
  switch (p) {
    case CMP_BOOL: fputs("bool", out); break;
    case CMP_S8: fputs("s8", out); break;      case CMP_U8: fputs("u8", out); break;
    case CMP_S16: fputs("s16", out); break;    case CMP_U16: fputs("u16", out); break;
    case CMP_S32: fputs("s32", out); break;    case CMP_U32: fputs("u32", out); break;
    case CMP_S64: fputs("s64", out); break;    case CMP_U64: fputs("u64", out); break;
    case CMP_F32: fputs("f32", out); break;    case CMP_F64: fputs("f64", out); break;
    case CMP_CHAR: fputs("char", out); break;  case CMP_STRING: fputs("string", out); break;
    case CMP_ERROR_CONTEXT: fputs("error-context", out); break;
    default: fprintf(out, "prim%02x", p); break;
  }
}

static void put_def(const CanonCtx *cx, const CmDefVal *d, FILE *out) {
  switch (d->kind) {
    case CMD_PRIM: put_prim(d->prim, out); break;
    case CMD_LIST: fputs("list<", out); put_type(cx, d->elem, out); fputc('>', out); break;
    case CMD_FIXED_LIST:
      fputs("list<", out); put_type(cx, d->elem, out);
      fprintf(out, ",%u>", d->len); break;

    case CMD_OWN:
      if (d->elem.is_index) fprintf(out, "own<%u>", d->elem.index);
      else fputs("own", out);
      break;
    case CMD_BORROW:
      if (d->elem.is_index) fprintf(out, "borrow<%u>", d->elem.index);
      else fputs("borrow", out);
      break;
    case CMD_STREAM: fputs("stream", out); break;
    case CMD_FUTURE: fputs("future", out); break;
    case CMD_FLAGS: fprintf(out, "flags%u", d->nlabels); break;
    case CMD_ENUM: fprintf(out, "enum%u", d->nlabels); break;
    case CMD_RECORD:
      fputs("record{", out);
      for (uint32_t i = 0; i < d->nfields; i++) {
        if (i) fputc(',', out);
        put_type(cx, d->fields[i].t, out);
      }
      fputc('}', out); break;
    case CMD_TUPLE:
      fputs("tuple{", out);
      for (uint32_t i = 0; i < d->nelems; i++) {
        if (i) fputc(',', out);
        put_type(cx, d->elems[i], out);
      }
      fputc('}', out); break;
    case CMD_VARIANT:
      fputs("variant{", out);
      for (uint32_t i = 0; i < d->ncases; i++) {
        if (i) fputc(',', out);
        if (d->cases[i].t.present) put_type(cx, d->cases[i].t.t, out);
        else fputs("_", out);
      }
      fputc('}', out); break;
    case CMD_OPTION:
      fputs("option<", out); put_type(cx, d->elem, out); fputc('>', out); break;
    case CMD_RESULT:
      fputs("result<", out);
      if (d->ok.present) put_type(cx, d->ok.t, out); else fputs("_", out);
      fputc(',', out);
      if (d->err.present) put_type(cx, d->err.t, out); else fputs("_", out);
      fputc('>', out); break;
  }
}

static bool type_opaque_in(const CanonScope *sc, uint32_t i) {
  if (!sc) return false;
  const CmTypeSlot *space = sc->dl ? sc->dl->type_space : sc->c->type_space;
  uint32_t n = sc->dl ? sc->dl->ntype_space : sc->c->ntype_space;
  if (i >= n) return false;
  const CmTypeSlot *sl = &space[i];
  const CmExternType *et = NULL;
  const CmAlias *a = NULL;
  if (sc->dl) {
    if (sl->index >= sc->dl->ndecls) return false;
    const CmDecl *d = &sc->dl->decls[sl->index];
    if (sl->kind == CMTS_IMPORT || sl->kind == CMTS_EXPORT) et = &d->et;
    else if (sl->kind == CMTS_ALIAS) a = &d->alias;
  } else if (sl->kind == CMTS_IMPORT) {
    if (sl->index >= sc->c->nimports) return false;
    et = &sc->c->imports[sl->index].et;
  } else if (sl->kind == CMTS_ALIAS) {
    if (sl->index >= sc->c->naliases) return false;
    a = &sc->c->aliases[sl->index];
  }
  if (et) return et->kind == CME_TYPE && et->bound != 0x00;
  if (a) return a->kind == CMA_EXPORT;
  return false;
}

static void put_type(const CanonCtx *cx, CmValType t, FILE *out) {
  if (!t.is_index) { put_prim(t.prim, out); return; }
  const CmType *ct = canon_type_in(cx->scope, t.index);
  if (!ct && !type_opaque_in(cx->scope, t.index)) {
    fprintf(out, "unknown<%u>", t.index);
    return;
  }

  if (!ct || ct->kind == CMT_RESOURCE) { fputs("own", out); return; }
  if (ct->kind != CMT_DEFVAL) { fprintf(out, "?%u", t.index); return; }
  put_def(cx, &ct->defval, out);
}

static void put_flat(const CanonCtx *cx, CmValType t, FILE *out) {
  CanonFlat f[CANON_MAX_SIG * 4];
  uint32_t n = canon_flatten(cx, t, f, sizeof f / sizeof f[0]);
  for (uint32_t i = 0; i < n && i < sizeof f / sizeof f[0]; i++)
    fprintf(out, "%s%s", i ? "," : "", canon_flat_name(f[i]));
}

static void put_sig(const CanonSig *s, FILE *out) {
  fputc('(', out);
  for (uint32_t i = 0; i < s->nparams; i++)
    fprintf(out, "%s%s", i ? "," : "", canon_flat_name(s->params[i]));
  fputs(")->(", out);
  for (uint32_t i = 0; i < s->nresults; i++)
    fprintf(out, "%s%s", i ? "," : "", canon_flat_name(s->results[i]));
  fputc(')', out);
}

static void dump_scope(const CanonScope *sc, bool ptr64, const char *path,
                       FILE *out) {
  CanonCtx cx = { sc->c, sc, ptr64, CSE_UTF8, CSE_UTF8, 0, 0 };
  uint32_t n = sc->dl ? sc->dl->ntype_space : sc->c->ntype_space;
  for (uint32_t i = 0; i < n; i++) {
    const CmType *ct = canon_type_in(sc, i);
    if (!ct) continue;
    if (ct->kind == CMT_DEFVAL) {
      CmValType t = { true, i, 0 };
      fprintf(out, "canon %s%u ", path, i);
      put_type(&cx, t, out);
      fprintf(out, " align=%u size=%u flat=", canon_align(&cx, t), canon_size(&cx, t));
      put_flat(&cx, t, out);
      fputc('\n', out);
    } else if (ct->kind == CMT_FUNC && !ct->func.is_async) {
      fprintf(out, "canon-func %s%u (", path, i);
      for (uint32_t k = 0; k < ct->func.nparams; k++) {
        if (k) fputc(',', out);
        put_type(&cx, ct->func.params[k].t, out);
      }
      fputs(")->(", out);
      if (ct->func.has_result) put_type(&cx, ct->func.result, out);
      fputs(") lift=", out);
      CanonSig sg;
      canon_flatten_functype(&cx, &ct->func, true, &sg);
      put_sig(&sg, out);
      fputs(" lower=", out);
      canon_flatten_functype(&cx, &ct->func, false, &sg);
      put_sig(&sg, out);
      fputc('\n', out);
    }
  }

  for (uint32_t i = 0; i < n; i++) {
    const CmType *ct = canon_type_in(sc, i);
    if (!ct || (ct->kind != CMT_COMPONENT && ct->kind != CMT_INSTANCE)) continue;
    CanonScope inner = { sc, sc->c, &ct->decls };
    char sub[64];
    snprintf(sub, sizeof sub, "%s%u.", path, i);
    dump_scope(&inner, ptr64, sub, out);
  }
}

static const CmType *instance_canon_type_in(const CanonScope *sc, uint32_t i,
                                      CanonScope *inner) {
  if (!sc) return NULL;
  const CmInstSlot *space = sc->dl ? sc->dl->inst_space : sc->c->inst_space;
  uint32_t n = sc->dl ? sc->dl->ninst_space : sc->c->ninst_space;
  if (i >= n) return NULL;
  const CmInstSlot *sl = &space[i];
  const CmExternType *et = NULL;
  if (sl->kind == CMIS_IMPORT && !sc->dl && sl->index < sc->c->nimports)
    et = &sc->c->imports[sl->index].et;
  else if (sl->kind == CMIS_DECL && sc->dl && sl->index < sc->dl->ndecls)
    et = &sc->dl->decls[sl->index].et;
  if (!et || et->kind != CME_INSTANCE) return NULL;
  const CmType *t = canon_type_in(sc, et->index);
  if (!t || t->kind != CMT_INSTANCE) return NULL;
  inner->outer = sc; inner->c = sc->c; inner->dl = &t->decls;
  return t;
}

static const CmFuncType *alias_func_type(const CanonScope *sc, const CmAlias *a,
                                         CanonScope *at) {
  if (a->kind != CMA_EXPORT) return NULL;
  const CmType *it = instance_canon_type_in(sc, a->instance, at);
  if (!it) return NULL;
  for (uint32_t j = 0; j < it->decls.ndecls; j++) {
    const CmDecl *d = &it->decls.decls[j];
    if (d->kind != CMDECL_EXPORT || !d->name || !a->name) continue;
    if (strcmp(d->name, a->name) != 0) continue;
    if (d->et.kind != CME_FUNC) return NULL;
    const CmType *t = canon_type_in(at, d->et.index);
    return t && t->kind == CMT_FUNC ? &t->func : NULL;
  }
  return NULL;
}

const CmFuncType *canon_func_type_in(const CanonScope *sc, uint32_t i,
                                     CanonScope *at) {
  if (!sc) return NULL;
  *at = *sc;
  const CmFuncSlot *space = sc->dl ? sc->dl->func_space : sc->c->func_space;
  uint32_t n = sc->dl ? sc->dl->nfunc_space : sc->c->nfunc_space;
  if (i >= n) return NULL;
  const CmFuncSlot *sl = &space[i];
  const Component *c = sc->c;
  switch (sl->kind) {
    case CMFS_IMPORT: {
      if (sc->dl || sl->index >= c->nimports) return NULL;
      const CmImport *im = &c->imports[sl->index];
      if (im->et.kind != CME_FUNC) return NULL;
      const CmType *t = canon_type_in(sc, im->et.index);
      return t && t->kind == CMT_FUNC ? &t->func : NULL;
    }
    case CMFS_LIFT: {
      if (sc->dl || sl->index >= c->ncanons) return NULL;
      const CmType *t = canon_type_in(sc, c->canons[sl->index].type);
      return t && t->kind == CMT_FUNC ? &t->func : NULL;
    }
    case CMFS_DECL: {
      if (!sc->dl || sl->index >= sc->dl->ndecls) return NULL;
      const CmDecl *d = &sc->dl->decls[sl->index];
      if (d->et.kind != CME_FUNC) return NULL;
      const CmType *t = canon_type_in(sc, d->et.index);
      return t && t->kind == CMT_FUNC ? &t->func : NULL;
    }
    case CMFS_ALIAS: {
      const CmAlias *a;
      if (sc->dl) {
        if (sl->index >= sc->dl->ndecls) return NULL;
        a = &sc->dl->decls[sl->index].alias;
      } else {
        if (sl->index >= c->naliases) return NULL;
        a = &c->aliases[sl->index];
      }
      return alias_func_type(sc, a, at);
    }
    default: return NULL;
  }
}

static const CompType *core_export_type(const Component *c, uint32_t inst,
                                        const char *name) {
  if (inst >= c->ncore_instances) return NULL;
  const CmInstance *in = &c->core_instances[inst];
  if (!in->instantiate) {

    for (uint32_t i = 0; i < in->nexports; i++)
      if (!strcmp(in->export_names[i], name)) return NULL;
    return NULL;
  }
  if (in->index >= c->nmodules) return NULL;
  const Module *m = c->modules[in->index];
  if (!m) return NULL;
  for (uint32_t i = 0; i < m->nexports; i++) {
    if (m->exports[i].kind != EK_FUNC || strcmp(m->exports[i].name, name))
      continue;
    uint32_t fi = m->exports[i].index;
    if (fi >= m->nfuncs) return NULL;
    uint32_t ti = m->funcs[fi].type_index;
    return ti < m->ntypes ? &m->types[ti] : NULL;
  }
  return NULL;
}

static const CompType *core_import_type(const Component *c, uint32_t cfunc) {
  for (uint32_t i = 0; i < c->ncore_instances; i++) {
    const CmInstance *inl = &c->core_instances[i];
    if (inl->instantiate) continue;
    for (uint32_t e = 0; e < inl->nexports; e++) {
      const CmSortIdx *si = &inl->exports[e];
      if (si->sort != CMS_CORE || si->core_sort != CMCS_FUNC ||
          si->index != cfunc)
        continue;

      for (uint32_t j = 0; j < c->ncore_instances; j++) {
        const CmInstance *ins = &c->core_instances[j];
        if (!ins->instantiate || ins->index >= c->nmodules) continue;
        for (uint32_t a = 0; a < ins->nargs; a++) {
          if (ins->args[a].index != i) continue;
          const Module *m = c->modules[ins->index];
          if (!m) continue;
          for (uint32_t k = 0; k < m->nimports; k++) {
            if (m->imports[k].kind != EK_FUNC) continue;
            if (strcmp(m->imports[k].module, ins->arg_names[a])) continue;
            if (strcmp(m->imports[k].name, inl->export_names[e])) continue;
            uint32_t ti = m->imports[k].type_index;
            return ti < m->ntypes ? &m->types[ti] : NULL;
          }
        }
      }
    }
  }
  return NULL;
}

static bool lower_cfunc(const Component *c, uint32_t canon, uint32_t *out) {
  for (uint32_t i = 0; i < c->ncfunc_space; i++)
    if (c->cfunc_space[i].kind == CMCF_LOWER && c->cfunc_space[i].index == canon) {
      *out = i;
      return true;
    }
  return false;
}

static const CompType *core_func_type(const Component *c, uint32_t i) {
  if (i >= c->ncfunc_space) return NULL;
  const CmCoreFuncSlot *sl = &c->cfunc_space[i];
  if (sl->kind == CMCF_ALIAS) {
    if (sl->index >= c->naliases) return NULL;
    const CmAlias *a = &c->aliases[sl->index];
    if (a->kind != CMA_CORE_EXPORT) return NULL;
    return core_export_type(c, a->instance, a->name);
  }
  if (sl->kind == CMCF_LOWER) return core_import_type(c, i);
  return NULL;
}

static uint8_t core_of(CanonFlat f) {
  switch (f) {
    case CF_I32: return VT_I32;
    case CF_I64: return VT_I64;
    case CF_F32: return VT_F32;
    default:     return VT_F64;
  }
}

static bool sig_matches(const CanonSig *sg, const CompType *ct) {
  if (ct->kind != CT_FUNC) return false;
  if (ct->nparams != sg->nparams || ct->nresults != sg->nresults) return false;
  for (uint32_t i = 0; i < sg->nparams; i++)
    if (ct->params[i] != core_of(sg->params[i])) return false;
  for (uint32_t i = 0; i < sg->nresults; i++)
    if (ct->results[i] != core_of(sg->results[i])) return false;
  return true;
}

static void dump_canons(const Component *c, bool ptr64, FILE *out) {
  CanonScope top = { NULL, c, NULL };
  CanonCtx cx = { c, &top, ptr64, CSE_UTF8, CSE_UTF8, 0, 0 };
  for (uint32_t i = 0; i < c->ncanons; i++) {
    const CmCanon *cn = &c->canons[i];
    if (cn->kind != CMC_LIFT && cn->kind != CMC_LOWER) continue;
    bool lift = cn->kind == CMC_LIFT;
    CanonOpts o;
    canon_options(cn, lift, &o);
    static const char *const enc[] = { "utf8", "utf16", "latin1+utf16" };
    fprintf(out, "canon-def %u %s enc=%s", i, lift ? "lift" : "lower",
            enc[o.string_encoding]);
    if (o.has_memory) fprintf(out, " memory=%u", o.memory);
    if (o.has_realloc) fprintf(out, " realloc=%u", o.realloc);
    if (o.has_post_return) fprintf(out, " post-return=%u", o.post_return);
    if (o.is_async) fputs(" async", out);

    const CmFuncType *ft = NULL;
    CanonScope at = top;
    if (lift) {
      const CmType *t = canon_type_in(&top, cn->type);
      if (t && t->kind == CMT_FUNC) ft = &t->func;
    } else {
      ft = canon_func_type_in(&top, cn->func, &at);
    }
    if (ft) {

      CanonCtx fx = cx;
      fx.scope = &at;
      fputs(" sig=(", out);
      for (uint32_t k = 0; k < ft->nparams; k++) {
        if (k) fputc(',', out);
        put_type(&fx, ft->params[k].t, out);
      }
      fputs(")->(", out);
      if (ft->has_result) put_type(&fx, ft->result, out);
      fputs(") core=", out);
      CanonSig sg;
      canon_flatten_functype(&fx, ft, lift, &sg);
      put_sig(&sg, out);
      bool need = canon_needs_memory(&fx, ft, lift);
      fprintf(out, " needs-memory=%s", need ? "yes" : "no");

      uint32_t cfi = cn->func;
      bool have = lift || lower_cfunc(c, i, &cfi);
      const CompType *ct = have ? core_func_type(c, cfi) : NULL;
      if (ct) fprintf(out, " declared=%s", sig_matches(&sg, ct) ? "same" : "DIFFERS");
      if (need && !o.has_memory) fputs(" MISSING-MEMORY", out);
    }
    fputc('\n', out);
  }
}

void canon_dump(const Component *c, bool ptr64, FILE *out) {
  CanonScope top = { NULL, c, NULL };
  dump_scope(&top, ptr64, "", out);
  dump_canons(c, ptr64, out);
}

void canon_options(const CmCanon *cn, bool lift, CanonOpts *out) {
  memset(out, 0, sizeof *out);
  out->string_encoding = CSE_UTF8;
  bool seen_enc = false;
  for (uint32_t i = 0; i < cn->nopts; i++) {
    const CmCanonOpt *o = &cn->opts[i];
    switch (o->code) {
      case 0x00: case 0x01: case 0x02:
        if (seen_enc)
          warmc_invalid("a canonical definition names more than one string encoding");
        seen_enc = true;
        out->string_encoding = (CanonStrEnc)o->code;
        break;
      case 0x03:
        if (out->has_memory) warmc_invalid("a canonical definition names two memories");
        out->has_memory = true; out->memory = o->index; break;
      case 0x04:
        if (out->has_realloc) warmc_invalid("a canonical definition names two reallocs");
        out->has_realloc = true; out->realloc = o->index; break;
      case 0x05:
        if (out->has_post_return)
          warmc_invalid("a canonical definition names two post-returns");
        out->has_post_return = true; out->post_return = o->index; break;
      case 0x06: out->is_async = true; break;
      case 0x07:
        if (out->has_callback) warmc_invalid("a canonical definition names two callbacks");
        out->has_callback = true; out->callback = o->index; break;
      default:
        warmc_invalid("unknown canonical option 0x%02x", o->code);
    }
  }

  if (out->has_post_return && !lift)
    warmc_invalid("post-return is only allowed on canon lift");
  if (out->has_post_return && out->is_async)
    warmc_invalid("post-return and async cannot both be given");
  if (out->has_callback && !out->is_async)
    warmc_invalid("callback is only allowed with async");
}

static bool has_pointer(const CanonCtx *cx, CmValType t) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  if (!d) return p == CMP_STRING;
  switch (d->kind) {
    case CMD_PRIM: return d->prim == CMP_STRING;
    case CMD_LIST: return true;
    case CMD_FIXED_LIST: return has_pointer(cx, d->elem);
    case CMD_RECORD:
      for (uint32_t i = 0; i < d->nfields; i++)
        if (has_pointer(cx, d->fields[i].t)) return true;
      return false;
    case CMD_TUPLE:
      for (uint32_t i = 0; i < d->nelems; i++)
        if (has_pointer(cx, d->elems[i])) return true;
      return false;
    default: break;
  }
  if (is_variant_like(d)) {
    uint32_t n = variant_ncases(d);
    for (uint32_t i = 0; i < n; i++) {
      CmValType ct;
      if (variant_case(d, i, &ct) && has_pointer(cx, ct)) return true;
    }
  }
  return false;
}

static bool has_kind(const CanonCtx *cx, CmValType t, bool handles) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  if (!d) return !handles && p == CMP_STRING;
  switch (d->kind) {
    case CMD_PRIM: return !handles && d->prim == CMP_STRING;
    case CMD_OWN: case CMD_BORROW: return handles;
    case CMD_LIST: case CMD_FIXED_LIST: return has_kind(cx, d->elem, handles);
    case CMD_RECORD:
      for (uint32_t i = 0; i < d->nfields; i++)
        if (has_kind(cx, d->fields[i].t, handles)) return true;
      return false;
    case CMD_TUPLE:
      for (uint32_t i = 0; i < d->nelems; i++)
        if (has_kind(cx, d->elems[i], handles)) return true;
      return false;
    default: break;
  }
  if (is_variant_like(d)) {
    uint32_t n = variant_ncases(d);
    for (uint32_t i = 0; i < n; i++) {
      CmValType ct;
      if (variant_case(d, i, &ct) && has_kind(cx, ct, handles)) return true;
    }
  }
  return false;
}

bool canon_needs_memory(const CanonCtx *cx, const CmFuncType *ft, bool lift) {
  for (uint32_t i = 0; i < ft->nparams; i++)
    if (has_pointer(cx, ft->params[i].t)) return true;
  if (ft->has_result && has_pointer(cx, ft->result)) return true;

  uint32_t flat_params = 0;
  for (uint32_t i = 0; i < ft->nparams; i++)
    flat_params += canon_flatten(cx, ft->params[i].t, NULL, 0);
  if (flat_params > CANON_MAX_FLAT_PARAMS) return true;
  if (ft->has_result && canon_flatten(cx, ft->result, NULL, 0) > CANON_MAX_FLAT_RESULTS)
    return true;
  (void)lift;
  return false;
}

typedef struct { char *s; size_t n, cap; } Wat;

static void bufp(Wat *b, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  for (;;) {
    size_t room = b->cap - b->n;
    va_list c;
    va_copy(c, ap);
    int k = vsnprintf(b->s + b->n, room, fmt, c);
    va_end(c);
    if (k < 0) warmc_fatal("adapter: cannot format");
    if ((size_t)k < room) { b->n += (size_t)k; break; }
    size_t want = b->n + (size_t)k + 1;
    b->cap = b->cap ? b->cap : 256;
    while (b->cap < want) b->cap *= 2;
    b->s = xrealloc(b->s, b->cap);
  }
  va_end(ap);
}

typedef struct {
  Wat body;
  Wat decls;
  uint32_t nlocals;
} Fn;

static uint32_t fn_local(Fn *f, const char *ty) {
  bufp(&f->decls, "    (local $l%u %s)\n", f->nlocals, ty);
  return f->nlocals++;
}

static const char *adapter_gap(const CanonCtx *cx, CmValType t) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  if (!d) {
    if (!p) return "a bare handle with no resource named";

    if (p == CMP_STRING && cx->str_enc != CSE_UTF8)
      return "a string in an encoding other than utf8, which is transcoding";
    if (p == CMP_ERROR_CONTEXT) return "an error-context";
    return NULL;
  }
  switch (d->kind) {
    case CMD_LIST: case CMD_FIXED_LIST:
      return adapter_gap(cx, d->elem);
    case CMD_OWN: case CMD_BORROW:
      return NULL;
    case CMD_STREAM: case CMD_FUTURE:
      return "a stream or future";
    case CMD_RECORD:
      for (uint32_t i = 0; i < d->nfields; i++) {
        const char *g = adapter_gap(cx, d->fields[i].t);
        if (g) return g;
      }
      return NULL;
    case CMD_TUPLE:
      for (uint32_t i = 0; i < d->nelems; i++) {
        const char *g = adapter_gap(cx, d->elems[i]);
        if (g) return g;
      }
      return NULL;
    default:
      if (is_variant_like(d)) {
        uint32_t n = variant_ncases(d);
        for (uint32_t i = 0; i < n; i++) {
          CmValType ct;
          if (!variant_case(d, i, &ct)) continue;
          const char *g = adapter_gap(cx, ct);
          if (g) return g;
        }
      }
      return NULL;
  }
}

static uint32_t payload_offset(const CanonCtx *cx, const CmDefVal *d) {
  uint32_t dw = prim_size(cx, discriminant_prim(variant_ncases(d)));
  return align_to(dw, max_case_align(cx, d));
}

#define ADAPTER_CALLEE_MEM 1u

static const char *memi(uint32_t m) {
  static const char *const s[] = { "", " 1", " 2", " 3" };
  if (m >= sizeof s / sizeof *s) warmc_fatal("adapter: memory %u is too high", m);
  return s[m];
}

static const char *prim_load(uint8_t p) {
  switch (p) {
    case CMP_BOOL: case CMP_U8: return "i32.load8_u";
    case CMP_S8:               return "i32.load8_s";
    case CMP_U16:              return "i32.load16_u";
    case CMP_S16:              return "i32.load16_s";
    case CMP_S32: case CMP_U32: case CMP_CHAR: case CMP_ERROR_CONTEXT:
                               return "i32.load";
    case CMP_S64: case CMP_U64: return "i64.load";
    case CMP_F32:              return "f32.load";
    default:                   return "f64.load";
  }
}

static const char *prim_store(uint8_t p) {
  switch (p) {
    case CMP_BOOL: case CMP_U8: case CMP_S8: return "i32.store8";
    case CMP_U16: case CMP_S16:              return "i32.store16";
    case CMP_S32: case CMP_U32: case CMP_CHAR: case CMP_ERROR_CONTEXT:
                                             return "i32.store";
    case CMP_S64: case CMP_U64:              return "i64.store";
    case CMP_F32:                            return "f32.store";
    default:                                 return "f64.store";
  }
}

static void coerce_up(Fn *f, CanonFlat have, CanonFlat want) {
  if (have == want) return;
  if (have == CF_F32 && want == CF_I32) {
    bufp(&f->body, "    i32.reinterpret_f32\n"); return;
  }
  if (have == CF_F32 && want == CF_I64) {
    bufp(&f->body, "    i32.reinterpret_f32\n    i64.extend_i32_u\n"); return;
  }
  if (have == CF_F64 && want == CF_I64) {
    bufp(&f->body, "    i64.reinterpret_f64\n"); return;
  }
  if (have == CF_I32 && want == CF_I64) {
    bufp(&f->body, "    i64.extend_i32_u\n"); return;
  }
  warmc_fatal("adapter: cannot widen %s into %s",
              canon_flat_name(have), canon_flat_name(want));
}

static void coerce_down(Fn *f, CanonFlat have, CanonFlat want) {
  if (have == want) return;
  if (have == CF_I32 && want == CF_F32) {
    bufp(&f->body, "    f32.reinterpret_i32\n"); return;
  }
  if (have == CF_I64 && want == CF_F32) {
    bufp(&f->body, "    i32.wrap_i64\n    f32.reinterpret_i32\n"); return;
  }
  if (have == CF_I64 && want == CF_F64) {
    bufp(&f->body, "    f64.reinterpret_i64\n"); return;
  }
  if (have == CF_I64 && want == CF_I32) {
    bufp(&f->body, "    i32.wrap_i64\n"); return;
  }
  warmc_fatal("adapter: cannot narrow %s into %s",
              canon_flat_name(have), canon_flat_name(want));
}

static uint32_t handle_rt(const CanonCtx *cx, CmValType t) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  if (!d || (d->kind != CMD_OWN && d->kind != CMD_BORROW)) return 0;
  return d->elem.is_index ? d->elem.index + 1 : 0;
}

static void lift_prim(Fn *f, uint8_t p) {
  if (p == CMP_BOOL) {
    bufp(&f->body, "    i32.const 0\n    i32.ne\n");
    return;
  }
  if (p != CMP_CHAR) return;
  uint32_t c = fn_local(f, "i32");
  bufp(&f->body,
       "    local.set $l%u\n"
       "    local.get $l%u\n    i32.const 1114112\n    i32.ge_u\n"
       "    if\n    unreachable\n    end\n"

       "    local.get $l%u\n    i32.const 55296\n    i32.sub\n"
       "    i32.const 2048\n    i32.lt_u\n"
       "    if\n    unreachable\n    end\n"
       "    local.get $l%u\n", c, c, c, c);
}

static void narrow_flat(Fn *f, uint8_t p) {
  switch (p) {
    case CMP_U8:  bufp(&f->body, "    i32.const 255\n    i32.and\n"); break;
    case CMP_U16: bufp(&f->body, "    i32.const 65535\n    i32.and\n"); break;
    case CMP_S8:  bufp(&f->body, "    i32.extend8_s\n"); break;
    case CMP_S16: bufp(&f->body, "    i32.extend16_s\n"); break;
    default: break;
  }
}

static void mask_flags(Fn *f, uint32_t nlabels) {
  if (nlabels < 32)
    bufp(&f->body, "    i32.const %u\n    i32.and\n", (1u << nlabels) - 1u);
}

static void zero_of(Fn *f, CanonFlat t) {
  bufp(&f->body, "    %s.const 0\n", canon_flat_name(t));
}

static const char *string_mover(const CanonCtx *cx, char *buf, size_t n) {
  static const char *const m[3][3] = {
    { "$smove_utf8_utf8",  "$smove_utf8_utf16",  "$smove_utf8_l1u16"  },
    { "$smove_utf16_utf8", "$smove_utf16_utf16", "$smove_utf16_l1u16" },
    { "$smove_l1u16_utf8", "$smove_l1u16_utf16", "$smove_l1u16_l1u16" },
  };
  const char *base = m[cx->str_enc][cx->to_enc];
  if (cx->str_enc == CSE_UTF8 && cx->to_enc == CSE_UTF8)
    snprintf(buf, n, "%s_%u_%u", base, cx->mem_from, cx->mem_to);
  else if (cx->mem_from || cx->mem_to)
    warmc_fatal("adapter: %s is only emitted over memory 0", base);
  else
    snprintf(buf, n, "%s", base);
  return buf;
}

static void emit_load(Fn *f, const CanonCtx *cx, CmValType t, uint32_t base,
                      uint32_t off, const CanonFlat *want, uint32_t *slot);
static void emit_store(Fn *f, const CanonCtx *cx, CmValType t, uint32_t base,
                       uint32_t off, const CanonFlat *have,
                       const uint32_t *from, uint32_t *slot);

static void emit_copy(Fn *f, const CanonCtx *cx, CmValType t,
                      uint32_t src, uint32_t soff, uint32_t dst, uint32_t doff);

static uint32_t emit_list_copy(Fn *f, const CanonCtx *cx, CmValType elem,
                               uint32_t srcp, uint32_t n) {
  uint32_t esize = canon_size(cx, elem), ealign = canon_align(cx, elem);
  uint32_t dstp = fn_local(f, "i32");
  uint32_t i = fn_local(f, "i32");
  uint32_t es = fn_local(f, "i32");
  uint32_t ed = fn_local(f, "i32");
  uint32_t bl = fn_local(f, "i64");

  if (ealign > 1)
    bufp(&f->body,
         "    local.get $l%u\n    i32.const %u\n    i32.and\n"
         "    if\n    unreachable\n    end\n", srcp, ealign - 1);

  bufp(&f->body,
       "    local.get $l%u\n    i64.extend_i32_u\n    i64.const %u\n"
       "    i64.mul\n    local.tee $l%u\n    i64.const 268435455\n"
       "    i64.gt_u\n    if\n    unreachable\n    end\n", n, esize, bl);

  bufp(&f->body,
       "    local.get $l%u\n    i64.extend_i32_u\n    local.get $l%u\n"
       "    i64.add\n    memory.size%s\n    i64.extend_i32_u\n"
       "    i64.const 65536\n    i64.mul\n    i64.gt_u\n"
       "    if\n    unreachable\n    end\n", srcp, bl, memi(cx->mem_from));
  bufp(&f->body,
       "    i32.const 0\n    i32.const 0\n    i32.const %u\n"
       "    local.get $l%u\n    i32.wrap_i64\n    call $realloc_%u\n"
       "    local.set $l%u\n", ealign, bl, cx->mem_to, dstp);

  bufp(&f->body, "    i32.const 0\n    local.set $l%u\n", i);
  bufp(&f->body, "    block\n    loop\n");
  bufp(&f->body,
       "    local.get $l%u\n    local.get $l%u\n    i32.ge_u\n    br_if 1\n", i, n);
  bufp(&f->body,
       "    local.get $l%u\n    local.get $l%u\n    i32.const %u\n    i32.mul\n"
       "    i32.add\n    local.set $l%u\n", srcp, i, esize, es);
  bufp(&f->body,
       "    local.get $l%u\n    local.get $l%u\n    i32.const %u\n    i32.mul\n"
       "    i32.add\n    local.set $l%u\n", dstp, i, esize, ed);
  emit_copy(f, cx, elem, es, 0, ed, 0);
  bufp(&f->body,
       "    local.get $l%u\n    i32.const 1\n    i32.add\n    local.set $l%u\n"
       "    br 0\n    end\n    end\n", i, i);
  return dstp;
}

static void emit_copy(Fn *f, const CanonCtx *cx, CmValType t,
                      uint32_t src, uint32_t soff, uint32_t dst, uint32_t doff) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  const char *mf = memi(cx->mem_from), *mt = memi(cx->mem_to);
  if ((!d && p == CMP_STRING) || (d && d->kind == CMD_PRIM && d->prim == CMP_STRING)) {
    uint32_t ps = ptr_size(cx);
    uint32_t sp = fn_local(f, "i32"), n = fn_local(f, "i32"), dp = fn_local(f, "i32");
    char mv[64];
    bufp(&f->body, "    local.get $l%u\n    i32.load%s offset=%u\n    local.set $l%u\n",
         src, mf, soff, sp);
    bufp(&f->body, "    local.get $l%u\n    i32.load%s offset=%u\n    local.set $l%u\n",
         src, mf, soff + ps, n);
    uint32_t no = fn_local(f, "i32");
    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    call %s\n"
                   "    local.set $l%u\n    local.set $l%u\n",
         sp, n, string_mover(cx, mv, sizeof mv), no, dp);
    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    i32.store%s offset=%u\n",
         dst, dp, mt, doff);
    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    i32.store%s offset=%u\n",
         dst, no, mt, doff + ps);
    return;
  }
  if (d && (d->kind == CMD_OWN || d->kind == CMD_BORROW)) {

    bufp(&f->body, "    local.get $l%u\n"
                   "    global.get $dst_table\n"
                   "    global.get $src_table\n"
                   "    local.get $l%u\n    i32.load%s offset=%u\n"
                   "    i32.const %u\n    call %s\n"
                   "    i32.const %u\n%s    call %s\n    i32.store%s offset=%u\n",
         dst, src, mf, soff, handle_rt(cx, t),
         d->kind == CMD_OWN ? "$lift_own" : "$lift_borrow",
         handle_rt(cx, t),
         d->kind == CMD_OWN ? "" : "    i32.const 0\n",
         d->kind == CMD_OWN ? "$lower_own" : "$lower_borrow", mt, doff);
    return;
  }
  if (!d || d->kind == CMD_PRIM) {
    uint8_t pp = d ? d->prim : p;

    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    %s%s offset=%u\n",
         dst, src, prim_load(pp), mf, soff);
    lift_prim(f, pp);
    bufp(&f->body, "    %s%s offset=%u\n", prim_store(pp), mt, doff);
    return;
  }
  switch (d->kind) {
    case CMD_RECORD: case CMD_TUPLE: {
      uint32_t n = d->kind == CMD_RECORD ? d->nfields : d->nelems, at = 0;
      for (uint32_t i = 0; i < n; i++) {
        CmValType ft = d->kind == CMD_RECORD ? d->fields[i].t : d->elems[i];
        at = align_to(at, canon_align(cx, ft));
        emit_copy(f, cx, ft, src, soff + at, dst, doff + at);
        at += canon_size(cx, ft);
      }
      return;
    }
    case CMD_FIXED_LIST: {
      uint32_t esize = canon_size(cx, d->elem);
      for (uint32_t k = 0; k < d->len; k++)
        emit_copy(f, cx, d->elem, src, soff + k * esize, dst, doff + k * esize);
      return;
    }
    case CMD_LIST: {
      uint32_t ps = ptr_size(cx);
      uint32_t sp = fn_local(f, "i32"), n = fn_local(f, "i32");
      bufp(&f->body, "    local.get $l%u\n    i32.load%s offset=%u\n    local.set $l%u\n",
           src, mf, soff, sp);
      bufp(&f->body, "    local.get $l%u\n    i32.load%s offset=%u\n    local.set $l%u\n",
           src, mf, soff + ps, n);
      uint32_t dp = emit_list_copy(f, cx, d->elem, sp, n);
      bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    i32.store%s offset=%u\n",
           dst, dp, mt, doff);
      bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    i32.store%s offset=%u\n",
           dst, n, mt, doff + ps);
      return;
    }
    case CMD_FLAGS: {
      const char *ld = flags_bytes(d->nlabels) == 1 ? "i32.load8_u" :
                       flags_bytes(d->nlabels) == 2 ? "i32.load16_u" : "i32.load";
      const char *st = flags_bytes(d->nlabels) == 1 ? "i32.store8" :
                       flags_bytes(d->nlabels) == 2 ? "i32.store16" : "i32.store";
      bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    %s%s offset=%u\n",
           dst, src, ld, mf, soff);
      mask_flags(f, d->nlabels);
      bufp(&f->body, "    %s%s offset=%u\n", st, mt, doff);
      return;
    }
    default: break;
  }
  if (!is_variant_like(d)) warmc_fatal("adapter: cannot copy this type");

  uint32_t ncases = variant_ncases(d);
  uint8_t dp = discriminant_prim(ncases);
  uint32_t spoff = soff + payload_offset(cx, d);
  uint32_t dpoff = doff + payload_offset(cx, d);
  uint32_t dloc = fn_local(f, "i32");

  bufp(&f->body, "    local.get $l%u\n    %s%s offset=%u\n    local.set $l%u\n",
       src, prim_load(dp), mf, soff, dloc);
  bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.ge_u\n"
                 "    if\n    unreachable\n    end\n", dloc, ncases);
  bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    %s%s offset=%u\n",
       dst, dloc, prim_store(dp), mt, doff);

  uint32_t ends = 0;
  bool any = false;
  for (uint32_t i = 0; i < ncases; i++) {
    CmValType ct;
    if (!variant_case(d, i, &ct)) continue;
    any = true;
    bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.eq\n    if\n",
         dloc, i);
    emit_copy(f, cx, ct, src, spoff, dst, dpoff);
    bufp(&f->body, "    end\n");
    ends++;
  }
  (void)any; (void)ends;
}

static void emit_load(Fn *f, const CanonCtx *cx, CmValType t, uint32_t base,
                      uint32_t off, const CanonFlat *want, uint32_t *slot) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  const char *mf = memi(cx->mem_from);
  if ((!d && p == CMP_STRING) || (d && d->kind == CMD_PRIM && d->prim == CMP_STRING)) {

    uint32_t ps = ptr_size(cx);
    uint32_t sp = fn_local(f, "i32"), n = fn_local(f, "i32"), dp = fn_local(f, "i32");
    uint32_t no = fn_local(f, "i32");
    char mv[64];
    bufp(&f->body, "    local.get $l%u\n    i32.load%s offset=%u\n    local.set $l%u\n",
         base, mf, off, sp);
    bufp(&f->body, "    local.get $l%u\n    i32.load%s offset=%u\n    local.set $l%u\n",
         base, mf, off + ps, n);

    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    call %s\n"
                   "    local.set $l%u\n    local.set $l%u\n",
         sp, n, string_mover(cx, mv, sizeof mv), no, dp);
    bufp(&f->body, "    local.get $l%u\n", dp);
    coerce_up(f, CF_I32, want[*slot]);
    (*slot)++;
    bufp(&f->body, "    local.get $l%u\n", no);
    coerce_up(f, CF_I32, want[*slot]);
    (*slot)++;
    return;
  }
  if (d && (d->kind == CMD_OWN || d->kind == CMD_BORROW)) {

    bufp(&f->body, "    global.get $dst_table\n"
                   "    global.get $src_table\n"
                   "    local.get $l%u\n    i32.load%s offset=%u\n"
                   "    i32.const %u\n    call %s\n"
                   "    i32.const %u\n%s    call %s\n",
         base, mf, off, handle_rt(cx, t),
         d->kind == CMD_OWN ? "$lift_own" : "$lift_borrow",
         handle_rt(cx, t),
         d->kind == CMD_OWN ? "" : "    i32.const 0\n",
         d->kind == CMD_OWN ? "$lower_own" : "$lower_borrow");
    coerce_up(f, CF_I32, want[*slot]);
    (*slot)++;
    return;
  }
  if (!d || d->kind == CMD_PRIM) {
    uint8_t pp = d ? d->prim : p;
    bufp(&f->body, "    local.get $l%u\n    %s%s offset=%u\n",
         base, prim_load(pp), mf, off);
    lift_prim(f, pp);
    coerce_up(f, prim_flat(cx, pp), want[*slot]);
    (*slot)++;
    return;
  }
  switch (d->kind) {
    case CMD_RECORD: case CMD_TUPLE: {
      uint32_t n = d->kind == CMD_RECORD ? d->nfields : d->nelems, at = 0;
      for (uint32_t i = 0; i < n; i++) {
        CmValType ft = d->kind == CMD_RECORD ? d->fields[i].t : d->elems[i];
        at = align_to(at, canon_align(cx, ft));
        emit_load(f, cx, ft, base, off + at, want, slot);
        at += canon_size(cx, ft);
      }
      return;
    }
    case CMD_FIXED_LIST: {

      uint32_t esize = canon_size(cx, d->elem);
      for (uint32_t k = 0; k < d->len; k++)
        emit_load(f, cx, d->elem, base, off + k * esize, want, slot);
      return;
    }
    case CMD_LIST: {
      uint32_t ps = ptr_size(cx);
      uint32_t srcp = fn_local(f, "i32"), n = fn_local(f, "i32");
      bufp(&f->body, "    local.get $l%u\n    i32.load%s offset=%u\n    local.set $l%u\n",
           base, mf, off, srcp);
      bufp(&f->body, "    local.get $l%u\n    i32.load%s offset=%u\n    local.set $l%u\n",
           base, mf, off + ps, n);
      uint32_t dstp = emit_list_copy(f, cx, d->elem, srcp, n);
      bufp(&f->body, "    local.get $l%u\n", dstp);
      coerce_up(f, CF_I32, want[*slot]);
      (*slot)++;
      bufp(&f->body, "    local.get $l%u\n", n);
      coerce_up(f, CF_I32, want[*slot]);
      (*slot)++;
      return;
    }
    case CMD_FLAGS:

      bufp(&f->body, "    local.get $l%u\n    %s%s offset=%u\n", base,
           flags_bytes(d->nlabels) == 1 ? "i32.load8_u" :
           flags_bytes(d->nlabels) == 2 ? "i32.load16_u" : "i32.load", mf, off);
      mask_flags(f, d->nlabels);
      coerce_up(f, CF_I32, want[*slot]);
      (*slot)++;
      return;
    default: break;
  }
  if (!is_variant_like(d)) warmc_fatal("adapter: cannot load this type");

  uint32_t ncases = variant_ncases(d);
  uint8_t dp = discriminant_prim(ncases);
  uint32_t poff = off + payload_offset(cx, d);
  uint32_t vslot = *slot;
  uint32_t dloc = fn_local(f, "i32");

  bufp(&f->body, "    local.get $l%u\n    %s%s offset=%u\n    local.set $l%u\n",
       base, prim_load(dp), mf, off, dloc);

  bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.ge_u\n"
                 "    if\n    unreachable\n    end\n", dloc, ncases);
  bufp(&f->body, "    local.get $l%u\n", dloc);
  coerce_up(f, CF_I32, want[vslot]);

  uint32_t njoined = canon_flatten(cx, t, NULL, 0) - 1;
  if (njoined == 0) { *slot = vslot + 1; return; }

  uint32_t ends = 0;
  for (uint32_t i = 0; i < ncases; i++) {
    bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.eq\n    if (result",
         dloc, i);
    for (uint32_t k = 0; k < njoined; k++)
      bufp(&f->body, " %s", canon_flat_name(want[vslot + 1 + k]));
    bufp(&f->body, ")\n");
    ends++;
    CmValType ct;
    uint32_t s = vslot + 1;
    if (variant_case(d, i, &ct)) emit_load(f, cx, ct, base, poff, want, &s);

    for (; s < vslot + 1 + njoined; s++) zero_of(f, want[s]);
    bufp(&f->body, "    else\n");
  }
  bufp(&f->body, "    unreachable\n");
  for (uint32_t i = 0; i < ends; i++) bufp(&f->body, "    end\n");
  *slot = vslot + 1 + njoined;
}

static void emit_store(Fn *f, const CanonCtx *cx, CmValType t, uint32_t base,
                       uint32_t off, const CanonFlat *have,
                       const uint32_t *from, uint32_t *slot) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
  const char *mt = memi(cx->mem_to);

#define GET(k) do { \
    if (from) bufp(&f->body, "    local.get $l%u\n", from[k]); \
    else      bufp(&f->body, "    local.get $f%u\n", (k)); \
  } while (0)

  if ((!d && p == CMP_STRING) || (d && d->kind == CMD_PRIM && d->prim == CMP_STRING)) {
    uint32_t ps = ptr_size(cx);
    uint32_t sp = fn_local(f, "i32"), n = fn_local(f, "i32"), dp = fn_local(f, "i32");
    char mv[64];
    GET(*slot);
    coerce_down(f, have[*slot], CF_I32);
    bufp(&f->body, "    local.set $l%u\n", sp);
    (*slot)++;
    GET(*slot);
    coerce_down(f, have[*slot], CF_I32);
    bufp(&f->body, "    local.set $l%u\n", n);
    (*slot)++;
    uint32_t no = fn_local(f, "i32");
    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    call %s\n"
                   "    local.set $l%u\n    local.set $l%u\n",
         sp, n, string_mover(cx, mv, sizeof mv), no, dp);
    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    i32.store%s offset=%u\n",
         base, dp, mt, off);
    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    i32.store%s offset=%u\n",
         base, no, mt, off + ps);
    return;
  }
  if (d && (d->kind == CMD_OWN || d->kind == CMD_BORROW)) {
    bufp(&f->body, "    local.get $l%u\n", base);
    bufp(&f->body, "    global.get $dst_table\n"
                   "    global.get $src_table\n");
    GET(*slot);
    coerce_down(f, have[*slot], CF_I32);
    bufp(&f->body, "    i32.const %u\n    call %s\n"
                   "    i32.const %u\n%s    call %s\n    i32.store%s offset=%u\n",
         handle_rt(cx, t),
         d->kind == CMD_OWN ? "$lift_own" : "$lift_borrow",
         handle_rt(cx, t),
         d->kind == CMD_OWN ? "" : "    i32.const 0\n",
         d->kind == CMD_OWN ? "$lower_own" : "$lower_borrow", mt, off);
    (*slot)++;
    return;
  }
  if (!d || d->kind == CMD_PRIM) {
    uint8_t pp = d ? d->prim : p;
    bufp(&f->body, "    local.get $l%u\n", base);
    GET(*slot);
    coerce_down(f, have[*slot], prim_flat(cx, pp));
    lift_prim(f, pp);
    bufp(&f->body, "    %s%s offset=%u\n", prim_store(pp), mt, off);
    (*slot)++;
    return;
  }
  switch (d->kind) {
    case CMD_RECORD: case CMD_TUPLE: {
      uint32_t n = d->kind == CMD_RECORD ? d->nfields : d->nelems, at = 0;
      for (uint32_t i = 0; i < n; i++) {
        CmValType ft = d->kind == CMD_RECORD ? d->fields[i].t : d->elems[i];
        at = align_to(at, canon_align(cx, ft));
        emit_store(f, cx, ft, base, off + at, have, from, slot);
        at += canon_size(cx, ft);
      }
      return;
    }
    case CMD_FIXED_LIST: {
      uint32_t esize = canon_size(cx, d->elem);
      for (uint32_t k = 0; k < d->len; k++)
        emit_store(f, cx, d->elem, base, off + k * esize, have, from, slot);
      return;
    }
    case CMD_LIST: {
      uint32_t ps = ptr_size(cx);
      uint32_t srcp = fn_local(f, "i32"), n = fn_local(f, "i32");
      GET(*slot);
      coerce_down(f, have[*slot], CF_I32);
      bufp(&f->body, "    local.set $l%u\n", srcp);
      (*slot)++;
      GET(*slot);
      coerce_down(f, have[*slot], CF_I32);
      bufp(&f->body, "    local.set $l%u\n", n);
      (*slot)++;
      uint32_t dstp = emit_list_copy(f, cx, d->elem, srcp, n);
      bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    i32.store%s offset=%u\n",
           base, dstp, mt, off);
      bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    i32.store%s offset=%u\n",
           base, n, mt, off + ps);
      return;
    }
    case CMD_FLAGS:
      bufp(&f->body, "    local.get $l%u\n", base);
      GET(*slot);
      coerce_down(f, have[*slot], CF_I32);
      mask_flags(f, d->nlabels);
      bufp(&f->body, "    %s%s offset=%u\n",
           flags_bytes(d->nlabels) == 1 ? "i32.store8" :
           flags_bytes(d->nlabels) == 2 ? "i32.store16" : "i32.store", mt, off);
      (*slot)++;
      return;
    default: break;
  }
  if (!is_variant_like(d)) warmc_fatal("adapter: cannot store this type");

  uint32_t ncases = variant_ncases(d);
  uint8_t dp = discriminant_prim(ncases);
  uint32_t poff = off + payload_offset(cx, d);
  uint32_t vslot = *slot;
  uint32_t dloc = fn_local(f, "i32");

  GET(vslot);
  coerce_down(f, have[vslot], CF_I32);
  bufp(&f->body, "    local.set $l%u\n", dloc);

  bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.ge_u\n"
                 "    if\n    unreachable\n    end\n", dloc, ncases);
  bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    %s%s offset=%u\n",
       base, dloc, prim_store(dp), mt, off);

  uint32_t njoined = canon_flatten(cx, t, NULL, 0) - 1;
  if (njoined == 0) { *slot = vslot + 1; return; }

  uint32_t ends = 0;
  for (uint32_t i = 0; i < ncases; i++) {
    bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.eq\n    if\n",
         dloc, i);
    ends++;
    CmValType ct;
    uint32_t s = vslot + 1;
    if (variant_case(d, i, &ct))
      emit_store(f, cx, ct, base, poff, have, from, &s);
    bufp(&f->body, "    else\n");
  }
  bufp(&f->body, "    unreachable\n");
  for (uint32_t i = 0; i < ends; i++) bufp(&f->body, "    end\n");
  *slot = vslot + 1 + njoined;
#undef GET
}

static void emit_move(Fn *f, const CanonCtx *cx, CmValType t,
                      const CanonFlat *flat, const uint32_t *from,
                      uint32_t *slot) {
  uint8_t p;
  const CmDefVal *d = resolve(cx, t, &p);
#define GET(k) do { \
    if (from) bufp(&f->body, "    local.get $l%u\n", from[k]); \
    else      bufp(&f->body, "    local.get $f%u\n", (k)); \
  } while (0)

  if ((!d && p == CMP_STRING) || (d && d->kind == CMD_PRIM && d->prim == CMP_STRING)) {
    uint32_t sp = fn_local(f, "i32"), n = fn_local(f, "i32");
    uint32_t dp = fn_local(f, "i32"), no = fn_local(f, "i32");
    char mv[64];
    GET(*slot);
    coerce_down(f, flat[*slot], CF_I32);
    bufp(&f->body, "    local.set $l%u\n", sp);
    (*slot)++;
    GET(*slot);
    coerce_down(f, flat[*slot], CF_I32);
    bufp(&f->body, "    local.set $l%u\n", n);
    bufp(&f->body, "    local.get $l%u\n    local.get $l%u\n    call %s\n"
                   "    local.set $l%u\n    local.set $l%u\n",
         sp, n, string_mover(cx, mv, sizeof mv), no, dp);
    bufp(&f->body, "    local.get $l%u\n", dp);
    coerce_up(f, CF_I32, flat[*slot - 1]);
    bufp(&f->body, "    local.get $l%u\n", no);
    coerce_up(f, CF_I32, flat[*slot]);
    (*slot)++;
    return;
  }
  if (d && (d->kind == CMD_OWN || d->kind == CMD_BORROW)) {
    bufp(&f->body, "    global.get $dst_table\n"
                   "    global.get $src_table\n");
    GET(*slot);
    coerce_down(f, flat[*slot], CF_I32);
    bufp(&f->body, "    i32.const %u\n    call %s\n"
                   "    i32.const %u\n%s    call %s\n",
         handle_rt(cx, t),
         d->kind == CMD_OWN ? "$lift_own" : "$lift_borrow",
         handle_rt(cx, t),
         d->kind == CMD_OWN ? "" : "    i32.const 0\n",
         d->kind == CMD_OWN ? "$lower_own" : "$lower_borrow");
    coerce_up(f, CF_I32, flat[*slot]);
    (*slot)++;
    return;
  }
  if (!d || d->kind == CMD_PRIM) {

    uint8_t pp = d ? d->prim : p;
    GET(*slot);
    coerce_down(f, flat[*slot], prim_flat(cx, pp));
    narrow_flat(f, pp);
    lift_prim(f, pp);
    coerce_up(f, prim_flat(cx, pp), flat[*slot]);
    (*slot)++;
    return;
  }
  switch (d->kind) {
    case CMD_RECORD: case CMD_TUPLE: {
      uint32_t n = d->kind == CMD_RECORD ? d->nfields : d->nelems;
      for (uint32_t i = 0; i < n; i++)
        emit_move(f, cx, d->kind == CMD_RECORD ? d->fields[i].t : d->elems[i],
                  flat, from, slot);
      return;
    }
    case CMD_FIXED_LIST:
      for (uint32_t k = 0; k < d->len; k++)
        emit_move(f, cx, d->elem, flat, from, slot);
      return;
    case CMD_LIST: {
      uint32_t srcp = fn_local(f, "i32"), n = fn_local(f, "i32");
      GET(*slot);
      coerce_down(f, flat[*slot], CF_I32);
      bufp(&f->body, "    local.set $l%u\n", srcp);
      (*slot)++;
      GET(*slot);
      coerce_down(f, flat[*slot], CF_I32);
      bufp(&f->body, "    local.set $l%u\n", n);
      uint32_t dstp = emit_list_copy(f, cx, d->elem, srcp, n);
      bufp(&f->body, "    local.get $l%u\n", dstp);
      coerce_up(f, CF_I32, flat[*slot - 1]);
      bufp(&f->body, "    local.get $l%u\n", n);
      coerce_up(f, CF_I32, flat[*slot]);
      (*slot)++;
      return;
    }
    case CMD_FLAGS:
      GET(*slot);
      coerce_down(f, flat[*slot], CF_I32);
      mask_flags(f, d->nlabels);
      coerce_up(f, CF_I32, flat[*slot]);
      (*slot)++;
      return;
    default: break;
  }
  if (!is_variant_like(d)) warmc_fatal("adapter: cannot move this type");

  uint32_t ncases = variant_ncases(d);
  uint32_t vslot = *slot;
  uint32_t dloc = fn_local(f, "i32");

  GET(vslot);
  coerce_down(f, flat[vslot], CF_I32);
  bufp(&f->body, "    local.set $l%u\n", dloc);
  bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.ge_u\n"
                 "    if\n    unreachable\n    end\n", dloc, ncases);
  bufp(&f->body, "    local.get $l%u\n", dloc);
  coerce_up(f, CF_I32, flat[vslot]);

  uint32_t njoined = canon_flatten(cx, t, NULL, 0) - 1;
  if (njoined == 0) { *slot = vslot + 1; return; }

  uint32_t ends = 0;
  for (uint32_t i = 0; i < ncases; i++) {
    bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.eq\n    if (result",
         dloc, i);
    for (uint32_t k = 0; k < njoined; k++)
      bufp(&f->body, " %s", canon_flat_name(flat[vslot + 1 + k]));
    bufp(&f->body, ")\n");
    ends++;
    CmValType ct;
    uint32_t s = vslot + 1;
    if (variant_case(d, i, &ct)) emit_move(f, cx, ct, flat, from, &s);
    for (; s < vslot + 1 + njoined; s++) zero_of(f, flat[s]);
    bufp(&f->body, "    else\n");
  }
  bufp(&f->body, "    unreachable\n");
  for (uint32_t i = 0; i < ends; i++) bufp(&f->body, "    end\n");
  *slot = vslot + 1 + njoined;
#undef GET
}

static void fn_flush(Wat *out, Fn *f) {
  if (f->decls.s) bufp(out, "%s", f->decls.s);
  if (f->body.s) bufp(out, "%s", f->body.s);
  xfree(f->decls.s);
  xfree(f->body.s);
}

static void emit_ptr_check(Fn *f, uint32_t mem, uint32_t p, uint32_t align,
                           uint32_t size) {
  if (align > 1)
    bufp(&f->body, "    local.get $l%u\n    i32.const %u\n    i32.and\n"
                   "    if\n    unreachable\n    end\n", p, align - 1);
  bufp(&f->body,
       "    local.get $l%u\n    i64.extend_i32_u\n    i64.const %u\n    i64.add\n"
       "    memory.size%s\n    i64.extend_i32_u\n    i64.const 65536\n    i64.mul\n"
       "    i64.gt_u\n    if\n    unreachable\n    end\n", p, size, memi(mem));
}

static uint32_t emit_alloc_raw(Fn *f, const CanonCtx *cx, uint32_t align,
                               uint32_t size) {
  uint32_t p = fn_local(f, "i32");
  bufp(&f->body, "    i32.const 0\n    i32.const 0\n    i32.const %u\n"
                 "    i32.const %u\n    call $realloc_%u\n    local.set $l%u\n",
       align, size, cx->mem_to, p);
  return p;
}

static uint32_t emit_alloc(Fn *f, const CanonCtx *cx, CmValType t) {
  return emit_alloc_raw(f, cx, canon_align(cx, t), canon_size(cx, t));
}

static uint32_t params_align(const CanonCtx *cx, const CmFuncType *ft) {
  uint32_t a = 1;
  for (uint32_t i = 0; i < ft->nparams; i++) {
    uint32_t fa = canon_align(cx, ft->params[i].t);
    if (fa > a) a = fa;
  }
  return a;
}

static uint32_t params_size(const CanonCtx *cx, const CmFuncType *ft) {
  uint32_t at = 0;
  for (uint32_t i = 0; i < ft->nparams; i++) {
    at = align_to(at, canon_align(cx, ft->params[i].t));
    at += canon_size(cx, ft->params[i].t);
  }
  return align_to(at, params_align(cx, ft));
}

static void emit_params_copy(Fn *f, const CanonCtx *cx, const CmFuncType *ft,
                             uint32_t src, uint32_t dst) {
  uint32_t at = 0;
  for (uint32_t i = 0; i < ft->nparams; i++) {
    at = align_to(at, canon_align(cx, ft->params[i].t));
    emit_copy(f, cx, ft->params[i].t, src, at, dst, at);
    at += canon_size(cx, ft->params[i].t);
  }
}

static void emit_params_move(Fn *f, const CanonCtx *cx, const CmFuncType *ft,
                             const CanonFlat *flat, uint32_t *slot) {
  for (uint32_t i = 0; i < ft->nparams; i++)
    emit_move(f, cx, ft->params[i].t, flat, NULL, slot);
}

static uint32_t flatten_params(const CanonCtx *cx, const CmFuncType *ft,
                               CanonFlat *out, uint32_t cap) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < ft->nparams; i++) {
    CanonFlat tmp[CANON_MAX_SIG * 4];
    uint32_t got = canon_flatten(cx, ft->params[i].t, tmp,
                                 sizeof tmp / sizeof tmp[0]);
    for (uint32_t k = 0; k < got; k++, n++)
      if (n < cap) out[n] = tmp[k];
  }
  return n;
}

static void adapter_body(Fn *f, const CanonCtx *dn, const CanonCtx *up,
                         const CmFuncType *ft, bool pspill, bool rspill,
                         const char *callee, const char *post) {
  CanonFlat pf[CANON_MAX_SIG * 4], rf[CANON_MAX_SIG * 4];
  uint32_t npf = flatten_params(dn, ft, pf, sizeof pf / sizeof pf[0]);
  uint32_t nrf = 0;
  if (ft->has_result)
    nrf = canon_flatten(up, ft->result, rf, sizeof rf / sizeof rf[0]);
  (void)npf;

  if (pspill) {

    uint32_t align = params_align(dn, ft), size = params_size(dn, ft);
    uint32_t sp = fn_local(f, "i32");
    bufp(&f->body, "    local.get $p\n    local.set $l%u\n", sp);
    emit_ptr_check(f, dn->mem_from, sp, align, size);
    uint32_t ap = emit_alloc_raw(f, dn, align, size);
    emit_params_copy(f, dn, ft, sp, ap);
    bufp(&f->body, "    local.get $l%u\n", ap);
  } else {
    uint32_t slot = 0;
    emit_params_move(f, dn, ft, pf, &slot);
  }
  bufp(&f->body, "    call %s\n", callee);

  if (rspill) {

    uint32_t align = canon_align(up, ft->result);
    uint32_t size = canon_size(up, ft->result);
    uint32_t rp = fn_local(f, "i32");
    bufp(&f->body, "    local.set $l%u\n", rp);
    emit_ptr_check(f, up->mem_from, rp, align, size);
    uint32_t op = fn_local(f, "i32");
    bufp(&f->body, "    local.get $ret\n    local.set $l%u\n", op);
    emit_ptr_check(f, up->mem_to, op, align, size);
    emit_copy(f, up, ft->result, rp, 0, op, 0);
    if (post) bufp(&f->body, "    local.get $l%u\n    call %s\n", rp, post);
    return;
  }

  uint32_t *keep = xmalloc((nrf ? nrf : 1) * sizeof *keep);
  for (uint32_t k = nrf; k-- > 0; ) {
    keep[k] = fn_local(f, canon_flat_name(rf[k]));
    bufp(&f->body, "    local.set $l%u\n", keep[k]);
  }
  uint32_t slot = 0;
  if (ft->has_result) emit_move(f, up, ft->result, rf, keep, &slot);
  if (post) {
    for (uint32_t k = 0; k < nrf; k++)
      bufp(&f->body, "    local.get $l%u\n", keep[k]);
    bufp(&f->body, "    call %s\n", post);
  }
  xfree(keep);
}

static void emit_adapter(Wat *b, const CanonCtx *dn, const CanonCtx *up,
                         const CanonCtx *cal, const CmFuncType *ft,
                         const CanonFlat *fl, uint32_t nf,
                         bool pspill, bool rspill, const char *path,
                         uint32_t i) {
  CmValType t = ft->result;

  bufp(b, "  (func $callee_%s%u", path, i);
  if (pspill) bufp(b, " (param $p i32)");
  else for (uint32_t k = 0; k < nf; k++)
    bufp(b, " (param $f%u %s)", k, canon_flat_name(fl[k]));
  bufp(b, " (result");
  if (rspill) bufp(b, " i32");
  else for (uint32_t k = 0; k < nf; k++) bufp(b, " %s", canon_flat_name(fl[k]));
  bufp(b, ")\n");
  if (pspill) {

    bufp(b, "    local.get $p\n    global.get $skew\n    i32.add\n");
  } else if (rspill) {

    Fn cf = { 0 };
    uint32_t base = emit_alloc(&cf, cal, t);
    uint32_t slot = 0;
    emit_store(&cf, cal, t, base, 0, fl, NULL, &slot);
    bufp(&cf.body, "    local.get $l%u\n    global.get $skew\n    i32.add\n", base);
    fn_flush(b, &cf);
  } else {
    for (uint32_t k = 0; k < nf; k++) bufp(b, "    local.get $f%u\n", k);
  }
  bufp(b, "  )\n");

  bufp(b, "  (func $post_%s%u", path, i);
  if (rspill) bufp(b, " (param $r i32)");
  else for (uint32_t k = 0; k < nf; k++)
    bufp(b, " (param $f%u %s)", k, canon_flat_name(fl[k]));
  bufp(b, "\n    global.get $posts\n    i32.const 1\n    i32.add\n"
          "    global.set $posts\n  )\n");

  Fn f = { 0 };
  char cname[80], pname[80];
  snprintf(cname, sizeof cname, "$callee_%s%u", path, i);
  snprintf(pname, sizeof pname, "$post_%s%u", path, i);
  adapter_body(&f, dn, up, ft, pspill, rspill, cname, pname);

  bufp(b, "  (func (export \"adapt_%s%u\")", path, i);
  if (pspill) bufp(b, " (param $p i32)");
  else for (uint32_t k = 0; k < nf; k++)
    bufp(b, " (param $f%u %s)", k, canon_flat_name(fl[k]));
  if (rspill) {
    bufp(b, " (param $ret i32)\n");
  } else {
    bufp(b, " (result");
    for (uint32_t k = 0; k < nf; k++) bufp(b, " %s", canon_flat_name(fl[k]));
    bufp(b, ")\n");
  }
  fn_flush(b, &f);
  bufp(b, "  )\n");
}

static void adapters_in_scope(Wat *bp, const CanonScope *sc, bool ptr64,
                              const char *path) {
  CanonCtx cx = { sc->c, sc, ptr64, CSE_UTF8, CSE_UTF8, 0, 0 };
  Wat b = *bp;
  uint32_t n = sc->dl ? sc->dl->ntype_space : sc->c->ntype_space;
  for (uint32_t i = 0; i < n; i++) {
    const CmType *ct = canon_type_in(sc, i);
    if (!ct || ct->kind != CMT_DEFVAL) continue;
    CmValType t = { true, i, 0 };
    const char *gap = adapter_gap(&cx, t);
    if (gap) { bufp(&b, "  ;; type %s%u: not yet -- %s\n", path, i, gap); continue; }
    uint32_t nf = canon_flatten(&cx, t, NULL, 0);

    if (nf > 128) {
      bufp(&b, "  ;; type %s%u: not emitted -- %u flat values\n", path, i, nf);
      continue;
    }
    CanonFlat *fl = xmalloc(nf * sizeof *fl);
    canon_flatten(&cx, t, fl, nf);

    Fn f = { 0 };
    uint32_t base = fn_local(&f, "i32");
    bufp(&f.body, "    local.get $p\n    local.set $l%u\n", base);
    uint32_t slot = 0;
    emit_load(&f, &cx, t, base, 0, fl, &slot);
    bufp(&b, "  (func (export \"load_%s%u\") (param $p i32) (result", path, i);
    for (uint32_t k = 0; k < nf; k++) bufp(&b, " %s", canon_flat_name(fl[k]));
    bufp(&b, ")\n");
    fn_flush(&b, &f);
    bufp(&b, "  )\n");

    Fn g = { 0 };
    base = fn_local(&g, "i32");
    bufp(&g.body, "    local.get $p\n    local.set $l%u\n", base);
    slot = 0;
    emit_store(&g, &cx, t, base, 0, fl, NULL, &slot);
    bufp(&b, "  (func (export \"store_%s%u\") (param $p i32)", path, i);
    for (uint32_t k = 0; k < nf; k++)
      bufp(&b, " (param $f%u %s)", k, canon_flat_name(fl[k]));
    bufp(&b, "\n");
    fn_flush(&b, &g);
    bufp(&b, "  )\n");

    Fn h = { 0 };
    slot = 0;
    emit_move(&h, &cx, t, fl, NULL, &slot);
    bufp(&b, "  (func (export \"move_%s%u\")", path, i);
    for (uint32_t k = 0; k < nf; k++)
      bufp(&b, " (param $f%u %s)", k, canon_flat_name(fl[k]));
    bufp(&b, " (result");
    for (uint32_t k = 0; k < nf; k++) bufp(&b, " %s", canon_flat_name(fl[k]));
    bufp(&b, ")\n");
    fn_flush(&b, &h);
    bufp(&b, "  )\n");

    CmField pf = { NULL, t };
    CmFuncType ft = { &pf, 1, true, t, false };
    CanonSig lo, li;
    CanonCtx dn = cx, up = cx, cal = cx;
    dn.mem_to = up.mem_from = cal.mem_from = cal.mem_to = ADAPTER_CALLEE_MEM;
    canon_flatten_functype(&cx, &ft, false, &lo);
    canon_flatten_functype(&cx, &ft, true, &li);

    bool rspill = li.nresults != nf;
    bool pspill = lo.nparams != nf + (rspill ? 1u : 0u);
    emit_adapter(&b, &dn, &up, &cal, &ft, fl, nf, pspill, rspill, path, i);
    xfree(fl);
  }

  for (uint32_t i = 0; i < n; i++) {
    const CmType *ct = canon_type_in(sc, i);
    if (!ct || (ct->kind != CMT_COMPONENT && ct->kind != CMT_INSTANCE)) continue;
    CanonScope inner = { sc, sc->c, &ct->decls };
    char sub[64];
    snprintf(sub, sizeof sub, "%s%u_", path, i);
    *bp = b;
    adapters_in_scope(bp, &inner, ptr64, sub);
    b = *bp;
  }
  *bp = b;
}

static const char ADAPTER_UTF8_VALID_DOC[] =
    "  ;; Whether `n` bytes at `p` in memory `M` are UTF-8, as `load_string`\n"
    "  ;; decides it -- one `$utf8_valid_M` per memory, because the memory an\n"
    "  ;; access names is an immediate and not an argument.  A lift decodes what\n"
    "  ;; it is handed, and a decoder that fails is a trap, so a string cannot be\n"
    "  ;; copied without being checked however alike the two encodings are.\n"
    "  ;;\n"
    "  ;; It answers rather than traps so it can be tested in bulk: one call per\n"
    "  ;; input against what Python's decoder says, hundreds of inputs in a single\n"
    "  ;; run.  `$string_copy_F_T` is what turns a no into the trap.\n"
    "  ;;\n"
    "  ;; The ranges are the ones that make the encoding injective.  0xC0 and 0xC1\n"
    "  ;; would be an overlong two-byte form; a lead of 0xE0 needs its continuation\n"
    "  ;; at 0xA0 or above for the same reason; 0xED must stay below 0xA0 because\n"
    "  ;; above it is a surrogate, which is not a scalar value; and 0xF4 stops at\n"
    "  ;; 0x8F because that is where U+10FFFF ends.\n";

static const char ADAPTER_UTF8_VALID[] =
    "  (func $utf8_valid_%u (param $p i32) (param $n i32) (result i32)\n"
    "    (local $i i32) (local $b i32) (local $c i32) (local $lo i32) (local $hi i32)\n"
    "    block $done\n"
    "    loop $lp\n"
    "      local.get $i\n"
    "      local.get $n\n"
    "      i32.ge_u\n"
    "      br_if $done\n"
    "      local.get $p\n"
    "      local.get $i\n"
    "      i32.add\n"
    "      i32.load8_u%s\n"
    "      local.set $b\n"
    "      local.get $b\n"
    "      i32.const 0x80\n"
    "      i32.lt_u\n"
    "      if\n"
    "        local.get $i\n"
    "        i32.const 1\n"
    "        i32.add\n"
    "        local.set $i\n"
    "        br $lp\n"
    "      end\n"
    "      i32.const 0x80\n"
    "      local.set $lo\n"
    "      i32.const 0xBF\n"
    "      local.set $hi\n"
    "      local.get $b\n"
    "      i32.const 0xC2\n"
    "      i32.lt_u\n"
    "      if\n"
    "        i32.const 0\n"
    "        return\n"
    "      end\n"
    "      local.get $b\n"
    "      i32.const 0xE0\n"
    "      i32.lt_u\n"
    "      if\n"
    "        i32.const 2\n"
    "        local.set $c\n"
    "      else\n"
    "        local.get $b\n"
    "        i32.const 0xF0\n"
    "        i32.lt_u\n"
    "        if\n"
    "          i32.const 3\n"
    "          local.set $c\n"
    "          local.get $b\n"
    "          i32.const 0xE0\n"
    "          i32.eq\n"
    "          if\n"
    "            i32.const 0xA0\n"
    "            local.set $lo\n"
    "          end\n"
    "          local.get $b\n"
    "          i32.const 0xED\n"
    "          i32.eq\n"
    "          if\n"
    "            i32.const 0x9F\n"
    "            local.set $hi\n"
    "          end\n"
    "        else\n"
    "          local.get $b\n"
    "          i32.const 0xF5\n"
    "          i32.ge_u\n"
    "          if\n"
    "            i32.const 0\n"
    "            return\n"
    "          end\n"
    "          i32.const 4\n"
    "          local.set $c\n"
    "          local.get $b\n"
    "          i32.const 0xF0\n"
    "          i32.eq\n"
    "          if\n"
    "            i32.const 0x90\n"
    "            local.set $lo\n"
    "          end\n"
    "          local.get $b\n"
    "          i32.const 0xF4\n"
    "          i32.eq\n"
    "          if\n"
    "            i32.const 0x8F\n"
    "            local.set $hi\n"
    "          end\n"
    "        end\n"
    "      end\n"
    "      local.get $i\n"
    "      local.get $c\n"
    "      i32.add\n"
    "      local.get $n\n"
    "      i32.gt_u\n"
    "      if\n"
    "        i32.const 0\n"
    "        return\n"
    "      end\n"
    "      local.get $p\n"
    "      local.get $i\n"
    "      i32.add\n"
    "      i32.load8_u%s offset=1\n"
    "      local.set $b\n"
    "      local.get $b\n"
    "      local.get $lo\n"
    "      i32.lt_u\n"
    "      if\n"
    "        i32.const 0\n"
    "        return\n"
    "      end\n"
    "      local.get $b\n"
    "      local.get $hi\n"
    "      i32.gt_u\n"
    "      if\n"
    "        i32.const 0\n"
    "        return\n"
    "      end\n"
    "      local.get $c\n"
    "      i32.const 2\n"
    "      i32.gt_u\n"
    "      if\n"
    "        local.get $p\n"
    "        local.get $i\n"
    "        i32.add\n"
    "        i32.load8_u%s offset=2\n"
    "        i32.const 0xC0\n"
    "        i32.and\n"
    "        i32.const 0x80\n"
    "        i32.ne\n"
    "        if\n"
    "          i32.const 0\n"
    "          return\n"
    "        end\n"
    "      end\n"
    "      local.get $c\n"
    "      i32.const 3\n"
    "      i32.gt_u\n"
    "      if\n"
    "        local.get $p\n"
    "        local.get $i\n"
    "        i32.add\n"
    "        i32.load8_u%s offset=3\n"
    "        i32.const 0xC0\n"
    "        i32.and\n"
    "        i32.const 0x80\n"
    "        i32.ne\n"
    "        if\n"
    "          i32.const 0\n"
    "          return\n"
    "        end\n"
    "      end\n"
    "      local.get $i\n"
    "      local.get $c\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp\n"
    "    end\n"
    "    end\n"
    "    i32.const 1\n"
    "  )\n";

static const char ADAPTER_STRING_COPY[] =
    "  ;; Validate, allocate, copy -- and leave the new pointer.  The length is a\n"
    "  ;; byte count and a code-unit count at once, which is only true of utf8.\n"
    "  (func $string_copy_%u_%u (param $p i32) (param $n i32) (result i32)\n"
    "    (local $d i32)\n"
    "    local.get $n\n"
    "    i32.const 268435455\n"
    "    i32.gt_u\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    local.get $p\n"
    "    local.get $n\n"
    "    call $utf8_valid_%u\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 1\n"
    "    local.get $n\n"
    "    call $realloc_%u\n"
    "    local.set $d\n"
    "    local.get $d\n"
    "    local.get $p\n"
    "    local.get $n\n"
    "    memory.copy %u %u\n"
    "    local.get $d\n"
    "  )\n";

static const char ADAPTER_HELPERS[] =
    "  ;; Whether `n` code units at `p` are UTF-16, which is entirely about\n"
    "  ;; surrogates: a high one must be followed by a low one, and a low one may\n"
    "  ;; not stand alone.  Everything else is a scalar value already.\n"
    "  (func $utf16_valid (param $p i32) (param $n i32) (result i32)\n"
    "    (local $i i32) (local $u i32)\n"
    "    block $done\n"
    "    loop $lp\n"
    "      local.get $i\n"
    "      local.get $n\n"
    "      i32.ge_u\n"
    "      br_if $done\n"
    "      local.get $p\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load16_u\n"
    "      local.set $u\n"
    "      local.get $u\n"
    "      i32.const 0xD800\n"
    "      i32.lt_u\n"
    "      if\n"
    "        local.get $i\n"
    "        i32.const 1\n"
    "        i32.add\n"
    "        local.set $i\n"
    "        br $lp\n"
    "      end\n"
    "      local.get $u\n"
    "      i32.const 0xDFFF\n"
    "      i32.gt_u\n"
    "      if\n"
    "        local.get $i\n"
    "        i32.const 1\n"
    "        i32.add\n"
    "        local.set $i\n"
    "        br $lp\n"
    "      end\n"
    "      local.get $u\n"
    "      i32.const 0xDC00\n"
    "      i32.ge_u\n"
    "      if\n"
    "        i32.const 0\n"
    "        return\n"
    "      end\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.get $n\n"
    "      i32.ge_u\n"
    "      if\n"
    "        i32.const 0\n"
    "        return\n"
    "      end\n"
    "      local.get $p\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load16_u offset=2\n"
    "      local.set $u\n"
    "      local.get $u\n"
    "      i32.const 0xDC00\n"
    "      i32.lt_u\n"
    "      if\n"
    "        i32.const 0\n"
    "        return\n"
    "      end\n"
    "      local.get $u\n"
    "      i32.const 0xDFFF\n"
    "      i32.gt_u\n"
    "      if\n"
    "        i32.const 0\n"
    "        return\n"
    "      end\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp\n"
    "    end\n"
    "    end\n"
    "    i32.const 1\n"
    "  )\n"
    "\n"
    "  ;; The three encodings, decoded into an array of scalar values.\n"
    "  ;;\n"
    "  ;; Nine transcodings would be nine functions written nine times; through\n"
    "  ;; scalars they are three decoders and three encoders, and each half can be\n"
    "  ;; wrong on its own and be caught on its own.  The array is scratch and its\n"
    "  ;; address is the caller's problem.\n"
    "  ;;\n"
    "  ;; Validation happens before any of this, not during: the specification\n"
    "  ;; decodes the whole string and only then stores, so a string that is\n"
    "  ;; invalid at its end must not have had its beginning written.\n"
    "  (func $dec_utf8 (param $p i32) (param $n i32) (param $out i32) (result i32)\n"
    "    (local $i i32) (local $k i32) (local $b i32) (local $c i32)\n"
    "    block $done\n"
    "    loop $lp\n"
    "      local.get $i\n"
    "      local.get $n\n"
    "      i32.ge_u\n"
    "      br_if $done\n"
    "      local.get $p\n"
    "      local.get $i\n"
    "      i32.add\n"
    "      i32.load8_u\n"
    "      local.set $b\n"
    "      local.get $b\n"
    "      i32.const 0x80\n"
    "      i32.lt_u\n"
    "      if\n"
    "        local.get $b\n"
    "        local.set $c\n"
    "        local.get $i\n"
    "        i32.const 1\n"
    "        i32.add\n"
    "        local.set $i\n"
    "      else\n"
    "        local.get $b\n"
    "        i32.const 0xE0\n"
    "        i32.lt_u\n"
    "        if\n"
    "          local.get $b\n"
    "          i32.const 0x1F\n"
    "          i32.and\n"
    "          i32.const 6\n"
    "          i32.shl\n"
    "          local.get $p\n"
    "          local.get $i\n"
    "          i32.add\n"
    "          i32.load8_u offset=1\n"
    "          i32.const 0x3F\n"
    "          i32.and\n"
    "          i32.or\n"
    "          local.set $c\n"
    "          local.get $i\n"
    "          i32.const 2\n"
    "          i32.add\n"
    "          local.set $i\n"
    "        else\n"
    "          local.get $b\n"
    "          i32.const 0xF0\n"
    "          i32.lt_u\n"
    "          if\n"
    "            local.get $b\n"
    "            i32.const 0x0F\n"
    "            i32.and\n"
    "            i32.const 12\n"
    "            i32.shl\n"
    "            local.get $p\n"
    "            local.get $i\n"
    "            i32.add\n"
    "            i32.load8_u offset=1\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.const 6\n"
    "            i32.shl\n"
    "            i32.or\n"
    "            local.get $p\n"
    "            local.get $i\n"
    "            i32.add\n"
    "            i32.load8_u offset=2\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.or\n"
    "            local.set $c\n"
    "            local.get $i\n"
    "            i32.const 3\n"
    "            i32.add\n"
    "            local.set $i\n"
    "          else\n"
    "            local.get $b\n"
    "            i32.const 0x07\n"
    "            i32.and\n"
    "            i32.const 18\n"
    "            i32.shl\n"
    "            local.get $p\n"
    "            local.get $i\n"
    "            i32.add\n"
    "            i32.load8_u offset=1\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.const 12\n"
    "            i32.shl\n"
    "            i32.or\n"
    "            local.get $p\n"
    "            local.get $i\n"
    "            i32.add\n"
    "            i32.load8_u offset=2\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.const 6\n"
    "            i32.shl\n"
    "            i32.or\n"
    "            local.get $p\n"
    "            local.get $i\n"
    "            i32.add\n"
    "            i32.load8_u offset=3\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.or\n"
    "            local.set $c\n"
    "            local.get $i\n"
    "            i32.const 4\n"
    "            i32.add\n"
    "            local.set $i\n"
    "          end\n"
    "        end\n"
    "      end\n"
    "      local.get $out\n"
    "      local.get $k\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      local.get $c\n"
    "      i32.store\n"
    "      local.get $k\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $k\n"
    "      br $lp\n"
    "    end\n"
    "    end\n"
    "    local.get $k\n"
    "  )\n"
    "\n"
    "  (func $dec_utf16 (param $p i32) (param $n i32) (param $out i32) (result i32)\n"
    "    (local $i i32) (local $k i32) (local $u i32) (local $c i32)\n"
    "    block $done\n"
    "    loop $lp\n"
    "      local.get $i\n"
    "      local.get $n\n"
    "      i32.ge_u\n"
    "      br_if $done\n"
    "      local.get $p\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load16_u\n"
    "      local.set $u\n"
    "      local.get $u\n"
    "      i32.const 0xD800\n"
    "      i32.ge_u\n"
    "      local.get $u\n"
    "      i32.const 0xDC00\n"
    "      i32.lt_u\n"
    "      i32.and\n"
    "      if\n"
    "        i32.const 0x10000\n"
    "        local.get $u\n"
    "        i32.const 0xD800\n"
    "        i32.sub\n"
    "        i32.const 10\n"
    "        i32.shl\n"
    "        i32.add\n"
    "        local.get $p\n"
    "        local.get $i\n"
    "        i32.const 1\n"
    "        i32.shl\n"
    "        i32.add\n"
    "        i32.load16_u offset=2\n"
    "        i32.const 0xDC00\n"
    "        i32.sub\n"
    "        i32.add\n"
    "        local.set $c\n"
    "        local.get $i\n"
    "        i32.const 2\n"
    "        i32.add\n"
    "        local.set $i\n"
    "      else\n"
    "        local.get $u\n"
    "        local.set $c\n"
    "        local.get $i\n"
    "        i32.const 1\n"
    "        i32.add\n"
    "        local.set $i\n"
    "      end\n"
    "      local.get $out\n"
    "      local.get $k\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      local.get $c\n"
    "      i32.store\n"
    "      local.get $k\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $k\n"
    "      br $lp\n"
    "    end\n"
    "    end\n"
    "    local.get $k\n"
    "  )\n"
    "\n"
    "  (func $dec_latin1 (param $p i32) (param $n i32) (param $out i32) (result i32)\n"
    "    (local $i i32)\n"
    "    block $done\n"
    "    loop $lp\n"
    "      local.get $i\n"
    "      local.get $n\n"
    "      i32.ge_u\n"
    "      br_if $done\n"
    "      local.get $out\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      local.get $p\n"
    "      local.get $i\n"
    "      i32.add\n"
    "      i32.load8_u\n"
    "      i32.store\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp\n"
    "    end\n"
    "    end\n"
    "    local.get $n\n"
    "  )\n"
    "\n"
    "  ;; latin1+utf16 is two encodings behind one length: the top bit of the\n"
    "  ;; tagged count says which, and the count itself is code units in one case\n"
    "  ;; and bytes in the other.\n"
    "  (func $dec_l1u16 (param $p i32) (param $tagged i32) (param $out i32) (result i32)\n"
    "    local.get $tagged\n"
    "    i32.const 0x80000000\n"
    "    i32.and\n"
    "    if (result i32)\n"
    "      local.get $p\n"
    "      local.get $tagged\n"
    "      i32.const 0x7FFFFFFF\n"
    "      i32.and\n"
    "      local.get $out\n"
    "      call $dec_utf16\n"
    "    else\n"
    "      local.get $p\n"
    "      local.get $tagged\n"
    "      local.get $out\n"
    "      call $dec_latin1\n"
    "    end\n"
    "  )\n"
    "\n"
    "  ;; How many bytes one scalar takes in each encoding.\n"
    "  (func $len_utf8 (param $c i32) (result i32)\n"
    "    local.get $c\n"
    "    i32.const 0x80\n"
    "    i32.lt_u\n"
    "    if (result i32)\n"
    "      i32.const 1\n"
    "    else\n"
    "      local.get $c\n"
    "      i32.const 0x800\n"
    "      i32.lt_u\n"
    "      if (result i32)\n"
    "        i32.const 2\n"
    "      else\n"
    "        local.get $c\n"
    "        i32.const 0x10000\n"
    "        i32.lt_u\n"
    "        if (result i32)\n"
    "          i32.const 3\n"
    "        else\n"
    "          i32.const 4\n"
    "        end\n"
    "      end\n"
    "    end\n"
    "  )\n"
    "\n"
    "  ;; Encode scalars, allocating exactly what they need.\n"
    "  ;;\n"
    "  ;; Two passes rather than the specification's allocate-then-grow-then-shrink:\n"
    "  ;; the sequence of reallocations is not part of the answer, only the bytes\n"
    "  ;; and the count are, and a first pass can say the size outright.\n"
    "  (func $enc_utf8 (param $sc i32) (param $k i32) (result i32 i32)\n"
    "    (local $i i32) (local $n i32) (local $d i32) (local $o i32) (local $c i32)\n"
    "    block $done1\n"
    "    loop $lp1\n"
    "      local.get $i\n"
    "      local.get $k\n"
    "      i32.ge_u\n"
    "      br_if $done1\n"
    "      local.get $n\n"
    "      local.get $sc\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load\n"
    "      call $len_utf8\n"
    "      i32.add\n"
    "      local.set $n\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp1\n"
    "    end\n"
    "    end\n"
    "    local.get $n\n"
    "    i32.const 268435455\n"
    "    i32.gt_u\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 1\n"
    "    local.get $n\n"
    "    call $realloc_0\n"
    "    local.set $d\n"
    "    i32.const 0\n"
    "    local.set $i\n"
    "    block $done2\n"
    "    loop $lp2\n"
    "      local.get $i\n"
    "      local.get $k\n"
    "      i32.ge_u\n"
    "      br_if $done2\n"
    "      local.get $sc\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load\n"
    "      local.set $c\n"
    "      local.get $c\n"
    "      i32.const 0x80\n"
    "      i32.lt_u\n"
    "      if\n"
    "        local.get $d\n"
    "        local.get $o\n"
    "        i32.add\n"
    "        local.get $c\n"
    "        i32.store8\n"
    "        local.get $o\n"
    "        i32.const 1\n"
    "        i32.add\n"
    "        local.set $o\n"
    "      else\n"
    "        local.get $c\n"
    "        i32.const 0x800\n"
    "        i32.lt_u\n"
    "        if\n"
    "          local.get $d\n"
    "          local.get $o\n"
    "          i32.add\n"
    "          local.get $c\n"
    "          i32.const 6\n"
    "          i32.shr_u\n"
    "          i32.const 0xC0\n"
    "          i32.or\n"
    "          i32.store8\n"
    "          local.get $d\n"
    "          local.get $o\n"
    "          i32.add\n"
    "          local.get $c\n"
    "          i32.const 0x3F\n"
    "          i32.and\n"
    "          i32.const 0x80\n"
    "          i32.or\n"
    "          i32.store8 offset=1\n"
    "          local.get $o\n"
    "          i32.const 2\n"
    "          i32.add\n"
    "          local.set $o\n"
    "        else\n"
    "          local.get $c\n"
    "          i32.const 0x10000\n"
    "          i32.lt_u\n"
    "          if\n"
    "            local.get $d\n"
    "            local.get $o\n"
    "            i32.add\n"
    "            local.get $c\n"
    "            i32.const 12\n"
    "            i32.shr_u\n"
    "            i32.const 0xE0\n"
    "            i32.or\n"
    "            i32.store8\n"
    "            local.get $d\n"
    "            local.get $o\n"
    "            i32.add\n"
    "            local.get $c\n"
    "            i32.const 6\n"
    "            i32.shr_u\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.const 0x80\n"
    "            i32.or\n"
    "            i32.store8 offset=1\n"
    "            local.get $d\n"
    "            local.get $o\n"
    "            i32.add\n"
    "            local.get $c\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.const 0x80\n"
    "            i32.or\n"
    "            i32.store8 offset=2\n"
    "            local.get $o\n"
    "            i32.const 3\n"
    "            i32.add\n"
    "            local.set $o\n"
    "          else\n"
    "            local.get $d\n"
    "            local.get $o\n"
    "            i32.add\n"
    "            local.get $c\n"
    "            i32.const 18\n"
    "            i32.shr_u\n"
    "            i32.const 0xF0\n"
    "            i32.or\n"
    "            i32.store8\n"
    "            local.get $d\n"
    "            local.get $o\n"
    "            i32.add\n"
    "            local.get $c\n"
    "            i32.const 12\n"
    "            i32.shr_u\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.const 0x80\n"
    "            i32.or\n"
    "            i32.store8 offset=1\n"
    "            local.get $d\n"
    "            local.get $o\n"
    "            i32.add\n"
    "            local.get $c\n"
    "            i32.const 6\n"
    "            i32.shr_u\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.const 0x80\n"
    "            i32.or\n"
    "            i32.store8 offset=2\n"
    "            local.get $d\n"
    "            local.get $o\n"
    "            i32.add\n"
    "            local.get $c\n"
    "            i32.const 0x3F\n"
    "            i32.and\n"
    "            i32.const 0x80\n"
    "            i32.or\n"
    "            i32.store8 offset=3\n"
    "            local.get $o\n"
    "            i32.const 4\n"
    "            i32.add\n"
    "            local.set $o\n"
    "          end\n"
    "        end\n"
    "      end\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp2\n"
    "    end\n"
    "    end\n"
    "    local.get $d\n"
    "    local.get $n\n"
    "  )\n"
    "\n"
    "  (func $enc_utf16 (param $sc i32) (param $k i32) (result i32 i32)\n"
    "    (local $i i32) (local $n i32) (local $d i32) (local $o i32) (local $c i32)\n"
    "    block $done1\n"
    "    loop $lp1\n"
    "      local.get $i\n"
    "      local.get $k\n"
    "      i32.ge_u\n"
    "      br_if $done1\n"
    "      local.get $n\n"
    "      local.get $sc\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load\n"
    "      i32.const 0x10000\n"
    "      i32.lt_u\n"
    "      if (result i32)\n"
    "        i32.const 1\n"
    "      else\n"
    "        i32.const 2\n"
    "      end\n"
    "      i32.add\n"
    "      local.set $n\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp1\n"
    "    end\n"
    "    end\n"
    "    local.get $n\n"
    "    i32.const 1\n"
    "    i32.shl\n"
    "    i32.const 268435455\n"
    "    i32.gt_u\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 2\n"
    "    local.get $n\n"
    "    i32.const 1\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $d\n"
    "    i32.const 0\n"
    "    local.set $i\n"
    "    block $done2\n"
    "    loop $lp2\n"
    "      local.get $i\n"
    "      local.get $k\n"
    "      i32.ge_u\n"
    "      br_if $done2\n"
    "      local.get $sc\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load\n"
    "      local.set $c\n"
    "      local.get $c\n"
    "      i32.const 0x10000\n"
    "      i32.lt_u\n"
    "      if\n"
    "        local.get $d\n"
    "        local.get $o\n"
    "        i32.const 1\n"
    "        i32.shl\n"
    "        i32.add\n"
    "        local.get $c\n"
    "        i32.store16\n"
    "        local.get $o\n"
    "        i32.const 1\n"
    "        i32.add\n"
    "        local.set $o\n"
    "      else\n"
    "        local.get $d\n"
    "        local.get $o\n"
    "        i32.const 1\n"
    "        i32.shl\n"
    "        i32.add\n"
    "        local.get $c\n"
    "        i32.const 0x10000\n"
    "        i32.sub\n"
    "        i32.const 10\n"
    "        i32.shr_u\n"
    "        i32.const 0xD800\n"
    "        i32.add\n"
    "        i32.store16\n"
    "        local.get $d\n"
    "        local.get $o\n"
    "        i32.const 1\n"
    "        i32.shl\n"
    "        i32.add\n"
    "        local.get $c\n"
    "        i32.const 0x10000\n"
    "        i32.sub\n"
    "        i32.const 0x3FF\n"
    "        i32.and\n"
    "        i32.const 0xDC00\n"
    "        i32.add\n"
    "        i32.store16 offset=2\n"
    "        local.get $o\n"
    "        i32.const 2\n"
    "        i32.add\n"
    "        local.set $o\n"
    "      end\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp2\n"
    "    end\n"
    "    end\n"
    "    local.get $d\n"
    "    local.get $n\n"
    "  )\n"
    "\n"
    "  ;; latin1 when every scalar fits in a byte, utf16 otherwise -- and the tag\n"
    "  ;; in the returned count is what says which.  The alignment is 2 either way,\n"
    "  ;; because the latin1 case may still have to become utf16.\n"
    "  (func $enc_l1u16 (param $sc i32) (param $k i32) (result i32 i32)\n"
    "    (local $i i32) (local $d i32) (local $u i32) (local $units i32)\n"
    "    block $done\n"
    "    loop $lp\n"
    "      local.get $i\n"
    "      local.get $k\n"
    "      i32.ge_u\n"
    "      br_if $done\n"
    "      local.get $sc\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load\n"
    "      i32.const 0x100\n"
    "      i32.ge_u\n"
    "      if\n"
    "        i32.const 1\n"
    "        local.set $u\n"
    "        br $done\n"
    "      end\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp\n"
    "    end\n"
    "    end\n"
    "    local.get $u\n"
    "    if\n"
    "      local.get $sc\n"
    "      local.get $k\n"
    "      call $enc_utf16\n"
    "      local.set $units\n"
    "      local.set $d\n"
    "      local.get $d\n"
    "      local.get $units\n"
    "      i32.const 0x80000000\n"
    "      i32.or\n"
    "      return\n"
    "    end\n"
    "    local.get $k\n"
    "    i32.const 268435455\n"
    "    i32.gt_u\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 2\n"
    "    local.get $k\n"
    "    call $realloc_0\n"
    "    local.set $d\n"
    "    i32.const 0\n"
    "    local.set $i\n"
    "    block $done2\n"
    "    loop $lp2\n"
    "      local.get $i\n"
    "      local.get $k\n"
    "      i32.ge_u\n"
    "      br_if $done2\n"
    "      local.get $d\n"
    "      local.get $i\n"
    "      i32.add\n"
    "      local.get $sc\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load\n"
    "      i32.store8\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      local.set $i\n"
    "      br $lp2\n"
    "    end\n"
    "    end\n"
    "    local.get $d\n"
    "    local.get $k\n"
    "  )\n"
    "\n"
    "  ;; The nine transcodings.  Each is a decode into scalars and an encode out\n"
    "  ;; of them, which is why there are three of each and not nine of either.\n"
    "  ;;\n"
    "  ;; They take and return what the canonical ABI puts in a string's two words:\n"
    "  ;; a pointer and a tagged code-unit count, whose meaning depends on the\n"
    "  ;; encoding -- bytes for utf8 and latin1, code units for utf16, and for\n"
    "  ;; latin1+utf16 whichever the top bit says.\n"
    "  (func $xcode_utf8_utf8 (export \"xcode_utf8_utf8\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    call $utf8_valid_0\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_utf8\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_utf8\n"
    "  )\n"
    "  (func $xcode_utf8_utf16 (export \"xcode_utf8_utf16\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    call $utf8_valid_0\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_utf8\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_utf16\n"
    "  )\n"
    "  (func $xcode_utf8_l1u16 (export \"xcode_utf8_l1u16\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    call $utf8_valid_0\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_utf8\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_l1u16\n"
    "  )\n"
    "  (func $xcode_utf16_utf8 (export \"xcode_utf16_utf8\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    call $utf16_valid\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_utf16\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_utf8\n"
    "  )\n"
    "  (func $xcode_utf16_utf16 (export \"xcode_utf16_utf16\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    call $utf16_valid\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_utf16\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_utf16\n"
    "  )\n"
    "  (func $xcode_utf16_l1u16 (export \"xcode_utf16_l1u16\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    call $utf16_valid\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_utf16\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_l1u16\n"
    "  )\n"
    "  (func $xcode_l1u16_utf8 (export \"xcode_l1u16_utf8\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $units\n"
    "    i32.const 0x80000000\n"
    "    i32.and\n"
    "    if\n"
    "      local.get $p\n"
    "      local.get $units\n"
    "      i32.const 0x7FFFFFFF\n"
    "      i32.and\n"
    "      call $utf16_valid\n"
    "      i32.eqz\n"
    "      if\n"
    "        unreachable\n"
    "      end\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 0x7FFFFFFF\n"
    "    i32.and\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_l1u16\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_utf8\n"
    "  )\n"
    "  (func $xcode_l1u16_utf16 (export \"xcode_l1u16_utf16\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $units\n"
    "    i32.const 0x80000000\n"
    "    i32.and\n"
    "    if\n"
    "      local.get $p\n"
    "      local.get $units\n"
    "      i32.const 0x7FFFFFFF\n"
    "      i32.and\n"
    "      call $utf16_valid\n"
    "      i32.eqz\n"
    "      if\n"
    "        unreachable\n"
    "      end\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 0x7FFFFFFF\n"
    "    i32.and\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_l1u16\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_utf16\n"
    "  )\n"
    "  (func $xcode_l1u16_l1u16 (export \"xcode_l1u16_l1u16\")\n"
    "        (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $sc i32) (local $k i32)\n"
    "    local.get $units\n"
    "    i32.const 0x80000000\n"
    "    i32.and\n"
    "    if\n"
    "      local.get $p\n"
    "      local.get $units\n"
    "      i32.const 0x7FFFFFFF\n"
    "      i32.and\n"
    "      call $utf16_valid\n"
    "      i32.eqz\n"
    "      if\n"
    "        unreachable\n"
    "      end\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 4\n"
    "    local.get $units\n"
    "    i32.const 0x7FFFFFFF\n"
    "    i32.and\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $sc\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    local.get $sc\n"
    "    call $dec_l1u16\n"
    "    local.set $k\n"
    "    local.get $sc\n"
    "    local.get $k\n"
    "    call $enc_l1u16\n"
    "  )\n"
    "\n"
    "  ;; Moving a string, which is not one operation but two.\n"
    "  ;;\n"
    "  ;; Where the two sides agree on the encoding the specification's own path is\n"
    "  ;; a copy: one allocation of exactly the right size, and the count comes out\n"
    "  ;; unchanged.  Matching that is what keeps the destination pointer the same\n"
    "  ;; as the reference computes, which is what the per-type tests compare.\n"
    "  ;;\n"
    "  ;; Where they disagree it is a transcode, and then the destination's size is\n"
    "  ;; not the source's: the reference allocates a worst case and shrinks, this\n"
    "  ;; allocates exactly, and the pointer is not part of the answer.  Those are\n"
    "  ;; compared by content.\n"
    "  ;;\n"
    "  ;; latin1+utf16 to itself is both, and the tag says which.  A latin1 source\n"
    "  ;; copies; a utf16 one may still *demote* to latin1 -- the specification\n"
    "  ;; reaches for store_probably_utf16_to_latin1_or_utf16 there and not for a\n"
    "  ;; copy -- so it has to go through the transcoder to find out.\n"
    "  ;;\n"
    "  ;; All nine take and return a string's two words, so a caller moving one\n"
    "  ;; does not care which of the three shapes it got.\n"
    "  (func $smove_utf16_utf16 (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $d i32)\n"
    "    local.get $units\n"
    "    i32.const 1\n"
    "    i32.shl\n"
    "    i32.const 268435455\n"
    "    i32.gt_u\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    call $utf16_valid\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 2\n"
    "    local.get $units\n"
    "    i32.const 1\n"
    "    i32.shl\n"
    "    call $realloc_0\n"
    "    local.set $d\n"
    "    local.get $d\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    i32.const 1\n"
    "    i32.shl\n"
    "    memory.copy\n"
    "    local.get $d\n"
    "    local.get $units\n"
    "  )\n"
    "  (func $smove_l1u16_l1u16 (param $p i32) (param $units i32) (result i32 i32)\n"
    "    (local $d i32)\n"
    "    local.get $units\n"
    "    i32.const 0x80000000\n"
    "    i32.and\n"
    "    if\n"
    "      local.get $p\n"
    "      local.get $units\n"
    "      call $xcode_l1u16_l1u16\n"
    "      return\n"
    "    end\n"
    "    local.get $units\n"
    "    i32.const 268435455\n"
    "    i32.gt_u\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"

    "    i32.const 0\n"
    "    i32.const 0\n"
    "    i32.const 2\n"
    "    local.get $units\n"
    "    call $realloc_0\n"
    "    local.set $d\n"
    "    local.get $d\n"
    "    local.get $p\n"
    "    local.get $units\n"
    "    memory.copy\n"
    "    local.get $d\n"
    "    local.get $units\n"
    "  )\n"
    "  (func $smove_utf8_utf16 (param $p i32) (param $units i32) (result i32 i32)\n"
    "    local.get $p\n    local.get $units\n    call $xcode_utf8_utf16\n  )\n"
    "  (func $smove_utf8_l1u16 (param $p i32) (param $units i32) (result i32 i32)\n"
    "    local.get $p\n    local.get $units\n    call $xcode_utf8_l1u16\n  )\n"
    "  (func $smove_utf16_utf8 (param $p i32) (param $units i32) (result i32 i32)\n"
    "    local.get $p\n    local.get $units\n    call $xcode_utf16_utf8\n  )\n"
    "  (func $smove_utf16_l1u16 (param $p i32) (param $units i32) (result i32 i32)\n"
    "    local.get $p\n    local.get $units\n    call $xcode_utf16_l1u16\n  )\n"
    "  (func $smove_l1u16_utf8 (param $p i32) (param $units i32) (result i32 i32)\n"
    "    local.get $p\n    local.get $units\n    call $xcode_l1u16_utf8\n  )\n"
    "  (func $smove_l1u16_utf16 (param $p i32) (param $units i32) (result i32 i32)\n"
    "    local.get $p\n    local.get $units\n    call $xcode_l1u16_utf16\n  )\n"
    "\n"
    "  ;; Handle tables.\n"
    "  ;;\n"
    "  ;; A handle is an i32 wherever the ABI puts it, but the number means \"the\n"
    "  ;; nth entry of *this instance's* table\" -- so moving one across the\n"
    "  ;; boundary is not a copy, it is a removal from one table and an insertion\n"
    "  ;; into another, and the index changes.  Which index comes out is\n"
    "  ;; observable, so the free list is a stack and the first index handed out is\n"
    "  ;; 1: entry zero is reserved, exactly as the specification's table is\n"
    "  ;; constructed holding one empty slot.\n"
    "  ;;\n"
    "  ;; The two tables here are the two sides of a conversion: whatever is read\n"
    "  ;; comes out of $src_table and whatever is written goes into $dst_table.\n"
    "  ;; That is what a lift and a lower do between two instances, and with one\n"
    "  ;; table it would be an identity that tested nothing.\n"
    "  ;;\n"
    "  ;; In a compiled instance these belong beside the other per-instance state,\n"
    "  ;; not in the guest's linear memory; they are here so the emitted module is\n"
    "  ;; self-contained and can be run.  The capacity is fixed for the same\n"
    "  ;; reason -- growing it would allocate, and an allocation here would\n"
    "  ;; interleave with the ones the conversion itself makes.\n"
    "  ;;\n"
    "  ;;   +0  len       slots in use, counting the reserved slot 0\n"
    "  ;;   +4  cap\n"
    "  ;;   +8  freelen\n"
    "  ;;   +16 slots     cap entries of 16 bytes: rt, rep, own, num_lends\n"
    "  ;;                 rt is 0 for an empty slot, so it doubles as the tag\n"
    "  ;;   +16+cap*16    the free list, one i32 per entry\n"
    "  (global $src_table (export \"src_table\") (mut i32) (i32.const 0))\n"
    "  (global $dst_table (export \"dst_table\") (mut i32) (i32.const 0))\n"
    "\n"
    "  (func $h_slot (param $t i32) (param $i i32) (result i32)\n"
    "    local.get $t\n"
    "    i32.const 16\n"
    "    i32.add\n"
    "    local.get $i\n"
    "    i32.const 4\n"
    "    i32.shl\n"
    "    i32.add\n"
    "  )\n"
    "\n"
    "  (func $h_free (param $t i32) (result i32)\n"
    "    local.get $t\n"
    "    i32.const 16\n"
    "    i32.add\n"
    "    local.get $t\n"
    "    i32.load offset=4\n"
    "    i32.const 4\n"
    "    i32.shl\n"
    "    i32.add\n"
    "  )\n"
    "\n"
    "  ;; A slot that exists and is not empty, or a trap.\n"
    "  (func $h_get (param $t i32) (param $i i32) (param $rt i32) (result i32)\n"
    "    (local $s i32)\n"
    "    local.get $i\n"
    "    local.get $t\n"
    "    i32.load\n"
    "    i32.ge_u\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    call $h_slot\n"
    "    local.set $s\n"
    "    local.get $s\n"
    "    i32.load\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    ;; A handle of another resource is as wrong as no handle at all: the index\n"
    "    ;; spaces are shared, so without this a caller could name someone else's.\n"
    "    local.get $s\n"
    "    i32.load\n"
    "    local.get $rt\n"
    "    i32.ne\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    local.get $s\n"
    "  )\n"
    "\n"
    "  (func $h_add (param $t i32) (param $rt i32) (param $rep i32) (param $own i32)\n"
    "                (result i32)\n"
    "    (local $i i32) (local $s i32) (local $n i32)\n"
    "    local.get $t\n"
    "    i32.load offset=8\n"
    "    local.tee $n\n"
    "    if\n"
    "      local.get $n\n"
    "      i32.const 1\n"
    "      i32.sub\n"
    "      local.tee $n\n"
    "      local.set $i\n"
    "      local.get $t\n"
    "      local.get $n\n"
    "      i32.store offset=8\n"
    "      local.get $t\n"
    "      call $h_free\n"
    "      local.get $i\n"
    "      i32.const 2\n"
    "      i32.shl\n"
    "      i32.add\n"
    "      i32.load\n"
    "      local.set $i\n"
    "    else\n"
    "      local.get $t\n"
    "      i32.load\n"
    "      local.set $i\n"
    "      local.get $i\n"
    "      local.get $t\n"
    "      i32.load offset=4\n"
    "      i32.ge_u\n"
    "      if\n"
    "        unreachable\n"
    "      end\n"
    "      local.get $t\n"
    "      local.get $i\n"
    "      i32.const 1\n"
    "      i32.add\n"
    "      i32.store\n"
    "    end\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    call $h_slot\n"
    "    local.set $s\n"
    "    local.get $s\n"
    "    local.get $rt\n"
    "    i32.store\n"
    "    local.get $s\n"
    "    local.get $rep\n"
    "    i32.store offset=4\n"
    "    local.get $s\n"
    "    local.get $own\n"
    "    i32.store offset=8\n"
    "    local.get $s\n"
    "    i32.const 0\n"
    "    i32.store offset=12\n"
    "    local.get $i\n"
    "  )\n"
    "\n"
    "  (func $h_drop_slot (param $t i32) (param $i i32)\n"
    "    (local $n i32)\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    call $h_slot\n"
    "    i32.const 0\n"
    "    i32.store\n"
    "    local.get $t\n"
    "    i32.load offset=8\n"
    "    local.set $n\n"
    "    local.get $t\n"
    "    call $h_free\n"
    "    local.get $n\n"
    "    i32.const 2\n"
    "    i32.shl\n"
    "    i32.add\n"
    "    local.get $i\n"
    "    i32.store\n"
    "    local.get $t\n"
    "    local.get $n\n"
    "    i32.const 1\n"
    "    i32.add\n"
    "    i32.store offset=8\n"
    "  )\n"
    "\n"
    "  ;; Taking an owned handle out: it must be owned, and nothing may be\n"
    "  ;; borrowing it.  Both are checked before the slot is freed, because a trap\n"
    "  ;; does not undo the free.\n"
    "  (func $lift_own (export \"lift_own\") (param $t i32) (param $i i32) (param $rt i32)\n"
    "                  (result i32)\n"
    "    (local $s i32) (local $rep i32)\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    local.get $rt\n"
    "    call $h_get\n"
    "    local.set $s\n"
    "    local.get $s\n"
    "    i32.load offset=12\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    local.get $s\n"
    "    i32.load offset=8\n"
    "    i32.eqz\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    local.get $s\n"
    "    i32.load offset=4\n"
    "    local.set $rep\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    call $h_drop_slot\n"
    "    local.get $rep\n"
    "  )\n"
    "\n"
    "  (func $lower_own (export \"lower_own\") (param $t i32) (param $rep i32) (param $rt i32)\n"
    "                   (result i32)\n"
    "    local.get $t\n"
    "    local.get $rt\n"
    "    local.get $rep\n"
    "    i32.const 1\n"
    "    call $h_add\n"
    "  )\n"
    "\n"
    "  ;; A borrow stays where it is and is counted, so that whoever owns it cannot\n"
    "  ;; give it away while it is out on loan.\n"
    "  (func $lift_borrow (export \"lift_borrow\") (param $t i32) (param $i i32) (param $rt i32)\n"
    "                     (result i32)\n"
    "    (local $s i32)\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    local.get $rt\n"
    "    call $h_get\n"
    "    local.set $s\n"
    "    local.get $s\n"
    "    local.get $s\n"
    "    i32.load offset=12\n"
    "    i32.const 1\n"
    "    i32.add\n"
    "    i32.store offset=12\n"
    "    local.get $s\n"
    "    i32.load offset=4\n"
    "  )\n"
    "\n"
    "  ;; `same` is whether the receiving instance is the one that implements the\n"
    "  ;; resource.  It knows its own representations, so it is handed the\n"
    "  ;; representation itself and no handle is made.\n"
    "  (func $lower_borrow (export \"lower_borrow\") (param $t i32) (param $rep i32)\n"
    "                      (param $rt i32) (param $same i32) (result i32)\n"
    "    local.get $same\n"
    "    if\n"
    "      local.get $rep\n"
    "      return\n"
    "    end\n"
    "    local.get $t\n"
    "    local.get $rt\n"
    "    local.get $rep\n"
    "    i32.const 0\n"
    "    call $h_add\n"
    "  )\n"
    "\n"
    "  (func $release_lender (export \"release_lender\") (param $t i32) (param $i i32)\n"
    "    (local $s i32)\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    call $h_slot\n"
    "    local.set $s\n"
    "    local.get $s\n"
    "    local.get $s\n"
    "    i32.load offset=12\n"
    "    i32.const 1\n"
    "    i32.sub\n"
    "    i32.store offset=12\n"
    "  )\n"
    "\n"
    "  ;; The three canonical built-ins over a resource, in the instance that\n"
    "  ;; defines it.\n"
    "  (func $resource_new (export \"resource_new\") (param $t i32) (param $rep i32)\n"
    "                      (param $rt i32) (result i32)\n"
    "    local.get $t\n"
    "    local.get $rt\n"
    "    local.get $rep\n"
    "    i32.const 1\n"
    "    call $h_add\n"
    "  )\n"
    "\n"
    "  (func $resource_rep (export \"resource_rep\") (param $t i32) (param $i i32)\n"
    "                      (param $rt i32) (result i32)\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    local.get $rt\n"
    "    call $h_get\n"
    "    i32.load offset=4\n"
    "  )\n"
    "\n"
    "  ;; Returns whether it was owned, and its representation.  The destructor is\n"
    "  ;; the caller's to run: in a compiled component it is a direct call to a\n"
    "  ;; known function, not something this can reach.\n"
    "  (func $resource_drop (export \"resource_drop\") (param $t i32) (param $i i32)\n"
    "                       (param $rt i32) (result i32 i32)\n"
    "    (local $s i32) (local $own i32) (local $rep i32)\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    local.get $rt\n"
    "    call $h_get\n"
    "    local.set $s\n"
    "    local.get $s\n"
    "    i32.load offset=12\n"
    "    if\n"
    "      unreachable\n"
    "    end\n"
    "    local.get $s\n"
    "    i32.load offset=8\n"
    "    local.set $own\n"
    "    local.get $s\n"
    "    i32.load offset=4\n"
    "    local.set $rep\n"
    "    local.get $t\n"
    "    local.get $i\n"
    "    call $h_drop_slot\n"
    "    local.get $own\n"
    "    local.get $rep\n"
    "  )\n";

static void emit_utf8_valid(Wat *b, uint32_t m) {
  const char *mi = memi(m);
  bufp(b, ADAPTER_UTF8_VALID, m, mi, mi, mi, mi);
}

static void emit_mem_helpers(Wat *b, uint32_t m) {
  bufp(b, "  (global $bump_%u (export \"bump%u\") (mut i32) (i32.const 65536))\n"
          "  (func $realloc_%u (export \"realloc%u\")\n"
          "        (param $old i32) (param $oldlen i32) (param $align i32)\n"
          "        (param $n i32) (result i32)\n"
          "    (local $p i32)\n"
          "    global.get $bump_%u\n    local.get $align\n    i32.add\n"
          "    i32.const 1\n    i32.sub\n"
          "    local.get $align\n    i32.const 1\n    i32.sub\n"
          "    i32.const -1\n    i32.xor\n    i32.and\n"
          "    local.set $p\n"
          "    local.get $p\n    local.get $n\n    i32.add\n"
          "    global.set $bump_%u\n"

          "    local.get $old\n"
          "    if\n"
          "      local.get $p\n      local.get $old\n"
          "      local.get $oldlen\n      local.get $n\n"
          "      local.get $oldlen\n      local.get $n\n"
          "      i32.lt_u\n      select\n"
          "      memory.copy %u %u\n"
          "    end\n"
          "    local.get $p\n"
          "  )\n", m, m, m, m, m, m, m, m);
  emit_utf8_valid(b, m);
}

static void emit_pair_helpers(Wat *b, uint32_t from, uint32_t to) {
  bufp(b, ADAPTER_STRING_COPY, from, to, from, to, to, from);
  bufp(b, "  (func $smove_utf8_utf8_%u_%u"
          " (param $p i32) (param $units i32) (result i32 i32)\n"
          "    local.get $p\n"
          "    local.get $units\n"
          "    call $string_copy_%u_%u\n"
          "    local.get $units\n"
          "  )\n", from, to, from, to);
}

const char *canon_adapter_why_not(const CanonCtx *dn, const CanonCtx *up,
                                  const CmFuncType *ft) {
  if (ft->is_async) return "an async lift or lower";
  for (uint32_t i = 0; i <= ft->nparams; i++) {
    CmValType t;
    if (i < ft->nparams) t = ft->params[i].t;
    else if (ft->has_result) t = ft->result;
    else break;
    if (has_kind(dn, t, true))
      return "a resource handle, whose table is not something an adapter "
             "imports yet";
    const char *g = adapter_gap(dn, t);
    if (!g) g = adapter_gap(up, t);
    if (g) return g;
  }
  return NULL;
}

char *canon_adapter_text(const CanonCtx *dn, const CanonCtx *up,
                         const CmFuncType *ft, bool post,
                         CanonAdapterImports *imp) {
  CanonSig lo, li;
  canon_flatten_functype(dn, ft, false, &lo);
  canon_flatten_functype(up, ft, true, &li);

  CanonFlat pf[CANON_MAX_SIG * 4];
  uint32_t npf = flatten_params(dn, ft, pf, sizeof pf / sizeof pf[0]);
  uint32_t nrf = ft->has_result
                   ? canon_flatten(up, ft->result, NULL, 0) : 0;
  bool pspill = npf > CANON_MAX_FLAT_PARAMS;
  bool rspill = nrf > CANON_MAX_FLAT_RESULTS;
  bool mem = canon_needs_memory(dn, ft, false);

  bool str_dn = false;
  for (uint32_t i = 0; i < ft->nparams; i++)
    if (has_kind(dn, ft->params[i].t, false)) str_dn = true;
  bool str_up = ft->has_result && has_kind(up, ft->result, false);

  Wat b = { 0 };
  bufp(&b, "(module\n");
  imp->n = 0;
#define IMPORT(k) (imp->kind[imp->n++] = (uint8_t)(k))
  if (mem) {

    bufp(&b, "  (import \"caller\" \"mem\" (memory 0))\n");
    IMPORT(CAI_CALLER_MEM);
    bufp(&b, "  (import \"callee\" \"mem\" (memory 0))\n");
    IMPORT(CAI_CALLEE_MEM);
    bufp(&b, "  (import \"caller\" \"realloc\" (func $realloc_0"
             " (param i32 i32 i32 i32) (result i32)))\n");
    IMPORT(CAI_CALLER_REALLOC);
    bufp(&b, "  (import \"callee\" \"realloc\" (func $realloc_1"
             " (param i32 i32 i32 i32) (result i32)))\n");
    IMPORT(CAI_CALLEE_REALLOC);
  }
  bufp(&b, "  (import \"callee\" \"fn\" (func $callee");
  for (uint32_t k = 0; k < li.nparams; k++)
    bufp(&b, " (param %s)", canon_flat_name(li.params[k]));
  for (uint32_t k = 0; k < li.nresults; k++)
    bufp(&b, " (result %s)", canon_flat_name(li.results[k]));
  bufp(&b, "))\n");
  IMPORT(CAI_CALLEE_FUNC);
  if (post) {

    bufp(&b, "  (import \"callee\" \"post\" (func $post");
    for (uint32_t k = 0; k < li.nresults; k++)
      bufp(&b, " (param %s)", canon_flat_name(li.results[k]));
    bufp(&b, "))\n");
    IMPORT(CAI_CALLEE_POST);
  }
#undef IMPORT

  if (str_dn || str_up) bufp(&b, "%s", ADAPTER_UTF8_VALID_DOC);
  if (str_dn) { emit_utf8_valid(&b, 0); emit_pair_helpers(&b, 0, 1); }
  if (str_up) { emit_utf8_valid(&b, 1); emit_pair_helpers(&b, 1, 0); }

  Fn f = { 0 };
  adapter_body(&f, dn, up, ft, pspill, rspill, "$callee", post ? "$post" : NULL);

  bufp(&b, "  (func (export \"%s\")", CANON_ADAPTER_EXPORT);
  if (pspill) bufp(&b, " (param $p i32)");
  else for (uint32_t k = 0; k < npf; k++)
    bufp(&b, " (param $f%u %s)", k, canon_flat_name(pf[k]));
  if (rspill) {
    bufp(&b, " (param $ret i32)\n");
  } else {
    bufp(&b, " (result");
    for (uint32_t k = 0; k < lo.nresults; k++)
      bufp(&b, " %s", canon_flat_name(lo.results[k]));
    bufp(&b, ")\n");
  }
  fn_flush(&b, &f);
  bufp(&b, "  )\n)\n");
  return b.s;
}

void canon_dump_adapters(const Component *c, bool ptr64, FILE *out) {
  CanonScope top = { NULL, c, NULL };
  Wat b = { 0 };

  bufp(&b, "(module\n  (memory (export \"mem\") 2)\n"
           "  (memory (export \"mem1\") 2)\n");

  bufp(&b, "  (global $posts (export \"posts\") (mut i32) (i32.const 0))\n");

  bufp(&b, "  (global $skew (mut i32) (i32.const 0))\n");

  bufp(&b, "%s", ADAPTER_UTF8_VALID_DOC);
  for (uint32_t m = 0; m <= ADAPTER_CALLEE_MEM; m++) emit_mem_helpers(&b, m);
  bufp(&b, "%s", ADAPTER_HELPERS);

  for (uint32_t i = 0; i <= ADAPTER_CALLEE_MEM; i++)
    for (uint32_t j = 0; j <= ADAPTER_CALLEE_MEM; j++)
      emit_pair_helpers(&b, i, j);
  adapters_in_scope(&b, &top, ptr64, "");
  bufp(&b, ")\n");
  fputs(b.s, out);
  xfree(b.s);
}
