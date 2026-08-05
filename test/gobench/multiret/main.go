// Several return values.  Go's multi-value return is wasm multi-value, which
// warmc gives a return area addressed through x8 -- a path no single-result
// kernel reaches.
package main

//go:noinline
func divmod3(a, b int) (int, int, bool) {
	if b == 0 {
		return 0, 0, false
	}
	return a / b, a % b, true
}

//go:noinline
func minmax(a []int32) (int32, int32, int) {
	lo, hi := a[0], a[0]
	for _, v := range a {
		if v < lo {
			lo = v
		}
		if v > hi {
			hi = v
		}
	}
	return lo, hi, len(a)
}

func main() {
	const n = 1024
	a := make([]int32, n)
	for i := range a {
		a[i] = int32(uint32(i) * 2654435761 >> 8)
	}
	total := 0
	for r := 0; r < 230000; r++ {
		a[r&(n-1)] = int32(r)
		lo, hi, ln := minmax(a)
		q, m, ok := divmod3(int(hi-lo), (r&7)+1)
		if ok {
			total += q + m + ln
		}
	}
	println("total =", total)
}
