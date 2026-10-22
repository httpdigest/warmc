// The high half of a uint16 product: c[i] = uint16(uint32(a[i])*uint32(b[i]) >> 16),
// which is fixed-point arithmetic in Q16.  TinyGo emits two i32.load16_u, an
// i32.mul, an i32.shr_u by 16 and an i32.store16: the product is formed in
// i32 and only its top half survives, so a 16-bit lane cannot hold it.
//
// NEON does it with UMULL and UMULL2 into .4s lanes and a UZP2 or SHRN back to
// sixteen bits.  warmc does not vectorise it yet ("body is not vectorisable:
// shru": the shift needs bits that a 16-bit lane does not have).  A rounding
// byte average, byte((uint16(a)+uint16(b)+1) >> 1), is refused for the same
// reason.  Here to show the gap.
package main

//go:noinline
func kern(a, b, c []uint16) {
	for i := range c {
		c[i] = uint16((uint32(a[i]) * uint32(b[i])) >> 16)
	}
}

func main() {
	const n = 4096
	a := make([]uint16, n)
	b := make([]uint16, n)
	c := make([]uint16, n)
	for i := range a {
		a[i] = uint16(i*40503 + 1)
		b[i] = uint16(i*9973 + 7)
	}
	total := 0
	for r := 0; r < 100000; r++ {
		a[r&(n-1)] = uint16(r * 40503)
		kern(a, b, c)
		total += int(c[(r*7)&(n-1)])
	}
	for i := range c {
		total += int(c[i]) * (i&7 + 1)
	}
	println("total =", total)
}
