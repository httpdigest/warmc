/* Stack switching through the C API, on many threads at once: each thread
   makes stores of its own from one module compiled once, and in each

     * a generator started by one call and resumed by the calls after it,
       its continuation in a global between them, holding a list of GC
       objects it checks every time it runs again -- with the store's heap
       collecting meanwhile, on the collector's thread, while no wasm of the
       store runs;
     * a trap in another fiber, after which the generator goes on;
     * a host function called on a fiber's stack, which calls back into the
       store: a generator run to its end in the nested call, and a
       suspension there that no handler of that call takes -- an unhandled
       tag, though the fiber the host was called from is under a resume
       that handles it -- and the fiber goes on once the host returns;
     * the store deleted with fibers still waiting in it, round after round.

   cont [THREADS [ROUNDS]] prints one line and exits 0, or names what went
   wrong and exits 1. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm.h"
#include "warm.h"

static const char MOD[] =
  "(module\n"
  "  (import \"host\" \"reenter\" (func $reenter (param i32) (result i32)))\n"
  "  (type $f_v (func)) (type $c_v (cont $f_v))\n"
  "  (type $f_i (func (param i32))) (type $c_i (cont $f_i))\n"
  "  (type $f_r_i (func (result i32))) (type $c_r_i (cont $f_r_i))\n"
  "  (type $f_i_r_i (func (param i32) (result i32))) (type $c_i_r_i (cont $f_i_r_i))\n"
  "  (type $node (struct (field $v i32) (field $next (ref null $node))))\n"
  "  (type $garr (array (mut i64)))\n"
  "  (tag $yield (param i32))\n"
  "  (global $k (mut (ref null $c_v)) (ref.null $c_v))\n"
  "  (global $pending (mut i32) (i32.const -1))\n"
  "  (elem declare func $gen $boom $calls_host $plain_gen)\n"
  "  (func $mklist (param $n i32) (result (ref null $node))\n"
  "    (local $h (ref null $node))\n"
  "    (loop $l (if (local.get $n) (then\n"
  "      (local.set $h (struct.new $node (local.get $n) (local.get $h)))\n"
  "      (local.set $n (i32.sub (local.get $n) (i32.const 1))) (br $l))))\n"
  "    (local.get $h))\n"
  "  (func $sumlist (param $h (ref null $node)) (result i32)\n"
  "    (local $s i32)\n"
  "    (block $e (loop $l\n"
  "      (br_if $e (ref.is_null (local.get $h)))\n"
  "      (local.set $s (i32.add (local.get $s) (struct.get $node $v (local.get $h))))\n"
  "      (local.set $h (struct.get $node $next (local.get $h))) (br $l)))\n"
  "    (local.get $s))\n"
  /* garbage the next array makes of the last, kept in a global so that no
     optimiser drops it */
  "  (global $sink (mut (ref null $garr)) (ref.null $garr))\n"
  "  (func $churn (export \"churn\") (param $n i32)\n"
  "    (loop $l (if (local.get $n) (then\n"
  "      (global.set $sink (array.new $garr (i64.const 7) (i32.const 64)))\n"
  "      (local.set $n (i32.sub (local.get $n) (i32.const 1))) (br $l)))))\n"
  /* yields 0 .. n-1, its own list checked each time it runs again */
  "  (func $gen (param $n i32)\n"
  "    (local $i i32) (local $l (ref null $node))\n"
  "    (local.set $l (call $mklist (i32.const 20)))\n"
  "    (block $done (loop $next\n"
  "      (br_if $done (i32.ge_u (local.get $i) (local.get $n)))\n"
  "      (suspend $yield (local.get $i))\n"
  "      (if (i32.ne (call $sumlist (local.get $l)) (i32.const 210)) (then (unreachable)))\n"
  "      (call $churn (i32.const 8))\n"
  "      (local.set $i (i32.add (local.get $i) (i32.const 1)))\n"
  "      (br $next))))\n"
  /* a resume of $k: the value it yields kept for the next call, or -1 */
  "  (func $step (param $k (ref null $c_v))\n"
  "    (block $done\n"
  "      (block $y (result i32 (ref $c_v))\n"
  "        (resume $c_v (on $yield $y) (local.get $k))\n"
  "        (global.set $k (ref.null $c_v)) (global.set $pending (i32.const -1)) (br $done))\n"
  "      (global.set $k) (global.set $pending)))\n"
  "  (func (export \"start\") (param $n i32)\n"
  "    (block $done\n"
  "      (block $y (result i32 (ref $c_v))\n"
  "        (resume $c_i (on $yield $y) (local.get $n) (cont.new $c_i (ref.func $gen)))\n"
  "        (global.set $k (ref.null $c_v)) (global.set $pending (i32.const -1)) (br $done))\n"
  "      (global.set $k) (global.set $pending)))\n"
  "  (func (export \"next\") (result i32)\n"
  "    (local $r i32)\n"
  "    (local.set $r (global.get $pending))\n"
  "    (if (i32.ge_s (local.get $r) (i32.const 0)) (then (call $step (global.get $k))))\n"
  "    (local.get $r))\n"
  "  (func $boom (unreachable))\n"
  "  (func (export \"trap_in_fiber\") (resume $c_v (cont.new $c_v (ref.func $boom))))\n"
  /* the sum of 0 .. n-1 from a generator of its own */
  "  (func $plain_gen (param $n i32)\n"
  "    (local $i i32)\n"
  "    (block $done (loop $l\n"
  "      (br_if $done (i32.ge_u (local.get $i) (local.get $n)))\n"
  "      (suspend $yield (local.get $i))\n"
  "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $l))))\n"
  "  (func (export \"inner\") (param $n i32) (result i32)\n"
  "    (local $k (ref null $c_v)) (local $s i32)\n"
  "    (block $done\n"
  "      (block $y (result i32 (ref $c_v))\n"
  "        (resume $c_i (on $yield $y) (local.get $n) (cont.new $c_i (ref.func $plain_gen)))\n"
  "        (br $done))\n"
  "      (local.set $k) (local.set $s (i32.add (local.get $s)))\n"
  "      (loop $l\n"
  "        (block $y2 (result i32 (ref $c_v))\n"
  "          (resume $c_v (on $yield $y2) (local.get $k)) (br $done))\n"
  "        (local.set $k) (local.set $s (i32.add (local.get $s))) (br $l)))\n"
  "    (local.get $s))\n"
  "  (func (export \"suspend_out\") (suspend $yield (i32.const 7)))\n"
  "  (func $calls_host (param $n i32) (result i32)\n"
  "    (i32.add (call $reenter (local.get $n)) (i32.const 1)))\n"
  /* a yield of the fiber's would come to $y: the host's nested call's may not */
  "  (func (export \"reenter_in_fiber\") (param $n i32) (result i32)\n"
  "    (block $y (result i32 (ref $c_r_i))\n"
  "      (return (resume $c_i_r_i (on $yield $y) (local.get $n)\n"
  "                      (cont.new $c_i_r_i (ref.func $calls_host)))))\n"
  "    (drop) (drop) (i32.const -999)))\n";

