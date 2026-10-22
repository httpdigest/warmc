// Interfaces.  Dynamic dispatch over several implementations, type switches
// with multi-type cases, nil and default, type assertions (comma-ok and to
// another interface), interface embedding, value against pointer method sets,
// interface equality (same dynamic type and value), the nil interface against
// an interface holding a typed nil pointer, and boxing every kind of value in
// `any` -- each integer width, floats, complex, strings, slices, maps,
// pointers, funcs, channels, structs, arrays -- then unboxing it again.
// TinyGo implements interfaces with its own type codes and method lookup
// thunks, so this is mostly indirect calls and comparisons of type ids.
package main

import (
	"fmt"
	"os"
	"sort"
	"strings"
)

type Shape interface {
	Area() float64
	Perimeter() float64
}

type Named interface{ Name() string }

type NamedShape interface {
	Shape
	Named
}

type Rect struct{ W, H float64 }
type Circle struct{ R float64 }
type Square struct{ Rect }
type Tri struct{ A, B, C float64 }

func (r Rect) Area() float64         { return r.W * r.H }
func (r Rect) Perimeter() float64    { return 2 * (r.W + r.H) }
func (r Rect) Name() string          { return "rect" }
func (c *Circle) Area() float64      { return 3 * c.R * c.R }
func (c *Circle) Perimeter() float64 { return 6 * c.R }
func (c *Circle) Name() string       { return "circle" }
func (s Square) Name() string        { return "square" }
func (t Tri) Area() float64          { return t.A * t.B / 2 }
func (t Tri) Perimeter() float64     { return t.A + t.B + t.C }

type Celsius float64

func (c Celsius) String() string { return fmt.Sprintf("%.1f°C", float64(c)) }

type MyErr struct{ code int }

func (e *MyErr) Error() string { return fmt.Sprintf("myerr %d", e.code) }

//go:noinline
func mayFail(fail bool) error {
	var e *MyErr
	if fail {
		e = &MyErr{7}
	}
	return e // non-nil interface even when e is nil
}

//go:noinline
func reallyNil(fail bool) error {
	if fail {
		return &MyErr{8}
	}
	return nil
}

func classify(v any) string {
	switch x := v.(type) {
	case nil:
		return "nil"
	case int, int8, int16, int32, int64:
		return fmt.Sprintf("signed %v", x)
	case uint, uint8, uint16, uint32, uint64, uintptr:
		return fmt.Sprintf("unsigned %v", x)
	case float32:
		return fmt.Sprintf("float32 %v", x*2)
	case float64:
		return fmt.Sprintf("float64 %v", x*2)
	case complex64, complex128:
		return fmt.Sprintf("complex %v", x)
	case string:
		return "string " + strings.ToUpper(x)
	case []int:
		return fmt.Sprintf("[]int len %d", len(x))
	case map[string]int:
		return fmt.Sprintf("map len %d", len(x))
	case *int:
		return fmt.Sprintf("*int -> %d", *x)
	case func(int) int:
		return fmt.Sprintf("func gives %d", x(20))
	case chan int:
		return fmt.Sprintf("chan cap %d", cap(x))
	case error:
		return "error " + x.Error()
	case fmt.Stringer:
		return "stringer " + x.String()
	case NamedShape:
		return fmt.Sprintf("namedshape %s %.2f", x.Name(), x.Area())
	case Shape:
		return fmt.Sprintf("shape %.2f", x.Area())
	case [3]int:
		return fmt.Sprintf("array sum %d", x[0]+x[1]+x[2])
	case struct{ A, B int }:
		return fmt.Sprintf("anon struct %d", x.A*x.B)
	case bool:
		if x {
			return "true bool"
		}
		return "false bool"
	default:
		return fmt.Sprintf("other %T", x)
	}
}

var scale = 1.0

func init() {
	if len(os.Args) > 1000 {
		scale = 2
	}
}

