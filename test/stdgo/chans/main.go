// Goroutines and a channel, which is the shape no other corpus in this tree
// contains at all.
//
// test/gobench is built `-scheduler=none`, so TinyGo's kernels have no
// concurrency in them and cannot: a channel send there is a compile error away
// from a deadlock.  Go's own runtime brings a real scheduler, and a channel
// send that blocks parks the goroutine and switches to another -- inside a
// single wasm instance, with no threads underneath it.  So this kernel measures
// warmc on the runtime's scheduling path: the atomics, the linked lists of
// waiting goroutines, and the stack switch itself.
//
// Deterministic despite the concurrency: one producer and one consumer over a
// fixed count, so the sum is fixed even though the interleaving is not.
package main

import "fmt"

//go:noinline
func pump(n int, ch chan uint64) {
	for i := 0; i < n; i++ {
		ch <- uint64(i)
	}
	close(ch)
}

func main() {
	var s uint64
	for r := 0; r < 300; r++ {
		// Unbuffered, so every value is a hand-off and every hand-off is a
		// scheduling decision.  A buffered channel would measure the buffer.
		ch := make(chan uint64)
		go pump(1000, ch)
		for v := range ch {
			s += v
		}
	}
	// And a buffered one, so the pair says what the hand-off costs against
	// what the queue costs.
	for r := 0; r < 300; r++ {
		ch := make(chan uint64, 64)
		go pump(1000, ch)
		for v := range ch {
			s += v * 2
		}
	}
	fmt.Println("s =", s)
}
