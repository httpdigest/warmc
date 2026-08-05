#include "wit.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum {
  T_EOF, T_ID, T_INT, T_STRING,
  T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN, T_LT, T_GT,
  T_COMMA, T_COLON, T_SEMI, T_EQ, T_ARROW, T_SLASH, T_DOT, T_AT, T_STAR,
  T_UNDERSCORE
} TokKind;

typedef struct {
  TokKind kind;
  const char *s;
  size_t n;
  uint32_t line, col;
  bool escaped;
} Tok;

typedef struct {
  const char *p, *end, *start;
  const char *path;
  uint32_t line, col;
  Tok tok;

  WitPackage *pkg;
  WitRegistry *reg;
  WitPackage *root;
} L;

static void wit_err(L *l, const char *what) {
  warmc_malformed("%s:%u:%u: %s", l->path ? l->path : "<wit>",
                  l->tok.line, l->tok.col, what);
}

static void advance_char(L *l) {
  if (*l->p == '\n') { l->line++; l->col = 1; }
  else l->col++;
  l->p++;
}

static bool id_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool id_cont(char c) {
  return id_start(c) || (c >= '0' && c <= '9') || c == '-';
}

static void skip_trivia(L *l) {
  for (;;) {
    if (l->p >= l->end) return;
    char c = *l->p;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance_char(l); continue; }
    if (c == '/' && l->p + 1 < l->end && l->p[1] == '/') {
      while (l->p < l->end && *l->p != '\n') advance_char(l);
      continue;
    }
    if (c == '/' && l->p + 1 < l->end && l->p[1] == '*') {

      uint32_t depth = 0;
      do {
        if (l->p + 1 < l->end && l->p[0] == '/' && l->p[1] == '*') {
          depth++; advance_char(l); advance_char(l);
        } else if (l->p + 1 < l->end && l->p[0] == '*' && l->p[1] == '/') {
          depth--; advance_char(l); advance_char(l);
        } else if (l->p < l->end) {
          advance_char(l);
        } else {
          warmc_malformed("%s: unterminated block comment",
                          l->path ? l->path : "<wit>");
        }
      } while (depth);
      continue;
    }
    return;
  }
}

static void next(L *l) {
  skip_trivia(l);
  Tok t;
  memset(&t, 0, sizeof t);
  t.line = l->line;
  t.col = l->col;
  if (l->p >= l->end) { t.kind = T_EOF; l->tok = t; return; }
  char c = *l->p;

  switch (c) {
    case '{': t.kind = T_LBRACE; goto one;
    case '}': t.kind = T_RBRACE; goto one;
    case '(': t.kind = T_LPAREN; goto one;
    case ')': t.kind = T_RPAREN; goto one;
    case '<': t.kind = T_LT; goto one;
    case '>': t.kind = T_GT; goto one;
    case ',': t.kind = T_COMMA; goto one;
    case ':': t.kind = T_COLON; goto one;
    case ';': t.kind = T_SEMI; goto one;
    case '=': t.kind = T_EQ; goto one;
    case '/': t.kind = T_SLASH; goto one;
    case '.': t.kind = T_DOT; goto one;
    case '@': t.kind = T_AT; goto one;
    case '*': t.kind = T_STAR; goto one;
    one:
      advance_char(l);
      l->tok = t;
      return;
    case '-':
      if (l->p + 1 < l->end && l->p[1] == '>') {
        t.kind = T_ARROW;
        advance_char(l); advance_char(l);
        l->tok = t;
        return;
      }
      break;
    case '_':

      t.kind = T_UNDERSCORE;
      advance_char(l);
      l->tok = t;
      return;
    case '"': {
      advance_char(l);
      t.kind = T_STRING;
      t.s = l->p;
      while (l->p < l->end && *l->p != '"') {
        if (*l->p == '\\' && l->p + 1 < l->end) advance_char(l);
        advance_char(l);
      }
      t.n = (size_t)(l->p - t.s);
      if (l->p >= l->end) { l->tok = t; wit_err(l, "unterminated string"); }
      advance_char(l);
      l->tok = t;
      return;
    }
    default: break;
  }

  if (c == '%') {
    advance_char(l);
    if (l->p >= l->end || !id_start(*l->p)) { l->tok = t; wit_err(l, "expected an identifier after %"); }
    t.escaped = true;
  }
  if (id_start(*l->p)) {
    t.kind = T_ID;
    t.s = l->p;
    while (l->p < l->end && id_cont(*l->p)) advance_char(l);
    t.n = (size_t)(l->p - t.s);
    l->tok = t;
    return;
  }
  if (*l->p >= '0' && *l->p <= '9') {

    t.kind = T_INT;
    t.s = l->p;
    for (;;) {
      if (l->p >= l->end) break;
      char d = *l->p;
      if ((d >= '0' && d <= '9') || id_start(d) || d == '-' || d == '+') {
        advance_char(l);
        continue;
      }
      if (d == '.' && l->p + 1 < l->end &&
          ((l->p[1] >= '0' && l->p[1] <= '9') || id_start(l->p[1]))) {
        advance_char(l);
        continue;
      }
      break;
    }
    t.n = (size_t)(l->p - t.s);
    l->tok = t;
    return;
  }
  l->tok = t;
  wit_err(l, "unexpected character");
}

static bool is_kw(const L *l, const char *kw) {

  return l->tok.kind == T_ID && !l->tok.escaped &&
         strlen(kw) == l->tok.n && !memcmp(l->tok.s, kw, l->tok.n);
}

static void expect(L *l, TokKind k, const char *what) {
  if (l->tok.kind != k) wit_err(l, what);
  next(l);
}

static const char *KEYWORDS[] = {
  "as", "async", "bool", "borrow", "char", "constructor", "enum", "export",
  "f32", "f64", "flags", "from", "func", "future", "import", "include",
  "interface", "list", "map", "option", "own", "package", "record",
  "resource", "result", "s16", "s32", "s64", "s8", "static", "stream",
  "string", "tuple", "type", "u16", "u32", "u64", "u8", "use", "variant",
  "with", "world",
};

static bool tok_is_keyword(const L *l) {
  if (l->tok.kind != T_ID || l->tok.escaped) return false;
  for (size_t i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++)
    if (strlen(KEYWORDS[i]) == l->tok.n &&
        !memcmp(l->tok.s, KEYWORDS[i], l->tok.n))
      return true;
  return false;
}

static char *take_id(L *l) {
  if (l->tok.kind != T_ID) wit_err(l, "expected an identifier");
  if (tok_is_keyword(l))
    wit_err(l, "expected an identifier, found a keyword (write %name to use one)");
  char *s = xstrndup(l->tok.s, l->tok.n);
  next(l);
  return s;
}

static void eat_kw(L *l, const char *kw, const char *what) {
  if (!is_kw(l, kw)) wit_err(l, what);
  next(l);
}

