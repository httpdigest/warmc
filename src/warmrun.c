/* warmrun: a module run through libwarmc, the JIT, as a program warmc links
   runs it -- written against the C API alone (include/wasm.h and
   include/warm.h), as any embedder would be.

     warmrun [OPTIONS] MODULE [ARGS...]

   A module that imports wasi_snapshot_preview1 is a WASI command: ARGS are
   its arguments, its _start runs, and its exit status is warmrun's, as with
   `warmc --wasi`.  Any other is run as warmc's CLI harness runs it: ARGS
   name exports, each followed by its arguments and separated by --, and
   each result is printed as the harness prints it (i32:7, f64:0x...).
   MODULE is binary or text.

   OPTIONS: -O<n> and -f[no-]<pass> as warmc takes them, and -j<n>, how many
   threads compile at once (the fewer, the less memory a compile takes at
   its peak, and the longer); --tiered to start the module at -O0 and send
   its calls to the code -O<n> makes once it is ready
   (warm_config_tiered_set); --dir=HOST[::GUEST],
   --env=NAME=VALUE, --inherit-env and --tcplisten=[ADDR:]PORT for a WASI
   command; --serialize=FILE to write the compiled module there, and
   --precompiled when MODULE is such a file. */

#include "wasm.h"
#include "warm.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(int code, const char *what, const char *msg) {
  fflush(stdout);
  fprintf(stderr, "warmrun: %s%s%s\n", what, msg ? ": " : "", msg ? msg : "");
  exit(code);
}

static void read_file(const char *path, wasm_byte_vec_t *out) {
  FILE *f = fopen(path, "rb");
  if (!f) die(1, "cannot open", path);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) die(1, "cannot read", path);
  wasm_byte_vec_new_uninitialized(out, (size_t)n);
  if (n && fread(out->data, 1, (size_t)n, f) != (size_t)n) die(1, "cannot read", path);
  fclose(f);
}

static void write_file(const char *path, const wasm_byte_vec_t *b) {
  FILE *f = fopen(path, "wb");
  if (!f || (b->size && fwrite(b->data, 1, b->size, f) != b->size) || fclose(f))
    die(1, "cannot write", path);
}

static bool is_text(const wasm_byte_vec_t *b) {
  return b->size < 4 || memcmp(b->data, "\0asm", 4) != 0;
}

static uint64_t parse_u64(const char *s) {
  bool neg = false;
  if (*s == '-') { neg = true; s++; }
  else if (*s == '+') s++;
  uint64_t v = 0;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    for (s += 2; *s; s++) {
      int d;
      if (*s >= '0' && *s <= '9') d = *s - '0';
      else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
      else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
      else break;
      v = v * 16 + (uint64_t)d;
    }
  } else {
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint64_t)(*s++ - '0');
  }
  return neg ? (uint64_t)-(int64_t)v : v;
}

static void print_val(wasm_valkind_t k, const warm_val_raw_t *v) {
  switch (k) {
    case WASM_I32: printf("i32:%" PRId64, (int64_t)v->i32); break;
    case WASM_I64: printf("i64:%" PRId64, v->i64); break;
    case WASM_F32: { uint32_t b; memcpy(&b, &v->f32, 4); printf("f32:0x%08" PRIx32, b); break; }
    case WASM_F64: { uint64_t b; memcpy(&b, &v->f64, 8); printf("f64:0x%016" PRIx64, b); break; }
    case WARM_V128: {
      uint64_t lo, hi;
      memcpy(&lo, v->v128, 8);
      memcpy(&hi, v->v128 + 8, 8);
      printf("v128:0x%016" PRIx64 "%016" PRIx64, hi, lo);
      break;
    }
    default: printf("ref:0x%016" PRIx64, (uint64_t)(uintptr_t)v->ref); break;
  }
}

static const wasm_extern_t *find_export(const wasm_module_t *m, const wasm_extern_vec_t *ex,
                                        const char *name) {
  wasm_exporttype_vec_t types;
  wasm_module_exports(m, &types);
  const wasm_extern_t *found = NULL;
  for (size_t i = 0; i < types.size && !found; i++) {
    const wasm_name_t *n = wasm_exporttype_name(types.data[i]);
    if (n->size == strlen(name) && !memcmp(n->data, name, n->size)) found = ex->data[i];
  }
  wasm_exporttype_vec_delete(&types);
  return found;
}

