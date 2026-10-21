// The f64 intrinsics, the same source as test/gobench/mathfn.
//
// Go makes math.Sqrt and math.Abs single wasm instructions, but not math.Min and
// math.Max: those are calls to the portable Go functions that handle NaN and
// the sign of zero themselves.  The reduction is over integers, so the output
// does not depend on how a float is formatted.
package main

import (
	"fmt"
	"math"
)

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
	for r := 0; r < 4000; r++ {
		x[r&(n-1)] = float64(r % 97)
		total += int(norms(x, y))
	}
	fmt.Println("total =", total)
}
