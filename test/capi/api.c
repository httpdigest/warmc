/* The C API's own tests: what the upstream examples (test/capi/run.py
 * fetches them) do not reach.  Each check prints nothing unless it fails;
 * the program ends saying how many ran, and exits 1 if any failed. */

#include "wasm.h"
#include "warm.h"

#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int nchecks, nfails;

#define CHECK(c) check_((c), #c, __FILE__, __LINE__)
static void check_(int ok, const char *what, const char *file, int line) {
  nchecks++;
  if (ok) return;
  nfails++;
  printf("FAIL %s:%d: %s\n", file, line, what);
}

static wasm_engine_t *engine;

static wasm_module_t *module_err(wasm_store_t *s, const char *wat, char *err, size_t n) {
  wasm_byte_vec_t b;
  wasm_message_t msg;
  if (!warm_wat2wasm(wat, strlen(wat), &b, &msg)) {
    snprintf(err, n, "wat: %s", msg.data);
    wasm_byte_vec_delete(&msg);
    return NULL;
  }
  wasm_module_t *m = warm_module_new(s, &b, &msg);
  wasm_byte_vec_delete(&b);
  snprintf(err, n, "%s", m ? "" : msg.data);
  wasm_byte_vec_delete(&msg);
  return m;
}

static wasm_module_t *module(wasm_store_t *s, const char *wat) {
  char err[512];
  wasm_module_t *m = module_err(s, wat, err, sizeof err);
  if (!m) printf("FAIL: cannot compile: %s\n%s\n", err, wat);
  CHECK(m != NULL);
  return m;
}

static wasm_instance_t *instance(wasm_store_t *s, wasm_module_t *m,
                                 wasm_extern_t **imports, size_t n) {
  if (!m) return NULL;
  wasm_extern_vec_t v = { n, imports };
  wasm_trap_t *trap = NULL;
  wasm_message_t err;
  wasm_instance_t *in = warm_instance_new(s, m, &v, &trap, &err);
  if (!in) printf("FAIL: cannot instantiate: %s\n", err.data);
  CHECK(in != NULL);
  wasm_byte_vec_delete(&err);
  return in;
}

/* export i of an instance (the vector is leaked: the store holds them) */
static wasm_extern_t *export(wasm_instance_t *in, size_t i) {
  if (!in) return NULL;
  wasm_extern_vec_t ex;
  wasm_instance_exports(in, &ex);
  CHECK(i < ex.size);
  return i < ex.size ? ex.data[i] : NULL;
}

static wasm_func_t *func(wasm_instance_t *in, size_t i) {
  wasm_extern_t *e = export(in, i);
  return e ? wasm_extern_as_func(e) : NULL;
}

static char *trap_msg(wasm_trap_t *t) {
  static char buf[256];
  wasm_message_t m;
  wasm_trap_message(t, &m);
  snprintf(buf, sizeof buf, "%s", m.data);
  wasm_byte_vec_delete(&m);
  return buf;
}

/* ---- compiling and its errors -------------------------------------------- */

static void test_errors(void) {
  wasm_store_t *s = wasm_store_new(engine);
  char err[512];

  wasm_byte_vec_t junk = { 4, (char *)"\0asX" };
  wasm_message_t msg;
  CHECK(!warm_module_new(s, &junk, &msg));
  CHECK(strstr(msg.data, "malformed") != NULL);
  wasm_byte_vec_delete(&msg);
  CHECK(!wasm_module_validate(s, &junk));

  CHECK(!module_err(s, "(module (func (result i32) (i64.const 1)))", err, sizeof err));
  CHECK(strstr(err, "invalid") != NULL);

  wasm_byte_vec_t out;
  CHECK(!warm_wat2wasm("(module (func (bogus)))", 22, &out, &msg));
  CHECK(msg.size > 1);
  wasm_byte_vec_delete(&msg);

  /* a valid module validates */
  CHECK(warm_wat2wasm("(module)", 8, &out, &msg));
  CHECK(wasm_module_validate(s, &out));
  wasm_byte_vec_delete(&out);
  wasm_byte_vec_delete(&msg);

  /* an option the compiler does not know fails every compile, saying so */
  wasm_config_t *c = wasm_config_new();
  warm_config_flag_add(c, "-fno-such-pass");
  wasm_engine_t *e2 = wasm_engine_new_with_config(c);
  wasm_store_t *s2 = wasm_store_new(e2);
  CHECK(!module_err(s2, "(module)", err, sizeof err));
  CHECK(strstr(err, "such-pass") != NULL);
  wasm_store_delete(s2);
  wasm_engine_delete(e2);

  c = wasm_config_new();
  CHECK(!warm_config_opt_level_set(c, 4));
  CHECK(warm_config_opt_level_set(c, 0));
  wasm_config_delete(c);

  /* one compile thread or several, the same module, the same answer */
  static const char wat[] =
    "(module (func $f (param i32) (result i32) (i32.mul (local.get 0) (i32.const 3)))"
    " (func $g (param i32) (result i32) (call $f (i32.add (local.get 0) (i32.const 1))))"
    " (func $h (param i32) (result i32) (call $g (call $f (local.get 0))))"
    " (func (export \"r\") (param i32) (result i32) (call $h (call $g (local.get 0)))))";
  for (uint32_t t = 1; t <= 3; t++) {
    c = wasm_config_new();
    warm_config_compile_threads_set(c, t);
    wasm_engine_t *e3 = wasm_engine_new_with_config(c);
    wasm_store_t *s3 = wasm_store_new(e3);
    wasm_module_t *m = module(s3, wat);
    wasm_extern_vec_t none = WASM_EMPTY_VEC;
    wasm_instance_t *in = m ? wasm_instance_new(s3, m, &none, NULL) : NULL;
    wasm_extern_vec_t ex;
    if (in) wasm_instance_exports(in, &ex);
    wasm_val_t a = WASM_I32_VAL(5), r;
    wasm_val_vec_t av = { 1, &a }, rv = { 1, &r };
    CHECK(in && !wasm_func_call(wasm_extern_as_func(ex.data[0]), &av, &rv) &&
          r.of.i32 == ((5 + 1) * 3 * 3 + 1) * 3);
    if (in) wasm_extern_vec_delete(&ex);
    if (m) wasm_module_delete(m);
    wasm_store_delete(s3);
    wasm_engine_delete(e3);
  }
  wasm_store_delete(s);
}

/* ---- types, as the module says them --------------------------------------- */

