// Constant shifts both ways, and a rotate built from them.  A vector shift
// by an immediate is a different instruction from one by a register, and a
// producer writes the immediate case constantly.
package main

//go:noinline
func kern(a, c []uint32) {
	for i := range c {
		v := a[i]
		c[i] = (v << 3) | (v >> 29)
	}
}

func main() {
	const n = 4096
	a := make([]uint32, n)
	c := make([]uint32, n)
	for i := range a {
		a[i] = uint32(i)*2654435761 + 1
	}
	total := 0
	for r := 0; r < 120000; r++ {
		a[r&(n-1)] = uint32(r)
		kern(a, c)
		total += int(c[r&(n-1)] & 0xffff)
	}
	println("total =", total)
}
