// fmt formatting verbs over every kind of operand.  %v %+v %#v %T on
// scalars, structs (nested and through pointers), slices, arrays, maps
// (printed in sorted key order), nil values and interfaces; integers with
// %d %b %o %O %x %X %#x %c %q %U %#U and width, zero padding, sign and
// left-justify flags; floats with %e %E %f %F %g %G %x and widths and
// precisions including %08.3f and %+.2e; strings and byte slices with %s %q
// %x % x %10s %-10s %.3s; booleans; argument indexes %[2]d; * widths;
// complex numbers; Stringer, error and GoStringer dispatch; and the
// diagnostics for wrong verbs, missing and extra arguments.  Sprint and
// Sprintln spacing rules are covered as well.
package main

import (
	"errors"
	"fmt"
	"math"
	"os"
)

type inner struct {
	A int
	B []string
}

type outer struct {
	Name  string
	In    inner
	Ptr   *inner
	M     map[string]int
	F     float64
	priv  bool
	Iface any
}

type color int

func (c color) String() string { return [...]string{"red", "green", "blue"}[c%3] }

type gs struct{ v int }

func (g gs) GoString() string { return fmt.Sprintf("gs<%d>", g.v) }

type pt struct{ X, Y int }

var n = 42

func init() {
	if len(os.Args) > 1000 {
		n = 43
	}
}