static uint32_t ty_new(WitPackage *p, WitTyKind k) {
  if (p->ntypes == p->cap_types) {
    p->cap_types = p->cap_types ? p->cap_types * 2 : 32;
    p->types = xrealloc(p->types, p->cap_types * sizeof(WitTy));
  }
  WitTy *t = &p->types[p->ntypes];
  memset(t, 0, sizeof *t);
  t->kind = k;
  t->elem = t->ok = t->err = t->key = t->def = WIT_NO_TYPE;
  return p->ntypes++;
}

static const struct { const char *name; WitTyKind kind; } PRIMS[] = {
  { "bool", WTY_BOOL }, { "u8", WTY_U8 }, { "u16", WTY_U16 },
  { "u32", WTY_U32 }, { "u64", WTY_U64 }, { "s8", WTY_S8 },
  { "s16", WTY_S16 }, { "s32", WTY_S32 }, { "s64", WTY_S64 },
  { "f32", WTY_F32 }, { "f64", WTY_F64 }, { "char", WTY_CHAR },
  { "string", WTY_STRING },
};

static uint32_t parse_ty(L *l);

static uint32_t parse_ty_args_one(L *l, WitTyKind k) {
  next(l);
  expect(l, T_LT, "expected < after a type constructor");
  uint32_t e = parse_ty(l);
  uint32_t id = ty_new(l->pkg, k);
  l->pkg->types[id].elem = e;
  expect(l, T_GT, "expected > to close a type constructor");
  return id;
}

static uint32_t parse_ty(L *l) {
  WitPackage *p = l->pkg;
  if (l->tok.kind != T_ID) wit_err(l, "expected a type");

  if (!l->tok.escaped) {
    for (size_t i = 0; i < sizeof PRIMS / sizeof PRIMS[0]; i++) {
      if (is_kw(l, PRIMS[i].name)) {
        next(l);
        return ty_new(p, PRIMS[i].kind);
      }
    }
    if (is_kw(l, "list")) {
      next(l);
      expect(l, T_LT, "expected < after list");
      uint32_t e = parse_ty(l);

      if (l->tok.kind == T_COMMA) {
        next(l);
        if (l->tok.kind != T_INT) wit_err(l, "expected a length in list<T, N>");
        uint32_t n = (uint32_t)strtoul(l->tok.s, NULL, 10);
        next(l);
        uint32_t id = ty_new(p, WTY_FIXED_LIST);
        p->types[id].elem = e;
        p->types[id].len = n;
        expect(l, T_GT, "expected > to close list");
        return id;
      }
      uint32_t id = ty_new(p, WTY_LIST);
      p->types[id].elem = e;
      expect(l, T_GT, "expected > to close list");
      return id;
    }
    if (is_kw(l, "option")) return parse_ty_args_one(l, WTY_OPTION);
    if (is_kw(l, "borrow")) return parse_ty_args_one(l, WTY_BORROW);
    if (is_kw(l, "own")) return parse_ty_args_one(l, WTY_OWN);
    if (is_kw(l, "tuple")) {
      next(l);
      expect(l, T_LT, "expected < after tuple");
      uint32_t cap = 4, n = 0;
      uint32_t *elems = xmalloc(cap * sizeof(uint32_t));
      for (;;) {
        if (n == cap) { cap *= 2; elems = xrealloc(elems, cap * sizeof(uint32_t)); }
        elems[n++] = parse_ty(l);
        if (l->tok.kind != T_COMMA) break;
        next(l);
        if (l->tok.kind == T_GT) break;
      }
      uint32_t id = ty_new(p, WTY_TUPLE);
      p->types[id].elems = elems;
      p->types[id].nelems = n;
      expect(l, T_GT, "expected > to close tuple");
      return id;
    }
    if (is_kw(l, "result")) {
      next(l);
      uint32_t id = ty_new(p, WTY_RESULT);
      if (l->tok.kind != T_LT) return id;
      next(l);
      uint32_t okv = WIT_NO_TYPE, errv = WIT_NO_TYPE;
      if (l->tok.kind == T_UNDERSCORE) next(l);
      else okv = parse_ty(l);
      if (l->tok.kind == T_COMMA) { next(l); errv = parse_ty(l); }
      p->types[id].ok = okv;
      p->types[id].err = errv;
      expect(l, T_GT, "expected > to close result");
      return id;
    }
    if (is_kw(l, "future") || is_kw(l, "stream")) {
      WitTyKind k = is_kw(l, "future") ? WTY_FUTURE : WTY_STREAM;
      next(l);
      uint32_t id = ty_new(p, k);
      if (l->tok.kind == T_LT) {
        next(l);
        p->types[id].elem = parse_ty(l);
        expect(l, T_GT, "expected > to close the type constructor");
      }
      return id;
    }
    if (is_kw(l, "map")) {
      next(l);
      expect(l, T_LT, "expected < after map");
      uint32_t k = parse_ty(l);
      expect(l, T_COMMA, "expected , in map<K, V>");
      uint32_t v = parse_ty(l);
      uint32_t id = ty_new(p, WTY_MAP);
      p->types[id].key = k;
      p->types[id].elem = v;
      expect(l, T_GT, "expected > to close map");
      return id;
    }
  }

  uint32_t id = ty_new(p, WTY_NAME);
  p->types[id].name = take_id(l);
  return id;
}

static void parse_params(L *l, WitFunc *f) {
  expect(l, T_LPAREN, "expected ( to open a parameter list");
  uint32_t cap = 0;
  while (l->tok.kind != T_RPAREN) {
    if (f->nparams == cap) {
      cap = cap ? cap * 2 : 4;
      f->params = xrealloc(f->params, cap * sizeof(WitNamed));
    }
    WitNamed *pa = &f->params[f->nparams++];
    memset(pa, 0, sizeof *pa);
    pa->name = take_id(l);
    expect(l, T_COLON, "expected : after a parameter name");
    pa->ty = parse_ty(l);
    if (l->tok.kind != T_COMMA) break;
    next(l);
  }
  expect(l, T_RPAREN, "expected ) to close a parameter list");
}

static void parse_func_type(L *l, WitFunc *f) {
  if (is_kw(l, "async")) { f->is_async = true; next(l); }
  eat_kw(l, "func", "expected func");
  parse_params(l, f);
  f->result = WIT_NO_TYPE;
  if (l->tok.kind == T_ARROW) { next(l); f->result = parse_ty(l); }
}

