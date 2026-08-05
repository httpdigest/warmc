// A comparison used as a value: counting matches.  The comparison produces a
// mask of all ones and the count needs a one, so something has to turn one
// into the other before it can be summed.
package main

//go:noinline
func kern(a []int32, k int32) int {
	n := 0
	for i := range a {
		if a[i] == k {
			n++
		}
	}
	return n
}

func main() {
	const n = 4096
	a := make([]int32, n)
	for i := range a {
		a[i] = int32(i % 251)
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int32(r % 251)
		total += kern(a, 42)
	}
	println("total =", total)
}
