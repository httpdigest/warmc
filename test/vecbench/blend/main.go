// A select that is not a min or a max: the comparison is against a constant,
// so the value chosen is not one of the two being compared.  Vectorisable as
// a compare and a bitwise select, and distinct from the min/max shape.
package main

//go:noinline
func kern(a, b, c []int32) {
	for i := range c {
		if a[i] > 0 {
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
		a[i] = int32(i%201) - 100
		b[i] = int32(i*3 + 2)
	}
	total := 0
	for r := 0; r < 120000; r++ {
		a[r&(n-1)] = int32(r%201) - 100
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
