/* A C host of libwarmc: compiles example/jit/guest.wat, gives it a host
   function, calls its exports and reads its memory, through the standard
   WebAssembly C API (wasm.h) and warmc's additions (warm.h).

     clang -Iinclude -o host example/jit/c/host.c -Lbuild -lwarmc -Wl,-rpath,build
     ./host example/jit/guest.wat */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm.h"
#include "warm.h"

static wasm_memory_t *memory;   /* the guest's, once it is instantiated */

/* host.log(ptr, len): the bytes the guest points at, printed */
static wasm_trap_t *host_log(const wasm_val_vec_t *args, wasm_val_vec_t *results) {
  (void)results;
  uint32_t ptr = (uint32_t)args->data[0].of.i32, len = (uint32_t)args->data[1].of.i32;
  const char *base = wasm_memory_data(memory);
  printf("log: %.*s\n", (int)len, base + ptr);
  return NULL;
}

/* the instance's export named name */
static wasm_extern_t *export_named(const wasm_module_t *module,
                                   const wasm_extern_vec_t *exports, const char *name) {
  wasm_exporttype_vec_t types;
  wasm_module_exports(module, &types);
  wasm_extern_t *found = NULL;
  for (size_t i = 0; i < types.size && !found; i++) {
    const wasm_name_t *n = wasm_exporttype_name(types.data[i]);
    if (n->size == strlen(name) && !memcmp(n->data, name, n->size)) found = exports->data[i];
  }
  wasm_exporttype_vec_delete(&types);
  return found;
}

static char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) n = 0;
  fclose(f);
  *len = (size_t)n;
  return buf;
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "example/jit/guest.wat";
  size_t len;
  char *text = read_file(path, &len);
  if (!text) { fprintf(stderr, "cannot read %s\n", path); return 1; }

  wasm_engine_t *engine = wasm_engine_new();
  wasm_store_t *store = wasm_store_new(engine);

  /* the text assembled, then compiled: at -O2, whole, as warmc -c does */
  wasm_byte_vec_t binary;
  wasm_message_t error;
  if (!warm_wat2wasm(text, len, &binary, &error)) {
    fprintf(stderr, "%s\n", error.data);
    return 1;
  }
  wasm_module_t *module = warm_module_new(store, &binary, &error);
  if (!module) { fprintf(stderr, "%s\n", error.data); return 1; }

  /* host.log, of type (i32, i32) -> () */
  wasm_valtype_vec_t params, results;
  wasm_valtype_t *ps[2] = { wasm_valtype_new(WASM_I32), wasm_valtype_new(WASM_I32) };
  wasm_valtype_vec_new(&params, 2, ps);
  wasm_valtype_vec_new_empty(&results);
  wasm_functype_t *log_type = wasm_functype_new(&params, &results);
  wasm_func_t *log_func = wasm_func_new(store, log_type, host_log);
  wasm_functype_delete(log_type);

  wasm_extern_t *imports[1] = { wasm_func_as_extern(log_func) };
  wasm_extern_vec_t import_vec = WASM_ARRAY_VEC(imports);
  wasm_trap_t *trap = NULL;
  wasm_instance_t *instance = wasm_instance_new(store, module, &import_vec, &trap);
  if (!instance) { fprintf(stderr, "cannot instantiate\n"); return 1; }
  wasm_extern_vec_t exports;
  wasm_instance_exports(instance, &exports);
  memory = wasm_extern_as_memory(export_named(module, &exports, "memory"));
  wasm_func_t *greet = wasm_extern_as_func(export_named(module, &exports, "greet"));
  wasm_func_t *fib = wasm_extern_as_func(export_named(module, &exports, "fib"));
  wasm_func_t *div = wasm_extern_as_func(export_named(module, &exports, "div"));
  wasm_func_t *peek = wasm_extern_as_func(export_named(module, &exports, "peek"));

  wasm_val_vec_t none = WASM_EMPTY_VEC;
  wasm_func_call(greet, &none, &none);

  wasm_val_t fa[1] = { WASM_I64_VAL(90) }, fr[1] = { WASM_INIT_VAL };
  wasm_val_vec_t fargs = WASM_ARRAY_VEC(fa), fres = WASM_ARRAY_VEC(fr);
  wasm_func_call(fib, &fargs, &fres);
  printf("fib(90) = %" PRId64 "\n", fr[0].of.i64);

  int32_t pairs[2][2] = { { 7, 2 }, { 1, 0 } };
  for (int i = 0; i < 2; i++) {
    wasm_val_t da[2] = { WASM_I32_VAL(pairs[i][0]), WASM_I32_VAL(pairs[i][1]) };
    wasm_val_t dr[1] = { WASM_INIT_VAL };
    wasm_val_vec_t dargs = WASM_ARRAY_VEC(da), dres = WASM_ARRAY_VEC(dr);
    trap = wasm_func_call(div, &dargs, &dres);
    if (trap) {
      wasm_message_t msg;
      wasm_trap_message(trap, &msg);
      printf("div(%d, %d) trapped: %s\n", pairs[i][0], pairs[i][1], msg.data);
      wasm_byte_vec_delete(&msg);
      wasm_trap_delete(trap);
    } else {
      printf("div(%d, %d) = %d\n", pairs[i][0], pairs[i][1], dr[0].of.i32);
    }
  }

  int32_t addrs[2] = { 16, 65536 };
  for (int i = 0; i < 2; i++) {
    wasm_val_t pa[1] = { WASM_I32_VAL(addrs[i]) }, pr[1] = { WASM_INIT_VAL };
    wasm_val_vec_t pargs = WASM_ARRAY_VEC(pa), pres = WASM_ARRAY_VEC(pr);
    trap = wasm_func_call(peek, &pargs, &pres);
    if (trap) {
      wasm_message_t msg;
      wasm_trap_message(trap, &msg);
      printf("peek(%d) trapped: %s\n", addrs[i], msg.data);
      wasm_byte_vec_delete(&msg);
      wasm_trap_delete(trap);
    } else {
      printf("peek(%d) = %d\n", addrs[i], pr[0].of.i32);
    }
  }

  wasm_extern_vec_delete(&exports);
  wasm_instance_delete(instance);
  wasm_func_delete(log_func);
  wasm_module_delete(module);
  wasm_byte_vec_delete(&binary);
  wasm_store_delete(store);
  wasm_engine_delete(engine);
  free(text);
  return 0;
}
