// Functions: multiple return values (passed straight into another call,
// discarded with _, swapped in a parallel assignment), named results with
// bare returns, variadic parameters called with no arguments, with a list and
// with an existing slice spread by ... (which aliases the caller's slice),
// mutual recursion, deep recursion, functions of many parameters of mixed
// widths (so arguments are passed in registers and on the stack at once),
// function types as parameters, results and map values, and the evaluation
// order of assignments and of index expressions.  Package-level variables
// are initialised in dependency order and init functions run in source
// order.
package main

import (
	"fmt"
	"os"
	"strings"
)

var (
	a1 = b1 + 1 // depends on b1, declared later
	b1 = c1 * 2
	c1 = initC()
)

var initLog []string

func initC() int {
	initLog = append(initLog, "initC")
	return 5
}

func init() { initLog = append(initLog, "init1") }
func init() { initLog = append(initLog, "init2") }

var bias = 0

func init() {
	if len(os.Args) > 1000 {
		bias = 1
	}
}

func divmod(a, b int) (int, int) { return a / b, a % b }

func minmax(xs ...int) (lo, hi int, ok bool) {
	if len(xs) == 0 {
		return // zero values
	}
	lo, hi = xs[0], xs[0]
	for _, x := range xs[1:] {
		lo = min(lo, x)
		hi = max(hi, x)
	}
	ok = true
	return
}

func sum(xs ...int) int {
	s := 0
	for _, x := range xs {
		s += x
	}
	return s
}

func zeroFirst(xs ...int) {
	if len(xs) > 0 {
		xs[0] = 0
	}
}

func join(sep string, parts ...string) string { return strings.Join(parts, sep) }

func describe(prefix string, vals ...any) string {
	return fmt.Sprintf("%s(%d): %v", prefix, len(vals), vals)
}

func three() (int, string, float64) { return 1 + bias, "two", 3.5 }

func takeThree(i int, s string, f float64) string { return fmt.Sprint(i, s, f) }

func isEven(n uint) bool {
	if n == 0 {
		return true
	}
	return isOdd(n - 1)
}

func isOdd(n uint) bool {
	if n == 0 {
		return false
	}
	return isEven(n - 1)
}

func ackermann(m, n int) int {
	switch {
	case m == 0:
		return n + 1
	case n == 0:
		return ackermann(m-1, 1)
	}
	return ackermann(m-1, ackermann(m, n-1))
}

func depth(n int) int {
	if n == 0 {
		return 0
	}
	var pad [8]int // a frame with some bulk
	pad[n%8] = n
	return depth(n-1) + pad[n%8] - n + 1
}

//go:noinline
func many(a int8, b uint16, c int32, d int64, e float32, f float64, g uint8,
	h int, i string, j bool, k uint64, l float32, m int16, n float64, o int64,
	p uint32, q []int, r byte, s float64, t int64) string {
	return fmt.Sprint(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t)
}

//go:noinline
func manyResults(x int) (int8, uint16, int32, int64, float32, float64, string, bool, [3]int) {
	return int8(x), uint16(x * 1000), int32(-x), int64(x) << 40, float32(x) / 3,
		float64(x) / 7, strings.Repeat("r", (x%5+5)%5), x%2 == 0, [3]int{x, x * x, -x}
}

type binop func(int, int) int

func apply(op binop, xs ...int) int {
	acc := xs[0]
	for _, x := range xs[1:] {
		acc = op(acc, x)
	}
	return acc
}

func makeAdder(k int) func(int) int { return func(x int) int { return x + k } }

func swapRet(a, b string) (string, string) { return b, a }

func countCalls() (f func() int, n *int) {
	c := 0
	return func() int { c++; return c }, &c
}

