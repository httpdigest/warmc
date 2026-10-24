#include "component.h"
#include "canon.h"
#include "reader.h"

#define CM_MAX_FIXED_LIST 65536u

static void grow(void **arr, uint32_t n, size_t elem);
static void func_slot(Component *c, CmFuncSlotKind k, uint32_t i);
static void inst_slot(Component *c, CmInstSlotKind k, uint32_t i);
static void cfunc_slot(Component *c, CmCoreFuncSlotKind k, uint32_t i);

static void cfunc_slot(Component *c, CmCoreFuncSlotKind k, uint32_t i) {
  grow((void **)&c->cfunc_space, c->ncfunc_space, sizeof *c->cfunc_space);
  c->cfunc_space[c->ncfunc_space].kind = k;
  c->cfunc_space[c->ncfunc_space].index = i;
  c->ncfunc_space++;
  c->n_core_func++;
}

static void inst_slot(Component *c, CmInstSlotKind k, uint32_t i) {
  grow((void **)&c->inst_space, c->ninst_space, sizeof *c->inst_space);
  c->inst_space[c->ninst_space].kind = k;
  c->inst_space[c->ninst_space].index = i;
  c->ninst_space++;
  c->n_instance++;
}

static void func_slot(Component *c, CmFuncSlotKind k, uint32_t i) {
  grow((void **)&c->func_space, c->nfunc_space, sizeof *c->func_space);
  c->func_space[c->nfunc_space].kind = k;
  c->func_space[c->nfunc_space].index = i;
  c->nfunc_space++;
  c->n_func++;
}

static void ord(Component *c, CmOrderKind k, uint32_t i) {
  grow((void **)&c->order, c->norder, sizeof *c->order);
  c->order[c->norder].kind = k;
  c->order[c->norder].index = i;
  c->norder++;
}

