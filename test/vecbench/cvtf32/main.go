// int32 to float32 and back: c[i] = int32(float32(a[i])*1.5 + 0.5).  TinyGo
// lowers the first conversion to f32.convert_i32_s and the second to
// i32.trunc_sat_f32_s followed by two selects, on f32.le and f32.ge against
// the int32 bounds, which is TinyGo's own saturation rule for an
// out-of-range conversion.  The inputs span the whole int32 range, so the
// int-to-float conversion rounds and some products overflow int32 and take
// the saturating path.
//
// NEON has SCVTF and FCVTZS on .4s, and the selects are a compare and a BSL,
// so the loop is vectorisable.  warmc does not vectorise it yet ("body op
// converts": no conversion is a lane operation to the vectoriser, and neither
// is a float compare).  Here to show the gap.
package main

//go:noinline
func kern(a, c []int32) {
	for i := range c {
		c[i] = int32(float32(a[i])*1.5 + 0.5)
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(uint32(i)*2654435761) >> uint(i&31)
	}
	total := 0
	for r := 0; r < 50000; r++ {
		a[r&(n-1)] = int32(uint32(r)*2246822519) >> uint(r&31)
		kern(a, c)
		total += int(c[(r*7)&(n-1)])
	}
	for i := range c {
		total += int(c[i])
	}
	println("total =", total)
}
