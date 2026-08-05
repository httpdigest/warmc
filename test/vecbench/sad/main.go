// Sum of absolute differences: a subtract, an absolute value and a
// reduction, which is three recognisers having to compose in one loop.
package main

//go:noinline
func kern(a, b []int32) int32 {
	var s int32
	for i := range a {
		d := a[i] - b[i]
		if d < 0 {
			d = -d
		}
		s += d
	}
	return s
}

func main() {
	const n = 4096
	a := make([]int32, n)
	b := make([]int32, n)
	for i := range a {
		a[i] = int32(i % 251)
		b[i] = int32(i % 157)
	}
	total := 0
	for r := 0; r < 30000; r++ {
		a[r&(n-1)] = int32(r % 251)
		total += int(kern(a, b))
	}
	println("total =", total)
}
