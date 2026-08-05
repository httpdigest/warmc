// Interface method dispatch, which is `call_indirect` in wasm and the one
// call shape a C benchmark never produces.  Three implementations behind one
// interface so the call site cannot be devirtualised to a single target.
package main

type shape interface{ area(int) int }

type sq struct{ k int }
type tri struct{ k int }
type rect struct{ w, h int }

func (s sq) area(x int) int   { return s.k * x * x }
func (t tri) area(x int) int  { return t.k * x * x / 2 }
func (r rect) area(x int) int { return r.w*x + r.h*x }

func main() {
	shapes := []shape{sq{1}, tri{3}, rect{2, 5}, sq{7}, rect{1, 1}, tri{2}, sq{4}}
	total := 0
	for r := 0; r < 60000000; r++ {
		total += shapes[r%len(shapes)].area(r & 15)
	}
	println("total =", total)
}
