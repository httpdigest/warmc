// A float32 sum, which must NOT be vectorised: reassociating it changes the
// answer.  Here so that the refusal stays a refusal.
package main

//go:noinline
func kern(a []float32) float32 {
	var s float32
	for i := range a {
		s += a[i]
	}
	return s
}

func main() {
	const n = 4096
	a := make([]float32, n)
	for i := range a {
		a[i] = float32(i%97) * 0.5
	}
	total := 0
	for r := 0; r < 20000; r++ {
		a[r&(n-1)] = float32(r % 97)
		total += int(kern(a))
	}
	println("total =", total)
}
