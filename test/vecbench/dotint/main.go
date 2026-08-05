// An integer dot product: a multiply feeding a sum.  Unlike the float one in
// the other corpus this may be reassociated, so it is a reduction whose term
// is an expression rather than a bare load.
package main

//go:noinline
func kern(a, b []int32) int32 {
	var s int32
	for i := range a {
		s += a[i] * b[i]
	}
	return s
}

func main() {
	const n = 4096
	a := make([]int32, n)
	b := make([]int32, n)
	for i := range a {
		a[i] = int32(i%251 + 1)
		b[i] = int32(i%157 + 1)
	}
	total := 0
	for r := 0; r < 180000; r++ {
		a[r&(n-1)] = int32(r % 251)
		total += int(kern(a, b))
	}
	println("total =", total)
}
