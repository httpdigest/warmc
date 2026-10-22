// Closures and function values.  Since Go 1.22 each iteration of a
// three-clause `for` and of `range` has its own loop variable, so closures
// created in different iterations capture different variables -- while a
// variable declared outside the loop is shared by all of them.  Counters and
// generators keep state in captured variables, several closures share one,
// a closure recurses through a variable that holds it, method values bind
// their receiver at evaluation time (a copy for value receivers), and
// higher-order functions compose, curry, memoise and fold.  Range-over-func
// iterators (Go 1.23) are closures driven by the compiler-generated yield,
// including early exit from the loop body.  TinyGo lowers captures to a
// context pointer passed alongside the function pointer, so all of this is
// indirect calls with an extra hidden argument.
package main

import (
	"fmt"
	"iter"
	"os"
	"slices"
	"sort"
	"strings"
)

var base = 10

func init() {
	if len(os.Args) > 1000 {
		base = 11
	}
}

func counter(start int) (inc func() int, get func() int, reset func()) {
	n := start
	inc = func() int { n++; return n }
	get = func() int { return n }
	reset = func() { n = start }
	return
}

func fibGen() func() int {
	a, b := 0, 1
	return func() int {
		r := a
		a, b = b, a+b
		return r
	}
}

func compose[A, B, C any](f func(A) B, g func(B) C) func(A) C {
	return func(a A) C { return g(f(a)) }
}

func curry(f func(int, int) int) func(int) func(int) int {
	return func(a int) func(int) int {
		return func(b int) int { return f(a, b) }
	}
}

func memo(f func(int) int) (func(int) int, *int) {
	cache := map[int]int{}
	misses := 0
	return func(n int) int {
		if v, ok := cache[n]; ok {
			return v
		}
		misses++
		v := f(n)
		cache[n] = v
		return v
	}, &misses
}

func mapInts(xs []int, f func(int) int) []int {
	out := make([]int, len(xs))
	for i, x := range xs {
		out[i] = f(x)
	}
	return out
}

func filter(xs []int, keep func(int) bool) []int {
	var out []int
	for _, x := range xs {
		if keep(x) {
			out = append(out, x)
		}
	}
	return out
}

func fold[T, A any](xs []T, init A, f func(A, T) A) A {
	acc := init
	for _, x := range xs {
		acc = f(acc, x)
	}
	return acc
}

// An iterator over the Collatz sequence from n.
func collatz(n int) iter.Seq[int] {
	return func(yield func(int) bool) {
		for {
			if !yield(n) {
				return
			}
			if n == 1 {
				return
			}
			if n%2 == 0 {
				n /= 2
			} else {
				n = 3*n + 1
			}
		}
	}
}

func pairs[T any](xs []T) iter.Seq2[T, T] {
	return func(yield func(T, T) bool) {
		for i := 0; i+1 < len(xs); i++ {
			if !yield(xs[i], xs[i+1]) {
				return
			}
		}
	}
}

type acc struct{ total int }

func (a acc) value() int { return a.total }
func (a *acc) add(n int) { a.total += n }

