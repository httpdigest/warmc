// The cardinality of a bitset: bits.OnesCount64 summed over a []uint64.  TinyGo
// lowers OnesCount64 to the wasm i64.popcnt instruction, then i32.wrap_i64
// and an i32.add into the count.  Base AArch64 has no scalar popcount, so
// warmc moves each word into a vector register for CNT.8b and ADDV.8b and back again
// -- vector instructions, one word at a time.
//
// Vectorised, two words go through one CNT.16b and the byte counts are
// accumulated with widening pairwise adds, with one cross-lane add at the end
// instead of one per word.  warmc does not vectorise it yet ("body op
// popcnt"), although it is already 0.4x of wasmtime as a scalar loop.  Here to
// show the gap.
package main

import "math/bits"

//go:noinline
func kern(set []uint64) int {
	n := 0
	for _, w := range set {
		n += bits.OnesCount64(w)
	}
	return n
}

func main() {
	const n = 4096
	set := make([]uint64, n)
	x := uint64(0x9E3779B97F4A7C15)
	for i := range set {
		x ^= x << 13
		x ^= x >> 7
		x ^= x << 17
		set[i] = x & (x >> uint(i&31))
	}
	total := 0
	for r := 0; r < 120000; r++ {
		set[(r*61)&(n-1)] ^= uint64(r) * 0x9E3779B97F4A7C15
		total += kern(set)
	}
	println("total =", total)
}
