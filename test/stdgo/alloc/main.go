// Allocation with something that survives it, the same source as
// test/gobench/alloc.
//
// The collector is Go's own, compiled into the module: a concurrent mark and
// sweep whose mark workers run as goroutines on the one thread wasm has, with a
// write barrier compiled into the mutator's pointer stores into the heap.  A large
// tree stays live the whole time so every cycle has to trace it, and three kinds
// of garbage churn on top of it: short-lived trees, a map rewritten in place,
// and slices grown from nothing.
package main

import "fmt"

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
	for r := 0; r < 130; r++ {
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
	fmt.Println("total =", total, "kept =", kept)
}
