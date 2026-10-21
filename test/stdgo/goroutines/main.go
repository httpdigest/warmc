// Starting goroutines and waiting for them: `go`, a sync.WaitGroup and a
// sync.Mutex, none of which test/gobench can contain.
//
// chans next door measures goroutines that already exist handing values to
// each other.  This measures their lifecycle instead: every round starts 256,
// each takes a lock that nothing else holds at that moment, and the main
// goroutine parks until the last one is done.  So the cost is the runtime's
// newproc, a goroutine stack taken from its cache, the scheduler finding each
// one, and the atomics under an uncontended Lock -- all of it Go code compiled
// into the module, with no thread anywhere underneath.
package main

import (
	"fmt"
	"sync"
)

type tally struct {
	mu sync.Mutex
	n  uint64
}

//go:noinline
func work(i, r int, out []uint64, t *tally, wg *sync.WaitGroup) {
	defer wg.Done()
	x := uint64(i*7919+r)*0x9e3779b97f4a7c15 + 1
	for j := 0; j < 32; j++ {
		x ^= x >> 29
		x *= 0xbf58476d1ce4e5b9
	}
	out[i] = x
	t.mu.Lock()
	t.n += x >> 48
	t.mu.Unlock()
}

func main() {
	const n = 256
	out := make([]uint64, n)
	t := &tally{}
	var s uint64
	for r := 0; r < 1500; r++ {
		var wg sync.WaitGroup
		wg.Add(n)
		for i := 0; i < n; i++ {
			go work(i, r, out, t, &wg)
		}
		wg.Wait()
		for _, v := range out {
			s += v & 0xffff
		}
	}
	fmt.Println("s =", s, "n =", t.n)
}
