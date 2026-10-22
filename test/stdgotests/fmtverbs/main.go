// fmt's formatting engine: every verb (%v %+v %#v %T %t %d %b %o %O %x %X
// %c %q %U %e %E %f %F %g %G %s %p-free) over every kind of value, with
// widths, precisions, the flags + - # 0 and space, explicit argument indexes
// (%[2]d) and * widths, the %!verb(type=value) and %!(EXTRA ...) error forms,
// Stringer, GoStringer, error and Formatter implementations, nested structs,
// maps (which fmt prints with sorted keys), slices of pointers printed by
// value, and the Sscan/Sscanf/Sscanln family parsing it all back.
//
// Pointers are never printed with %p or %v at top level, since an address is
// not a deterministic output; pointers to structs print as &{...}, which is.
package main

import (
	"errors"
	"fmt"
	"os"
	"strings"
)

type Point struct {
	X, Y int
}

type Labeled struct {
	Name  string
	P     Point
	PP    *Point
	Tags  []string
	Attrs map[string]int
	Iface any
	priv  float64
}

type Celsius float64

func (c Celsius) String() string { return fmt.Sprintf("%.1f°C", float64(c)) }

type Secret string

func (s Secret) GoString() string { return `Secret("***")` }
func (s Secret) String() string   { return "***" }

type Money struct {
	Cents int64
}

func (m Money) Format(f fmt.State, verb rune) {
	switch verb {
	case 'v', 's':
		sign := ""
		c := m.Cents
		if c < 0 {
			sign, c = "-", -c
		}
		s := fmt.Sprintf("%s$%d.%02d", sign, c/100, c%100)
		if f.Flag('+') {
			s = "USD " + s
		}
		if w, ok := f.Width(); ok {
			if f.Flag('-') {
				s = s + strings.Repeat(" ", max(0, w-len(s)))
			} else {
				s = strings.Repeat(" ", max(0, w-len(s))) + s
			}
		}
		fmt.Fprint(f, s)
	case 'd':
		fmt.Fprintf(f, "%d", m.Cents)
	default:
		fmt.Fprintf(f, "%%!%c(Money)", verb)
	}
}

type myErr struct{ Code int }

func (e myErr) Error() string { return fmt.Sprintf("error code %d", e.Code) }

type Weekday int

const (
	Sunday Weekday = iota
	Monday
)

func (d Weekday) String() string { return [...]string{"Sun", "Mon"}[d] }

type Tree struct {
	Val         int
	Left, Right *Tree
}

