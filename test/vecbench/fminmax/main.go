// A float32 min and max reduction written with Go's built-in min and max.
// TinyGo lowers those straight to f32.min and f32.max -- not to a compare and
// a select -- because the wasm instructions already have Go's semantics: a
// NaN anywhere wins, and -0 orders below +0.  NEON's FMIN and FMAX .4s have
// the same semantics, and unlike the sum in `f32red` a min or a max may be
// reassociated without changing the answer, so this reduction is
// vectorisable exactly: four lanes per accumulator, folded once at the end.
//
// warmc does not vectorise it yet ("body op fmax": f32.min and f32.max are not
// lane operations to the vectoriser).  The scalar loop is two dependence
// chains of FMIN and FMAX, so it is latency-bound, and warmc and wasmtime run
// it at the same speed.  Here to show the gap.
package main

import "math"

//go:noinline
func kern(a []float32) (float32, float32) {
	lo, hi := a[0], a[0]
	for _, v := range a {
		lo = min(lo, v)
		hi = max(hi, v)
	}
	return lo, hi
}

func main() {
	const n = 4096
	a := make([]float32, n)
	for i := range a {
		a[i] = float32((i*37)%1009)*0.25 - 100
	}
	total := uint32(0)
	for r := 0; r < 45000; r++ {
		a[(r*61)&(n-1)] = float32((r*53)%2003)*0.125 - 125
		lo, hi := kern(a)
		total = total*31 + math.Float32bits(lo) ^ math.Float32bits(hi)
	}
	println("total =", total)
}