func main() {
	fmt.Println("-- per-iteration loop variables")
	var fs []func() int
	for i := 0; i < 5; i++ {
		fs = append(fs, func() int { return i * base })
	}
	for _, s := range []string{"a", "bb", "ccc"} {
		fs = append(fs, func() int { return len(s) })
	}
	for i := range 3 {
		fs = append(fs, func() int { return -i })
	}
	shared := 0
	for k := 0; k < 3; k++ {
		shared = k
		fs = append(fs, func() int { return shared * 100 })
	}
	for _, f := range fs {
		fmt.Print(f(), " ")
	}
	fmt.Println()
	// The per-iteration copy is taken before the post statement, so a closure
	// that modifies i affects only its own iteration's variable.
	var incs []func() int
	for i := 0; i < 3; i++ {
		incs = append(incs, func() int { i += 10; return i })
	}
	for _, f := range incs {
		fmt.Print(f(), f(), " ")
	}
	fmt.Println()
	fmt.Println("-- counters and generators")
	inc, get, reset := counter(base)
	inc()
	inc()
	fmt.Println(get(), inc(), get())
	reset()
	fmt.Println(get())
	inc2, get2, _ := counter(0)
	inc2()
	fmt.Println(get(), get2())
	fib := fibGen()
	for i := 0; i < 20; i++ {
		fmt.Print(fib(), " ")
	}
	fmt.Println()
	fmt.Println("-- recursion through a variable")
	var fact func(int) int
	fact = func(n int) int {
		if n <= 1 {
			return 1
		}
		return n * fact(n-1)
	}
	var walk func(n, depth int) string
	walk = func(n, depth int) string {
		if n == 0 {
			return "."
		}
		return fmt.Sprintf("(%d%s%s)", depth, walk(n/2, depth+1), walk(n/3, depth+1))
	}
	fmt.Println(fact(10), fact(12), walk(7, 0))
	fmt.Println("-- higher order")
	xs := []int{5, 3, 8, 1, 9, 2, 7, base}
	sq := func(x int) int { return x * x }
	plus1 := func(x int) int { return x + 1 }
	f := compose(sq, plus1)
	g := compose(plus1, sq)
	h := compose(f, func(x int) string { return strings.Repeat("*", x%7) })
	fmt.Println(mapInts(xs, f), mapInts(xs, g), h(3), h(4))
	add := curry(func(a, b int) int { return a*10 + b })
	add3 := add(3)
	fmt.Println(add3(4), add(5)(6), mapInts(xs, add(1)))
	fmt.Println(filter(xs, func(x int) bool { return x%2 == 1 }),
		fold(xs, 0, func(a, x int) int { return a + x }),
		fold(xs, "", func(a string, x int) string { return a + fmt.Sprint(x) }))
	var slowFib func(int) int
	cached, misses := memo(func(n int) int { return slowFib(n) })
	slowFib = func(n int) int {
		if n < 2 {
			return n
		}
		return cached(n-1) + cached(n-2)
	}
	fmt.Println(cached(50), cached(80), *misses)
	fmt.Println("-- sort with captured state")
	words := strings.Fields("pear fig apple banana kiwi cherry date grape")
	calls := 0
	sort.Slice(words, func(i, j int) bool {
		calls++
		if len(words[i]) != len(words[j]) {
			return len(words[i]) < len(words[j])
		}
		return words[i] < words[j]
	})
	fmt.Println(words, calls > 0)
	fmt.Println("-- method values")
	a := acc{1}
	val := a.value // copies a now
	addTo := a.add // binds &a
	addTo(5)
	addTo(10)
	fmt.Println(val(), a.value(), a.total)
	methods := map[string]func(int){"add": a.add}
	methods["add"](100)
	fmt.Println(a.total)
	fmt.Println("-- iterators")
	for n := range collatz(27) {
		if n > 1000 {
			fmt.Println("first above 1000:", n)
			break
		}
	}
	steps := 0
	peak := 0
	for n := range collatz(97) {
		steps++
		peak = max(peak, n)
	}
	fmt.Println(steps, peak, slices.Collect(collatz(6)))
	for a, b := range pairs([]string{"x", "y", "z", "w"}) {
		if a == "z" {
			break
		}
		fmt.Print(a, b, " ")
	}
	fmt.Println()
	total := 0
outer:
	for x := range collatz(base) {
		for y := range collatz(x + 3) {
			if y == 1 {
				continue outer
			}
			if total > 500 {
				break outer
			}
			total += y
		}
	}
	fmt.Println("total", total)
	fmt.Println("-- closures in structs and maps")
	type op struct {
		name string
		f    func(a, b int) int
	}
	ops := []op{
		{"add", func(a, b int) int { return a + b }},
		{"sub", func(a, b int) int { return a - b }},
		{"mul", func(a, b int) int { return a * b }},
		{"pow", func(a, b int) int {
			r := 1
			for i := 0; i < b; i++ {
				r *= a
			}
			return r
		}},
	}
	for _, o := range ops {
		fmt.Print(o.name, "=", o.f(base, 3), " ")
	}
	fmt.Println()
	dispatch := map[string]func(...int) int{}
	for _, o := range ops {
		dispatch[o.name] = func(xs ...int) int {
			r := xs[0]
			for _, x := range xs[1:] {
				r = o.f(r, x)
			}
			return r
		}
	}
	keys := make([]string, 0)
	for k := range dispatch {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		fmt.Print(k, ":", dispatch[k](2, 3, 4), " ")
	}
	fmt.Println()
}
