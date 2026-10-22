// bits.ReverseBytes32 per element.  Wasm has no byte-swap instruction, so
// LLVM expands it into two rotates and two masks: ((x & 0x00ff00ff) rotr 8) |
// ((x rotr 24) & 0x00ff00ff), with i32.rotr and i32.and.  warmc's fold pass
// recognises that shape and turns it back into one bswap, which is a single
// REV as a scalar instruction.
//
// The vectoriser has no lane form of bswap, so it rejects the loop ("body op
// bswap") -- and with -fno-fold the and/rotr/or body does vectorise, so an
// idiom recogniser running first is what stops it.  The vector form is one
// REV32.16b for four elements.  Here to show the gap and the interaction.
package main

import "math/bits"

//go:noinline
func kern(a, c []uint32) {
	for i := range c {
		c[i] = bits.ReverseBytes32(a[i])
	}
}

func main() {
	const n = 4096
	a := make([]uint32, n)
	c := make([]uint32, n)
	for i := range a {
		a[i] = uint32(i)*2654435761 + 1
	}
	total := uint32(0)
	for r := 0; r < 150000; r++ {
		a[r&(n-1)] = uint32(r) * 2246822519
		kern(a, c)
		total = total*31 + c[(r*7)&(n-1)]
	}
	for i := range c {
		total = total*31 + c[i]
	}
	println("total =", total)
}
