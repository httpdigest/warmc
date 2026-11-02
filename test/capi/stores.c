/* Many stores on many threads at once, as a server that calls into wasm on
   every thread it serves from has them: each thread makes stores of its
   own, from modules compiled once and shared, and runs in them at the same
   time as every other thread -- what the runtime keeps for a store (its
   instances, its GC heap and that heap's collector, the trap a call is
   catching, the stack limit, its WASI state) must be that store's alone.

     * every thread is inside wasm at the same moment, once: a host function
       waits there until all of them are, which no lock across stores lets
       happen;
     * linear memory filled and summed, against the same sum in C;
     * GC objects made by the megabyte, so that every store's heap collects
       many times -- on a collector thread of its own, or, past as many as
       there are processors, on the thread itself;
     * traps: a division by zero, a load past the memory (a fault on its
       guard pages), a recursion past the thread's stack limit, an
       exception thrown and caught, and one thrown out of wasm;
     * a host function that calls back into its own store and into a second
       store of the thread's, nested, and a trap in the first once it is
       back;
     * WASI's arguments, different in every store;
     * stores made and deleted, round after round, while the other threads
       run;
     * one store every thread calls into, whose calls the store's lock takes
       one at a time: a counter in wasm each of them bumps adds up;
     * an import from another store, and a reference to another store's
       function put in a table, refused.

   stores [THREADS [ROUNDS]] prints one line and exits 0, or names what
   went wrong and exits 1. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wasm.h"
#include "warm.h"

#ifdef _WIN32
#include <windows.h>
static void nap_ms(int ms) { Sleep((DWORD)ms); }
static int env_set(const char *k, const char *v) { return _putenv_s(k, v); }
#else
#include <unistd.h>
static void nap_ms(int ms) { usleep((useconds_t)ms * 1000); }
static int env_set(const char *k, const char *v) { return setenv(k, v, 0); }
#endif

static const char WORK[] =
  "(module\n"
  "  (import \"host\" \"rendezvous\" (func $rv (param i32) (result i32)))\n"
  "  (import \"host\" \"callback\" (func $cb (param i32) (result i32)))\n"
  "  (memory (export \"memory\") 1 1)\n"
  "  (tag $boom (param i32))\n"
  /* an LCG's stream written over the memory, its sum */
  "  (func (export \"fill\") (param $x i32) (param $n i32) (result i64)\n"
  "    (local $i i32) (local $s i64)\n"
  "    (loop $l\n"
  "      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1103515245))\n"
  "                             (i32.const 12345)))\n"
  "      (i32.store (i32.shl (local.get $i) (i32.const 2)) (local.get $x))\n"
  "      (local.set $s (i64.add (local.get $s) (i64.extend_i32_u (local.get $x))))\n"
  "      (local.set $i (i32.add (local.get $i) (i32.const 1)))\n"
  "      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))\n"
  "    (local.get $s))\n"
  "  (func (export \"div\") (param i32 i32) (result i32)\n"
  "    (i32.div_s (local.get 0) (local.get 1)))\n"
  "  (func (export \"peek\") (param i32) (result i32) (i32.load (local.get 0)))\n"
  "  (func $deep (export \"deep\") (param i32) (result i32)\n"
  "    (i32.add (call $deep (i32.add (local.get 0) (i32.const 1))) (i32.const 1)))\n"
  /* thrown and caught inside: the payload plus one */
  "  (func (export \"catch\") (param i32) (result i32)\n"
  "    (block $h (result i32)\n"
  "      (try_table (catch $boom $h) (throw $boom (local.get 0)))\n"
  "      (i32.const -1))\n"
  "    (i32.const 1) (i32.add))\n"
  "  (func (export \"throw\") (param i32) (result i32) (throw $boom (local.get 0)))\n"
  "  (func (export \"rendezvous\") (param i32) (result i32) (call $rv (local.get 0)))\n"
  "  (func (export \"twice\") (param i32) (result i32)\n"
  "    (i32.mul (call $cb (local.get 0)) (i32.const 2)))\n"
  /* back from the other store, a trap this store's call catches */
  "  (func (export \"cb_then_trap\") (param i32) (result i32)\n"
  "    (drop (call $cb (local.get 0)))\n"
  "    (i32.div_s (local.get 0) (i32.const 0)))\n"
  "  (func (export \"inc\") (param i32) (result i32)\n"
  "    (i32.add (local.get 0) (i32.const 1))))\n";

