// Sixteen bytes in, four int32 accumulators out.  Accumulating in the narrow
// lane would wrap at 256 and give a different answer, so this is a widening
// reduction and not merely a fast one.
package main

//go:noinline
func kern(a []byte) uint32 {
	var s uint32
	for i := range a {
		s += uint32(a[i])
	}
	return s
}

func main() {
	const n = 4096
	a := make([]byte, n)
	for i := range a {
		a[i] = byte(i*7 + 1)
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = byte(r)
		total += int(kern(a))
	}
	println("total =", total)
}
