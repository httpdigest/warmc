// Deep non-tail recursion: every call pays a prologue, a stack-limit check
// and an epilogue, which no other kernel makes the dominant cost.
package main

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
	println("total =", total)
}
