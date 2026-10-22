// math.Sqrt per element, in float64: c[i] = sqrt(x[i]*x[i] + y[i]*y[i]).  TinyGo
// treats math.Sqrt as an intrinsic and emits f64.sqrt, not a call, so the
// body is two multiplies, an add and a square root on two f64 loads.  A
// square root is correctly rounded, so doing two lanes at once gives exactly
// the scalar answer; the only question is whether the vectoriser has a lane
// form of it.
//
// warmc vectorises it: fmul.2d, fadd.2d and fsqrt.2d.  Two lanes, but the
// square root is the long-latency instruction, so halving the number of them
// gets the whole 2x: 0.51x of the -fno-vectorize loop, where the single add
// in `addi64` gets 0.68x.
package main

import "math"

//go:noinline
func kern(x, y, c []float64) {
	for i := range c {
		c[i] = math.Sqrt(x[i]*x[i] + y[i]*y[i])
	}
}

func main() {
	const n = 4096
	x := make([]float64, n)
	y := make([]float64, n)
	c := make([]float64, n)
	for i := range x {
		x[i] = float64(i%97) * 0.5
		y[i] = float64(i%31) - 15
	}
	total := uint64(0)
	for r := 0; r < 85000; r++ {
		x[r&(n-1)] = float64(r%97) * 0.75
		kern(x, y, c)
		total = total*31 + math.Float64bits(c[(r*7)&(n-1)])
	}
	for i := range c {
		total = total*31 + math.Float64bits(c[i])
	}
	println("total =", total)
}
