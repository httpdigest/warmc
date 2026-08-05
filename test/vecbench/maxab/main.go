// Elementwise max of two slices, as opposed to a max *reduction*: the
// min/max shape has to be recognised in the body as well as in an
// accumulator.
package main

//go:noinline
func kern(a, b, c []int32) {
	for i := range c {
		if a[i] > b[i] {
			c[i] = a[i]
		} else {
			c[i] = b[i]
		}
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	b := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i % 251)
		b[i] = int32(i % 157)
	}
	total := 0
	for r := 0; r < 40000; r++ {
		a[r&(n-1)] = int32(r % 251)
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