static char *parse_annotations(L *l) {
  char *external_id = NULL;
  while (l->tok.kind == T_AT) {
    next(l);
    bool ext = is_kw(l, "external-id");
    if (l->tok.kind != T_ID) wit_err(l, "expected an annotation name after @");
    next(l);
    if (l->tok.kind != T_LPAREN) continue;

    uint32_t depth = 0;
    const char *str = NULL;
    size_t strn = 0;
    do {
      if (l->tok.kind == T_LPAREN) depth++;
      else if (l->tok.kind == T_RPAREN) depth--;
      else if (l->tok.kind == T_STRING && ext && !str) {
        str = l->tok.s;
        strn = l->tok.n;
      } else if (l->tok.kind == T_EOF) wit_err(l, "unterminated annotation");
      next(l);
    } while (depth);
    if (ext && str) {
      xfree(external_id);
      external_id = xstrndup(str, strn);
    }
  }
  return external_id;
}

static void push_def(WitTypeDef **defs, uint32_t *n, uint32_t *cap, WitTypeDef d) {
  if (*n == *cap) {
    *cap = *cap ? *cap * 2 : 8;
    *defs = xrealloc(*defs, *cap * sizeof(WitTypeDef));
  }
  (*defs)[(*n)++] = d;
}

static void push_func(WitFunc **fs, uint32_t *n, uint32_t *cap, WitFunc f) {
  if (*n == *cap) {
    *cap = *cap ? *cap * 2 : 8;
    *fs = xrealloc(*fs, *cap * sizeof(WitFunc));
  }
  (*fs)[(*n)++] = f;
}

static char *parse_use_path_from(L *l, char *first) {
  char buf[512];
  size_t at = 0;
#define PUT(s, n) do { \
    if (at + (n) < sizeof buf) { memcpy(buf + at, (s), (n)); at += (n); } \
  } while (0)
  if (first) {
    PUT(first, strlen(first));
    xfree(first);
  } else {
    if (l->tok.kind != T_ID) wit_err(l, "expected a use path");
    PUT(l->tok.s, l->tok.n);
    next(l);
  }
  while (l->tok.kind == T_COLON) {
    PUT(":", 1); next(l);
    if (l->tok.kind != T_ID) wit_err(l, "expected a name after : in a use path");
    PUT(l->tok.s, l->tok.n); next(l);
  }
  while (l->tok.kind == T_SLASH) {
    PUT("/", 1); next(l);
    if (l->tok.kind != T_ID) wit_err(l, "expected a name after / in a use path");
    PUT(l->tok.s, l->tok.n); next(l);
  }
  if (l->tok.kind == T_AT) {
    PUT("@", 1); next(l);

    if (l->tok.kind != T_INT && l->tok.kind != T_ID)
      wit_err(l, "expected a version after @");
    PUT(l->tok.s, l->tok.n);
    next(l);
  }
#undef PUT
  return xstrndup(buf, at);
}

static char *parse_use_path(L *l) { return parse_use_path_from(l, NULL); }

static void parse_use(L *l, WitTypeDef **defs, uint32_t *ndefs, uint32_t *cap) {
  next(l);
  char *path = parse_use_path(l);
  expect(l, T_DOT, "expected . after a use path");
  expect(l, T_LBRACE, "expected { after a use path");
  while (l->tok.kind != T_RBRACE) {
    WitTypeDef d;
    memset(&d, 0, sizeof d);
    d.kind = WD_USE;
    d.alias = WIT_NO_TYPE;
    d.from_iface = d.from_def = WIT_NO_TYPE;
    d.path = xstrndup(path, strlen(path));
    d.orig = take_id(l);
    if (is_kw(l, "as")) { next(l); d.name = take_id(l); }
    else d.name = xstrndup(d.orig, strlen(d.orig));
    push_def(defs, ndefs, cap, d);
    if (l->tok.kind != T_COMMA) break;
    next(l);
  }
  expect(l, T_RBRACE, "expected } to close a use list");
  expect(l, T_SEMI, "expected ; after a use statement");
  xfree(path);
}

static bool parse_typedef(L *l, WitTypeDef **defs, uint32_t *ndefs,
                          uint32_t *cap, char *external_id);

static void parse_resource_body(L *l, WitTypeDef *d) {
  uint32_t fcap = 0;
  expect(l, T_LBRACE, "expected { to open a resource");
  while (l->tok.kind != T_RBRACE) {
    char *ext = parse_annotations(l);
    WitFunc f;
    memset(&f, 0, sizeof f);
    f.result = WIT_NO_TYPE;
    f.external_id = ext;
    if (is_kw(l, "constructor")) {
      next(l);
      f.kind = WF_CONSTRUCTOR;
      f.name = xstrndup("constructor", 11);
      parse_params(l, &f);
    } else {
      f.name = take_id(l);
      expect(l, T_COLON, "expected : after a method name");
      if (is_kw(l, "static")) { next(l); f.kind = WF_STATIC; }
      else f.kind = WF_METHOD;
      parse_func_type(l, &f);
    }
    expect(l, T_SEMI, "expected ; after a resource method");
    push_func(&d->methods, &d->nmethods, &fcap, f);
  }
  expect(l, T_RBRACE, "expected } to close a resource");
}

static bool parse_typedef(L *l, WitTypeDef **defs, uint32_t *ndefs,
                          uint32_t *cap, char *external_id) {
  WitTypeDef d;
  memset(&d, 0, sizeof d);
  d.alias = WIT_NO_TYPE;
  d.from_iface = d.from_def = WIT_NO_TYPE;
  d.external_id = external_id;

  if (is_kw(l, "type")) {
    next(l);
    d.kind = WD_ALIAS;
    d.name = take_id(l);
    expect(l, T_EQ, "expected = in a type alias");
    d.alias = parse_ty(l);
    expect(l, T_SEMI, "expected ; after a type alias");
  } else if (is_kw(l, "record")) {
    next(l);
    d.kind = WD_RECORD;
    d.name = take_id(l);
    expect(l, T_LBRACE, "expected { to open a record");
    uint32_t ccap = 0;
    while (l->tok.kind != T_RBRACE) {
      if (d.ncases == ccap) {
        ccap = ccap ? ccap * 2 : 4;
        d.cases = xrealloc(d.cases, ccap * sizeof(WitNamed));
      }
      WitNamed *f = &d.cases[d.ncases++];
      memset(f, 0, sizeof *f);
      f->name = take_id(l);
      expect(l, T_COLON, "expected : after a record field name");
      f->ty = parse_ty(l);
      if (l->tok.kind != T_COMMA) break;
      next(l);
    }
    expect(l, T_RBRACE, "expected } to close a record");
  } else if (is_kw(l, "variant")) {
    next(l);
    d.kind = WD_VARIANT;
    d.name = take_id(l);
    expect(l, T_LBRACE, "expected { to open a variant");
    uint32_t ccap = 0;
    while (l->tok.kind != T_RBRACE) {
      if (d.ncases == ccap) {
        ccap = ccap ? ccap * 2 : 4;
        d.cases = xrealloc(d.cases, ccap * sizeof(WitNamed));
      }
      WitNamed *c = &d.cases[d.ncases++];
      memset(c, 0, sizeof *c);
      c->name = take_id(l);
      c->ty = WIT_NO_TYPE;
      if (l->tok.kind == T_LPAREN) {
        next(l);
        c->ty = parse_ty(l);
        expect(l, T_RPAREN, "expected ) after a variant case payload");
      }
      if (l->tok.kind != T_COMMA) break;
      next(l);
    }
    expect(l, T_RBRACE, "expected } to close a variant");
  } else if (is_kw(l, "enum") || is_kw(l, "flags")) {
    d.kind = is_kw(l, "enum") ? WD_ENUM : WD_FLAGS;
    next(l);
    d.name = take_id(l);
    expect(l, T_LBRACE, "expected { to open an enum or flags");
    uint32_t lcap = 0;
    while (l->tok.kind != T_RBRACE) {
      if (d.nlabels == lcap) {
        lcap = lcap ? lcap * 2 : 4;
        d.labels = xrealloc(d.labels, lcap * sizeof(char *));
      }
      d.labels[d.nlabels++] = take_id(l);
      if (l->tok.kind != T_COMMA) break;
      next(l);
    }
    expect(l, T_RBRACE, "expected } to close an enum or flags");
  } else if (is_kw(l, "resource")) {
    next(l);
    d.kind = WD_RESOURCE;
    d.name = take_id(l);
    if (l->tok.kind == T_SEMI) next(l);
    else parse_resource_body(l, &d);
  } else {

    return false;
  }
  push_def(defs, ndefs, cap, d);
  return true;
}