static void test_reflect(void) {
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = module(s,
    "(module"
    "  (import \"a\" \"f\" (func (param i32 f64) (result externref)))"
    "  (import \"a\" \"g\" (global (mut i64)))"
    "  (import \"a\" \"t\" (table 2 9 funcref))"
    "  (import \"a\" \"m\" (memory 1 3))"
    "  (import \"a\" \"e\" (tag (param i32)))"
    "  (func (export \"v\") (param v128 anyref) (result f32) (f32.const 0))"
    "  (global (export \"c\") f64 (f64.const 1))"
    "  (table (export \"u\") 4 externref)"
    "  (memory (export \"n\") 2)"
    "  (tag (export \"x\")))");
  wasm_importtype_vec_t im;
  wasm_module_imports(m, &im);
  CHECK(im.size == 5);
  const wasm_functype_t *ft = wasm_externtype_as_functype_const(wasm_importtype_type(im.data[0]));
  CHECK(ft && wasm_functype_params(ft)->size == 2 && wasm_functype_results(ft)->size == 1);
  CHECK(wasm_valtype_kind(wasm_functype_params(ft)->data[1]) == WASM_F64);
  CHECK(wasm_valtype_kind(wasm_functype_results(ft)->data[0]) == WASM_EXTERNREF);
  const wasm_name_t *mn = wasm_importtype_module(im.data[0]);
  CHECK(mn->size == 1 && mn->data[0] == 'a');
  const wasm_globaltype_t *gt = wasm_externtype_as_globaltype_const(wasm_importtype_type(im.data[1]));
  CHECK(gt && wasm_globaltype_mutability(gt) == WASM_VAR &&
        wasm_valtype_kind(wasm_globaltype_content(gt)) == WASM_I64);
  const wasm_tabletype_t *tt = wasm_externtype_as_tabletype_const(wasm_importtype_type(im.data[2]));
  CHECK(tt && wasm_tabletype_limits(tt)->min == 2 && wasm_tabletype_limits(tt)->max == 9 &&
        wasm_valtype_kind(wasm_tabletype_element(tt)) == WASM_FUNCREF);
  const wasm_memorytype_t *mt = wasm_externtype_as_memorytype_const(wasm_importtype_type(im.data[3]));
  CHECK(mt && wasm_memorytype_limits(mt)->min == 1 && wasm_memorytype_limits(mt)->max == 3);
  const wasm_tagtype_t *tg = wasm_externtype_as_tagtype_const(wasm_importtype_type(im.data[4]));
  CHECK(tg && wasm_functype_params(wasm_tagtype_functype(tg))->size == 1);
  wasm_importtype_vec_delete(&im);

  wasm_exporttype_vec_t ex;
  wasm_module_exports(m, &ex);
  CHECK(ex.size == 5);
  ft = wasm_externtype_as_functype_const(wasm_exporttype_type(ex.data[0]));
  CHECK(ft && wasm_valtype_kind(wasm_functype_params(ft)->data[0]) == WARM_V128);
  CHECK(ft && wasm_valtype_kind(wasm_functype_params(ft)->data[1]) == WARM_ANYREF);
  CHECK(wasm_externtype_kind(wasm_exporttype_type(ex.data[1])) == WASM_EXTERN_GLOBAL);
  tt = wasm_externtype_as_tabletype_const(wasm_exporttype_type(ex.data[2]));
  CHECK(tt && wasm_tabletype_limits(tt)->max == wasm_limits_max_default);
  CHECK(wasm_externtype_kind(wasm_exporttype_type(ex.data[4])) == WASM_EXTERN_TAG);
  wasm_exporttype_vec_t copy;
  wasm_exporttype_vec_copy(&copy, &ex);
  CHECK(copy.size == ex.size);
  wasm_exporttype_vec_delete(&copy);
  wasm_exporttype_vec_delete(&ex);
  wasm_module_delete(m);
  wasm_store_delete(s);
}

/* ---- calls with more than registers carry ----------------------------------- */

static void test_many(void) {
  wasm_store_t *s = wasm_store_new(engine);
  /* 12 i64 and 12 f64 parameters, each added in with a weight that tells
     them apart; 16 results, the most an export returns, each a parameter */
  char wat[8192];
  size_t n = (size_t)snprintf(wat, sizeof wat, "(module (func (export \"sum\") (param");
  for (int i = 0; i < 12; i++) n += (size_t)snprintf(wat + n, sizeof wat - n, " i64 f64");
  n += (size_t)snprintf(wat + n, sizeof wat - n, ") (result f64) (local $s f64)");
  for (int i = 0; i < 12; i++)
    n += (size_t)snprintf(wat + n, sizeof wat - n,
                          " (local.set $s (f64.add (local.get $s) (f64.mul (f64.add "
                          "(f64.convert_i64_s (local.get %d)) (local.get %d)) (f64.const %d))))",
                          2 * i, 2 * i + 1, i + 1);
  n += (size_t)snprintf(wat + n, sizeof wat - n, " (local.get $s))");
  n += (size_t)snprintf(wat + n, sizeof wat - n, " (func (export \"many\") (param i32) (result");
  for (int i = 0; i < 16; i++) n += (size_t)snprintf(wat + n, sizeof wat - n, " %s", i % 2 ? "f32" : "i32");
  n += (size_t)snprintf(wat + n, sizeof wat - n, ")");
  for (int i = 0; i < 16; i++)
    n += (size_t)snprintf(wat + n, sizeof wat - n, i % 2 ? " (f32.convert_i32_s (i32.add (local.get 0) (i32.const %d)))"
                                                           : " (i32.add (local.get 0) (i32.const %d))", i);
  n += (size_t)snprintf(wat + n, sizeof wat - n, ")"
       " (func (export \"vec\") (param v128 i32 v128) (result v128 i32)"
       "   (i32x4.add (local.get 0) (local.get 2)) (local.get 1)))");
  wasm_module_t *m = module(s, wat);
  wasm_instance_t *in = instance(s, m, NULL, 0);

  wasm_val_t a[24], r[1];
  double want = 0;
  for (int i = 0; i < 12; i++) {
    a[2 * i].kind = WASM_I64;
    a[2 * i].of.i64 = 1000 * (i + 1);
    a[2 * i + 1].kind = WASM_F64;
    a[2 * i + 1].of.f64 = 0.5 * (i + 1);
    want += (1000.0 * (i + 1) + 0.5 * (i + 1)) * (i + 1);
  }
  wasm_val_vec_t av = { 24, a }, rv = { 1, r };
  wasm_trap_t *t = wasm_func_call(func(in, 0), &av, &rv);
  CHECK(!t && r[0].kind == WASM_F64 && r[0].of.f64 == want);

  wasm_val_t one = WASM_I32_VAL(100), res[16];
  wasm_val_vec_t ov = { 1, &one }, resv = { 16, res };
  t = wasm_func_call(func(in, 1), &ov, &resv);
  CHECK(!t);
  for (int i = 0; i < 16 && !t; i++)
    CHECK(i % 2 ? res[i].kind == WASM_F32 && res[i].of.f32 == 100.0f + i
                : res[i].kind == WASM_I32 && res[i].of.i32 == 100 + i);

  /* v128 through the raw call */
  warm_val_raw_t v[3];
  memset(v, 0, sizeof v);
  for (int i = 0; i < 4; i++) {
    uint32_t x = (uint32_t)(i + 1), y = (uint32_t)(10 * (i + 1));
    memcpy(v[0].v128 + 4 * i, &x, 4);
    memcpy(v[2].v128 + 4 * i, &y, 4);
  }
  v[1].i32 = 77;
  t = warm_func_call_raw(func(in, 2), v, 3);
  CHECK(!t);
  for (int i = 0; i < 4; i++) {
    uint32_t x;
    memcpy(&x, v[0].v128 + 4 * i, 4);
    CHECK(x == (uint32_t)(11 * (i + 1)));
  }
  CHECK(v[1].i32 == 77);
  /* and refused through wasm_val_t */
  wasm_val_t vv[3] = { WASM_I32_VAL(0), WASM_I32_VAL(0), WASM_I32_VAL(0) };
  wasm_val_vec_t vvv = { 3, vv }, vr = { 2, res };
  t = wasm_func_call(func(in, 2), &vvv, &vr);
  CHECK(t && strstr(trap_msg(t), "v128"));
  if (t) wasm_trap_delete(t);

  /* too few arguments is a trap, not a crash */
  wasm_val_vec_t none = WASM_EMPTY_VEC;
  t = wasm_func_call(func(in, 0), &none, &rv);
  CHECK(t != NULL);
  if (t) wasm_trap_delete(t);
  wasm_module_delete(m);
  wasm_store_delete(s);
}