static wasm_engine_t *engine;
static wasm_shared_module_t *shared;
static atomic_int failures;

static void fail(int tid, const char *what, long long a, long long b) {
  if (atomic_fetch_add(&failures, 1) < 20)
    fprintf(stderr, "cont: thread %d: %s (%lld, %lld)\n", tid, what, a, b);
}

typedef struct {
  int tid;
  wasm_func_t *inner, *suspend_out;
} Ctx;

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
  if (!f) { fprintf(stderr, "cont: no export %s\n", name); exit(1); }
  return f;
}

/* f(args) with nargs i32 values: 0 and its result in *r, or 1 and its
   trap's message in msg */
static int call(wasm_func_t *f, const wasm_val_t *args, size_t nargs, int32_t *r,
                char *msg, size_t cap) {
  wasm_val_vec_t av = { nargs, (wasm_val_t *)args };
  wasm_val_t res[1] = { WASM_INIT_VAL };
  wasm_val_vec_t rv = { wasm_func_result_arity(f), res };
  wasm_trap_t *t = wasm_func_call(f, &av, &rv);
  if (!t) {
    if (r) *r = res[0].of.i32;
    return 0;
  }
  wasm_message_t m;
  wasm_trap_message(t, &m);
  snprintf(msg, cap, "%.*s", (int)(m.size && !m.data[m.size - 1] ? m.size - 1 : m.size),
           m.data);
  wasm_byte_vec_delete(&m);
  wasm_trap_delete(t);
  return 1;
}

/* reenter(n): on the stack of the fiber that called it, a generator run to
   its end in a call of its own, and a suspension in another that nothing
   in that call handles; the sum, and 1000 more when that one trapped */
static wasm_trap_t *reenter(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *res) {
  Ctx *c = env;
  char msg[160];
  int32_t sum = -1;
  wasm_val_t a = WASM_I32_VAL(args->data[0].of.i32);
  if (call(c->inner, &a, 1, &sum, msg, sizeof msg)) {
    fail(c->tid, "the nested generator trapped", 0, 0);
    fprintf(stderr, "cont: ... %s\n", msg);
  }
  int trapped = call(c->suspend_out, NULL, 0, NULL, msg, sizeof msg);
  if (trapped && !strstr(msg, "unhandled tag")) {
    fail(c->tid, "the nested suspension trapped otherwise", 0, 0);
    fprintf(stderr, "cont: ... %s\n", msg);
  }
  res->data[0].kind = WASM_I32;
  res->data[0].of.i32 = sum + (trapped ? 1000 : 0);
  return NULL;
}

static int nrounds = 6, ngen = 60;