static void parse_interface_body(L *l, WitInterface *in) {
  uint32_t dcap = 0, fcap = 0;
  expect(l, T_LBRACE, "expected { to open an interface");
  while (l->tok.kind != T_RBRACE) {
    if (l->tok.kind == T_EOF) wit_err(l, "unterminated interface");

    char *ext = parse_annotations(l);
    if (is_kw(l, "use")) {
      xfree(ext);
      parse_use(l, &in->defs, &in->ndefs, &dcap);
      continue;
    }
    if (parse_typedef(l, &in->defs, &in->ndefs, &dcap, ext)) continue;

    WitFunc f;
    memset(&f, 0, sizeof f);
    f.result = WIT_NO_TYPE;
    f.kind = WF_FREE;
    f.external_id = ext;
    f.name = take_id(l);
    expect(l, T_COLON, "expected : after a function name");
    parse_func_type(l, &f);
    expect(l, T_SEMI, "expected ; after a function");
    push_func(&in->funcs, &in->nfuncs, &fcap, f);
  }
  expect(l, T_RBRACE, "expected } to close an interface");
}

static uint32_t add_iface(WitPackage *p, WitInterface in) {
  p->ifaces = xrealloc(p->ifaces, (p->nifaces + 1) * sizeof(WitInterface));
  p->ifaces[p->nifaces] = in;
  return p->nifaces++;
}

static void parse_world_body(L *l, WitWorld *w) {
  uint32_t dcap = 0, icap = 0, inccap = 0;
  expect(l, T_LBRACE, "expected { to open a world");
  while (l->tok.kind != T_RBRACE) {
    if (l->tok.kind == T_EOF) wit_err(l, "unterminated world");
    char *ext = parse_annotations(l);
    if (is_kw(l, "use")) {
      xfree(ext);
      parse_use(l, &w->defs, &w->ndefs, &dcap);
      continue;
    }
    if (is_kw(l, "include")) {
      xfree(ext);
      next(l);
      char *path = parse_use_path(l);
      if (is_kw(l, "with")) {

        next(l);
        expect(l, T_LBRACE, "expected { after with");
        while (l->tok.kind != T_RBRACE) {
          xfree(take_id(l));
          eat_kw(l, "as", "expected as in an include rename");
          xfree(take_id(l));
          if (l->tok.kind != T_COMMA) break;
          next(l);
        }
        expect(l, T_RBRACE, "expected } to close an include rename list");
      } else {
        expect(l, T_SEMI, "expected ; after include");
      }
      if (w->nincludes == inccap) {
        inccap = inccap ? inccap * 2 : 4;
        w->includes = xrealloc(w->includes, inccap * sizeof(char *));
      }
      w->includes[w->nincludes++] = path;
      continue;
    }
    if (parse_typedef(l, &w->defs, &w->ndefs, &dcap, ext)) continue;

    bool is_import = is_kw(l, "import");
    if (!is_import && !is_kw(l, "export")) {
      xfree(ext);
      wit_err(l, "expected import, export, use, include or a type in a world");
    }
    next(l);

    WitWorldItem it;
    memset(&it, 0, sizeof it);
    it.dir = is_import ? WW_IMPORT : WW_EXPORT;
    it.iface = WIT_NO_TYPE;
    it.func.result = WIT_NO_TYPE;
    it.external_id = ext;

    char *first = take_id(l);
    bool named = false;
    if (l->tok.kind == T_COLON) {
      const char *save_p = l->p;
      uint32_t save_line = l->line, save_col = l->col;
      Tok save_tok = l->tok;
      next(l);
      named = is_kw(l, "func") || is_kw(l, "async") || is_kw(l, "interface");
      if (!named) {

        l->p = save_p;
        l->line = save_line;
        l->col = save_col;
        l->tok = save_tok;
      }
    }
    if (named) {
      it.name = first;
      if (is_kw(l, "interface")) {
        next(l);
        WitInterface inl;
        memset(&inl, 0, sizeof inl);
        inl.inline_in_world = true;
        inl.name = xstrndup(it.name, strlen(it.name));
        inl.ty_first = l->pkg->ntypes;
        parse_interface_body(l, &inl);
        inl.ty_last = l->pkg->ntypes;
        it.kind = WWI_INLINE;
        it.iface = add_iface(l->pkg, inl);
        if (l->tok.kind == T_SEMI) next(l);
      } else if (is_kw(l, "func") || is_kw(l, "async")) {
        it.kind = WWI_FUNC;
        it.func.name = xstrndup(it.name, strlen(it.name));
        parse_func_type(l, &it.func);
        expect(l, T_SEMI, "expected ; after a world function");
      } else {
        wit_err(l, "expected func, async or interface after a world item name");
      }
    } else {

      it.kind = WWI_PATH;
      it.name = parse_use_path_from(l, first);
      expect(l, T_SEMI, "expected ; after a world item");
    }

    if (w->nitems == icap) {
      icap = icap ? icap * 2 : 8;
      w->items = xrealloc(w->items, icap * sizeof(WitWorldItem));
    }
    w->items[w->nitems++] = it;
  }
  expect(l, T_RBRACE, "expected } to close a world");
}

