// Closures over captured state, called indirectly; the same source as
// test/gobench/closures.  The callee is a function value: Go loads the code
// pointer out of the closure, passes the closure itself in a global, and makes
// a call_indirect on the pointer's upper bits, which on wasm are the function's
// table index.
package main

import "fmt"

//go:noinline
func apply(n int, f func(int) int) int {
	s := 0
	for i := 0; i < n; i++ {
		s += f(i)
	}
	return s
}

func main() {
	total := 0
	for r := 0; r < 15000; r++ {
		k := r & 7
		add := func(x int) int { return x + k }
		mul := func(x int) int { return x * (k + 1) }
		total += apply(1024, add) + apply(1024, mul)
	}
	fmt.Println("total =", total)
}
