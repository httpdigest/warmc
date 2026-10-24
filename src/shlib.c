/* warmc --shared: a module as a shared library that other programs call
   into through their C FFI.

   Every exported function C can call -- one taking and returning i32, i64,
   f32 and f64 -- gets a C symbol of its own name: its export index in a
   scratch register and a jump to warmc_lib_thunk, which hands the C
   arguments to the runtime's library face (rt/lib.c).  The library's own
   helpers, PREFIX_last_trap and the rest, are a jump each to a runtime
   function of the same signature.  shlib_write_header declares them all.

   An export several results returns them through pointers that follow its
   parameters.  Imports are functions the program loading the library binds
   with PREFIX_bind_import before the first call; each is called as
   R fn(void *ctx, params...) with the ctx it was bound with. */

#include "warmc.h"
#include "target.h"
#include "x64.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Shlib {
  Module *m;
  char *prefix;
  char **cname;              /* per export; NULL for one C cannot call */
  char **helper;             /* the C names of the helpers */
};

/* name, runtime function, C prototype after the name */
static const struct { const char *name, *rt, *proto, *ret; } HELPERS[] = {
  { "last_trap", "warmc_lib_last_trap", "(void)", "uint32_t" },
  { "trap_name", "warmc_trap_name", "(uint32_t trap)", "const char *" },
  { "memory", "warmc_lib_memory", "(size_t *size)", "uint8_t *" },
  { "bind_import", "warmc_lib_bind_import",
    "(const char *module, const char *name, void *fn, void *ctx)", "int" },
  { "custom_section", "warmc_lib_custom_section",
    "(const char *name, size_t index, const uint8_t **data, size_t *size)", "size_t" },
};
#define NHELPERS (sizeof HELPERS / sizeof HELPERS[0])

static const char *const C_RESERVED[] = {
  "auto", "break", "case", "char", "const", "continue", "default", "do",
  "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline",
  "int", "long", "register", "restrict", "return", "short", "signed",
  "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
  "void", "volatile", "while", "bool", "true", "false", "alignas", "alignof",
  "nullptr", "static_assert", "thread_local", "typeof", "_Bool", "_Complex",
  "_Imaginary", "main", "_start",
};

static bool c_scalar(ValType t) {
  return t == VT_I32 || t == VT_I64 || t == VT_F32 || t == VT_F64;
}

static const char *c_type_name(ValType t) {
  switch (t) {
    case VT_I32: return "int32_t";
    case VT_I64: return "int64_t";
    case VT_F32: return "float";
    default:     return "double";
  }
}

/* The parameters warmc_invoke can carry on arm64: seven integers after the
   instance and eight floats.  x64 passes the rest on the stack. */
/* arm64 passes 7 integer and 8 float parameters in registers and the C
   entry no more; x64 passes 3 and 4 and the rest in stack slots, as many as
   rt/lib.c's image of them holds (480 bytes, WARMC_X64_STACK_BYTES) */
static bool fits_registers(const CompType *t) {
  uint32_t ni = 0, nf = 0;
  for (uint32_t i = 0; i < t->nparams; i++)
    if (vt_is_float(t->params[i])) nf++; else ni++;
  if (g_target == &warmc_target_x64)
    return (ni > 3 ? ni - 3 : 0) + (nf > 4 ? nf - 4 : 0) <= 60;
  return ni <= 7 && nf <= 8;
}

/* Why C cannot call a function of type t, or NULL. */
static const char *c_callable(const CompType *t) {
  for (uint32_t i = 0; i < t->nparams; i++)
    if (!c_scalar(t->params[i])) return "a parameter is not i32, i64, f32 or f64";
  for (uint32_t i = 0; i < t->nresults; i++)
    if (!c_scalar(t->results[i])) return "a result is not i32, i64, f32 or f64";
  if (t->nresults > 16) return "it has more than 16 results";
  if (!fits_registers(t))
    return g_target == &warmc_target_x64
      ? "it takes more than 60 parameters past the 3 integer and 4 float registers"
      : "it takes more than 7 integer or 8 float parameters";
  return NULL;
}

