// Closures, function values and methods as values: counters and
// accumulators sharing captured variables, per-iteration loop variables
// (Go 1.22 semantics for three-clause and range loops), closures capturing
// by reference and seeing later writes, recursive closures, closures
// returning closures (currying, composition, memoisation), method values
// bound to values vs pointers (and when the receiver is copied), method
// expressions with value and pointer receivers, interface method values,
// functions in maps and slices as dispatch tables, variadic functions and
// spreading, multiple return values through function values, defer with
// captured variables, and a small stack-machine interpreter built from
// closures.
//
// Each closure on wasm is a context pointer plus a function-table index; a
// call through a func value is a call_indirect with the context passed in a
// dedicated register-like global, so these exercise indirect calls of many
// signatures.
package main

import (
	"fmt"
	"sort"
	"strings"
)

type Counter struct {
	name string
	n    int
}

func (c Counter) Get() int            { return c.n }
func (c *Counter) Inc() int           { c.n++; return c.n }
func (c Counter) Label() string       { return fmt.Sprintf("%s=%d", c.name, c.n) }
func (c *Counter) Add(k int) *Counter { c.n += k; return c }

type Op func(stack []float64) ([]float64, error)

func binop(name string, f func(a, b float64) float64) Op {
	return func(st []float64) ([]float64, error) {
		if len(st) < 2 {
			return st, fmt.Errorf("%s: stack underflow", name)
		}
		a, b := st[len(st)-2], st[len(st)-1]
		return append(st[:len(st)-2], f(a, b)), nil
	}
}

func push(v float64) Op {
	return func(st []float64) ([]float64, error) { return append(st, v), nil }
}

func compose[A, B, C any](f func(A) B, g func(B) C) func(A) C {
	return func(a A) C { return g(f(a)) }
}

func curry(f func(int, int, int) int) func(int) func(int) func(int) int {
	return func(a int) func(int) func(int) int {
		return func(b int) func(int) int {
			return func(c int) int { return f(a, b, c) }
		}
	}
}

func sum(xs ...int) (total, count int) {
	for _, x := range xs {
		total += x
	}
	return total, len(xs)
}

func apply(f func(...int) (int, int), xs ...int) string {
	t, c := f(xs...)
	return fmt.Sprintf("%d/%d", t, c)
}