static void *thread_main(void *arg) {
  int tid = (int)(intptr_t)arg;
  char msg[160];
  for (int round = 0; round < nrounds; round++) {
    wasm_store_t *s = wasm_store_new(engine);
    wasm_module_t *m = wasm_module_obtain(s, shared);
    Ctx ctx = { tid, NULL, NULL };
    wasm_valtype_t *p[1] = { wasm_valtype_new_i32() }, *r[1] = { wasm_valtype_new_i32() };
    wasm_valtype_vec_t ps, rs;
    wasm_valtype_vec_new(&ps, 1, p);
    wasm_valtype_vec_new(&rs, 1, r);
    wasm_functype_t *ft = wasm_functype_new(&ps, &rs);
    wasm_func_t *host = wasm_func_new_with_env(s, ft, reenter, &ctx, NULL);
    wasm_functype_delete(ft);
    wasm_extern_t *imps[1] = { wasm_func_as_extern(host) };
    wasm_extern_vec_t iv = { 1, imps };
    wasm_instance_t *in = wasm_instance_new(s, m, &iv, NULL);
    if (!in) { fail(tid, "no instance", round, 0); break; }
    wasm_extern_vec_t ex;
    wasm_instance_exports(in, &ex);
    wasm_func_t *start = export_func(&ex, m, "start"), *next = export_func(&ex, m, "next");
    wasm_func_t *churn = export_func(&ex, m, "churn");
    wasm_func_t *boom = export_func(&ex, m, "trap_in_fiber");
    wasm_func_t *refib = export_func(&ex, m, "reenter_in_fiber");
    ctx.inner = export_func(&ex, m, "inner");
    ctx.suspend_out = export_func(&ex, m, "suspend_out");

    wasm_val_t n = WASM_I32_VAL(ngen), many = WASM_I32_VAL(400), ten = WASM_I32_VAL(10);
    if (call(start, &n, 1, NULL, msg, sizeof msg)) fail(tid, "start trapped", round, 0);
    /* the last round's store goes with its generator half run */
    int upto = round == nrounds - 1 ? ngen / 2 : ngen;
    for (int i = 0; i < upto; i++) {
      int32_t got = -2;
      if (call(next, NULL, 0, &got, msg, sizeof msg)) {
        fail(tid, "next trapped", round, i);
        fprintf(stderr, "cont: ... %s\n", msg);
        break;
      }
      if (got != i) { fail(tid, "next gave the wrong value", got, i); break; }
      if (i % 7 == 0 && call(churn, &many, 1, NULL, msg, sizeof msg))
        fail(tid, "churn trapped", round, i);
      if (i % 17 == 3) {
        if (!call(boom, NULL, 0, NULL, msg, sizeof msg))
          fail(tid, "the fiber's unreachable returned", round, i);
        else if (!strstr(msg, "unreachable"))
          fail(tid, "the fiber trapped otherwise", round, i);
      }
      if (i % 23 == 5) {
        int32_t v = 0;
        if (call(refib, &ten, 1, &v, msg, sizeof msg)) {
          fail(tid, "reenter_in_fiber trapped", round, i);
          fprintf(stderr, "cont: ... %s\n", msg);
        } else if (v != 45 + 1000 + 1) {
          fail(tid, "reenter_in_fiber gave the wrong value", v, 1046);
        }
      }
    }
    if (upto == ngen) {
      /* the call that took the last value ran the generator to its end */
      int32_t end = 0;
      if (call(next, NULL, 0, &end, msg, sizeof msg) || end != -1)
        fail(tid, "the generator did not end", end, -1);
    }
    wasm_extern_vec_delete(&ex);
    wasm_instance_delete(in);
    wasm_func_delete(host);
    wasm_module_delete(m);
    wasm_store_delete(s);
  }
  return NULL;
}

int main(int argc, char **argv) {
  int nthreads = argc > 1 ? atoi(argv[1]) : 24;
  if (argc > 2) nrounds = atoi(argv[2]);
  engine = wasm_engine_new();
  wasm_byte_vec_t bin;
  wasm_message_t err;
  if (!warm_wat2wasm(MOD, strlen(MOD), &bin, &err)) {
    fprintf(stderr, "cont: %.*s\n", (int)err.size, err.data);
    return 1;
  }
  wasm_store_t *s = wasm_store_new(engine);
  wasm_module_t *m = warm_module_new(s, &bin, &err);
  if (!m) {
    fprintf(stderr, "cont: %.*s\n", (int)err.size, err.data);
    return 1;
  }
  wasm_byte_vec_delete(&bin);
  shared = wasm_module_share(m);
  wasm_module_delete(m);
  wasm_store_delete(s);

  pthread_t *t = calloc((size_t)nthreads, sizeof *t);
  for (int i = 0; i < nthreads; i++)
    pthread_create(&t[i], NULL, thread_main, (void *)(intptr_t)i);
  for (int i = 0; i < nthreads; i++) pthread_join(t[i], NULL);
  free(t);
  wasm_shared_module_delete(shared);
  wasm_engine_delete(engine);
  int f = atomic_load(&failures);
  if (f) {
    fprintf(stderr, "cont: %d failures\n", f);
    return 1;
  }
  printf("cont: %d threads, %d rounds each of a %d-value generator across calls, "
         "traps, re-entry and stores deleted with fibers waiting\n",
         nthreads, nrounds, ngen);
  return 0;
}
