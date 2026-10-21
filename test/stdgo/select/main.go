// A select over two channels, one buffered and one not, fed by two goroutines.
//
// The runtime's selectgo is a different path from the plain send and receive
// that chans measures: it locks every channel in the select in address order,
// shuffles the cases into a random poll order, and, when nothing is ready,
// enqueues the goroutine on all of them at once and dequeues it from the rest
// when one fires.  A closed channel is set to nil so its case drops out, the
// usual way a select loop drains two producers.
//
// Deterministic although select picks at random among ready cases: every
// value is counted exactly once whichever case receives it first.
package main

import "fmt"

//go:noinline
func feed(n int, base uint64, ch chan<- uint64) {
	for i := 0; i < n; i++ {
		ch <- base + uint64(i)
	}
	close(ch)
}

func main() {
	var s uint64
	for r := 0; r < 400; r++ {
		a := make(chan uint64, 16)
		b := make(chan uint64)
		go feed(500, 0, a)
		go feed(500, 1<<20, b)
		for a != nil || b != nil {
			select {
			case v, ok := <-a:
				if !ok {
					a = nil
					continue
				}
				s += v
			case v, ok := <-b:
				if !ok {
					b = nil
					continue
				}
				s += v * 3
			}
		}
	}
	fmt.Println("s =", s)
}
