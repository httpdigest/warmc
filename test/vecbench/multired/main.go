// Four accumulators in one loop, each a different operation.  VEC_MAX_RED
// is four, so this is exactly at the limit and says whether the limit is
// reachable.
package main

//go:noinline
func kern(a []int32) (int32, int32, int32, int32) {
	s, x := int32(0), int32(0)
	lo, hi := int32(2147483647), int32(-2147483648)
	for i := range a {
		v := a[i]
		s += v
		x ^= v
		if v < lo {
			lo = v
		}
		if v > hi {
			hi = v
		}
	}
	return s, x, lo, hi
}

func main() {
	const n = 4096
	a := make([]int32, n)
	for i := range a {
		a[i] = int32(i%1997) - 900
	}
	total := 0
	for r := 0; r < 80000; r++ {
		a[r&(n-1)] = int32(r%1997) - 900
		s, x, lo, hi := kern(a)
		total += int(s ^ x ^ lo ^ hi)
	}
	println("total =", total)
}
