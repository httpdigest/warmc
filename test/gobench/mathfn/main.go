// The f64 intrinsics: sqrt, abs, min and max are single AArch64 instructions
// and no other kernel emits one.  Written so the reduction is over integers,
// because TinyGo's println formats a float differently from big Go's.
package main

import "math"

//go:noinline
func norms(a, b []float64) float64 {
	s := 0.0
	for i := range a {
		d := math.Abs(a[i] - b[i])
		s += math.Sqrt(d) + math.Min(a[i], b[i]) - math.Max(-a[i], -b[i])
	}
	return s
}

func main() {
	const n = 4096
	x := make([]float64, n)
	y := make([]float64, n)
	for i := range x {
		x[i] = float64(i%97) * 1.5
		y[i] = float64(i%31) * 0.25
	}
	total := 0
	for r := 0; r < 20000; r++ {
		x[r&(n-1)] = float64(r % 97)
		total += int(norms(x, y))
	}
	println("total =", total)
}