static const char GC[] =
  "(module\n"
  "  (type $node (struct (field $v i64) (field $next (ref null $node))))\n"
  "  (type $buf (array (mut i64)))\n"
  /* rounds lists of n nodes and an array of n each, all but the last list
     garbage; the last list's sum and the last array's length */
  "  (func (export \"churn\") (param $rounds i32) (param $n i32) (result i64)\n"
  "    (local $r i32) (local $i i32) (local $list (ref null $node))\n"
  "    (local $b (ref null $buf)) (local $s i64)\n"
  "    (loop $outer\n"
  "      (local.set $list (ref.null $node))\n"
  "      (local.set $i (i32.const 0))\n"
  "      (loop $inner\n"
  "        (local.set $list (struct.new $node\n"
  "          (i64.extend_i32_u (i32.add (local.get $i) (local.get $r)))\n"
  "          (local.get $list)))\n"
  "        (local.set $i (i32.add (local.get $i) (i32.const 1)))\n"
  "        (br_if $inner (i32.lt_u (local.get $i) (local.get $n))))\n"
  "      (local.set $b (array.new $buf (i64.const 7) (local.get $n)))\n"
  "      (local.set $r (i32.add (local.get $r) (i32.const 1)))\n"
  "      (br_if $outer (i32.lt_u (local.get $r) (local.get $rounds))))\n"
  "    (block $done\n"
  "      (loop $walk\n"
  "        (br_if $done (ref.is_null (local.get $list)))\n"
  "        (local.set $s (i64.add (local.get $s) (struct.get $node $v (local.get $list))))\n"
  "        (local.set $list (struct.get $node $next (local.get $list)))\n"
  "        (br $walk)))\n"
  "    (i64.add (local.get $s) (i64.extend_i32_u (array.len (local.get $b))))))\n";

static const char ARGS[] =
  "(module\n"
  "  (import \"wasi_snapshot_preview1\" \"args_sizes_get\"\n"
  "    (func $sizes (param i32 i32) (result i32)))\n"
  "  (import \"wasi_snapshot_preview1\" \"args_get\"\n"
  "    (func $get (param i32 i32) (result i32)))\n"
  "  (memory (export \"memory\") 1)\n"
  /* the sum of the bytes of every argument, NULs and all */
  "  (func (export \"argsum\") (result i32)\n"
  "    (local $i i32) (local $n i32) (local $s i32)\n"
  "    (drop (call $sizes (i32.const 0) (i32.const 4)))\n"
  "    (drop (call $get (i32.const 16) (i32.const 1024)))\n"
  "    (local.set $n (i32.load (i32.const 4)))\n"
  "    (block $done\n"
  "      (loop $l\n"
  "        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))\n"
  "        (local.set $s (i32.add (local.get $s)\n"
  "          (i32.load8_u (i32.add (i32.const 1024) (local.get $i)))))\n"
  "        (local.set $i (i32.add (local.get $i) (i32.const 1)))\n"
  "        (br $l)))\n"
  "    (local.get $s)))\n";

static const char INC[] =
  "(module (func (export \"inc\") (param i32) (result i32)\n"
  "  (i32.add (local.get 0) (i32.const 1))))\n";

static const char COUNTER[] =
  "(module\n"
  "  (global $g (mut i64) (i64.const 0))\n"
  /* one at a time, every bump of n is seen whole */
  "  (func (export \"bump\") (param $n i32)\n"
  "    (loop $l\n"
  "      (global.set $g (i64.add (global.get $g) (i64.const 1)))\n"
  "      (local.set $n (i32.sub (local.get $n) (i32.const 1)))\n"
  "      (br_if $l (local.get $n))))\n"
  "  (func (export \"get\") (result i64) (global.get $g)))\n";

static const char IMPORTER[] =
  "(module (import \"m\" \"inc\" (func (param i32) (result i32))))\n";

static wasm_engine_t *engine;
static wasm_shared_module_t *m_work, *m_gc, *m_args, *m_inc, *m_counter;
static wasm_store_t *shared_store;
static wasm_func_t *shared_bump;
static int nthreads = 200, nrounds = 4;
static atomic_int inside;
static atomic_int failures;

#define FILL_N 16384
#define CHURN_ROUNDS 24
#define CHURN_N 6000
#define BUMP_N 2000
#define BUMPS 5

