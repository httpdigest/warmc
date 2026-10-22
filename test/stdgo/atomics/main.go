// sync/atomic: typed counters -- atomic.Int64, Uint64, Uint32 and Pointer --
// shared by sixteen goroutines, with a compare-and-swap loop for a running
// maximum and runtime.Gosched so the workers interleave.
//
// goroutines measures a Mutex; this is the lock-free side, and on wasm it is
// not what the name suggests.  Go's wasm port has no threads, so it uses no
// wasm atomic instructions at all.  atomic.Int64.Add inlines to a call to
// sync/atomic.AddInt64, a stub whose assembly is a jump, which on wasm becomes
// a second call, to internal/runtime/atomic.Xadd64: a plain i64.load, i64.add
// and i64.store.  So one atomic add is two calls through Go's stack in linear
// memory, and that call overhead, with the goroutine yields, is what this
// measures.  Every update commutes, so the totals do not depend on the order
// the scheduler runs the workers in.
package main

import (
	"fmt"
	"runtime"
	"sync"
	"sync/atomic"
)

type stats struct {
	hits  atomic.Int64
	bytes atomic.Uint64
	max   atomic.Uint64
	flags atomic.Uint32
	last  atomic.Pointer[result]
}

type result struct{ id, v uint64 }

// Raise s.max to v with a compare-and-swap loop.
func (s *stats) raise(v uint64) {
	for {
		old := s.max.Load()
		if v <= old || s.max.CompareAndSwap(old, v) {
			return
		}
	}
}

//go:noinline
func worker(id int, s *stats, wg *sync.WaitGroup) {
	defer wg.Done()
	x := uint64(id)*0x9e3779b97f4a7c15 + 1
	for j := 0; j < 2000; j++ {
		x ^= x << 13
		x ^= x >> 7
		x ^= x << 17
		s.hits.Add(1)
		s.bytes.Add(x & 0xfff)
		s.raise(x >> 16)
		s.flags.Or(1 << (x & 31))
		if j&255 == 0 {
			// Yield, so the workers interleave and every CAS loop can see a
			// value another goroutine stored.
			runtime.Gosched()
		}
	}
	s.last.Store(&result{uint64(id), x})
}

func main() {
	var acc uint64
	for r := 0; r < 220; r++ {
		var s stats
		var wg sync.WaitGroup
		const workers = 16
		wg.Add(workers)
		for i := 0; i < workers; i++ {
			go worker(r*workers+i, &s, &wg)
		}
		wg.Wait()
		// Which worker stored last depends on scheduling, so only the fact
		// that one did goes into the checksum.
		if s.last.Load() == nil {
			panic("no result")
		}
		acc = acc*31 + uint64(s.hits.Load()) + s.bytes.Load() + s.max.Load() +
			uint64(s.flags.Load())
	}
	fmt.Println("acc =", acc)
}
