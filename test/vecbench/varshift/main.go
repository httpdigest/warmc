// A shift by an amount that is the same for every lane but not known until
// run time.  A vector shift takes an immediate, so this needs the register
// form -- or the loop-invariant amount splatted.
package main

//go:noinline
func kern(a, c []uint32, k uint32) {
	for i := range c {
		c[i] = a[i] << k
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
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = uint32(r)
		kern(a, c, uint32(r&7))
		total += int(c[r&(n-1)] & 0xffff)
	}
	println("total =", total)
}
