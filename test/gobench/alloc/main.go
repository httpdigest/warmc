// Allocation with something that survives it.
//
// The collector here is TinyGo's, running inside the guest's own memory --
// warmc's never runs for TinyGo output.  So what this times is how well each
// engine compiles somebody else's allocator and mark loop: a pointer-chasing
// traversal over a large live heap, which is a different kind of code from
// the arithmetic the rest of the corpus is made of.
//
// `bintree` next door builds a tree and drops it, so almost nothing is live
// when a collection runs and what it costs is allocation throughput.  This
// keeps a large tree alive across all the churn instead, so every collection
// has to trace it -- and puts three kinds of garbage on top, because a real
// program does not allocate one shape: short-lived trees, a map churned in
// place, and slices grown from nothing.
package main

type node struct {
	left, right *node
	val         int
}

//go:noinline
func build(depth, v int) *node {
	if depth == 0 {
		return &node{val: v}
	}
	return &node{left: build(depth-1, v*2), right: build(depth-1, v*2+1), val: v}
}

//go:noinline
func walk(n *node) int {
	if n == nil {
		return 0
	}
	return n.val + walk(n.left) + walk(n.right)
}

func main() {
	// The survivor: built once and walked every round, so it is live for the
	// whole run and every collection has to trace it.
	keep := build(16, 1)
	kept := walk(keep) & 0xffff

	total := 0
	m := map[int][]byte{}
	for r := 0; r < 400; r++ {
		t := build(11, r)
		total += walk(t) & 0xffff

		for i := 0; i < 500; i++ {
			m[(r*500+i)%1500] = make([]byte, i%48+1)
			if i%3 == 0 {
				delete(m, (i*7)%1500)
			}
		}

		s := make([]int, 0, 1)
		for j := 0; j < 400; j++ {
			s = append(s, j*r)
		}
		total += len(s) + len(m)

		// Still there, still the same.
		total += walk(keep) & 0xff
	}
	println("total =", total, "kept =", kept)
}
