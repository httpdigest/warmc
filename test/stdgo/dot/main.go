// A float reduction, the same source as test/gobench/dot.  No engine may
// vectorise it, since reassociating changes the result, so it measures what is
// left once that is off the table: two loads, a multiply and an add per
// element, and a bounds check on the second slice.
package main

import "fmt"

//go:noinline
func dot(a, b []float64) float64 {
	var s float64
	for i := range a {
		s += a[i] * b[i]
	}
	return s
}

func main() {
	x := make([]float64, 4096)
	y := make([]float64, 4096)
	for i := range x {
		x[i] = float64(i) * 0.5
		y[i] = float64(i) * 0.25
	}
	var d float64
	for r := 0; r < 30000; r++ {
		x[r&4095] = float64(r)
		d += dot(x, y)
	}
	fmt.Println("d =", int(d/1e6))
}