static void fail(int tid, const char *fmt, const char *a, long long b) {
  if (atomic_fetch_add(&failures, 1) < 20) {
    fprintf(stderr, "stores: thread %d: ", tid);
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
  }
}

static wasm_module_t *compile(const char *wat, wasm_shared_module_t **shared) {
  wasm_byte_vec_t bin;
  wasm_message_t err;
  if (!warm_wat2wasm(wat, strlen(wat), &bin, &err)) {
    fprintf(stderr, "stores: %.*s\n", (int)err.size, err.data);
    exit(1);
  }
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = warm_module_new(s, &bin, &err);
  if (!m) {
    fprintf(stderr, "stores: %.*s\n", (int)err.size, err.data);
    exit(1);
  }
  wasm_byte_vec_delete(&bin);
  *shared = wasm_module_share(m);
  wasm_module_delete(m);
  wasm_store_delete(s);
  return NULL;
}

static wasm_func_t *export_func(const wasm_extern_vec_t *ex, const wasm_module_t *m,
                                const char *name) {
  wasm_exporttype_vec_t ts;
  wasm_module_exports(m, &ts);
  wasm_func_t *f = NULL;
  for (size_t i = 0; i < ts.size && !f; i++) {
    const wasm_name_t *n = wasm_exporttype_name(ts.data[i]);
    if (n->size == strlen(name) && !memcmp(n->data, name, n->size))
      f = wasm_extern_as_func(ex->data[i]);
  }
  wasm_exporttype_vec_delete(&ts);
  if (!f) { fprintf(stderr, "stores: no export %s\n", name); exit(1); }
  return f;
}

/* f(args) with nargs i32 or i64 values; its trap's message to msg, or the
   one result in *r */
static int call(wasm_func_t *f, const wasm_val_t *args, size_t nargs, wasm_val_t *r,
                char *msg, size_t cap) {
  wasm_val_vec_t av = { nargs, (wasm_val_t *)args };
  wasm_val_t res[1] = { WASM_INIT_VAL };
  wasm_val_vec_t rv = { wasm_func_result_arity(f), res };
  wasm_trap_t *t = wasm_func_call(f, &av, &rv);
  if (!t) {
    if (r) *r = res[0];
    return 0;
  }
  wasm_message_t m;
  wasm_trap_message(t, &m);
  snprintf(msg, cap, "%.*s", (int)(m.size && !m.data[m.size - 1] ? m.size - 1 : m.size), m.data);
  wasm_byte_vec_delete(&m);
  wasm_trap_delete(t);
  return 1;
}

/* ---- the host's functions ---- */

typedef struct {
  int tid;
  wasm_func_t *own_inc;    /* the store's own, called back into */
  wasm_func_t *other_inc;  /* the thread's second store's */
} Ctx;

/* waits until every thread is in one, or until ten seconds after the
   first came: 1 when all were.  The deadline is one for all, so that
   threads let in one at a time fail in ten seconds, not ten each. */
static atomic_int rendezvous_ms;
static wasm_trap_t *rendezvous(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *res) {
  (void)env;
  (void)args;
  atomic_fetch_add(&inside, 1);
  int ok = 0;
  for (;;) {
    if (atomic_load(&inside) >= nthreads) { ok = 1; break; }
    if (atomic_fetch_add(&rendezvous_ms, 1) >= 10000) break;
    nap_ms(1);
  }
  res->data[0].kind = WASM_I32;
  res->data[0].of.i32 = ok;
  return NULL;
}

/* x + 1 from its own store and x + 1 from the other, added */
static wasm_trap_t *callback(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *res) {
  Ctx *c = env;
  char msg[128];
  wasm_val_t a = WASM_I32_VAL(args->data[0].of.i32), r1, r2;
  if (call(c->own_inc, &a, 1, &r1, msg, sizeof msg) ||
      call(c->other_inc, &a, 1, &r2, msg, sizeof msg)) {
    fail(c->tid, "a call back into wasm trapped: %s%lld", msg, 0);
    r1.of.i32 = r2.of.i32 = 0;
  }
  res->data[0].kind = WASM_I32;
  res->data[0].of.i32 = r1.of.i32 + r2.of.i32;
  return NULL;
}

