// A polynomial per element: the odd Taylor series of sin to x^9, in Horner
// form, in float32.  TinyGo emits six f32.mul and four f32.add with five
// f32.const operands, every one on a single dependence chain, so the scalar
// loop is latency-bound and the throughput is in running lanes side by side.
// Every constant has to be splatted into a vector register, and warmc does
// that once with DUP before the vector loop.
//
// warmc vectorises it: fmul.4s and fadd.4s, with no fused multiply-add, which
// would change the rounding.  This is the shape of every vectorised libm
// approximation, and no other loop in this corpus has float constants.
package main

import "math"

//go:noinline
func kern(x, c []float32) {
	for i := range c {
		v := x[i]
		v2 := v * v
		c[i] = v * (1 + v2*(-1.0/6+v2*(1.0/120+v2*(-1.0/5040+v2*(1.0/362880)))))
	}
}

func main() {
	const n = 4096
	x := make([]float32, n)
	c := make([]float32, n)
	for i := range x {
		x[i] = float32(i%629)*0.005 - 1.57
	}
	total := uint32(0)
	for r := 0; r < 140000; r++ {
		x[r&(n-1)] = float32(r%629)*0.005 - 1.57
		kern(x, c)
		total = total*31 + math.Float32bits(c[(r*7)&(n-1)])
	}
	for i := range c {
		total = total*31 + math.Float32bits(c[i])
	}
	println("total =", total)
}