static char *pkg_id(const char *ns, const char *name, const char *version) {
  size_t n = strlen(ns) + 1 + strlen(name) + (version ? 1 + strlen(version) : 0);
  char *s = xmalloc(n + 1);
  if (version) snprintf(s, n + 1, "%s:%s@%s", ns, name, version);
  else snprintf(s, n + 1, "%s:%s", ns, name);
  return s;
}

static WitPackage *reg_find(WitRegistry *r, const char *id) {
  for (uint32_t i = 0; i < r->npkgs; i++)
    if (r->pkgs[i]->id && !strcmp(r->pkgs[i]->id, id)) return r->pkgs[i];
  return NULL;
}

static WitPackage *reg_intern(WitRegistry *r, const char *ns, const char *name,
                              const char *version) {
  char *id = pkg_id(ns, name, version);
  WitPackage *p = reg_find(r, id);
  if (p) { xfree(id); return p; }
  p = xcalloc(1, sizeof *p);
  p->ns = xstrndup(ns, strlen(ns));
  p->name = xstrndup(name, strlen(name));
  p->version = version ? xstrndup(version, strlen(version)) : NULL;
  p->id = id;
  r->pkgs = xrealloc(r->pkgs, (r->npkgs + 1) * sizeof(WitPackage *));
  r->pkgs[r->npkgs++] = p;
  return p;
}

static uint32_t reg_slot(const WitRegistry *r, const WitPackage *p) {
  for (uint32_t i = 0; i < r->npkgs; i++) if (r->pkgs[i] == p) return i;
  return WIT_NO_TYPE;
}

static uint32_t find_def(const WitTypeDef *defs, uint32_t n, const char *name) {
  for (uint32_t i = 0; i < n; i++)
    if (!strcmp(defs[i].name, name)) return i;
  return WIT_NO_TYPE;
}

static uint32_t find_iface_in(const WitPackage *p, const char *name) {
  for (uint32_t i = 0; i < p->nifaces; i++)
    if (!p->ifaces[i].inline_in_world && !strcmp(p->ifaces[i].name, name))
      return i;
  return WIT_NO_TYPE;
}

static WitPackage *resolve_path_pkg(WitRegistry *r, WitPackage *self,
                                    const char *path, char *iface_out,
                                    size_t iface_cap, const char *where) {
  const char *slash = strchr(path, '/');
  if (!slash) {

    snprintf(iface_out, iface_cap, "%s", path);
    return self;
  }

  const char *at = strchr(slash + 1, '@');
  size_t iface_len = at ? (size_t)(at - (slash + 1)) : strlen(slash + 1);
  if (iface_len >= iface_cap) iface_len = iface_cap - 1;
  memcpy(iface_out, slash + 1, iface_len);
  iface_out[iface_len] = 0;

  char id[512];
  size_t pn = (size_t)(slash - path);
  if (pn >= sizeof id) pn = sizeof id - 1;
  memcpy(id, path, pn);
  id[pn] = 0;
  if (at) snprintf(id + pn, sizeof id - pn, "%s", at);

  WitPackage *found = reg_find(r, id);
  if (!found) {
    warmc_invalid("%s: no package `%s` in scope", where, id);
    return NULL;
  }
  return found;
}

static void resolve_scope(WitPackage *p, const WitTypeDef *defs, uint32_t ndefs,
                          uint32_t first_ty, uint32_t last_ty,
                          const char *where) {
  for (uint32_t i = first_ty; i < last_ty && i < p->ntypes; i++) {
    WitTy *t = &p->types[i];
    if (t->kind != WTY_NAME || t->def != WIT_NO_TYPE) continue;
    uint32_t d = find_def(defs, ndefs, t->name);
    if (d == WIT_NO_TYPE)
      warmc_invalid("%s: unknown type `%s`", where, t->name);
    t->def = d;
  }
}

static void resolve_iface_ref(WitRegistry *r, WitPackage *self,
                              const char *path, const char *where,
                              uint32_t *pkg_out, uint32_t *iface_out) {
  *pkg_out = *iface_out = WIT_NO_TYPE;
  const char *name = path;
  WitPackage *owner = self;

  char namebuf[256];
  if (!strchr(path, '/')) {

    uint32_t i = find_iface_in(self, path);
    if (i != WIT_NO_TYPE) {
      *pkg_out = reg_slot(r, self);
      *iface_out = i;
      return;
    }
    for (uint32_t k = 0; k < self->ntop_uses; k++) {
      const char *as = self->top_names[k];
      const char *up = self->top_uses[k];
      const char *tail = strrchr(up, '/');
      const char *bare = tail ? tail + 1 : up;
      char bbuf[256];
      const char *cut = strchr(bare, '@');
      if (cut) {
        size_t n = (size_t)(cut - bare);
        if (n >= sizeof bbuf) n = sizeof bbuf - 1;
        memcpy(bbuf, bare, n);
        bbuf[n] = 0;
        bare = bbuf;
      }
      if (!strcmp(as ? as : bare, path)) {
        owner = resolve_path_pkg(r, self, up, namebuf, sizeof namebuf, where);
        if (!owner) return;
        name = namebuf;
        goto have_owner;
      }
    }
    warmc_invalid("%s: no interface `%s` in scope", where, path);
    return;
  }

  owner = resolve_path_pkg(r, self, path, namebuf, sizeof namebuf, where);
  if (!owner) return;
  name = namebuf;

have_owner: {
    uint32_t i = find_iface_in(owner, name);
    if (i == WIT_NO_TYPE) {
      warmc_invalid("%s: package `%s` has no interface `%s`", where,
                    owner->id ? owner->id : "-", name);
      return;
    }
    *pkg_out = reg_slot(r, owner);
    *iface_out = i;
  }
}

