// y[i] += alpha*x[i]: the BLAS axpy, in float32.  Three things at once that no
// other kernel here combines: a float operand that is a parameter rather than
// a constant, so it has to be splatted from a register; a float
// read-modify-write of the same slice; and a multiply feeding an add that
// must stay two operations.  Wasm has no fused multiply-add in its base
// instruction set, so TinyGo emits f32.mul then f32.add, and fusing them into
// FMLA would change the rounding of every element.
//
// warmc vectorises it: fmul.4s and fadd.4s, with no FMLA.  alpha changes every
// round, the products are inexact, and the checksum takes the bits of every
// y, so the rounding of each one is checked.
package main

import "math"

//go:noinline
func kern(alpha float32, x, y []float32) {
	for i := range y {
		y[i] += alpha * x[i]
	}
}

func main() {
	const n = 4096
	x := make([]float32, n)
	y := make([]float32, n)
	for i := range x {
		x[i] = float32(i%97) * 0.1
		y[i] = float32(i%31) * 0.25
	}
	total := uint32(0)
	for r := 0; r < 300000; r++ {
		x[r&(n-1)] = float32(r%97) * 0.1
		kern((float32(r%5)-2)*0.37, x, y)
		total = total*31 + math.Float32bits(y[(r*7)&(n-1)])
	}
	for i := range y {
		total = total*31 + math.Float32bits(y[i])
	}
	println("total =", total)
}