/* trap, as the program warmc links reports it; never returns */
static void report_trap(wasm_trap_t *t, bool wasi) {
  int status;
  if (warm_trap_exit_status(t, &status)) {
    fflush(stdout);
    exit(status);
  }
  wasm_message_t msg;
  wasm_trap_message(t, &msg);
  fflush(stdout);
  if (wasi) fprintf(stderr, "warmc-wasi: trap: %s\n", msg.data);
  else fprintf(stderr, "trap: %s\n", msg.data);
  exit(wasi ? 73 : 1);
}

/* An export called as the CLI harness calls it, its arguments the bits of
   each parameter; 2 when it cannot be */
static int call_export(const wasm_func_t *f, int argc, char **argv) {
  wasm_functype_t *ft = wasm_func_type(f);
  const wasm_valtype_vec_t *ps = wasm_functype_params(ft), *rs = wasm_functype_results(ft);
  if ((size_t)argc < ps->size) die(2, "not enough arguments", NULL);
  size_t n = ps->size > rs->size ? ps->size : rs->size;
  warm_val_raw_t *vals = calloc(n ? n : 1, sizeof *vals);
  for (size_t i = 0; i < ps->size; i++) {
    uint64_t bits = parse_u64(argv[i]);
    switch (wasm_valtype_kind(ps->data[i])) {
      case WASM_I32: case WASM_I64: case WASM_F32: case WASM_F64:
        memcpy(&vals[i], &bits, 8);
        break;
      case WASM_EXTERNREF: case WASM_FUNCREF: vals[i].ref = NULL; break;
      default: die(2, "a parameter the command line cannot give", NULL);
    }
  }
  wasm_trap_t *t = warm_func_call_raw(f, vals, n);
  if (t) report_trap(t, false);
  for (size_t i = 0; i < rs->size; i++) {
    if (i) printf(" ");
    print_val(wasm_valtype_kind(rs->data[i]), &vals[i]);
  }
  printf("\n");
  fflush(stdout);
  free(vals);
  wasm_functype_delete(ft);
  return 0;
}

