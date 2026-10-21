// Signed 64-bit division and remainder, the same source as test/gobench/div64.
//
// Go defines -2^63 / -1 rather than trapping, and wasm traps on it, so Go's back
// end does not emit i64.div_s for the quotient at all: it calls runtime.wasmDiv,
// which tests for that case first.  The remainder is i64.rem_s inline, which
// wasm already defines for it.  So this row is a call per element where the
// LLVM corpora have an instruction.
package main

import "fmt"

//go:noinline
func ratios(a []int64, d int64) int64 {
	var s int64
	for i := range a {
		s += a[i]/d + a[i]%(d+1)
	}
	return s
}

func main() {
	const n = 2048
	a := make([]int64, n)
	for i := range a {
		a[i] = int64(i)*0x1e3779b97f4a7c1 - 1<<40
	}
	var total int64
	for r := 0; r < 55000; r++ {
		a[r&(n-1)] = int64(r) - 1<<20
		total += ratios(a, int64(r%13)+3)
	}
	fmt.Println("total =", total)
}