/* ---- host functions --------------------------------------------------------- */

static int64_t g_seen[32];
static int g_nseen;

static wasm_trap_t *host_sum(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
  double sum = 0;
  g_nseen = (int)args->size;
  for (size_t i = 0; i < args->size; i++) {
    const wasm_val_t *v = &args->data[i];
    double x = v->kind == WASM_I32 ? v->of.i32 : v->kind == WASM_I64 ? (double)v->of.i64
             : v->kind == WASM_F32 ? v->of.f32 : v->of.f64;
    g_seen[i] = (int64_t)x;
    sum += x * (double)(i + 1);
  }
  results->data[0].kind = WASM_F64;
  results->data[0].of.f64 = sum + (double)(intptr_t)env;
  results->data[1].kind = WASM_I32;
  results->data[1].of.i32 = (int32_t)args->size;
  results->data[2].kind = WASM_I64;
  results->data[2].of.i64 = -5;
  return NULL;
}

static wasm_trap_t *host_fail(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
  wasm_message_t m;
  wasm_name_new_from_string_nt(&m, "the host says no");
  wasm_trap_t *t = wasm_trap_new((wasm_store_t *)env, &m);
  wasm_name_delete(&m);
  return t;
}

static wasm_func_t *g_reenter;
static int g_depth_max;

/* calls back into wasm, which calls this again, n levels deep */
static wasm_trap_t *host_reenter(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
  int32_t n = args->data[0].of.i32;
  if (n > g_depth_max) g_depth_max = n;
  results->data[0].kind = WASM_I32;
  if (n == 0) { results->data[0].of.i32 = 1; return NULL; }
  wasm_val_t a = WASM_I32_VAL(n - 1), r = WASM_INIT_VAL;
  wasm_val_vec_t av = { 1, &a }, rv = { 1, &r };
  wasm_trap_t *t = wasm_func_call(g_reenter, &av, &rv);
  if (t) return t;
  results->data[0].of.i32 = r.of.i32 + n;
  return NULL;
}

static int g_finalized;
static void fin(void *p) { g_finalized += (int)(intptr_t)p; }