static wasm_func_t *host_func(wasm_store_t *s, int nparams,
                              wasm_func_callback_with_env_t cb, void *env) {
  wasm_valtype_vec_t ps, rs;
  wasm_valtype_t *p[1] = { wasm_valtype_new_i32() }, *r[1] = { wasm_valtype_new_i32() };
  wasm_valtype_vec_new(&ps, (size_t)nparams, p);
  wasm_valtype_vec_new(&rs, 1, r);
  wasm_functype_t *ft = wasm_functype_new(&ps, &rs);
  wasm_func_t *f = wasm_func_new_with_env(s, ft, cb, env, NULL);
  wasm_functype_delete(ft);
  return f;
}

/* ---- what C expects ---- */

static long long fill_sum(uint32_t x, int n) {
  long long s = 0;
  for (int i = 0; i < n; i++) {
    x = x * 1103515245u + 12345u;
    s += x;
  }
  return s;
}

static long long churn_sum(int rounds, int n) {
  return (long long)n * (n - 1) / 2 + (long long)n * (rounds - 1) + n;
}

static int bytes_sum(const char *const *v, int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    for (const char *p = v[i];; p++) {
      s += (unsigned char)*p;
      if (!*p) break;
    }
  return s;
}

/* ---- a thread ---- */

static int expect_trap(int tid, wasm_func_t *f, const wasm_val_t *a, size_t n,
                       const char *want) {
  char msg[160];
  wasm_val_t r;
  if (!call(f, a, n, &r, msg, sizeof msg)) {
    fail(tid, "expected the trap \"%s\" and returned %lld", want, r.of.i32);
    return 0;
  }
  if (strcmp(msg, want)) {
    fail(tid, "expected the trap \"%s\", trapped otherwise%lld", want, 0);
    fprintf(stderr, "stores: thread %d: ... with \"%s\"\n", tid, msg);
    return 0;
  }
  return 1;
}

