// Signed 64-bit division and remainder.  `divmod` covers the 32-bit and
// unsigned forms; the signed 64-bit ones lower differently -- the overflow
// case -2^63 / -1 has to be excluded before `sdiv` can be used bare.
package main

//go:noinline
func ratios(a []int64, d int64) int64 {
	var s int64
	for i := range a {
		s += a[i]/d + a[i]%(d+1)
	}
	return s
}

func main() {
	const n = 2048
	a := make([]int64, n)
	for i := range a {
		a[i] = int64(i)*0x1e3779b97f4a7c1 - 1<<40
	}
	var total int64
	for r := 0; r < 55000; r++ {
		a[r&(n-1)] = int64(r) - 1<<20
		total += ratios(a, int64(r%13)+3)
	}
	println("total =", total)
}
