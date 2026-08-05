// Absolute value: a negate and a select, or one ABS instruction.
package main

//go:noinline
func kern(a, c []int32) {
	for i := range c {
		v := a[i]
		if v < 0 {
			v = -v
		}
		c[i] = v
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i%401) - 200
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int32(r%401) - 200
		kern(a, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
