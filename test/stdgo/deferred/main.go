// `defer`, the same source as test/gobench/deferred.
//
// TinyGo keeps a record per deferred call and runs a loop over them on exit.
// Go's compiler open-codes these instead, because the function has no loop:
// each defer sets a bit, and every return calls the deferred function values
// inline under those bits, so the only runtime call left is deferreturn on the
// panic path.
package main

import "fmt"

type counter struct{ n int }

//go:noinline
func (c *counter) add(k int) { c.n += k }

//go:noinline
func withDefer(c *counter, k int) int {
	defer c.add(k)
	defer c.add(1)
	if k&7 == 0 {
		return c.n
	}
	return c.n + 1
}

func main() {
	c := &counter{}
	total := 0
	for r := 0; r < 8000000; r++ {
		total += withDefer(c, r&15)
	}
	fmt.Println("total =", total, "n =", c.n)
}
