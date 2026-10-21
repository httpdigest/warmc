// Several return values, the same source as test/gobench/multiret.
//
// TinyGo returns these as wasm multi-value.  Go's back end has no use for it:
// results come back through the caller's frame on Go's own stack in linear
// memory, like the arguments, so this measures stores in the callee and loads
// in the caller where the LLVM corpora have registers.
package main

import "fmt"

//go:noinline
func divmod3(a, b int) (int, int, bool) {
	if b == 0 {
		return 0, 0, false
	}
	return a / b, a % b, true
}

//go:noinline
func minmax(a []int32) (int32, int32, int) {
	lo, hi := a[0], a[0]
	for _, v := range a {
		if v < lo {
			lo = v
		}
		if v > hi {
			hi = v
		}
	}
	return lo, hi, len(a)
}

func main() {
	const n = 1024
	a := make([]int32, n)
	for i := range a {
		a[i] = int32(uint32(i) * 2654435761 >> 8)
	}
	total := 0
	for r := 0; r < 100000; r++ {
		a[r&(n-1)] = int32(r)
		lo, hi, ln := minmax(a)
		q, m, ok := divmod3(int(hi-lo), (r&7)+1)
		if ok {
			total += q + m + ln
		}
	}
	fmt.Println("total =", total)
}