static void test_host(void) {
  wasm_store_t *s = wasm_store_new(engine);
  /* 10 ints and 10 floats, interleaved: past both register sets */
  wasm_valtype_vec_t ps, rs;
  wasm_valtype_vec_new_uninitialized(&ps, 20);
  for (int i = 0; i < 20; i++)
    ps.data[i] = wasm_valtype_new(i % 4 == 0 ? WASM_I32 : i % 4 == 1 ? WASM_F64
                                  : i % 4 == 2 ? WASM_I64 : WASM_F32);
  wasm_valtype_t *r3[3] = { wasm_valtype_new(WASM_F64), wasm_valtype_new(WASM_I32),
                            wasm_valtype_new(WASM_I64) };
  wasm_valtype_vec_new(&rs, 3, r3);
  wasm_functype_t *ft = wasm_functype_new(&ps, &rs);
  wasm_func_t *hf = wasm_func_new_with_env(s, ft, host_sum, (void *)(intptr_t)1000, fin);
  wasm_functype_delete(ft);

  wasm_functype_t *ft0 = wasm_functype_new_0_0();
  wasm_func_t *hfail = wasm_func_new_with_env(s, ft0, host_fail, s, NULL);
  wasm_functype_delete(ft0);
  wasm_functype_t *ft1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
  wasm_func_t *hre = wasm_func_new_with_env(s, ft1, host_reenter, NULL, NULL);
  wasm_functype_delete(ft1);

  char wat[4096];
  size_t n = (size_t)snprintf(wat, sizeof wat,
    "(module (import \"h\" \"sum\" (func $sum (param");
  for (int i = 0; i < 20; i++)
    n += (size_t)snprintf(wat + n, sizeof wat - n, " %s",
                          i % 4 == 0 ? "i32" : i % 4 == 1 ? "f64" : i % 4 == 2 ? "i64" : "f32");
  n += (size_t)snprintf(wat + n, sizeof wat - n,
    ") (result f64 i32 i64)))"
    " (import \"h\" \"fail\" (func $fail))"
    " (import \"h\" \"re\" (func $re (param i32) (result i32)))"
    " (func (export \"call\") (result f64 i32 i64)");
  for (int i = 0; i < 20; i++)
    n += (size_t)snprintf(wat + n, sizeof wat - n, " (%s.const %d)",
                          i % 4 == 0 ? "i32" : i % 4 == 1 ? "f64" : i % 4 == 2 ? "i64" : "f32", i + 1);
  n += (size_t)snprintf(wat + n, sizeof wat - n,
    " (call $sum))"
    " (func (export \"fail\") (result i32) (call $fail) (i32.const 1))"
    " (func (export \"down\") (param i32) (result i32) (call $re (local.get 0))))");
  wasm_module_t *m = module(s, wat);
  wasm_extern_t *imps[3] = { wasm_func_as_extern(hf), wasm_func_as_extern(hfail),
                             wasm_func_as_extern(hre) };
  wasm_instance_t *in = instance(s, m, imps, 3);

  wasm_val_t res[3];
  wasm_val_vec_t none = WASM_EMPTY_VEC, rv = { 3, res };
  wasm_trap_t *t = wasm_func_call(func(in, 0), &none, &rv);
  double want = 1000;
  for (int i = 0; i < 20; i++) want += (double)(i + 1) * (double)(i + 1);
  CHECK(!t && res[0].of.f64 == want && res[1].of.i32 == 20 && res[2].of.i64 == -5);
  CHECK(g_nseen == 20);
  for (int i = 0; i < 20; i++) CHECK(g_seen[i] == i + 1);

  /* the host's trap ends the wasm call, its message intact */
  wasm_val_t one;
  wasm_val_vec_t onev = { 1, &one };
  t = wasm_func_call(func(in, 1), &none, &onev);
  CHECK(t && !strcmp(trap_msg(t), "the host says no"));
  if (t) wasm_trap_delete(t);
  /* and the instance still runs */
  t = wasm_func_call(func(in, 0), &none, &rv);
  CHECK(!t);

  /* wasm -> host -> wasm -> host ..., 200 deep */
  g_reenter = func(in, 2);
  wasm_val_t a = WASM_I32_VAL(200);
  wasm_val_vec_t av = { 1, &a };
  t = wasm_func_call(g_reenter, &av, &onev);
  CHECK(!t && one.of.i32 == 1 + 200 * 201 / 2 && g_depth_max == 200);

  /* calling a host function from C calls it */
  wasm_val_t h[20];
  for (int i = 0; i < 20; i++) {
    h[i].kind = i % 4 == 0 ? WASM_I32 : i % 4 == 1 ? WASM_F64 : i % 4 == 2 ? WASM_I64 : WASM_F32;
    h[i].of.i64 = 0;
  }
  wasm_val_vec_t hv = { 20, h };
  t = wasm_func_call(hf, &hv, &rv);
  CHECK(!t && res[0].of.f64 == 1000);

  CHECK(wasm_func_param_arity(hf) == 20 && wasm_func_result_arity(hf) == 3);
  wasm_func_delete(hf);
  wasm_func_delete(hfail);
  wasm_func_delete(hre);
  wasm_module_delete(m);
  g_finalized = 0;
  wasm_store_delete(s);
  CHECK(g_finalized == 1000);
}

/* ---- traps ------------------------------------------------------------------- */

static void test_traps(void) {
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = module(s,
    "(module (memory 1) (table 1 funcref) (type $v (func))"
    "  (func (export \"unreachable\") unreachable)"
    "  (func (export \"oob\") (drop (i32.load (i32.const 70000))))"
    "  (func (export \"div\") (drop (i32.div_s (i32.const 1) (i32.const 0))))"
    "  (func (export \"overflow\") (drop (i32.div_s (i32.const 0x80000000) (i32.const -1))))"
    "  (func (export \"null\") (call_indirect (type $v) (i32.const 0)))"
    "  (func (export \"undef\") (call_indirect (type $v) (i32.const 5)))"
    "  (func $deep (export \"deep\") (param i32) (result i32)"
    "    (i32.add (call $deep (i32.add (local.get 0) (i32.const 1))) (i32.const 1)))"
    "  (func (export \"conv\") (drop (i32.trunc_f32_s (f32.const nan)))))");
  wasm_instance_t *in = instance(s, m, NULL, 0);
  static const char *const want[] = {
    "unreachable executed", "out of bounds memory access", "integer divide by zero",
    "integer overflow", "uninitialized element", "undefined element (index out of bounds)",
    "call stack exhausted", "invalid conversion to integer" };
  for (int i = 0; i < 8; i++) {
    wasm_val_t a = WASM_I32_VAL(0), r;
    wasm_val_vec_t av = { 1, &a }, rv = { 1, &r };
    wasm_trap_t *t = wasm_func_call(func(in, (size_t)i), &av, &rv);
    CHECK(t != NULL);
    if (!t) continue;
    if (strstr(trap_msg(t), want[i]) == NULL) printf("  trap %d: %s\n", i, trap_msg(t));
    CHECK(strstr(trap_msg(t), want[i]) != NULL);
    CHECK(wasm_trap_origin(t) == NULL);
    wasm_frame_vec_t tr;
    wasm_trap_trace(t, &tr);
    CHECK(tr.size == 0);
    wasm_frame_vec_delete(&tr);
    wasm_trap_delete(t);
  }
  /* a trap in a start function fails the instance and is handed back */
  wasm_module_t *ms = module(s, "(module (func $s unreachable) (start $s))");
  wasm_extern_vec_t none = WASM_EMPTY_VEC;
  wasm_trap_t *t = NULL;
  CHECK(!wasm_instance_new(s, ms, &none, &t));
  CHECK(t && !strcmp(trap_msg(t), "unreachable executed"));
  if (t) wasm_trap_delete(t);
  wasm_module_delete(ms);
  wasm_module_delete(m);
  wasm_store_delete(s);
}

/* ---- linking ----------------------------------------------------------------- */

