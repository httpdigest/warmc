package main

// #include <stdint.h>
// #include "wasm.h"
import "C"

import (
	"fmt"
	"unsafe"
)

// goLog is host.log: the bytes the guest points at, printed.  callback.c
// calls it with the guest's arguments.
//
//export goLog
func goLog(ptr, n C.uint32_t) {
	base := unsafe.Pointer(C.wasm_memory_data(memory))
	bytes := unsafe.Slice((*byte)(base), uint64(ptr)+uint64(n))[ptr:]
	fmt.Printf("log: %s\n", bytes)
}