/* The export's name as a C identifier: anything else becomes '_', and a
   name C or the runtime already has is taken under the prefix. */
static char *c_ident(const char *name, uint32_t len, const char *prefix) {
  char *s = xmalloc(len + 2);
  uint32_t n = 0;
  if (len && isdigit((unsigned char)name[0])) s[n++] = '_';
  for (uint32_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)name[i];
    s[n++] = (isalnum(c) || c == '_') ? (char)c : '_';
  }
  s[n] = 0;
  bool clash = !n || !strncmp(s, "warmc_", 6) || !strncmp(s, "__warmc", 7);
  for (size_t i = 0; i < sizeof C_RESERVED / sizeof C_RESERVED[0] && !clash; i++)
    if (!strcmp(s, C_RESERVED[i])) clash = true;
  if (!clash) return s;
  size_t pl = strlen(prefix);
  char *p = xmalloc(pl + 1 + n + 1);
  snprintf(p, pl + 1 + n + 1, "%s_%s", prefix, s);
  xfree(s);
  return p;
}

Shlib *shlib_plan(Module *m, const char *prefix, const char *input) {
  Shlib *p = xcalloc(1, sizeof *p);
  p->m = m;
  p->prefix = xmalloc(strlen(prefix) + 1);
  memcpy(p->prefix, prefix, strlen(prefix) + 1);
  p->cname = xcalloc(m->nexports ? m->nexports : 1, sizeof(char *));

  for (uint32_t i = 0; i < m->nimports; i++) {
    const Import *im = &m->imports[i];
    if (im->kind != EK_FUNC)
      warmc_fatal("--shared: %s imports %.*s.%.*s, which is not a function; a "
                  "library's imports are functions its host binds",
                  input, (int)im->module_len, im->module, (int)im->name_len,
                  im->name);
    const CompType *t = &m->types[im->type_index];
    const char *why = c_callable(t);
    if (!why && t->nresults > 1) why = "it returns more than one result";
    if (!why && g_target == &warmc_target_x64 && t->nparams > 3)
      why = "on Windows an import takes at most three parameters for now";
    if (why)
      warmc_fatal("--shared: %s imports %.*s.%.*s, which a C function cannot "
                  "stand in for: %s", input, (int)im->module_len, im->module,
                  (int)im->name_len, im->name, why);
  }

  uint32_t ncallable = 0;
  for (uint32_t i = 0; i < m->nexports; i++) {
    const Export *ex = &m->exports[i];
    if (ex->kind != EK_FUNC) continue;
    const CompType *t = &m->types[m->funcs[ex->index].type_index];
    const char *why = c_callable(t);
    if (why) {
      fprintf(stderr, "warmc: --shared: export \"%.*s\" gets no C symbol: %s\n",
              (int)ex->name_len, ex->name, why);
      continue;
    }
    p->cname[i] = c_ident(ex->name, ex->name_len, prefix);
    ncallable++;
  }

  p->helper = xcalloc(NHELPERS, sizeof(char *));
  for (size_t h = 0; h < NHELPERS; h++) {
    size_t n = strlen(prefix) + 1 + strlen(HELPERS[h].name) + 1;
    p->helper[h] = xmalloc(n);
    snprintf(p->helper[h], n, "%s_%s", prefix, HELPERS[h].name);
  }

  /* Two names that come out the same in C cannot both be symbols. */
  for (uint32_t i = 0; i < m->nexports; i++) {
    if (!p->cname[i]) continue;
    for (uint32_t j = i + 1; j < m->nexports; j++)
      if (p->cname[j] && !strcmp(p->cname[i], p->cname[j]))
        warmc_fatal("--shared: exports \"%.*s\" and \"%.*s\" both become the "
                    "C name %s", (int)m->exports[i].name_len, m->exports[i].name,
                    (int)m->exports[j].name_len, m->exports[j].name, p->cname[i]);
    for (size_t h = 0; h < NHELPERS; h++)
      if (!strcmp(p->cname[i], p->helper[h]))
        warmc_fatal("--shared: export \"%.*s\" is named like the library's own "
                    "%s; pick another --symbol-prefix",
                    (int)m->exports[i].name_len, m->exports[i].name, p->helper[h]);
  }
  if (!ncallable)
    fprintf(stderr, "warmc: --shared: %s exports no function C can call\n", input);
  return p;
}

