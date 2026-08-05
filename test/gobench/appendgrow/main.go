// append() with growth: the reallocate-and-copy path, which is a runtime call
// plus a memory.copy each time the slice doubles.
package main

//go:noinline
func collect(n int, seed uint32) []uint32 {
	out := make([]uint32, 0, 8)
	x := seed
	for i := 0; i < n; i++ {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		if x&3 != 0 {
			out = append(out, x)
		}
	}
	return out
}

func main() {
	total := 0
	for r := 0; r < 12000; r++ {
		v := collect(2048, uint32(r)+1)
		total += len(v) + int(v[0]&0xff)
	}
	println("total =", total)
}
