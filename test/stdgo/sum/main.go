// A widening integer reduction, the same source as test/gobench/sum.
//
// That is the point of this corpus: the arithmetic here is identical to the
// TinyGo kernel of the same name, so the gap between the two rows is the gap
// between two Go compilers rather than between two programs.  TinyGo goes
// through LLVM; this goes through Go's own back end, which keeps almost
// everything 64-bit and brings a scheduler and a collector with it.
//
// `fmt.Println` rather than `println`: the builtin writes to stderr, and the
// harness compares stdout.  It is outside the timed loop either way.
package main

import "fmt"

//go:noinline
func sumSlice(a []uint32) uint64 {
	var s uint64
	for i := 0; i < len(a); i++ {
		s += uint64(a[i])
	}
	return s
}

func main() {
	a := make([]uint32, 4096)
	for i := range a {
		a[i] = uint32(i) * 2654435761
	}
	var s uint64
	// The store keeps the call from being loop-invariant, as in every other
	// corpus; without it the whole reduction hoists out.
	for r := 0; r < 52000; r++ {
		a[r&4095] = uint32(r)
		s += sumSlice(a)
	}
	fmt.Println("s =", s)
}