static void a64_word(ObjBuilder *o, uint32_t w) { buf_u32(&o->text, w); }

/* A branch from here to sym, the only instruction a helper needs. */
static void emit_jump(ObjBuilder *o, const char *sym) {
  uint32_t s = obj_intern_sym(o, sym, SYM_UNDEF_FUNC, -1, 0);
  if (g_target == &warmc_target_x64) {
    x64_jmp_rel32(&o->text);
    obj_add_reloc(o, REL_PC32, 0, o->text.len - 4, s, -4);
  } else {
    obj_add_reloc(o, REL_CALL26, 0, o->text.len, s, 0);
    a64_word(o, 0x14000000u);
  }
}

/* x64: an adapter per import, which rt/lib.c binds in its place.  Wasm
   calls it with the internal convention -- integers in RDX/R8/R9 and floats
   in XMM0-3, class by class -- and RCX holding the {fn, ctx} pair the
   binding made; it moves each argument to its Win64 position after the
   ctx and jumps to the host's function with the ctx in RCX.  The moves run
   from the last parameter down, as none goes lower than it came. */
static void emit_adapters(Shlib *p, ObjBuilder *o) {
  static const int WIN_INT[4] = { X_RCX, X_RDX, X_R8, X_R9 };
  static const int IN_INT[3] = { X_RDX, X_R8, X_R9 };
  Module *m = p->m;
  uint32_t *sym = xcalloc(m->nimports ? m->nimports : 1, sizeof *sym);
  for (uint32_t i = 0; i < m->nimports; i++) {
    const CompType *t = &m->types[m->imports[i].type_index];
    char nm[64];
    snprintf(nm, sizeof nm, "__warmc_lib_adapter_%u", i);
    buf_align(&o->text, 16);
    sym[i] = obj_add_sym(o, nm, SYM_LOCAL_FUNC, 0, o->text.len);
    int src[3];
    bool fp[3];
    uint32_t ni = 0, nf = 0;
    for (uint32_t k = 0; k < t->nparams; k++) {
      fp[k] = vt_is_float(t->params[k]);
      src[k] = fp[k] ? (int)nf++ : IN_INT[ni++];
    }
    for (uint32_t k = t->nparams; k-- > 0;) {
      if (fp[k]) {
        if ((uint32_t)src[k] != k + 1) x64_vmovdqa_rr(&o->text, (int)k + 1, src[k]);
      } else if (src[k] != WIN_INT[k + 1]) {
        x64_mov_rr(&o->text, 1, WIN_INT[k + 1], src[k]);
      }
    }
    x64_load64(&o->text, X_R11, X_RCX, X64_NOIDX, 0, 0);
    x64_load64(&o->text, X_RCX, X_RCX, X64_NOIDX, 0, 8);
    x64_jmp_r(&o->text, X_R11);
  }
  buf_align(&o->data, 8);
  obj_add_sym(o, "warmc_lib_adapters", SYM_GLOBAL_DATA, 2, o->data.len);
  for (uint32_t i = 0; i < m->nimports; i++) {
    obj_add_reloc(o, REL_ABS64, 2, o->data.len, sym[i], 0);
    buf_u64(&o->data, 0);
  }
  if (!m->nimports) buf_u64(&o->data, 0);
  xfree(sym);
}