static void resolve_registry(WitRegistry *r) {
  for (uint32_t pi = 0; pi < r->npkgs; pi++) {
    WitPackage *p = r->pkgs[pi];

    for (uint32_t i = 0; i < p->nifaces; i++) {
      WitInterface *in = &p->ifaces[i];
      for (uint32_t k = 0; k < in->ndefs; k++) {
        WitTypeDef *d = &in->defs[k];
        if (d->kind != WD_USE) continue;
        resolve_iface_ref(r, p, d->path, in->name, &d->from_pkg, &d->from_iface);
        if (d->from_iface == WIT_NO_TYPE) continue;
        WitPackage *src = r->pkgs[d->from_pkg];
        d->from_def = find_def(src->ifaces[d->from_iface].defs,
                               src->ifaces[d->from_iface].ndefs, d->orig);
        if (d->from_def == WIT_NO_TYPE)
          warmc_invalid("%s: `%s` has no type `%s`", in->name, d->path, d->orig);
      }
    }
    for (uint32_t i = 0; i < p->nworlds; i++) {
      WitWorld *w = &p->worlds[i];
      for (uint32_t k = 0; k < w->ndefs; k++) {
        WitTypeDef *d = &w->defs[k];
        if (d->kind != WD_USE) continue;
        resolve_iface_ref(r, p, d->path, w->name, &d->from_pkg, &d->from_iface);
        if (d->from_iface == WIT_NO_TYPE) continue;
        WitPackage *src = r->pkgs[d->from_pkg];
        d->from_def = find_def(src->ifaces[d->from_iface].defs,
                               src->ifaces[d->from_iface].ndefs, d->orig);
        if (d->from_def == WIT_NO_TYPE)
          warmc_invalid("%s: `%s` has no type `%s`", w->name, d->path, d->orig);
      }
    }

    for (uint32_t i = 0; i < p->nifaces; i++)
      resolve_scope(p, p->ifaces[i].defs, p->ifaces[i].ndefs,
                    p->ifaces[i].ty_first, p->ifaces[i].ty_last,
                    p->ifaces[i].name);
    for (uint32_t i = 0; i < p->nworlds; i++)
      resolve_scope(p, p->worlds[i].defs, p->worlds[i].ndefs,
                    p->worlds[i].ty_first, p->worlds[i].ty_last,
                    p->worlds[i].name);

    for (uint32_t i = 0; i < p->nworlds; i++) {
      WitWorld *w = &p->worlds[i];
      for (uint32_t k = 0; k < w->nitems; k++) {
        WitWorldItem *it = &w->items[k];
        if (it->kind != WWI_PATH) continue;
        uint32_t pk;
        resolve_iface_ref(r, p, it->name, w->name, &pk, &it->iface);
        it->pkg = pk;
      }
      for (uint32_t k = 0; k < w->nincludes; k++) {
        uint32_t pk, ifc;

        char tail[256];
        if (strchr(w->includes[k], '/')) {
          WitPackage *owner = resolve_path_pkg(r, p, w->includes[k], tail,
                                               sizeof tail, w->name);
          (void)owner;
        }
        (void)pk; (void)ifc;
      }
    }
  }
}

static void parse_document(WitRegistry *reg, const char *text, size_t len,
                           const char *path) {
  L l;
  memset(&l, 0, sizeof l);
  l.p = text;
  l.end = text + len;
  l.start = text;
  l.path = path;
  l.line = 1;
  l.col = 1;
  l.reg = reg;
  next(&l);

  uint32_t depth = 0;

  parse_annotations(&l);
  if (is_kw(&l, "package")) {
    next(&l);
    char *ns = take_id(&l);
    expect(&l, T_COLON, "expected : in a package name");
    char *nm = take_id(&l);
    while (l.tok.kind == T_COLON || l.tok.kind == T_SLASH) {

      next(&l);
      char *more = take_id(&l);
      xfree(more);
    }
    char *ver = NULL;
    if (l.tok.kind == T_AT) {
      next(&l);
      if (l.tok.kind != T_INT && l.tok.kind != T_ID)
        wit_err(&l, "expected a version after @");
      ver = xstrndup(l.tok.s, l.tok.n);
      next(&l);
    }
    expect(&l, T_SEMI, "expected ; after a package declaration");
    l.root = reg_intern(reg, ns, nm, ver);
    xfree(ns); xfree(nm); xfree(ver);
  } else {

    l.root = reg_intern(reg, "", "", NULL);
    xfree(l.root->id);
    l.root->id = NULL;
  }
  l.pkg = l.root;

  while (l.tok.kind != T_EOF) {
    char *ext = parse_annotations(&l);
    xfree(ext);

    if (is_kw(&l, "package")) {
      next(&l);
      char pbuf[256];
      size_t pat = 0;
      memset(pbuf, 0, sizeof pbuf);
#define PPUT(str, n) do { \
        if (pat + (n) < sizeof pbuf) { memcpy(pbuf + pat, (str), (n)); pat += (n); } \
      } while (0)
      {
        char *seg = take_id(&l);
        PPUT(seg, strlen(seg));
        xfree(seg);
      }
      while (l.tok.kind == T_COLON || l.tok.kind == T_SLASH) {
        PPUT(l.tok.kind == T_COLON ? ":" : "/", 1);
        next(&l);
        char *seg = take_id(&l);
        PPUT(seg, strlen(seg));
        xfree(seg);
      }
      if (l.tok.kind == T_AT) {
        PPUT("@", 1);
        next(&l);
        if (l.tok.kind != T_INT && l.tok.kind != T_ID)
          wit_err(&l, "expected a version after @");
        PPUT(l.tok.s, l.tok.n);
        next(&l);
      }
#undef PPUT
      if (l.tok.kind == T_LBRACE) {
        next(&l);
        depth++;

        char *colon = strchr(pbuf, ':');
        char *at = strchr(pbuf, '@');
        if (!colon) wit_err(&l, "a package name needs a namespace");
        *colon = 0;
        if (at) *at = 0;
        l.pkg = reg_intern(reg, pbuf, colon + 1, at ? at + 1 : NULL);
      } else {
        expect(&l, T_SEMI, "expected ; or { after a package declaration");
      }
      continue;
    }
    if (depth && l.tok.kind == T_RBRACE) {
      next(&l);
      depth--;
      if (!depth) l.pkg = l.root;
      continue;
    }
    if (is_kw(&l, "use")) {

      next(&l);
      char *up = parse_use_path(&l);
      char *as = NULL;
      if (is_kw(&l, "as")) { next(&l); as = take_id(&l); }
      expect(&l, T_SEMI, "expected ; after a top-level use");
      WitPackage *own = l.pkg;
      own->top_uses = xrealloc(own->top_uses,
                               (own->ntop_uses + 1) * sizeof(char *));
      own->top_names = xrealloc(own->top_names,
                                (own->ntop_uses + 1) * sizeof(char *));
      own->top_uses[own->ntop_uses] = up;
      own->top_names[own->ntop_uses] = as;
      own->ntop_uses++;
      continue;
    }
    if (is_kw(&l, "interface")) {
      next(&l);
      WitInterface in;
      memset(&in, 0, sizeof in);
      in.name = take_id(&l);
      in.ty_first = l.pkg->ntypes;
      parse_interface_body(&l, &in);
      in.ty_last = l.pkg->ntypes;
      add_iface(l.pkg, in);
      continue;
    }
    if (is_kw(&l, "world")) {
      next(&l);
      WitWorld w;
      memset(&w, 0, sizeof w);
      w.name = take_id(&l);
      uint32_t first = l.pkg->ntypes;
      parse_world_body(&l, &w);
      w.ty_first = first;
      w.ty_last = l.pkg->ntypes;
      WitPackage *wp = l.pkg;
      wp->worlds = xrealloc(wp->worlds, (wp->nworlds + 1) * sizeof(WitWorld));
      wp->worlds[wp->nworlds++] = w;
      continue;
    }
    wit_err(&l, "expected package, use, interface or world");
  }
  if (depth) wit_err(&l, "unterminated nested package definition");
}