static void test_link(void) {
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *a = module(s,
    "(module (memory (export \"mem\") 1 5) (global (export \"g\") (mut i32) (i32.const 7))"
    "  (table (export \"t\") 3 funcref) (func $f (export \"f\") (result i32) (i32.const 42))"
    "  (elem (i32.const 1) $f))");
  wasm_instance_t *ia = instance(s, a, NULL, 0);
  wasm_extern_vec_t ex;
  wasm_instance_exports(ia, &ex);
  CHECK(ex.size == 4);

  wasm_module_t *b = module(s,
    "(module (import \"a\" \"mem\" (memory 1)) (import \"a\" \"g\" (global $g (mut i32)))"
    "  (import \"a\" \"t\" (table 3 funcref)) (import \"a\" \"f\" (func $f (result i32)))"
    "  (type $r (func (result i32)))"
    "  (func (export \"run\") (result i32)"
    "    (i32.store (i32.const 8) (i32.const 99))"
    "    (global.set $g (i32.add (global.get $g) (i32.const 1)))"
    "    (i32.add (call $f) (call_indirect (type $r) (i32.const 1)))))");
  wasm_extern_t *imps[4] = { ex.data[0], ex.data[1], ex.data[2], ex.data[3] };
  wasm_instance_t *ib = instance(s, b, imps, 4);
  wasm_val_t r;
  wasm_val_vec_t none = WASM_EMPTY_VEC, rv = { 1, &r };
  wasm_trap_t *t = wasm_func_call(func(ib, 0), &none, &rv);
  CHECK(!t && r.of.i32 == 84);
  /* what b did to a's memory and global */
  wasm_memory_t *mem = wasm_extern_as_memory(ex.data[0]);
  CHECK(wasm_memory_data(mem)[8] == 99);
  wasm_val_t g;
  wasm_global_get(wasm_extern_as_global(ex.data[1]), &g);
  CHECK(g.of.i32 == 8);
  /* a's table holds a's function, the same function as its export */
  wasm_ref_t *f1 = wasm_table_get(wasm_extern_as_table(ex.data[2]), 1);
  CHECK(f1 && wasm_ref_same(f1, wasm_func_as_ref(wasm_extern_as_func(ex.data[3]))));
  if (f1) wasm_ref_delete(f1);
  CHECK(!wasm_table_get(wasm_extern_as_table(ex.data[2]), 0));
  CHECK(!wasm_table_get(wasm_extern_as_table(ex.data[2]), 3));

  /* every mismatch refused, saying which import */
  wasm_message_t err;
  wasm_trap_t *trap;
  wasm_extern_t *bad1[4] = { ex.data[1], ex.data[1], ex.data[2], ex.data[3] };
  wasm_extern_vec_t bv = { 4, bad1 };
  CHECK(!warm_instance_new(s, b, &bv, &trap, &err));
  CHECK(strstr(err.data, "\"a\".\"mem\"") != NULL);
  wasm_byte_vec_delete(&err);
  wasm_module_t *c = module(s, "(module (import \"a\" \"f\" (func (result i64))))");
  wasm_extern_t *bad2[1] = { ex.data[3] };
  wasm_extern_vec_t bv2 = { 1, bad2 };
  CHECK(!warm_instance_new(s, c, &bv2, &trap, &err));
  wasm_byte_vec_delete(&err);
  wasm_module_t *d = module(s, "(module (import \"a\" \"mem\" (memory 6)))");
  wasm_extern_t *bad3[1] = { ex.data[0] };
  wasm_extern_vec_t bv3 = { 1, bad3 };
  CHECK(!warm_instance_new(s, d, &bv3, &trap, &err));
  wasm_byte_vec_delete(&err);
  wasm_extern_vec_t empty = WASM_EMPTY_VEC;
  CHECK(!warm_instance_new(s, d, &empty, &trap, &err));
  CHECK(strstr(err.data, "no import") != NULL);
  wasm_byte_vec_delete(&err);

  wasm_module_delete(c);
  wasm_module_delete(d);
  wasm_module_delete(a);
  wasm_module_delete(b);
  wasm_store_delete(s);
}

/* ---- references -------------------------------------------------------------- */

static void test_refs(void) {
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = module(s,
    "(module (table $t (export \"t\") 4 externref) (global $g (export \"g\") (mut externref) (ref.null extern))"
    "  (table $ft 2 funcref) (elem declare func $one)"
    "  (func $one (export \"one\") (result i32) (i32.const 1))"
    "  (func (export \"id\") (param externref) (result externref) (local.get 0))"
    "  (func (export \"keep\") (param externref) (global.set $g (local.get 0)))"
    "  (func (export \"fref\") (result funcref) (ref.func $one))"
    "  (func (export \"isnull\") (param externref) (result i32) (ref.is_null (local.get 0)))"
    "  (func (export \"anyid\") (param anyref) (result anyref) (local.get 0))"
    "  (func (export \"call\") (param funcref) (result i32)"
    "    (table.set $ft (i32.const 0) (local.get 0))"
    "    (call_indirect $ft (result i32) (i32.const 0))))");
  wasm_instance_t *in = instance(s, m, NULL, 0);
  wasm_extern_vec_t ex;
  wasm_instance_exports(in, &ex);

  wasm_foreign_t *fo = wasm_foreign_new(s);
  wasm_foreign_set_host_info(fo, (void *)0x1234);
  wasm_val_t a = WASM_REF_VAL(wasm_foreign_as_ref(fo)), r;
  wasm_val_vec_t av = { 1, &a }, rv = { 1, &r }, none = WASM_EMPTY_VEC;
  wasm_trap_t *t = wasm_func_call(wasm_extern_as_func(ex.data[3]), &av, &rv);
  CHECK(!t && r.of.ref && wasm_ref_same(r.of.ref, wasm_foreign_as_ref(fo)));
  CHECK(r.of.ref && wasm_ref_get_host_info(r.of.ref) == (void *)0x1234);
  if (r.of.ref) wasm_ref_delete(r.of.ref);

  /* kept in a global, read back */
  t = wasm_func_call(wasm_extern_as_func(ex.data[4]), &av, &none);
  CHECK(!t);
  wasm_val_t g;
  wasm_global_get(wasm_extern_as_global(ex.data[1]), &g);
  CHECK(g.kind == WASM_EXTERNREF && g.of.ref && wasm_ref_same(g.of.ref, wasm_foreign_as_ref(fo)));
  wasm_val_delete(&g);

  /* null in, null out */
  wasm_val_t nul = WASM_REF_VAL(NULL);
  wasm_val_vec_t nv = { 1, &nul };
  t = wasm_func_call(wasm_extern_as_func(ex.data[6]), &nv, &rv);
  CHECK(!t && r.of.i32 == 1);

  /* a table of externrefs set by the host */
  wasm_table_t *tab = wasm_extern_as_table(ex.data[0]);
  CHECK(wasm_table_set(tab, 2, wasm_foreign_as_ref(fo)));
  CHECK(!wasm_table_set(tab, 4, NULL));
  wasm_ref_t *got = wasm_table_get(tab, 2);
  CHECK(got && wasm_ref_same(got, wasm_foreign_as_ref(fo)));
  if (got) wasm_ref_delete(got);
  CHECK(wasm_table_size(tab) == 4 && wasm_table_grow(tab, 3, wasm_foreign_as_ref(fo)));
  CHECK(wasm_table_size(tab) == 7);

  /* a function reference out of wasm, called from C and handed back in */
  t = wasm_func_call(wasm_extern_as_func(ex.data[5]), &none, &rv);
  CHECK(!t && r.of.ref);
  wasm_func_t *one = r.of.ref ? wasm_ref_as_func(r.of.ref) : NULL;
  CHECK(one && wasm_func_same(one, wasm_extern_as_func(ex.data[2])));
  wasm_val_t res;
  wasm_val_vec_t resv = { 1, &res };
  CHECK(one && !wasm_func_call(one, &none, &resv) && res.of.i32 == 1);
  wasm_val_t fa = WASM_REF_VAL(r.of.ref);
  fa.kind = WASM_FUNCREF;
  wasm_val_vec_t fav = { 1, &fa };
  t = wasm_func_call(wasm_extern_as_func(ex.data[8]), &fav, &resv);
  CHECK(!t && res.of.i32 == 1);
  if (r.of.ref) wasm_ref_delete(r.of.ref);

  /* a host function handed to wasm as a reference and called through it */
  wasm_functype_t *ft = wasm_functype_new_0_1(wasm_valtype_new_i32());
  wasm_func_t *seven = wasm_func_new_with_env(s, ft, host_fail, s, NULL);
  wasm_functype_delete(ft);
  fa.of.ref = wasm_func_as_ref(seven);
  t = wasm_func_call(wasm_extern_as_func(ex.data[8]), &fav, &resv);
  CHECK(t && !strcmp(trap_msg(t), "the host says no"));
  if (t) wasm_trap_delete(t);

  /* a host object in the any hierarchy, and back */
  wasm_val_t aa = { .kind = WARM_ANYREF, .of = { .ref = wasm_foreign_as_ref(fo) } };
  wasm_val_vec_t aav = { 1, &aa };
  t = wasm_func_call(wasm_extern_as_func(ex.data[7]), &aav, &rv);
  CHECK(!t && r.of.ref && wasm_ref_same(r.of.ref, wasm_foreign_as_ref(fo)));
  if (!t && r.of.ref) wasm_ref_delete(r.of.ref);

  wasm_func_delete(seven);
  wasm_foreign_delete(fo);
  wasm_module_delete(m);
  wasm_store_delete(s);
}