func main() {
	// Counters sharing state.
	makeCounter := func() (inc func() int, get func() int, reset func()) {
		n := 0
		inc = func() int { n++; return n }
		get = func() int { return n }
		reset = func() { n = 0 }
		return
	}
	inc, get, reset := makeCounter()
	inc2, get2, _ := makeCounter()
	inc()
	inc()
	inc2()
	fmt.Println("counters", get(), get2(), inc(), inc2())
	reset()
	fmt.Println("after reset", get(), get2())

	// Accumulator generator.
	acc := func(start float64) func(float64) float64 {
		return func(d float64) float64 { start += d; return start }
	}
	a1 := acc(10)
	a1(5)
	a2 := acc(1.5)
	fmt.Println("accumulators", a1(2.5), a2(0.25), a1(0))

	// Loop variables: per-iteration.
	var fs []func() int
	for i := 0; i < 5; i++ {
		fs = append(fs, func() int { return i * 10 })
	}
	var gs []func() string
	for i, s := range []string{"a", "b", "c"} {
		gs = append(gs, func() string { return fmt.Sprint(i, s) })
	}
	var hs []func() int
	for i := range 3 {
		hs = append(hs, func() int { i += 100; return i })
	}
	var out []string
	for _, f := range fs {
		out = append(out, fmt.Sprint(f()))
	}
	for _, g := range gs {
		out = append(out, g())
	}
	for _, h := range hs {
		out = append(out, fmt.Sprint(h(), h()))
	}
	fmt.Println("loop captures", out)
	// Modifying the loop variable inside the body affects only that iteration's copy.
	var seen []int
	for i := 0; i < 10; i++ {
		f := func() { i++ }
		f()
		seen = append(seen, i)
	}
	fmt.Println("body-modified loop var", seen)

	// Capture by reference.
	x := 1
	show := func() int { return x }
	x = 2
	fmt.Println("captured by reference", show())
	ptrs := make([]*int, 3)
	for i := range ptrs {
		ptrs[i] = &i
	}
	fmt.Println("distinct loop var addresses", *ptrs[0], *ptrs[1], *ptrs[2], ptrs[0] != ptrs[1])

	// Recursive closures.
	var fib func(int) int
	fib = func(n int) int {
		if n < 2 {
			return n
		}
		return fib(n-1) + fib(n-2)
	}
	var ack func(m, n int) int
	ack = func(m, n int) int {
		switch {
		case m == 0:
			return n + 1
		case n == 0:
			return ack(m-1, 1)
		}
		return ack(m-1, ack(m, n-1))
	}
	fmt.Println("recursive closures fib(20)", fib(20), "ack(2,3)", ack(2, 3), "ack(3,3)", ack(3, 3))

	// Composition and currying.
	toUpper := strings.ToUpper
	exclaim := func(s string) string { return s + "!" }
	length := func(s string) int { return len(s) }
	shout := compose(toUpper, exclaim)
	shoutLen := compose(shout, length)
	fmt.Println("compose", shout("hey"), shoutLen("hello"))
	vol := curry(func(a, b, c int) int { return a*100 + b*10 + c })
	fmt.Println("curry", vol(1)(2)(3), vol(9)(8)(7))
	partial := vol(5)(5)
	fmt.Println("partial", partial(0), partial(9))

	// Method values: bound at evaluation time.
	c := Counter{"c", 1}
	getV := c.Get     // copies c now
	labelV := c.Label // copies c now
	incP := c.Inc     // &c bound
	incP()
	incP()
	fmt.Println("method values", getV(), labelV(), c.Get(), c.Label())
	pc := &Counter{"p", 10}
	getFromPtr := pc.Get // copies *pc now
	pc.Inc()
	fmt.Println("value method via pointer, copied at bind", getFromPtr(), pc.Get())
	// Method expressions.
	getE := Counter.Get
	incE := (*Counter).Inc
	labelE := (*Counter).Label
	fmt.Println("method exprs", getE(c), incE(&c), labelE(&c))
	fmt.Println("chained", pc.Add(5).Add(-2).Label())
	// Interface method values.
	var st fmt.Stringer = &strings.Builder{}
	str := st.String
	st.(*strings.Builder).WriteString("written after binding")
	fmt.Println("interface method value", str())

	// Dispatch tables.
	ops := map[string]func(int, int) int{
		"add": func(a, b int) int { return a + b },
		"sub": func(a, b int) int { return a - b },
		"mul": func(a, b int) int { return a * b },
		"pow": func(a, b int) int {
			r := 1
			for ; b > 0; b-- {
				r *= a
			}
			return r
		},
	}
	names := make([]string, 0, len(ops))
	for k := range ops {
		names = append(names, k)
	}
	sort.Strings(names)
	for _, n := range names {
		fmt.Printf("op %s(7,3)=%d\n", n, ops[n](7, 3))
	}
	pipeline := []func(string) string{strings.TrimSpace, strings.ToLower, func(s string) string { return strings.ReplaceAll(s, " ", "-") }, exclaim}
	s := "  Hello Closure World  "
	for i, f := range pipeline {
		s = f(s)
		fmt.Printf("pipeline step %d: %q\n", i, s)
	}

	// Variadic.
	fmt.Println("variadic", apply(sum), apply(sum, 1), apply(sum, 1, 2, 3), apply(sum, []int{4, 5, 6, 7}...))
	t, n := sum()
	fmt.Println("empty variadic", t, n)
	appendAll := func(dst []string, srcs ...[]string) []string {
		for _, s := range srcs {
			dst = append(dst, s...)
		}
		return dst
	}
	fmt.Println("variadic slices", appendAll(nil, []string{"a"}, nil, []string{"b", "c"}))

	// Defer with captured variables vs arguments.
	func() {
		v := 1
		defer fmt.Println("defer arg (evaluated at defer):", v)
		defer func() { fmt.Println("defer closure (sees final):", v) }()
		defer func(w int) { fmt.Println("defer param:", w, "captured:", v) }(v * 100)
		v = 42
	}()

	// Stack-machine interpreter built from closures.
	prog := []Op{push(3), push(4), binop("add", func(a, b float64) float64 { return a + b }), push(5),
		binop("mul", func(a, b float64) float64 { return a * b }), push(2), binop("div", func(a, b float64) float64 { return a / b }),
		push(0.5), binop("sub", func(a, b float64) float64 { return a - b })}
	var stack []float64
	for i, op := range prog {
		var err error
		stack, err = op(stack)
		fmt.Printf("vm step %d stack=%v err=%v\n", i, stack, err)
	}
	_, err := binop("mul", nil)([]float64{1})
	fmt.Println("vm underflow:", err)

	// Closures over large captured structs and arrays (copied vs shared).
	type big struct {
		arr [32]int
		tag string
	}
	b := big{tag: "orig"}
	for i := range b.arr {
		b.arr[i] = i
	}
	byValue := func(bb big) func() int {
		return func() int { return bb.arr[31] + len(bb.tag) }
	}(b)
	byRef := func() int { return b.arr[31] + len(b.tag) }
	b.arr[31] = 1000
	b.tag = "changed!"
	fmt.Println("captured copy vs reference", byValue(), byRef())

	// Generator via closure state machine.
	gen := func() func() (int, bool) {
		state, a, bb := 0, 0, 1
		return func() (int, bool) {
			if state >= 15 {
				return 0, false
			}
			state++
			r := a
			a, bb = bb, a+bb
			return r, true
		}
	}()
	var fibs []int
	for v, ok := gen(); ok; v, ok = gen() {
		fibs = append(fibs, v)
	}
	fmt.Println("generator", fibs)

	// Immediately invoked and nested closures modifying outer variables at depth.
	depth := 0
	func() {
		depth++
		func() {
			depth++
			func() {
				depth *= 10
			}()
		}()
	}()
	fmt.Println("nested closure depth", depth)
}
