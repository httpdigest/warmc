// Deep non-tail recursion, the same source as test/gobench/recurse.
//
// Every call pays Go's prologue: a compare of the stack pointer, a wasm global,
// against the goroutine's stack guard with a call to morestack beyond it, the
// frame stored on Go's stack in linear memory, and on return the flag that says
// whether the stack is unwinding.
package main

import "fmt"

//go:noinline
func fib(n int) int {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func main() {
	total := 0
	for r := 0; r < 30; r++ {
		total += fib(27 + r%3)
	}
	fmt.Println("total =", total)
}
