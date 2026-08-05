// A widening integer reduction: uint32 elements into a uint64 accumulator.
// The store into a[] each round is not decoration -- without it the call is
// loop-invariant and LLVM hoists the whole reduction out.
package main

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
	for r := 0; r < 60000; r++ {
		a[r&4095] = uint32(r)
		s += sumSlice(a)
	}
	println("s =", s)
}
