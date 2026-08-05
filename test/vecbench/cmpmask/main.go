// A comparison stored rather than summed: zero or one per element.
package main

//go:noinline
func kern(a, b, c []int32) {
	for i := range c {
		if a[i] < b[i] {
			c[i] = 1
		} else {
			c[i] = 0
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
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int32(r % 251)
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
