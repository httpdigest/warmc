// host.log as the C API calls a host function: its arguments go to the
// Go function goLog (export.go), which cgo's _cgo_export.h declares.
#include "wasm.h"
#include "_cgo_export.h"

wasm_trap_t *log_callback(const wasm_val_vec_t *args, wasm_val_vec_t *results) {
  (void)results;
  goLog((uint32_t)args->data[0].of.i32, (uint32_t)args->data[1].of.i32);
  return NULL;
}
