// Two int32 slices added elementwise: four lanes, and the base case every
// other kernel here is a variation on.  Indexing two slices at once is the
// point -- Go bounds-checks the second one, and whether that branch stops the
// vectoriser is the first thing this corpus asks.
package main

//go:noinline
func kern(a, b, c []int32) {
	for i := range c {
		c[i] = a[i] + b[i]
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	b := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i*7 + 1)
		b[i] = int32(i*3 + 2)
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int32(r)
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
