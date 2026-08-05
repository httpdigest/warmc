// math/bits: popcount, leading and trailing zeros.  These are single wasm
// instructions, so this measures whether they reach the AArch64 ones or go
// through a library.
package main

import "math/bits"

//go:noinline
func scan(a []uint64) int {
	n := 0
	for i := 0; i < len(a); i++ {
		v := a[i]
		n += bits.OnesCount64(v) + bits.LeadingZeros64(v) + bits.TrailingZeros64(v|1)
	}
	return n
}

func main() {
	a := make([]uint64, 4096)
	for i := range a {
		a[i] = uint64(i) * 0x9e3779b97f4a7c15
	}
	total := 0
	for r := 0; r < 50000; r++ {
		a[r&4095] = uint64(r) * 31
		total += scan(a)
	}
	println("total =", total)
}