/* ---- what the host makes ---------------------------------------------------- */

static void test_host_objects(void) {
  wasm_store_t *s = wasm_store_new(engine);
  wasm_limits_t ml = { 1, 4 };
  wasm_memorytype_t *mt = wasm_memorytype_new(&ml);
  wasm_memory_t *mem = wasm_memory_new(s, mt);
  wasm_memorytype_delete(mt);
  CHECK(wasm_memory_size(mem) == 1 && wasm_memory_data_size(mem) == 65536);
  wasm_memory_data(mem)[100] = 5;
  CHECK(wasm_memory_grow(mem, 2) && wasm_memory_size(mem) == 3);
  CHECK(!wasm_memory_grow(mem, 2));

  wasm_valtype_t *i64 = wasm_valtype_new_i64();
  wasm_globaltype_t *gt = wasm_globaltype_new(i64, WASM_VAR);
  wasm_val_t init = WASM_I64_VAL(-9);
  wasm_global_t *glob = wasm_global_new(s, gt, &init);
  wasm_globaltype_delete(gt);
  gt = wasm_globaltype_new(wasm_valtype_new_f32(), WASM_CONST);
  wasm_val_t fi = WASM_F32_VAL(2.5f);
  wasm_global_t *cglob = wasm_global_new(s, gt, &fi);
  wasm_globaltype_delete(gt);

  wasm_limits_t tl = { 2, 10 };
  wasm_tabletype_t *tt = wasm_tabletype_new(wasm_valtype_new_funcref(), &tl);
  wasm_table_t *tab = wasm_table_new(s, tt, NULL);
  wasm_tabletype_delete(tt);

  wasm_module_t *m = module(s,
    "(module (import \"\" \"m\" (memory 1)) (import \"\" \"g\" (global $g (mut i64)))"
    "  (import \"\" \"c\" (global $c f32)) (import \"\" \"t\" (table 2 funcref))"
    "  (func $f (result i32) (i32.const 3)) (elem (i32.const 1) $f)"
    "  (func (export \"run\") (result i64)"
    "    (global.set $g (i64.add (global.get $g) (i64.const 1)))"
    "    (i64.add (i64.add (i64.load8_u (i32.const 100)) (i64.trunc_f32_s (global.get $c)))"
    "             (global.get $g))))");
  wasm_extern_t *imps[4] = { wasm_memory_as_extern(mem), wasm_global_as_extern(glob),
                             wasm_global_as_extern(cglob), wasm_table_as_extern(tab) };
  wasm_instance_t *in = instance(s, m, imps, 4);
  wasm_val_t r;
  wasm_val_vec_t none = WASM_EMPTY_VEC, rv = { 1, &r };
  wasm_trap_t *t = wasm_func_call(func(in, 0), &none, &rv);
  CHECK(!t && r.of.i64 == 5 + 2 - 8);
  wasm_val_t g;
  wasm_global_get(glob, &g);
  CHECK(g.of.i64 == -8);
  wasm_val_t g2 = WASM_I64_VAL(100);
  wasm_global_set(glob, &g2);
  t = wasm_func_call(func(in, 0), &none, &rv);
  CHECK(!t && r.of.i64 == 5 + 2 + 101);
  /* a const global stays what it is */
  wasm_val_t f2 = WASM_F32_VAL(9.0f);
  wasm_global_set(cglob, &f2);
  wasm_global_get(cglob, &g);
  CHECK(g.of.f32 == 2.5f);
  /* the table the module filled */
  wasm_ref_t *e1 = wasm_table_get(tab, 1);
  CHECK(e1 != NULL);
  wasm_func_t *f = e1 ? wasm_ref_as_func(e1) : NULL;
  wasm_val_t fr;
  wasm_val_vec_t frv = { 1, &fr };
  CHECK(f && !wasm_func_call(f, &none, &frv) && fr.of.i32 == 3);
  if (e1) wasm_ref_delete(e1);
  CHECK(!wasm_table_grow(tab, 9, NULL) && wasm_table_grow(tab, 8, NULL));
  wasm_tabletype_t *tt2 = wasm_table_type(tab);
  CHECK(wasm_tabletype_limits(tt2)->min == 10 && wasm_tabletype_limits(tt2)->max == 10);
  wasm_tabletype_delete(tt2);

  wasm_memory_delete(mem);
  wasm_global_delete(glob);
  wasm_global_delete(cglob);
  wasm_table_delete(tab);
  wasm_module_delete(m);
  wasm_store_delete(s);
}

/* ---- serialising --------------------------------------------------------------- */

