package main

import "sort"

func fib(n int) int {
	if n < 2 {
		return n
	}
	a, b := 0, 1
	for i := 2; i <= n; i++ {
		a, b = b, a+b
	}
	return b
}

func main() {
	println("hello from tinygo")
	println("fib(30) =", fib(30))

	xs := []int{5, 3, 8, 1, 9, 2}
	sort.Ints(xs)
	sum := 0
	for _, x := range xs {
		sum += x
	}
	println("sorted head =", xs[0], "tail =", xs[len(xs)-1], "sum =", sum)

	m := map[string]int{"alpha": 1, "beta": 2, "gamma": 3}
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		println("map", k, m[k])
	}

	s := "reverse me"
	r := []byte(s)
	for i, j := 0, len(r)-1; i < j; i, j = i+1, j-1 {
		r[i], r[j] = r[j], r[i]
	}
	println("reversed:", string(r))

	// Interface dispatch is an indirect call through a table.
	shapes := []interface{ area() int }{rect{3, 4}, square{5}}
	total := 0
	for _, sh := range shapes {
		total += sh.area()
	}
	println("areas =", total)

	// A closure over a mutable local.
	counter := makeCounter()
	counter()
	counter()
	println("counted to", counter())

	// A deferred closure mutating a named result runs after the return
	// value is set and before the caller sees it.  (recover is off the
	// table: TinyGo's -scheduler=none makes panics fatal.)
	println("deferred:", deferred())

	// f64 arithmetic, printed exactly: 1.5^10 is 59049/1024, so scaling by
	// 1024 makes the conversion integer-exact and sidesteps the difference
	// between big Go's float formatter and TinyGo's.
	f := 1.0
	for i := 0; i < 10; i++ {
		f *= 1.5
	}
	println("1.5^10 * 1024 =", int(f*1024))
}

type rect struct{ w, h int }
type square struct{ s int }

func (r rect) area() int   { return r.w * r.h }
func (s square) area() int { return s.s * s.s }

func makeCounter() func() int {
	n := 0
	return func() int {
		n++
		return n
	}
}

func deferred() (n int) {
	defer func() { n += 40 }()
	n = 2
	return n
}
