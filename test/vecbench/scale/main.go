// One input, constants for the rest: c[i] = a[i]*3 + 7.  A multiply by a
// constant and an add of one, which is the shape a splatted operand has to
// be recognised for.
package main

//go:noinline
func kern(a, c []int32) {
	for i := range c {
		c[i] = a[i]*3 + 7
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i*7 + 1)
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int32(r)
		kern(a, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