func main() {
	p := Point{3, -4}
	l := Labeled{Name: "lbl", P: p, PP: nil, Tags: []string{"a", "b c"},
		Attrs: map[string]int{"z": 26, "a": 1, "m": 13}, Iface: Celsius(21.5), priv: 0.5}
	fmt.Printf("%%v   %v\n", l)
	fmt.Printf("%%+v  %+v\n", l)
	fmt.Printf("%%#v  %#v\n", p)
	fmt.Printf("%%T   %T | %T | %T | %T | %T\n", l, &l, l.Attrs, l.Iface, nil)
	fmt.Printf("ptr to struct %v %+v\n", &p, &p)
	for i, q := range []*Point{{1, 2}, {3, 4}, nil} {
		fmt.Printf("struct ptr %d: %v %+v\n", i, q, q)
	}
	t := Tree{1, &Tree{2, nil, nil}, nil}
	fmt.Printf("tree root %d, left %+v, right %v\n", t.Val, *t.Left, t.Right)
	pp := &l
	fmt.Printf("ptr to struct holding nil ptr %+v\n", pp)
	fmt.Printf("%%#v map %#v\n", map[string]int{"b": 2, "a": 1})
	fmt.Printf("%%#v slice %#v %#v %#v\n", []int{1, 2}, []string{"x"}, [2]bool{true})
	fmt.Printf("%%#v misc %#v %#v %#v %#v %#v %#v\n", 42, "s", 1.0, int8(-3), uint(7), any(nil))
	fmt.Printf("%%#v anon %#v\n", struct {
		A int
		B string
	}{1, "x"})

	// Integers.
	for _, n := range []int64{0, 7, -7, 255, -256, 1 << 40, -1 << 63} {
		fmt.Printf("[%d] [%5d] [%-5d|] [%05d] [%+d] [% d] [%b] [%o] [%O] [%#o] [%x] [%X] [%#x] [%#X] [%08x]\n",
			n, n, n, n, n, n, n, n, n, n, n, n, n, n, n)
	}
	fmt.Printf("unsigned %d %x %b\n", ^uint64(0), ^uint32(0), uint8(200))
	fmt.Printf("precision on ints [%.5d] [%8.3d] [%-8.3d] [%.0d] [%x]\n", 42, -7, 9, 0, -1)
	fmt.Printf("chars %c %c %c %q %q %U %#U %U\n", 'A', 0x4E16, 0x1F600, 'x', '\n', 0x1F600, 'ä', -1)
	fmt.Printf("byte slices %s %q %x % x %X % X %v\n", []byte("hi"), []byte("q\""), []byte("abc"), []byte("abc"), []byte{0xde, 0xad}, []byte{1, 2}, []byte("ab"))

	// Strings.
	for _, s := range []string{"", "go", "héllo", "tab\tq\"", "日本"} {
		fmt.Printf("[%s] [%8s] [%-8s] [%.2s] [%q] [%+q] [%#q] [%x] [% X] [%5.1q]\n", s, s, s, s, s, s, s, s, s, s)
	}

	// Floats.
	for _, f := range []float64{0, 1, -1.5, 3.14159265, 1e-9, 2.5e10, 123456.789} {
		fmt.Printf("[%v] [%e] [%E] [%f] [%F] [%g] [%G] [%.3e] [%10.2f] [%-10.1f|] [%+.0f] [%08.3f] [%#.3g]\n",
			f, f, f, f, f, f, f, f, f, f, f, f, f)
	}
	fmt.Printf("complex %v %.2f %+.1e %g\n", complex(1, -2), complex(3.14159, 2.71828), complex(-1, 0.5), complex64(complex(1e10, 1e-10)))

	// Booleans.
	fmt.Printf("bools %t %v %5t %-6t| %q\n", true, false, true, false, true)

	// Stringer, GoStringer, Formatter, error.
	c := Celsius(-3.25)
	fmt.Printf("stringer %v %s %d %.2f %q\n", c, c, Monday, c, c)
	fmt.Printf("stringer in slice %v and map %v\n", []Celsius{1, 2}, map[Weekday]Celsius{Monday: 1, Sunday: 0})
	sec := Secret("pw")
	fmt.Printf("gostringer %v %s %#v %+v\n", sec, sec, sec, sec)
	m := Money{-12345}
	fmt.Printf("formatter [%v] [%+v] [%12v] [%-12v|] [%d] [%x]\n", m, m, m, m, m, m)
	fmt.Println("formatter via Println:", m, Money{5})
	e := myErr{404}
	fmt.Printf("error %v %s %+v %q\n", e, e, e, e)
	fmt.Println("wrapped:", fmt.Errorf("ctx: %w", e), errors.Unwrap(fmt.Errorf("ctx: %w", e)) == error(e))

	// Argument indexes and star widths.
	fmt.Printf("%[2]d %[1]d %d\n", 10, 20)
	fmt.Printf("%[3]*.[2]*[1]f|\n", 12.0, 2, 6)
	fmt.Printf("%*d|%-*d|%.*f\n", 6, 42, 6, 42, 2, 3.14159)
	fmt.Printf("%[1]x %[1]o %[1]b %[1]q\n", 65)

	// Errors in format strings.
	fmt.Printf("bad verb %z\n", 3)
	fmt.Printf("wrong type %d %s\n", "str", 5)
	fmt.Printf("missing %d %d\n", 1)
	fmt.Printf("extra %d\n", 1, 2, "three")
	fmt.Printf("bad index %[5]d %[0]d\n", 1)
	fmt.Printf("nil values %d %s %v\n", nil, nil, nil)
	fmt.Printf("bad width %*d\n", "x", 5)
	fmt.Printf("percent at end %")
	fmt.Println()
	fmt.Printf("%!\n")

	// Println / Print spacing rules.
	fmt.Println("println", 1, "a", 2.5, true, nil, []int{1}, p)
	fmt.Print("print", 1, 2, "a", "b", 3, "\n")
	fmt.Print(1, 2, 3, "\n")
	s := fmt.Sprint("x", 1, 2, "y", 3.5, 'c')
	fmt.Println("sprint:", s)
	s = fmt.Sprintln("a", 1)
	fmt.Printf("sprintln: %q\n", s)
	fmt.Fprintln(os.Stdout, "fprintln to stdout", 42)
	fmt.Println("Append:", string(fmt.Appendf([]byte("pre:"), "%03d", 7)), string(fmt.Append(nil, 1, "x")), string(fmt.Appendln(nil, "y")))

	// Maps of various key types: fmt sorts keys.
	fmt.Println(map[int]string{3: "c", -1: "a", 2: "b", 10: "d"})
	fmt.Println(map[float64]bool{2.5: true, -1: false, 0: true})
	fmt.Println(map[bool]int{true: 1, false: 0})
	fmt.Println(map[Point]string{{2, 1}: "b", {1, 2}: "a", {1, 1}: "z"})
	fmt.Println(map[[2]int]int{{2, 0}: 1, {1, 5}: 2})
	fmt.Println(map[any]int{"s": 1, 2: 2, 1.5: 3, false: 4})

	// Widths with multi-byte runes and padding with zeros on strings.
	fmt.Printf("[%6s] [%-6s] [%06s] [%6.2s]\n", "äöü", "日本", "ab", "日本語")
	fmt.Printf("[%6v] [%-8v|] [%08v]\n", []int{1, 2}, p, 3.5)

	// Sscan family.
	var a, b2 int
	var f float64
	var str string
	n, err := fmt.Sscanf("12 abc 3.5 -7", "%d %s %g %d", &a, &str, &f, &b2)
	fmt.Println("Sscanf:", n, err, a, str, f, b2)
	n, err = fmt.Sscanf("x=0x1F,y=077", "x=%x,y=%o", &a, &b2)
	fmt.Println("Sscanf hex/oct:", n, err, a, b2)
	n, err = fmt.Sscanf("v:10", "v:%d", &a)
	fmt.Println("Sscanf literal:", n, err, a)
	n, err = fmt.Sscanf("abc", "%d", &a)
	fmt.Println("Sscanf bad:", n, err)
	var w1, w2, w3 string
	n, err = fmt.Sscan("one two\nthree", &w1, &w2, &w3)
	fmt.Println("Sscan:", n, err, w1, w2, w3)
	n, err = fmt.Sscanln("four five\nsix", &w1, &w2, &w3)
	fmt.Println("Sscanln:", n, err, w1, w2)
	var bl bool
	var r rune
	var u8 uint8
	n, err = fmt.Sscanf("true Z 255", "%t %c %d", &bl, &r, &u8)
	fmt.Println("Sscanf bool/rune/uint8:", n, err, bl, string(r), u8)
	n, err = fmt.Sscanf("256", "%d", &u8)
	fmt.Println("Sscanf overflow:", n, err)
	var q string
	n, err = fmt.Sscanf(`"quoted str" rest`, "%q", &q)
	fmt.Println("Sscanf %q:", n, err, q)

	// Sprintf of a big table to exercise buffer growth.
	var sb strings.Builder
	for i := 0; i < 40; i++ {
		fmt.Fprintf(&sb, "|%3d|%-6s|%7.3f|%#06x|%v|\n", i, strings.Repeat("*", i%7), float64(i)/7, i*i, i%3 == 0)
	}
	fmt.Print(sb.String())
}