void shlib_emit(Shlib *p, ObjBuilder *o) {
  Module *m = p->m;
  bool x64 = g_target == &warmc_target_x64;
  for (uint32_t i = 0; i < m->nexports; i++) {
    if (!p->cname[i]) continue;
    buf_align(&o->text, x64 ? 16 : 4);
    obj_add_sym(o, p->cname[i], SYM_GLOBAL_FUNC, 0, o->text.len);
    if (x64) {
      /* mov r10d, i */
      buf_u8(&o->text, 0x41); buf_u8(&o->text, 0xba); buf_u32(&o->text, i);
    } else {
      a64_word(o, 0x52800009u | ((i & 0xffffu) << 5));          /* movz w9 */
      if (i > 0xffffu)
        a64_word(o, 0x72a00009u | ((i >> 16) << 5));           /* movk w9, lsl 16 */
    }
    emit_jump(o, "warmc_lib_thunk");
  }
  for (size_t h = 0; h < NHELPERS; h++) {
    buf_align(&o->text, x64 ? 16 : 4);
    obj_add_sym(o, p->helper[h], SYM_GLOBAL_FUNC, 0, o->text.len);
    emit_jump(o, HELPERS[h].rt);
  }
  if (x64) emit_adapters(p, o);
}

char **shlib_symbols(Shlib *p, uint32_t *n) {
  char **v = xmalloc((p->m->nexports + NHELPERS + 1) * sizeof(char *));
  uint32_t k = 0;
  for (uint32_t i = 0; i < p->m->nexports; i++)
    if (p->cname[i]) v[k++] = p->cname[i];
  for (size_t h = 0; h < NHELPERS; h++) v[k++] = p->helper[h];
  *n = k;
  return v;
}

static void write_signature(FILE *f, const char *ret, const char *name,
                            const CompType *t, bool ctx) {
  fprintf(f, "%s %s(", ret, name);
  uint32_t k = 0;
  if (ctx) { fputs("void *ctx", f); k++; }
  for (uint32_t i = 0; i < t->nparams; i++, k++)
    fprintf(f, "%s%s p%u", k ? ", " : "", c_type_name(t->params[i]), i);
  if (t->nresults > 1)
    for (uint32_t i = 0; i < t->nresults; i++, k++)
      fprintf(f, "%s%s *r%u", k ? ", " : "", c_type_name(t->results[i]), i);
  if (!k) fputs("void", f);
  fputs(")", f);
}

static const char *ret_type(const CompType *t) {
  return t->nresults == 1 ? c_type_name(t->results[0]) : "void";
}

