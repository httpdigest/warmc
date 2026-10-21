// A variadic call, the same source as test/gobench/variadic.  The argument slice
// does not escape `sum`, so Go builds it in the caller's frame rather than on
// the heap: each call stores its arguments into an array there and passes a
// slice header pointing at it.
package main

import "fmt"

//go:noinline
func sum(base int, rest ...int) int {
	s := base
	for _, v := range rest {
		s += v
	}
	return s
}

func main() {
	total := 0
	for r := 0; r < 30000000; r++ {
		total += sum(r&7, r&1, r&3, r&15)
		total += sum(r & 31)
	}
	fmt.Println("total =", total)
}
