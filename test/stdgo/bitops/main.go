// The math/bits intrinsics, each of which should be one wasm opcode.
//
// Worth having in this corpus specifically because Go's back end is not LLVM:
// whether `bits.RotateLeft64` becomes `i64.rotl` or a shift pair is the
// compiler's choice, and rsbench/bitops showed what the answer costs -- that
// kernel is latency-bound on the population count, and moving it to the end of
// its exclusive-or chain was worth 18%.
package main

import (
	"fmt"
	"math/bits"
)

//go:noinline
func mix(x uint64, k uint) uint64 {
	a := bits.RotateLeft64(x, int(k%64))
	b := bits.RotateLeft64(x, -int(k%64)-1)
	c := uint64(bits.OnesCount64(x))
	d := uint64(bits.LeadingZeros64(x))
	e := uint64(bits.TrailingZeros64(x))
	f := bits.ReverseBytes64(x)
	return a ^ b ^ (c << 8) ^ (d << 16) ^ (e << 24) ^ f
}

func main() {
	x := uint64(0x0123456789abcdef)
	var s uint64
	for r := 0; r < 21000000; r++ {
		x = mix(x, uint(r))
		x += uint64(r) | 1
		s += x
	}
	fmt.Println("s =", s)
}
