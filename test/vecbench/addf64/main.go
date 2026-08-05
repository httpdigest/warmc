// float64 elementwise, two lanes.
package main

//go:noinline
func kern(a, b, c []float64) {
	for i := range c {
		c[i] = a[i]*b[i] + a[i]
	}
}

func main() {
	const n = 4096
	a := make([]float64, n)
	b := make([]float64, n)
	c := make([]float64, n)
	for i := range a {
		a[i] = float64(i%97) * 0.5
		b[i] = float64(i%31) * 0.25
	}
	total := 0
	for r := 0; r < 180000; r++ {
		a[r&(n-1)] = float64(r % 97)
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
