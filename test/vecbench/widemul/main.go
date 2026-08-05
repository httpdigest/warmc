// A widening multiply feeding a widening sum: int32 lanes multiplied into
// int64 accumulators.  The product does not fit the lane it came from, so
// this cannot be done by accumulating narrow and widening at the end.
package main

//go:noinline
func kern(a, b []int32) int64 {
	var s int64
	for i := range a {
		s += int64(a[i]) * int64(b[i])
	}
	return s
}

func main() {
	const n = 4096
	a := make([]int32, n)
	b := make([]int32, n)
	for i := range a {
		a[i] = int32(i%65521) - 30000
		b[i] = int32(i%65519) - 30000
	}
	total := 0
	for r := 0; r < 160000; r++ {
		a[r&(n-1)] = int32(r%65521) - 30000
		total += int(kern(a, b))
	}
	println("total =", total)
}