func main() {
	fmt.Println("-- integers")
	ints := []int64{0, 1, -1, int64(n), -255, 65535, 1 << 40, math.MinInt64, math.MaxInt64}
	for _, i := range ints {
		fmt.Printf("|%d|%5d|%-5d|%05d|%+d|%x|%X|%#x|%o|%O|%b|\n", i, i, i, i, i, i, i, i, i, i, i)
	}
	for _, u := range []uint64{0, 7, 255, math.MaxUint64} {
		fmt.Printf("|%d|%x|%#o|%#b|%20d|%-20x|\n", u, u, u, u, u, u)
	}
	for _, r := range []rune{'A', 'é', '日', 0x1F600, 0} {
		fmt.Printf("|%c|%q|%U|%#U|%d|%x|\n", r, r, r, r, r, r)
	}
	fmt.Printf("|%3c|%-3c|%08b|%.3d|%6.3d|%x|% x|% d|\n", 'x', 'y', byte(5), 7, 7, -255, 255, 5)
	fmt.Println("-- floats")
	floats := []float64{0, 1, -1, 3.14159265358979, -0.000123456, 123456789.123, 1e21, 1e-7,
		math.Inf(1), math.Inf(-1), math.NaN(), math.Copysign(0, -1), 2.5, 0.125}
	for _, f := range floats {
		fmt.Printf("|%v|%e|%E|%f|%.2f|%8.3f|%08.3f|%-8.3f|%+.2e|%g|%G|%.3g|%10.4g|\n",
			f, f, f, f, f, f, f, f, f, f, f, f, f)
		fmt.Printf("  |%x|%.2x|%X|%F|%#g|%#.0f|%6.0f|\n", f, f, f, f, f, f, f)
	}
	f32 := float32(0.1)
	fmt.Printf("%v %g %.10f %v\n", f32, f32, f32, float64(f32))
	fmt.Println("-- strings and bytes")
	strs := []string{"", "go", "hello, world", "tab\there", "日本語", "quote\"d", "\x00\x01"}
	for _, s := range strs {
		fmt.Printf("|%s|%q|%+q|%#q|%x|% x|%X|%10s|%-10s|%.3s|%5.1s|\n", s, s, s, s, s, s, s, s, s, s, s)
	}
	// Invalid UTF-8 only through the quoting and hex verbs: the output
	// itself stays valid UTF-8.
	bad := "a\xffb\xc3"
	fmt.Printf("%q|%+q|%#q|%x|% X|%q\n", bad, bad, bad, bad, bad, []byte(bad))
	bs := []byte("bytes\n")
	fmt.Printf("%s|%q|%x|% X|%v|%d\n", bs, bs, bs, bs, bs, bs)
	fmt.Printf("%v %t %5t %-6t|\n", true, false, true, false)
	fmt.Println("-- composites")
	// A pointer nested in a value prints as an address, which depends on the
	// heap layout and so on the program's arguments and environment; only
	// top-level pointers (printed as &{...}) and nil ones appear here.
	in := &inner{7, []string{"x", "y"}}
	o := outer{"top", inner{1, nil}, nil, map[string]int{"b": 2, "a": 1, "c": 3}, 2.5, true, pt{1, 2}}
	fmt.Printf("%v\n%+v\n", o, o)
	fmt.Printf("%#v\n", inner{3, []string{"q"}})
	fmt.Printf("%v %+v\n", in, in)
	fmt.Printf("%v %d %x %q\n", []int{1, 2, 3}, []int{10, 11}, []int{255, 16}, []string{"a", "b"})
	fmt.Printf("%v %#v %T\n", [3]bool{true}, [2]string{"p", "q"}, [2]string{})
	fmt.Printf("%v %#v\n", map[int]string{3: "c", 1: "a", 2: "b"}, map[string]bool{"z": true, "a": false})
	fmt.Printf("%v %+v %#v\n", pt{1, -2}, pt{3, 4}, pt{5, 6})
	fmt.Printf("%v %#v %v %#v\n", []any{1, "a", nil, 2.5}, []any{1, "a"}, map[pt]int{{2, 1}: 1, {1, 2}: 2}, struct{}{})
	var np *pt
	var ni any
	var ns []int
	var nm map[string]int
	var ne error
	fmt.Printf("%v %v %v %v %v %+v %#v %#v\n", np, ni, ns, nm, ne, ns, ns, nm)
	fmt.Println("-- types")
	vals := []any{1, int8(1), uint(1), 1.5, float32(1.5), "s", 'r', true, nil, []int{}, map[string]pt{},
		pt{}, &pt{}, [2]int{}, color(1), errors.New("e"), func() {}, make(chan int), complex(1, 2), struct{ A int }{}}
	for _, v := range vals {
		fmt.Printf("%T|", v)
	}
	fmt.Println()
	fmt.Println("-- methods")
	fmt.Printf("%v %s %d %q %x %+v %#v\n", color(0), color(1), color(2), color(4), color(5), color(1), color(1))
	fmt.Printf("%v %+v %#v\n", gs{3}, gs{4}, gs{5})
	fmt.Printf("%v %s %q\n", errors.New("an error"), errors.New("e2"), errors.New("e3"))
	fmt.Printf("%v\n", []color{0, 1, 2})
	fmt.Printf("%v\n", map[color]int{2: 20, 0: 0, 1: 10})
	fmt.Println("-- complex")
	for _, c := range []complex128{0, 1 + 2i, -1.5 - 0.25i, complex(math.Inf(1), math.NaN())} {
		fmt.Printf("%v %.2f %e %8.1f|\n", c, c, c, c)
	}
	fmt.Printf("%v\n", complex64(1+1i))
	fmt.Println("-- indexes and stars")
	fmt.Printf("%[2]d %[1]d %d|\n", 10, 20)
	fmt.Printf("%[3]*.[2]*[1]f|\n", 12.0, 2, 6)
	fmt.Printf("%*d|%-*d|%.*f|\n", 6, n, 6, n, 3, math.Pi)
	fmt.Printf("%[1]x %[1]X %[1]o %[1]b %[1]c\n", 77)
	fmt.Println("-- diagnostics")
	fmt.Printf("%d|%s|%z\n", "str", 5, 1)
	fmt.Printf("%d %d\n", 1)
	fmt.Printf("%d\n", 1, 2, "x")
	fmt.Printf("%!|%|100%%\n")
	fmt.Printf("%.2s|%[5]d|%[0]d\n", "abc", 1)
	fmt.Println("-- Sprint spacing")
	fmt.Println(fmt.Sprint("a", "b", 1, 2, "c", 3.5, true, false))
	fmt.Println(fmt.Sprint(1, 2, 3), fmt.Sprint("x", 1, 2, "y"))
	fmt.Print(fmt.Sprintln("a", 1, "b", 2.0, nil))
	fmt.Printf("%q\n", fmt.Sprintln())
	s := fmt.Sprintf("%6.2f|%-8s|%08d|%x", 3.14159, "ab", -42, "hi")
	fmt.Println(s, len(s))
	fmt.Println(fmt.Append([]byte("pre:"), 1, "x", 2.5), string(fmt.Appendf(nil, "%03d", 7)))
	table := [][]any{{"name", "qty", "price"}, {"apple", 3, 0.5}, {"watermelon", 12, 3.25}, {"fig", 100, 12.0}}
	for _, row := range table {
		fmt.Printf("%-12v|%5v|%8v|\n", row[0], row[1], row[2])
	}
}
