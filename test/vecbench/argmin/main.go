// The index of the smallest element, first one on a tie.  LLVM if-converts the
// update into two selects that share one i32.lt_s: one picks the new minimum,
// the other the new index.  So there are two carried values, and the index is
// not a reduction of anything -- it depends on the other carried value.
//
// Vectorised, each lane keeps its own minimum and index, updated with a
// compare and two BSLs, and a cross-lane pass at the end picks the smallest
// value with the lowest index.  warmc does not vectorise it yet ("carried
// value is not a reduction").  It is also about 2x slower than wasmtime as a
// scalar loop: warmc materialises the shared compare with CSET and tests it
// again with CMP #0 for the second select, so the carried minimum's chain is
// CMP, CSET, CMP, CSEL per element where Cranelift repeats the compare and has
// CMP, CSEL.  Here for both gaps.
package main

//go:noinline
func kern(a []int32) int {
	best, at := a[0], 0
	for i, v := range a {
		if v < best {
			best, at = v, i
		}
	}
	return at
}

func main() {
	const n = 4096
	a := make([]int32, n)
	for i := range a {
		a[i] = int32((i*7919)%100003) - 50000
	}
	total := 0
	for r := 0; r < 25000; r++ {
		a[(r*61)&(n-1)] = int32((r*104729)%120011) - 60000
		total += kern(a) * (r&3 + 1)
	}
	println("total =", total)
}