func main() {
	fmt.Println("-- dispatch")
	shapes := []Shape{Rect{3 * scale, 4}, &Circle{2 * scale}, Square{Rect{5, 5}},
		Tri{3, 4, 5}, Rect{1, 1}, &Circle{0.5}}
	total := 0.0
	for i, s := range shapes {
		total += s.Area()
		n := "?"
		if nm, ok := s.(Named); ok {
			n = nm.Name()
		}
		_, isNS := s.(NamedShape)
		fmt.Printf("%d %T %s %.2f %.2f %v\n", i, s, n, s.Area(), s.Perimeter(), isNS)
	}
	fmt.Printf("total %.3f\n", total)
	sort.Slice(shapes, func(i, j int) bool { return shapes[i].Area() < shapes[j].Area() })
	for _, s := range shapes {
		fmt.Printf("%.2f ", s.Area())
	}
	fmt.Println()
	fmt.Println("-- assertions")
	var s Shape = Rect{2, 3}
	r, ok := s.(Rect)
	fmt.Println(r, ok)
	c, ok := s.(*Circle)
	fmt.Println(c, ok)
	sq, ok := s.(Square)
	fmt.Println(sq, ok)
	var a any = Square{Rect{2, 2}}
	_, isShape := a.(Shape) // promoted methods count
	_, isNamed := a.(Named) // Square's own Name
	ns, isNS := a.(NamedShape)
	fmt.Println(isShape, isNamed, isNS, ns.Name(), ns.Area())
	var circ any = Circle{1} // value: pointer methods not in the method set
	_, cs := circ.(Shape)
	var circp any = &Circle{1}
	_, cps := circp.(Shape)
	fmt.Println(cs, cps)
	fmt.Println("-- equality")
	var x1, x2 any = 1, 1
	var x3 any = int64(1)
	var x4 any = "1"
	var s1, s2 Shape = Rect{1, 2}, Rect{1, 2}
	cp := &Circle{1}
	var s3, s4 Shape = cp, cp
	var s5 Shape = &Circle{1}
	fmt.Println(x1 == x2, x1 == x3, x1 == x4, s1 == s2, s3 == s4, s3 == s5)
	var nilAny any
	var nilShape Shape
	fmt.Println(nilAny == nil, nilShape == nil, nilAny == any(nilShape))
	fmt.Println("-- typed nil")
	e1 := mayFail(false)
	e2 := mayFail(true)
	e3 := reallyNil(false)
	e4 := reallyNil(true)
	fmt.Println(e1 == nil, e2 == nil, e3 == nil, e4 == nil)
	fmt.Println(e1.(*MyErr) == nil, e2, e4)
	var np *Circle
	var ps Shape = np
	fmt.Println(ps == nil, ps != nil)
	fmt.Println("-- boxing every kind")
	n := 42
	ch := make(chan int, 3)
	values := []any{
		nil, true, false, int(-1), int8(-8), int16(-16), int32(-32), int64(-64),
		uint(1), uint8(8), uint16(16), uint32(32), uint64(64), uintptr(99),
		float32(1.25), 2.5, complex64(1 + 2i), complex(3, -4), "go",
		[]int{1, 2, 3}, map[string]int{"a": 1}, &n, func(x int) int { return x + n },
		ch, &MyErr{3}, Celsius(21.5), Rect{2, 5}, &Circle{1}, Tri{1, 2, 3},
		[3]int{1, 2, 3}, struct{ A, B int }{6, 7}, []string{"x"}, 'r', byte('b'),
		Square{Rect{1, 1}},
	}
	for _, v := range values {
		fmt.Printf("%-10T %s\n", v, classify(v))
	}
	fmt.Println("-- unboxing round trips")
	sum := int64(0)
	for i := 0; i < 100; i++ {
		var box any
		switch i % 6 {
		case 0:
			box = int8(i)
		case 1:
			box = uint16(i * 300)
		case 2:
			box = int64(-i) << 40
		case 3:
			box = float32(i) / 3
		case 4:
			box = fmt.Sprint(i)
		case 5:
			box = Celsius(i)
		}
		switch v := box.(type) {
		case int8:
			sum += int64(v)
		case uint16:
			sum += int64(v)
		case int64:
			sum += v >> 30
		case float32:
			sum += int64(v * 3)
		case string:
			sum += int64(len(v))
		case Celsius:
			sum += int64(v) * 2
		}
	}
	fmt.Println(sum)
	fmt.Println("-- interface slices and maps")
	byKind := map[string][]any{}
	for _, v := range values {
		k := fmt.Sprintf("%T", v)
		if i := strings.IndexAny(k, "[*"); i == 0 {
			k = "composite"
		}
		byKind[k[:1]] = append(byKind[k[:1]], v)
	}
	keys := make([]string, 0, len(byKind))
	for k := range byKind {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		fmt.Println(k, len(byKind[k]))
	}
	var st fmt.Stringer = Celsius(-3)
	fmt.Println(st, st.String(), Celsius(100))
	fmt.Printf("%v %s %d\n", Celsius(5), Celsius(6), 7)
}