static void type_slot(Component *c, CmTypeSlotKind k, uint32_t i) {
  grow((void **)&c->type_space, c->ntype_space, sizeof *c->type_space);
  c->type_space[c->ntype_space].kind = k;
  c->type_space[c->ntype_space].index = i;
  c->ntype_space++;
  c->n_type++;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMPONENT_VERSION 0x0001000dU

bool wasm_is_component(const uint8_t *bin, size_t len) {
  if (len < 8) return false;
  uint32_t magic = (uint32_t)bin[0] | ((uint32_t)bin[1] << 8) |
                   ((uint32_t)bin[2] << 16) | ((uint32_t)bin[3] << 24);
  if (magic != 0x6d736100u) return false;

  return bin[4] == 0x0d && bin[5] == 0x00;
}

static uint32_t rd_count(Rd *r) {
  uint32_t n = rd_u32(r);
  if ((size_t)n > (size_t)(r->end - r->p))
    warmc_malformed("vector of %u elements does not fit in the section", n);
  return n;
}

static char *rd_cm_name(Rd *r) {
  uint32_t n = rd_u32(r);
  rd_check(r, n);
  wasm_check_utf8(r->p, n);
  char *s = xstrndup((const char *)r->p, n);
  r->p += n;
  return s;
}

static uint32_t rd_idx(Rd *r, uint32_t space, const char *what) {
  uint32_t i = rd_u32(r);
  if (i >= space) warmc_invalid("unknown %s %u", what, i);
  return i;
}

static char *rd_name_attributes(Rd *r) {
  uint8_t tag = rd_u8(r);
  if (tag > 0x02) warmc_malformed("unknown name attribute form 0x%02x", tag);
  char *name = rd_cm_name(r);
  if (tag == 0x02) {
    uint32_t n = rd_count(r);
    for (uint32_t i = 0; i < n; i++) {
      uint8_t a = rd_u8(r);
      switch (a) {
        case 0x00: case 0x01: xfree(rd_cm_name(r)); break;
        case 0x02: xfree(rd_cm_name(r)); break;
        default: warmc_malformed("unknown name attribute 0x%02x", a);
      }
    }
  }
  return name;
}

static CmValType rd_cm_valtype(Rd *r, const Component *c) {
  CmValType v = { 0 };

  const uint8_t *save = r->p;
  uint8_t b = rd_u8(r);
  switch (b) {
    case CMP_BOOL: case CMP_S8: case CMP_U8: case CMP_S16: case CMP_U16:
    case CMP_S32: case CMP_U32: case CMP_S64: case CMP_U64:
    case CMP_F32: case CMP_F64: case CMP_CHAR: case CMP_STRING:
    case CMP_ERROR_CONTEXT:
      v.prim = b;
      return v;
    default: break;
  }
  r->p = save;
  v.is_index = true;
  v.index = rd_idx(r, c->n_type, "type");
  return v;
}

static CmOptValType rd_cm_opt_valtype(Rd *r, const Component *c) {
  CmOptValType o = { 0 };
  uint8_t tag = rd_u8(r);
  if (tag == 0x01) { o.present = true; o.t = rd_cm_valtype(r, c); }
  else if (tag != 0x00) warmc_malformed("malformed optional value type 0x%02x", tag);
  return o;
}

static void rd_defval(Rd *r, const Component *c, uint8_t tag, CmDefVal *d) {
  memset(d, 0, sizeof *d);
  switch (tag) {
    case 0x72:
      d->kind = CMD_RECORD;
      d->nfields = rd_count(r);
      if (!d->nfields) warmc_invalid("a record must have at least one field");
      d->fields = xcalloc(d->nfields, sizeof *d->fields);
      for (uint32_t i = 0; i < d->nfields; i++) {
        d->fields[i].label = rd_cm_name(r);
        d->fields[i].t = rd_cm_valtype(r, c);
      }
      break;
    case 0x71:
      d->kind = CMD_VARIANT;
      d->ncases = rd_count(r);
      if (!d->ncases) warmc_invalid("a variant must have at least one case");
      d->cases = xcalloc(d->ncases, sizeof *d->cases);
      for (uint32_t i = 0; i < d->ncases; i++) {
        d->cases[i].label = rd_cm_name(r);
        d->cases[i].t = rd_cm_opt_valtype(r, c);

        if (rd_u8(r) != 0x00) warmc_malformed("malformed variant case");
      }
      break;
    case 0x70: d->kind = CMD_LIST; d->elem = rd_cm_valtype(r, c); break;
    case 0x67:
      d->kind = CMD_FIXED_LIST;
      d->elem = rd_cm_valtype(r, c);
      d->len = rd_u32(r);
      if (!d->len) warmc_invalid("a fixed-size list must have a non-zero length");
      if (d->len > CM_MAX_FIXED_LIST)
        warmc_invalid("a fixed-size list of %u elements is above the limit "
                      "of %u", d->len, CM_MAX_FIXED_LIST);
      break;
    case 0x6f:
      d->kind = CMD_TUPLE;
      d->nelems = rd_count(r);
      if (!d->nelems) warmc_invalid("a tuple must have at least one element");
      d->elems = xcalloc(d->nelems, sizeof *d->elems);
      for (uint32_t i = 0; i < d->nelems; i++) d->elems[i] = rd_cm_valtype(r, c);
      break;
    case 0x6e:
    case 0x6d:
      d->kind = tag == 0x6e ? CMD_FLAGS : CMD_ENUM;
      d->nlabels = rd_count(r);
      if (!d->nlabels)
        warmc_invalid("a %s must have at least one label",
                      tag == 0x6e ? "flags" : "enum");
      if (tag == 0x6e && d->nlabels > 32)
        warmc_invalid("flags may have at most 32 labels, not %u", d->nlabels);
      d->labels = xcalloc(d->nlabels, sizeof *d->labels);
      for (uint32_t i = 0; i < d->nlabels; i++) d->labels[i] = rd_cm_name(r);
      break;
    case 0x6b: d->kind = CMD_OPTION; d->elem = rd_cm_valtype(r, c); break;
    case 0x6a:
      d->kind = CMD_RESULT;
      d->ok = rd_cm_opt_valtype(r, c);
      d->err = rd_cm_opt_valtype(r, c);
      break;
    case 0x69: case 0x68:
      d->kind = tag == 0x69 ? CMD_OWN : CMD_BORROW;
      d->elem.is_index = true;
      d->elem.index = rd_idx(r, c->n_type, "type");
      break;
    case 0x66: case 0x65:
      d->kind = tag == 0x66 ? CMD_STREAM : CMD_FUTURE;
      d->ok = rd_cm_opt_valtype(r, c);
      break;
    default:

      switch (tag) {
        case CMP_BOOL: case CMP_S8: case CMP_U8: case CMP_S16: case CMP_U16:
        case CMP_S32: case CMP_U32: case CMP_S64: case CMP_U64:
        case CMP_F32: case CMP_F64: case CMP_CHAR: case CMP_STRING:
        case CMP_ERROR_CONTEXT:
          break;
        default:
          warmc_malformed("malformed type constructor 0x%02x", tag);
      }
      d->kind = CMD_PRIM;
      d->prim = tag;
      break;
  }
}

static CmExternType rd_externtype(Rd *r, const Component *c) {
  CmExternType e = { 0 };
  uint8_t tag = rd_u8(r);
  switch (tag) {
    case 0x00:

      if (rd_u8(r) != 0x11) warmc_malformed("only core modules may be imported");
      e.kind = CME_CORE_MODULE;
      e.index = rd_idx(r, c->n_core_type, "core type");
      break;
    case 0x01: e.kind = CME_FUNC; e.index = rd_idx(r, c->n_type, "type"); break;
    case 0x02: {
      e.kind = CME_VALUE;
      uint8_t b = rd_u8(r);
      if (b == 0x00) e.index = rd_idx(r, c->n_value, "value");
      else if (b == 0x01) { Rd save = *r; (void)save; rd_cm_valtype(r, c); }
      else warmc_malformed("malformed value bound 0x%02x", b);
      break;
    }
    case 0x03:
      e.kind = CME_TYPE;
      e.bound = rd_u8(r);
      if (e.bound == 0x00) e.index = rd_idx(r, c->n_type, "type");
      else if (e.bound != 0x01) warmc_malformed("malformed type bound 0x%02x", e.bound);
      break;
    case 0x04: e.kind = CME_COMPONENT; e.index = rd_idx(r, c->n_type, "type"); break;
    case 0x05: e.kind = CME_INSTANCE; e.index = rd_idx(r, c->n_type, "type"); break;
    default: warmc_malformed("malformed extern type 0x%02x", tag);
  }
  return e;
}

static void rd_type(Rd *r, Component *c, CmType *t);
static void rd_core_type(Rd *r, Component *c, CmType *t);
static void rd_alias(Rd *r, Component *c, CmAlias *a, uint32_t owner);
static void free_type(CmType *t);

#define CM_MAX_NESTING 128u
static uint32_t cm_nesting;

static void cm_enter(void) {
  if (++cm_nesting > CM_MAX_NESTING)
    warmc_malformed("component definitions nest deeper than %u",
                    CM_MAX_NESTING);
}

static void cm_leave(void) { cm_nesting--; }

static void rd_decls(Rd *r, Component *outer, CmDeclList *dl, bool component_type) {
  Component scope;
  memset(&scope, 0, sizeof scope);
  (void)outer;
  dl->ndecls = rd_count(r);
  dl->decls = xcalloc(dl->ndecls ? dl->ndecls : 1, sizeof *dl->decls);
  for (uint32_t i = 0; i < dl->ndecls; i++) {
    CmDecl *d = &dl->decls[i];
    uint8_t tag = rd_u8(r);
    switch (tag) {
      case 0x00:
        d->kind = CMDECL_CORE_TYPE;
        rd_core_type(r, &scope, &d->type);
        scope.n_core_type++;
        break;
      case 0x01:
        d->kind = CMDECL_TYPE;
        rd_type(r, &scope, &d->type);
        type_slot(&scope, CMTS_DECLARED, i);
        break;
      case 0x02:
        d->kind = CMDECL_ALIAS;
        rd_alias(r, &scope, &d->alias, i);
        break;
      case 0x04:
        d->kind = CMDECL_EXPORT;
        d->name = rd_name_attributes(r);
        d->et = rd_externtype(r, &scope);

        if (d->et.kind == CME_TYPE) type_slot(&scope, CMTS_EXPORT, i);
        else if (d->et.kind == CME_INSTANCE) inst_slot(&scope, CMIS_DECL, i);
        else if (d->et.kind == CME_FUNC) func_slot(&scope, CMFS_DECL, i);
        else if (d->et.kind == CME_COMPONENT) scope.n_component++;
        else if (d->et.kind == CME_VALUE) scope.n_value++;
        break;
      case 0x03:
        if (!component_type)
          warmc_malformed("an instance type may not contain an import");
        d->kind = CMDECL_IMPORT;
        d->name = rd_name_attributes(r);
        d->et = rd_externtype(r, &scope);
        if (d->et.kind == CME_TYPE) type_slot(&scope, CMTS_IMPORT, i);
        else if (d->et.kind == CME_INSTANCE) inst_slot(&scope, CMIS_DECL, i);
        else if (d->et.kind == CME_FUNC) func_slot(&scope, CMFS_DECL, i);
        else if (d->et.kind == CME_COMPONENT) scope.n_component++;
        else if (d->et.kind == CME_VALUE) scope.n_value++;
        break;
      default:
        warmc_malformed("malformed %s declarator 0x%02x",
                        component_type ? "component" : "instance", tag);
    }
  }

  dl->type_space = scope.type_space;
  dl->ntype_space = scope.ntype_space;
  dl->func_space = scope.func_space;
  dl->nfunc_space = scope.nfunc_space;
  dl->inst_space = scope.inst_space;
  dl->ninst_space = scope.ninst_space;
}

static void rd_functype(Rd *r, Component *c, CmFuncType *f, bool is_async) {
  memset(f, 0, sizeof *f);
  f->is_async = is_async;
  f->nparams = rd_count(r);
  f->params = xcalloc(f->nparams ? f->nparams : 1, sizeof *f->params);
  for (uint32_t i = 0; i < f->nparams; i++) {
    f->params[i].label = rd_cm_name(r);
    f->params[i].t = rd_cm_valtype(r, c);
  }
  uint8_t tag = rd_u8(r);
  if (tag == 0x00) { f->has_result = true; f->result = rd_cm_valtype(r, c); }
  else if (tag == 0x01) {
    if (rd_u8(r) != 0x00) warmc_malformed("malformed empty result list");
  } else warmc_malformed("malformed result list 0x%02x", tag);
}

static void rd_type(Rd *r, Component *c, CmType *t) {
  memset(t, 0, sizeof *t);
  cm_enter();
  uint8_t tag = rd_u8(r);
  switch (tag) {
    case 0x40: t->kind = CMT_FUNC; rd_functype(r, c, &t->func, false); break;
    case 0x43: t->kind = CMT_FUNC; rd_functype(r, c, &t->func, true); break;
    case 0x41: t->kind = CMT_COMPONENT; rd_decls(r, c, &t->decls, true); break;
    case 0x42: t->kind = CMT_INSTANCE; rd_decls(r, c, &t->decls, false); break;
    case 0x3f:
      t->kind = CMT_RESOURCE;
      t->rep = rd_cm_valtype(r, c);
      {
        uint8_t b = rd_u8(r);
        if (b == 0x01) { t->has_dtor = true; t->dtor = rd_u32(r); }
        else if (b != 0x00) warmc_malformed("malformed resource destructor");
      }
      break;
    default:
      t->kind = CMT_DEFVAL;
      rd_defval(r, c, tag, &t->defval);
      break;
  }
  cm_leave();
}

static void rd_core_type(Rd *r, Component *c, CmType *t) {
  memset(t, 0, sizeof *t);
  cm_enter();
  const uint8_t *save = r->p;
  uint8_t tag = rd_u8(r);
  if (tag == 0x50) {

    t->kind = CMT_INSTANCE;
    uint32_t n = rd_count(r);
    t->decls.ndecls = n;
    t->decls.decls = xcalloc(n ? n : 1, sizeof *t->decls.decls);
    for (uint32_t i = 0; i < n; i++) {
      uint8_t d = rd_u8(r);
      switch (d) {
        case 0x00:
          xfree(rd_cm_name(r));
          xfree(rd_cm_name(r));
          wasm_skip_core_externtype(r);
          break;
        case 0x01: {
          CmType sub;
          rd_core_type(r, c, &sub);
          free_type(&sub);
          break;
        }
        case 0x02:
          if (rd_u8(r) != 0x10 || rd_u8(r) != 0x01)
            warmc_malformed("malformed core alias in a module type");
          rd_u32(r); rd_u32(r);
          break;
        case 0x03:
          xfree(rd_cm_name(r));
          wasm_skip_core_externtype(r);
          break;
        default: warmc_malformed("malformed module type declarator 0x%02x", d);
      }
    }
    cm_leave();
    return;
  }

  r->p = save;
  t->kind = CMT_DEFVAL;
  wasm_skip_core_rectype(r);
  cm_leave();
}

static uint8_t rd_core_sort(Rd *r) {
  uint8_t s = rd_u8(r);
  switch (s) {
    case CMCS_FUNC: case CMCS_TABLE: case CMCS_MEMORY: case CMCS_GLOBAL:
    case CMCS_TAG: case CMCS_TYPE: case CMCS_MODULE: case CMCS_INSTANCE:
      return s;
    default: warmc_malformed("malformed core sort 0x%02x", s);
  }
  return 0;
}

static CmSortIdx rd_sort(Rd *r) {
  CmSortIdx si = { 0 };
  si.sort = rd_u8(r);
  switch (si.sort) {
    case CMS_CORE: si.core_sort = rd_core_sort(r); break;
    case CMS_FUNC: case CMS_VALUE: case CMS_TYPE:
    case CMS_COMPONENT: case CMS_INSTANCE: break;
    default: warmc_malformed("malformed sort 0x%02x", si.sort);
  }
  return si;
}

static uint32_t space_of(const Component *c, const CmSortIdx *si, const char **what) {
  if (si->sort == CMS_CORE) {
    switch (si->core_sort) {
      case CMCS_FUNC:     *what = "core function"; return c->n_core_func;
      case CMCS_TABLE:    *what = "core table";    return c->n_core_table;
      case CMCS_MEMORY:   *what = "core memory";   return c->n_core_memory;
      case CMCS_GLOBAL:   *what = "core global";   return c->n_core_global;
      case CMCS_TAG:      *what = "core tag";      return c->n_core_tag;
      case CMCS_TYPE:     *what = "core type";     return c->n_core_type;
      case CMCS_MODULE:   *what = "core module";   return c->n_core_module;
      default:            *what = "core instance"; return c->n_core_instance;
    }
  }
  switch (si->sort) {
    case CMS_FUNC:      *what = "function";  return c->n_func;
    case CMS_VALUE:     *what = "value";     return c->n_value;
    case CMS_TYPE:      *what = "type";      return c->n_type;
    case CMS_COMPONENT: *what = "component"; return c->n_component;
    default:            *what = "instance";  return c->n_instance;
  }
}

static void bump_space(Component *c, const CmSortIdx *si, uint32_t owner,
                       bool from_export) {
  if (si->sort == CMS_CORE) {
    switch (si->core_sort) {
      case CMCS_FUNC:     cfunc_slot(c, CMCF_ALIAS, owner); break;
      case CMCS_TABLE:    c->n_core_table++; break;
      case CMCS_MEMORY:   c->n_core_memory++; break;
      case CMCS_GLOBAL:   c->n_core_global++; break;
      case CMCS_TAG:      c->n_core_tag++; break;
      case CMCS_TYPE:     c->n_core_type++; break;
      case CMCS_MODULE:   c->n_core_module++; break;
      default:            c->n_core_instance++; break;
    }
    return;
  }
  switch (si->sort) {
    case CMS_FUNC:      func_slot(c, from_export ? CMFS_EXPORT : CMFS_ALIAS, owner); break;
    case CMS_VALUE:     c->n_value++; break;
    case CMS_TYPE:      type_slot(c, from_export ? CMTS_EXPORT : CMTS_ALIAS, owner); break;
    case CMS_COMPONENT: c->n_component++; break;
    default:            inst_slot(c, from_export ? CMIS_EXPORT : CMIS_ALIAS, owner); break;
  }
}

static CmSortIdx rd_sortidx(Rd *r, const Component *c) {
  CmSortIdx si = rd_sort(r);
  const char *what;
  uint32_t space = space_of(c, &si, &what);
  si.index = rd_idx(r, space, what);
  return si;
}

static void rd_alias(Rd *r, Component *c, CmAlias *a, uint32_t owner) {
  memset(a, 0, sizeof *a);
  CmSortIdx si = rd_sort(r);
  uint8_t form = rd_u8(r);
  a->target = si;
  switch (form) {
    case 0x00:
      a->kind = CMA_EXPORT;
      a->instance = rd_idx(r, c->n_instance, "instance");
      a->name = rd_cm_name(r);
      break;
    case 0x01:
      a->kind = CMA_CORE_EXPORT;
      a->instance = rd_idx(r, c->n_core_instance, "core instance");
      a->name = rd_cm_name(r);
      break;
    case 0x02:
      a->kind = CMA_OUTER;
      a->count = rd_u32(r);
      a->index = rd_u32(r);
      break;
    default: warmc_malformed("malformed alias form 0x%02x", form);
  }
  bump_space(c, &si, owner, false);
}

static void rd_core_instance(Rd *r, Component *c, CmInstance *in) {
  memset(in, 0, sizeof *in);
  uint8_t tag = rd_u8(r);
  if (tag == 0x00) {
    in->instantiate = true;
    in->index = rd_idx(r, c->n_core_module, "core module");
    in->nargs = rd_count(r);
    in->arg_names = xcalloc(in->nargs ? in->nargs : 1, sizeof *in->arg_names);
    in->args = xcalloc(in->nargs ? in->nargs : 1, sizeof *in->args);
    for (uint32_t i = 0; i < in->nargs; i++) {
      in->arg_names[i] = rd_cm_name(r);

      if (rd_u8(r) != 0x12)
        warmc_malformed("only an instance may be a core instantiation argument");
      in->args[i].sort = CMS_CORE;
      in->args[i].core_sort = CMCS_INSTANCE;
      in->args[i].index = rd_idx(r, c->n_core_instance, "core instance");
    }
  } else if (tag == 0x01) {
    in->nexports = rd_count(r);
    in->export_names = xcalloc(in->nexports ? in->nexports : 1, sizeof *in->export_names);
    in->exports = xcalloc(in->nexports ? in->nexports : 1, sizeof *in->exports);
    for (uint32_t i = 0; i < in->nexports; i++) {
      in->export_names[i] = rd_cm_name(r);
      CmSortIdx si = { 0 };
      si.sort = CMS_CORE;
      si.core_sort = rd_core_sort(r);
      const char *what;
      si.index = rd_idx(r, space_of(c, &si, &what), what);
      in->exports[i] = si;
    }
  } else warmc_malformed("malformed core instance 0x%02x", tag);
  c->n_core_instance++;
}

static void rd_instance(Rd *r, Component *c, CmInstance *in) {
  memset(in, 0, sizeof *in);
  uint8_t tag = rd_u8(r);
  if (tag == 0x00) {
    in->instantiate = true;
    in->index = rd_idx(r, c->n_component, "component");
    in->nargs = rd_count(r);
    in->arg_names = xcalloc(in->nargs ? in->nargs : 1, sizeof *in->arg_names);
    in->args = xcalloc(in->nargs ? in->nargs : 1, sizeof *in->args);
    for (uint32_t i = 0; i < in->nargs; i++) {
      in->arg_names[i] = rd_cm_name(r);
      in->args[i] = rd_sortidx(r, c);
    }
  } else if (tag == 0x01) {
    in->nexports = rd_count(r);
    in->export_names = xcalloc(in->nexports ? in->nexports : 1, sizeof *in->export_names);
    in->exports = xcalloc(in->nexports ? in->nexports : 1, sizeof *in->exports);
    for (uint32_t i = 0; i < in->nexports; i++) {
      in->export_names[i] = rd_name_attributes(r);
      in->exports[i] = rd_sortidx(r, c);
    }
  } else warmc_malformed("malformed instance 0x%02x", tag);
  inst_slot(c, CMIS_INSTANTIATE, c->ninstances - 1);
}

static void rd_opts(Rd *r, Component *c, CmCanon *cn) {
  cn->nopts = rd_count(r);
  cn->opts = xcalloc(cn->nopts ? cn->nopts : 1, sizeof *cn->opts);
  for (uint32_t i = 0; i < cn->nopts; i++) {
    uint8_t o = rd_u8(r);
    cn->opts[i].code = o;
    switch (o) {
      case 0x00: case 0x01: case 0x02: break;
      case 0x03: cn->opts[i].index = rd_idx(r, c->n_core_memory, "core memory"); break;
      case 0x04: case 0x05: case 0x07:
        cn->opts[i].index = rd_idx(r, c->n_core_func, "core function"); break;
      case 0x06: break;
      default: warmc_malformed("malformed canonical option 0x%02x", o);
    }
  }
}

static void rd_canon(Rd *r, Component *c, CmCanon *cn) {
  memset(cn, 0, sizeof *cn);
  uint32_t op = rd_u8(r);
  cn->opcode = op;
  switch (op) {
    case 0x00:
      if (rd_u8(r) != 0x00) warmc_malformed("canon lift must name a core function");
      cn->kind = CMC_LIFT;
      cn->func = rd_idx(r, c->n_core_func, "core function");
      rd_opts(r, c, cn);
      cn->type = rd_idx(r, c->n_type, "type");
      /* the options' own rules, here so that validating a component
         refuses what compiling it does */
      { CanonOpts o; canon_options(cn, true, &o); }
      func_slot(c, CMFS_LIFT, (uint32_t)(cn - c->canons));
      break;
    case 0x01:
      if (rd_u8(r) != 0x00) warmc_malformed("canon lower must name a function");
      cn->kind = CMC_LOWER;
      cn->func = rd_idx(r, c->n_func, "function");
      rd_opts(r, c, cn);
      { CanonOpts o; canon_options(cn, false, &o); }
      cfunc_slot(c, CMCF_LOWER, (uint32_t)(cn - c->canons));
      break;
    case 0x02: case 0x03: case 0x04:
      cn->kind = op == 0x02 ? CMC_RESOURCE_NEW
               : op == 0x03 ? CMC_RESOURCE_DROP : CMC_RESOURCE_REP;
      cn->type = rd_idx(r, c->n_type, "type");
      cfunc_slot(c, CMCF_BUILTIN, (uint32_t)(cn - c->canons));
      break;
    default:

      warmc_invalid("canonical built-in 0x%02x is not supported "
                    "(async, threading and error-context are not implemented)",
                    op);
  }
}

static void grow(void **arr, uint32_t n, size_t elem) {
  *arr = xrealloc(*arr, (size_t)(n + 1) * elem);
}

Component *comp_parse(const uint8_t *bin, size_t len) {
  Rd r = rd_make(bin, bin + len, bin);
  uint32_t magic = rd_u32le(&r);
  uint32_t version = rd_u32le(&r);
  if (magic != 0x6d736100u) warmc_malformed("magic header not detected: 0x%08x", magic);
  if (version != COMPONENT_VERSION)
    warmc_malformed("unknown component version 0x%08x", version);

  Component *c = xcalloc(1, sizeof *c);

  cm_enter();
  while (!rd_eof(&r)) {
    uint8_t id = rd_u8(&r);
    uint32_t size = rd_u32(&r);
    rd_check(&r, size);
    Rd sec = rd_make(r.p, r.p + size, bin);
    r.p += size;

    switch (id) {
      case 0:
        break;
      case 1: {
        grow((void **)&c->modules, c->nmodules, sizeof *c->modules);
        c->modules[c->nmodules] = wasm_parse(sec.p, (size_t)(sec.end - sec.p));
        wasm_validate(c->modules[c->nmodules++]);
        c->n_core_module++;
        ord(c, CMO_MODULE, c->nmodules - 1);
        sec.p = sec.end;
        break;
      }
      case 2:
        for (uint32_t n = rd_count(&sec), i = 0; i < n; i++) {
          grow((void **)&c->core_instances, c->ncore_instances, sizeof *c->core_instances);
          rd_core_instance(&sec, c, &c->core_instances[c->ncore_instances++]);
          ord(c, CMO_CORE_INSTANCE, c->ncore_instances - 1);
        }
        break;
      case 3:
        for (uint32_t n = rd_count(&sec), i = 0; i < n; i++) {
          grow((void **)&c->core_types, c->ncore_types, sizeof *c->core_types);
          rd_core_type(&sec, c, &c->core_types[c->ncore_types++]);
          c->n_core_type++;
          ord(c, CMO_CORE_TYPE, c->ncore_types - 1);
        }
        break;
      case 4: {
        grow((void **)&c->components, c->ncomponents, sizeof *c->components);
        c->components[c->ncomponents++] = comp_parse(sec.p, (size_t)(sec.end - sec.p));
        c->n_component++;
        ord(c, CMO_COMPONENT, c->ncomponents - 1);
        sec.p = sec.end;
        break;
      }
      case 5:
        for (uint32_t n = rd_count(&sec), i = 0; i < n; i++) {
          grow((void **)&c->instances, c->ninstances, sizeof *c->instances);
          rd_instance(&sec, c, &c->instances[c->ninstances++]);
          ord(c, CMO_INSTANCE, c->ninstances - 1);
        }
        break;
      case 6:
        for (uint32_t n = rd_count(&sec), i = 0; i < n; i++) {
          grow((void **)&c->aliases, c->naliases, sizeof *c->aliases);
          rd_alias(&sec, c, &c->aliases[c->naliases], c->naliases);
          c->naliases++;
          ord(c, CMO_ALIAS, c->naliases - 1);
        }
        break;
      case 7:
        for (uint32_t n = rd_count(&sec), i = 0; i < n; i++) {
          grow((void **)&c->types, c->ntypes, sizeof *c->types);
          rd_type(&sec, c, &c->types[c->ntypes++]);
          type_slot(c, CMTS_DECLARED, c->ntypes - 1);
          ord(c, CMO_TYPE, c->ntypes - 1);
        }
        break;
      case 8:
        for (uint32_t n = rd_count(&sec), i = 0; i < n; i++) {
          grow((void **)&c->canons, c->ncanons, sizeof *c->canons);
          rd_canon(&sec, c, &c->canons[c->ncanons++]);
          ord(c, CMO_CANON, c->ncanons - 1);
        }
        break;
      case 9:
        if (c->has_start) warmc_malformed("more than one start section");
        c->has_start = true;
        c->start_func = rd_idx(&sec, c->n_func, "function");
        c->start_nargs = rd_count(&sec);
        c->start_args = xcalloc(c->start_nargs ? c->start_nargs : 1,
                                sizeof *c->start_args);
        for (uint32_t i = 0; i < c->start_nargs; i++)
          c->start_args[i] = rd_idx(&sec, c->n_value, "value");
        c->start_nresults = rd_u32(&sec);
        c->n_value += c->start_nresults;
        ord(c, CMO_START, 0);
        break;
      case 10:
        for (uint32_t n = rd_count(&sec), i = 0; i < n; i++) {
          grow((void **)&c->imports, c->nimports, sizeof *c->imports);
          CmImport *im = &c->imports[c->nimports++];
          memset(im, 0, sizeof *im);
          im->name = rd_name_attributes(&sec);
          im->et = rd_externtype(&sec, c);

          switch (im->et.kind) {
            case CME_CORE_MODULE: c->n_core_module++; break;
            case CME_FUNC:        func_slot(c, CMFS_IMPORT, c->nimports - 1); break;
            case CME_VALUE:       c->n_value++; break;
            case CME_TYPE:        type_slot(c, CMTS_IMPORT, c->nimports - 1); break;
            case CME_COMPONENT:   c->n_component++; break;
            case CME_INSTANCE:    inst_slot(c, CMIS_IMPORT, c->nimports - 1); break;
          }
          ord(c, CMO_IMPORT, c->nimports - 1);
        }
        break;
      case 11:
        for (uint32_t n = rd_count(&sec), i = 0; i < n; i++) {
          grow((void **)&c->exports, c->nexports, sizeof *c->exports);
          CmExport *ex = &c->exports[c->nexports++];
          memset(ex, 0, sizeof *ex);
          ex->name = rd_name_attributes(&sec);
          ex->si = rd_sortidx(&sec, c);
          uint8_t has = rd_u8(&sec);
          if (has == 0x01) { ex->has_type = true; ex->et = rd_externtype(&sec, c); }
          else if (has != 0x00) warmc_malformed("malformed export type");

          bump_space(c, &ex->si, c->nexports - 1, true);
          ord(c, CMO_EXPORT, c->nexports - 1);
        }
        break;
      case 12:
        warmc_invalid("value definitions are not supported");
        break;
      default:
        warmc_malformed("malformed component section id %u", id);
    }
    if (id != 0 && sec.p != sec.end)
      warmc_malformed("component section %u has %zd trailing bytes",
                      id, sec.end - sec.p);
  }
  cm_leave();
  return c;
}

static const char *sort_name(const CmSortIdx *si) {
  if (si->sort == CMS_CORE) {
    switch (si->core_sort) {
      case CMCS_FUNC: return "core func";
      case CMCS_TABLE: return "core table";
      case CMCS_MEMORY: return "core memory";
      case CMCS_GLOBAL: return "core global";
      case CMCS_TAG: return "core tag";
      case CMCS_TYPE: return "core type";
      case CMCS_MODULE: return "core module";
      default: return "core instance";
    }
  }
  switch (si->sort) {
    case CMS_FUNC: return "func";
    case CMS_VALUE: return "value";
    case CMS_TYPE: return "type";
    case CMS_COMPONENT: return "component";
    default: return "instance";
  }
}

static const char *type_kind_name(const CmType *t) {
  switch (t->kind) {
    case CMT_FUNC: return t->func.is_async ? "func async" : "func";
    case CMT_COMPONENT: return "component";
    case CMT_INSTANCE: return "instance";
    case CMT_RESOURCE: return "resource";
    default: break;
  }
  switch (t->defval.kind) {
    case CMD_RECORD: return "record";
    case CMD_VARIANT: return "variant";
    case CMD_LIST: return "list";
    case CMD_FIXED_LIST: return "fixed-list";
    case CMD_TUPLE: return "tuple";
    case CMD_FLAGS: return "flags";
    case CMD_ENUM: return "enum";
    case CMD_OPTION: return "option";
    case CMD_RESULT: return "result";
    case CMD_OWN: return "own";
    case CMD_BORROW: return "borrow";
    case CMD_STREAM: return "stream";
    case CMD_FUTURE: return "future";
    default: return "prim";
  }
}

static const char *canon_name(const CmCanon *cn) {
  switch (cn->kind) {
    case CMC_LIFT: return "lift";
    case CMC_LOWER: return "lower";
    case CMC_RESOURCE_NEW: return "resource.new";
    case CMC_RESOURCE_DROP: return "resource.drop";
    default: return "resource.rep";
  }
}

static void dump_at(const Component *c, FILE *out, int depth) {
  char ind[32];
  int n = depth * 2 < 30 ? depth * 2 : 30;
  memset(ind, ' ', (size_t)n);
  ind[n] = 0;

  for (uint32_t i = 0; i < c->nmodules; i++)
    fprintf(out, "%score module %u: %u funcs, %u exports\n", ind, i,
            c->modules[i]->nfuncs, c->modules[i]->nexports);
  for (uint32_t i = 0; i < c->ncore_types; i++)
    fprintf(out, "%score type %u\n", ind, i);
  for (uint32_t i = 0; i < c->ncore_instances; i++) {
    const CmInstance *in = &c->core_instances[i];
    if (in->instantiate)
      fprintf(out, "%score instance %u: instantiate module %u, %u args\n",
              ind, i, in->index, in->nargs);
    else
      fprintf(out, "%score instance %u: %u inline exports\n", ind, i, in->nexports);
  }
  for (uint32_t i = 0; i < c->ntypes; i++)
    fprintf(out, "%stype %u: %s\n", ind, i, type_kind_name(&c->types[i]));
  for (uint32_t i = 0; i < c->naliases; i++) {
    const CmAlias *a = &c->aliases[i];
    if (a->kind == CMA_OUTER)
      fprintf(out, "%salias %u: outer %u %u (%s)\n", ind, i, a->count, a->index,
              sort_name(&a->target));
    else
      fprintf(out, "%salias %u: %sexport %u \"%s\" (%s)\n", ind, i,
              a->kind == CMA_CORE_EXPORT ? "core " : "", a->instance, a->name,
              sort_name(&a->target));
  }
  for (uint32_t i = 0; i < c->ninstances; i++) {
    const CmInstance *in = &c->instances[i];
    if (in->instantiate)
      fprintf(out, "%sinstance %u: instantiate component %u, %u args\n",
              ind, i, in->index, in->nargs);
    else
      fprintf(out, "%sinstance %u: %u inline exports\n", ind, i, in->nexports);
  }
  for (uint32_t i = 0; i < c->ncanons; i++)
    fprintf(out, "%scanon %u: %s, %u options\n", ind, i,
            canon_name(&c->canons[i]), c->canons[i].nopts);
  for (uint32_t i = 0; i < c->nimports; i++)
    fprintf(out, "%simport %u: \"%s\" kind %u\n", ind, i, c->imports[i].name,
            (unsigned)c->imports[i].et.kind);
  for (uint32_t i = 0; i < c->nexports; i++)
    fprintf(out, "%sexport %u: \"%s\" (%s %u)\n", ind, i, c->exports[i].name,
            sort_name(&c->exports[i].si), c->exports[i].si.index);
  if (c->has_start)
    fprintf(out, "%sstart: func %u, %u args, %u results\n", ind,
            c->start_func, c->start_nargs, c->start_nresults);
  for (uint32_t i = 0; i < c->ncomponents; i++) {
    fprintf(out, "%scomponent %u:\n", ind, i);
    dump_at(c->components[i], out, depth + 1);
  }
}

void comp_dump(const Component *c, FILE *out) { dump_at(c, out, 0); }

static void free_decls(CmDeclList *dl);
static void free_type(CmType *t);

static void free_defval(CmDefVal *d) {
  for (uint32_t i = 0; i < d->nfields; i++) xfree(d->fields[i].label);
  xfree(d->fields);
  for (uint32_t i = 0; i < d->ncases; i++) xfree(d->cases[i].label);
  xfree(d->cases);
  for (uint32_t i = 0; i < d->nlabels; i++) xfree(d->labels[i]);
  xfree(d->labels);
  xfree(d->elems);
}

static void free_type(CmType *t) {
  switch (t->kind) {
    case CMT_DEFVAL: free_defval(&t->defval); break;
    case CMT_FUNC:
      for (uint32_t i = 0; i < t->func.nparams; i++) xfree(t->func.params[i].label);
      xfree(t->func.params);
      break;
    case CMT_COMPONENT: case CMT_INSTANCE: free_decls(&t->decls); break;
    default: break;
  }
}

static void free_decls(CmDeclList *dl) {
  for (uint32_t i = 0; i < dl->ndecls; i++) {
    xfree(dl->decls[i].name);

    xfree(dl->decls[i].alias.name);
    free_type(&dl->decls[i].type);
  }
  xfree(dl->decls);
  xfree(dl->type_space);
  xfree(dl->func_space);
  xfree(dl->inst_space);
}

static void free_instance(CmInstance *in) {
  for (uint32_t i = 0; i < in->nargs; i++) xfree(in->arg_names[i]);
  xfree(in->arg_names);
  xfree(in->args);
  for (uint32_t i = 0; i < in->nexports; i++) xfree(in->export_names[i]);
  xfree(in->export_names);
  xfree(in->exports);
}

void comp_free(Component *c) {
  if (!c) return;
  for (uint32_t i = 0; i < c->nmodules; i++) module_free(c->modules[i]);
  xfree(c->modules);
  for (uint32_t i = 0; i < c->ncomponents; i++) comp_free(c->components[i]);
  xfree(c->components);
  for (uint32_t i = 0; i < c->ncore_instances; i++) free_instance(&c->core_instances[i]);
  xfree(c->core_instances);
  for (uint32_t i = 0; i < c->ninstances; i++) free_instance(&c->instances[i]);
  xfree(c->instances);
  for (uint32_t i = 0; i < c->naliases; i++) xfree(c->aliases[i].name);
  xfree(c->aliases);
  for (uint32_t i = 0; i < c->ntypes; i++) free_type(&c->types[i]);
  xfree(c->types);
  for (uint32_t i = 0; i < c->ncore_types; i++) free_type(&c->core_types[i]);
  xfree(c->core_types);
  for (uint32_t i = 0; i < c->ncanons; i++) xfree(c->canons[i].opts);
  xfree(c->canons);
  for (uint32_t i = 0; i < c->nimports; i++) xfree(c->imports[i].name);
  xfree(c->imports);
  for (uint32_t i = 0; i < c->nexports; i++) xfree(c->exports[i].name);
  xfree(c->exports);
  xfree(c->start_args);
  xfree(c->order);
  xfree(c->type_space);
  xfree(c->func_space);
  xfree(c->inst_space);
  xfree(c->cfunc_space);
  xfree(c);
}