static char *read_whole(const char *path, size_t *len_out) {
  FILE *f = fopen(path, "rb");
  if (!f) warmc_fatal("cannot open %s", path);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) warmc_fatal("cannot size %s", path);
  char *buf = xmalloc((size_t)n + 1);
  if (n && fread(buf, 1, (size_t)n, f) != (size_t)n)
    warmc_fatal("short read on %s", path);
  buf[n] = 0;
  fclose(f);
  *len_out = (size_t)n;
  return buf;
}

static bool is_dir(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void parse_file(WitRegistry *r, const char *path) {
  size_t len;
  char *text = read_whole(path, &len);
  parse_document(r, text, len, path);
  xfree(text);
}

static void parse_dir_files(WitRegistry *r, const char *dir) {
  DIR *d = opendir(dir);
  if (!d) warmc_fatal("cannot open directory %s", dir);
  char **names = NULL;
  uint32_t n = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    size_t ln = strlen(e->d_name);
    if (ln < 5 || strcmp(e->d_name + ln - 4, ".wit")) continue;
    names = xrealloc(names, (n + 1) * sizeof(char *));
    names[n++] = xstrndup(e->d_name, ln);
  }
  closedir(d);
  for (uint32_t i = 0; i < n; i++)
    for (uint32_t k = i + 1; k < n; k++)
      if (strcmp(names[i], names[k]) > 0) {
        char *t = names[i]; names[i] = names[k]; names[k] = t;
      }
  for (uint32_t i = 0; i < n; i++) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s/%s", dir, names[i]);
    parse_file(r, buf);
    xfree(names[i]);
  }
  xfree(names);
}

WitRegistry *wit_load(const char *path) {
  WitRegistry *r = xcalloc(1, sizeof *r);

  if (!is_dir(path)) {

    parse_file(r, path);
  } else {

    char deps[1024];
    snprintf(deps, sizeof deps, "%s/deps", path);
    if (is_dir(deps)) {
      DIR *d = opendir(deps);
      struct dirent *e;
      char **entries = NULL;
      uint32_t n = 0;
      while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        entries = xrealloc(entries, (n + 1) * sizeof(char *));
        entries[n++] = xstrndup(e->d_name, strlen(e->d_name));
      }
      if (d) closedir(d);
      for (uint32_t i = 0; i < n; i++)
        for (uint32_t k = i + 1; k < n; k++)
          if (strcmp(entries[i], entries[k]) > 0) {
            char *t = entries[i]; entries[i] = entries[k]; entries[k] = t;
          }
      for (uint32_t i = 0; i < n; i++) {
        char buf[1024];
        snprintf(buf, sizeof buf, "%s/%s", deps, entries[i]);
        if (is_dir(buf)) parse_dir_files(r, buf);
        else {
          size_t ln = strlen(entries[i]);
          if (ln >= 5 && !strcmp(entries[i] + ln - 4, ".wit")) parse_file(r, buf);
        }
        xfree(entries[i]);
      }
      xfree(entries);
    }
    uint32_t before = r->npkgs;
    parse_dir_files(r, path);

    r->root = before < r->npkgs ? before : 0;
  }

  resolve_registry(r);
  return r;
}

static const char *PRIM_NAME[] = {
  "bool", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64",
  "f32", "f64", "char", "string"
};

static void dump_ty(const WitPackage *p, uint32_t id, FILE *out) {
  if (id == WIT_NO_TYPE) { fputs("_", out); return; }
  const WitTy *t = &p->types[id];
  if (t->kind <= WTY_STRING) { fputs(PRIM_NAME[t->kind], out); return; }
  switch (t->kind) {
    case WTY_TUPLE:
      fputs("tuple<", out);
      for (uint32_t i = 0; i < t->nelems; i++) {
        if (i) fputs(", ", out);
        dump_ty(p, t->elems[i], out);
      }
      fputs(">", out);
      break;
    case WTY_LIST: fputs("list<", out); dump_ty(p, t->elem, out); fputs(">", out); break;
    case WTY_FIXED_LIST:
      fputs("list<", out); dump_ty(p, t->elem, out);
      fprintf(out, ", %u>", t->len);
      break;
    case WTY_OPTION: fputs("option<", out); dump_ty(p, t->elem, out); fputs(">", out); break;
    case WTY_BORROW: fputs("borrow<", out); dump_ty(p, t->elem, out); fputs(">", out); break;
    case WTY_OWN: fputs("own<", out); dump_ty(p, t->elem, out); fputs(">", out); break;
    case WTY_MAP:
      fputs("map<", out); dump_ty(p, t->key, out); fputs(", ", out);
      dump_ty(p, t->elem, out); fputs(">", out);
      break;
    case WTY_FUTURE: case WTY_STREAM:
      fputs(t->kind == WTY_FUTURE ? "future" : "stream", out);
      if (t->elem != WIT_NO_TYPE) { fputs("<", out); dump_ty(p, t->elem, out); fputs(">", out); }
      break;
    case WTY_RESULT:
      fputs("result", out);
      if (t->ok != WIT_NO_TYPE || t->err != WIT_NO_TYPE) {
        fputs("<", out);
        dump_ty(p, t->ok, out);
        if (t->err != WIT_NO_TYPE) { fputs(", ", out); dump_ty(p, t->err, out); }
        fputs(">", out);
      }
      break;
    case WTY_NAME:
      fprintf(out, "%s#%u", t->name, t->def);
      break;
    default: fputs("?", out); break;
  }
}

static void dump_func_named(const WitPackage *p, const WitFunc *f,
                            const char *owner, const char *ind, FILE *out) {
  fputs(ind, out);
  fputs("func ", out);
  if (f->is_async) fputs("async ", out);
  switch (f->kind) {
    case WF_METHOD: fprintf(out, "[method]%s.%s", owner, f->name); break;
    case WF_STATIC: fprintf(out, "[static]%s.%s", owner, f->name); break;
    case WF_CONSTRUCTOR: fprintf(out, "[constructor]%s", owner); break;
    default: fputs(f->name, out); break;
  }
  fputs("(", out);
  for (uint32_t i = 0; i < f->nparams; i++) {
    if (i) fputs(", ", out);
    fprintf(out, "%s: ", f->params[i].name);
    dump_ty(p, f->params[i].ty, out);
  }
  fputs(")", out);
  if (f->result != WIT_NO_TYPE) { fputs(" -> ", out); dump_ty(p, f->result, out); }
  if (f->external_id) fprintf(out, " @external-id(%s)", f->external_id);
  fputs("\n", out);
}

static void dump_func(const WitPackage *p, const WitFunc *f, const char *ind,
                      FILE *out) {
  dump_func_named(p, f, "", ind, out);
}

