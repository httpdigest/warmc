// float32, four lanes.  Elementwise floating point is vectorisable where a
// float *reduction* is not: nothing is reassociated, so every lane computes
// exactly what the scalar loop would have.
package main

//go:noinline
func kern(a, b, c []float32) {
	for i := range c {
		c[i] = a[i]*b[i] + a[i]
	}
}

func main() {
	const n = 4096
	a := make([]float32, n)
	b := make([]float32, n)
	c := make([]float32, n)
	for i := range a {
		a[i] = float32(i%97) * 0.5
		b[i] = float32(i%31) * 0.25
	}
	total := 0
	for r := 0; r < 180000; r++ {
		a[r&(n-1)] = float32(r % 97)
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
