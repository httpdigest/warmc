// A Go host of libwarmc: compiles example/jit/guest.wat, gives it a host
// function written in Go, calls its exports and reads its memory, through
// the standard WebAssembly C API (wasm.h) and cgo.
//
//	cd example/jit/go && go build -o host . && ./host ../guest.wat
package main

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../build -lwarmc
#cgo !windows LDFLAGS: -Wl,-rpath,${SRCDIR}/../../../build
#include <stdlib.h>
#include <string.h>
#include "wasm.h"
#include "warm.h"

// callback.c's: host.log as a wasm_func_callback_t
extern wasm_trap_t *log_callback(const wasm_val_vec_t *args, wasm_val_vec_t *results);

// wasm_val_t's union, which cgo shows as bytes
static wasm_val_t val_i32(int32_t v) { wasm_val_t x = WASM_I32_VAL(v); return x; }
static wasm_val_t val_i64(int64_t v) { wasm_val_t x = WASM_I64_VAL(v); return x; }
static int32_t of_i32(const wasm_val_t *v) { return v->of.i32; }
static int64_t of_i64(const wasm_val_t *v) { return v->of.i64; }
static wasm_val_t *val_at(wasm_val_t *vs, size_t i) { return &vs[i]; }
static wasm_extern_t *extern_at(const wasm_extern_vec_t *v, size_t i) { return v->data[i]; }
static const wasm_name_t *export_name(const wasm_exporttype_vec_t *v, size_t i) {
  return wasm_exporttype_name(v->data[i]);
}
*/
import "C"

import (
	"fmt"
	"os"
	"unsafe"
)

// the guest's memory, once it is instantiated (export.go's goLog reads it)
var memory *C.wasm_memory_t

// export returns the instance's export named name.
func export(module *C.wasm_module_t, exports *C.wasm_extern_vec_t, name string) *C.wasm_extern_t {
	var types C.wasm_exporttype_vec_t
	C.wasm_module_exports(module, &types)
	defer C.wasm_exporttype_vec_delete(&types)
	for i := C.size_t(0); i < types.size; i++ {
		n := C.export_name(&types, i)
		if C.GoStringN(n.data, C.int(n.size)) == name {
			return C.extern_at(exports, i)
		}
	}
	panic("no export " + name)
}

// call calls fn with args, the results going to results: the trap's
// message, or "".
func call(fn *C.wasm_func_t, args []C.wasm_val_t, results []C.wasm_val_t) string {
	n := len(args) + len(results)
	if n == 0 {
		n = 1
	}
	// the values in C's memory: cgo lets C keep no pointer into Go's
	vals := (*C.wasm_val_t)(C.calloc(C.size_t(n), C.size_t(unsafe.Sizeof(C.wasm_val_t{}))))
	defer C.free(unsafe.Pointer(vals))
	for i, a := range args {
		*C.val_at(vals, C.size_t(i)) = a
	}
	av := C.wasm_val_vec_t{size: C.size_t(len(args)), data: vals}
	rv := C.wasm_val_vec_t{size: C.size_t(len(results)), data: C.val_at(vals, C.size_t(len(args)))}
	trap := C.wasm_func_call(fn, &av, &rv)
	for i := range results {
		results[i] = *C.val_at(vals, C.size_t(len(args)+i))
	}
	if trap == nil {
		return ""
	}
	defer C.wasm_trap_delete(trap)
	var msg C.wasm_message_t
	C.wasm_trap_message(trap, &msg)
	defer C.wasm_byte_vec_delete(&msg)
	return C.GoString(msg.data)
}

func main() {
	path := "../guest.wat"
	if len(os.Args) > 1 {
		path = os.Args[1]
	}
	wat, err := os.ReadFile(path)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	engine := C.wasm_engine_new()
	store := C.wasm_store_new(engine)

	text := C.CBytes(wat)
	defer C.free(text)
	var binary, msg C.wasm_byte_vec_t
	if !C.warm_wat2wasm((*C.char)(text), C.size_t(len(wat)), &binary, &msg) {
		fmt.Fprintln(os.Stderr, C.GoString(msg.data))
		os.Exit(1)
	}
	module := C.warm_module_new(store, &binary, &msg)
	if module == nil {
		fmt.Fprintln(os.Stderr, C.GoString(msg.data))
		os.Exit(1)
	}

	// host.log, of type (i32, i32) -> (), calling callback.c's log_callback
	var params, results C.wasm_valtype_vec_t
	kinds := (**C.wasm_valtype_t)(C.malloc(2 * C.size_t(unsafe.Sizeof(uintptr(0)))))
	ks := unsafe.Slice(kinds, 2)
	ks[0] = C.wasm_valtype_new(C.WASM_I32)
	ks[1] = C.wasm_valtype_new(C.WASM_I32)
	C.wasm_valtype_vec_new(&params, 2, kinds)
	C.free(unsafe.Pointer(kinds))
	C.wasm_valtype_vec_new_empty(&results)
	logType := C.wasm_functype_new(&params, &results)
	logFunc := C.wasm_func_new(store, logType, C.wasm_func_callback_t(C.log_callback))
	C.wasm_functype_delete(logType)

	imports := (**C.wasm_extern_t)(C.malloc(C.size_t(unsafe.Sizeof(uintptr(0)))))
	*imports = C.wasm_func_as_extern(logFunc)
	importVec := C.wasm_extern_vec_t{size: 1, data: imports}
	instance := C.wasm_instance_new(store, module, &importVec, nil)
	C.free(unsafe.Pointer(imports))
	if instance == nil {
		fmt.Fprintln(os.Stderr, "cannot instantiate")
		os.Exit(1)
	}
	var exports C.wasm_extern_vec_t
	C.wasm_instance_exports(instance, &exports)
	memory = C.wasm_extern_as_memory(export(module, &exports, "memory"))
	greet := C.wasm_extern_as_func(export(module, &exports, "greet"))
	fib := C.wasm_extern_as_func(export(module, &exports, "fib"))
	div := C.wasm_extern_as_func(export(module, &exports, "div"))
	peek := C.wasm_extern_as_func(export(module, &exports, "peek"))

	call(greet, nil, nil)

	r := make([]C.wasm_val_t, 1)
	call(fib, []C.wasm_val_t{C.val_i64(90)}, r)
	fmt.Printf("fib(90) = %d\n", C.of_i64(&r[0]))

	for _, p := range [][2]int32{{7, 2}, {1, 0}} {
		if trap := call(div, []C.wasm_val_t{C.val_i32(C.int32_t(p[0])), C.val_i32(C.int32_t(p[1]))}, r); trap != "" {
			fmt.Printf("div(%d, %d) trapped: %s\n", p[0], p[1], trap)
		} else {
			fmt.Printf("div(%d, %d) = %d\n", p[0], p[1], C.of_i32(&r[0]))
		}
	}
	for _, addr := range []int32{16, 65536} {
		if trap := call(peek, []C.wasm_val_t{C.val_i32(C.int32_t(addr))}, r); trap != "" {
			fmt.Printf("peek(%d) trapped: %s\n", addr, trap)
		} else {
			fmt.Printf("peek(%d) = %d\n", addr, C.of_i32(&r[0]))
		}
	}

	C.wasm_extern_vec_delete(&exports)
	C.wasm_instance_delete(instance)
	C.wasm_func_delete(logFunc)
	C.wasm_module_delete(module)
	C.wasm_byte_vec_delete(&binary)
	C.wasm_store_delete(store)
	C.wasm_engine_delete(engine)
}