static void one_round(int tid, int round) {
  char msg[160];
  wasm_val_t r;
  wasm_store_t *s = wasm_store_new(engine), *s2 = wasm_store_new(engine);

  /* the second store: an inc, which the callback calls into */
  wasm_module_t *minc = wasm_module_obtain(s2, m_inc);
  wasm_extern_vec_t none = WASM_EMPTY_VEC, ex2;
  wasm_instance_t *i2 = wasm_instance_new(s2, minc, &none, NULL);
  wasm_instance_exports(i2, &ex2);
  Ctx ctx = { tid, NULL, export_func(&ex2, minc, "inc") };

  wasm_module_t *mw = wasm_module_obtain(s, m_work);
  wasm_func_t *rv = host_func(s, 1, rendezvous, NULL), *cb = host_func(s, 1, callback, &ctx);
  wasm_extern_t *imp[2] = { wasm_func_as_extern(rv), wasm_func_as_extern(cb) };
  wasm_extern_vec_t iv = WASM_ARRAY_VEC(imp), ex;
  wasm_instance_t *iw = wasm_instance_new(s, mw, &iv, NULL);
  if (!iw) { fail(tid, "the work module does not instantiate%s%lld", "", 0); goto out; }
  wasm_instance_exports(iw, &ex);
  ctx.own_inc = export_func(&ex, mw, "inc");

  if (round == 0) {
    wasm_val_t a = WASM_I32_VAL(tid);
    if (call(export_func(&ex, mw, "rendezvous"), &a, 1, &r, msg, sizeof msg) || r.of.i32 != 1)
      fail(tid, "not every thread was in wasm at once%s (%lld were)", "",
           (long long)atomic_load(&inside));
  }

  wasm_val_t fa[2] = { WASM_I32_VAL(tid * 7919 + round), WASM_I32_VAL(FILL_N) };
  if (call(export_func(&ex, mw, "fill"), fa, 2, &r, msg, sizeof msg) ||
      r.of.i64 != fill_sum((uint32_t)(tid * 7919 + round), FILL_N))
    fail(tid, "fill's sum is wrong%s: %lld", "", (long long)r.of.i64);

  wasm_val_t d1[2] = { WASM_I32_VAL(7 + tid), WASM_I32_VAL(1) }, d0[2] = { WASM_I32_VAL(tid), WASM_I32_VAL(0) };
  if (call(export_func(&ex, mw, "div"), d1, 2, &r, msg, sizeof msg) || r.of.i32 != 7 + tid)
    fail(tid, "div is wrong%s: %lld", "", r.of.i32);
  expect_trap(tid, export_func(&ex, mw, "div"), d0, 2, "integer divide by zero");
  wasm_val_t pk = WASM_I32_VAL(65536 + 64 * tid);
  expect_trap(tid, export_func(&ex, mw, "peek"), &pk, 1, "out of bounds memory access");
  wasm_val_t z = WASM_I32_VAL(0);
  expect_trap(tid, export_func(&ex, mw, "deep"), &z, 1, "call stack exhausted");

  wasm_val_t c = WASM_I32_VAL(tid);
  if (call(export_func(&ex, mw, "catch"), &c, 1, &r, msg, sizeof msg) || r.of.i32 != tid + 1)
    fail(tid, "catch is wrong%s: %lld", "", r.of.i32);
  expect_trap(tid, export_func(&ex, mw, "throw"), &c, 1, "uncaught exception");

  if (call(export_func(&ex, mw, "twice"), &c, 1, &r, msg, sizeof msg) ||
      r.of.i32 != 2 * (2 * tid + 2))
    fail(tid, "twice is wrong%s: %lld", "", r.of.i32);
  expect_trap(tid, export_func(&ex, mw, "cb_then_trap"), &c, 1, "integer divide by zero");

  /* the store still works after every trap */
  if (call(export_func(&ex, mw, "fill"), fa, 2, &r, msg, sizeof msg) ||
      r.of.i64 != fill_sum((uint32_t)(tid * 7919 + round), FILL_N))
    fail(tid, "fill after the traps is wrong%s: %lld", "", (long long)r.of.i64);
  wasm_extern_vec_delete(&ex);

  /* GC objects by the megabyte, collected in this store's heap */
  wasm_module_t *mg = wasm_module_obtain(s, m_gc);
  wasm_instance_t *ig = wasm_instance_new(s, mg, &none, NULL);
  wasm_extern_vec_t eg;
  wasm_instance_exports(ig, &eg);
  wasm_val_t ga[2] = { WASM_I32_VAL(CHURN_ROUNDS), WASM_I32_VAL(CHURN_N + tid) };
  if (call(export_func(&eg, mg, "churn"), ga, 2, &r, msg, sizeof msg))
    fail(tid, "churn trapped: %s%lld", msg, 0);
  else if (r.of.i64 != churn_sum(CHURN_ROUNDS, CHURN_N + tid))
    fail(tid, "churn's sum is wrong%s: %lld", "", (long long)r.of.i64);
  wasm_extern_vec_delete(&eg);

  /* WASI's arguments, this store's */
  char a1[64];
  snprintf(a1, sizeof a1, "store-%d-%d", tid, round);
  const char *argv[2] = { "stores", a1 };
  warm_wasi_config_t *wc = warm_wasi_config_new();
  warm_wasi_config_set_argv(wc, 2, argv);
  wasm_module_t *ma = wasm_module_obtain(s, m_args);
  wasm_extern_vec_t wi, ea;
  if (!warm_wasi_imports(s, ma, wc, &wi)) {
    fail(tid, "WASI does not set up%s%lld", "", 0);
  } else {
    wasm_instance_t *ia = wasm_instance_new(s, ma, &wi, NULL);
    wasm_instance_exports(ia, &ea);
    if (call(export_func(&ea, ma, "argsum"), NULL, 0, &r, msg, sizeof msg) ||
        r.of.i32 != bytes_sum(argv, 2))
      fail(tid, "WASI's arguments are another store's%s: %lld", "", r.of.i32);
    wasm_extern_vec_delete(&ea);
    wasm_instance_delete(ia);
    wasm_extern_vec_delete(&wi);
  }
  warm_wasi_config_delete(wc);
  wasm_module_delete(ma);

  /* the store every thread calls into */
  for (int k = 0; k < BUMPS; k++) {
    wasm_val_t n = WASM_I32_VAL(BUMP_N);
    if (call(shared_bump, &n, 1, NULL, msg, sizeof msg))
      fail(tid, "bump trapped: %s%lld", msg, 0);
  }

  wasm_instance_delete(ig);
  wasm_module_delete(mg);
  wasm_instance_delete(iw);
out:
  wasm_func_delete(rv);
  wasm_func_delete(cb);
  wasm_module_delete(mw);
  wasm_extern_vec_delete(&ex2);
  wasm_instance_delete(i2);
  wasm_module_delete(minc);
  wasm_store_delete(s);
  wasm_store_delete(s2);
}

