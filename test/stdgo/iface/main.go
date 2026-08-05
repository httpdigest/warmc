// Interface dispatch: a method call through an itab.
//
// Go's is the most expensive of the three shapes this tree measures.  A Rust
// trait object is two words with a static vtable and an AssemblyScript virtual
// call is a table index; Go's interface value carries a pointer to an *itab*
// that the runtime builds per (interface, concrete type) pair, and the call is
// a load from it followed by an indirect call.  test/gobench/iface is the
// TinyGo answer to the same source, and TinyGo implements interfaces a
// different way again -- it compares type ids -- so the two rows are not the
// same mechanism even though the Go is identical.
//
// The types alternate so the indirect branch cannot be predicted.
package main

import "fmt"

type shape interface {
	area() float64
	sides() uint32
}

type circle struct{ r float64 }
type rect struct{ w, h float64 }
type tri struct{ b, h float64 }

func (c circle) area() float64 { return 3.14159265358979 * c.r * c.r }
func (c circle) sides() uint32 { return 0 }
func (x rect) area() float64   { return x.w * x.h }
func (x rect) sides() uint32   { return 4 }
func (t tri) area() float64    { return 0.5 * t.b * t.h }
func (t tri) sides() uint32    { return 3 }

//go:noinline
func total(v []shape) (float64, uint32) {
	a := 0.0
	var n uint32
	for _, s := range v {
		a += s.area()
		n += s.sides()
	}
	return a, n
}

func main() {
	v := make([]shape, 0, 300)
	for i := 0; i < 300; i++ {
		x := float64(i%7) + 1.0
		switch i % 3 {
		case 0:
			v = append(v, circle{x})
		case 1:
			v = append(v, rect{x, x + 1})
		default:
			v = append(v, tri{x, x + 2})
		}
	}
	a := 0.0
	var n uint32
	for r := 0; r < 80000; r++ {
		da, dn := total(v)
		a += da
		n += dn
	}
	// The float as bits: a decimal rendering would round away exactly the
	// one-ulp disagreement worth catching.
	fmt.Println("a =", int64(a*1e6), "n =", n)
}
