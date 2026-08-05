// A float reduction, which no engine may vectorise: reassociating it changes
// the result.  It measures what is left once that is off the table.
package main

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
	for r := 0; r < 60000; r++ {
		x[r&4095] = float64(r)
		d += dot(x, y)
	}
	println("d =", int(d/1e6))
}
