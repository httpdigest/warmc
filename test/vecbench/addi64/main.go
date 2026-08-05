// Two lanes.  Worth only 2x even when it works, and several NEON operations
// have no .2d form at all, so this is where a vectoriser is most likely to
// either give up or emit something worse than the scalar loop.
package main

//go:noinline
func kern(a, b, c []int64) {
	for i := range c {
		c[i] = a[i] + b[i]
	}
}

func main() {
	const n = 4096
	a := make([]int64, n)
	b := make([]int64, n)
	c := make([]int64, n)
	for i := range a {
		a[i] = int64(i)*7 + 1
		b[i] = int64(i)*3 + 2
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int64(r)
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