func main() {
	fmt.Println("-- init order", a1, b1, c1, initLog)
	fmt.Println("-- multiple results")
	q, r := divmod(17+bias, 5)
	fmt.Println(q, r)
	_, r2 := divmod(-17, 5)
	q3, _ := divmod(100, -7)
	fmt.Println(r2, q3)
	fmt.Println(takeThree(three()))
	lo, hi, ok := minmax(3, -1, 4, 1, -5, 9, 2, 6)
	fmt.Println(lo, hi, ok)
	fmt.Println(minmax())
	x, y := swapRet("a", "b")
	x, y = y, x
	fmt.Println(x, y)
	fmt.Println(manyResults(7 + bias))
	fmt.Println(manyResults(-12))
	fmt.Println("-- variadic")
	nums := []int{1, 2, 3, 4}
	fmt.Println(sum(), sum(1), sum(1, 2, 3), sum(nums...), sum(nums[1:3]...))
	zeroFirst(nums...) // aliases nums
	zeroFirst(5, 6)
	fmt.Println(nums)
	fmt.Println(join("-"), join("-", "a"), join(", ", "x", "y", "z"))
	parts := []string{"p", "q"}
	fmt.Println(join("+", parts...))
	fmt.Println(describe("none"), describe("some", 1, "two", 3.0, nil, []int{4}))
	vs := []any{"a", 2}
	fmt.Println(describe("spread", vs...), describe("nested", vs))
	fmt.Println("-- recursion")
	for _, n := range []uint{0, 1, 10, 77, 1000} {
		fmt.Print(isEven(n), isOdd(n), " ")
	}
	fmt.Println()
	for m := 0; m <= 2; m++ {
		for n := 0; n <= 4; n++ {
			fmt.Print(ackermann(m, n), " ")
		}
	}
	fmt.Println(ackermann(3, 3))
	fmt.Println(depth(3000), depth(3))
	fmt.Println("-- many arguments")
	fmt.Println(many(-1, 65535, -100000, 1<<50, 1.5, 2.25, 255, -7, "s", true,
		1<<63, -0.5, -300, 1e100, -1<<62, 4000000000, []int{1, 2}, 'z', 0.1, int64(bias)))
	for i := 0; i < 5; i++ {
		fmt.Println(many(int8(i*60), uint16(i*20000), int32(i)*-1000000, int64(i)<<60,
			float32(i)/7, float64(i)/7, uint8(i*99), i-2, strings.Repeat("x", i),
			i%2 == 0, uint64(i)*0x1111111111111111, float32(-i), int16(i*-9000),
			float64(i)*1e-300, int64(-i), uint32(i)*0x40000000, nums[:i%4], byte('a'+i),
			float64(i), int64(i*i)))
	}
	fmt.Println("-- function values")
	ops := map[string]binop{
		"+": func(a, b int) int { return a + b },
		"-": func(a, b int) int { return a - b },
		"*": func(a, b int) int { return a * b },
		"max": func(a, b int) int {
			if a > b {
				return a
			}
			return b
		},
	}
	for _, k := range []string{"+", "-", "*", "max"} {
		fmt.Print(k, "=", apply(ops[k], 3, 1, 4, 1, 5, 9, 2, 6), " ")
	}
	fmt.Println()
	adders := []func(int) int{makeAdder(1), makeAdder(10), makeAdder(-100)}
	v := 0
	for i := 0; i < 9; i++ {
		v = adders[i%3](v)
		fmt.Print(v, " ")
	}
	fmt.Println()
	var nilFn func()
	fmt.Println(nilFn == nil, ops["none"] == nil)
	fmt.Println("-- evaluation order")
	next, calls := countCalls()
	arr := make([]int, 6)
	i := 0
	i, arr[i] = 1, 9 // index evaluated before assignment
	fmt.Println(i, arr)
	arr[next()], arr[next()] = next(), next() // calls left to right
	fmt.Println(arr, *calls)
	a, b, c := next(), next(), next()
	fmt.Println(a, b, c)
	m := map[string]int{}
	k := "x"
	m[k], k = 1, "y"
	fmt.Println(m, k)
	s := []int{1, 2, 3}
	j := 0
	j, s[j], s[j+1] = 2, s[2], s[0]
	fmt.Println(j, s)
}