static void dump_defs(const WitPackage *p, const WitTypeDef *defs, uint32_t n,
                      const char *ind, FILE *out) {
  static const char *KIND[] = { "type", "record", "variant", "enum", "flags",
                                "resource", "use" };
  for (uint32_t i = 0; i < n; i++) {
    const WitTypeDef *d = &defs[i];
    fprintf(out, "%s%s %s", ind, KIND[d->kind], d->name);
    switch (d->kind) {
      case WD_ALIAS: fputs(" = ", out); dump_ty(p, d->alias, out); break;
      case WD_RECORD: case WD_VARIANT:
        fputs(" {", out);
        for (uint32_t k = 0; k < d->ncases; k++) {
          if (k) fputs(",", out);
          fprintf(out, " %s", d->cases[k].name);
          if (d->cases[k].ty != WIT_NO_TYPE) {
            fputs(": ", out);
            dump_ty(p, d->cases[k].ty, out);
          }
        }
        fputs(" }", out);
        break;
      case WD_ENUM: case WD_FLAGS:
        fputs(" {", out);
        for (uint32_t k = 0; k < d->nlabels; k++)
          fprintf(out, "%s %s", k ? "," : "", d->labels[k]);
        fputs(" }", out);
        break;
      case WD_USE:
        fprintf(out, " from %s.%s (pkg %u iface %u def %u)", d->path, d->orig,
                d->from_pkg, d->from_iface, d->from_def);
        break;
      default: break;
    }
    fputs("\n", out);
    if (d->kind == WD_RESOURCE) {
      char sub[64];
      snprintf(sub, sizeof sub, "%s  ", ind);
      for (uint32_t k = 0; k < d->nmethods; k++)
        dump_func_named(p, &d->methods[k], d->name, sub, out);
    }
  }
}

static void dump_package(const WitPackage *p, FILE *out) {
  const char *self = p->id ? p->id : "-";
  fprintf(out, "package %s\n", self);
  for (uint32_t i = 0; i < p->ntop_uses; i++)
    fprintf(out, "use %s%s%s\n", p->top_uses[i],
            p->top_names[i] ? " as " : "", p->top_names[i] ? p->top_names[i] : "");
  for (uint32_t i = 0; i < p->nifaces; i++) {
    fprintf(out, "interface%s %u %s [%s]\n",
            p->ifaces[i].inline_in_world ? "-inline" : "",
            i, p->ifaces[i].name, self);
    dump_defs(p, p->ifaces[i].defs, p->ifaces[i].ndefs, "  ", out);
    for (uint32_t k = 0; k < p->ifaces[i].nfuncs; k++)
      dump_func(p, &p->ifaces[i].funcs[k], "  ", out);
  }
  for (uint32_t i = 0; i < p->nworlds; i++) {
    const WitWorld *w = &p->worlds[i];
    fprintf(out, "world %u %s [%s]\n", i, w->name, self);
    dump_defs(p, w->defs, w->ndefs, "  ", out);
    for (uint32_t k = 0; k < w->nincludes; k++)
      fprintf(out, "  include %s\n", w->includes[k]);
    for (uint32_t k = 0; k < w->nitems; k++) {
      const WitWorldItem *it = &w->items[k];
      const char *dir = it->dir == WW_IMPORT ? "import" : "export";
      switch (it->kind) {
        case WWI_PATH:
          fprintf(out, "  %s path %s (pkg %u iface %u)\n", dir, it->name,
                  it->pkg, it->iface);
          break;
        case WWI_INLINE:
          fprintf(out, "  %s inline %s (iface %u)\n", dir, it->name, it->iface);
          break;
        default: {
          char ind[32];
          snprintf(ind, sizeof ind, "  %s ", dir);
          dump_func(p, &it->func, ind, out);
          break;
        }
      }
    }
  }
}

void wit_registry_dump(const WitRegistry *r, FILE *out) {

  if (r->root < r->npkgs) dump_package(r->pkgs[r->root], out);
  for (uint32_t i = 0; i < r->npkgs; i++)
    if (i != r->root) dump_package(r->pkgs[i], out);
}

static void free_func(WitFunc *f) {
  xfree(f->name);
  for (uint32_t i = 0; i < f->nparams; i++) xfree(f->params[i].name);
  xfree(f->params);
  xfree(f->external_id);
}

static void free_defs(WitTypeDef *defs, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    WitTypeDef *d = &defs[i];
    xfree(d->name);
    for (uint32_t k = 0; k < d->ncases; k++) xfree(d->cases[k].name);
    xfree(d->cases);
    for (uint32_t k = 0; k < d->nlabels; k++) xfree(d->labels[k]);
    xfree(d->labels);
    for (uint32_t k = 0; k < d->nmethods; k++) free_func(&d->methods[k]);
    xfree(d->methods);
    xfree(d->path);
    xfree(d->orig);
    xfree(d->external_id);
  }
  xfree(defs);
}

static void wit_free(WitPackage *p) {
  if (!p) return;
  for (uint32_t i = 0; i < p->ntypes; i++) {
    xfree(p->types[i].elems);
    xfree(p->types[i].name);
  }
  xfree(p->types);
  for (uint32_t i = 0; i < p->nifaces; i++) {
    xfree(p->ifaces[i].name);
    free_defs(p->ifaces[i].defs, p->ifaces[i].ndefs);
    for (uint32_t k = 0; k < p->ifaces[i].nfuncs; k++)
      free_func(&p->ifaces[i].funcs[k]);
    xfree(p->ifaces[i].funcs);
  }
  xfree(p->ifaces);
  for (uint32_t i = 0; i < p->nworlds; i++) {
    WitWorld *w = &p->worlds[i];
    xfree(w->name);
    free_defs(w->defs, w->ndefs);
    for (uint32_t k = 0; k < w->nitems; k++) {
      xfree(w->items[k].name);
      xfree(w->items[k].external_id);
      if (w->items[k].kind == WWI_FUNC) free_func(&w->items[k].func);
    }
    xfree(w->items);
    for (uint32_t k = 0; k < w->nincludes; k++) xfree(w->includes[k]);
    xfree(w->includes);
  }
  xfree(p->worlds);
  for (uint32_t i = 0; i < p->ntop_uses; i++) {
    xfree(p->top_uses[i]);
    xfree(p->top_names[i]);
  }
  xfree(p->top_uses);
  xfree(p->top_names);
  xfree(p->ns); xfree(p->name); xfree(p->version); xfree(p->id);
  xfree(p);
}

void wit_registry_free(WitRegistry *r) {
  if (!r) return;
  for (uint32_t i = 0; i < r->npkgs; i++) wit_free(r->pkgs[i]);
  xfree(r->pkgs);
  xfree(r);
}
