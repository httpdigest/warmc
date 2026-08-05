// `defer`, which TinyGo lowers into a per-frame record and a run-on-exit
// loop.  Common in real Go and in no other kernel here.
package main

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
	for r := 0; r < 24000000; r++ {
		total += withDefer(c, r&15)
	}
	println("total =", total, "n =", c.n)
}