int main(int argc, char **argv) {
  wasm_config_t *config = wasm_config_new();
  warm_wasi_config_t *wasi = warm_wasi_config_new();
  const char *serialize = NULL;
  bool precompiled = false;
  int i = 1;
  for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "--")) { i++; break; }
    if (a[1] == 'O' || a[1] == 'f') warm_config_flag_add(config, a);
    else if (a[1] == 'j') {
      const char *v = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : "");
      char *end;
      long n = strtol(v, &end, 10);
      if (!*v || *end || n < 1) die(2, "-j wants a thread count of 1 or more", NULL);
      warm_config_compile_threads_set(config, (uint32_t)n);
    }
    else if (!strncmp(a, "--dir=", 6)) {
      const char *spec = a + 6, *sep = strstr(spec, "::");
      char host[4096];
      size_t n = sep ? (size_t)(sep - spec) : strlen(spec);
      if (n >= sizeof host) die(2, "a directory name too long", spec);
      memcpy(host, spec, n);
      host[n] = 0;
      if (!warm_wasi_config_preopen_dir(wasi, host, sep ? sep + 2 : NULL))
        die(2, "cannot preopen", spec);
    }
    else if (!strncmp(a, "--env=", 6)) warm_wasi_config_env_add(wasi, a + 6);
    else if (!strcmp(a, "--inherit-env")) warm_wasi_config_inherit_env(wasi);
    else if (!strncmp(a, "--tcplisten=", 12)) warm_wasi_config_tcplisten(wasi, a + 12);
    else if (!strncmp(a, "--serialize=", 12)) serialize = a + 12;
    else if (!strcmp(a, "--precompiled")) precompiled = true;
    else if (!strcmp(a, "--tiered")) warm_config_tiered_set(config, true);
    else die(2, "unknown option", a);
  }
  if (i >= argc) {
    fputs("usage: warmrun [-O<n>] [-f<pass>] [-j<n>] [--tiered] [--dir=HOST[::GUEST]] [--env=K=V] "
          "[--inherit-env]\n               [--tcplisten=ADDR] [--serialize=FILE] "
          "[--precompiled] MODULE [ARGS...]\n", stderr);
    return 2;
  }
  const char *path = argv[i++];

  wasm_engine_t *engine = wasm_engine_new_with_config(config);
  if (!engine) die(1, "this machine cannot run warmc's code", NULL);
  wasm_store_t *store = wasm_store_new(engine);

  wasm_byte_vec_t bin;
  read_file(path, &bin);
  wasm_module_t *module;
  if (precompiled) {
    module = wasm_module_deserialize(store, &bin);
    if (!module) die(1, "not a module this warmc serialised", path);
  } else {
    if (is_text(&bin)) {
      wasm_byte_vec_t text = bin;
      wasm_message_t err;
      if (!warm_wat2wasm(text.data, text.size, &bin, &err)) die(1, path, err.data);
      wasm_byte_vec_delete(&text);
    }
    wasm_message_t err;
    module = warm_module_new(store, &bin, &err);
    if (!module) die(1, path, err.data);
    wasm_byte_vec_delete(&err);
  }
  wasm_byte_vec_delete(&bin);
  if (serialize) {
    wasm_byte_vec_t s;
    wasm_module_serialize(module, &s);
    write_file(serialize, &s);
    wasm_byte_vec_delete(&s);
  }

  wasm_importtype_vec_t imports;
  wasm_module_imports(module, &imports);
  bool is_wasi = false;
  for (size_t k = 0; k < imports.size; k++) {
    const wasm_name_t *m = wasm_importtype_module(imports.data[k]);
    if (m->size == 22 && !memcmp(m->data, "wasi_snapshot_preview1", 22)) is_wasi = true;
  }

  wasm_extern_vec_t externs;
  if (is_wasi) {
    int n = argc - i + 1;
    const char **pargv = malloc(sizeof(char *) * (size_t)n);
    pargv[0] = path;
    for (int k = 1; k < n; k++) pargv[k] = argv[i + k - 1];
    warm_wasi_config_set_argv(wasi, (size_t)n, pargv);
    free(pargv);
    if (!warm_wasi_imports(store, module, wasi, &externs))
      die(74, "cannot set up the WASI host (a --dir or --tcplisten)", NULL);
  } else {
    wasm_extern_vec_new_uninitialized(&externs, imports.size);
  }
  for (size_t k = 0; k < imports.size; k++) {
    if (externs.data[k]) continue;
    const wasm_name_t *m = wasm_importtype_module(imports.data[k]);
    const wasm_name_t *n = wasm_importtype_name(imports.data[k]);
    fflush(stdout);
    fprintf(stderr, "warmrun: nothing to import as \"%.*s\".\"%.*s\"\n",
            (int)m->size, m->data, (int)n->size, n->data);
    return is_wasi ? 71 : 4;
  }
  wasm_importtype_vec_delete(&imports);

  wasm_trap_t *trap = NULL;
  wasm_message_t err;
  wasm_instance_t *instance = warm_instance_new(store, module, &externs, &trap, &err);
  if (!instance) {
    if (trap) report_trap(trap, is_wasi);
    die(is_wasi ? 71 : 4, "the module does not link", err.data);
  }
  wasm_extern_vec_t ex;
  wasm_instance_exports(instance, &ex);

  int rc = 0;
  if (is_wasi) {
    const wasm_extern_t *start = find_export(module, &ex, "_start");
    if (!start || !wasm_extern_as_func_const(start))
      die(72, "the module exports no _start; it is not a WASI command", NULL);
    wasm_val_vec_t none = WASM_EMPTY_VEC;
    wasm_trap_t *t = wasm_func_call(wasm_extern_as_func_const(start), &none, &none);
    if (t) report_trap(t, true);
  } else if (i >= argc) {
    const wasm_extern_t *e = find_export(module, &ex, "_start");
    if (!e) e = find_export(module, &ex, "main");
    if (e && wasm_extern_as_func_const(e)) {
      rc = call_export(wasm_extern_as_func_const(e), 0, NULL);
    } else {
      wasm_exporttype_vec_t types;
      wasm_module_exports(module, &types);
      printf("exports:\n");
      for (size_t k = 0; k < types.size; k++) {
        const wasm_name_t *n = wasm_exporttype_name(types.data[k]);
        printf("  %.*s\n", (int)n->size, n->data);
      }
      wasm_exporttype_vec_delete(&types);
    }
  } else {
    while (i < argc) {
      int j = i + 1;
      while (j < argc && strcmp(argv[j], "--")) j++;
      const wasm_extern_t *e = find_export(module, &ex, argv[i]);
      if (!e || !wasm_extern_as_func_const(e)) {
        fflush(stdout);
        fprintf(stderr, "warmc-rt: no such export: %s\n", argv[i]);
        return 2;
      }
      int r = call_export(wasm_extern_as_func_const(e), j - i - 1, argv + i + 1);
      if (r) rc = r;
      i = j + 1;
    }
  }
  fflush(stdout);
  wasm_extern_vec_delete(&ex);
  wasm_instance_delete(instance);
  wasm_module_delete(module);
  wasm_store_delete(store);
  wasm_engine_delete(engine);
  warm_wasi_config_delete(wasi);
  return rc;
}