void shlib_write_header(Shlib *p, const char *path, const char *libname) {
  Module *m = p->m;
  FILE *f = fopen(path, "w");
  if (!f) warmc_fatal("cannot write %s", path);

  char guard[256];
  size_t g = 0;
  for (const char *s = p->prefix; *s && g < sizeof guard - 3; s++)
    guard[g++] = isalnum((unsigned char)*s) ? (char)toupper((unsigned char)*s) : '_';
  guard[g] = 0;

  const char *hb = strrchr(path, '/'), *lb = strrchr(libname, '/');
  fprintf(f, "/* %s: the C interface of %s, generated by warmc --shared. */\n\n",
          hb ? hb + 1 : path, lb ? lb + 1 : libname);
  fprintf(f, "#ifndef %s_H\n#define %s_H\n\n", guard, guard);
  fputs("#include <stddef.h>\n#include <stdint.h>\n\n", f);
  fputs("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", f);

  fputs("/* The module's exports.  The first call makes the instance, with the\n"
        "   imports bound by then; calls from several threads take turns.  A\n"
        "   call that traps returns zero, and ", f);
  fprintf(f, "%s_last_trap() then says which\n   trap. */\n", p->prefix);
  for (uint32_t i = 0; i < m->nexports; i++) {
    if (!p->cname[i]) continue;
    const Export *ex = &m->exports[i];
    const CompType *t = &m->types[m->funcs[ex->index].type_index];
    if ((size_t)ex->name_len != strlen(p->cname[i]) ||
        memcmp(ex->name, p->cname[i], ex->name_len))
      fprintf(f, "/* export \"%.*s\" */\n", (int)ex->name_len, ex->name);
    write_signature(f, ret_type(t), p->cname[i], t, false);
    fputs(";\n", f);
  }

  bool any_import = false;
  for (uint32_t i = 0; i < m->nimports; i++) {
    const Import *im = &m->imports[i];
    const CompType *t = &m->types[im->type_index];
    if (!any_import) {
      fputs("\n/* The imports, each a function of the program loading the library,\n"
            "   bound before the first call with\n", f);
      fprintf(f, "   %s_bind_import(module, name, fn, ctx); fn is called with that ctx\n"
              "   first. */\n", p->prefix);
      any_import = true;
    }
    char tn[512];
    snprintf(tn, sizeof tn, "(*%s_import_%.*s_%.*s)", p->prefix,
             (int)im->module_len, im->module, (int)im->name_len, im->name);
    for (char *c = tn + 2; *c && *c != ')'; c++)
      if (!isalnum((unsigned char)*c) && *c != '_') *c = '_';
    fprintf(f, "/* \"%.*s\" \"%.*s\" */\ntypedef ", (int)im->module_len, im->module,
            (int)im->name_len, im->name);
    write_signature(f, ret_type(t), tn, t, true);
    fputs(";\n", f);
  }

  fputs("\n/* The library's own.  last_trap is the trap of the calling thread's\n"
        "   last call, 0 if it returned; memory is linear memory 0, which a call\n"
        "   that grows it may move; bind_import returns 0, 1 for an import the\n"
        "   module does not have, 2 once the first call has been made.\n"
        "   custom_section returns how many of the module's custom sections are\n"
        "   named name, as WebAssembly.Module.customSections does, and puts the\n"
        "   index-th one's payload in *data and *size, NULL and 0 when there\n"
        "   is none; it needs no instance, and the payload lives as long as the\n"
        "   library.\n"
        "   The name section and DWARF's .debug_* are there only if the library\n"
        "   was built with --custom-sections=all. */\n", f);
  for (size_t h = 0; h < NHELPERS; h++)
    fprintf(f, "%s%s%s%s;\n", HELPERS[h].ret,
            HELPERS[h].ret[strlen(HELPERS[h].ret) - 1] == '*' ? "" : " ",
            p->helper[h], HELPERS[h].proto);

  fputs("\n#ifdef __cplusplus\n}\n#endif\n\n", f);
  fprintf(f, "#endif\n");
  fclose(f);
}

/* Windows: the module-definition file a program linking against the DLL
   makes its import library from (`lib /def:`, `llvm-dlltool -d`). */
void shlib_write_def(Shlib *p, const char *path, const char *dllname) {
  FILE *f = fopen(path, "w");
  if (!f) warmc_fatal("cannot write %s", path);
  const char *b = strrchr(dllname, '/');
  fprintf(f, "LIBRARY %s\nEXPORTS\n", b ? b + 1 : dllname);
  uint32_t n;
  char **v = shlib_symbols(p, &n);
  for (uint32_t i = 0; i < n; i++) fprintf(f, "  %s\n", v[i]);
  xfree(v);
  fclose(f);
}

void shlib_free(Shlib *p) {
  for (uint32_t i = 0; i < p->m->nexports; i++) xfree(p->cname[i]);
  for (size_t h = 0; h < NHELPERS; h++) xfree(p->helper[h]);
  xfree(p->cname);
  xfree(p->helper);
  xfree(p->prefix);
  xfree(p);
}
