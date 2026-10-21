// A large struct passed and returned by value, the same source as
// test/gobench/bigstruct.
//
// Go's back end passes every argument and result through its own stack in
// linear memory, so the 80-byte struct goes there and back by memory.copy at
// each call, with memory.fill zeroing the result slot first.  Those are fixed
// sizes beyond the sixteen bytes warmc copies inline, so each is a runtime
// call.
package main

import "fmt"

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
	for r := 0; r < 10000000; r++ {
		m = scale(m, 1.0)
		m.a = float64(r & 3)
		total += int(trace(m))
	}
	fmt.Println("total =", total, "tag =", m.tag)
}
