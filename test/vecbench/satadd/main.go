// A saturating byte add written the way Go code writes it, with a compare and
// a select: s := a+b; if s < a { s = 255 }.  LLVM recognises the idiom and,
// with no SIMD in the module, lowers it in i32: two i32.load8_u, an i32.add
// that is not wrapped to eight bits, a select of 255 when the sum is
// i32.ge_u 255, and an i32.store8.  So the overflow test the source wrote on
// the wrapped byte arrives as a test of the ninth bit of a wider sum.
//
// NEON does all of it in one UQADD.16b, sixteen lanes at a time.  warmc does
// not vectorise it yet ("body is not vectorisable: select": the select is on
// a value wider than the byte lane it came from), and runs it as a scalar
// loop.  Here to show the gap.
package main

//go:noinline
func kern(a, b, c []byte) {
	for i := range c {
		s := a[i] + b[i]
		if s < a[i] {
			s = 255
		}
		c[i] = s
	}
}

func main() {
	const n = 4096
	a := make([]byte, n)
	b := make([]byte, n)
	c := make([]byte, n)
	for i := range a {
		a[i] = byte(i*7 + 1)
		b[i] = byte(i*13 + 5)
	}
	total := 0
	for r := 0; r < 90000; r++ {
		a[r&(n-1)] = byte(r)
		kern(a, b, c)
		total += int(c[(r*7)&(n-1)])
	}
	for i := range c {
		total += int(c[i]) * (i&7 + 1)
	}
	println("total =", total)
}
