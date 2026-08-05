// A variadic call, which allocates a slice for the arguments at every call
// site unless the compiler can see through it.
package main

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
	println("total =", total)
}
