// and, or, xor and not in one body.  All four are single NEON instructions,
// so a body that mixes them should vectorise whole or not at all.
package main

//go:noinline
func kern(a, b, c []uint32) {
	for i := range c {
		c[i] = (a[i] & b[i]) ^ (a[i] | b[i]) ^ ^a[i]
	}
}

func main() {
	const n = 4096
	a := make([]uint32, n)
	b := make([]uint32, n)
	c := make([]uint32, n)
	for i := range a {
		a[i] = uint32(i)*2654435761 + 1
		b[i] = uint32(i*40503 + 7)
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = uint32(r)
		kern(a, b, c)
		total += int(c[r&(n-1)] & 0xffff)
	}
	println("total =", total)
}
