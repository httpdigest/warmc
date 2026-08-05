// A large struct passed and returned by value.  Go copies it, which TinyGo
// emits as a fixed-size `memory.copy` at every call -- the small-constant
// case of the bulk operation, where `memcpy` only exercises the large one.
package main

type mat3 struct {
	a, b, c float64
	d, e, f float64
	g, h, i float64
	tag     int
}

//go:noinline
func scale(m mat3, k float64) mat3 {
	m.a *= k
	m.e *= k
	m.i *= k
	m.tag++
	return m
}

//go:noinline
func trace(m mat3) float64 { return m.a + m.e + m.i }

func main() {
	m := mat3{a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7, h: 8, i: 9}
	total := 0
	for r := 0; r < 40000000; r++ {
		m = scale(m, 1.0)
		m.a = float64(r & 3)
		total += int(trace(m))
	}
	println("total =", total, "tag =", m.tag)
}