static void test_serialize(void) {
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = module(s,
    "(module (memory 1) (data (i32.const 16) \"hello\")"
    "  (func (export \"f\") (param i32) (result i32)"
    "    (i32.add (i32.load8_u (i32.const 16)) (local.get 0))))");
  wasm_byte_vec_t bytes;
  wasm_module_serialize(m, &bytes);
  CHECK(bytes.size > 16);
  wasm_module_delete(m);

  wasm_store_t *s2 = wasm_store_new(engine);
  wasm_module_t *m2 = wasm_module_deserialize(s2, &bytes);
  CHECK(m2 != NULL);
  if (m2) {
    wasm_instance_t *in = instance(s2, m2, NULL, 0);
    wasm_val_t a = WASM_I32_VAL(1), r;
    wasm_val_vec_t av = { 1, &a }, rv = { 1, &r };
    CHECK(!wasm_func_call(func(in, 0), &av, &rv) && r.of.i32 == 'h' + 1);
    wasm_module_delete(m2);
  }
  /* every cut short, and every byte flipped in the header, is refused */
  for (size_t n = 0; n < bytes.size; n += bytes.size / 97 + 1) {
    wasm_byte_vec_t cut = { n, bytes.data };
    wasm_module_t *bad = wasm_module_deserialize(s2, &cut);
    CHECK(bad == NULL);
    if (bad) wasm_module_delete(bad);
  }
  for (size_t i = 0; i < 16 && i < bytes.size; i++) {
    bytes.data[i] ^= 0x40;
    wasm_module_t *bad = wasm_module_deserialize(s2, &bytes);
    CHECK(bad == NULL);
    if (bad) wasm_module_delete(bad);
    bytes.data[i] ^= 0x40;
  }
  wasm_byte_vec_delete(&bytes);
  wasm_store_delete(s2);
  wasm_store_delete(s);
}

/* ---- stores freed as they go ---------------------------------------------------- */

static void test_churn(void) {
  /* more stores with memories than the runtime has guarded reservations
     (1024): each memory goes back with its store */
  for (int i = 0; i < 1500; i++) {
    wasm_store_t *s = wasm_store_new(engine);
    wasm_module_t *m = module(s, "(module (memory (export \"m\") 1) (func (export \"f\") (result i32)"
                                 " (i32.store (i32.const 0) (i32.const 3)) (i32.load (i32.const 0))))");
    if (!m) { wasm_store_delete(s); break; }
    wasm_instance_t *in = instance(s, m, NULL, 0);
    if (!in) { wasm_module_delete(m); wasm_store_delete(s); break; }
    wasm_val_t r;
    wasm_val_vec_t none = WASM_EMPTY_VEC, rv = { 1, &r };
    if (wasm_func_call(func(in, 1), &none, &rv) || r.of.i32 != 3) { CHECK(0); break; }
    wasm_module_delete(m);
    wasm_store_delete(s);
  }
  /* and modules compiled one after another, each freed before the next:
     each is its own, wherever the last one was */
  wasm_store_t *s = wasm_store_new(engine);
  for (int i = 0; i < 64; i++) {
    char wat[256];
    snprintf(wat, sizeof wat, "(module (func (export \"f\") (result i32) (i32.const %d))"
                              "%s)", i, i % 2 ? " (func (export \"g\"))" : "");
    wasm_module_t *m = module(s, wat);
    if (!m) break;
    wasm_instance_t *in = instance(s, m, NULL, 0);
    wasm_val_t r;
    wasm_val_vec_t none = WASM_EMPTY_VEC, rv = { 1, &r };
    CHECK(in && !wasm_func_call(func(in, 0), &none, &rv) && r.of.i32 == i);
    wasm_instance_delete(in);
    wasm_module_delete(m);
  }
  wasm_store_delete(s);
}

/* ---- threads -------------------------------------------------------------------- */

static wasm_shared_module_t *g_shared;

static void *thread_main(void *arg) {
  intptr_t id = (intptr_t)arg;
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = wasm_module_obtain(s, g_shared);
  intptr_t bad = 0;
  for (int k = 0; k < 20; k++) {
    wasm_extern_vec_t none = WASM_EMPTY_VEC;
    wasm_instance_t *in = wasm_instance_new(s, m, &none, NULL);
    wasm_extern_vec_t ex;
    wasm_instance_exports(in, &ex);
    wasm_val_t a = WASM_I32_VAL((int32_t)(id * 100 + k)), r;
    wasm_val_vec_t av = { 1, &a }, rv = { 1, &r };
    if (wasm_func_call(wasm_extern_as_func(ex.data[0]), &av, &rv) ||
        r.of.i32 != (int32_t)((id * 100 + k) * 2))
      bad++;
    /* and compiles of their own, at once */
    char wat[128];
    snprintf(wat, sizeof wat, "(module (func (export \"f\") (result i32) (i32.const %d)))", (int)id);
    wasm_byte_vec_t b;
    wasm_message_t err;
    if (!warm_wat2wasm(wat, strlen(wat), &b, &err)) bad++;
    wasm_module_t *own = warm_module_new(s, &b, &err);
    if (!own) bad++;
    else wasm_module_delete(own);
    wasm_byte_vec_delete(&b);
    wasm_byte_vec_delete(&err);
    wasm_extern_vec_delete(&ex);
    wasm_instance_delete(in);
  }
  wasm_module_delete(m);
  wasm_store_delete(s);
  return (void *)bad;
}

static void test_threads(void) {
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = module(s, "(module (func (export \"dbl\") (param i32) (result i32)"
                               " (i32.mul (local.get 0) (i32.const 2))))");
  g_shared = wasm_module_share(m);
  pthread_t th[8];
  for (intptr_t i = 0; i < 8; i++) pthread_create(&th[i], NULL, thread_main, (void *)i);
  for (int i = 0; i < 8; i++) {
    void *bad;
    pthread_join(th[i], &bad);
    CHECK(bad == NULL);
  }
  wasm_shared_module_delete(g_shared);
  wasm_module_delete(m);
  wasm_store_delete(s);
}

/* ---- WASI ------------------------------------------------------------------------- */

