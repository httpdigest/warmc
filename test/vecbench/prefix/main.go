// A prefix sum: c[i] = a[0] + ... + a[i].  The carried value is a running total
// like a reduction's, but every intermediate value is stored, so it cannot be
// split into four independent lane sums and combined at the end.  TinyGo
// emits one i32.load, an i32.add into the carried local and an i32.store of
// the same value.
//
// It only partly vectorises: within a vector the scan is two shift-and-add
// steps (EXT and ADD on .4s), and the last lane is splatted and carried into
// the next vector.  warmc does not do that yet -- it rejects the loop with
// "body is not vectorisable: param", because the carried value is used by the
// store rather than only by its own update.  Here to show the gap, and to
// keep a scan from being mistaken for a reduction: vectorising it as one
// would store the wrong partial sums and change the checksum.
package main

//go:noinline
func kern(a, c []int32) {
	var s int32
	for i := range c {
		s += a[i]
		c[i] = s
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i%251) - 125
	}
	total := 0
	for r := 0; r < 85000; r++ {
		a[r&(n-1)] = int32(r%251) - 125
		kern(a, c)
		total += int(c[(r*7)&(n-1)]) + int(c[n-1])
	}
	println("total =", total)
}
