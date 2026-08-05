// Closures over captured state, called indirectly.  A different indirect-call
// shape from an interface: the callee is a function value with an environment.
package main

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
	for r := 0; r < 30000; r++ {		k := r & 7
		add := func(x int) int { return x + k }
		mul := func(x int) int { return x * (k + 1) }
		total += apply(1024, add) + apply(1024, mul)
	}
	println("total =", total)
}