static void test_wasi(void) {
  char dir[] = "/tmp/warm-capi-XXXXXX";
  if (!mkdtemp(dir)) { CHECK(0); return; }
  char path[256];
  snprintf(path, sizeof path, "%s/in.txt", dir);
  FILE *f = fopen(path, "w");
  fputs("from the host", f);
  fclose(f);

  wasm_store_t *s = wasm_store_new(engine);
  /* reads in.txt from its preopened directory, writes it and its first
     argument and environment entry to out.txt, and exits with 7 */
  wasm_module_t *m = module(s,
    "(module"
    "  (import \"wasi_snapshot_preview1\" \"path_open\" (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))"
    "  (import \"wasi_snapshot_preview1\" \"fd_read\" (func $read (param i32 i32 i32 i32) (result i32)))"
    "  (import \"wasi_snapshot_preview1\" \"fd_write\" (func $write (param i32 i32 i32 i32) (result i32)))"
    "  (import \"wasi_snapshot_preview1\" \"args_get\" (func $args (param i32 i32) (result i32)))"
    "  (import \"wasi_snapshot_preview1\" \"environ_get\" (func $env (param i32 i32) (result i32)))"
    "  (import \"wasi_snapshot_preview1\" \"proc_exit\" (func $exit (param i32)))"
    "  (memory (export \"memory\") 1)"
    "  (data (i32.const 0) \"in.txt\") (data (i32.const 8) \"out.txt\")"
    "  (func (export \"_start\") (local $n i32)"
    "    (if (call $open (i32.const 3) (i32.const 0) (i32.const 0) (i32.const 6) (i32.const 0)"
    "                    (i64.const -1) (i64.const -1) (i32.const 0) (i32.const 100)) (then unreachable))"
    "    (i32.store (i32.const 200) (i32.const 1024)) (i32.store (i32.const 204) (i32.const 64))"
    "    (drop (call $read (i32.load (i32.const 100)) (i32.const 200) (i32.const 1) (i32.const 104)))"
    "    (if (call $open (i32.const 3) (i32.const 0) (i32.const 8) (i32.const 7) (i32.const 1)"
    "                    (i64.const -1) (i64.const -1) (i32.const 0) (i32.const 108)) (then unreachable))"
    "    (i32.store (i32.const 208) (i32.const 1024)) (i32.store (i32.const 212) (i32.load (i32.const 104)))"
    "    (drop (call $args (i32.const 300) (i32.const 2048)))"
    "    (drop (call $env (i32.const 400) (i32.const 3072)))"
    "    (i32.store (i32.const 216) (i32.load (i32.const 304))) (i32.store (i32.const 220) (i32.const 3))"
    "    (i32.store (i32.const 224) (i32.load (i32.const 400))) (i32.store (i32.const 228) (i32.const 5))"
    "    (drop (call $write (i32.load (i32.const 108)) (i32.const 208) (i32.const 3) (i32.const 112)))"
    "    (call $exit (i32.const 7))))");
  warm_wasi_config_t *c = warm_wasi_config_new();
  const char *argv[] = { "prog", "abc" };
  warm_wasi_config_set_argv(c, 2, argv);
  warm_wasi_config_env_add(c, "K=V12");
  CHECK(warm_wasi_config_preopen_dir(c, dir, "."));
  wasm_extern_vec_t imps;
  CHECK(warm_wasi_imports(s, m, c, &imps));
  warm_wasi_config_delete(c);
  for (size_t i = 0; i < imps.size; i++) CHECK(imps.data[i] != NULL);
  wasm_trap_t *t = NULL;
  wasm_instance_t *in = wasm_instance_new(s, m, &imps, &t);
  CHECK(in != NULL);
  wasm_val_vec_t none = WASM_EMPTY_VEC;
  t = in ? wasm_func_call(func(in, 1), &none, &none) : NULL;
  int status = -1;
  CHECK(t && warm_trap_exit_status(t, &status) && status == 7);
  if (t) wasm_trap_delete(t);
  snprintf(path, sizeof path, "%s/out.txt", dir);
  f = fopen(path, "r");
  char buf[64] = { 0 };
  if (f) { fread(buf, 1, sizeof buf - 1, f); fclose(f); }
  CHECK(!strcmp(buf, "from the hostabcK=V12"));
  remove(path);
  snprintf(path, sizeof path, "%s/in.txt", dir);
  remove(path);
  rmdir(dir);

  /* a preview1 function imported at another type is not bound */
  wasm_module_t *wrong = module(s,
    "(module (import \"wasi_snapshot_preview1\" \"fd_write\" (func (param i32) (result i32))))");
  c = warm_wasi_config_new();
  CHECK(warm_wasi_imports(s, wrong, c, &imps));
  CHECK(imps.size == 1 && imps.data[0] == NULL);
  wasm_extern_vec_delete(&imps);
  /* nor a directory that is not there */
  CHECK(warm_wasi_config_preopen_dir(c, "/no/such/dir/here", NULL));
  CHECK(!warm_wasi_imports(s, wrong, c, &imps));
  warm_wasi_config_delete(c);
  wasm_module_delete(wrong);
  wasm_module_delete(m);
  wasm_store_delete(s);
}

/* ---- custom sections ---------------------------------------------------------------- */

static void test_custom(void) {
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = module(s, "(module (@custom \"tag\" \"one\") (@custom \"tag\" \"second\"))");
  const wasm_byte_t *data;
  size_t size;
  CHECK(warm_module_custom_section(m, "tag", 1, &data, &size) == 2);
  CHECK(size == 6 && !memcmp(data, "second", 6));
  CHECK(warm_module_custom_section(m, "tag", 2, &data, &size) == 2 && !data && !size);
  CHECK(warm_module_custom_section(m, "other", 0, &data, &size) == 0);
  wasm_module_delete(m);
  wasm_store_delete(s);
}

/* -v names each test as it starts, unbuffered, so a crash says where; other
   arguments name the tests to run, all of them when none is named */
static int g_argc;
static char **g_argv;
static int g_verbose;

static int wanted(const char *name) {
  int any = 0;
  for (int i = 1; i < g_argc; i++) {
    if (!strcmp(g_argv[i], "-v")) continue;
    any = 1;
    if (strstr(name, g_argv[i])) return 1;
  }
  return !any;
}

#define RUN(t) do { if (wanted(#t)) { if (g_verbose) printf("%s\n", #t); t(); } } while (0)

int main(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) g_verbose = 1;
  setvbuf(stdout, NULL, _IONBF, 0);
  engine = wasm_engine_new();
  CHECK(engine != NULL);
  RUN(test_errors);
  RUN(test_reflect);
  RUN(test_many);
  RUN(test_host);
  RUN(test_traps);
  RUN(test_link);
  RUN(test_refs);
  RUN(test_host_objects);
  RUN(test_serialize);
  RUN(test_churn);
  RUN(test_threads);
  RUN(test_wasi);
  RUN(test_custom);
  wasm_engine_delete(engine);
  printf("capi: %d checks, %d failed\n", nchecks, nfails);
  return nfails ? 1 : 0;
}
