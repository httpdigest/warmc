// Sixteen-bit lanes: eight at a time.  Between the byte and the int32 cases
// so a lane-width bug that only affects the middle size cannot hide.
package main

//go:noinline
func kern(a, b, c []int16) {
	for i := range c {
		c[i] = a[i] + b[i]
	}
}

func main() {
	const n = 4096
	a := make([]int16, n)
	b := make([]int16, n)
	c := make([]int16, n)
	for i := range a {
		a[i] = int16(i*7 + 1)
		b[i] = int16(i*3 + 2)
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int16(r)
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