static void *thread_main(void *arg) {
  int tid = (int)(intptr_t)arg;
  for (int round = 0; round < nrounds; round++) one_round(tid, round);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc > 1) nthreads = atoi(argv[1]);
  if (argc > 2) nrounds = atoi(argv[2]);
  /* collections early and often: every store's heap collects many times */
  env_set("WARMC_GC_MIN_KB", "256");
  engine = wasm_engine_new();
  compile(WORK, &m_work);
  compile(GC, &m_gc);
  compile(ARGS, &m_args);
  compile(INC, &m_inc);
  compile(COUNTER, &m_counter);

  shared_store = wasm_store_new(engine);
  wasm_module_t *mc = wasm_module_obtain(shared_store, m_counter);
  wasm_extern_vec_t none = WASM_EMPTY_VEC, ec;
  wasm_instance_t *ic = wasm_instance_new(shared_store, mc, &none, NULL);
  wasm_instance_exports(ic, &ec);
  shared_bump = export_func(&ec, mc, "bump");

  /* an import from another store is refused */
  {
    wasm_store_t *a = wasm_store_new(engine), *b = wasm_store_new(engine);
    wasm_module_t *ma = wasm_module_obtain(a, m_inc);
    wasm_instance_t *ia = wasm_instance_new(a, ma, &none, NULL);
    wasm_extern_vec_t ea;
    wasm_instance_exports(ia, &ea);
    wasm_byte_vec_t bin;
    warm_wat2wasm(IMPORTER, strlen(IMPORTER), &bin, NULL);
    wasm_module_t *mb = wasm_module_new(b, &bin);
    wasm_byte_vec_delete(&bin);
    wasm_message_t err;
    wasm_instance_t *ib = warm_instance_new(b, mb, &ea, NULL, &err);
    if (ib || !err.size || !strstr(err.data, "another store")) {
      fprintf(stderr, "stores: an import from another store was not refused (%.*s)\n",
              (int)err.size, err.data);
      atomic_fetch_add(&failures, 1);
    }
    if (ib) wasm_instance_delete(ib);
    wasm_byte_vec_delete(&err);
    /* nor may b's table hold a's function */
    wasm_limits_t lim = { 1, 1 };
    wasm_tabletype_t *tt = wasm_tabletype_new(wasm_valtype_new(WASM_FUNCREF), &lim);
    wasm_table_t *tb = wasm_table_new(b, tt, NULL);
    wasm_tabletype_delete(tt);
    if (!tb || wasm_table_set(tb, 0, wasm_func_as_ref(wasm_extern_as_func(ea.data[0])))) {
      fprintf(stderr, "stores: a table took another store's function\n");
      atomic_fetch_add(&failures, 1);
    }
    if (tb) wasm_table_delete(tb);
    wasm_module_delete(mb);
    wasm_extern_vec_delete(&ea);
    wasm_instance_delete(ia);
    wasm_module_delete(ma);
    wasm_store_delete(a);
    wasm_store_delete(b);
  }

  pthread_t *th = calloc((size_t)nthreads, sizeof *th);
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 2u << 20);
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (int i = 0; i < nthreads; i++)
    if (pthread_create(&th[i], &at, thread_main, (void *)(intptr_t)i)) {
      fprintf(stderr, "stores: cannot start thread %d\n", i);
      return 1;
    }
  for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
  clock_gettime(CLOCK_MONOTONIC, &t1);

  wasm_val_t r;
  char msg[64];
  wasm_func_t *get = export_func(&ec, mc, "get");
  long long want = (long long)nthreads * nrounds * BUMPS * BUMP_N;
  if (call(get, NULL, 0, &r, msg, sizeof msg) || r.of.i64 != want) {
    fprintf(stderr, "stores: the shared store's counter is %lld, not %lld\n",
            (long long)r.of.i64, want);
    atomic_fetch_add(&failures, 1);
  }
  wasm_extern_vec_delete(&ec);
  wasm_instance_delete(ic);
  wasm_module_delete(mc);
  wasm_store_delete(shared_store);
  wasm_shared_module_delete(m_work);
  wasm_shared_module_delete(m_gc);
  wasm_shared_module_delete(m_args);
  wasm_shared_module_delete(m_inc);
  wasm_shared_module_delete(m_counter);
  wasm_engine_delete(engine);
  free(th);

  int f = atomic_load(&failures);
  if (f) {
    printf("stores: %d failure(s)\n", f);
    return 1;
  }
  double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
  printf("stores: %d threads, %d rounds of two stores each, all at once; every result its "
         "own store's (%.0f ms)\n", nthreads, nrounds, ms);
  return 0;
}
