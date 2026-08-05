// and, or and xor as reductions rather than elementwise.  Each has its own
// identity -- all ones for and, zero for the others -- and getting one wrong
// is invisible unless the data reaches the identity, which this does.
package main

//go:noinline
func kern(a []uint32) (uint32, uint32, uint32) {
	x, y, z := ^uint32(0), uint32(0), uint32(0)
	for i := range a {
		x &= a[i]
		y |= a[i]
		z ^= a[i]
	}
	return x, y, z
}

func main() {
	const n = 4096
	a := make([]uint32, n)
	for i := range a {
		a[i] = uint32(i)*2654435761 + 1
	}
	total := 0
	for r := 0; r < 180000; r++ {
		a[r&(n-1)] = uint32(r)
		x, y, z := kern(a)
		total += int((x ^ y ^ z) & 0xffff)
	}
	println("total =", total)
}
